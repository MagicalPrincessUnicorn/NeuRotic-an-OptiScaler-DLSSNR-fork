// GPL-3.0. Adapted from IndependentDepthTensor.h; explicit SDR bicubic fixed profile.
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace nrw::guidance
{
inline bool DepthProfile(uint32_t width,uint32_t height)
{return width && height && width<=3840 && height<=2160;}
// Encoded SDR RGB, not linear-light color. CPU preprocessing belongs to the
// serialized preview worker, never Present. This port makes no zero-copy claim.
inline std::vector<float> DepthInput(const std::vector<uint8_t>& rgb,uint32_t width,uint32_t height)
{
    if(!DepthProfile(width,height) || rgb.size()!=static_cast<size_t>(width)*height*3)return {};
    constexpr uint32_t w=518,h=294;constexpr float mean[]{.485f,.456f,.406f},sd[]{.229f,.224f,.225f};
    const auto cubic=[](float x){x=std::abs(x);constexpr float a=-.75f;
        if(x<=1)return ((a+2)*x-(a+3))*x*x+1;
        if(x<2)return ((a*x-5*a)*x+8*a)*x-4*a;
        return 0.f;};
    std::vector<float> tensor(3*w*h);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
    {
        // Match OpenCV's half-pixel coordinates: compute the position in
        // double, then cast the fractional coefficient to FP32. Rounding the
        // whole 4K coordinate first magnifies error at sharp image edges.
        const double px=(x+.5)*width/w-.5,py=(y+.5)*height/h-.5;
        const int bx=static_cast<int>(std::floor(px)),by=static_cast<int>(std::floor(py));
        const float fx=static_cast<float>(px-bx),fy=static_cast<float>(py-by);
        float color[3]{};
        for(int dy=-1;dy<=2;++dy)for(int dx=-1;dx<=2;++dx)
        {
            const auto sx=std::clamp(bx+dx,0,static_cast<int>(width)-1),sy=std::clamp(by+dy,0,static_cast<int>(height)-1);
            const float weight=cubic(fx-dx)*cubic(fy-dy);
            for(size_t c=0;c<3;++c)color[c]+=weight*(rgb[(static_cast<size_t>(sy)*width+sx)*3+c]/255.f);
        }
        for(size_t c=0;c<3;++c)tensor[c*w*h+y*w+x]=(color[c]-mean[c])/sd[c];
    }
    return tensor;
}
}

