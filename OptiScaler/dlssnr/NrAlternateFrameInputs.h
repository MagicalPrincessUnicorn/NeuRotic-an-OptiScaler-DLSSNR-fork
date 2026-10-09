#pragma once
#include "NrAlternateFramePolicy.h"
#include "NrAlternateFrameReference.h"

namespace DlssNr::AlternateFrame
{
enum class Provenance {Unknown,Observed,SchemaDefined,Assumed,Contradicted};
struct GuideRect {
    unsigned x=0,y=0,width=0,height=0,backingWidth=0,backingHeight=0;
    bool Fits()const noexcept{return width&&height&&x<=backingWidth&&y<=backingHeight&&width<=backingWidth-x&&height<=backingHeight-y;}
    bool operator==(const GuideRect&)const=default;
};
struct MappingInput {
    std::array<float,2> p{},delta{},currentJitter{},anchorJitter{};
    GuideRect motion,depth;
    bool guidesJittered=false,includesJitter=false;
};
struct MappingResult {
    bool valid=false;
    std::array<float,2> q{};
    unsigned motionX=0,motionY=0,depthX=0,depthY=0;
};
inline MappingResult MapCurrentPixel(const MappingInput& i)noexcept
{
    MappingResult r;
    if(!i.motion.Fits()||!i.depth.Fits())return r;
    for(unsigned axis=0;axis<2;++axis) {
        if(!std::isfinite(i.p[axis])||!std::isfinite(i.delta[axis])||
           !std::isfinite(i.currentJitter[axis])||!std::isfinite(i.anchorJitter[axis]))return r;
        r.q[axis]=i.p[axis]+i.delta[axis]+(i.includesJitter?i.currentJitter[axis]-i.anchorJitter[axis]:0);
    }
    const float gx=i.p[0]+(i.guidesJittered?i.currentJitter[0]:0),gy=i.p[1]+(i.guidesJittered?i.currentJitter[1]:0);
    if(gx<0||gx>=1||gy<0||gy>=1||!std::isfinite(r.q[0])||!std::isfinite(r.q[1]))return r;
    r.motionX=i.motion.x+static_cast<unsigned>(gx*i.motion.width);r.motionY=i.motion.y+static_cast<unsigned>(gy*i.motion.height);
    r.depthX=i.depth.x+static_cast<unsigned>(gx*i.depth.width);r.depthY=i.depth.y+static_cast<unsigned>(gy*i.depth.height);
    r.valid=true;return r;
}
struct NativeSourceView {
    std::optional<SourceRef> source,predecessor;
    ViewRef view;
    InvocationRef invocation;
    std::uint64_t scope=0,linearDomain=0,association=0;
    std::int64_t timeUs=0;
    bool identityObserved=false,d3d12=false,postSr=false,rgba16f=false,rgb11f=false,currentAccess=false,clockKnown=false;
    bool presentationHdr=false,rayReconstruction=false,originalMetadata=false,resetDue=false;
    bool guidesJittered=false,includesJitter=false,depthInverted=false,projectionKnown=false;
    unsigned width=0,height=0,workWidth=0,workHeight=0,samples=0,streams=1,views=1,requestedPasses=1,producedPasses=1;
    GuideRect motion,depth;
    Provenance motionConvention=Provenance::Unknown,depthEncoding=Provenance::Unknown;
    DepthTest depthTest=DepthTest::DeviceNormalized;
    std::array<float,2> motionToSceneUv{},jitterUv{};
    StorageScaleFacts scale;
};
struct AnchorMetadata {NativeSourceView source;};
struct AlternateFrameConfig {bool enabled=false,outputRequiresModelAttempt=false;};
struct InputDecision {
    bool accepted=false;
    Reason reason=Reason::None;
    float scaleRatio=1;
    bool assumedScale=false,assumedProjection=false;
    const char* guidePolicy="ObservedMotion_SurfaceReconstruction_V3";
};
inline bool Declared(Provenance p)noexcept{return p==Provenance::Observed||p==Provenance::SchemaDefined;}
inline InputDecision BuildAlternateFrameInputs(const NativeSourceView& v,const AnchorMetadata* anchor,
                                               const AlternateFrameConfig& c)noexcept
{
    const auto no=[](Reason reason){return InputDecision{false,reason};};
    if(!c.enabled)return no(Reason::Off);
    if(!v.identityObserved||!Valid(v.source))return no(Reason::InputIdentityUnknown);
    if(!v.d3d12)return no(Reason::UnsupportedApi);
    if(!v.postSr||v.streams!=1||v.views!=1)return no(Reason::UnsupportedRoute);
    if(v.requestedPasses!=1||v.producedPasses!=1)return no(Reason::UnsupportedPassCount);
    if(!v.width||!v.height||v.width>8192||v.height>8192||v.width!=v.workWidth||v.height!=v.workHeight)return no(Reason::UnsupportedRaster);
    if(v.samples!=1)return no(Reason::UnsupportedSampleCount);
    if(v.rgba16f==v.rgb11f)return no(Reason::UnsupportedSceneFormat);
    if(!v.linearDomain||v.scale.domain!=v.linearDomain)return no(Reason::SceneDomainUnknown);
    if(v.presentationHdr)return no(Reason::PresentationHdrDeferred);
    if(v.rayReconstruction)return no(Reason::RayReconstructionDeferred);
    if(!v.originalMetadata||!Declared(v.motionConvention))return no(Reason::MissingMotionConvention);
    if(!Declared(v.depthEncoding))return no(Reason::MissingDepthEncoding);
    // This renderer implements the labelled continuity approximation. A real
    // comparable-anchor projection needs its own supplied GPU predicate.
    if(v.depthTest==DepthTest::ComparableAnchor)return no(Reason::MissingDepthEncoding);
    if(!v.motion.Fits()||!v.depth.Fits())return no(Reason::GuideBounds);
    for(unsigned axis=0;axis<2;++axis)
        if(!std::isfinite(v.motionToSceneUv[axis])||!std::isfinite(v.jitterUv[axis]))return no(Reason::MappingNonfinite);
    if(!v.currentAccess)return no(Reason::ReaderReservationUnavailable);
    if(!v.clockKnown||v.timeUs<0)return no(Reason::ClockUnknown);
    if(c.outputRequiresModelAttempt)return no(Reason::OutputContractRequiresModelAttempt);
    InputDecision result;result.accepted=true;result.assumedProjection=!v.projectionKnown;
    if(anchor) {
        const auto& a=anchor->source;
        if(v.predecessor!=a.source)return no(Reason::PredecessorMismatch);
        if(v.rgba16f!=a.rgba16f||v.rgb11f!=a.rgb11f||v.scope!=a.scope||v.width!=a.width||v.height!=a.height||v.motion!=a.motion||v.depth!=a.depth||
           v.depthInverted!=a.depthInverted||v.depthTest!=a.depthTest||v.includesJitter!=a.includesJitter||
           v.guidesJittered!=a.guidesJittered||v.view!=a.view||v.motionToSceneUv!=a.motionToSceneUv)
            return no(Reason::ConfigRevoked);
        const auto scale=ResolveStorageScale(a.scale,v.scale);
        if(!scale.eligible)return no(scale.reason);
        result.scaleRatio=scale.ratio;result.assumedScale=scale.assumed;
    }
    else {
        const auto scale=ResolveStorageScale(v.scale,v.scale);
        if(!scale.eligible)return no(scale.reason);
        result.assumedScale=scale.assumed;
    }
    return result;
}
}
