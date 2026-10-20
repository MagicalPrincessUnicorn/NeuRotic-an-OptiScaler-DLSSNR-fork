#pragma once
#include <array>
#include <cmath>

namespace DlssNr::AdaptivePublicFixture
{
// Independent reference for constant, low-luminance background patches in the
// real controlled scene. Configuration is white=1, soft-knee, full transfer and
// colour, no comparison, no supersampling, guard=2. In these patches the knee
// and guard are inactive and residual composition reduces to the model result
// in linear light. This does not copy the production composition shader.
inline float ExpectedEditedChannel(float original)
{
    const double encoded=original<=0.0031308f?double(original)*12.92:
        1.055*std::pow(double(original),1.0/2.4)-0.055;
    const double edited=encoded*0.5+0.125;
    return float(edited<=0.04045?edited/12.92:std::pow((edited+0.055)/1.055,2.4));
}
struct CompositionSample
{
    unsigned x,y;
    std::array<float,3> original;
};
inline constexpr std::array<CompositionSample,4> CompositionSamples{{
    {2,2,{.125f,.125f,.25f}},
    {10,2,{.25f,.125f,.25f}},
    {2,10,{.25f,.125f,.25f}},
    {10,10,{.125f,.125f,.25f}},
}};
inline bool MatchesCompositionSample(const std::array<float,4>& actual,const CompositionSample& sample)
{
    // Allows RGBA16 rounding, sRGB round trips and the small hue correction
    // caused by proxy quantization. An unchanged image or zero model output
    // is well outside this bound at every selected background sample.
    for(unsigned channel=0;channel<3;++channel)
        if(!std::isfinite(actual[channel])||
           std::abs(actual[channel]-ExpectedEditedChannel(sample.original[channel]))>0.003f)return false;
    return std::isfinite(actual[3])&&std::abs(actual[3]-1.f)<0.001f;
}
}
