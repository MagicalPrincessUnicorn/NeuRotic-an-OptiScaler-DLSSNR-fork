#pragma once
#include "ObservationPublisher.h"

namespace Neurotic::Feed
{
#define NR_FEED_XESS_TRANSFORM 1
template<class T>void ObserveXessJitterTransform(Callback& cb,const T* d,float scaleX,float scaleY)noexcept
{
    if(!cb.Active()||!d)return;
    const auto number=[&](double value){return std::isfinite(value)?
        C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{value},cb.Evidence()):C::OptionalFact<C::ScalarValue>{};};
    // Stored scale/default is a legacy effective input. Its original setter observation is separate.
    cb.Scalar("legacy.jitterScaleX",{},number(scaleX));cb.Scalar("legacy.jitterScaleY",{},number(scaleY));
    cb.Scalar("jitterOffsetX",number(d->jitterOffsetX),number(d->jitterOffsetX*scaleX));
    cb.Scalar("jitterOffsetY",number(d->jitterOffsetY),number(d->jitterOffsetY*scaleY));
    C::Symbol formula;if(!formula.Assign("XeSS.jitterOffset.Multiply.context.jitterScale"))return;
    cb.Scalar("transform.jitter.formula",C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{formula},cb.Evidence()));
    C::Symbol units;if(!units.Assign("Pixels"))return;
    cb.Scalar("transform.jitter.outputUnits",C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{units},cb.Evidence()));
    // Frame/raster association remains owner-established; no size or native handle becomes identity.
}
template<class T>void ObserveXessExecute(Callback& cb,const T* d)noexcept
{
    if(!cb.Active()||!d)return;
    try
    {
#define NR_FEED_FIELD(member) if constexpr(requires{d->member;})cb.Value(#member,d->member);else cb.Missing(#member)
        NR_FEED_FIELD(jitterOffsetX);NR_FEED_FIELD(jitterOffsetY);NR_FEED_FIELD(exposureScale);NR_FEED_FIELD(resetHistory);
        NR_FEED_FIELD(inputWidth);NR_FEED_FIELD(inputHeight);
        NR_FEED_FIELD(inputColorBase.x);NR_FEED_FIELD(inputColorBase.y);
        NR_FEED_FIELD(inputDepthBase.x);NR_FEED_FIELD(inputDepthBase.y);
        NR_FEED_FIELD(inputMotionVectorBase.x);NR_FEED_FIELD(inputMotionVectorBase.y);
        NR_FEED_FIELD(outputColorBase.x);NR_FEED_FIELD(outputColorBase.y);
        NR_FEED_FIELD(inputResponsiveMaskBase.x);NR_FEED_FIELD(inputResponsiveMaskBase.y);
#undef NR_FEED_FIELD
#define NR_FEED_RESOURCE(member,vulkan,kind) if constexpr(requires{d->member;})cb.Resource(#member,C::SemanticKind::kind,d->member);else if constexpr(requires{d->vulkan;})cb.Resource(#vulkan,C::SemanticKind::kind,&d->vulkan);else cb.Resource(#member,C::SemanticKind::kind,nullptr)
        NR_FEED_RESOURCE(pColorTexture,colorTexture,Color);NR_FEED_RESOURCE(pOutputTexture,outputTexture,Color);
        NR_FEED_RESOURCE(pDepthTexture,depthTexture,Depth);NR_FEED_RESOURCE(pVelocityTexture,velocityTexture,Motion);
        NR_FEED_RESOURCE(pExposureScaleTexture,exposureScaleTexture,Exposure);
        NR_FEED_RESOURCE(pResponsivePixelMaskTexture,responsivePixelMaskTexture,Mask);
#undef NR_FEED_RESOURCE
        if constexpr(requires{d->jitterOffsetX;d->jitterOffsetY;})
            cb.Jitter(C::OptionalFact<C::Vec2>::FromKnown({d->jitterOffsetX,d->jitterOffsetY},cb.Evidence()));
    }catch(...){}
}
template<class T>void ObserveXessInit(Callback& cb,const T* d)noexcept
{
    if(!cb.Active()||!d)return;
    cb.Value("initFlags",d->initFlags);cb.Value("qualitySetting",d->qualitySetting);
    cb.Value("outputResolution.x",d->outputResolution.x);cb.Value("outputResolution.y",d->outputResolution.y);
    if constexpr(requires{d->creationNodeMask;})cb.Value("creationNodeMask",d->creationNodeMask);
    if constexpr(requires{d->visibleNodeMask;})cb.Value("visibleNodeMask",d->visibleNodeMask);
}
}
