#pragma once
#include <cstdint>
#include <optional>

namespace DlssNr::Capability {
// The native upscaler API is not the capture API in Present mode. Use the
// observed Present owner when known; both UI and worker must select identically.
inline bool ObservationUsesVulkan(uint32_t route,bool nativeVulkan,bool presentKnown,bool presentVulkan) noexcept {
    return route != 0 && presentKnown ? presentVulkan : nativeVulkan;
}
// A value-only UI change key. Sample counters and timing are intentionally absent.
struct RefreshState {
    bool requestedEnabled=false;
    uint32_t requestedRoute=0;
    uint64_t profileGeneration=0;
    std::optional<uint64_t> configurationRevision;
    uint64_t hdrChangeRevision=0;
    uint64_t menuVisibilityGeneration=0;
    struct Native {
        bool running=false, failed=false, transitionPending=false, outputQuarantined=false;
        bool modelLoaded=false, preSrDisplayReady=false, resetPending=false;
        bool nativeRayReconstructionActive=false, lifecycleOpen=false, historyResetRequested=false;
        bool layer2Requested=false, layer2Loaded=false, layer2Ready=false;
        bool layer2Retiring=false, layer2ResetPending=false, layer2Failed=false;
        uint64_t lifecycleGeneration=0;
        uint32_t frameWidth=0, frameHeight=0, workWidth=0, workHeight=0, guideWidth=0, guideHeight=0;
        bool operator==(const Native&) const = default;
    } native;
    struct Present {
        bool requested=false, active=false, failed=false, policyBlocked=false, historyResetPending=false;
        bool colorSpaceObserved=false, hdrDescriptorTransitioning=false;
        uint32_t backbufferWidth=0, backbufferHeight=0, backbufferFormat=0, backbufferSampleCount=0;
        uint32_t swapEffect=0, colorSpace=0, hdrMetadataType=0, hdrMetadataSize=0;
        uint32_t workload=0, resolution=0, workWidth=0, workHeight=0;
        uint32_t requestedInputPolicy=0, actualInputClass=0;
        uint64_t hdrResizeGeneration=0, hdrMetadataHash=0, resourceGeneration=0;
        int64_t lastColorSpaceResult=0, lastHdrMetadataResult=0;
        bool operator==(const Present&) const = default;
    } present;
    struct Provider {
        bool known=false, enabled=false, supported=false;
        uint64_t generation=0;
        bool operator==(const Provider&) const = default;
    } provider;
    bool operator==(const RefreshState&) const = default;
};

struct RefreshHdrState {
    bool registered=false, colorSpaceObserved=false, transitioning=false;
    uint32_t colorSpace=0, format=0, metadataType=0, metadataSize=0;
    uint64_t identityGeneration=0, resizeGeneration=0, metadataHash=0;
    int64_t colorSpaceResult=0, metadataResult=0;
    bool operator==(const RefreshHdrState&) const = default;
};
// Called under the existing observer's value mutex. Revision is a UI dirty
// hint, not an owner identity or a renderer authority. Never compare event
// sequence/metadata generation: equal per-frame notifications are still idle.
class HdrChangeTracker {
    RefreshHdrState _last;
    bool _seen=false;
    uint64_t _revision=0;
  public:
    uint64_t Observe(const RefreshHdrState& state) noexcept {
        if(!_seen || state!=_last) {
            _seen=true; _last=state;
            if(_revision!=UINT64_MAX) ++_revision;
        }
        return _revision;
    }
};

template<class N,class P,class F>
RefreshState MakeRefreshState(bool enabled,uint32_t route,uint64_t profile,const N& n,const P& p,const F& f,
                              std::optional<uint64_t> configurationRevision={}) noexcept {
    RefreshState s; s.requestedEnabled=enabled; s.requestedRoute=route; s.profileGeneration=profile;
    s.configurationRevision=configurationRevision;
    s.native={n.running,n.failed,n.transitionPending,n.outputQuarantined,n.modelLoaded,n.preSrDisplayReady,
              n.resetPending,n.nativeRayReconstructionActive,n.lifecycleOpen,n.historyResetRequested,
              n.layer2Requested,n.layer2Loaded,n.layer2Ready,n.layer2Retiring,n.layer2ResetPending,n.layer2Failed,
              n.lifecycleGeneration,n.frameWidth,n.frameHeight,n.workWidth,n.workHeight,n.guideWidth,n.guideHeight};
    s.present={p.requested,p.active,p.failed,p.policyBlocked,p.historyResetPending,p.colorSpaceObserved,
               p.hdrDescriptorTransitioning,p.backbufferWidth,p.backbufferHeight,static_cast<uint32_t>(p.backbufferFormat),
               p.backbufferSampleCount,static_cast<uint32_t>(p.swapEffect),static_cast<uint32_t>(p.colorSpace),
               static_cast<uint32_t>(p.hdrMetadataType),p.hdrMetadataSize,p.workload,p.resolution,p.workWidth,p.workHeight,
               static_cast<uint32_t>(p.requestedInputPolicy),static_cast<uint32_t>(p.actualInputClass),
               p.hdrResizeGeneration,p.hdrMetadataHash,p.resourceGeneration,
               static_cast<int64_t>(p.lastColorSpaceResult),static_cast<int64_t>(p.lastHdrMetadataResult)};
    s.provider={f.known,f.enabled,f.supported,f.generation}; return s;
}

enum class RefreshRequest { Automatic, Manual, Copy };
enum class RefreshOutcome { NotDue, Captured, Unavailable };
class RefreshController {
    RefreshState _observed;
    bool _visible=false, _pending=false, _attempted=false;
    uint64_t _lastFrame=0;
    double _lastAttempt=0;
  public:
    void Close() noexcept { _visible=false; }
    bool Pending() const noexcept { return _pending; }
    template<class Collect>
    RefreshOutcome Update(const RefreshState& state,double now,uint64_t frame,RefreshRequest request,Collect&& collect) noexcept {
        // A gap in UI frames covers closing the overlay or a containing section.
        // Latch dirty through ABA changes: returning to an older value still needs
        // a new collection rather than relabeling that older snapshot as current.
        if(!_visible || (frame!=_lastFrame && frame!=_lastFrame+1) || state!=_observed) _pending=true;
        _visible=true; _lastFrame=frame; _observed=state;
        if(_attempted && now<_lastAttempt) _attempted=false;
        if(request==RefreshRequest::Automatic &&
           (!_pending || (_attempted && now-_lastAttempt<0.5))) return RefreshOutcome::NotDue;
        _lastAttempt=now; _attempted=true;
        bool captured=false;
        try { captured=collect(); } catch(...) {}
        _pending=!captured;
        return captured?RefreshOutcome::Captured:RefreshOutcome::Unavailable;
    }
};
}
