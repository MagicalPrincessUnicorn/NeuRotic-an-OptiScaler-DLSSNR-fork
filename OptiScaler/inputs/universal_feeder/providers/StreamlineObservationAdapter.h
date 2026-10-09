#pragma once
#include "ObservationPublisher.h"

namespace Neurotic::Feed
{
template<class T>void StreamlineField(Callback& cb,std::string_view field,T value)noexcept
{
    if constexpr(std::is_floating_point_v<T>)
    {if(!std::isfinite(value)||value==(std::numeric_limits<float>::max)()){cb.Missing(field);return;}}
    cb.Value(field,value);
}
template<class T>void StreamlineBoolean(Callback& cb,std::string_view field,T value)noexcept
{if(static_cast<unsigned>(value)>1)cb.Missing(field);else cb.Value(field,static_cast<unsigned>(value)!=0);}
template<class T>void ObserveStreamlineConstants(Callback& cb,const T& d)noexcept
{
    if(!cb.Active())return;
    unsigned version=1;if constexpr(requires{d.structVersion;})version=d.structVersion;
    cb.Value("structVersion",version);
    // Unknown layouts remain descriptive. Do not copy a larger version's trailing fields.
    if(version!=1&&version!=2)return;
#define NR_FEED_FIELD(member) if constexpr(requires{d.member;})StreamlineField(cb,#member,d.member);else cb.Missing(#member)
    NR_FEED_FIELD(jitterOffset.x);NR_FEED_FIELD(jitterOffset.y);NR_FEED_FIELD(mvecScale.x);NR_FEED_FIELD(mvecScale.y);
    NR_FEED_FIELD(cameraNear);NR_FEED_FIELD(cameraFar);NR_FEED_FIELD(cameraFOV);NR_FEED_FIELD(cameraAspectRatio);
#undef NR_FEED_FIELD
#define NR_FEED_BOOL(member) if constexpr(requires{d.member;})StreamlineBoolean(cb,#member,d.member);else cb.Missing(#member)
    NR_FEED_BOOL(depthInverted);NR_FEED_BOOL(motionVectorsJittered);NR_FEED_BOOL(motionVectorsDilated);
    NR_FEED_BOOL(cameraMotionIncluded);NR_FEED_BOOL(reset);NR_FEED_BOOL(orthographicProjection);
#undef NR_FEED_BOOL
    if constexpr(requires{d.jitterOffset.x;d.jitterOffset.y;})
        cb.Jitter(std::isfinite(d.jitterOffset.x)&&std::isfinite(d.jitterOffset.y)&&
            d.jitterOffset.x!=(std::numeric_limits<float>::max)()&&d.jitterOffset.y!=(std::numeric_limits<float>::max)()?
            C::OptionalFact<C::Vec2>::FromKnown({d.jitterOffset.x,d.jitterOffset.y},cb.Evidence()):C::OptionalFact<C::Vec2>{});
}
template<class T>void ObserveStreamlineTag(Callback& cb,const T& tag)noexcept
{
    if(!cb.Active())return;
    cb.Value("tag.type",tag.type);cb.Value("tag.lifecycle",tag.lifecycle);
    cb.Value("tag.extent.left",tag.extent.left);cb.Value("tag.extent.top",tag.extent.top);
    cb.Value("tag.extent.width",tag.extent.width);cb.Value("tag.extent.height",tag.extent.height);
    if(tag.resource){cb.Value("tag.state",tag.resource->state);cb.Resource("tag.resource",C::SemanticKind::Other,tag.resource->native);}
    else {cb.Missing("tag.state");cb.Resource("tag.resource",C::SemanticKind::Other,nullptr);}
}
}
