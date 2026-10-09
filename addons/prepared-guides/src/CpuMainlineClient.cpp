#include "CpuMainlineClient.h"
#include <dlssnr/PreparedGuideDispatch.h>
#include <dlssnr/NativeIdentity.h>
#include <dlssnr/NativeGuideTiming.h>
#include <dlssnr/connections/GpuBudget.h>
#include <nr/context/PreparedGuideNormalization.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <TlHelp32.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace nrpg {
namespace P=Neurotic::Feed::Prepared;
namespace G=DlssNr::PreparedGuides;
using Microsoft::WRL::ComPtr;
namespace {
constexpr std::uint64_t MaxBytes=160ull*1024*1024;
bool Refuse(std::string& reason,const char* message){reason=message;return false;}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* image,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
float ReadFloat(const std::byte* src){float value;std::memcpy(&value,src,4);return value;}
bool Admit(const P::Descriptor& d,std::span<const std::byte> packed,std::string& reason){
    const auto normalized=Neurotic::Context::NormalizePreparedGuides(d);
    if(!normalized){reason=normalized.reason;return false;}
    const auto w=d.color.width,h=d.color.height;const auto n=std::uint64_t(w)*h;
    if(w>4096||h>2160||n>MaxBytes/20||packed.size()!=n*20)
        return Refuse(reason,"CPU prepared payload size or allocation bound refused");
    if(d.colorDomain!=P::C::ColorDomain::EncodedDisplay||d.motionUnits!=P::C::MotionUnits::Pixels||d.motionGridWidth!=1||d.motionGridHeight!=1)
        return Refuse(reason,"CPU prepared capture requires SDR and full-resolution pixel motion");
    const std::array<P::Raster,4> rasters{d.color,d.depth,d.motion,d.distrust};
    for(unsigned i=0;i<((d.flags&P::HasDistrust)?4u:3u);++i){const auto& r=rasters[i];
        if(r.x||r.y||r.width!=w||r.height!=h||r.allocationWidth!=w||r.allocationHeight!=h)
            return Refuse(reason,"CPU prepared capture requires exact single-subresource coverage");}
    for(std::size_t i=0;i<n;++i){
        const float depth=ReadFloat(packed.data()+n*4+i*4),mx=ReadFloat(packed.data()+n*8+i*8),my=ReadFloat(packed.data()+n*8+i*8+4),mask=ReadFloat(packed.data()+n*16+i*4);
        if(!std::isfinite(depth)||!std::isfinite(mx)||!std::isfinite(my)||!std::isfinite(mask)||mask<0||mask>1)
            return Refuse(reason,"CPU prepared payload contains invalid depth, motion or distrust samples");
    }
    return true;
}
bool AdmitGpu(std::uint64_t luid,const P::Descriptor& d,const CpuMainlineClient::GpuInputs& g,
              std::span<const float> depth,std::string& reason){
    const auto normalized=Neurotic::Context::NormalizePreparedGuides(d);
    if(!normalized){reason=normalized.reason;return false;}
    const auto w=d.color.width,h=d.color.height;const auto n=std::uint64_t(w)*h;
    if(!w||!h||w>4096||h>2160||n>MaxBytes/20||(!g.depth&&depth.size()!=n)||(g.depth&&!depth.empty())||!g.device||!g.color||!g.motion||!g.distrust||
       !g.lease||!g.producer||!g.value||g.value==UINT64_MAX)return Refuse(reason,"GPU prepared inputs or depth span unavailable");
    const auto actual=g.device->GetAdapterLuid();std::uint64_t identity=0;std::memcpy(&identity,&actual,8);
    if(identity!=luid)return Refuse(reason,"GPU prepared adapter identity mismatch");
    if(d.colorDomain!=P::C::ColorDomain::EncodedDisplay||d.motionUnits!=P::C::MotionUnits::Pixels||
       d.motionGridWidth!=1||d.motionGridHeight!=1||!(d.flags&P::HasDistrust))return Refuse(reason,"GPU prepared input semantics unsupported");
    for(const auto& r:{d.color,d.depth,d.motion,d.distrust})if(r.x||r.y||r.width!=w||r.height!=h||r.allocationWidth!=w||r.allocationHeight!=h)
        return Refuse(reason,"GPU prepared input coverage mismatch");
    for(float v:depth)if(!std::isfinite(v))return Refuse(reason,"GPU prepared depth contains nonfinite samples");
    const std::array<ID3D12DeviceChild*,4> children{g.color.Get(),g.motion.Get(),g.distrust.Get(),g.producer.Get()};
    for(auto* child:children){ComPtr<ID3D12Device> owner;
        if(FAILED(child->GetDevice(IID_PPV_ARGS(&owner)))||!DlssNr::NativeIdentity::CompareDevices(g.device.Get(),owner.Get()).equal)
            return Refuse(reason,"GPU prepared resource or producer belongs to another device");}
    const std::array<ID3D12Resource*,3> images{g.color.Get(),g.motion.Get(),g.distrust.Get()};
    const std::array<DXGI_FORMAT,3> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM};
    for(unsigned i=0;i<images.size();++i){const auto r=images[i]->GetDesc();
        if(r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||r.Width!=w||r.Height!=h||r.DepthOrArraySize!=1||r.MipLevels!=1||
           r.SampleDesc.Count!=1||r.Format!=formats[i]||(r.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
            return Refuse(reason,"GPU prepared resource shape or format mismatch");}
    if(g.depth){ComPtr<ID3D12Device> owner;const auto r=g.depth->GetDesc();
        if(FAILED(g.depth->GetDevice(IID_PPV_ARGS(&owner)))||!DlssNr::NativeIdentity::CompareDevices(g.device.Get(),owner.Get()).equal||
           r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||r.Width!=w||r.Height!=h||r.DepthOrArraySize!=1||r.MipLevels!=1||
           r.SampleDesc.Count!=1||r.Format!=DXGI_FORMAT_R32_FLOAT||(r.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
            return Refuse(reason,"GPU prepared depth ownership/shape mismatch");}
    const auto completed=g.producer->GetCompletedValue();
    return (completed!=UINT64_MAX&&completed>=g.value)||Refuse(reason,"GPU prepared producer completion not established");
}
}
struct CpuMainlineClient::Impl {
    std::vector<std::shared_ptr<DlssNr::Connections::GpuBudget::Reservation>> allocations;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    std::array<ComPtr<ID3D12Resource>,4> images;
    ComPtr<ID3D12Resource> upload,readback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> footprints{};
    HMODULE module=nullptr;HANDLE event=nullptr;
    G::PrepareFn prepare=nullptr;G::RecordFn record=nullptr;G::SealFn seal=nullptr;G::ReasonFn describe=nullptr;
    std::uint64_t luid=0,value=0,uploadBytes=0,readbackBytes=0;
    unsigned width=0,height=0;
    bool initialized=false,initialImages=true,failed=false,uncertain=false,modulePinned=false;
    bool preparing=false,deliveryHistory=false;
    P::Descriptor previous{};
    GpuInputs retained;
    bool gpuMode=false,residentMode=false;
    std::weak_ptr<void> outputLease;
    ~Impl(){if(event)CloseHandle(event);if(module)FreeLibrary(module);}
    bool Bind(std::string& reason){
        if(prepare&&record&&seal)return true;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        if(snapshot==INVALID_HANDLE_VALUE)return Refuse(reason,"Loaded core module inventory unavailable");
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
        for(BOOL next=Module32FirstW(snapshot,&entry);next;next=Module32NextW(snapshot,&entry)){
            auto p=reinterpret_cast<G::PrepareFn>(GetProcAddress(entry.hModule,"NeuRotic_PrepareGuidesV1"));
            auto r=reinterpret_cast<G::RecordFn>(GetProcAddress(entry.hModule,"NeuRotic_RecordGuidesV1"));
            auto s=reinterpret_cast<G::SealFn>(GetProcAddress(entry.hModule,"NeuRotic_SealGuidesV1"));
            if(p&&r&&s&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(r),&module)){
                prepare=p;record=r;seal=s;describe=reinterpret_cast<G::ReasonFn>(GetProcAddress(entry.hModule,"NeuRotic_LastGuidesReasonV1"));found=true;break;}}
        CloseHandle(snapshot);return found||Refuse(reason,"Loaded NeuRotic core lacks the prepared mainline ABI");
    }
    bool Initialize(std::uint64_t adapterLuid,std::string& reason,ID3D12Device* supplied=nullptr){
        if(!Bind(reason))return false;
        if(supplied)device=supplied;
        else {
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;LUID identity{};std::memcpy(&identity,&adapterLuid,8);
        if(!adapterLuid||FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(identity,IID_PPV_ARGS(&adapter))))
            return Refuse(reason,"CPU prepared source adapter LUID unavailable or unmatched");
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))
            return Refuse(reason,"Matched-adapter D3D12 creation refused");
        }
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)))||
           FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||
           FAILED(list->Close())||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))
            return Refuse(reason,"CPU prepared private recording owner creation refused");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return Refuse(reason,"CPU prepared completion event unavailable");
        luid=adapterLuid;initialized=true;return true;
    }
    bool Buffer(D3D12_HEAP_TYPE type,std::uint64_t size,ComPtr<ID3D12Resource>& result){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        auto charged=DlssNr::Connections::GpuBudget::Reserve(DlssNr::Connections::GpuBudget::TextureBytes(device.Get(),desc));if(!charged)return false;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result))))return false;
        allocations.push_back(std::move(charged));return true;
    }
    bool Allocate(unsigned w,unsigned h,std::string& reason,bool gpu=false,bool resident=false){
        if(!DlssNr::Connections::GpuBudget::AdmitRaster(w,h))return Refuse(reason,"Prepared raster exceeds allocation contract");
        if(width==w&&height==h&&images[0]&&gpuMode==gpu&&residentMode==resident)return true;
        // Reached only before submission or after proven completion. Core owns
        // any additional provider references through RetainDerivedUse.
        images={};upload.Reset();readback.Reset();allocations.clear();width=height=0;uploadBytes=0;initialImages=true;
        const std::array<DXGI_FORMAT,4> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R8_UNORM};
        for(unsigned i=0;i<(resident?1u:gpu?2u:4u);++i){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=formats[i];
            desc.Flags=i?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            auto charged=DlssNr::Connections::GpuBudget::Reserve(DlssNr::Connections::GpuBudget::TextureBytes(device.Get(),desc));if(!charged)return Refuse(reason,"Prepared aggregate GPU allocation budget exhausted");
            if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&images[i]))))
                return Refuse(reason,"CPU prepared owned texture allocation refused");
            allocations.push_back(std::move(charged));
            UINT64 bytes=0;device->GetCopyableFootprints(&desc,0,1,0,&footprints[i],nullptr,nullptr,&bytes);
            if(!bytes||bytes>MaxBytes)return Refuse(reason,"CPU prepared texture footprint refused");
            uploadBytes=(uploadBytes+D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT-1)&~std::uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT-1);
            footprints[i].Offset=uploadBytes;if(!gpu||i)uploadBytes+=bytes;if(i==0)readbackBytes=bytes;
        }
        if(!resident&&(uploadBytes>MaxBytes||!Buffer(D3D12_HEAP_TYPE_UPLOAD,uploadBytes,upload)||!Buffer(D3D12_HEAP_TYPE_READBACK,readbackBytes,readback)))
            return Refuse(reason,"CPU prepared staging allocation refused");
        width=w;height=h;gpuMode=gpu;residentMode=resident;return true;
    }
    bool UploadDepth(std::span<const float> depth,std::string& reason){
        void* ptr=nullptr;const D3D12_RANGE none{0,0};
        if(FAILED(upload->Map(0,&none,&ptr))||!ptr)return Refuse(reason,"GPU prepared depth upload mapping refused");
        for(unsigned y=0;y<height;++y)std::memcpy(static_cast<std::byte*>(ptr)+footprints[1].Offset+std::size_t(y)*footprints[1].Footprint.RowPitch,
            depth.data()+std::size_t(y)*width,std::size_t(width)*4);
        const D3D12_RANGE written{0,static_cast<SIZE_T>(uploadBytes)};upload->Unmap(0,&written);return true;
    }
    bool Upload(std::span<const std::byte> packed,std::string& reason){
        void* ptr=nullptr;const D3D12_RANGE nothing{0,0};
        if(FAILED(upload->Map(0,&nothing,&ptr))||!ptr)return Refuse(reason,"CPU prepared upload mapping refused");
        auto* dst=static_cast<std::byte*>(ptr);const auto n=std::size_t(width)*height;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto pixel=std::size_t(y)*width+x;
            auto* color=dst+footprints[0].Offset+std::size_t(y)*footprints[0].Footprint.RowPitch+x*4;
            color[0]=packed[pixel*4+2];color[1]=packed[pixel*4+1];color[2]=packed[pixel*4];color[3]=packed[pixel*4+3];
            std::memcpy(dst+footprints[1].Offset+std::size_t(y)*footprints[1].Footprint.RowPitch+x*4,packed.data()+n*4+pixel*4,4);
            std::memcpy(dst+footprints[2].Offset+std::size_t(y)*footprints[2].Footprint.RowPitch+x*8,packed.data()+n*8+pixel*8,8);
            dst[footprints[3].Offset+std::size_t(y)*footprints[3].Footprint.RowPitch+x]=std::byte(static_cast<unsigned char>(ReadFloat(packed.data()+n*16+pixel*4)*255.f+.5f));
        }
        const D3D12_RANGE written{0,static_cast<SIZE_T>(uploadBytes)};upload->Unmap(0,&written);return true;
    }
    bool Wait(std::string& reason){
        auto completed=fence->GetCompletedValue();if(completed==UINT64_MAX)return Refuse(reason,"CPU prepared device removed; completion unknown");
        if(completed>=value)return true;
        ResetEvent(event);
        if(FAILED(fence->SetEventOnCompletion(value,event))||WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)
            return Refuse(reason,"CPU prepared GPU completion not proven within bounded wait");
        completed=fence->GetCompletedValue();return (completed!=UINT64_MAX&&completed>=value)||Refuse(reason,"CPU prepared fence woke without completion");
    }
};
CpuMainlineClient::CpuMainlineClient()=default;
CpuMainlineClient::~CpuMainlineClient(){if(impl_&&impl_->uncertain)(void)impl_.release();}
#ifdef NRPG_CPU_MAINLINE_TESTING
CpuMainlineClient::CpuMainlineClient(TestRecorder recorder):impl_(std::make_unique<Impl>()){
    impl_->prepare=recorder.prepare;impl_->record=recorder.record;impl_->seal=recorder.seal;impl_->describe=recorder.reason;
}
#endif
bool CpuMainlineClient::RequiresRestart()const{return impl_&&impl_->failed;}
bool CpuMainlineClient::Preparing()const{return impl_&&impl_->preparing;}
bool CpuMainlineClient::ProcessResident(std::uint64_t luid,const P::Descriptor& d,const GpuInputs& input,GpuOutput& output,std::string& reason){
    output={};if(!input.depth)return Refuse(reason,"Resident depth unavailable");
    std::vector<std::uint8_t> unused;Completion reader;
    const bool delivered=ProcessImpl(luid,d,{},&input,{},unused,&reader,reason,&output);
    output.completion=reader;return delivered;
}
bool CpuMainlineClient::Process(std::uint64_t adapterLuid,const P::Descriptor& description,
    std::span<const std::byte> packed,std::vector<std::uint8_t>& output,std::string& reason){
    return ProcessImpl(adapterLuid,description,packed,nullptr,{},output,nullptr,reason);
}
bool CpuMainlineClient::ProcessGpu(std::uint64_t adapterLuid,const P::Descriptor& description,const GpuInputs& input,
    std::span<const float> depth,std::vector<std::uint8_t>& output,Completion& completion,std::string& reason){
    completion={};return ProcessImpl(adapterLuid,description,{},&input,depth,output,&completion,reason);
}
bool CpuMainlineClient::ProcessImpl(std::uint64_t adapterLuid,const P::Descriptor& description,
    std::span<const std::byte> packed,const GpuInputs* gpu,std::span<const float> depth,
    std::vector<std::uint8_t>& output,Completion* completion,std::string& reason,GpuOutput* resident){
    output.clear();reason.clear();
    struct Timings {
        std::string& reason;bool gpu;double begin=DlssNr::NativeGuides::NowMs(),record=0,wait=0,readback=0;
        ~Timings(){if(!gpu)return;try {const double end=DlssNr::NativeGuides::NowMs();char text[192]{};
            std::snprintf(text,sizeof(text)," [GPU guides; setup/upload %.2f ms; record %.2f ms; submit/wait %.2f ms; readback %.2f ms]",
                (record?record:end)-begin,record?(wait?wait:end)-record:0.,wait?(readback?readback:end)-wait:0.,readback?end-readback:0.);
            reason+=text;}catch(...){}}
    } timings{reason,gpu!=nullptr};
    if(impl_)impl_->preparing=false;
    if(RequiresRestart())return Refuse(reason,"CPU prepared slot unavailable; owner restart required");
    if(impl_&&!impl_->outputLease.expired())return Refuse(reason,"GPU output remains leased by its reader");
    if(gpu&&gpu->depth&&!resident)return Refuse(reason,"Resident depth requires resident output");
    if(gpu?!AdmitGpu(adapterLuid,description,*gpu,depth,reason):!Admit(description,packed,reason))return false;
    if(!impl_)impl_=std::make_unique<Impl>();auto& o=*impl_;
    if(!o.initialized&&!o.Initialize(adapterLuid,reason,gpu?gpu->device.Get():nullptr)){o.failed=true;return false;}
    if(o.luid!=adapterLuid){o.failed=true;return Refuse(reason,"CPU prepared adapter changed; recreate completed client");}
    if(gpu&&!DlssNr::NativeIdentity::CompareDevices(o.device.Get(),gpu->device.Get()).equal)
        return Refuse(reason,"GPU prepared device changed; recreate completed client");
    P::Descriptor prepared=description;
    if(o.previous.capture){
        const bool episode=o.previous.producer==prepared.producer&&o.previous.session==prepared.session&&o.previous.stream==prepared.stream&&
            o.previous.device==prepared.device&&o.previous.generation==prepared.generation;
        if(episode&&prepared.capture<=o.previous.capture)return Refuse(reason,"CPU prepared capture is stale or replayed");
        if(!o.deliveryHistory||!Neurotic::Context::PreparedHistoryContinuous(o.previous,prepared)||((o.previous.flags^prepared.flags)&P::HasDistrust)){
            prepared.flags|=P::Reset;prepared.previousCapture=0;}
    }else{prepared.flags|=P::Reset;prepared.previousCapture=0;}
    if(!o.Allocate(prepared.color.width,prepared.color.height,reason,gpu!=nullptr,resident!=nullptr)||
       (!resident&&(gpu?!o.UploadDepth(depth,reason):!o.Upload(packed,reason))))return false;
    if(o.value>=UINT64_MAX-1){o.failed=true;return Refuse(reason,"CPU prepared completion timeline exhausted");}
    // Hooks can remain installed after this caller dies. Keep their code loaded
    // for process lifetime; the ordinary module reference is balanced by Impl.
    if(o.module&&!o.modulePinned){HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(o.record),&pinned))
            return Refuse(reason,"CPU prepared core module retention refused");o.modulePinned=true;}
    if(!o.prepare(o.list.Get())||FAILED(o.allocator->Reset())||FAILED(o.list->Reset(o.allocator.Get(),nullptr)))
        return Refuse(reason,"CPU prepared recording preparation/reset refused");
    if(gpu)o.retained=*gpu;
    for(unsigned i=0;i<(resident?1u:gpu?2u:4u);++i){const auto ready=i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if(!o.initialImages)Barrier(o.list.Get(),o.images[i].Get(),ready,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=o.images[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        if(gpu&&i==0){src.pResource=gpu->color.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            Barrier(o.list.Get(),src.pResource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);}
        else {src.pResource=o.upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=o.footprints[i];}
        o.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(o.list.Get(),o.images[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,ready);}
    if(gpu)Barrier(o.list.Get(),gpu->color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    G::Dispatch dispatch;dispatch.source=prepared;dispatch.list=o.list.Get();dispatch.queue=o.queue.Get();
    dispatch.color=o.images[0].Get();dispatch.depth=resident?gpu->depth.Get():o.images[1].Get();dispatch.motion=gpu?gpu->motion.Get():o.images[2].Get();
    dispatch.distrust=(prepared.flags&P::HasDistrust)?(gpu?gpu->distrust.Get():o.images[3].Get()):nullptr;
    dispatch.workWidth=prepared.color.width;dispatch.workHeight=prepared.color.height;
    // From Record onward, any failure can leave recorded/provider references.
    o.uncertain=true;o.failed=true;
    timings.record=DlssNr::NativeGuides::NowMs();
    const auto result=static_cast<G::Result>(o.record(&dispatch));
    timings.wait=DlssNr::NativeGuides::NowMs();
    char detail[768]{};if(o.describe)o.describe(detail,sizeof(detail));detail[sizeof(detail)-1]=0;
    const std::string recordReason="record result="+std::string(G::ResultName(result))+(detail[0]?"; "+std::string(detail):"");
    if(result==G::Result::Recorded&&!resident){
        Barrier(o.list.Get(),o.images[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=o.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=o.footprints[0];
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=o.images[0].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        o.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(o.list.Get(),o.images[0].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    // A rejected Record can still have recorded commands. Submit and seal it;
    // never copy/read/return color for that rejected result.
    if(FAILED(o.list->Close()))return Refuse(reason,"CPU prepared list close refused; references retained");
    ID3D12CommandList* commands=o.list.Get();o.queue->ExecuteCommandLists(1,&commands);
    const bool sealed=o.seal(o.list.Get())!=0;
    if(FAILED(o.queue->Signal(o.fence.Get(),++o.value)))return Refuse(reason,"CPU prepared completion signal refused; references retained");
    if(completion)*completion={o.fence,o.value};
    if(!o.Wait(reason))return false;
    timings.readback=DlssNr::NativeGuides::NowMs();
    if(!sealed||(result!=G::Result::Recorded&&result!=G::Result::Preparing))return Refuse(reason,("CPU prepared core refused; "+recordReason+"; seal="+(sealed?"completed":"refused")+"; slot retained").c_str());
    o.uncertain=false;o.failed=false;o.initialImages=false;o.retained={};
    if(result==G::Result::Preparing){
        // Uploads have executed: next frame transitions UAV/NP_SHADER back to
        // COPY_DEST. Preserve capture ordering, but never carry delivery history.
        o.preparing=true;o.deliveryHistory=false;o.previous=prepared;
        return Refuse(reason,"Mainline model lifecycle preparation completed; waiting for the next capture");
    }
    if(resident){resident->color=o.images[0];resident->completion={o.fence,o.value};
        resident->lease=std::make_shared<int>(0);o.outputLease=resident->lease;
        o.previous=prepared;o.deliveryHistory=true;return true;}
    void* ptr=nullptr;const D3D12_RANGE read{0,static_cast<SIZE_T>(o.readbackBytes)};
    if(FAILED(o.readback->Map(0,&read,&ptr))||!ptr){o.previous={};return Refuse(reason,"CPU prepared completed readback mapping refused");}
    // Keep Unmap balanced even if the host output allocation fails.
    try{output.resize(std::size_t(o.width)*o.height*4);}catch(...){const D3D12_RANGE none{0,0};o.readback->Unmap(0,&none);o.previous={};throw;}
    const auto* bytes=static_cast<const std::uint8_t*>(ptr);
    for(unsigned y=0;y<o.height;++y)for(unsigned x=0;x<o.width;++x){auto* dst=output.data()+(std::size_t(y)*o.width+x)*4;
        const auto* src=bytes+std::size_t(y)*o.footprints[0].Footprint.RowPitch+x*4;
        dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];}
    const D3D12_RANGE none{0,0};o.readback->Unmap(0,&none);o.previous=prepared;o.deliveryHistory=true;return true;
}
}
