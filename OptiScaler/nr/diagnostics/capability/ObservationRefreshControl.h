#pragma once
#include "CapabilityRefresh.h"
#include <mutex>
#include <atomic>

namespace DlssNr::Capability {
struct ObservationRefreshTicket {uint64_t identity=0,context=0,started=0;bool manual=false;};
class ObservationRefreshControl {
    mutable std::mutex mutex_;
    RefreshState context_{};
    bool seen_=false,vulkan_=false,busy_=false,dirty_=true,released_=true,attempted_=false;
    uint64_t generation_=0,identity_=0,lastAttempt_=0,lastFinished_=0,visibility_=0;
    bool finished_=false;
    std::atomic<bool> closed_{false};
  public:
    void Observe(RefreshState value,bool vulkan,bool trackVisibility=true) noexcept {
        std::lock_guard lock(mutex_);
        if(trackVisibility && visibility_!=value.menuVisibilityGeneration) {visibility_=value.menuVisibilityGeneration;dirty_=true;}
        value.menuVisibilityGeneration=0;
        if(!seen_ || value!=context_ || vulkan!=vulkan_) {
            if(generation_==UINT64_MAX) {closed_.store(true);return;}
            ++generation_;context_=value;vulkan_=vulkan;seen_=true;dirty_=true;
        }
    }
    void Activation(bool held) noexcept {
        std::lock_guard lock(mutex_);if(!held)released_=true;else if(busy_)released_=false;
    }
    std::optional<ObservationRefreshTicket> Request(bool manual,uint64_t now) noexcept {
        std::lock_guard lock(mutex_);
        if(manual) {if(!released_)return {};released_=false;}
        if(closed_.load() || !seen_ || busy_ || identity_==UINT64_MAX)return {};
        if(manual && finished_ && (now<lastFinished_ || now-lastFinished_<500))return {};
        if(!manual && (!dirty_ || (attempted_ && now>=lastAttempt_ && now-lastAttempt_<500)))return {};
        busy_=true;dirty_=false;attempted_=true;lastAttempt_=now;
        return ObservationRefreshTicket{++identity_,generation_,now,manual};
    }
    static bool CollectionDone(const ObservationRefreshTicket& token,uint64_t now) noexcept {
        return !token.manual || (now>=token.started && now-token.started>=2000);
    }
    bool Cancelled(const ObservationRefreshTicket& token) const noexcept {
        if(closed_.load())return true;
        std::lock_guard lock(mutex_);return !busy_ || token.identity!=identity_ || token.context!=generation_;
    }
    template<class Publish>
    bool Finish(const ObservationRefreshTicket& token,uint64_t now,Publish&& publish,bool available=true) noexcept {
        std::lock_guard lock(mutex_);
        if(!busy_ || token.identity!=identity_)return false;
        bool accepted=available && !closed_.load() && token.context==generation_;
        if(accepted)try {publish();}catch(...) {accepted=false;}
        if(!accepted)dirty_=true;
        busy_=false;lastFinished_=now;finished_=true;return accepted;
    }
    bool Busy() const noexcept {std::lock_guard lock(mutex_);return busy_;}
    uint64_t Context() const noexcept {std::lock_guard lock(mutex_);return closed_.load()?0:generation_;}
    void Close() noexcept {closed_.store(true,std::memory_order_release);}
};
}
