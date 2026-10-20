// GPL-3.0. Bounded host timing evidence; Present acceptance is not display completion.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace nrw {
struct FrameTiming {
    double endMs=0,captureMs=0,guidanceMs=0,nrMs=0,presentMs=0,totalMs=0,ageMs=0;
    bool accepted=false,ageKnown=true;
};
class FrameTimings {
    std::array<FrameTiming,120> frames{};
    size_t count=0,next=0;
    bool discontinuity=false;
public:
    void Clear(){frames={};count=next=0;discontinuity=false;}
    void Pause(){discontinuity=true;}
    size_t Count()const{return count;}
    FrameTiming Last()const{return count?frames[(next+frames.size()-1)%frames.size()]:FrameTiming{};}
    void Add(FrameTiming sample){
        for(auto value:{sample.endMs,sample.captureMs,sample.guidanceMs,sample.nrMs,sample.presentMs,sample.totalMs,sample.ageMs})if(!std::isfinite(value)||value<0)return;
        if(count&&sample.endMs<=Last().endMs)return;
        if(discontinuity)Clear();
        frames[next]=sample;next=(next+1)%frames.size();count=std::min(count+1,frames.size());
    }
    double Fps()const{
        if(count<2)return 0;
        auto first=frames[count==frames.size()?next:0].endMs;
        return Last().endMs>first?1000.*double(count-1)/(Last().endMs-first):0;
    }
    double P95Age()const{
        std::array<double,120> ages{};size_t n=0;
        for(size_t i=0;i<count;++i)if(frames[i].ageKnown)ages[n++]=frames[i].ageMs;
        if(!n)return 0;std::sort(ages.begin(),ages.begin()+n);
        return ages[size_t(std::ceil(.95*double(n)))-1];
    }
};
}
