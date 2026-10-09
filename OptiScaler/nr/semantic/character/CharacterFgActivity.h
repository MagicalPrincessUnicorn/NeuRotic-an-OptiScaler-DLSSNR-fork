#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
namespace Neurotic::Semantic::Character {
inline std::uint64_t CharacterActivityNow() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct NativeFgWorkView {bool available=false,requested=false,active=false,observed=false;unsigned inFlight=0;};
class NativeFgWorkTracker {
    struct Entry {std::uint64_t handle=0,instance=0,lastWork=0;};
    std::mutex mutex_;
    std::array<Entry,16> entries_{};
    std::uint64_t generation_=1,unknownWork_=0;
    unsigned inFlight_=0;
    bool requested_=false,observed_=false;
public:
    struct Token {std::uint64_t generation=0,handle=0,instance=0;std::size_t index=16;bool pending=false;};
    void Requested(bool value){std::lock_guard lock(mutex_);requested_=value;if(!value){++generation_;for(auto& e:entries_)e.lastWork=0;unknownWork_=0;}}
    void Reset(){std::lock_guard lock(mutex_);++generation_;entries_={};unknownWork_=0;requested_=false;observed_=false;}
    Token Begin(std::uint64_t handle,std::uint64_t instance){
        std::lock_guard lock(mutex_);std::size_t index=16;
        for(std::size_t i=0;i<16;++i)if(entries_[i].handle==handle&&entries_[i].instance==instance){index=i;break;}
        if(index==16&&instance)for(std::size_t i=0;i<16;++i)if(!entries_[i].handle){entries_[i]={handle,instance,0};index=i;break;}
        ++inFlight_;observed_=true;return {generation_,handle,instance,index,true};
    }
    void Finish(Token& token,bool success,std::uint64_t currentInstance,std::uint64_t now){
        std::lock_guard lock(mutex_);if(!token.pending)return;token.pending=false;if(inFlight_)--inFlight_;
        if(!success||token.generation!=generation_||token.instance!=currentInstance)return;
        if(token.index<16){auto& entry=entries_[token.index];if(entry.handle==token.handle&&entry.instance==token.instance)entry.lastWork=now;}
        else unknownWork_=now;
    }
    void Released(std::uint64_t handle,std::uint64_t instance){
        std::lock_guard lock(mutex_);for(auto& entry:entries_)if(entry.handle==handle&&entry.instance==instance)entry={};
    }
    NativeFgWorkView Read(std::uint64_t now){
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return {false,false,true,false,0};
        const auto recent=[&](std::uint64_t time){return time&&(time>now||now-time<=500'000'000);};
        bool active=inFlight_>0||recent(unknownWork_);
        for(const auto& e:entries_)active=active||recent(e.lastWork);
        return {true,requested_,active,observed_,inFlight_};
    }
};
inline NativeFgWorkTracker& NativeFgWork(){static NativeFgWorkTracker value;return value;}
}
