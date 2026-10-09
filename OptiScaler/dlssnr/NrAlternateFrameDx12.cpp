#ifndef NR_AFNR_TEST
#include "pch.h"
#endif
#include "NrAlternateFrameDx12.h"
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <cstring>
#include <vector>
#include <shaders/dlssnr/precompile/NrAlternateFrame_Shader.h>

namespace DlssNr::AlternateFrame
{
using Microsoft::WRL::ComPtr;
namespace {
struct Constants {
    unsigned width,height,depthX,depthY,depthW,depthH,motionX,motionY,motionW,motionH,mode,flags;
    float jitterX,jitterY,anchorJitterX,anchorJitterY,motionScaleX,motionScaleY,ratio,invalidDepth;
};
static_assert(sizeof(Constants)==80);
struct Pipeline {ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> shader;};
struct Buffers {
    GpuSafety::DerivedAllocationRef allocation;
    std::shared_ptr<Pipeline> pipeline;
    ComPtr<ID3D12Resource> image,depth,counts,readback,zeros;
    ComPtr<ID3D12Resource> stagingImage,stagingDepth;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12QueryHeap> query;
    unsigned width=0,height=0;
    bool countersInitialized=false;
    bool budgetKnown=false;
    std::vector<ID3D12Resource*> Resources()const {
        std::vector<ID3D12Resource*> r{image.Get(),depth.Get(),counts.Get(),readback.Get(),zeros.Get()};
        if(stagingImage){r.push_back(stagingImage.Get());r.push_back(stagingDepth.Get());}return r;
    }
};
struct Slot {
    std::shared_ptr<Buffers> buffers;
    GpuSafety::DerivedArtifactRef artifact;
    GpuSafety::Ticket ticket;
    AnchorMetadata metadata;
    bool recorded=false,accepted=false,carry=false,measurementTaken=true;
};
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    if(before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    list->ResourceBarrier(1,&b);
}
D3D12_RESOURCE_DESC TextureDesc(DXGI_FORMAT format,unsigned width,unsigned height){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
    d.DepthOrArraySize=d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;return d;
}
ComPtr<ID3D12Resource> Create(ID3D12Device* device,D3D12_RESOURCE_DESC d,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result))))return {};
    return result;
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,D3D12_HEAP_TYPE type){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=256;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;
    d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(type==D3D12_HEAP_TYPE_DEFAULT)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return Create(device,d,type,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
        type==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COMMON);
}
std::shared_ptr<Pipeline> MakePipeline(ID3D12Device* device){
    auto p=std::make_shared<Pipeline>();
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,5};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,20};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[1].DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC desc{2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> bytes,errors;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&errors))||
       FAILED(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&p->root))))return {};
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};compute.pRootSignature=p->root.Get();compute.CS={g_nrAlternateFrameShader,sizeof(g_nrAlternateFrameShader)};
    if(FAILED(device->CreateComputePipelineState(&compute,IID_PPV_ARGS(&p->shader))))return {};
    return p;
}
bool BudgetHeadroom(ID3D12Device* device,uint64_t increment,bool& known){
    known=false;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter3> adapter;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return true;
    DXGI_QUERY_VIDEO_MEMORY_INFO budget{};
    if(FAILED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&budget)))return true;
    known=true;
    const auto reserve=(std::max)(64ull*1024*1024,budget.Budget/20);
    return budget.CurrentUsage<=budget.Budget&&increment<=budget.Budget-budget.CurrentUsage&&reserve<=budget.Budget-budget.CurrentUsage-increment;
}
std::shared_ptr<Buffers> Allocate(ID3D12Device* device,const std::shared_ptr<Pipeline>& pipeline,unsigned width,unsigned height,bool anchor,DXGI_FORMAT sceneFormat){
    const auto imageDesc=TextureDesc(anchor?DXGI_FORMAT_R16G16B16A16_FLOAT:sceneFormat,width,height);
    const auto depthDesc=TextureDesc(DXGI_FORMAT_R32_FLOAT,anchor?width:1,anchor?height:1);
    // Three small buffers plus descriptor/query allocations are conservatively
    // charged in addition to the device-reported committed texture sizes.
    const auto imageBytes=device->GetResourceAllocationInfo(0,1,&imageDesc).SizeInBytes;
    const auto depthBytes=device->GetResourceAllocationInfo(0,1,&depthDesc).SizeInBytes;
    if(imageBytes==UINT64_MAX||depthBytes==UINT64_MAX||imageBytes>AllocationCap||depthBytes>AllocationCap-imageBytes)return {};
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=1;
    buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto plainBytes=device->GetResourceAllocationInfo(0,1,&buffer).SizeInBytes;
    buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto writableBytes=device->GetResourceAllocationInfo(0,1,&buffer).SizeInBytes;
    if(plainBytes>AllocationCap||writableBytes>AllocationCap)return {};
    const auto estimate=(anchor?2:1)*(imageBytes+depthBytes)+2*plainBytes+writableBytes+65536;
    bool budgetKnown=false;
    if(estimate>AllocationCap||GpuSafety::DerivedAllocatedBytes()>AllocationCap-estimate||!BudgetHeadroom(device,estimate,budgetKnown))return {};
    auto allocation=GpuSafety::ReserveDerivedAllocation(anchor?GpuSafety::DerivedArtifactKind::Anchor:GpuSafety::DerivedArtifactKind::Scratch,estimate);
    if(!allocation)return {};
    auto b=std::make_shared<Buffers>();b->allocation=std::move(allocation);b->pipeline=pipeline;b->width=width;b->height=height;b->budgetKnown=budgetKnown;
    b->image=Create(device,imageDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b->depth=Create(device,depthDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(anchor) {
        b->stagingImage=Create(device,imageDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        b->stagingDepth=Create(device,depthDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(!b->stagingImage||!b->stagingDepth)return {};
    }
    b->counts=Buffer(device,D3D12_HEAP_TYPE_DEFAULT);b->readback=Buffer(device,D3D12_HEAP_TYPE_READBACK);b->zeros=Buffer(device,D3D12_HEAP_TYPE_UPLOAD);
    if(!b->image||!b->depth||!b->counts||!b->readback||!b->zeros)return {};
    void* memory=nullptr;D3D12_RANGE noRead{0,0};if(FAILED(b->zeros->Map(0,&noRead,&memory)))return {};
    std::memset(memory,0,256);b->zeros->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=8;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&b->heap)))||FAILED(device->CreateQueryHeap(&query,IID_PPV_ARGS(&b->query))))return {};
    return b;
}
DXGI_FORMAT SceneFormat(const NativeSourceView& v){return v.rgb11f?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;}
DXGI_FORMAT DepthViewFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return f;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT MotionViewFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_R32G32_TYPELESS:case DXGI_FORMAT_R32G32_FLOAT:return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:case DXGI_FORMAT_R16G16_FLOAT:return DXGI_FORMAT_R16G16_FLOAT;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
bool SceneResource(ID3D12Resource* r,const NativeSourceView& v){
    if(!r)return false;const auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==v.width&&d.Height==v.height&&
        (d.Format==SceneFormat(v)||(v.rgba16f&&d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS))&&d.SampleDesc.Count==1&&d.DepthOrArraySize==1&&d.MipLevels==1;
}
}
struct PreparedFrame::Impl {
    GpuRequest request;
    std::shared_ptr<Slot> slot,anchor;
    std::unique_ptr<GpuSafety::LocalRecordingAction> action;
    std::unique_ptr<GpuSafety::DerivedReadLease> read;
    Constants constants{};
    bool carry=false,recorded=false;
};
struct Renderer::Impl {
    ComPtr<ID3D12Device> device;
    std::shared_ptr<Pipeline> pipeline;
    std::array<std::shared_ptr<Slot>,2> anchors{},scratches{};
    std::shared_ptr<Slot> candidate,accepted;
    Reason reason=Reason::None;
    std::unique_ptr<PreparedFrame::Impl> Prepare(const GpuRequest& r,bool carry){
        reason=Reason::None;
        const auto inputs=BuildAlternateFrameInputs(r.source,carry&&accepted?&accepted->metadata:nullptr,{true,false});
        if(!inputs.accepted){reason=inputs.reason;return {};}
        if(!r.list||!SceneResource(r.target,r.source)||!r.depth||!r.motion||
           !(r.target->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||
           (!carry&&(!SceneResource(r.base,r.source)||r.base==r.target))) {reason=Reason::UnsupportedSceneFormat;return {};}
        if(carry&&(!accepted||!accepted->accepted)){reason=Reason::NoAnchor;return {};}
        const auto depthDesc=r.depth->GetDesc(),motionDesc=r.motion->GetDesc();
        if(depthDesc.SampleDesc.Count!=1||motionDesc.SampleDesc.Count!=1||depthDesc.DepthOrArraySize!=1||motionDesc.DepthOrArraySize!=1||
           DepthViewFormat(depthDesc.Format)==DXGI_FORMAT_UNKNOWN||MotionViewFormat(motionDesc.Format)==DXGI_FORMAT_UNKNOWN||
           depthDesc.MipLevels!=1||motionDesc.MipLevels!=1||
           depthDesc.Width!=r.source.depth.backingWidth||depthDesc.Height!=r.source.depth.backingHeight||
           motionDesc.Width!=r.source.motion.backingWidth||motionDesc.Height!=r.source.motion.backingHeight)
            {reason=Reason::GuideBounds;return {};}
        if(r.target==r.depth||r.target==r.motion||r.depth==r.motion){reason=Reason::UnsupportedSceneFormat;return {};}
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{SceneFormat(r.source)};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)){reason=Reason::UnsupportedSceneFormat;return {};}
        if(!pipeline){pipeline=MakePipeline(device.Get());if(!pipeline){reason=Reason::RecordingFailure;return {};}}
        GpuSafety::Pending(); // nonblocking existing-owner retirement
        auto& pool=carry?scratches:anchors;std::shared_ptr<Slot>* selected=nullptr;
        for(auto& entry:pool)if(!entry||(GpuSafety::Reusable(entry->ticket)&&GpuSafety::DerivedArtifactReusable(entry->artifact))){selected=&entry;break;}
        if(!selected){reason=Reason::ResourceCapacity;return {};}
        if(*selected&&((*selected)->buffers->width!=r.source.width||(*selected)->buffers->height!=r.source.height||
           (carry&&(*selected)->buffers->image->GetDesc().Format!=SceneFormat(r.source)))) {
            if(accepted==*selected)accepted.reset();if(candidate==*selected)candidate.reset();selected->reset();
        }
        auto slot=*selected;
        if(!slot){auto buffers=Allocate(device.Get(),pipeline,r.source.width,r.source.height,!carry,SceneFormat(r.source));
            if(!buffers){reason=Reason::ResourceCapacity;return {};}
            slot=std::make_shared<Slot>();slot->buffers=std::move(buffers);}
        auto p=std::make_unique<PreparedFrame::Impl>();p->request=r;p->slot=slot;p->carry=carry;p->anchor=carry?accepted:nullptr;
        std::vector<ID3D12Resource*> resources{r.target,r.depth,r.motion};if(!carry)resources.push_back(r.base);
        const auto own=slot->buffers->Resources();resources.insert(resources.end(),own.begin(),own.end());
        if(carry){const auto other=accepted->buffers->Resources();resources.insert(resources.end(),other.begin(),other.end());}
        auto ticket=GpuSafety::Record(r.list);p->action=GpuSafety::BeginLocalAction(ticket,r.list,resources);
        if(!p->action){reason=Reason::ReaderReservationUnavailable;return {};}
        auto inputsRetained=std::make_shared<std::vector<ComPtr<ID3D12Resource>>>();
        for(auto* resource:resources)inputsRetained->emplace_back(resource);
        if(!GpuSafety::RetainDerivedUse(*p->action,inputsRetained)){reason=Reason::ReaderReservationUnavailable;return {};}
        if(carry){p->read=GpuSafety::ReserveDerivedRead(accepted->artifact,*p->action,r.queue);
            if(!p->read){reason=Reason::AnchorNotImmutable;return {};}}
        if(!carry) {
            const std::array copies{GpuSafety::DerivedSnapshotCopy{slot->buffers->stagingImage.Get(),slot->buffers->image.Get()},
                GpuSafety::DerivedSnapshotCopy{slot->buffers->stagingDepth.Get(),slot->buffers->depth.Get()}};
            if(!GpuSafety::PrepareDerivedSnapshot(*p->action,copies,slot->artifact,slot->buffers,slot->buffers->allocation))
                {reason=Reason::ResourceCapacity;return {};}
        }
        else if(slot->artifact){if(!GpuSafety::RearmDerivedArtifact(slot->artifact,*p->action)){reason=Reason::ResourceCapacity;return {};}}
        else {slot->artifact=GpuSafety::CreateDerivedArtifact(*p->action,own,
            carry?GpuSafety::DerivedArtifactKind::Scratch:GpuSafety::DerivedArtifactKind::Anchor,slot->buffers,65536,slot->buffers->allocation);
            if(!slot->artifact){reason=Reason::ResourceCapacity;return {};}}
        *selected=slot;slot->ticket=ticket;slot->recorded=slot->accepted=false;slot->measurementTaken=true;slot->carry=carry;
        slot->metadata={r.source};
        const auto& v=r.source;auto& c=p->constants;
        c={v.width,v.height,v.depth.x,v.depth.y,v.depth.width,v.depth.height,v.motion.x,v.motion.y,v.motion.width,v.motion.height,
            carry?1u:0u,16u|(v.depthInverted?64u:0u)|(v.rgb11f?32u:0u)|(v.guidesJittered?1u:0u)|(v.includesJitter?2u:0u)|(v.depthTest==DepthTest::LinearView?4u:0u),
            v.jitterUv[0],v.jitterUv[1],carry?accepted->metadata.source.jitterUv[0]:0,carry?accepted->metadata.source.jitterUv[1]:0,
            v.motionToSceneUv[0],v.motionToSceneUv[1],inputs.scaleRatio,0};
        auto handle=slot->buffers->heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const std::array<ID3D12Resource*,5> srvs{carry?slot->buffers->image.Get():r.base,
            carry?accepted->buffers->image.Get():r.target,r.depth,r.motion,carry?accepted->buffers->depth.Get():r.depth};
        for(auto* resource:srvs){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=resource->GetDesc().Format;
            if(resource==r.depth)d.Format=DepthViewFormat(d.Format);
            else if(resource==r.motion)d.Format=MotionViewFormat(d.Format);
            else if(d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS)d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(resource,&d,handle);handle.ptr+=step;}
        const std::array<ID3D12Resource*,2> uavs{carry?r.target:slot->buffers->stagingImage.Get(),
            carry?slot->buffers->depth.Get():slot->buffers->stagingDepth.Get()};
        for(auto* resource:uavs){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=resource->GetDesc().Format;
            if(d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS)d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(resource,nullptr,&d,handle);handle.ptr+=step;}
        D3D12_UNORDERED_ACCESS_VIEW_DESC counter{};counter.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;counter.Format=DXGI_FORMAT_R32_TYPELESS;
        counter.Buffer.NumElements=64;counter.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;device->CreateUnorderedAccessView(slot->buffers->counts.Get(),nullptr,&counter,handle);
        return p;
    }
};
PreparedFrame::PreparedFrame(std::unique_ptr<Impl> p):impl_(std::move(p)){}
PreparedFrame::~PreparedFrame()=default;
Renderer::Renderer(ID3D12Device* device):impl_(std::make_unique<Impl>()){impl_->device=device;}
Renderer::~Renderer()=default;
std::unique_ptr<PreparedFrame> Renderer::PrepareCapture(const GpuRequest& r){
    try{auto p=impl_->Prepare(r,false);return p?std::unique_ptr<PreparedFrame>(new PreparedFrame(std::move(p))):nullptr;}
    catch(...){impl_->reason=Reason::ResourceCapacity;return {};}}
