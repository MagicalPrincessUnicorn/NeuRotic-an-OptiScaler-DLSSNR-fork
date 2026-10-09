#pragma once
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <cstdint>

namespace Neurotic::Lifecycle
{
// CPU callback registration beneath the existing FSR owner. No Resource/GPU
// right or SDK quiescence is inferred. Retained inert addresses cannot be reused
// while a provider could still have a queued callback containing that address.
template<class Owner,std::size_t Capacity=128> class Fsr3CallbackBindings
{
  public:
    class Binding
    {
        friend class Fsr3CallbackBindings;
        std::recursive_mutex mutex_;
        Owner* owner_;
        void* const context_;
        std::optional<std::uint64_t> lastFrame_;
        unsigned active_=0;
        Binding(Owner& owner,void* context):owner_(&owner),context_(context){}
        bool Close(){std::lock_guard lock(mutex_);owner_=nullptr;return active_==0;}
        // False can mean contention and does not promise entry was disarmed.
        // The serialized lifecycle owner retains this binding and retries.
        bool TryClose()
        {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock())return false;
            owner_=nullptr;return active_==0;
        }
        bool Open() {std::lock_guard lock(mutex_);return owner_!=nullptr;}
      public:
        Binding(const Binding&)=delete;
        void* Context()const noexcept{return context_;}
        template<class Call> auto Invoke(Call&& call)->std::optional<std::invoke_result_t<Call,Owner&>>
        {
            std::lock_guard lock(mutex_);
            if(!owner_)return {};
            struct Entered {unsigned& active;Entered(unsigned& n):active(n){++active;}~Entered(){--active;}} entered(active_);
            return std::forward<Call>(call)(*owner_);
        }
        bool Duplicate(std::uint64_t frame){std::lock_guard lock(mutex_);return lastFrame_&&*lastFrame_==frame;}
        void Mark(std::uint64_t frame){std::lock_guard lock(mutex_);lastFrame_=frame;}
    };
  private:
    struct Retained
    {
        std::mutex mutex;
        std::array<std::unique_ptr<Binding>,Capacity> bindings;
    };
    // Bounded process-lifetime CPU tombstones only. No feature, COM resource,
    // provider handle or callable owner survives Close. Full storage refuses.
    static Retained& Storage(){static auto* value=new Retained;return *value;}
    Binding* current_=nullptr;
  public:
    Fsr3CallbackBindings()=default;
    Fsr3CallbackBindings(const Fsr3CallbackBindings&)=delete;
    ~Fsr3CallbackBindings(){Close();}
    // Called by the FSR owner's serialized create/configure/destroy operations.
    Binding* Open(Owner& owner,void* context)
    {
        if(!context)return nullptr;
        if(current_)return current_->Context()==context&&current_->Open()?current_:nullptr;
        auto& storage=Storage();std::lock_guard lock(storage.mutex);
        for(auto& entry:storage.bindings)if(!entry)
        {entry=std::unique_ptr<Binding>(new Binding(owner,context));current_=entry.get();return current_;}
        return nullptr;
    }
    Binding* Current()const noexcept{return current_;}
    // CPU entry only: this never establishes SDK/GPU/provider drainage.
    bool TryClose()
    {
        if(!current_)return true;
        if(!current_->TryClose())return false;
        current_=nullptr;return true;
    }
    bool Close()
    {
        if(!current_)return true;
        // Wait only for entered CPU calls; drop this lock BEFORE SDK teardown.
        // This does not establish GPU completion or provider release.
        // Reentrant closure disarms future entries but is NOT drainage. The
        // serialized owner must defer teardown and retry after Invoke returns.
        if(!current_->Close())return false;
        current_=nullptr;return true;
    }
};
}
