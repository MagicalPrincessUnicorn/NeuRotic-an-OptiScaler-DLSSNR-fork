// GPL-3.0. Optional standalone experimental guidance; no engine guide qualification.
#include "../WorkerContracts.h"
#include "DepthRuntime.h"
#include "DepthTensor.h"
#include "ImageMotion.h"
#include "dav2/Dav2Host.h"
#include <dxgi1_6.h>
#include <mutex>
namespace nrw {
namespace {
bool Same(IUnknown* a,IUnknown* b) {
    ComPtr<IUnknown> left,right;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&left))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&right))) && left==right;
}
bool DriverPresent() {
    wchar_t system[MAX_PATH]{};auto n=GetSystemDirectoryW(system,MAX_PATH);
    return n && n<MAX_PATH && GetFileAttributesW((std::filesystem::path(system)/L"nvofapi64.dll").c_str())!=INVALID_FILE_ATTRIBUTES;
}
}
struct GuidanceHost::Impl {
    std::mutex mutex;
    GuidanceOptions options;
    std::unique_ptr<depth::Dav2Host> dav2;
    bool initialized=false,quarantined=false,depthReady=false;
    std::string depthReason,hardwareReason;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging,inFlight;
    ComPtr<ID3D11Query> readback;
    // Bundle handles outlive runtime module/session destruction (reverse member order).
    std::unique_ptr<guidance::DepthBundle> bundle;
    ComPtr<ID3D12Device> depthDevice;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue=0;
    std::unique_ptr<guidance::DepthRuntime> runtime;
    FrameStamp previousStamp{};
    uint32_t previousWidth=0,previousHeight=0;
    std::vector<float> previous;
    void ResetHistory(){previous.clear();previousStamp={};previousWidth=previousHeight=0;}
    bool Drain() {
        if(!queue)return true;
        if(!fence || FAILED(queue->Signal(fence.Get(),++fenceValue))) {quarantined=true;return false;}
        const auto start=GetTickCount64();
        for(;;) {
            auto value=fence->GetCompletedValue();
            if(value==UINT64_MAX){quarantined=true;return false;}
            if(value>=fenceValue)return true;
            if(GetTickCount64()-start>=5000){quarantined=true;return false;}
            Sleep(1);
        }
    }
    void StopLocked() {
        initialized=false;depthReady=false;ResetHistory();
        if(dav2){dav2->Stop();if(dav2->RequiresRestart()){quarantined=true;return;}dav2.reset();}
        if(quarantined || !Drain())return;
        runtime.reset();fence.Reset();queue.Reset();depthDevice.Reset();bundle.reset();
        readback.Reset();inFlight.Reset();staging.Reset();context.Reset();device.Reset();
        depthReason.clear();hardwareReason.clear();fenceValue=0;
    }
    bool InitializeDepth() {
        if(options.depthBundle.empty()){depthReason="Depth disabled: explicit pinned local depthBundle was not supplied";return false;}
        bundle=std::make_unique<guidance::DepthBundle>();
        if(!bundle->Open(std::filesystem::path(options.depthBundle),depthReason))return false;
        const auto& root=bundle->Root(); // the directory namespace leased before verification
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;
        if(FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&depthDevice)))) {
            depthReason="Same-adapter owned D3D12 depth device unavailable";return false;
        }
        D3D12_COMMAND_QUEUE_DESC d{};d.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        if(FAILED(depthDevice->CreateCommandQueue(&d,IID_PPV_ARGS(&queue))) || FAILED(depthDevice->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) {
            depthReason="Owned DirectML queue/fence creation failed";return false;
        }
        runtime=std::make_unique<guidance::DepthRuntime>();
        bool ready=runtime->Initialize(depthDevice.Get(),queue.Get(),root,root/L"model.onnx",{});
        if(!ready)depthReason="Depth DirectML initialization failed: "+runtime->Error();
        if(!Drain()){depthReason="Depth initialization GPU work is uncertain; restart worker required";return false;}
        return ready;
    }
    bool Read(const CapturedFrame& frame,std::vector<uint8_t>& rgb,std::string& reason) {
        if(!frame.texture || !frame.stamp.session || !frame.stamp.sequence || !frame.stamp.width || !frame.stamp.height || frame.stamp.width>3840 || frame.stamp.height>2160) {
            reason="Captured guidance input absent, unstamped or beyond bounded 3840x2160 profile";return false;
        }
        D3D11_TEXTURE2D_DESC d{};frame.texture->GetDesc(&d);ComPtr<ID3D11Device> parent;frame.texture->GetDevice(&parent);
        if(!Same(parent.Get(),device.Get()) || d.Width!=frame.stamp.width || d.Height!=frame.stamp.height || d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || d.SampleDesc.Count!=1 || d.MipLevels!=1 || d.ArraySize!=1) {
            reason="Captured guidance texture device/format/extent differs from frame stamp";return false;
        }
        D3D11_TEXTURE2D_DESC existing{};if(staging)staging->GetDesc(&existing);
        if(!staging || existing.Width!=d.Width || existing.Height!=d.Height) {
            staging.Reset();d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))){reason="Guidance staging creation failed";return false;}
        }
        if(!readback) {D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};if(FAILED(device->CreateQuery(&query,&readback))){reason="Guidance completion query creation failed";return false;}}
        inFlight=frame.texture;context->CopyResource(staging.Get(),inFlight.Get());context->End(readback.Get());context->Flush();
        const auto start=GetTickCount64();
        for(;;) {
            BOOL completed=FALSE;HRESULT status=context->GetData(readback.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(status==S_OK && completed)break;
            if(FAILED(status) || GetTickCount64()-start>=5000){quarantined=true;reason="Guidance readback completion uncertain; owned resources retained until worker exit";return false;}
            Sleep(1);
        }
        inFlight.Reset();D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT status=context->Map(staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(FAILED(status)){reason="Completed guidance staging map failed";return false;}
        struct Unmap {ID3D11DeviceContext* context;ID3D11Texture2D* texture;~Unmap(){context->Unmap(texture,0);}} unmap{context.Get(),staging.Get()};
        rgb.resize(size_t(d.Width)*d.Height*3);
        for(uint32_t y=0;y<d.Height;++y)for(uint32_t x=0;x<d.Width;++x) {
            auto pixel=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+x*4;auto i=(size_t(y)*d.Width+x)*3;
            rgb[i]=pixel[2];rgb[i+1]=pixel[1];rgb[i+2]=pixel[0];
        }
        return true;
    }
};
GuidanceHost::GuidanceHost():impl_(std::make_unique<Impl>()){}
bool GuidanceHost::RequiresRestart() const {return impl_ && impl_->quarantined;}
GuidanceHost::~GuidanceHost(){Stop();if(impl_->quarantined)impl_.release();}
bool GuidanceHost::Initialize(ID3D11Device* device,ID3D11DeviceContext* context,const GuidanceOptions& options,std::string& reason) {
    std::lock_guard lock(impl_->mutex);auto& p=*impl_;p.StopLocked();
    if(p.quarantined){reason="Guidance owns uncertain GPU work; restart worker required";return false;}
    p.options=options;
    if(options.mode==GuideMode::Off&&options.dav2.provider==depth::Provider::Off){p.initialized=true;reason="Guidance Off; no readback, model load or inference";return true;}
    if(!device || !context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){reason="Guidance requires worker immediate D3D11 device/context";return false;}
    ComPtr<ID3D11Device> parent;context->GetDevice(&parent);
    if(!Same(parent.Get(),device)){reason="Guidance device/context mismatch";return false;}
    p.device=device;p.context=context;
    if(options.dav2.provider==depth::Provider::Dav2){
        p.dav2=std::make_unique<depth::Dav2Host>();const bool ready=p.dav2->Initialize(device,context,options.dav2,options.dav2Root,reason);p.depthReason=reason;
        if(p.dav2->RequiresRestart()){p.quarantined=true;return false;}
        p.initialized=true;return ready||!options.dav2.preview;
    }
    p.hardwareReason=DriverPresent()?"NVOF driver present; hardware provider not integrated/qualified":"NVOF driver absent; hardware provider unavailable";
    if(options.depth) {
        p.depthReady=p.InitializeDepth();
        if(p.quarantined){reason=p.depthReason;return false;}
        if(!p.depthReady) {p.runtime.reset();p.fence.Reset();p.queue.Reset();p.depthDevice.Reset();p.bundle.reset();}
    }
    p.initialized=true;
    reason=p.depthReady?"Pinned Depth Anything V2 Small DirectML ready; relative inverse-depth only":p.depthReason;
    if(options.motion){if(!reason.empty())reason+="; ";reason+="CPU image motion ready; "+p.hardwareReason;}
    if(reason.empty())reason="No optional guide channels selected";
    return true;
}
GuideResult GuidanceHost::Process(const CapturedFrame& frame) {
    std::lock_guard lock(impl_->mutex);auto& p=*impl_;GuideResult result;result.stamp=frame.stamp;
    if(!p.initialized){result.reason="Guidance host stopped/uninitialized";return result;}
    if(p.dav2){result.reason=p.depthReason;result.depthProvider=p.dav2->Ready()?"depth-anything-v2-small-tensorrt-gpu-v1":"";return result;}
    if(p.options.mode==GuideMode::Off){result.reason="Guidance Off";return result;}
    if(p.quarantined){result.reason="Guidance GPU work uncertain; restart worker required";return result;}
    if(!p.options.motion && !p.depthReady){result.reason=p.depthReason.empty()?"No guide channels selected":p.depthReason;return result;}
    std::vector<uint8_t> rgb;
    if(!p.Read(frame,rgb,result.reason)){p.ResetHistory();return result;}
    if(p.depthReady){result.width=518;result.height=294;}
    else {
        double scale=std::min({1.,320./frame.stamp.width,180./frame.stamp.height});
        result.width=std::max(1u,uint32_t(std::floor(frame.stamp.width*scale)));result.height=std::max(1u,uint32_t(std::floor(frame.stamp.height*scale)));
    }
    if(p.depthReady) {
        auto input=guidance::DepthInput(rgb,frame.stamp.width,frame.stamp.height);
        bool ran=p.runtime->Run(input,result.depth);
        if(!p.Drain()){result.depth.clear();result.reason="Depth GPU completion uncertain; restart worker required";return result;}
        if(ran){result.depthCompleted=true;result.depthProvider="depth-anything-v2-small-directml-fp32-v1";}
        else {result.depth.clear();result.reason="Depth inference failed: "+p.runtime->Error();}
    } else if(p.options.depth)result.reason=p.depthReason;
    if(p.options.motion) {
        auto luma=guidance::Luma(rgb,frame.stamp.width,frame.stamp.height,result.width,result.height);
        bool adjacent=!frame.stamp.reset && !p.previous.empty() && frame.stamp.session==p.previousStamp.session && p.previousStamp.sequence!=UINT64_MAX && frame.stamp.sequence==p.previousStamp.sequence+1 &&
            frame.stamp.width==p.previousStamp.width && frame.stamp.height==p.previousStamp.height && result.width==p.previousWidth && result.height==p.previousHeight && frame.stamp.timestampQpc>p.previousStamp.timestampQpc;
        if(adjacent) {
            result.motionXY=guidance::Motion(luma,p.previous,result.width,result.height);
            result.motionCompleted=!result.motionXY.empty();if(result.motionCompleted)result.motionProvider="cpu-block-match-sdr-v1";
        }
        p.previous=std::move(luma);p.previousStamp=frame.stamp;p.previousWidth=result.width;p.previousHeight=result.height;
        if(!result.reason.empty())result.reason+="; ";
        result.reason+=adjacent?"Estimated CPU image displacement current->previous in guide pixels; search +/-12, jitter unknown":"Motion reference reset/first observation; no pair estimate";
        result.reason+="; "+p.hardwareReason;
    }
    if(result.depthCompleted){if(!result.reason.empty())result.reason+="; ";result.reason+="Estimated relative inverse-depth; larger is nearer, metric scale/projection unknown; CPU SDR bicubic preparation";}
    return result;
}
void GuidanceHost::Stop(){std::lock_guard lock(impl_->mutex);impl_->StopLocked();}
void GuidanceHost::OfferDepth(const CapturedFrame& frame,uint64_t sourceId){std::lock_guard lock(impl_->mutex);if(impl_->dav2)impl_->dav2->Offer(frame,sourceId);}
void GuidanceHost::ObserveDepthEpochs(const FrameStamp& stamp,uint64_t sourceId,uint64_t configEpoch){std::lock_guard lock(impl_->mutex);if(impl_->dav2)impl_->dav2->ObserveEpochs({sourceId,stamp.streamEpoch,stamp.sequence,stamp.geometryEpoch,configEpoch});}
bool GuidanceHost::PollDepth(depth::DepthFrame& frame,std::string& reason){std::lock_guard lock(impl_->mutex);if(!impl_->dav2)return false;auto result=impl_->dav2->Poll(reason);impl_->depthReason=reason;if(impl_->dav2->RequiresRestart())impl_->quarantined=true;if(!result)return false;frame=std::move(*result);return true;}
bool GuidanceHost::DepthReady()const{return impl_->dav2&&impl_->dav2->Ready();}
bool GuidanceHost::DepthBusy()const{return impl_->dav2&&impl_->dav2->Busy();}
uint64_t GuidanceHost::DepthFresh()const{return impl_->dav2?impl_->dav2->Fresh():0;}
uint64_t GuidanceHost::DepthDropped()const{return impl_->dav2?impl_->dav2->Dropped():0;}
}
