#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include "SemanticMath.h"
#include <optional>

namespace Neurotic::Context
{
#define NR_PREPARED_CONTINUITY_V1 1
namespace Prepared=Feed::Prepared;
struct PreparedGuideValues
{
    Prepared::Descriptor source;
    C::Vec2 motionPixelScale;
    bool reset=false;
};
struct PreparedGuideResult
{
    std::optional<PreparedGuideValues> value;
    std::string_view reason;
    explicit operator bool()const noexcept{return value.has_value();}
};
inline bool PreparedHistoryContinuous(const Prepared::Descriptor& a,const Prepared::Descriptor& b)noexcept
{
    return !(b.flags&Prepared::Reset)&&a.capture==b.previousCapture&&a.capture<b.capture&&
        a.producer==b.producer&&a.session==b.session&&a.stream==b.stream&&a.device==b.device&&
        a.generation==b.generation&&a.color==b.color&&a.depth==b.depth&&a.motion==b.motion&&
        a.sourceApi==b.sourceApi&&a.colorDomain==b.colorDomain&&a.depthKind==b.depthKind&&
        a.depthReversed==b.depthReversed&&a.motionUnits==b.motionUnits&&a.motionDirection==b.motionDirection&&
        a.motionGridWidth==b.motionGridWidth&&a.motionGridHeight==b.motionGridHeight&&
        a.scaleX==b.scaleX&&a.scaleY==b.scaleY&&a.rasterOrigin==b.rasterOrigin&&
        a.colorOrigin==b.colorOrigin&&a.depthOrigin==b.depthOrigin&&a.motionOrigin==b.motionOrigin&&
        ((a.flags^b.flags)&(Prepared::HudIncluded|Prepared::ZeroJitterPolicy))==0;
}
inline bool ValidPreparedRaster(const Prepared::Raster& r)noexcept
{
    constexpr std::uint32_t limit=16384;
    return r.allocationWidth && r.allocationHeight && r.allocationWidth<=limit && r.allocationHeight<=limit &&
        r.width && r.height && r.x<=r.allocationWidth && r.y<=r.allocationHeight &&
        r.width<=r.allocationWidth-r.x && r.height<=r.allocationHeight-r.y;
}
// Pure semantic admission. Physical resources, exact API queue ordering and C03
// leases must be independently established at the consuming boundary.
inline PreparedGuideResult NormalizePreparedGuides(const Prepared::Descriptor& d)noexcept
{
    const auto refuse=[](std::string_view why){return PreparedGuideResult{{},why};};
    if(d.size!=sizeof(d)||d.version!=Prepared::Version)return refuse("Prepared.UnsupportedVersion");
    for(auto v:d.reserved)if(v)return refuse("Prepared.ReservedField");
    if(d.flags&~(Prepared::Reset|Prepared::HasDistrust|Prepared::HudIncluded|Prepared::ZeroJitterPolicy))
        return refuse("Prepared.UnknownFlags");
    if(!d.producer||!d.session||!d.stream||!d.device||!d.generation||!d.capture)
        return refuse("Prepared.IdentityMissing");
    if(d.previousCapture>=d.capture||(!(d.flags&Prepared::Reset)&&!d.previousCapture))
        return refuse("Prepared.InvalidCapturePair");
    const auto observed=[](C::SourceClass p){return p==C::SourceClass::HostObserved||p==C::SourceClass::External;};
    if(!observed(d.colorOrigin)||!observed(d.depthOrigin)||
       (d.motionOrigin!=C::SourceClass::Derived&&d.motionOrigin!=C::SourceClass::External)||
       (d.maskOrigin!=C::SourceClass::Derived&&d.maskOrigin!=C::SourceClass::External))
        return refuse("Prepared.UnsupportedProvenance");
    if(C::EnumName(d.sourceApi).empty()||d.sourceApi==C::GraphicsApi::Other)
        return refuse("Prepared.UnknownSourceApi");
    if(d.rasterOrigin!=C::RasterOrigin::TopLeft||!ValidPreparedRaster(d.color)||
       !ValidPreparedRaster(d.depth)||!ValidPreparedRaster(d.motion))return refuse("Prepared.InvalidRaster");
    if(d.depth.width!=d.color.width||d.depth.height!=d.color.height)
        return refuse("Prepared.DepthCoverage");
    if(d.colorDomain!=C::ColorDomain::EncodedDisplay && d.colorDomain!=C::ColorDomain::DisplayLinear)
        return refuse("Prepared.UnsupportedColor");
    if(d.depthKind!=C::DepthKind::Device||d.depthReversed>1)return refuse("Prepared.UnsupportedDepth");
    if(!(d.flags&Prepared::ZeroJitterPolicy))return refuse("Prepared.JitterPolicyMissing");
    if(d.motionDirection!=C::MotionDirection::CurrentToPrevious)
        return refuse("Prepared.DirectionNeedsResampling");
    if(!d.motionGridWidth||!d.motionGridHeight||d.motionGridWidth>32||d.motionGridHeight>32||
       d.motion.width!=(d.color.width+d.motionGridWidth-1)/d.motionGridWidth||
       d.motion.height!=(d.color.height+d.motionGridHeight-1)/d.motionGridHeight)
        return refuse("Prepared.MotionGridCoverage");
    C::Vec2 scale{d.scaleX,d.scaleY};
    if(!Finite(scale)||scale.x==0||scale.y==0)return refuse("Prepared.InvalidScale");
    switch(d.motionUnits)
    {
    case C::MotionUnits::Pixels: break;
    case C::MotionUnits::ActiveRasterUV: scale.x*=d.color.width;scale.y*=d.color.height;break;
    case C::MotionUnits::NDC: scale.x*=d.color.width*.5;scale.y*=d.color.height*.5;break;
    case C::MotionUnits::Blocks: scale.x*=d.motionGridWidth;scale.y*=d.motionGridHeight;break;
    default:return refuse("Prepared.UnknownMotionUnits");
    }
    if(!Finite(scale)||std::abs(scale.x)>1e9||std::abs(scale.y)>1e9)return refuse("Prepared.ScaleOverflow");
    if(d.flags&Prepared::HasDistrust)
    {
        if(!ValidPreparedRaster(d.distrust)||d.distrust.width!=d.color.width||d.distrust.height!=d.color.height||
           d.maskCapture!=d.capture)return refuse("Prepared.MaskCoverageOrCapture");
    }
    else if(d.maskCapture||d.distrust!=Prepared::Raster{})return refuse("Prepared.UndeclaredMask");
    return {PreparedGuideValues{d,scale,(d.flags&Prepared::Reset)!=0},{}};
}
}
