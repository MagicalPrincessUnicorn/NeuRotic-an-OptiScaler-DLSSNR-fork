#pragma once
#include "PreparedHelperProtocol.h"
#include "../../../apps/NeuRoticWindowWorker/WorkerContracts.h"
#include <limits>
namespace Neurotic::Feed::Prepared::Helper
{
struct Payload {std::vector<std::uint8_t> color;std::vector<float> depth,motion,distrust;};
inline bool Normalize(const Request& request,const Payload& payload,const nrw::FrameStamp& stamp,
    nrw::GuideResult& guides,std::string& reason) {
    const auto reject=[&](const char* why){reason=why;return false;};
    if(!Validate(request)||request.flags&Cancel)return reject("Helper.InvalidRequest");
    const auto normalized=Context::NormalizePreparedGuides(request.guides);if(!normalized)return reject("Helper.SemanticAdmission");
    const auto& d=request.guides;const auto pixels=std::size_t(d.color.width)*d.color.height;
    const auto motionPixels=std::size_t(d.motion.width)*d.motion.height;
    if(stamp.session!=d.session||stamp.sequence!=d.capture||stamp.timestampQpc!=request.timestampQpc||
       stamp.width!=d.color.width||stamp.height!=d.color.height)return reject("Helper.StampMismatch");
    if(payload.color.size()!=pixels*4||payload.depth.size()!=pixels||payload.motion.size()!=motionPixels*2||
       payload.distrust.size()!=((d.flags&HasDistrust)?pixels:0))return reject("Helper.PayloadSize");
    for(float value:payload.depth)if(!std::isfinite(value)||value<0||value>1)return reject("Helper.DeviceDepthRange");
    for(float value:payload.motion)if(!std::isfinite(value))return reject("Helper.MotionNonfinite");
    for(float value:payload.distrust)if(!std::isfinite(value)||value<0||value>1)return reject("Helper.DistrustRange");
    nrw::GuideResult output;output.stamp=stamp;output.width=d.color.width;output.height=d.color.height;
    output.depth=payload.depth;output.motionXY.resize(pixels*2);output.rawDeviceDepth=true;
    output.rawDeviceDepthReversed=d.depthReversed!=0;output.estimated=true;
    // Expand block sampling, independently of displacement units. Values become
    // current-to-previous COLOR-pixel displacement exactly once. NrHost then uses
    // its existing guide-pixel -> model-pixel ratio; no units conversion repeats.
    for(std::uint32_t y=0;y<output.height;++y)for(std::uint32_t x=0;x<output.width;++x){
        const auto grid=std::size_t(y/d.motionGridHeight)*d.motion.width+x/d.motionGridWidth;
        const auto pixel=std::size_t(y)*output.width+x;
        const double vx=payload.motion[grid*2]*normalized.value->motionPixelScale.x;
        const double vy=payload.motion[grid*2+1]*normalized.value->motionPixelScale.y;
        if(!std::isfinite(vx)||!std::isfinite(vy)||std::abs(vx)>std::numeric_limits<float>::max()||
           std::abs(vy)>std::numeric_limits<float>::max())return reject("Helper.MotionScaleOverflow");
        output.motionXY[pixel*2]=static_cast<float>(vx);output.motionXY[pixel*2+1]=static_cast<float>(vy);
    }
    output.depthCompleted=output.motionCompleted=true;
    output.depthProvider="Prepared.DeviceDepth";output.motionProvider="Prepared.DerivedMotion";
    output.reason=(d.flags&HasDistrust)?"Prepared depth/derived motion; distrust retained as diagnostics only":"Prepared depth/derived motion";
    guides=std::move(output);reason.clear();return true;
}
}
