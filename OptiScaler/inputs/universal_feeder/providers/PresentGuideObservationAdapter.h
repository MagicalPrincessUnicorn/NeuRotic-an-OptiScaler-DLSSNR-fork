#pragma once
#include "ObservationPublisher.h"

namespace Neurotic::Feed
{
// DlssNrFrameInfo has legacy defaults and no per-field presence bits. Preserve its effective
// values explicitly; raw native evidence comes from the earlier NGX/provider observation.
template<class Frame>void ObservePresentGuide(Callback& cb,const Frame& frame,const void* depth,const void* motion)noexcept
{
    if(!cb.Active())return;
    const auto effective=[&](std::string_view key,double value){
        cb.Scalar(key,{},std::isfinite(value)?C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{value},cb.Evidence()):C::OptionalFact<C::ScalarValue>{});};
    effective("legacy.MvScaleX",frame.MvScaleX);effective("legacy.MvScaleY",frame.MvScaleY);
    effective("legacy.JitterX",frame.JitterX);effective("legacy.JitterY",frame.JitterY);
    effective("legacy.PreExposure",frame.PreExposure);
    if constexpr(requires{frame.Reset;})effective("legacy.Reset",frame.Reset?1.0:0.0);
    if constexpr(requires{frame.DepthInverted;})effective("legacy.DepthInverted",frame.DepthInverted?1.0:0.0);
    if constexpr(requires{frame.ColourIsLinearHdr;})effective("legacy.ColourIsLinearHdr",frame.ColourIsLinearHdr?1.0:0.0);
    effective("legacy.RenderSubrectWidth",frame.RenderSubrectWidth);effective("legacy.RenderSubrectHeight",frame.RenderSubrectHeight);
    cb.Resource("depth",C::SemanticKind::Depth,depth);cb.Resource("motion",C::SemanticKind::Motion,motion);
}
}