std::unique_ptr<PreparedFrame> Renderer::PrepareCarry(const GpuRequest& r){
    try{auto p=impl_->Prepare(r,true);return p?std::unique_ptr<PreparedFrame>(new PreparedFrame(std::move(p))):nullptr;}
    catch(...){impl_->reason=Reason::ResourceCapacity;return {};}}
namespace {
template<class Prepared>GpuRecordResult Record(Prepared& p,bool carry){
    if(p.recorded||p.carry!=carry||!p.action||!p.action->Current())return {};
    p.recorded=true;const auto& r=p.request;auto* list=r.list;auto& b=*p.slot->buffers;
    // All storage, descriptors, constants and read rights were acquired above.
    // Once this timestamp is recorded, any failure is after possible effects.
    try {
        list->EndQuery(b.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        Barrier(list,b.counts.Get(),b.countersInitialized?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        b.countersInitialized=true;
        list->CopyBufferRegion(b.counts.Get(),0,b.zeros.Get(),0,24);
        Barrier(list,b.counts.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(carry){
            Barrier(list,r.target,r.targetState,D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(list,b.image.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyResource(b.image.Get(),r.target);
            Barrier(list,b.image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(list,r.target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list,p.anchor->buffers->image.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(list,p.anchor->buffers->depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }else {
            Barrier(list,r.base,r.baseState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(list,r.target,r.targetState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        Barrier(list,r.depth,r.depthState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list,r.motion,r.motionState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->SetComputeRootSignature(b.pipeline->root.Get());list->SetPipelineState(b.pipeline->shader.Get());
        ID3D12DescriptorHeap* heaps[]{b.heap.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRoot32BitConstants(0,20,&p.constants,0);list->SetComputeRootDescriptorTable(1,b.heap->GetGPUDescriptorHandleForHeapStart());
        list->Dispatch((r.source.width+7)/8,(r.source.height+7)/8,1);
        D3D12_RESOURCE_BARRIER uav{};uav.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&uav);
        if(carry){
            Barrier(list,b.image.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list,p.anchor->buffers->image.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list,p.anchor->buffers->depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list,r.target,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,r.targetState);
        }else {
            Barrier(list,r.base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,r.baseState);
            Barrier(list,r.target,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,r.targetState);
        }
        Barrier(list,r.depth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,r.depthState);
        Barrier(list,r.motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,r.motionState);
        Barrier(list,b.counts.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(b.readback.Get(),0,b.counts.Get(),0,24);
        Barrier(list,b.counts.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->EndQuery(b.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(b.query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,b.readback.Get(),64);
        const bool current=p.action->Current();p.slot->recorded=current;p.slot->measurementTaken=!current;
        GpuRecordResult result{current,true,current?S_OK:E_FAIL};
        if(carry) {
            result.output.emplace(list,r.target,r.source.width,r.source.height,NativeOutputDerivation::AlternateFrameCarry);
            result.output->ObserveDispatchPossible();if(current)result.output->ObserveDispatchRecorded();
        }
        p.action.reset();return result;
    }catch(...){p.action.reset();return {false,true,E_FAIL};}
}
}
GpuRecordResult Renderer::RecordCapture(PreparedFrame& p){auto result=Record(*p.impl_,false);if(result.recorded)impl_->candidate=p.impl_->slot;return result;}
GpuRecordResult Renderer::RecordCarry(PreparedFrame& p){return Record(*p.impl_,true);}
bool Renderer::AcceptCapture(const SourceRef& source){
    if(!impl_->candidate||!impl_->candidate->recorded||impl_->candidate->metadata.source.source!=source)return false;
    impl_->candidate->accepted=true;impl_->accepted=impl_->candidate;impl_->candidate.reset();return true;
}
const AnchorMetadata* Renderer::Anchor()const noexcept{return impl_->accepted?&impl_->accepted->metadata:nullptr;}
Reason Renderer::LastReason()const noexcept{return impl_->reason;}
void Renderer::Invalidate(){impl_->accepted.reset();impl_->candidate.reset();for(auto& s:impl_->anchors)s.reset();for(auto& s:impl_->scratches)s.reset();GpuSafety::Pending();}
std::optional<GpuMeasurement> Renderer::Poll(){
    for(auto* pool:{&impl_->anchors,&impl_->scratches})for(auto& s:*pool){
        if(!s||s->measurementTaken||!GpuSafety::Readable(s->ticket))continue;
        s->measurementTaken=true;void* memory=nullptr;D3D12_RANGE range{0,80};
        if(FAILED(s->buffers->readback->Map(0,&range,&memory)))continue;
        GpuMeasurement result;result.source=*s->metadata.source.source;result.carry=s->carry;result.association=s->metadata.source.association;
        result.budgetKnown=s->buffers->budgetKnown;
        std::memcpy(result.pixels.data(),memory,24);std::uint64_t ticks[2];std::memcpy(ticks,static_cast<char*>(memory)+64,16);
        const auto frequency=GpuSafety::TimestampFrequency(s->ticket);
        if(frequency&&ticks[1]>=ticks[0])result.milliseconds=1000.0*static_cast<double>(ticks[1]-ticks[0])/frequency;
        D3D12_RANGE noWrite{0,0};s->buffers->readback->Unmap(0,&noWrite);return result;
    }
    return {};
}
}
