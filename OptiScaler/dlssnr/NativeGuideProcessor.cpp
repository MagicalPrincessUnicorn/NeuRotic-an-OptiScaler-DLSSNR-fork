#include "NativeGuideProcessor.h"
#include "NativeGuideTiming.h"
#include "FinalFallbackControl.h"
#include "NativeGuideRoute.h"
#ifndef _WIN64
namespace DlssNr::NativeGuides {
struct Processor::Impl {};
Processor::Processor()=default;
Processor::~Processor()=default;
bool Processor::CanYieldOutput(std::string& reason) const {reason.clear();return true;}
bool Processor::CanSwitchInput(std::string& reason) const {reason.clear();return true;}
bool CanSwitchInput(std::string& reason) {reason.clear();return true;}
Outcome Processor::Process(const Capture&,std::vector<std::uint8_t>& output,std::string& reason){
    output.clear();reason="Native software guide processing requires a 64-bit host";return Outcome::Unavailable;
}
Outcome Process(const Capture& c,std::vector<std::uint8_t>& output,std::string& reason){static Processor p;return p.Process(c,output,reason);}
ID3D12Device* Processor::SharedDevice(std::uint64_t,std::string& reason){reason="Shared GPU processing requires 64-bit host";return nullptr;}
Outcome Processor::ProcessShared(const Capture&,const SharedCapture&,std::string& reason){reason="Shared GPU processing requires 64-bit host";return Outcome::Unavailable;}
Outcome Processor::ProcessTextures(const TextureCapture&,nrpg::CpuMainlineClient::GpuOutput& output,std::string& reason){output={};reason="Texture processing requires a 64-bit host";return Outcome::Unavailable;}
ID3D12Device* SharedDevice(std::uint64_t luid,std::string& reason){static Processor p;return p.SharedDevice(luid,reason);}
Outcome ProcessShared(const Capture& c,const SharedCapture& s,std::string& reason){static Processor p;return p.ProcessShared(c,s,reason);}
}
#else
#include "../../addons/prepared-guides/src/CpuMainlineClient.h"
#include "../../addons/prepared-guides/flow/PreparedFlowContract.h"
#include "NativeGpuPixels.h"
#include <nr/context/PreparedGuideNormalization.h>
#include "NativeFsr3D3D12.h"
#include <dxgi1_4.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>

