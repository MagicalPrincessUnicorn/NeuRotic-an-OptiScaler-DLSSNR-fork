#include "SharedGpuClient.h"
#include <nr/context/PreparedRasterBounds.h>
#include <dlssnr/PreparedGuideDispatch.h>
#include <dlssnr/PreparedContentProbe.h>
#include <dlssnr/PreparedVulkanDevice.h>
#include <nr/context/PreparedGuideNormalization.h>
#include <wrl/client.h>
#include <TlHelp32.h>
#include <limits>
#include <cstdio>
namespace nrpg {
namespace A=reshade::api;
namespace P=Neurotic::Feed::Prepared;
namespace T=DlssNr::PreparedTransport;
namespace V=DlssNr::PreparedVulkan;
namespace G=DlssNr::PreparedGuides;
using Microsoft::WRL::ComPtr;
namespace {
bool Refuse(std::string& why,const char* text){why=text;return false;}
DXGI_FORMAT DxFormat(A::format f){switch(f){
case A::format::r8g8b8a8_unorm:return DXGI_FORMAT_R8G8B8A8_UNORM;
case A::format::r16g16b16a16_float:return DXGI_FORMAT_R16G16B16A16_FLOAT;
case A::format::r32_float:return DXGI_FORMAT_R32_FLOAT;
case A::format::r16g16_float:return DXGI_FORMAT_R16G16_FLOAT;
case A::format::r32g32_float:return DXGI_FORMAT_R32G32_FLOAT;
case A::format::r8_unorm:return DXGI_FORMAT_R8_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
template<class Handle>Handle Native(std::uint64_t value){return (Handle)(std::uintptr_t)value;}
template<class Handle>std::uint64_t Bits(Handle value){return (std::uint64_t)(std::uintptr_t)value;}
T::Dispatch Functions(const V::DeviceInfo& v){T::Dispatch d;
    d.getProperties2=v.getProperties2;d.getImageFormatProperties2=v.getImageFormatProperties2;
    d.getExternalSemaphoreProperties=v.getExternalSemaphoreProperties;d.getMemoryProperties=v.getMemoryProperties;
    if(!v.getDeviceProcAddr)return d;
#define LOAD(field,name) d.field=reinterpret_cast<decltype(d.field)>(v.getDeviceProcAddr(v.device,name))
    LOAD(getHandleProperties,"vkGetMemoryWin32HandlePropertiesKHR");LOAD(createImage,"vkCreateImage");LOAD(destroyImage,"vkDestroyImage");
    LOAD(getImageMemoryRequirements,"vkGetImageMemoryRequirements");LOAD(allocateMemory,"vkAllocateMemory");LOAD(freeMemory,"vkFreeMemory");
    LOAD(bindImageMemory,"vkBindImageMemory");LOAD(createSemaphore,"vkCreateSemaphore");LOAD(destroySemaphore,"vkDestroySemaphore");
    LOAD(importSemaphore,"vkImportSemaphoreWin32HandleKHR");LOAD(barrier,"vkCmdPipelineBarrier");LOAD(copyImage,"vkCmdCopyImage");
#undef LOAD
    return d;
}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* image,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
}
bool ValidateSharedInput(const P::Descriptor& d,const std::array<A::resource_desc,4>& images,std::string& why){
    const auto normalized=Neurotic::Context::NormalizePreparedGuides(d);
    if(!normalized){why=normalized.reason;return false;}
    if(d.sourceApi!=P::C::GraphicsApi::Vulkan||d.colorDomain!=P::C::ColorDomain::EncodedDisplay||
       d.motionGridWidth!=1||d.motionGridHeight!=1)return Refuse(why,"Shared transport requires Vulkan SDR and a full-resolution motion grid");
    const auto w=d.color.width,h=d.color.height;
    if(!Neurotic::PreparedRaster::AdmitAxes(w,h))return Refuse(why,"Shared transport exceeds its bounded single-frame allocation");
    const std::array<P::Raster,4> rasters{d.color,d.depth,d.motion,d.distrust};
    const unsigned count=(d.flags&P::HasDistrust)?4:3;
    std::uint64_t bytes=0;
    for(unsigned i=0;i<count;++i){const auto& r=rasters[i];const auto& t=images[i];
        if(r.x||r.y||r.width!=w||r.height!=h||r.allocationWidth!=w||r.allocationHeight!=h||
           t.type!=A::resource_type::texture_2d||t.texture.width!=w||t.texture.height!=h||t.texture.levels!=1||
           t.texture.depth_or_layers!=1||t.texture.samples!=1)return Refuse(why,"Shared transport requires exact single-subresource raster coverage");
        const auto f=t.texture.format;
        if((i==0&&f!=A::format::r8g8b8a8_unorm&&f!=A::format::r16g16b16a16_float)||
           (i==1&&f!=A::format::r32_float)||(i==2&&f!=A::format::r16g16_float&&f!=A::format::r32g32_float)||
           (i==3&&f!=A::format::r8_unorm))return Refuse(why,"Shared image has an unsupported typed format");
        const unsigned bpp=i==3?1:(f==A::format::r16g16b16a16_float||f==A::format::r32g32_float?8:4);
        bytes+=std::uint64_t(w)*h*bpp;
    }
    if(bytes>256ull*1024*1024)return Refuse(why,"Shared transport byte bound exceeded");
    why.clear();return true;
}
struct SharedGpuClient::Impl {
    T::Session transport;ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    HMODULE module=nullptr;HANDLE event=nullptr;G::PrepareFn prepare=nullptr;G::RecordFn record=nullptr;G::SealFn seal=nullptr;V::QueryFn query=nullptr;
    A::device* sourceDevice=nullptr;A::command_queue* sourceQueue=nullptr;V::DeviceInfo facts;
    std::array<A::resource_desc,4> shapes{};P::Descriptor previous{};
    std::uint64_t inputValue=0,outputValue=0,completedCapture=0;bool initial=true,failed=false,modelReferenced=false;
    bool preparing=false,deliveryHistory=false;
    ComPtr<ID3D12Resource> readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};std::uint64_t readbackBytes=0;
    DlssNr::PreparedContent::Probe probe;DlssNr::PreparedContent::Bindings probeBindings{};
    std::uint64_t inputDigest=0,processedDigest=0,copybackDigest=0;unsigned diagnosticCaptures=0;bool previousTransportOnly=false;
    ~Impl(){if(event)CloseHandle(event);if(module)FreeLibrary(module);}
    bool Bind(std::string& why){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        if(snapshot==INVALID_HANDLE_VALUE)return Refuse(why,"Loaded core module inventory unavailable");
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
        for(BOOL next=Module32FirstW(snapshot,&entry);next;next=Module32NextW(snapshot,&entry)){
            auto p=reinterpret_cast<G::PrepareFn>(GetProcAddress(entry.hModule,"NeuRotic_PrepareGuidesV1"));
            auto r=reinterpret_cast<G::RecordFn>(GetProcAddress(entry.hModule,"NeuRotic_RecordGuidesV1"));
            auto s=reinterpret_cast<G::SealFn>(GetProcAddress(entry.hModule,"NeuRotic_SealGuidesV1"));
            auto q=reinterpret_cast<V::QueryFn>(GetProcAddress(entry.hModule,"NeuRotic_QueryPreparedVulkanV1"));
            if(p&&r&&s&&q&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(r),&module)){
                prepare=p;record=r;seal=s;query=q;found=true;break;}}
        CloseHandle(snapshot);return found||Refuse(why,"Loaded NeuRotic core lacks the prepared Vulkan ABI");
    }
    bool Query(A::command_queue* source,V::DeviceInfo& out,std::string& why){
        if(!source||!source->get_device()||source->get_device()->get_api()!=A::device_api::vulkan)return Refuse(why,"Shared caller is not a Vulkan queue");
        out.device=Native<VkDevice>(source->get_device()->get_native());out.queue=Native<VkQueue>(source->get_native());
        if(!query(&out)||!out.physical||!out.generation||!(out.queueFlags&VK_QUEUE_GRAPHICS_BIT)||
           !out.externalMemoryWin32||!out.externalSemaphoreWin32||!out.timeline)return Refuse(why,"Existing Vulkan device/queue sharing enablement not proven");
        return true;
    }
    bool Initialize(A::command_queue* source,const std::array<A::resource_desc,4>& images,unsigned count,std::string& why){
        if(!Bind(why)||!Query(source,facts,why))return false;
        sourceDevice=source->get_device();sourceQueue=source;shapes=images;
        const auto functions=Functions(facts);ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return Refuse(why,"DXGI factory creation refused");
        const auto matched=T::MatchAdapter(facts.physical,functions,factory.Get(),&adapter);
        if(!matched){why=matched.reason;return false;}
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d12))))return Refuse(why,"Matched-adapter D3D12 creation refused");
        std::array<T::ImageSpec,4> specs{};
        for(unsigned i=0;i<count;++i)specs[i]={images[i].texture.width,images[i].texture.height,DxFormat(images[i].texture.format),i==0};
        auto made=transport.Initialize(facts.physical,facts.device,facts.queue,facts.family,d12.Get(),functions,{true,true,true},std::span(specs).first(count));
        if(!made){why=made.reason;return false;}
        D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(d12->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)))||
           FAILED(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
           FAILED(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||
           FAILED(list->Close()))return Refuse(why,"Private prepared recording owner creation refused");
        // Install canonical recording/submission hooks before the first Reset.
        if(!prepare(list.Get()))return Refuse(why,"Core recording preparation refused");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);return event!=nullptr||Refuse(why,"Shared completion event unavailable");
    }
    bool Wait(ID3D12Fence* fence,std::uint64_t value,std::string& why){
        auto completed=fence->GetCompletedValue();
        if(completed==UINT64_MAX)return Refuse(why,"Shared device removed; completion unknown");
        if(completed>=value)return true;
        ResetEvent(event);
        if(FAILED(fence->SetEventOnCompletion(value,event))||WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)
            return Refuse(why,"Shared GPU completion not proven within the bounded wait");
        completed=fence->GetCompletedValue();return (completed!=UINT64_MAX&&completed>=value)||Refuse(why,"Shared fence woke without GPU completion");
    }
    bool ReadbackRecording(std::string& why){
        auto* image=transport.Image(0).resource;
        if(!readback){const auto texture=image->GetDesc();
            d12->GetCopyableFootprints(&texture,0,1,0,&footprint,nullptr,nullptr,&readbackBytes);
            if(!readbackBytes||readbackBytes>256ull*1024*1024)return Refuse(why,"Diagnostic readback bound exceeded");
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=readbackBytes;
            buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
            if(FAILED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))
                return Refuse(why,"Diagnostic readback allocation refused");}
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        Transition(list.Get(),image,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Transition(list.Get(),image,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);return true;
    }
    bool Observe(const P::Descriptor& d,DlssNr::PreparedContent::Stage stage,std::string& why){
        namespace C=DlssNr::PreparedContent;void* ptr=nullptr;const D3D12_RANGE reading{0,static_cast<SIZE_T>(readbackBytes)};
        if(!readback||FAILED(readback->Map(0,&reading,&ptr))||!ptr)return Refuse(why,"Completed diagnostic readback map refused");
        const unsigned pixelBytes=shapes[0].texture.format==A::format::r16g16b16a16_float?8:4;
        C::View view{{static_cast<const std::byte*>(ptr)+footprint.Offset,static_cast<std::size_t>(readbackBytes-footprint.Offset)},
            footprint.Footprint.RowPitch,d.color.width,d.color.height,pixelBytes,static_cast<std::uint32_t>(shapes[0].texture.format),true};
        auto report=probe.Observe({P::Ipc::IdentityOf(d),stage,probeBindings[static_cast<std::size_t>(stage)],d.maskCapture,view,C::Evidence::CompletedReadback});
        const D3D12_RANGE written{0,0};readback->Unmap(0,&written);
        if(stage==C::Stage::Capture)inputDigest=report.digest;
        if(stage==C::Stage::Processed)processedDigest=report.digest;
        if(stage==C::Stage::CopyBack)copybackDigest=report.digest;
        if(diagnosticCaptures<16)std::fprintf(stderr,"shared-probe device=%llu generation=%llu capture=%llu stage=%u hash=%llu sampled=%u accepted=%u code=%u displayed=false\n",
            static_cast<unsigned long long>(d.device),static_cast<unsigned long long>(d.generation),static_cast<unsigned long long>(d.capture),
            static_cast<unsigned>(stage),static_cast<unsigned long long>(report.digest),report.sampleBytes,unsigned(report.accepted),static_cast<unsigned>(report.code));
        return report.accepted||Refuse(why,"Actual shared roundtrip content probe refused");
    }
};
SharedGpuClient::SharedGpuClient()=default;
SharedGpuClient::~SharedGpuClient(){
    // Retain the entire bounded packet, including code/module and D3D12 owners,
    // when canonical provider/replay release has not been explicitly supplied.
    if(impl_&&impl_->transport.PendingOwnership())(void)impl_.release();
}
bool SharedGpuClient::RequiresRestart()const{return impl_&&impl_->failed;}
bool SharedGpuClient::Preparing()const{return impl_&&impl_->preparing;}
std::uint64_t SharedGpuClient::CompletedCapture()const{return impl_?impl_->completedCapture:0;}
std::uint64_t SharedGpuClient::CompletionValue()const{return impl_&&impl_->completedCapture?impl_->outputValue:0;}
std::uint64_t SharedGpuClient::InputDigest()const{return impl_?impl_->inputDigest:0;}
std::uint64_t SharedGpuClient::ProcessedDigest()const{return impl_?impl_->processedDigest:0;}
std::uint64_t SharedGpuClient::CopybackDigest()const{return impl_?impl_->copybackDigest:0;}
bool SharedGpuClient::Retire(const T::DrainEvidence& evidence,std::string& why){
    if(!impl_)return true;auto result=impl_->transport.Retire(evidence);
    if(!result){why=result.reason;return false;}impl_.reset();why.clear();return true;
}
bool SharedGpuClient::Process(A::command_queue* source,const P::Descriptor& description,const std::array<A::resource,4>& images,std::string& why,bool transportOnly){
    if(impl_)impl_->preparing=false;
    if(RequiresRestart())return Refuse(why,"Shared slot quarantined; owner restart required");
    if(!source||!source->get_device())return Refuse(why,"Shared source queue unavailable");
    const unsigned count=(description.flags&P::HasDistrust)?4:3;std::array<A::resource_desc,4> shapes{};
    for(unsigned i=0;i<count;++i){if(!images[i].handle)return Refuse(why,"Shared source image missing");shapes[i]=source->get_device()->get_resource_desc(images[i]);
        for(unsigned j=0;j<i;++j)if(images[i].handle==images[j].handle)return Refuse(why,"Shared source image roles alias");}
    if(!ValidateSharedInput(description,shapes,why))return false;
    if(!impl_){auto next=std::make_unique<Impl>();if(!next->Initialize(source,shapes,count,why))return false;impl_=std::move(next);}
    auto& o=*impl_;V::DeviceInfo current;
    if(source!=o.sourceQueue||source->get_device()!=o.sourceDevice||!o.Query(source,current,why)||
       current.device!=o.facts.device||current.queue!=o.facts.queue||current.physical!=o.facts.physical||current.generation!=o.facts.generation||
       current.family!=o.facts.family){o.failed=true;return Refuse(why,"Shared device/queue generation changed");}
    if(count!=o.transport.Count()){o.failed=true;return Refuse(why,"Shared image-role shape changed; explicit retirement required");}
    for(unsigned i=0;i<count;++i)if(shapes[i].texture.width!=o.shapes[i].texture.width||shapes[i].texture.height!=o.shapes[i].texture.height||
        shapes[i].texture.format!=o.shapes[i].texture.format){o.failed=true;return Refuse(why,"Shared image allocation changed; explicit retirement required");}
    P::Descriptor prepared=description;
    if(o.previous.capture){
        const bool episode=o.previous.producer==prepared.producer&&o.previous.session==prepared.session&&o.previous.stream==prepared.stream&&
            o.previous.device==prepared.device&&o.previous.generation==prepared.generation;
        if(episode&&prepared.capture<=o.previous.capture)return Refuse(why,"Shared capture is stale or replayed");
        if(!o.deliveryHistory||!Neurotic::Context::PreparedHistoryContinuous(o.previous,prepared)||o.previousTransportOnly!=transportOnly){prepared.flags|=P::Reset;prepared.previousCapture=0;}
    }else{prepared.flags|=P::Reset;prepared.previousCapture=0;}
    auto* commands=source->get_immediate_command_list();if(!commands)return Refuse(why,"Serialized graphics immediate list unavailable");
    if(commands->get_device()!=o.sourceDevice)return Refuse(why,"Source immediate list device mismatch");
    const auto cmd=Native<VkCommandBuffer>(commands->get_native());if(!cmd)return Refuse(why,"Source Vulkan recording unavailable");
    if(o.inputValue>UINT64_MAX-3||o.outputValue>UINT64_MAX-3){o.failed=true;return Refuse(why,"Shared timeline exhausted");}
    o.completedCapture=o.inputDigest=o.processedDigest=o.copybackDigest=0;o.failed=true; // Every subsequent early return quarantines recorded references.
    for(unsigned i=0;i<count;++i){auto acquired=o.transport.Acquire(cmd,i,o.initial);if(!acquired){why=acquired.reason;return false;}
        const auto state=i?A::resource_usage::shader_resource:A::resource_usage::render_target;
        commands->barrier(images[i],state,A::resource_usage::copy_source);
        const auto copied=o.transport.CopyFrom(cmd,i,Native<VkImage>(images[i].handle),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,prepared.color.width,prepared.color.height);
        commands->barrier(images[i],A::resource_usage::copy_source,state);
        if(!copied){why=copied.reason;return false;}const auto released=o.transport.Release(cmd,i);if(!released){why=released.reason;return false;}}
    o.initial=false;source->flush_immediate_command_list();
    const auto input=++o.inputValue;
    if(!source->signal(A::fence{Bits(o.transport.InputSemaphore())},input))return Refuse(why,"Source shared timeline signal refused");
    // A real source completion also protects caller-owned input lifetimes before
    // an optional provider call. Do not enqueue a game wait on uncertain NR work.
    if(!o.Wait(o.transport.InputFence(),input,why))return false;
    if(FAILED(o.queue->Wait(o.transport.InputFence(),input))||FAILED(o.allocator->Reset())||FAILED(o.list->Reset(o.allocator.Get(),nullptr)))
        return Refuse(why,"Private shared recording reset/wait refused");
    G::Result recorded=G::Result::Refused;
    if(transportOnly){if(!o.ReadbackRecording(why))return false;}
    else{
        for(unsigned i=0;i<count;++i)Transition(o.list.Get(),o.transport.Image(i).resource,D3D12_RESOURCE_STATE_COMMON,
            i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        G::Dispatch request;request.source=prepared;request.list=o.list.Get();request.queue=o.queue.Get();
        request.color=o.transport.Image(0).resource;request.depth=o.transport.Image(1).resource;request.motion=o.transport.Image(2).resource;
        request.distrust=count==4?o.transport.Image(3).resource:nullptr;request.workWidth=prepared.color.width;request.workHeight=prepared.color.height;
        o.modelReferenced=true;recorded=static_cast<G::Result>(o.record(&request));
        if(recorded==G::Result::Recorded||recorded==G::Result::Preparing)for(unsigned i=0;i<count;++i)Transition(o.list.Get(),o.transport.Image(i).resource,
            i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    }
    if(FAILED(o.list->Close()))return Refuse(why,"Private shared recording close refused");
    ID3D12CommandList* submitted=o.list.Get();o.queue->ExecuteCommandLists(1,&submitted);
    const bool sealed=transportOnly||o.seal(o.list.Get())!=0;const auto output=++o.outputValue;
    if(FAILED(o.queue->Signal(o.transport.OutputFence(),output)))return Refuse(why,"Private shared completion signal refused");
    if(!o.Wait(o.transport.OutputFence(),output,why))return false;
    if(!sealed||(!transportOnly&&recorded!=G::Result::Recorded&&recorded!=G::Result::Preparing))return Refuse(why,"Prepared model recording/submission ownership refused; completed slot retained");
    if(!transportOnly&&recorded==G::Result::Preparing){
        if(!source->wait(A::fence{Bits(o.transport.OutputSemaphore())},output))return Refuse(why,"Source completed-initialization timeline wait refused");
        o.preparing=true;o.deliveryHistory=false;o.previous=prepared;o.previousTransportOnly=false;o.failed=false;
        return Refuse(why,"Mainline model lifecycle preparation completed; waiting for the next capture");
    }
    if(transportOnly){namespace C=DlssNr::PreparedContent;
        o.probeBindings={};o.probeBindings[0]={Bits(o.facts.device),o.facts.generation,images[0].handle};
        o.probeBindings[1]=o.probeBindings[2]={reinterpret_cast<std::uintptr_t>(o.d12.Get()),1,reinterpret_cast<std::uintptr_t>(o.transport.Image(0).resource)};
        o.probeBindings[3]=o.probeBindings[4]=o.probeBindings[0];
        if(!o.probe.Begin(prepared,o.probeBindings,C::Mode::Processed).accepted||!o.Observe(prepared,C::Stage::Capture,why)||
           !o.Observe(prepared,C::Stage::Prepared,why)||!o.Observe(prepared,C::Stage::Processed,why)||o.inputDigest!=o.processedDigest)
            return Refuse(why,"Completed transport-only D3D12 content mismatch");
    }
    if(!source->wait(A::fence{Bits(o.transport.OutputSemaphore())},output))return Refuse(why,"Source completed-output timeline wait refused");
    commands=source->get_immediate_command_list();
    if(!commands||commands->get_device()!=o.sourceDevice)return Refuse(why,"Copyback recording owner unavailable");
    const auto backCmd=Native<VkCommandBuffer>(commands->get_native());
    auto acquired=o.transport.Acquire(backCmd,0);if(!acquired){why=acquired.reason;return false;}
    commands->barrier(images[0],A::resource_usage::render_target,A::resource_usage::copy_dest);
    auto copied=o.transport.CopyTo(backCmd,0,Native<VkImage>(images[0].handle),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,prepared.color.width,prepared.color.height);
    commands->barrier(images[0],A::resource_usage::copy_dest,A::resource_usage::render_target);
    if(!copied){why=copied.reason;return false;}auto released=o.transport.Release(backCmd,0);if(!released){why=released.reason;return false;}
    source->flush_immediate_command_list();const auto copyback=++o.inputValue;
    if(!source->signal(A::fence{Bits(o.transport.InputSemaphore())},copyback))return Refuse(why,"Copyback shared timeline signal refused");
    if(!o.Wait(o.transport.InputFence(),copyback,why))return false;
    if(transportOnly){
        // Read the actual copyback target, not the already-shared source. Reuse
        // the same transport image only after its prior queue work completed.
        commands=source->get_immediate_command_list();if(!commands||commands->get_device()!=o.sourceDevice)return Refuse(why,"Copyback probe source owner unavailable");
        const auto probeCmd=Native<VkCommandBuffer>(commands->get_native());
        acquired=o.transport.Acquire(probeCmd,0);if(!acquired){why=acquired.reason;return false;}
        commands->barrier(images[0],A::resource_usage::render_target,A::resource_usage::copy_source);
        copied=o.transport.CopyFrom(probeCmd,0,Native<VkImage>(images[0].handle),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,prepared.color.width,prepared.color.height);
        commands->barrier(images[0],A::resource_usage::copy_source,A::resource_usage::render_target);
        if(!copied){why=copied.reason;return false;}released=o.transport.Release(probeCmd,0);if(!released){why=released.reason;return false;}
        source->flush_immediate_command_list();const auto probeInput=++o.inputValue;
        if(!source->signal(A::fence{Bits(o.transport.InputSemaphore())},probeInput)||!o.Wait(o.transport.InputFence(),probeInput,why))return false;
        if(FAILED(o.queue->Wait(o.transport.InputFence(),probeInput))||FAILED(o.allocator->Reset())||FAILED(o.list->Reset(o.allocator.Get(),nullptr))||
            !o.ReadbackRecording(why)||FAILED(o.list->Close()))return Refuse(why,"Copyback probe D3D12 recording refused");
        submitted=o.list.Get();o.queue->ExecuteCommandLists(1,&submitted);const auto probeOutput=++o.outputValue;
        if(FAILED(o.queue->Signal(o.transport.OutputFence(),probeOutput))||!o.Wait(o.transport.OutputFence(),probeOutput,why))return false;
        if(!o.Observe(prepared,DlssNr::PreparedContent::Stage::CopyBack,why))return false;++o.diagnosticCaptures;
    }
    o.previous=prepared;o.previousTransportOnly=transportOnly;o.completedCapture=prepared.capture;o.failed=false;o.deliveryHistory=true;why.clear();return true;
}
}
