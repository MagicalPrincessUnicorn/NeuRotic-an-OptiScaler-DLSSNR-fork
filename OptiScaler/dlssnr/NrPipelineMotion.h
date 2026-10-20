#pragma once
#include <array>
#include <algorithm>
namespace DlssNr::PipelineUi {
struct Motion {
    std::array<float,7> from{},target{},position{};
    double started=0,last=0;
    bool initialized=false;
    std::array<float,7> Update(const std::array<float,7>& next,double now,bool reduced,bool snap=false) {
        if(!initialized||reduced||snap||now<last){from=target=position=next;started=now;initialized=true;}
        else {
            const float t=std::clamp(float((now-started)/.18),0.f,1.f),e=t*t*(3.f-2.f*t);
            for(size_t i=0;i<position.size();++i)position[i]=t>=1?target[i]:from[i]+(target[i]-from[i])*e;
            if(next!=target){from=position;target=next;started=now;}
        }
        last=now;return position;
    }
};
}
