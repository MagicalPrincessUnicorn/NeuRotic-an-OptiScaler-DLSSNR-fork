#pragma once
#include <nr/context/PreparedGuideNormalization.h>

namespace DlssNr
{
// Frame projection after semantic normalization, before the physical-resource
// owner performs admission. Camera/exposure defaults are effective policy only.
template<class Frame>void ProjectPreparedGuideFrame(const Neurotic::Context::PreparedGuideValues& n,Frame& f)
{
    const auto& d=n.source;
    f.PreparedSource=d;
    f.Reset=n.reset;
    f.DepthInverted=d.depthReversed!=0;
    f.ColourIsLinearHdr=d.colorDomain==Neurotic::Contracts::ColorDomain::DisplayLinear;
    f.MvScaleX=static_cast<float>(n.motionPixelScale.x);f.MvScaleY=static_cast<float>(n.motionPixelScale.y);
    f.JitterX=f.JitterY=0;f.PreExposure=1;f.ExposureTexture=nullptr;
    f.RenderSubrectWidth=d.color.width;f.RenderSubrectHeight=d.color.height;
    f.DepthSubrectX=d.depth.x;f.DepthSubrectY=d.depth.y;
    f.DepthSubrectWidth=d.depth.width;f.DepthSubrectHeight=d.depth.height;
    f.MotionSubrectX=d.motion.x;f.MotionSubrectY=d.motion.y;
    f.MotionSubrectWidth=d.motion.width;f.MotionSubrectHeight=d.motion.height;
}
}
