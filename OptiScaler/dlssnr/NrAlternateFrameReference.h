#pragma once
#include "NrAlternateFrameContract.h"
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <algorithm>

namespace DlssNr::AlternateFrame
{
struct ScenePixel {float r=0,g=0,b=0,a=0;};
struct ResidualSample {std::array<float,3> rgb{};bool valid=false;};
enum class DepthTest {DeviceNormalized,LinearView,ComparableAnchor};
struct ReferenceInput {
    ScenePixel base;
    float qx=0,qy=0,currentDepth=0,expectedAnchorDepth=0,tolerance=.002f,ratio=1;
    unsigned width=0,height=0;
    std::span<const ResidualSample> residual;
    std::span<const float> depths;
    DepthTest depthTest=DepthTest::DeviceNormalized;
};
struct CarryPixelResult {bool writeCurrentTarget=false;ScenePixel pixel;Reason reason=Reason::None;};
struct StorageScaleFacts {
    std::uint64_t domain=0;
    std::optional<float> scale;
    bool contradicted=false,fixedUnits=false;
};
struct ScaleDecision {bool eligible=false;float ratio=1;bool assumed=false;Reason reason=Reason::None;};

// Independent IEEE754 binary16 rounding, including ties and subnormals.
// No production shader helper or GPU output supplies this oracle.
inline float RoundStoredHalf(float x) noexcept
{
    const auto bits=std::bit_cast<std::uint32_t>(x);
    const auto sign=bits&0x80000000u;
    const auto absolute=bits&0x7fffffffu;
    if(absolute>=0x7f800000u)return x;
    const int exponent=static_cast<int>((absolute>>23)&255)-127;
    if(exponent>15)return std::bit_cast<float>(sign|0x7f800000u);
    if(exponent < -25)return std::bit_cast<float>(sign);
    const auto mantissa=(absolute&0x7fffffu)|0x800000u;
    const unsigned shift=static_cast<unsigned>(exponent < -14 ? -exponent-1 : 13);
    const auto quotient=mantissa>>shift;
    const auto remainder=mantissa&((1u<<shift)-1);
    const auto midpoint=1u<<(shift-1);
    const auto rounded=quotient+(remainder>midpoint||(remainder==midpoint&&(quotient&1u)));
    const float magnitude=std::ldexp(static_cast<float>(rounded),exponent < -14 ? -24 : exponent-10);
    return std::copysign(magnitude,x);
}
inline bool Finite(ScenePixel p)noexcept
{return std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a);}
inline ResidualSample CaptureResidual(const ScenePixel& base,const ScenePixel& enhanced) noexcept
{
    ResidualSample r;
    if(!std::isfinite(base.r)||!std::isfinite(base.g)||!std::isfinite(base.b)||
       !std::isfinite(enhanced.r)||!std::isfinite(enhanced.g)||!std::isfinite(enhanced.b))return r;
    const std::array delta{enhanced.r-base.r,enhanced.g-base.g,enhanced.b-base.b};
    for(float v:delta)if(!std::isfinite(v)||std::abs(v)>65504)return r;
    for(unsigned c=0;c<3;++c)r.rgb[c]=RoundStoredHalf(delta[c]);
    r.valid=true;return r;
}
inline bool AcceptDepth(float anchor,const ReferenceInput& i)noexcept
{
    if(!std::isfinite(anchor)||!std::isfinite(i.currentDepth))return false;
    if(i.depthTest==DepthTest::DeviceNormalized)
        return anchor>=0&&anchor<=1&&i.currentDepth>=0&&i.currentDepth<=1&&std::abs(anchor-i.currentDepth)<=.002f;
    if(i.depthTest==DepthTest::LinearView)
        return anchor>0&&i.currentDepth>0&&std::abs(anchor-i.currentDepth)<=.01f*(std::max)(anchor,i.currentDepth);
    return std::isfinite(i.expectedAnchorDepth)&&std::isfinite(i.tolerance)&&i.tolerance>=0&&
        std::abs(anchor-i.expectedAnchorDepth)<=i.tolerance;
}
inline CarryPixelResult ReconstructPixel(const ReferenceInput& i) noexcept
{
    CarryPixelResult r{false,i.base,Reason::None};
    const auto reject=[&](Reason reason){r.reason=reason;return r;};
    if(!Finite(i.base))return reject(Reason::OriginalInputNonfinite);
    if(!std::isfinite(i.ratio)||i.ratio<=0)return reject(Reason::ExposureScaleInvalid);
    if(!i.width||!i.height||i.width>8192||i.height>8192||
       i.residual.size()!=static_cast<std::size_t>(i.width)*i.height||i.depths.size()!=i.residual.size())
        return reject(Reason::AnchorInvalid);
    if(!std::isfinite(i.qx)||!std::isfinite(i.qy))return reject(Reason::MappingNonfinite);
    if(i.qx<0||i.qx>1||i.qy<0||i.qy>1)return reject(Reason::AnchorBounds);
    const float ux=i.qx*i.width-.5f,uy=i.qy*i.height-.5f;
    const int x=static_cast<int>(std::floor(ux)),y=static_cast<int>(std::floor(uy));
    const float tx=ux-std::floor(ux),ty=uy-std::floor(uy);
    std::array<float,3> sum{};
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
        const float weight=(dx?tx:1-tx)*(dy?ty:1-ty);
        if(weight<=0)continue;
        if(x+dx<0||y+dy<0||x+dx>=static_cast<int>(i.width)||y+dy>=static_cast<int>(i.height))
            return reject(Reason::AnchorBounds);
        const auto index=static_cast<std::size_t>(y+dy)*i.width+x+dx;
        const auto& tap=i.residual[index];
        if(!tap.valid)return reject(Reason::AnchorInvalid);
        if(!AcceptDepth(i.depths[index],i))return reject(Reason::DepthMismatch);
        for(unsigned c=0;c<3;++c){if(!std::isfinite(tap.rgb[c]))return reject(Reason::ResidualNonfinite);sum[c]+=weight*tap.rgb[c];}
    }
    ScenePixel candidate{i.base.r+i.ratio*sum[0],i.base.g+i.ratio*sum[1],i.base.b+i.ratio*sum[2],i.base.a};
    if(!Finite(candidate)||std::abs(candidate.r)>65504||std::abs(candidate.g)>65504||
       std::abs(candidate.b)>65504||std::abs(candidate.a)>65504)return reject(Reason::OutputRange);
    r.writeCurrentTarget=true;r.pixel=candidate;return r;
}
inline ScaleDecision ResolveStorageScale(const StorageScaleFacts& a,const StorageScaleFacts& b) noexcept
{
    if(a.contradicted||b.contradicted)return {false,1,false,Reason::SceneDomainContradicted};
    if(!a.domain||a.domain!=b.domain)return {false,1,false,Reason::SceneDomainUnknown};
    if(a.fixedUnits!=b.fixedUnits||a.scale.has_value()!=b.scale.has_value())return {false,1,false,Reason::ExposureScaleTransition};
    if(a.fixedUnits&&(a.scale||b.scale))return {false,1,false,Reason::ExposureScaleInvalid};
    if(!a.scale)return {true,1,!a.fixedUnits,Reason::None};
    if(!std::isfinite(*a.scale)||!std::isfinite(*b.scale)||*a.scale<=0||*b.scale<=0)
        return {false,1,false,Reason::ExposureScaleInvalid};
    const float ratio=*b.scale / *a.scale;
    if(!std::isfinite(ratio)||ratio<.25f||ratio>4)return {false,1,false,Reason::ExposureRatioOutOfRange};
    return {true,ratio,false,Reason::None};
}
}
