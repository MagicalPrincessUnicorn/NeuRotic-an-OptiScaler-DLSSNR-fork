#pragma once
#include "NrPreparedDepth.h"

// Descriptor observations only. This does not allocate, record GPU work or issue
// permission to use a guide. Monitor is called under the existing NR mutex.
namespace DlssNr::PreparedDepth::Diagnostics
{
inline bool Enabled() noexcept
{
    static const bool enabled=[] {char value[2]{};return GetEnvironmentVariableA(
        "NEUROTIC_PREPARED_DEPTH_DIAGNOSTICS",value,sizeof(value))==1&&ParseOptIn(value);}();
    return enabled;
}
enum class EntryKind { Direct, Candidate, Refused };
struct Entry {EntryKind kind;const char* reason;};
inline const char* Name(EntryKind kind) noexcept
{
    switch(kind){case EntryKind::Direct:return "direct";case EntryKind::Candidate:return "candidate";
    default:return "refused";}
}
inline Entry Classify(bool enabled,bool privateList,bool observer,
                      const D3D12_RESOURCE_DESC& descriptor,Rect active) noexcept
{
    if(!enabled)return {EntryKind::Direct,"policy-off"};
    if(privateList)return {EntryKind::Direct,"private-command-list"};
    if(descriptor.MipLevels==1)return {EntryKind::Direct,"single-mip"};
    if(observer)return {EntryKind::Refused,"protocol-representation-not-enrolled"};
    const auto decision=Decide(true,descriptor,active);
    return {decision.kind==Kind::Prepared?EntryKind::Candidate:EntryKind::Refused,decision.reason};
}
struct Snapshot
{
    bool enabled=false,privateList=false,protocolObserver=false;
    UINT64 generation=0;
    D3D12_RESOURCE_DESC descriptor{};
    Rect active{};
};
struct Monitor
{
    static constexpr unsigned Limit=64;
    unsigned count=0;
    Snapshot last{};
    bool Observe(const Snapshot& current) noexcept
    {
        if(count>=Limit)return false;
        if(count&&last.enabled==current.enabled&&last.privateList==current.privateList&&
           last.protocolObserver==current.protocolObserver&&last.generation==current.generation&&
           SameDescriptor(last.descriptor,current.descriptor)&&
           last.descriptor.Alignment==current.descriptor.Alignment&&
           last.active.x==current.active.x&&last.active.y==current.active.y&&
           last.active.width==current.active.width&&last.active.height==current.active.height)return false;
        last=current;++count;return true;
    }
};
}
