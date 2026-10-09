#pragma once

// First-party RND-011 realization. The caller owns allocation publication and
// retirement; this helper uses the existing recording owner, not frame-age reuse.
#include "NrGpuSafety.h"
#include "NativeIdentity.h"
#include <wrl/client.h>
#include <string_view>
#include <limits>
#include <optional>

namespace DlssNr::PreparedDepth
{
enum class Kind { Direct, Prepared, Refused };
struct Rect { UINT x=0,y=0,width=0,height=0; };
struct Decision { Kind kind=Kind::Refused;const char* reason="missing-source";D3D12_RESOURCE_DESC descriptor{}; };
inline constexpr unsigned RetirementLimit=8;
inline bool ParseOptIn(std::string_view value) noexcept { return value=="1"; }
inline bool Enabled(bool experimentalChoice = false) noexcept
{
    // Process-local review launchers override the saved UI choice, including explicit 0 for Baseline.
    static const auto environment=[] {
        struct Override { bool present; bool enabled; };
        char value[2]{};
        const auto length=GetEnvironmentVariableA("NEUROTIC_PREPARED_DEPTH_MIP0",value,sizeof(value));
        return Override{length!=0,length==1&&ParseOptIn(value)};
    }();
    return environment.present ? environment.enabled : experimentalChoice;
}
inline DXGI_FORMAT TypedFormat(DXGI_FORMAT format) noexcept
{
    switch(format){
    case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:return format;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
inline Decision Decide(bool enabled,const D3D12_RESOURCE_DESC& d,Rect active) noexcept
{
    if(!enabled)return {Kind::Direct,"policy-off"};
    if(d.MipLevels==1)return {Kind::Direct,"single-mip"};
    auto refuse=[](const char* reason){return Decision{Kind::Refused,reason};};
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)return refuse("dimension");
    if(d.DepthOrArraySize!=1)return refuse("array");
    if(d.SampleDesc.Count!=1||d.SampleDesc.Quality!=0)return refuse("samples");
    if(!d.MipLevels||!d.Width||!d.Height||d.Width>8192||d.Height>8192)return refuse("extent");
    if(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)return refuse("unreadable-source");
    const auto format=TypedFormat(d.Format);
    if(format==DXGI_FORMAT_UNKNOWN)return refuse("depth-format");
    if(!active.width||!active.height||active.x>=d.Width||active.y>=d.Height||
       active.width>d.Width-active.x||active.height>d.Height-active.y)return refuse("active-rect");
    auto prepared=d;prepared.MipLevels=1;prepared.Format=format;
    prepared.Flags=D3D12_RESOURCE_FLAG_NONE;prepared.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;prepared.Alignment=0;
    return {Kind::Prepared,"mip-zero",prepared};
}
inline void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,UINT subresource,
                       D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
{
    if(before==after)return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,subresource,before,after};list->ResourceBarrier(1,&barrier);
}
inline ID3D12Resource* Allocate(ID3D12Device* device,const D3D12_RESOURCE_DESC& desc)
{
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* resource=nullptr;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&resource))))return nullptr;
    return resource;
}
// A slot is state in the existing NR owner. It does not release published storage.
struct Slot
{
    ID3D12Resource* resource=nullptr;
    GpuSafety::Ticket use;
    UINT64 generation=0,id=0;
};
struct Request
{
    bool enabled=false;
    ID3D12Device* device=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Resource* source=nullptr;
    GpuSafety::Ticket ticket;
    UINT64 generation=0;
    Rect active;
    D3D12_RESOURCE_STATES arrival=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bool arrivalKnown=false;
    unsigned pendingRetirements=0;
};
#define NR_PREPARED_DEPTH_NATIVE_ARRIVAL_V1 1
inline void SetNativeArrival(Request& request,std::optional<D3D12_RESOURCE_STATES> declared,
                             bool ngxInputContract) noexcept
{
    // Explicit adapter/host state wins. An absent override is known only at a
    // seam that actually carries the direct NGX input-state contract.
    request.arrivalKnown=declared.has_value()||ngxInputContract;
    request.arrival=declared.value_or(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
struct Result
{
    Kind kind=Kind::Refused;
    const char* reason="missing-source";
    ID3D12Resource* input=nullptr;
    UINT64 id=0;
    bool allocated=false;
    bool copyRecorded=false;
    // Keep the exact recording/resource borrow through all provider uses.
    std::unique_ptr<GpuSafety::LocalRecordingAction> action;
};
inline bool CanUse(const Result& result) noexcept
{
    return result.kind==Kind::Prepared&&result.input&&result.action&&result.action->Current();
}
inline bool SameDescriptor(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b) noexcept
{
    return a.Dimension==b.Dimension&&a.Width==b.Width&&a.Height==b.Height&&
        a.DepthOrArraySize==b.DepthOrArraySize&&a.MipLevels==b.MipLevels&&a.Format==b.Format&&
        a.SampleDesc.Count==b.SampleDesc.Count&&a.SampleDesc.Quality==b.SampleDesc.Quality&&
        a.Flags==b.Flags&&a.Layout==b.Layout;
}
inline bool SameDevice(ID3D12Resource* resource,ID3D12Device* expected)
{
    Microsoft::WRL::ComPtr<ID3D12Device> actual;
    return resource&&expected&&SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&actual)))&&
           NativeIdentity::CompareDevices(actual.Get(),expected).equal;
}
template<class Allocator,class Retire>
Result Prepare(const Request& request,Slot& slot,UINT64& sequence,Allocator allocate,Retire retire)
{
    if(!request.enabled)return {Kind::Direct,"policy-off",request.source};
    if(!request.source)return {};
    const auto decision=Decide(true,request.source->GetDesc(),request.active);
    if(decision.kind!=Kind::Prepared)return {decision.kind,decision.reason,
        decision.kind==Kind::Direct?request.source:nullptr};
    auto refuse=[](const char* reason){return Result{Kind::Refused,reason};};
    if(!request.generation)return refuse("generation");
    if(!request.arrivalKnown||request.arrival!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        return refuse("arrival-state");
    if(!request.device||!request.list||!request.ticket||
       !GpuSafety::MatchesLocalRecording(request.ticket,request.list,request.source))return refuse("recording");
    if(!SameDevice(request.source,request.device))return refuse("source-device");
    const bool reuse=slot.resource&&slot.generation==request.generation&&
        SameDescriptor(slot.resource->GetDesc(),decision.descriptor)&&SameDevice(slot.resource,request.device)&&
        GpuSafety::Reusable(slot.use);
    if(!reuse&&request.pendingRetirements>=RetirementLimit)return refuse("retirement-pressure");
    if(!reuse&&sequence==(std::numeric_limits<UINT64>::max)())return refuse("realization-id-exhausted");
    Microsoft::WRL::ComPtr<ID3D12Resource> replacement;
    if(!reuse){
        replacement.Attach(allocate(request.device,decision.descriptor));
        if(!replacement)return refuse("allocation");
        if(!SameDescriptor(replacement->GetDesc(),decision.descriptor)||!SameDevice(replacement.Get(),request.device))
            return refuse("allocation-identity");
    }
    ID3D12Resource* destination=reuse?slot.resource:replacement.Get();
    ID3D12Resource* resources[]={request.source,destination};
    auto action=GpuSafety::BeginLocalAction(request.ticket,request.list,resources);
    if(!action||!action->Current())return refuse("recording-revoked");
    if(!reuse){
        if(slot.resource)retire(slot); // Existing owner parks the previous published allocation.
        slot.resource=replacement.Detach();slot.generation=request.generation;slot.id=++sequence;
    }
    // Publication precedes recording so every recorded allocation is retained.
    slot.use=request.ticket;
    Transition(request.list,request.source,0,request.arrival,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(request.list,destination,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=request.source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;
    to.pResource=destination;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=0;
    request.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(request.list,request.source,0,D3D12_RESOURCE_STATE_COPY_SOURCE,request.arrival);
    Transition(request.list,destination,0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(!action->Current())return {Kind::Refused,"recording-revoked-after-copy",nullptr,
        slot.id,!reuse,true,std::move(action)};
    return {Kind::Prepared,"mip-zero-copy-recorded",destination,slot.id,!reuse,true,std::move(action)};
}
}
