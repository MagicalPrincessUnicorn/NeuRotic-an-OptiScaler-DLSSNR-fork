#pragma once
#include "ObservationPublisher.h"
#include <nr/context/PreparedGuideNormalization.h>

namespace Neurotic::Feed
{
inline void ObservePreparedGuides(Callback& cb,const Context::PreparedGuideValues& value,
    const void* color,const void* depth,const void* motion,const void* distrust)noexcept
{
    if(!cb.Active())return;
    const auto& d=value.source;
    cb.Value("prepared.version",d.version);cb.Value("prepared.producer",d.producer);
    cb.Value("prepared.session",d.session);cb.Value("prepared.stream",d.stream);
    cb.Value("prepared.device",d.device);cb.Value("prepared.generation",d.generation);
    cb.Value("prepared.capture",d.capture);cb.Value("prepared.previousCapture",d.previousCapture);
    cb.Value("prepared.motionGridX",d.motionGridWidth);cb.Value("prepared.motionGridY",d.motionGridHeight);
    cb.Scalar("prepared.motionPixelScaleX",{},C::OptionalFact<C::ScalarValue>::FromKnown(value.motionPixelScale.x,cb.Evidence()));
    cb.Scalar("prepared.motionPixelScaleY",{},C::OptionalFact<C::ScalarValue>::FromKnown(value.motionPixelScale.y,cb.Evidence()));
    // Zero jitter is an explicit effective policy, never observed camera jitter.
    cb.Scalar("prepared.jitterX",{},C::OptionalFact<C::ScalarValue>::FromKnown(0.0,cb.Evidence()));
    cb.Scalar("prepared.jitterY",{},C::OptionalFact<C::ScalarValue>::FromKnown(0.0,cb.Evidence()));
    cb.Value("prepared.reset",value.reset);cb.Value("prepared.hudIncluded",bool(d.flags&Prepared::HudIncluded));
    cb.PreparedResource("color",C::SemanticKind::Color,color,d.colorOrigin);
    cb.PreparedResource("depth",C::SemanticKind::Depth,depth,d.depthOrigin);
    cb.PreparedResource("motion",C::SemanticKind::Motion,motion,d.motionOrigin);
    if(d.flags&Prepared::HasDistrust)cb.PreparedResource("distrust",C::SemanticKind::Mask,distrust,d.maskOrigin);
}
}
