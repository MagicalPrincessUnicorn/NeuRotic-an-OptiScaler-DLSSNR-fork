#pragma once
#include <cmath>
#include <cstdint>
namespace DlssNr::NativeFg {
struct Decision { bool accepted=false,reset=true,interpolate=false; };
class History {
    std::uint64_t session_=0,generation_=0,frame_=0;
    unsigned width_=0,height_=0;bool inverted_=false;
public:
    void Reset() {session_=generation_=frame_=0;}
    Decision Accept(std::uint64_t session,std::uint64_t generation,std::uint64_t frame,
                    unsigned width,unsigned height,bool inverted,bool reset,double deltaMs) {
        if(!session||!generation||!frame||frame==UINT64_MAX||!width||!height||width>4096||height>4096||
           std::uint64_t(width)*height>8388608||!std::isfinite(deltaMs)||deltaMs<=0||deltaMs>250)return {};
        const bool same=session==session_&&generation==generation_;
        if(same&&frame<=frame_)return {};
        reset=reset||!same||!frame_||frame-frame_!=1||width!=width_||height!=height_||inverted!=inverted_;
        session_=session;generation_=generation;frame_=frame;width_=width;height_=height;inverted_=inverted;
        return {true,reset,!reset};
    }
};
}
