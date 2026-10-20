// GPL-3.0. Standalone estimated image motion; appearance displacement, jitter unknown.
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
namespace nrw::guidance {
// Area-filtered encoded SDR luma. Adapted from IndependentFlow.h FlowLuma.
inline std::vector<float> Luma(const std::vector<uint8_t>& rgb,uint32_t width,uint32_t height,uint32_t aw,uint32_t ah) {
    if(!width || !height || !aw || !ah || rgb.size()!=size_t(width)*height*3)return {};
    std::vector<float> result(size_t(aw)*ah);
    for(uint32_t y=0;y<ah;++y)for(uint32_t x=0;x<aw;++x) {
        double lx=double(x)*width/aw,hx=double(x+1)*width/aw,ly=double(y)*height/ah,hy=double(y+1)*height/ah,sum=0,area=0;
        for(uint32_t sy=uint32_t(std::floor(ly));sy<std::min(height,uint32_t(std::ceil(hy)));++sy)
        for(uint32_t sx=uint32_t(std::floor(lx));sx<std::min(width,uint32_t(std::ceil(hx)));++sx) {
            double weight=(std::min(hx,double(sx+1))-std::max(lx,double(sx)))*(std::min(hy,double(sy+1))-std::max(ly,double(sy)));
            auto i=(size_t(sy)*width+sx)*3;sum+=weight*(.2126*rgb[i]+.7152*rgb[i+1]+.0722*rgb[i+2]);area+=weight;
        }
        result[size_t(y)*aw+x]=float(sum/area);
    }
    return result;
}
// Bounded 8x8 block SSD search. Output uses current guide pixels -> previous guide pixels.
// +/-12 guide pixels is the search limit, without engine history or jitter correction.
inline std::vector<float> Motion(const std::vector<float>& current,const std::vector<float>& previous,uint32_t w,uint32_t h) {
    if(!w || !h || current.size()!=size_t(w)*h || previous.size()!=current.size())return {};
    std::vector<float> output(size_t(w)*h*2);
    constexpr int block=8,radius=12;
    for(int by=0;by<int(h);by+=block)for(int bx=0;bx<int(w);bx+=block) {
        int bw=std::min(block,int(w)-bx),bh=std::min(block,int(h)-by),bestX=0,bestY=0;
        double best=std::numeric_limits<double>::infinity();
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
            if(bx+dx<0 || by+dy<0 || bx+dx+bw>int(w) || by+dy+bh>int(h))continue;
            double error=0;
            for(int y=0;y<bh && error<=best;++y)for(int x=0;x<bw;++x) {
                double delta=current[size_t(by+y)*w+bx+x]-previous[size_t(by+y+dy)*w+bx+x+dx];error+=delta*delta;
            }
            if(error<best || (error==best && dx*dx+dy*dy<bestX*bestX+bestY*bestY)){best=error;bestX=dx;bestY=dy;}
        }
        for(int y=0;y<bh;++y)for(int x=0;x<bw;++x) {auto i=(size_t(by+y)*w+bx+x)*2;output[i]=float(bestX);output[i+1]=float(bestY);}
    }
    return output;
}
}