namespace DlssNr::NativeGuides {
namespace F=Neurotic::PreparedFlow;
namespace P=Neurotic::Feed::Prepared;
using Microsoft::WRL::ComPtr;
namespace {
constexpr std::uint64_t MaxPackedBytes=160ull*1024*1024;
constexpr std::uint64_t Producer=0x4e524e4154495645ull; // NRNATIVE
bool Refuse(std::string& reason,const char* message){reason=message;return false;}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* image,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
bool Admit(const Capture& c,std::string& reason,bool shared=false){
    const auto pixels=std::uint64_t(c.width)*c.height;
    if(!c.adapterLuid||!c.session||!c.generation||!c.capture)return Refuse(reason,"Native capture identity or adapter LUID missing");
    if(!F::Dimensions(c.width,c.height)||pixels>MaxPackedBytes/20||(!shared&&(c.bgra.size()!=pixels*4||c.depth.size()!=pixels)))
        return Refuse(reason,"Native capture shape, payload or allocation bound refused");
    for(float z:c.depth)if(!std::isfinite(z)||z<0||z>1)return Refuse(reason,"Native captured device depth is outside finite [0,1]");
    return true;
}
}
struct Processor::Impl {
    nrpg::CpuMainlineClient mainline;
    F::Session flow;
    NativeFg::FsrSession framegen;
    NativeFg::FsrOutput generatedOutput;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> upload;
    // Hold the most recent recording's resources even when completion fails.
    ComPtr<ID3D12Resource> recordingColor;
    ComPtr<ID3D12Resource> recordingDepth;
    GpuPixels pixels;
    GpuPixels generatedPixels;
    struct Normalized {ComPtr<ID3D12Resource> color,depth;bool ready=false;};
    // One immutable history input and one next input. Flow owns the lease
    // through its in-flight/previous Input; no frame-number-based recycling.
    std::array<std::shared_ptr<Normalized>,2> normalized;
    std::uint64_t normalizedAllocations=0;
    nrpg::CpuMainlineClient::GpuOutput residentOutput;
    // Retain external COM references if submission/recording becomes uncertain.
    ComPtr<ID3D12Resource> sharedColor,sharedDepth,sharedOutput;
    ComPtr<ID3D12Resource> sharedGenerated;
    ComPtr<ID3D12Resource> sharedGuideDepth,sharedGuideMotion;
    ComPtr<ID3D12Fence> sharedInputFence,sharedOutputFence;
    F::Output flowOutput;
    nrpg::CpuMainlineClient::GpuInputs textureInputs;
    P::Descriptor lastTexture;
    std::weak_ptr<void> textureOutput;
    bool textureMode=false,lastDerived=false;
    uint64_t inputEpoch=0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorFootprint{};
    std::uint64_t uploadBytes=0,timeline=0,luid=0,session=0,generation=0,lastCapture=0;
    unsigned width=0,height=0;
    bool depthInverted=false,unsafe=false,initialized=false,configured=false;
    HANDLE event=nullptr;
    Impl()=default;
#ifdef NRPG_CPU_MAINLINE_TESTING
    bool injectedDevice=false;
    explicit Impl(nrpg::CpuMainlineClient::TestRecorder r,ID3D12Device* testDevice,NativeFg::FsrApi fg):mainline(r),framegen(fg),device(testDevice),injectedDevice(testDevice!=nullptr){}
#endif
    ~Impl(){if(event)CloseHandle(event);}
    bool Buffer(D3D12_HEAP_TYPE type,std::uint64_t size,ComPtr<ID3D12Resource>& result){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
        d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?
            D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));
    }
    D3D12_RESOURCE_DESC Texture(DXGI_FORMAT format)const{
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
        d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;return d;
    }
    bool Initialize(const Capture& c,std::string& reason,ID3D12Device* supplied=nullptr){
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;LUID identity{};std::memcpy(&identity,&c.adapterLuid,8);
        if(supplied){const auto actual=supplied->GetAdapterLuid();
            if(actual.LowPart!=identity.LowPart||actual.HighPart!=identity.HighPart)return Refuse(reason,"Texture device adapter mismatch");
            device=supplied;
        }else
#ifdef NRPG_CPU_MAINLINE_TESTING
        if(injectedDevice){const auto actual=device->GetAdapterLuid();
            if(actual.LowPart!=identity.LowPart||actual.HighPart!=identity.HighPart)return Refuse(reason,"Injected test device adapter mismatch");}
        else
#endif
        {
            if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(identity,IID_PPV_ARGS(&adapter)))||
               FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))
                return Refuse(reason,"Native flow cannot open the observed source adapter");
        }
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||FAILED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)))||
           FAILED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list->Close())||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return Refuse(reason,"Native flow recording owner creation failed");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return Refuse(reason,"Native flow completion event creation failed");
        luid=c.adapterLuid;initialized=true;return true;
    }
    bool Configure(const Capture& c,std::string& reason){
        if(configured&&width==c.width&&height==c.height&&session==c.session&&generation==c.generation)return true;
        if(!flow.Close(reason)){unsafe=true;return false;}
        normalized={};
        configured=false;
        width=c.width;height=c.height;session=c.session;generation=c.generation;lastCapture=0;
        upload.Reset();recordingColor.Reset();
        auto d=Texture(DXGI_FORMAT_R8G8B8A8_UNORM);device->GetCopyableFootprints(&d,0,1,0,&colorFootprint,nullptr,nullptr,&uploadBytes);
        if(!textureMode&&(uploadBytes>MaxPackedBytes||!Buffer(D3D12_HEAP_TYPE_UPLOAD,uploadBytes,upload)))return Refuse(reason,"Native flow bounded upload allocation failed" );
        F::Options options;options.width=width;options.height=height;options.requested=F::Backend::Software;
        return configured=flow.Initialize(device.Get(),options,reason);
    }
    bool Begin(std::string& reason){
        if(timeline>=UINT64_MAX-1)return Refuse(reason,"Native flow timeline exhausted");
        return (SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)))||Refuse(reason,"Native flow command reset failed");
    }
    bool Submit(std::string& reason,ID3D12Fence* external=nullptr,std::uint64_t externalValue=0){
        if(FAILED(list->Close()))return Refuse(reason,"Native flow command close failed");
        // A failure from here retains the entire owner including both queues.
        unsafe=true;ID3D12CommandList* command=list.Get();queue->ExecuteCommandLists(1,&command);
        if(external&&FAILED(queue->Signal(external,externalValue)))return Refuse(reason,"Shared GPU output signal failed; owner retained");
        return SUCCEEDED(queue->Signal(fence.Get(),++timeline))||Refuse(reason,"Native flow completion signal failed; owner retained");
    }
    bool Wait(std::string& reason){
        if(F::Completed(fence->GetCompletedValue(),timeline))return true;
        if(fence->GetCompletedValue()==UINT64_MAX)return Refuse(reason,"Native flow device lost; owner retained");
        ResetEvent(event);
        if(FAILED(fence->SetEventOnCompletion(timeline,event))||WaitForSingleObject(event,5000)!=WAIT_OBJECT_0||!F::Completed(fence->GetCompletedValue(),timeline))
            return Refuse(reason,"Native flow GPU completion unknown after bounded wait; owner retained");
        return true;
    }
    bool Upload(const Capture& c,F::Input& input,std::string& reason){
        auto d=Texture(DXGI_FORMAT_R8G8B8A8_UNORM);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        recordingColor.Reset();
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&recordingColor))))
            return Refuse(reason,"Native flow immutable color allocation failed");
        void* ptr=nullptr;const D3D12_RANGE none{0,0};if(FAILED(upload->Map(0,&none,&ptr)))return Refuse(reason,"Native flow upload map failed");
        auto* bytes=static_cast<std::uint8_t*>(ptr);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto* out=bytes+std::size_t(y)*colorFootprint.Footprint.RowPitch+x*4;
            const auto* in=c.bgra.data()+(std::size_t(y)*width+x)*4;out[0]=in[2];out[1]=in[1];out[2]=in[0];out[3]=in[3];}
        upload->Unmap(0,nullptr);if(!Begin(reason))return false;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=recordingColor.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=colorFootprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(list.Get(),recordingColor.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!Submit(reason)||!Wait(reason))return false;unsafe=false;
        input.color=recordingColor;input.ownership=std::make_shared<ComPtr<ID3D12Resource>>(recordingColor);input.producer={fence,timeline};return true;
    }

};
Processor::Processor()=default;
Processor::~Processor(){if(impl_&&impl_->unsafe)(void)impl_.release();}
bool Processor::CanYieldOutput(std::string& reason) const {
    if(!CanSwitchInput(reason))return false;
    if(impl_&&!impl_->textureMode&&impl_->lastCapture)
        return Refuse(reason,"Shared guide consumer retirement is not verified");
    return true;
}
bool Processor::CanSwitchInput(std::string& reason) const {
    if(!impl_){reason.clear();return true;}const auto& o=*impl_;
    if(o.unsafe||o.mainline.RequiresRestart()||o.framegen.Unsafe())
        return Refuse(reason,"Guide processor completion is quarantined");
    if(o.mainline.Preparing())return Refuse(reason,"Guide model preparation is still pending");
    if(o.fence){const auto done=o.fence->GetCompletedValue();
        if(done==UINT64_MAX||done<o.timeline)return Refuse(reason,"Guide processor completion is pending or unknown");}
    if(o.textureInputs.lease||o.flowOutput.ownership)
        return Refuse(reason,"Guide processor input or flow lease remains active");
    if(o.framegen.Busy()||o.generatedOutput.color||o.residentOutput.lease||
       o.sharedColor||o.sharedDepth||o.sharedOutput||o.sharedGenerated||
       o.sharedGuideDepth||o.sharedGuideMotion||o.sharedInputFence||o.sharedOutputFence)
        return Refuse(reason,"Guide processor shared consumer remains active");
    reason.clear();return true;
}
#ifdef NRPG_CPU_MAINLINE_TESTING
Processor::Processor(nrpg::CpuMainlineClient::TestRecorder r,ID3D12Device* testDevice,NativeFg::FsrApi fg):impl_(std::make_unique<Impl>(r,testDevice,fg)){}
#endif
Outcome Processor::Process(const Capture& c,std::vector<std::uint8_t>& output,std::string& reason){
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted()){output.clear();reason="Output handoff is active";return Outcome::Unavailable;}
    return ProcessImpl(c,output,reason,nullptr);
}
ID3D12Device* Processor::SharedDevice(std::uint64_t luid,std::string& reason){
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted()){reason="Output handoff is active";return nullptr;}
    if(!impl_)impl_=std::make_unique<Impl>();auto& o=*impl_;Capture c{};c.adapterLuid=luid;
    if(o.unsafe||(!o.initialized&&!o.Initialize(c,reason))||o.luid!=luid)return nullptr;
    if(!o.pixels.Ready()&&!o.pixels.Initialize(o.device.Get(),reason))return nullptr;
    return o.pixels.Ready()?o.device.Get():nullptr;
}
Outcome Processor::ProcessShared(const Capture& c,const SharedCapture& shared,std::string& reason){
    if(shared.generatedReady)*shared.generatedReady=false;
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted()){reason="Output handoff is active";return Outcome::Unavailable;}
    std::vector<std::uint8_t> unused;const auto result=ProcessImpl(c,unused,reason,&shared);
    if(result!=Outcome::Unsafe&&result!=Outcome::Delivered&&impl_&&shared.outputFence&&shared.value&&shared.value<UINT64_MAX){
        auto& o=*impl_;ComPtr<ID3D12Device> owner;
        if(!o.queue||!shared.inputFence||FAILED(shared.outputFence->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(o.device.Get(),owner.Get()).equal)return result;
        owner.Reset();
        if(FAILED(shared.inputFence->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(o.device.Get(),owner.Get()).equal)return result;
        // Even an admission refusal must not return shared storage while its
        // producer is still writing it. Retain the packet if this wait is uncertain.
        o.sharedColor=shared.color;o.sharedDepth=shared.depth;o.sharedOutput=shared.output;
        o.sharedInputFence=shared.inputFence;o.sharedOutputFence=shared.outputFence;
        o.unsafe=true;
        if(o.timeline>=UINT64_MAX-1||shared.inputFence->GetCompletedValue()==UINT64_MAX||FAILED(o.queue->Wait(shared.inputFence,shared.value))||FAILED(o.queue->Signal(shared.outputFence,shared.value))||FAILED(o.queue->Signal(o.fence.Get(),++o.timeline))||!o.Wait(reason))return Outcome::Unsafe;
        o.unsafe=false;
    }
    if(result!=Outcome::Unsafe&&impl_){auto& o=*impl_;o.sharedColor.Reset();o.sharedDepth.Reset();o.sharedOutput.Reset();o.sharedGenerated.Reset();o.sharedInputFence.Reset();o.sharedOutputFence.Reset();}
    return result;
}
Outcome Processor::ProcessTextures(const TextureCapture& capture,nrpg::CpuMainlineClient::GpuOutput& output,std::string& reason){
    output={};reason.clear();
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted()){reason="Output handoff is active";return Outcome::Unavailable;}
    if(impl_&&impl_->unsafe){reason="Texture owner quarantined; restart required";return Outcome::Unsafe;}
    const auto& g=capture.normalized;auto d=capture.description;
    const auto refuse=[&](const char* why){reason=why;return Outcome::Unavailable;};
    const auto w=d.color.width,h=d.color.height;
    if(!capture.adapterLuid||!g.device||!g.color||!g.depth||!g.producer||!g.lease||
       !g.value||g.value==UINT64_MAX||!F::Dimensions(w,h)||std::uint64_t(w)*h>MaxPackedBytes/20)
        return refuse("Canonical texture identity, dimensions or ownership missing");
    if(!F::Completed(g.producer->GetCompletedValue(),g.value))return refuse("Texture producer completion not established");
    const auto actual=g.device->GetAdapterLuid();std::uint64_t actualLuid=0;std::memcpy(&actualLuid,&actual,8);
    if(actualLuid!=capture.adapterLuid)return refuse("Texture adapter mismatch");
    for(ID3D12DeviceChild* child:{static_cast<ID3D12DeviceChild*>(g.color.Get()),static_cast<ID3D12DeviceChild*>(g.depth.Get()),static_cast<ID3D12DeviceChild*>(g.producer.Get())}){
        ComPtr<ID3D12Device> owner;if(FAILED(child->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(g.device.Get(),owner.Get()).equal)
            return refuse("Canonical texture belongs to another logical device");
    }
    const std::array<ID3D12Resource*,2> images{g.color.Get(),g.depth.Get()};
    const std::array<DXGI_FORMAT,2> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT};
    for(unsigned i=0;i<2;++i){const auto r=images[i]->GetDesc();
        if(r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||r.Width!=w||r.Height!=h||r.DepthOrArraySize!=1||
           r.MipLevels!=1||r.SampleDesc.Count!=1||r.Format!=formats[i]||(r.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
            return refuse("Canonical texture shape or format unsupported");
    }
    const P::Raster full{w,h,0,0,w,h};
    if(d.color!=full||d.depth!=full||d.colorDomain!=P::C::ColorDomain::EncodedDisplay)
        return refuse("Canonical textures require full-raster SDR");
    // Validate source facts before issuing any flow work. These flow fields are
    // prospective only; actual publication below requires ApplyCompletedFlow.
    auto validation=d;
    if(capture.deriveMotion){validation.motion=validation.distrust=full;validation.maskCapture=d.capture;
        validation.motionOrigin=validation.maskOrigin=P::C::SourceClass::Derived;
        validation.motionUnits=P::C::MotionUnits::Pixels;validation.motionDirection=P::C::MotionDirection::CurrentToPrevious;
        validation.motionGridWidth=validation.motionGridHeight=1;validation.scaleX=validation.scaleY=1;
        validation.flags|=P::HasDistrust|P::Reset;validation.previousCapture=0;}
    const auto valid=Neurotic::Context::NormalizePreparedGuides(validation);
    if(!valid){reason=valid.reason;return Outcome::Unavailable;}
    if(!capture.deriveMotion&&(d.motion!=full||d.distrust!=full||d.motionUnits!=P::C::MotionUnits::Pixels||
       d.motionGridWidth!=1||d.motionGridHeight!=1||d.scaleX!=1||d.scaleY!=1||!(d.flags&P::HasDistrust)))
        return refuse("Supplied motion must be normalized full-resolution pixel vectors");
    if(!impl_)impl_=std::make_unique<Impl>();auto& o=*impl_;
    if(!o.textureOutput.expired())return refuse("Texture output remains leased by copyback");
    if(o.initialized&&((!o.textureMode&&o.lastCapture)||!NativeIdentity::CompareDevices(o.device.Get(),g.device.Get()).equal))
        return refuse("Texture owner device or input mode changed; recreate after retirement");
    const auto epoch=FinalFallback::CurrentInputEpoch();
    if(o.inputEpoch!=epoch){d.flags|=P::Reset;d.previousCapture=0;}
    const auto& old=o.lastTexture;
    const bool episode=old.capture&&old.producer==d.producer&&old.session==d.session&&old.stream==d.stream&&
        old.device==d.device&&old.generation==d.generation;
    if(episode&&d.capture<=old.capture)return refuse("Texture capture is stale or replayed");
    Capture c{};c.adapterLuid=capture.adapterLuid;c.session=d.stream;c.generation=d.generation;c.width=w;c.height=h;
    try{
        if(!o.initialized&&!o.Initialize(c,reason,g.device.Get()))return Outcome::Unavailable;
        o.textureMode=true;
        nrpg::CpuMainlineClient::GpuInputs inputs=g;
        if(capture.deriveMotion){
            const bool reset=!episode||!o.lastDerived||(d.flags&P::Reset)||d.capture-old.capture!=1||
                old.color!=d.color||old.depthReversed!=d.depthReversed||old.sourceApi!=d.sourceApi;
            if(!o.Configure(c,reason))return o.unsafe?Outcome::Unsafe:Outcome::Unavailable;
            d.previousCapture=reset?0:old.capture;
            F::Input input;input.color=g.color;input.ownership=g.lease;input.producer={g.producer,g.value};
            input.pair={d.capture,d.previousCapture,d.stream,d.generation};input.reset=reset;
            const auto submitted=o.flow.SubmitChecked(input,reason);
            if(submitted!=F::SubmitDisposition::Submitted){o.unsafe=submitted==F::SubmitDisposition::Unsafe;return o.unsafe?Outcome::Unsafe:Outcome::Unavailable;}
            o.unsafe=true;o.textureInputs=g;
            if(!o.flow.Wait(o.flowOutput,reason)||!F::ApplyCompletedFlow(o.flowOutput,d))return Outcome::Unsafe;
            inputs.motion=o.flowOutput.motion;inputs.distrust=o.flowOutput.distrust;
            inputs.producer=o.flowOutput.producer.fence;inputs.value=o.flowOutput.producer.value;inputs.lease=o.flowOutput.ownership;
        }
        o.textureInputs=g;
        const bool delivered=o.mainline.ProcessResident(capture.adapterLuid,d,inputs,o.residentOutput,reason);
        if(o.mainline.RequiresRestart()){o.unsafe=true;return Outcome::Unsafe;}
        if(capture.deriveMotion){std::string retirement;
            if(o.residentOutput.completion.fence&&!o.flow.AddReader({o.residentOutput.completion.fence,o.residentOutput.completion.value},retirement)){reason=retirement;return Outcome::Unsafe;}
            inputs={};o.flowOutput={};
            if(!o.flow.ReturnOutput(retirement)){reason=retirement;return Outcome::Unsafe;}
        }
        o.textureInputs={};o.unsafe=false;o.lastTexture=d;o.lastDerived=capture.deriveMotion;o.inputEpoch=epoch;
        if(delivered){output=std::move(o.residentOutput);o.textureOutput=output.lease;return Outcome::Delivered;}
        o.residentOutput={};return o.mainline.Preparing()?Outcome::Preparing:Outcome::Unavailable;
    }catch(...){o.unsafe=true;reason="Texture processing exception; owner retained";return Outcome::Unsafe;}
}
Outcome Processor::ProcessImpl(const Capture& c,std::vector<std::uint8_t>& output,std::string& reason,const SharedCapture* shared){
    output.clear();reason.clear();
    if(impl_&&impl_->textureMode){reason="Committed texture owner cannot switch to raw capture; retire and recreate it";return impl_->unsafe?Outcome::Unsafe:Outcome::Unavailable;}
    struct Timings {std::string& reason;double start=NowMs(),core=0,decodeRecord=-1,decodeWait=-1;bool reused=false;uint64_t allocations=0;~Timings(){try{
        const auto end=NowMs();reason+=" [guides "+std::to_string((core?core:end)-start)+" ms; mainline "+(core?std::to_string(end-core):"not entered")+(core?" ms]":"]");
        if(decodeRecord>=0)reason+=" [input decode record/submit "+std::to_string(decodeRecord)+" ms; producer/decode wait "+std::to_string(decodeWait)+" ms]";
        if(decodeRecord>=0)reason+=" [normalized textures: "+std::string(reused?"reused":"allocated")+"; pairs allocated="+std::to_string(allocations)+"; pool limit=2]";
    }catch(...){}}} timings{reason};
    if(impl_&&impl_->unsafe){reason="Native guide owner quarantined after uncertain work; restart required";return Outcome::Unsafe;}
    if(!Admit(c,reason,shared!=nullptr))return Outcome::Unavailable;
    if(!impl_)impl_=std::make_unique<Impl>();auto& o=*impl_;
    const auto failure=[&]{return o.unsafe?Outcome::Unsafe:Outcome::Unavailable;};
    try{
        if(!o.initialized&&!o.Initialize(c,reason))return failure();
        if(o.luid!=c.adapterLuid){reason="Native source adapter changed; owner restart required";return Outcome::Unavailable;}
        if(o.session==c.session&&o.generation==c.generation&&c.capture<=o.lastCapture){reason="Native capture is stale or replayed";return Outcome::Unavailable;}
        if(!o.Configure(c,reason))return failure();
        // Admission above requires a strictly newer capture in this episode.
        // Subtraction therefore cannot wrap, including at UINT64_MAX.
        const bool reset=c.reset||!o.lastCapture||c.capture-o.lastCapture!=1||o.depthInverted!=c.depthInverted||
            o.inputEpoch!=FinalFallback::CurrentInputEpoch();
        F::Input input;input.pair={c.capture,reset?0:o.lastCapture,c.session,c.generation};input.reset=reset;
        if(shared){
            if(!SharedDevice(c.adapterLuid,reason)||!shared->color||!shared->depth||!shared->output||!shared->inputFence||!shared->outputFence||
               !shared->value||shared->value==UINT64_MAX){reason="Shared native packet unavailable";return failure();}
            const std::array<ID3D12DeviceChild*,5> children{shared->color,shared->depth,shared->output,shared->inputFence,shared->outputFence};
            for(ID3D12DeviceChild* child:children){
                ComPtr<ID3D12Device> owner;if(FAILED(child->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(o.device.Get(),owner.Get()).equal){reason="Shared packet device mismatch";return failure();}}
            if(shared->inputFence->GetCompletedValue()==UINT64_MAX){reason="Shared Vulkan producer device lost";o.unsafe=true;return failure();}
            o.sharedColor=shared->color;o.sharedDepth=shared->depth;o.sharedOutput=shared->output;o.sharedInputFence=shared->inputFence;o.sharedOutputFence=shared->outputFence;
            if(shared->generated){ComPtr<ID3D12Device> owner;
                if(!shared->generatedReady||FAILED(shared->generated->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(o.device.Get(),owner.Get()).equal){reason="Shared FG output device mismatch";return failure();}
                o.sharedGenerated=shared->generated;
            }
            const auto decodeStart=NowMs();
            std::shared_ptr<Impl::Normalized> slot;
            for(auto& candidate:o.normalized){
                if(!candidate)candidate=std::make_shared<Impl::Normalized>();
                if(candidate.use_count()==1){slot=candidate;break;}
            }
            if(!slot){reason="Normalized capture slots remain leased by flow history";return failure();}
            timings.reused=slot->ready;
            if(!o.Begin(reason)||!o.pixels.Decode(o.list.Get(),shared->color,shared->depth,c.width,c.height,shared->depthBits,shared->bgra,slot->color,slot->depth,reason,slot->ready,shared->depthWidth,shared->depthHeight))return failure();
            if(!slot->ready)++o.normalizedAllocations;slot->ready=true;timings.allocations=o.normalizedAllocations;
            o.recordingColor=slot->color;o.recordingDepth=slot->depth;
            o.unsafe=true;
            if(FAILED(o.queue->Wait(shared->inputFence,shared->value))||!o.Submit(reason))return failure();
            const auto decodeSubmitted=NowMs();timings.decodeRecord=decodeSubmitted-decodeStart;
            if(!o.Wait(reason))return failure();timings.decodeWait=NowMs()-decodeSubmitted;
            o.unsafe=false;if(!o.pixels.Valid(reason))return failure();
            input.color=o.recordingColor;input.ownership=slot;input.producer={o.fence,o.timeline};
        }else if(!o.Upload(c,input,reason))return failure();
        if(shared&&shared->consumeGuides){
            for(unsigned i=0;i<2;++i){auto* resource=i?shared->guideMotion:shared->guideDepth;
                ComPtr<ID3D12Device> owner;
                if(!resource||FAILED(resource->GetDevice(IID_PPV_ARGS(&owner)))||!NativeIdentity::CompareDevices(o.device.Get(),owner.Get()).equal){reason="Guide export device missing or mismatched";return failure();}
                const auto desc=resource->GetDesc();
                if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=c.width||desc.Height!=c.height||desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1||desc.Format!=(i?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32_FLOAT)){
                    reason="Guide export shape or format refused";return failure();}
            }
        }
        const auto submission=o.flow.SubmitChecked(input,reason);
        if(submission!=F::SubmitDisposition::Submitted){o.unsafe=submission==F::SubmitDisposition::Unsafe;return failure();}
        o.unsafe=true;
        if(!o.flow.Wait(o.flowOutput,reason))return failure();
        P::Descriptor descriptor;descriptor.producer=Producer;descriptor.session=c.session;descriptor.stream=c.session;descriptor.device=c.adapterLuid;
        descriptor.generation=c.generation;descriptor.capture=c.capture;descriptor.previousCapture=input.pair.previousCapture;
        descriptor.color=descriptor.depth={c.width,c.height,0,0,c.width,c.height};descriptor.sourceApi=P::C::GraphicsApi::Vulkan;
        descriptor.depthReversed=c.depthInverted?1u:0u;descriptor.flags=P::ZeroJitterPolicy|P::HudIncluded;
        if(!F::ApplyCompletedFlow(o.flowOutput,descriptor)){reason="Native flow capture identity mismatch";return failure();}
        if(shared&&shared->consumeGuides){
            // Keep export aliases in the uncertain owner's retention set.
            o.sharedGuideDepth=shared->guideDepth;o.sharedGuideMotion=shared->guideMotion;
            if(!o.Begin(reason))return failure();
            for(unsigned i=0;i<2;++i){auto* source=i?o.flowOutput.motion.Get():o.recordingDepth.Get();auto* destination=i?shared->guideMotion:shared->guideDepth;
                GpuPixels::Transition(o.list.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
                GpuPixels::Transition(o.list.Get(),destination,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
                o.list->CopyResource(destination,source);
                GpuPixels::Transition(o.list.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                GpuPixels::Transition(o.list.Get(),destination,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            }
            if(!o.Submit(reason,shared->outputFence,shared->value))return failure();
            std::string retirement;
            if(!o.flow.AddReader({o.fence,o.timeline},retirement)||!o.Wait(reason)){reason+=" "+retirement;return failure();}
            const ExportedGuides facts{descriptor.capture,descriptor.previousCapture,descriptor.stream,descriptor.generation,
                bool(descriptor.depthReversed),bool(descriptor.flags&P::Reset)};
            o.flowOutput={};input.ownership.reset();
            if(!o.flow.ReturnOutput(retirement)){reason=retirement;return failure();}
            timings.core=NowMs();
            const auto delivered=shared->consumeGuides(facts,reason);
            if(delivered==Outcome::Unsafe)return Outcome::Unsafe;
            o.unsafe=false;o.lastCapture=c.capture;o.depthInverted=c.depthInverted;o.inputEpoch=FinalFallback::CurrentInputEpoch();
            o.sharedGuideDepth.Reset();o.sharedGuideMotion.Reset();
            reason+=" [completed depth + estimated pixel motion; Vulkan NR consumer; CPU pixel transfers=0]";
            return delivered;
        }
        nrpg::CpuMainlineClient::GpuInputs gpu;
        gpu.device=o.device;gpu.color=o.recordingColor;gpu.motion=o.flowOutput.motion;gpu.distrust=o.flowOutput.distrust;
        gpu.producer=o.flowOutput.producer.fence;gpu.value=o.flowOutput.producer.value;gpu.lease=o.flowOutput.ownership;
        nrpg::CpuMainlineClient::Completion reader;
        timings.core=NowMs();
        bool delivered=false;
        if(shared){gpu.depth=o.recordingDepth;delivered=o.mainline.ProcessResident(c.adapterLuid,descriptor,gpu,o.residentOutput,reason);reader=o.residentOutput.completion;}
        else delivered=o.mainline.ProcessGpu(c.adapterLuid,descriptor,gpu,c.depth,output,reader,reason);
        std::string retirement;
        if(reader.fence&&!o.flow.AddReader({reader.fence,reader.value},retirement)){reason=retirement;return failure();}
        if(o.mainline.RequiresRestart()){o.unsafe=true;return Outcome::Unsafe;}
        std::string fgReason;
        if(shared&&shared->generated&&delivered){
            NativeFg::FsrFrame frame{c.session,c.generation,c.capture,c.width,c.height,c.depthInverted,
                reset||o.flowOutput.sceneCut||o.flowOutput.warmup,c.deltaMs,o.residentOutput.color.Get(),
                o.recordingDepth.Get(),o.flowOutput.motion.Get(),reader.fence.Get(),reader.value};
            if(!o.framegen.Process(o.device.Get(),frame,o.generatedOutput,fgReason)&&o.framegen.Unsafe()){
                o.unsafe=true;reason=fgReason;return Outcome::Unsafe;
            }
            if(o.generatedOutput.completion&&!o.flow.AddReader({o.generatedOutput.completion,o.generatedOutput.value},retirement)){o.unsafe=true;reason=retirement;return failure();}
        }else if(!o.framegen.Close(fgReason)){o.unsafe=true;reason=fgReason;return Outcome::Unsafe;}
        o.flowOutput={};gpu={};
        if(!o.flow.ReturnOutput(retirement)){reason=retirement;return failure();}
        if(shared&&delivered){
            if(!o.Begin(reason)||!o.pixels.Encode(o.list.Get(),o.residentOutput.color.Get(),shared->output,shared->bgra,reason))return failure();
            if(o.generatedOutput.generated&&(!o.generatedOutput.color||(!o.generatedPixels.Ready()&&!o.generatedPixels.Initialize(o.device.Get(),reason))||!o.generatedPixels.Encode(o.list.Get(),o.generatedOutput.color.Get(),shared->generated,shared->bgra,reason)))return failure();
            if(!o.Submit(reason,shared->outputFence,shared->value)||!o.Wait(reason))return failure();
            if(shared->generatedReady)*shared->generatedReady=o.generatedOutput.generated;
            o.generatedOutput={};
            reason+=" [shared GPU pixels; full-frame CPU transfers=0]";
            if(!fgReason.empty())reason+=" ["+fgReason+"]";
        }
        o.residentOutput={};o.sharedColor.Reset();o.sharedDepth.Reset();o.sharedOutput.Reset();o.sharedGenerated.Reset();o.sharedInputFence.Reset();o.sharedOutputFence.Reset();
        o.unsafe=false;o.lastCapture=c.capture;o.depthInverted=c.depthInverted;o.inputEpoch=FinalFallback::CurrentInputEpoch();
        if(delivered)return Outcome::Delivered;
        return o.mainline.Preparing()?Outcome::Preparing:Outcome::Unavailable;
    }catch(...){o.unsafe=true;output.clear();reason="Native guide processing exception; owner retained";return Outcome::Unsafe;}
}
namespace {std::mutex processorMutex;Processor& GlobalProcessor(){static Processor processor;return processor;}}
Outcome Process(const Capture& c,std::vector<std::uint8_t>& output,std::string& reason){std::lock_guard lock(processorMutex);return GlobalProcessor().Process(c,output,reason);}
ID3D12Device* SharedDevice(std::uint64_t luid,std::string& reason){std::lock_guard lock(processorMutex);return GlobalProcessor().SharedDevice(luid,reason);}
Outcome ProcessShared(const Capture& c,const SharedCapture& shared,std::string& reason){std::lock_guard lock(processorMutex);return GlobalProcessor().ProcessShared(c,shared,reason);}
bool CanSwitchInput(std::string& reason){
    std::unique_lock lock(processorMutex,std::try_to_lock);
    if(!lock.owns_lock()){reason="Guide processor is busy";return false;}
    return GlobalProcessor().CanSwitchInput(reason);
}
}
#endif
