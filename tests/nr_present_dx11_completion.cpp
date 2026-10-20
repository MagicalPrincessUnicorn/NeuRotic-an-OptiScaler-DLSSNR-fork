// A completed final target copy must permit handoff; preceding D3D12 completion
// and Flush must not permit handoff while that copy is still pending.
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <array>
#ifdef NR_BASELINE_DX11_COPY
#include "baseline-copy-owner.h"
#else
#include <dlssnr/PresentDx11CopyCompletion.h>
#endif
using Microsoft::WRL::ComPtr;
static unsigned checks=0, failures=0;
static void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}else std::printf("PASS %s\n",name);}
static void Hr(HRESULT value){if(FAILED(value))throw std::runtime_error("HRESULT "+std::to_string(static_cast<unsigned long>(value)));}
struct Device {
    ComPtr<ID3D11Device5> device;
    ComPtr<ID3D11DeviceContext4> context;
    Device(){ComPtr<ID3D11Device> base;ComPtr<ID3D11DeviceContext> immediate;
        Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&base,nullptr,&immediate));
        Hr(base.As(&device));Hr(immediate.As(&context));}
    ComPtr<ID3D11Texture2D> Texture(const std::array<unsigned,4>& pixels){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=2;desc.Height=2;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),8,16};ComPtr<ID3D11Texture2D> result;Hr(device->CreateTexture2D(&desc,&data,&result));return result;}
    void Complete(){ComPtr<ID3D11Fence> fence;Hr(device->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        Hr(context->Signal(fence.Get(),1));context->Flush();HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event)throw std::runtime_error("event allocation");Hr(fence->SetEventOnCompletion(1,event));
        const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);if(waited!=WAIT_OBJECT_0)throw std::runtime_error("native fence timeout");}
    bool Equals(ID3D11Texture2D* texture,const std::array<unsigned,4>& want){
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Hr(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),texture);Complete();
        D3D11_MAPPED_SUBRESOURCE mapped{};Hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));bool equal=true;
        for(unsigned y=0;y<2;++y){auto row=reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);
            for(unsigned x=0;x<2;++x)equal=equal&&row[x]==want[y*2+x];}context->Unmap(staging.Get(),0);return equal;}
};
struct Gate {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;ComPtr<ID3D11Fence> observed;
    explicit Gate(Device& d){ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));Hr(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)));
        HANDLE shared=nullptr;Hr(device->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&shared));
        const auto opened=d.device->OpenSharedFence(shared,IID_PPV_ARGS(&observed));CloseHandle(shared);Hr(opened);}
    void Block(Device& d,UINT64 value){Hr(d.context->Wait(observed.Get(),value));}
    void Release(UINT64 value){Hr(fence->Signal(value));}
};
int main(){try{
    Device d;Gate gate(d);const std::array<unsigned,4> original{0xff010203,0xff040506,0xff070809,0xff0a0b0c};
    const std::array<unsigned,4> changed{0xffa01234,0xffbb5678,0xffcc9abc,0xffddeeff};
    auto target=d.Texture(original),source=d.Texture(changed);
#ifdef NR_BASELINE_DX11_COPY
    auto canYield=[] {std::string reason;return BaselineDx11Copy::CanYieldPresentOutput(reason);};
    auto copy=[&](ID3D11DeviceContext4* context,ID3D11Resource* destination,ID3D11Resource* input){
        BaselineDx11Copy::Copy(context,static_cast<ID3D11Texture2D*>(destination),static_cast<ID3D11Texture2D*>(input));return true;};
#else
    DlssNr::PresentDx11CopyCompletion completion;
    auto canYield=[&] {return completion.CanYield();};
    auto copy=[&](ID3D11DeviceContext4* context,ID3D11Resource* destination,ID3D11Resource* input){return completion.Copy(context,destination,input);};
#endif
    Check(canYield(),"unused D3D11 owner is yieldable");
    gate.Block(d,1);Check(copy(d.context.Get(),target.Get(),source.Get()),"final target copy admitted with its own proof");
    Check(gate.fence->GetCompletedValue()==0,"native D3D11 work is independently blocked");
    Check(!canYield(),"Flush and completed preceding D3D12 work do not prove final copy");
#ifndef NR_BASELINE_DX11_COPY
    Check(!completion.ResetCompleted(),"pending proof and private source survive resource reset request");
#endif
    gate.Release(1);d.Complete();
    Check(d.Equals(target.Get(),changed),"known chromatic edit reaches actual D3D11 target");
    Check(d.Equals(source.Get(),changed),"private final source remains unchanged");
    Check(canYield(),"genuine completed final D3D11 copy permits handoff without later Present");
#ifndef NR_BASELINE_DX11_COPY
    Check(completion.ResetCompleted()&&completion.CanYield(),"completed proof permits reset and fresh restart");
    Device other;auto foreign=other.Texture(original);ComPtr<ID3D11DeviceContext> deferredBase;ComPtr<ID3D11DeviceContext4> deferred;
    Hr(d.device->CreateDeferredContext(0,&deferredBase));Hr(deferredBase.As(&deferred));
    Check(!copy(deferred.Get(),target.Get(),source.Get()),"deferred context cannot publish immediate final-copy proof");
    Check(!copy(other.context.Get(),target.Get(),source.Get()),"wrong context device rejected before target modification");
    Check(!copy(d.context.Get(),foreign.Get(),source.Get()),"foreign target device rejected before target modification");
    Check(!copy(nullptr,target.Get(),source.Get())&&!copy(d.context.Get(),nullptr,source.Get()),"missing context or target rejected without a write");
    Check(completion.CanYield(),"pre-write refusals do not invent pending output");
    Check(d.Equals(target.Get(),changed)&&other.Equals(foreign.Get(),original),"refused target copies preserve both devices' textures");
    gate.Block(d,2);Check(copy(d.context.Get(),target.Get(),source.Get()),"second final-copy epoch admitted");
    Check(copy(d.context.Get(),target.Get(),source.Get()),"same private source uses advancing fence values");
    auto replacement=d.Texture(original);
    Check(!copy(d.context.Get(),target.Get(),replacement.Get()),"pending source lifetime cannot be replaced");
    Check(!completion.CanYield()&&!completion.ResetCompleted(),"latest submitted final copy blocks yield and reset");
    gate.Release(2);d.Complete();Check(completion.CanYield(),"all consecutive final copies are covered by latest proof");
    Check(copy(d.context.Get(),target.Get(),replacement.Get()),"completed source may be replaced safely");d.Complete();
    Check(d.Equals(target.Get(),original)&&completion.CanYield(),"replacement copy and its final completion are genuine");
    Check(!DlssNr::PresentDx11CopyCompletion::FenceComplete(UINT64_MAX,1),"device-loss sentinel cannot authorize reuse");
    Check(!DlssNr::PresentDx11CopyCompletion::FenceComplete(0,1)&&DlssNr::PresentDx11CopyCompletion::FenceComplete(1,1),"only observed submitted final fence authorizes completion");
#endif
    std::printf("RESULT checks=%u failures=%u adapter=WARP no_model=true\n",checks,failures);return failures?1:0;
}catch(const std::exception& error){std::printf("FIXTURE ERROR %s\n",error.what());return 2;}}
