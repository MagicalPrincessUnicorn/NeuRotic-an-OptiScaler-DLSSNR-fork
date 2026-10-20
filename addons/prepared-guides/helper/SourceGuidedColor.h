#pragma once
#include "../../../OptiScaler/nr/context/PreparedRasterBounds.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>
namespace nrpg
{
// Opt-in encoded-SDR color experiment. Preserve processed high-frequency luma,
// move its 3x3 low-frequency luma/chroma toward the SAME capture's source, and
// damp processed detail where this capture's distrust requests it. No temporal
// history, guessed camera, or previous-frame color is introduced here.
inline bool StabilizeColor(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,
    std::uint32_t width,std::uint32_t height,float strength,std::span<const float> distrust={})
{
    const auto count=std::uint64_t(width)*height;
    if(!Neurotic::PreparedRaster::Admit(width,height) || source.size()!=count*4 || output.size()!=count*4 ||
       !std::isfinite(strength) || strength<0 || strength>1 || (!distrust.empty() && distrust.size()!=count))return false;
    for(float d:distrust)if(!std::isfinite(d) || d<0 || d>1)return false;
    if(strength==0)return true;
    // Two bounded scalar planes avoid an in-place neighborhood feedback loop.
    std::vector<float> inputLuma(static_cast<std::size_t>(count)),processedLuma(inputLuma.size());
    const auto luma=[](const std::uint8_t* p){return .0722f*p[0]+.7152f*p[1]+.2126f*p[2];};
    for(std::size_t i=0;i<count;++i){inputLuma[i]=luma(source.data()+i*4);processedLuma[i]=luma(output.data()+i*4);}
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
        float delta=0;unsigned samples=0;
        for(auto sy=y?y-1:0;sy<=(std::min)(y+1,height-1);++sy)
            for(auto sx=x?x-1:0;sx<=(std::min)(x+1,width-1);++sx){
                const auto j=std::size_t(sy)*width+sx;delta+=inputLuma[j]-processedLuma[j];++samples;}
        const auto i=std::size_t(y)*width+x;const float trusted=distrust.empty()?1.f:1.f-distrust[i];
        const float detail=(processedLuma[i]+delta/samples-inputLuma[i])*trusted;
        for(std::size_t c=0;c<3;++c){
            const float target=source[i*4+c]+detail;
            const float value=output[i*4+c]+strength*(target-output[i*4+c]);
            output[i*4+c]=static_cast<std::uint8_t>(std::clamp(std::lround(value),0L,255L));}
        output[i*4+3]=source[i*4+3];
    }
    return true;
}
}
