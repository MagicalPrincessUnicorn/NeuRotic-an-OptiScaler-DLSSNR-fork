#include "NativeFsr3D3D12.h"
#include "NativeIdentity.h"
#include <dx12/ffx_api_dx12.h>
#include <Windows.h>
#include <array>
#include <vector>
namespace DlssNr::NativeFg {
using Microsoft::WRL::ComPtr;
namespace {
bool Same(ID3D12Device* device,ID3D12DeviceChild* child){ComPtr<ID3D12Device> owner;
    return child&&SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&owner)))&&NativeIdentity::CompareDevices(device,owner.Get()).equal;}
bool Shape(ID3D12Resource* r,unsigned w,unsigned h,DXGI_FORMAT format){if(!r)return false;const auto d=r->GetDesc();
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&d.Format==format;}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);}
}
struct FsrSession::Impl {
    FsrApi api;History history;ffxContext context=nullptr;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence,producer;
    ComPtr<ID3D12Resource> output,color,depth,motion;
    HANDLE event=nullptr;std::uint64_t value=0;unsigned width=0,height=0;bool inverted=false,unsafe=false,pending=false,interpolated=false;
    ~Impl(){if(event)CloseHandle(event);}
    bool Wait(){const auto deadline=GetTickCount64()+5000;
        if(FAILED(fence->SetEventOnCompletion(value,event)))return false;
        for(;;){const auto completed=fence->GetCompletedValue();if(completed==UINT64_MAX)return false;if(completed>=value)return true;
            const auto now=GetTickCount64();if(now>=deadline||WaitForSingleObject(event,DWORD(deadline-now))!=WAIT_OBJECT_0)return false;}}
    bool Setup(ID3D12Device* d,std::string& reason){
        if(device)return NativeIdentity::CompareDevices(device.Get(),d).equal;
        if(!api.create||!api.destroy||!api.configure||!api.dispatch){
#if defined(NRPG_CPU_MAINLINE_TESTING) || defined(NRW_FG_EXTERNAL_API)
            reason="Native FG provider not injected in this fixture";return false;
#else
            if(!LoadFsrApi(api,reason))return false;
#endif
        }
        device=d;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||FAILED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)))||
           FAILED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list->Close())||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))||!(event=CreateEventW(nullptr,FALSE,FALSE,nullptr))){reason="Native FG command owner creation failed";unsafe=true;return false;}
        return true;
    }
    bool Configure(const FsrFrame& f,std::string& reason){
        if(context&&output&&width==f.width&&height==f.height&&inverted==f.inverted)return true;
        if(context){if(api.destroy(&context,nullptr)!=FFX_API_RETURN_OK){unsafe=true;reason="Native FG provider destruction unconfirmed";return false;}context=nullptr;}
        output.Reset();width=f.width;height=f.height;inverted=f.inverted;
        ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=device.Get();
        ffxCreateContextDescFrameGeneration create{};create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;create.header.pNext=&backend.header;
        create.displaySize=create.maxRenderSize={width,height};create.backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        create.flags=FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS|(inverted?FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED:0);
        if(api.create(&context,&create.header,nullptr)!=FFX_API_RETURN_OK||!context){reason="Native FSR frame-generation context creation failed";context=nullptr;return false;}
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)))){reason="Native FG output allocation failed";return false;}
        return true;
    }
};
FsrSession::FsrSession()=default;
FsrSession::FsrSession(FsrApi api):impl_(std::make_unique<Impl>()),injectedApi_(api){impl_->api=api;}
FsrSession::~FsrSession(){std::string reason;if(!Close(reason))impl_.release();}
bool FsrSession::Unsafe()const{return impl_&&impl_->unsafe&&!impl_->pending;}
bool FsrSession::Busy()const{return impl_&&impl_->pending;}
HANDLE FsrSession::CompletionWakeHandle()const{return Busy()?impl_->event:nullptr;}
bool FsrSession::Close(std::string& reason){
    if(!impl_)return true;auto& o=*impl_;
    if(o.pending){reason="Native FG completion pending; owner retained";return false;}
    if(o.unsafe){reason="Native FG GPU/provider owner retained after uncertain work";return false;}
    if(o.context&&o.api.destroy(&o.context,nullptr)!=FFX_API_RETURN_OK){o.unsafe=true;reason="Native FG provider release failed";return false;}
    impl_.reset();return true;
}
bool FsrSession::Process(ID3D12Device* device,const FsrFrame& f,FsrOutput& out,std::string& reason){
    out={};if(!Begin(device,f,reason))return false;
    if(!impl_->Wait()){impl_->pending=false;reason="Native FG GPU completion uncertain; owner retained";return false;}
    return Poll(out,reason);
}
bool FsrSession::Begin(ID3D12Device* device,const FsrFrame& f,std::string& reason){
    reason.clear();
    if(Busy()){reason="Native FG operation already pending";return false;}
    if(!device||!Shape(f.color,f.width,f.height,DXGI_FORMAT_R8G8B8A8_UNORM)||!Shape(f.depth,f.width,f.height,DXGI_FORMAT_R32_FLOAT)||
       !Shape(f.motion,f.width,f.height,DXGI_FORMAT_R16G16_FLOAT)||!Same(device,f.color)||!Same(device,f.depth)||!Same(device,f.motion)||!Same(device,f.producer)||
       !f.producerValue||f.producerValue==UINT64_MAX||f.producer->GetCompletedValue()==UINT64_MAX){reason="Native FG input device, geometry or completion refused";return false;}
    if(!impl_){impl_=std::make_unique<Impl>();
        impl_->api=injectedApi_;
    }auto& o=*impl_;
    if(o.unsafe||!o.Setup(device,reason)){if(o.unsafe)reason="Native FG owner quarantined";return false;}
    const auto decision=o.history.Accept(f.session,f.generation,f.frame,f.width,f.height,f.inverted,f.reset,f.deltaMs);
    if(!decision.accepted){reason="Native FG capture identity, timing or history refused";return false;}
    if(!o.Configure(f,reason)||!o.output){o.history.Reset();return false;}
    ffxConfigureDescFrameGeneration cfg{};cfg.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    cfg.frameGenerationEnabled=true;cfg.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;cfg.frameID=f.frame;cfg.generationRect={0,0,static_cast<int>(f.width),static_cast<int>(f.height)};
    if(o.api.configure(&o.context,&cfg.header)!=FFX_API_RETURN_OK){o.history.Reset();reason="Native FG standalone configure failed";return false;}
    if(o.value>=UINT64_MAX-1||FAILED(o.allocator->Reset())||FAILED(o.list->Reset(o.allocator.Get(),nullptr))){o.unsafe=true;reason="Native FG command reset failed";return false;}
    // Input aliases are held through the actual signal/join, including failure.
    o.color=f.color;o.depth=f.depth;o.motion=f.motion;o.producer=f.producer;o.unsafe=true;
    Transition(o.list.Get(),f.color,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ffxDispatchDescFrameGenerationPrepare prepare{};prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
    prepare.frameID=f.frame;prepare.flags=cfg.flags;prepare.commandList=o.list.Get();prepare.renderSize={f.width,f.height};prepare.motionVectorScale={1,1};
    prepare.frameTimeDelta=static_cast<float>(f.deltaMs);
    // Explicit projection policy; no claim that these are engine observations.
    prepare.cameraNear=0.1f;prepare.cameraFar=1000.f;prepare.cameraFovAngleVertical=1.0471975512f;prepare.viewSpaceToMetersFactor=1.f;
    prepare.depth=ffxApiGetResourceDX12(f.depth,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    prepare.motionVectors=ffxApiGetResourceDX12(f.motion,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    ffxDispatchDescFrameGeneration dispatch{};dispatch.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    dispatch.commandList=o.list.Get();dispatch.frameID=f.frame;dispatch.reset=decision.reset;dispatch.numGeneratedFrames=1;
    dispatch.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;dispatch.generationRect=cfg.generationRect;
    dispatch.presentColor=ffxApiGetResourceDX12(f.color,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatch.outputs[0]=ffxApiGetResourceDX12(o.output.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    if(o.api.dispatch(&o.context,&prepare.header)!=FFX_API_RETURN_OK||o.api.dispatch(&o.context,&dispatch.header)!=FFX_API_RETURN_OK){reason="Native FG provider recording failed; owner retained";return false;}
    Transition(o.list.Get(),f.color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(FAILED(o.list->Close())||FAILED(o.queue->Wait(f.producer,f.producerValue))){reason="Native FG producer wait or recording close failed";return false;}
    ID3D12CommandList* lists[]{o.list.Get()};o.queue->ExecuteCommandLists(1,lists);
    if(FAILED(o.queue->Signal(o.fence.Get(),++o.value))){reason="Native FG GPU completion signal failed; owner retained";return false;}
    o.pending=true;o.interpolated=decision.interpolate;
    if(FAILED(o.fence->SetEventOnCompletion(o.value,o.event))){o.pending=false;reason="Native FG completion event failed; owner retained";return false;}
    return true;
}
bool FsrSession::Poll(FsrOutput& out,std::string& reason){
    out={};reason.clear();
    if(!impl_||!impl_->pending){reason="Native FG has no pending operation";return false;}
    auto& o=*impl_;const auto completed=o.fence->GetCompletedValue();
    if(completed==UINT64_MAX){o.pending=false;reason="Native FG device lost; GPU owner retained";return false;}
    if(completed<o.value)return false;
    o.pending=false;
    o.unsafe=false;o.color.Reset();o.depth.Reset();o.motion.Reset();o.producer.Reset();
    out={o.output,o.fence,o.value,o.interpolated};reason=o.interpolated?"Native FSR generated output GPU-complete":"Native FSR history reset/warmup; real frame only";return true;
}
}
