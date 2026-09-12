#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/DlssNr_Dx11Transport.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentGuides.h"
#include "../OptiScaler/dlssnr/NrNativeDx11OutputContract.h"
#include <d3d11sdklayers.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>
#include <DirectXPackedVector.h>

#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ExitProcess(1); } } while (false)

namespace T = DlssNr::Dx11Transport;
namespace G = DlssNr::PresentGuides;
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) std::printf("HRESULT %08x\n", unsigned(hr)); assert(SUCCEEDED(hr)); }
static void Wait(ID3D12Fence* fence, UINT64 value)
{
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(event);
    Check(fence->SetEventOnCompletion(value, event));
    assert(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0); CloseHandle(event);
}
static void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &b);
}
int main(int argc, char** argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    using OutputResult = DlssNr::NativeDx11::OutputContractResult;
    using DlssNr::NativeDx11::ValidateOutputContract;
    assert(ValidateOutputContract(true,0,0,true,true,2560,1440,3840,2160)==OutputResult::Accepted);
    assert(ValidateOutputContract(false,0,0,true,true,2560,1440,3840,2160)==
        OutputResult::PresentTargetMismatch);
    assert(ValidateOutputContract(false,0,0,true,true,3840,2160,3840,2160)==OutputResult::Accepted);
    assert(ValidateOutputContract(true,1,0,true,true,2560,1440,3840,2160)==
        OutputResult::PartialOrUnsupported);
    assert(ValidateOutputContract(true,0,0,false,true,2560,1440,3840,2160)==
        OutputResult::PartialOrUnsupported);
    assert(ValidateOutputContract(true,0,0,true,false,2560,1440,3840,2160)==
        OutputResult::PartialOrUnsupported);
    const bool hardware = argc > 1 && std::strcmp(argv[1], "--hardware") == 0;
    ComPtr<ID3D12Debug> debug12;
    const bool debug = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug12)));
    if (debug) debug12->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;
    if (!hardware) Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    else Check(factory->EnumAdapters(0, &adapter));
    DXGI_ADAPTER_DESC ad {}; Check(adapter->GetDesc(&ad));
    std::printf("Native DX11 transport adapter: %ls; D3D12 debug=%d\n", ad.Description, debug);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    UINT flags = debug ? D3D11_CREATE_DEVICE_DEBUG : 0;
    HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr) && flags)
    {
        std::puts("SKIP D3D11 debug layer: not installed"); flags = 0;
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &device, &level, &context);
    }
    Check(hr);
    ComPtr<ID3D11Device5> d11; Check(device.As(&d11));
    ComPtr<ID3D11DeviceContext4> c11; Check(context.As(&c11));
    ComPtr<ID3D12Device> d12; Check(D3D12CreateDevice(adapter.Get(), level, IID_PPV_ARGS(&d12)));
    assert(T::SameAdapter(d11.Get(), d12.Get()));
    T::Converter converter; std::string reason; assert(converter.Initialize(d11.Get(), reason));
    ComPtr<ID3DBlob> sentinelCode;
    const char sentinelSource[]="[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID) {}";
    Check(D3DCompile(sentinelSource,sizeof(sentinelSource)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&sentinelCode,nullptr));
    ComPtr<ID3D11ComputeShader> sentinelShader;
    Check(d11->CreateComputeShader(sentinelCode->GetBufferPointer(),sentinelCode->GetBufferSize(),nullptr,&sentinelShader));
    ComPtr<ID3D12CommandQueue> queue, other;
    D3D12_COMMAND_QUEUE_DESC qd {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    Check(d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&other)));
    ComPtr<ID3D11Fence> f11; Check(d11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f11)));
    HANDLE shared = nullptr; Check(f11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared));
    ComPtr<ID3D12Fence> f12; Check(d12->OpenSharedHandle(shared, IID_PPV_ARGS(&f12))); CloseHandle(shared);
    ComPtr<ID3D12Fence> done; Check(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&done)));
    UINT64 frameId = 0;
    D3D11_TEXTURE2D_DESC paddedDepth {}; paddedDepth.Width=4096; paddedDepth.Height=2160;
    paddedDepth.MipLevels=1; paddedDepth.ArraySize=1; paddedDepth.SampleDesc.Count=1;
    paddedDepth.Format=DXGI_FORMAT_R32_TYPELESS;
    auto paddedMotion=paddedDepth; paddedMotion.Format=DXGI_FORMAT_R32G32_FLOAT;
    assert(T::SlotCount*(T::Texture::RequiredBytes(paddedDepth,true)+
        T::Texture::RequiredBytes(paddedMotion,false)) <= T::BudgetBytes);
    auto texture11 = [&](UINT w, UINT h, DXGI_FORMAT format, const void* data, UINT pitch, UINT requestedBind=0u)
    {
        D3D11_TEXTURE2D_DESC desc {}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=format; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
        if(requestedBind) desc.BindFlags=requestedBind;
        else if(format==DXGI_FORMAT_D16_UNORM || format==DXGI_FORMAT_D24_UNORM_S8_UINT ||
           format==DXGI_FORMAT_D32_FLOAT || format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT) desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        D3D11_SUBRESOURCE_DATA init {data, pitch, 0};
        ComPtr<ID3D11Texture2D> r; const auto createHr=d11->CreateTexture2D(&desc, data ? &init : nullptr, &r);
        if(FAILED(createHr)) std::printf("CreateTexture2D failed format=%u bind=0x%x data=%d\n",format,desc.BindFlags,data!=nullptr);
        Check(createHr); return r;
    };
    auto checkPixels = [&](ID3D11Texture2D* texture, const std::vector<float>& expected, UINT components, float tolerance)
    {
        D3D11_TEXTURE2D_DESC d {}; texture->GetDesc(&d); d.Usage=D3D11_USAGE_STAGING;
        d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; const auto stagingHr=d11->CreateTexture2D(&d, nullptr, &staging);
        if(FAILED(stagingHr)) std::printf("Staging CreateTexture2D failed format=%u bind=0x%x misc=0x%x cpu=0x%x\n",d.Format,d.BindFlags,d.MiscFlags,d.CPUAccessFlags);
        Check(stagingHr);
        c11->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE map {}; const auto mapHr=c11->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
        if(FAILED(mapHr)) std::puts("Staging Map failed"); Check(mapHr);
        for(UINT y=0;y<d.Height;++y) for(UINT x=0;x<d.Width*components;++x)
        {
            const auto* row = static_cast<const char*>(map.pData)+y*map.RowPitch;
            const bool half = d.Format==DXGI_FORMAT_R16G16_FLOAT || d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
            const float actual = half ? DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const unsigned short*>(row)[x]) :
                reinterpret_cast<const float*>(row)[x];
            if(std::abs(actual-expected[y*d.Width*components+x]) > tolerance)
                std::fprintf(stderr,"PIXEL format=%u x=%u y=%u component=%u actual=%g expected=%g tolerance=%g\n",
                    d.Format,x/components,y,x%components,actual,expected[y*d.Width*components+x],tolerance);
            assert(std::abs(actual-expected[y*d.Width*components+x]) <= tolerance);
        }
        c11->Unmap(staging.Get(),0);
    };
    auto checkRaw32 = [&](ID3D11Texture2D* texture, const std::vector<UINT>& expected)
    {
        D3D11_TEXTURE2D_DESC d {}; texture->GetDesc(&d); d.Usage=D3D11_USAGE_STAGING;
        d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; Check(d11->CreateTexture2D(&d,nullptr,&staging));
        c11->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE map {}; Check(c11->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        for(UINT y=0;y<d.Height;++y)
        {
            const auto* row=reinterpret_cast<const UINT*>(static_cast<const char*>(map.pData)+y*map.RowPitch);
            for(UINT x=0;x<d.Width;++x) assert(row[x]==expected[y*d.Width+x]);
        }
        c11->Unmap(staging.Get(),0);
    };
    // Native Temporal Post-SR uses a private, exact-format output carrier and returns it only
    // after the D3D12 queue signals the shared fence.
    {
        const UINT w=17,h=9;
        std::vector<float> values(w*h*4);
        std::vector<unsigned short> half(values.size());
        for(UINT i=0;i<values.size();++i) { values[i]=float(i%61)/64.0f; half[i]=DirectX::PackedVector::XMConvertFloatToHalf(values[i]); }
        auto source=texture11(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,nullptr,0,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        c11->UpdateSubresource(source.Get(),0,nullptr,half.data(),w*8,0);
        auto destination=texture11(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,nullptr,0,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        D3D11_TEXTURE2D_DESC desc {}; source->GetDesc(&desc);
        T::Output output; assert(output.Prepare(d11.Get(),d12.Get(),desc,reason));
        assert(output.bytes==UINT64(w)*h*8 && output.Matches(desc));
        c11->CopyResource(output.shared.Get(),source.Get());
        const UINT64 ready=2; Check(c11->Signal(f11.Get(),ready)); c11->Flush(); Check(queue->Wait(f12.Get(),ready));
        Check(queue->Signal(f12.Get(),ready+1)); Check(c11->Wait(f11.Get(),ready+1));
        c11->CopyResource(destination.Get(),output.shared.Get()); c11->Flush();
        checkPixels(destination.Get(),values,4,0);
        frameId=2; // keep the shared timeline monotonic for the matrix below
    }
    // Native Temporal Pre-SR accepts a non-UAV game colour texture, crops its independent
    // input subrect into a private SRV/UAV carrier, and leaves the entire original untouched.
    {
        const UINT sourceW=23,sourceH=13,x0=3,y0=2,w=17,h=9;
        std::vector<float> sourceValues(sourceW*sourceH*4);
        std::vector<unsigned short> sourceHalf(sourceValues.size());
        for(UINT i=0;i<sourceValues.size();++i)
        {
            sourceValues[i]=float((i*7)%251)/256.0f;
            sourceHalf[i]=DirectX::PackedVector::XMConvertFloatToHalf(sourceValues[i]);
            sourceValues[i]=DirectX::PackedVector::XMConvertHalfToFloat(sourceHalf[i]);
        }
        auto source=texture11(sourceW,sourceH,DXGI_FORMAT_R16G16B16A16_FLOAT,sourceHalf.data(),sourceW*8,
            D3D11_BIND_SHADER_RESOURCE);
        D3D11_TEXTURE2D_DESC carrierDesc {}; source->GetDesc(&carrierDesc);
        carrierDesc.Width=w; carrierDesc.Height=h;
        carrierDesc.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;
        carrierDesc.MiscFlags=0;
        T::Output carrier; assert(carrier.Prepare(d11.Get(),d12.Get(),carrierDesc,reason));
        assert(carrier.bytes==UINT64(w)*h*8 && carrier.Matches(carrierDesc));
        D3D11_BOX box {x0,y0,0,x0+w,y0+h,1};
        c11->CopySubresourceRegion(carrier.shared.Get(),0,0,0,0,source.Get(),0,&box);
        const UINT64 ready=4; Check(c11->Signal(f11.Get(),ready)); c11->Flush(); Check(queue->Wait(f12.Get(),ready));
        Check(queue->Signal(f12.Get(),ready+1)); Check(c11->Wait(f11.Get(),ready+1));
        std::vector<float> cropped(w*h*4);
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) for(UINT c=0;c<4;++c)
            cropped[(y*w+x)*4+c]=sourceValues[((y+y0)*sourceW+(x+x0))*4+c];
        checkPixels(carrier.shared.Get(),cropped,4,0);
        checkPixels(source.Get(),sourceValues,4,0);
        frameId=4;
    }
    // BG3 uses packed R11G11B10_FLOAT for its native DLSS output. The exact-format
    // carrier must preserve every packed bit; no colour reinterpretation or conversion occurs.
    {
        const UINT w=19,h=11;
        std::vector<UINT> values(w*h);
        for(UINT i=0;i<values.size();++i)
            values[i]=((i*37u)&0x7ffu)|(((i*73u)&0x7ffu)<<11)|(((i*29u)&0x3ffu)<<22);
        auto source=texture11(w,h,DXGI_FORMAT_R11G11B10_FLOAT,values.data(),w*4,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        auto destination=texture11(w,h,DXGI_FORMAT_R11G11B10_FLOAT,nullptr,0,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        D3D11_TEXTURE2D_DESC desc {}; source->GetDesc(&desc);
        T::Output output; assert(output.Prepare(d11.Get(),d12.Get(),desc,reason));
        assert(output.bytes==UINT64(w)*h*4 && output.Matches(desc));
        c11->CopyResource(output.shared.Get(),source.Get());
        const UINT64 ready=6; Check(c11->Signal(f11.Get(),ready)); c11->Flush(); Check(queue->Wait(f12.Get(),ready));
        Check(queue->Signal(f12.Get(),ready+1)); Check(c11->Wait(f11.Get(),ready+1));
        c11->CopyResource(destination.Get(),output.shared.Get()); c11->Flush();
        checkRaw32(destination.Get(),values);
        frameId=6;
    }
    // Every texel, including the margins outside the render rectangle, crosses the production converter.
    for (UINT w : {17u,33u,65u}) for (DXGI_FORMAT motionFormat : {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32G32_FLOAT})
    for (DXGI_FORMAT depthFormat : {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS})
    {
        const UINT h=w/2+1, mw=82-w, mh=35;
        std::vector<float> depthValues(w*h), motionValues(mw*mh*2);
        std::vector<unsigned short> depth16(w*h);
        std::vector<UINT> depth24(w*h), depth64(w*h*2);
        for(UINT i=0;i<w*h;++i)
        {
            depthValues[i]=float(i%251)/256.0f;
            depth16[i]=static_cast<unsigned short>(std::lround(depthValues[i]*65535));
            depth24[i]=static_cast<UINT>(std::lround(depthValues[i]*16777215)) | 0x5a000000;
            std::memcpy(&depth64[i*2],&depthValues[i],sizeof(float)); depth64[i*2+1]=0x5a;
        }
        for(UINT i=0;i<motionValues.size();++i) motionValues[i]=float(int(i%97)-48)/64.0f;
        std::vector<float> expandedMotion(mw*mh*4,0);
        for(UINT i=0;i<mw*mh;++i) { expandedMotion[i*4]=motionValues[i*2]; expandedMotion[i*4+1]=motionValues[i*2+1]; }
        const bool is16=depthFormat==DXGI_FORMAT_R16_UNORM || depthFormat==DXGI_FORMAT_D16_UNORM ||
            depthFormat==DXGI_FORMAT_R16_TYPELESS || depthFormat==DXGI_FORMAT_R16_FLOAT;
        if(depthFormat==DXGI_FORMAT_R16_FLOAT) for(UINT i=0;i<w*h;++i)
            depth16[i]=DirectX::PackedVector::XMConvertFloatToHalf(depthValues[i]);
        const bool is24=depthFormat==DXGI_FORMAT_D24_UNORM_S8_UINT || depthFormat==DXGI_FORMAT_R24G8_TYPELESS;
        const bool is64=depthFormat==DXGI_FORMAT_D32_FLOAT_S8X24_UINT || depthFormat==DXGI_FORMAT_R32G8X24_TYPELESS;
        const void* pixels=is16 ? static_cast<void*>(depth16.data()) : is24 ? static_cast<void*>(depth24.data()) :
            is64 ? static_cast<void*>(depth64.data()) : static_cast<void*>(depthValues.data());
        const UINT depthBind=depthFormat==DXGI_FORMAT_R32_FLOAT ? D3D11_BIND_SHADER_RESOURCE : 0;
        auto depth=texture11(w,h,depthFormat,pixels,w*(is16?2:is64?8:4),depthBind);
        std::vector<unsigned short> motion16(motionValues.size());
        for(UINT i=0;i<motionValues.size();++i) motion16[i]=DirectX::PackedVector::XMConvertFloatToHalf(motionValues[i]);
        const bool halfMotion=motionFormat==DXGI_FORMAT_R16G16_FLOAT;
        const UINT motionBind=halfMotion ? 0 : D3D11_BIND_SHADER_RESOURCE;
        auto motion=texture11(mw,mh,motionFormat,halfMotion ? static_cast<void*>(motion16.data()) :
            static_cast<void*>(motionValues.data()),mw*(halfMotion?4:8),motionBind);
        D3D11_TEXTURE2D_DESC dd {},md {}; depth->GetDesc(&dd); motion->GetDesc(&md);
        T::Texture td,tm; assert(td.Prepare(d11.Get(),d12.Get(),dd,true,reason,depth.Get()));
        const bool motionReady=tm.Prepare(d11.Get(),d12.Get(),md,false,reason,motion.Get());
        if(!motionReady)
        {
            std::printf("Motion sharing: %s\n",reason.c_str());
            ComPtr<ID3D11InfoQueue> info;
            if(SUCCEEDED(d11.As(&info))) for(UINT64 i=0;i<info->GetNumStoredMessages();++i)
            {
                SIZE_T n=0; info->GetMessage(i,nullptr,&n); std::vector<char> bytes(n);
                auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); info->GetMessage(i,message,&n);
                std::printf("D3D11: %s\n",message->pDescription);
            }
            ComPtr<ID3D12InfoQueue> info12;
            if(SUCCEEDED(d12.As(&info12))) for(UINT64 i=0;i<info12->GetNumStoredMessages();++i)
            {
                SIZE_T n=0; info12->GetMessage(i,nullptr,&n); std::vector<char> bytes(n);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); info12->GetMessage(i,message,&n);
                std::printf("D3D12: %s\n",message->pDescription);
            }
        }
        assert(motionReady);
        assert((halfMotion && tm.sourceCarrier) ||
            (!halfMotion && tm.directSource && !tm.sourceCarrier && !tm.srv));
        assert(tm.bytes==UINT64(mw)*mh*(halfMotion?12:16));
        auto* allocation=td.shared.Get(); assert(td.Prepare(d11.Get(),d12.Get(),dd,true,reason));
        assert(td.shared.Get()==allocation);
        // Converter binds a different compute shader; the original shader and other state must survive.
        D3D11_BUFFER_DESC bd {}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> sentinel; Check(d11->CreateBuffer(&bd,nullptr,&sentinel));
        auto* sentinelRaw=sentinel.Get(); c11->CSSetConstantBuffers(3,1,&sentinelRaw);
        c11->CSSetShader(sentinelShader.Get(),nullptr,0);
        assert(converter.Copy(c11.Get(),td,depth.Get(),tm,motion.Get(),reason));
        ComPtr<ID3D11Buffer> restored; c11->CSGetConstantBuffers(3,1,&restored); assert(restored==sentinel);
        ComPtr<ID3D11ComputeShader> restoredShader; c11->CSGetShader(&restoredShader,nullptr,nullptr);
        assert(restoredShader==sentinelShader);
        checkPixels(td.shared.Get(),depthValues,1,is16?1.0f/65535:1.0e-6f);
        checkPixels(tm.shared.Get(),expandedMotion,4,0);
        checkPixels(motion.Get(),motionValues,2,0); // original guide unchanged
        const UINT64 value=(++frameId)*2;
        Check(c11->Signal(f11.Get(),value)); c11->Flush(); Check(queue->Wait(f12.Get(),value));
        ComPtr<ID3D12CommandAllocator> pa,ca;
        Check(d12->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&pa)));
        Check(d12->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&ca)));
        ComPtr<ID3D12GraphicsCommandList> producer,consumer;
        Check(d12->CreateCommandList(0,qd.Type,pa.Get(),nullptr,IID_PPV_ARGS(&producer)));
        Check(d12->CreateCommandList(0,qd.Type,ca.Get(),nullptr,IID_PPV_ARGS(&consumer)));
        auto proof=std::make_shared<G::Dx11Producer>(); proof->ready=f12; proof->orderedQueue=queue;
        proof->sourceDevice=d11; proof->value=value; proof->feature=1; proof->evaluation=value;
        G::Bridge guides; guides.Enable(true);
        DlssNrFrameInfo frame {}; frame.RenderSubrectWidth=8; frame.RenderSubrectHeight=4;
        frame.DepthSubrectWidth=8; frame.DepthSubrectHeight=4;
        frame.MotionSubrectWidth=mw-3; frame.MotionSubrectHeight=mh;
        frame.DepthSubrectX=2; frame.MotionSubrectX=3; frame.MvScaleX=-1; frame.JitterX=0.25f;
        auto capture=[&] { guides.Capture(producer.Get(),td.resource12.Get(),tm.resource12.Get(),frame,
            d11.Get(),0,128,72,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON,true,nullptr,proof); };
        capture(); auto selection=guides.BeginPresent(); G::Inputs inputs;
        assert(!guides.MatchMetadata(selection,queue.Get(),d11.Get(),0,128,72));
        Check(producer->Close()); ID3D12CommandList* lists[]={producer.Get()}; queue->ExecuteCommandLists(1,lists);
        assert(guides.MatchMetadata(selection,queue.Get(),d11.Get(),0,128,72));
        assert(!guides.MatchMetadata(selection,other.Get(),d11.Get(),0,128,72));
        assert(!guides.MatchMetadata(selection,queue.Get(),d11.Get(),1,128,72));
        assert(!guides.MatchMetadata(selection,queue.Get(),d12.Get(),0,128,72));
        assert(guides.Bind(selection,consumer.Get(),queue.Get(),d11.Get(),0,128,72,inputs));
        assert(inputs.frame.MotionSubrectX==3 && inputs.frame.MotionSubrectWidth==mw-3 &&
            inputs.frame.MotionSubrectHeight==mh && inputs.frame.JitterX==0.25f);
        // Production capture -> separate DX12 consumer -> private shared DX11 destination, all pixels.
        T::Texture output; D3D11_TEXTURE2D_DESC od=md;
        assert(output.Prepare(d11.Get(),d12.Get(),od,false,reason));
        Barrier(consumer.Get(),inputs.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(consumer.Get(),output.resource12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        consumer->CopyResource(output.resource12.Get(),inputs.motion.Get());
        Barrier(consumer.Get(),output.resource12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        Barrier(consumer.Get(),inputs.motion.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Check(consumer->Close()); lists[0]=consumer.Get(); queue->ExecuteCommandLists(1,lists);
        // Signal the actual queue, never CPU-complete a GPU work ticket.
        Check(queue->Signal(f12.Get(),value+1)); Check(c11->Wait(f11.Get(),value+1));
        checkPixels(output.shared.Get(),expandedMotion,4,0);
        Check(queue->Signal(done.Get(),value)); Wait(done.Get(),value);
        assert(!guides.Bind(selection,consumer.Get(),queue.Get(),d11.Get(),0,128,72,inputs));
        producer.Reset(); consumer.Reset(); assert(DlssNr::GpuSafety::Drain(10000));
        // Missing producer and lifecycle invalidation never reuse earlier guides.
        auto empty=guides.BeginPresent(); assert(!guides.MatchMetadata(empty,queue.Get(),d11.Get(),0,128,72));
        guides.Invalidate(); assert(!guides.MatchMetadata(selection,queue.Get(),d11.Get(),0,128,72));
        std::printf("PASS depthFormat=%u motionFormat=%u depth=%ux%u motion=%ux%u: pixels, state, fence, final target, matching\n",
            depthFormat,motionFormat,w,h,mw,mh);
    }
    if (debug)
    {
        ComPtr<ID3D12InfoQueue> info; Check(d12.As(&info));
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i)
        {
            SIZE_T bytes=0; Check(info->GetMessage(i,nullptr,&bytes)); std::vector<char> data(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data()); Check(info->GetMessage(i,message,&bytes));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) std::printf("D3D12: %s\n",message->pDescription);
            assert(message->Severity>D3D12_MESSAGE_SEVERITY_ERROR);
        }
    }
    if (flags)
    {
        ComPtr<ID3D11InfoQueue> info; Check(d11.As(&info));
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i)
        {
            SIZE_T bytes=0; Check(info->GetMessage(i,nullptr,&bytes)); std::vector<char> data(bytes);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data()); Check(info->GetMessage(i,message,&bytes));
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) std::printf("D3D11: %s\n",message->pDescription);
            assert(message->Severity>D3D11_MESSAGE_SEVERITY_ERROR);
        }
    }
    Check(d11->GetDeviceRemovedReason()); Check(d12->GetDeviceRemovedReason());
    std::puts("PASS native DX11 transport matrix; real NGX/model validation is a separate harness gate.");
}
