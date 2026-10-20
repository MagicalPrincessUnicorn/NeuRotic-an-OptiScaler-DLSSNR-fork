#pragma once
#include "D3D12Connection.h"
#include "ConnectionPolicy.h"

namespace DlssNr::Connections {
// The prepared owner invokes this inside the SAME acquisition callback after
// SourceIdle, while source writes are excluded. This local capture identity is
// not an engine frame identity and MUST NOT be exported to unrelated callbacks.
// Legacy Native/Present observations do not establish this boundary.
inline bool CapturePreparedInputs(D3D12Connection& connection,const D3D12ObservedFrame& frame,
    Source anchorSource,Source depthSource,Source motionSource,const Mixed::Policy& policy,
    PendingFrame& pending,SignalStatusWireV2& status,std::string& reason) {
    const auto& d=frame.description;
    Mixed::Anchor anchor{{d.session,d.stream,d.stream,d.generation,d.device,d.capture,0,Mixed::IdentityScope::CoherentCapture},
        Mixed::Stage::Present,anchorSource,d.color.width,d.color.height,d.previousCapture};
    std::array<D3D12SignalOffer,3> offers{};
    offers[0].frame=frame;auto& color=offers[0].info;
    color.signal=Mixed::Signal::Color;color.source=anchorSource;color.frame=anchor.frame;
    color.stage=anchor.stage;color.providerGeneration=d.generation;color.width=d.color.width;color.height=d.color.height;
    color.conventionsKnown=true;
    offers[1].frame=frame;auto& depth=offers[1].info;
    depth.signal=Mixed::Signal::Depth;depth.source=depthSource;depth.frame=anchor.frame;
    depth.stage=anchor.stage;depth.providerGeneration=d.generation;depth.width=d.depth.width;depth.height=d.depth.height;
    depth.conventionsKnown=true;
    offers[2].frame=frame;offers[2].info=depth;offers[2].info.signal=Mixed::Signal::Motion;
    offers[2].info.source=motionSource;offers[2].info.previous=d.previousCapture;
    offers[2].info.width=d.motion.width;offers[2].info.height=d.motion.height;
    Mixed::Selection result;const auto count=frame.deriveMotion?2u:3u;
    const bool captured=connection.CaptureMixed(frame,anchor,{offers.data(),count},policy,pending,result,reason);
    status={};status.capture=d.capture;status.generation=d.generation;status.revision=policy.revision;
    status.color=result.color>=0?uint32_t(offers[result.color].info.source):0;
    status.colorReason=uint32_t(result.colorReason);
    status.colorOrigin=result.color>=0?uint32_t(offers[result.color].frame.description.colorOrigin)+1:0;
    status.depth=result.depth>=0?uint32_t(offers[result.depth].info.source):0;
    status.motion=result.motion>=0?uint32_t(offers[result.motion].info.source):result.requestMotion?uint32_t(Source::BuiltIn):0;
    status.depthReason=uint32_t(result.depthReason);status.motionReason=uint32_t(result.motionReason);
    status.depthOrigin=result.depth>=0?uint32_t(offers[result.depth].frame.description.depthOrigin)+1:0;
    status.motionOrigin=result.motion>=0?uint32_t(offers[result.motion].frame.description.motionOrigin)+1:
        result.requestMotion?uint32_t(Neurotic::Feed::Prepared::C::SourceClass::Derived)+1:0;
    if(result.requestMotion)status.motionReason=uint32_t(policy.motion==Source::Automatic||policy.motion==Source::BuiltIn?
        Mixed::Reason::NeedsActivation:Mixed::Reason::PreferenceFallback);
    // Semantic selection does not imply physical acquisition succeeded.
    if(!captured){status.color=status.depth=status.motion=0;status.colorOrigin=status.depthOrigin=status.motionOrigin=0;}
    return captured;
}
}
