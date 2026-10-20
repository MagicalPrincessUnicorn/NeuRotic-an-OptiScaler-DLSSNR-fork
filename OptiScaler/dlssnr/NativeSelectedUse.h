#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace DlssNr {
// Selected use and entered shared renderer use are distinct events. Neither
// counter is telemetry or proof of GPU completion. All methods are CPU-only;
// no lock remains held across a caller's foreign work.
class NativeSelectedUse {
public:
    class Witness {
        friend class NativeSelectedUse;
        const NativeSelectedUse* owner_ = nullptr;
        std::uint64_t selected_ = 0, history_ = 0;
        Witness(const NativeSelectedUse* owner,std::uint64_t selected,std::uint64_t history)
            :owner_(owner),selected_(selected),history_(history){}
    public:
        Witness(const Witness&)=default;
        Witness& operator=(const Witness&)=default;
    };
private:
    friend struct NativeSelectedUseTestAccess;
    mutable std::mutex mutex_;
    std::uint64_t selected_=0,history_=0;
    bool exhausted_=false;
    bool CurrentLocked(const Witness& witness) const noexcept {
        return !exhausted_&&witness.owner_==this&&witness.selected_==selected_&&
            witness.history_==history_&&selected_!=0;
    }
    bool Advance(std::uint64_t& value) noexcept {
        if(exhausted_||value==std::numeric_limits<std::uint64_t>::max()){
            exhausted_=true;return false;
        }
        ++value;return true;
    }
public:
    std::optional<Witness> BeginSelected() {
        std::lock_guard lock(mutex_);
        if(!Advance(selected_))return {};
        return Witness(this,selected_,history_);
    }
    bool Current(const Witness& witness) const {
        std::lock_guard lock(mutex_);return CurrentLocked(witness);
    }
    // Ordinary entered Dispatch always proceeds. Exhaustion only disables
    // pending-request matching; it must not disable established rendering.
    void ObserveHistory() {
        std::lock_guard lock(mutex_);Advance(history_);
    }
    // A deferred Dispatch consumes exactly its current witness, then advances
    // that local witness over its own entered use. Never refresh a stale one.
    bool EnterHistory(Witness& witness) {
        std::lock_guard lock(mutex_);
        if(!CurrentLocked(witness)||!Advance(history_))return false;
        witness.history_=history_;return true;
    }
};
}
