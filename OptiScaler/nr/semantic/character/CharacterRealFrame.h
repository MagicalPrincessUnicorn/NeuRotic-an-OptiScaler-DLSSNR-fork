#pragma once
#include "CharacterBoxHold.h"
#include <atomic>
#include <memory>

namespace Neurotic::Semantic::Character {
// The application Present boundary is known; asynchronous generated-output
// membership is not. Publish once there and draw unchanged until its successor.
class RealFrameClock {
    Epoch epoch_{};
    std::uintptr_t owner_=0;
    std::uint64_t token_=0,time_=0;
public:
    std::uint64_t intervalNs=0;
    bool Advance(const Epoch& epoch,std::uintptr_t owner,std::uint64_t token,std::uint64_t now) noexcept {
        if(!owner||!now)return false;
        if(owner_!=owner||epoch_!=epoch){owner_=owner;epoch_=epoch;token_=time_=intervalNs=0;}
        if(token>0x1'0000'0000ull)return false;
        if(time_&&now<=time_)return false;
        if(token&&token_){
            const auto delta=static_cast<std::uint32_t>(token-1)-static_cast<std::uint32_t>(token_-1);
            if(!delta||delta>=0x8000'0000u)return false;
        }
        intervalNs=time_?std::min<std::uint64_t>(now-time_,125'000'000):0;
        if(token)token_=token;
        time_=now;return true;
    }
};
struct FrozenCharacterDisplay { HeldSnapshot held;DisplayContext display;bool ready=false; };
class RealFrameDisplay {
    std::atomic<std::shared_ptr<const FrozenCharacterDisplay>> published_{};
public:
    void Clear() noexcept {published_.store({});}
    void Publish(const BoxHold& boxes,DisplayContext display,unsigned holdMs,unsigned maximum,bool smart,bool awaiting) {
        auto value=std::make_shared<FrozenCharacterDisplay>();value->display=display;
        value->ready=boxes.Read(display,holdMs,value->held,maximum,smart,awaiting);
        published_.store(std::move(value));
    }
    std::shared_ptr<const FrozenCharacterDisplay> Read(std::uint64_t generation,std::uint64_t scene,std::uint64_t now) const noexcept {
        auto value=published_.load();
        if(!value||!value->ready||value->display.epoch.config!=generation||value->display.epoch.scene!=scene||
            now<value->display.nowNs||now-value->display.nowNs>500'000'000)return {};
        return value;
    }
};
}
