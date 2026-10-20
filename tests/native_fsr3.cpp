#include "../OptiScaler/dlssnr/NativeFsr3D3D12.h"
#include <dxgi1_4.h>
#include <dx12/ffx_api_dx12.h>
#include <cassert>
#include <cstdio>
using namespace DlssNr::NativeFg;
using Microsoft::WRL::ComPtr;
static unsigned configurations=0,prepares=0,dispatches=0,destroys=0;
static ffxReturnCode_t Create(ffxContext* c,ffxCreateContextDescHeader* h,const ffxAllocationCallbacks*){
    assert(h->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION);*c=reinterpret_cast<void*>(1);return FFX_API_RETURN_OK;
}
static ffxReturnCode_t Destroy(ffxContext*,const ffxAllocationCallbacks*){++destroys;return FFX_API_RETURN_OK;}
static ffxReturnCode_t Configure(ffxContext*,const ffxConfigureDescHeader* h){
    auto d=reinterpret_cast<const ffxConfigureDescFrameGeneration*>(h);
    assert(d->flags&FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY);
    assert(!d->swapChain&&!d->allowAsyncWorkloads&&d->frameGenerationEnabled);++configurations;return FFX_API_RETURN_OK;
}
static ffxReturnCode_t Dispatch(ffxContext*,const ffxDispatchDescHeader* h){
    if(h->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE){
        auto d=reinterpret_cast<const ffxDispatchDescFrameGenerationPrepare*>(h);
        assert(d->motionVectorScale.x==1&&d->motionVectorScale.y==1);
        assert(d->depth.resource&&d->motionVectors.resource);++prepares;return FFX_API_RETURN_OK;
    }
    auto d=reinterpret_cast<const ffxDispatchDescFrameGeneration*>(h);assert(d->numGeneratedFrames==1);
    auto list=static_cast<ID3D12GraphicsCommandList*>(d->commandList);
    auto src=static_cast<ID3D12Resource*>(d->presentColor.resource),dst=static_cast<ID3D12Resource*>(d->outputs[0].resource);
    D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& e:b)e.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(2,b);list->CopyResource(dst,src);
    for(auto& e:b)std::swap(e.Transition.StateBefore,e.Transition.StateAfter);
    list->ResourceBarrier(2,b);++dispatches;return FFX_API_RETURN_OK;
}
int main(){
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> device;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))));
    auto texture=[&](DXGI_FORMAT fmt,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> r;D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=64;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
        d.Format=fmt;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))));return r;};
    auto color=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto depth=texture(DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto motion=texture(DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ComPtr<ID3D12Fence> producer;assert(SUCCEEDED(device->CreateFence(1,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&producer))));
    FsrSession session({Create,Destroy,Configure,Dispatch});FsrFrame f{1,1,1,64,64,false,false,16.0,color.Get(),depth.Get(),motion.Get(),producer.Get(),1};FsrOutput out;std::string reason;
    assert(session.Process(device.Get(),f,out,reason)&&!out.generated&&out.completion->GetCompletedValue()>=out.value);
    auto first=out.color;out={};f.frame=2;
    assert(session.Process(device.Get(),f,out,reason)&&out.generated&&out.color==first);
    out={};assert(!session.Process(device.Get(),f,out,reason));
    f.frame=3;f.reset=true;assert(session.Process(device.Get(),f,out,reason)&&!out.generated);out={};
    assert(session.Close(reason)&&destroys==1&&configurations==3&&prepares==3&&dispatches==3);
    std::puts("PASS: native FSR adapter GPU publication, reuse, reset, replay and prepare conventions (explicit provider fixture)");
}
