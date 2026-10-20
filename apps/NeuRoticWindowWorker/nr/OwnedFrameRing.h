#pragma once
#include <array>
#include <memory>
#include <cstddef>
namespace nrw {
// The producer is single-threaded. Consumers retain the returned lease through
// their last CPU/GPU use. A completed copy alone never releases a live lease.
template<class Slot,std::size_t Capacity=3> class OwnedFrameRing {
    std::array<std::shared_ptr<Slot>,Capacity> slots_{};
  public:
    std::shared_ptr<Slot> TryAcquire() {
        for(auto& slot:slots_) {
            if(!slot) slot=std::make_shared<Slot>();
            if(slot.use_count()==1 && slot->retired) {
                slot->retired=false;
                return slot;
            }
        }
        return {}; // Drop latest admission; never grow or evict unknown work.
    }
};
}
