#pragma once
#include <sl_dlss_g.h>
#include <cstdint>

namespace DlssNr::PreFg::Admission {
enum class Reason : unsigned {
    None, ProviderUnsupported, Queue, Mode, RequestedCount, Ui, ShowOnly,
    ProviderUnknown, Viewport, Replacement, ObservedCount, NativeCount, NativeInstance
};
struct Raw {
    bool known=false;
    unsigned abi=0, mode=0, generated=0, queue=0, ui=0, flags=0;
    bool operator==(const Raw&) const=default;
};
inline Raw Capture(const sl::DLSSGOptions& o) noexcept {
    return {true,static_cast<unsigned>(o.structVersion),static_cast<unsigned>(o.mode),
        o.numFramesToGenerate,static_cast<unsigned>(o.queueParallelismMode),
        static_cast<unsigned>(o.enableUserInterfaceRecomposition),static_cast<unsigned>(o.flags)};
}
// The accepted scalar options may be an SDK-normalized snapshot after a
// successful original-options fallback. Caller ABI is a separate observation.
inline Raw Capture(const sl::DLSSGOptions& o,unsigned callerAbi) noexcept {
    auto r=Capture(o);r.abi=callerAbi;return r;
}
struct Decision { Raw raw; bool queueSupported=false, supported=false; Reason reason=Reason::ProviderUnsupported; };
// The pinned public SDK documents eBlockNoClientQueues as Vulkan-only;
// D3D clients fall back to eBlockPresentingClientQueue. Interpret that contract
// for admission without rewriting the original option sent to the provider.
// Unknown enum values do not acquire this execution guarantee.
// UI recomposition allocation is not proof of an active separate UI plane.
// The actual full-image transaction must accept the three null declarations
// before NR work/publication; this classifier alone grants no tag permission.
inline Decision ClassifyD3D12(Raw r) noexcept {
    Decision d;d.raw=r;
    d.queueSupported=r.known&&
        (r.queue==static_cast<unsigned>(sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue)||
         r.queue==static_cast<unsigned>(sl::DLSSGQueueParallelismMode::eBlockNoClientQueues));
    if(!r.known)d.reason=Reason::ProviderUnsupported;
    else if(r.mode!=static_cast<unsigned>(sl::DLSSGMode::eOn))d.reason=Reason::Mode;
    else if(r.generated<1||r.generated>5)d.reason=Reason::RequestedCount;
    else if(!d.queueSupported)d.reason=Reason::Queue;
    else if(r.flags&static_cast<unsigned>(sl::DLSSGFlags::eShowOnlyInterpolatedFrame))d.reason=Reason::ShowOnly;
    else d.reason=Reason::None;
    d.supported=d.reason==Reason::None;
    return d;
}
struct Snapshot {
    bool providerKnown=false, providerEnabled=false, providerSupported=false, ambiguous=false, nativeOutput=false;
    unsigned observedGenerated=0,nativeActive=0;
    std::uint64_t nativeInstance=0,providerGeneration=0,nativeGeneration=0,viewport=0;
    Raw raw;
    bool consumerReady=false,frameValid=false;
    bool operator==(const Snapshot&) const=default;
};
// Same admission truth as the existing provider composite, in its actual
// short-circuit order. Raw option reasons explain only an already refused
// provider; they never override the selected provider's authoritative result.
inline Reason ProviderGate(const Snapshot& s) noexcept {
    if(!s.providerKnown)return Reason::ProviderUnknown;
    if(!s.providerSupported) {
        if(s.ambiguous)return Reason::Viewport;
        const auto options=ClassifyD3D12(s.raw);
        return s.raw.known&&!options.supported?options.reason:Reason::ProviderUnsupported;
    }
    if(!s.nativeOutput)return Reason::Replacement;
    if(s.observedGenerated<1||s.observedGenerated>5)return Reason::ObservedCount;
    if(s.nativeActive!=1)return Reason::NativeCount;
    if(!s.nativeInstance)return Reason::NativeInstance;
    return Reason::None;
}
inline const char* Describe(Reason r) noexcept {
    switch(r) {
    case Reason::None:return "none";
    case Reason::Queue:return "FG provider queue mode has no supported D3D execution contract";
    case Reason::Mode:return "FG provider mode is not a fixed enabled mode";
    case Reason::RequestedCount:return "FG provider requested fixed frame count is unsupported";
    case Reason::Ui:return "FG provider UI recomposition is enabled";
    case Reason::ShowOnly:return "FG provider show-only-interpolated mode is enabled";
    case Reason::ProviderUnknown:return "FG provider options have not been observed";
    case Reason::Viewport:return "Present FG needs one unambiguous contributing viewport for this operation";
    case Reason::Replacement:return "FG output is owned by a replacement provider";
    case Reason::ObservedCount:return "FG generated count has not been observed in the supported fixed range";
    case Reason::NativeCount:return "FG native feature count is not exactly one";
    case Reason::NativeInstance:return "FG native feature instance is unavailable";
    default:return "Present FG needs an observed native provider, a fixed 2x-6x multiplier, and supported queue settings.";
    }
}
// One instance per authentic presentation owner; only diagnostics depend on it.
// The owner serializes access. Equality excludes the frame token/sequence so a
// stable refusal cannot emit a new row every frame. At most one changed
// snapshot per second per owner; the cache is never execution evidence.
struct TraceState {
    bool seen=false;Snapshot last;double lastEmitMs=0;
    bool Changed(const Snapshot& s,double nowMs) noexcept {
        if(seen&&last==s)return false;
        // Keep the last emitted snapshot during suppression: a stable new
        // failure is emitted after the interval even without another change.
        if(seen&&nowMs>=lastEmitMs&&nowMs-lastEmitMs<1000)return false;
        last=s;seen=true;lastEmitMs=nowMs;return true;
    }
};
}
