#pragma once
#include <vector>
#include <utility>
// Reentry is local to the calling thread, exact chain and operation family.
// A different chain on this thread is an independent operation.
class ScopedFgReentry {
    inline static thread_local std::vector<std::pair<const void*, unsigned>> active_;
    bool recursive_ = false;
  public:
    ScopedFgReentry(const void* owner,unsigned operation) {
        const auto key=std::pair{owner,operation};
        for(const auto& current:active_) if(current==key) recursive_=true;
        active_.push_back(key);
    }
    ~ScopedFgReentry(){active_.pop_back();}
    bool Recursive() const noexcept{return recursive_;}
    ScopedFgReentry(const ScopedFgReentry&)=delete;
    ScopedFgReentry& operator=(const ScopedFgReentry&)=delete;
};
