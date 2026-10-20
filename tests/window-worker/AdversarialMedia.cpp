// Audit-only probes of the actual production policies and motion implementation.
#include "../../apps/NeuRoticWindowWorker/nr/NrPolicy.h"
#include "../../apps/NeuRoticWindowWorker/guidance/ImageMotion.h"
#include <iostream>
#include <chrono>
int main() {
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    nrw::FrameStamp previous{1,1,1,1920,1080,false};
    auto current=previous;current.sequence=2;current.timestampQpc+=600*frequency.QuadPart;
    std::cout<<"pause_seconds=600 reset="<<nrw::detail::ResetHistory(current,previous)<<'\n';
    constexpr unsigned w=64,h=64;std::vector<float> a(w*h),b(w*h);uint32_t seed=17;
    for(auto& v:a){seed=1664525*seed+1013904223;v=float(seed>>24);}
    for(auto& v:b){seed=1664525*seed+1013904223;v=float(seed>>24);}
    auto flow=nrw::guidance::Motion(b,a,w,h);size_t nonzero=0;
    for(size_t i=0;i<flow.size();i+=2)nonzero+=(flow[i]!=0 || flow[i+1]!=0);
    nrw::NrOptions options;options.guides=nrw::GuideMode::Experimental;
    nrw::GuideResult guide;guide.stamp=current;guide.width=w;guide.height=h;
    guide.depthCompleted=guide.motionCompleted=true;guide.depth.assign(w*h,1);guide.motionXY=flow;
    std::cout<<"unrelated_images nonzero_motion_pixels="<<nonzero<<"/"<<w*h<<" admitted="<<nrw::detail::UseGuides(options,current,&guide)<<'\n';
    current.width=480;current.height=270;guide.stamp=current;guide.width=518;guide.height=294;
    guide.depth.assign(518*294,1);guide.motionXY.assign(518*294*2,0);
    std::cout<<"small_video=480x270 completed_depth_raster=518x294 admitted="<<nrw::detail::UseGuides(options,current,&guide)<<'\n';
    std::vector<float> flat(518*294,128);
    auto begin=std::chrono::steady_clock::now();auto flatFlow=nrw::guidance::Motion(flat,flat,518,294);
    std::cout<<"flat_518x294_CPU_motion_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" samples="<<flatFlow.size()<<'\n';
}
