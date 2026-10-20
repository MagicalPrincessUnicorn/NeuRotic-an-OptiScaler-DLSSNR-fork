#pragma once
#include "StreamlineModuleEpoch.h"
#include "StreamlineOnLoadInvocation.h"
#include "../runtime/RuntimeLifetime.h"
#include <array>
#include <utility>

namespace Neurotic::Hooks
{
// A finite compiled entry table avoids allocating executable code or guessing
// callback ABIs. Issued entries are NEVER reused, including after provider unload.
// Exhaustion leaves the exact upstream pointer available with an explicit status;
// it does not silently bind an old entry to a new generation.
// Domain keeps independent callback families from consuming one another's
// compiled entries. The default domain retains the existing Common budget.
template<class Parameters,std::size_t Capacity=256,class Domain=void>
class StreamlineOnLoadBindings
{
  public:
    using Load=bool(*)(Parameters*,const char*,const char**);
    using Invocation=StreamlineOnLoadInvocation<Parameters>;
    using Transform=bool(*)(const Invocation&,const char*,const char**);
    enum class Status {Wrapped,OriginalNull,ProcessClosing,StorageUnavailable,OwnerUnavailable,CapacityExhausted};
    struct Result {Load function=nullptr;Status status=Status::OriginalNull;};
  private:
    struct Record
    {
        StreamlineModuleEpochs::Epoch* epoch=nullptr;
        Load original=nullptr;
        Transform transform=nullptr;
        std::atomic<bool> published{false};
    };
    StreamlineModuleEpochs epochs;
    SRWLOCK lock=SRWLOCK_INIT;
    std::array<Record,Capacity> records{};
    std::size_t used=0;
    static inline std::atomic<StreamlineOnLoadBindings*> instance{nullptr};
    StreamlineOnLoadBindings()=default;
    ~StreamlineOnLoadBindings()=default;
    static_assert(Capacity>0);
    static_assert(std::atomic<StreamlineOnLoadBindings*>::is_always_lock_free);
    static StreamlineOnLoadBindings* Get() noexcept
    {
        auto* existing=instance.load(std::memory_order_acquire);
        if(existing)return existing;
        auto* memory=HeapAlloc(GetProcessHeap(),0,sizeof(StreamlineOnLoadBindings));
        if(!memory)return nullptr;
        auto* candidate=new(memory)StreamlineOnLoadBindings;
        if(instance.compare_exchange_strong(existing,candidate,std::memory_order_release,std::memory_order_acquire))
            return candidate;
        // No observer/record was published from the losing candidate.
        candidate->~StreamlineOnLoadBindings();HeapFree(GetProcessHeap(),0,candidate);return existing;
    }
    template<std::size_t Index>
    static bool Invoke(Parameters* params,const char* loaderJSON,const char** pluginJSON)
    {
        if(Neurotic::Runtime::IsProcessExiting())return false;
        const auto* self=instance.load(std::memory_order_acquire);
        if(!self)return false;
        const auto& record=self->records[Index];
        if(!record.published.load(std::memory_order_acquire))return false;
        auto lease=StreamlineModuleEpochs::Lease::Acquire(record.epoch,reinterpret_cast<const void*>(record.original));
        if(!lease)return false;
        // No registry/publication lock spans loader APIs, transformation, JSON
        // allocation or the provider. Original exceptions unwind the real lease.
        const Invocation invocation{record.original,params,&record,record.epoch};
        return record.transform(invocation,loaderJSON,pluginJSON);
    }
    template<std::size_t... Index>
    static constexpr auto Entries(std::index_sequence<Index...>) noexcept
    {return std::array<Load,Capacity>{&Invoke<Index>...};}
    static inline constexpr auto entries=Entries(std::make_index_sequence<Capacity>{});
  public:
    // Called by existing hook preparation before its getter entry is exposed.
    // A concurrent starter is never waited on from a loader-sensitive path.
    static bool Prepare() noexcept
    {
        if(Neurotic::Runtime::IsProcessExiting())return false;
        auto* self=Get();return self&&self->epochs.Prepare();
    }
    static Result Bind(Load original,Transform transform) noexcept
    {
        if(!original)return {};
        if(Neurotic::Runtime::IsProcessExiting())return {original,Status::ProcessClosing};
        auto* self=Get();
        if(!self)return {original,Status::StorageUnavailable};
        auto* epoch=self->epochs.Observe(reinterpret_cast<const void*>(original));
        if(!epoch||!transform)return {original,Status::OwnerUnavailable};
        AcquireSRWLockExclusive(&self->lock);
        for(std::size_t index=0;index<self->used;++index)
        {
            const auto& record=self->records[index];
            if(record.epoch==epoch&&record.original==original&&record.transform==transform)
            {ReleaseSRWLockExclusive(&self->lock);return {entries[index],Status::Wrapped};}
        }
        if(self->used==Capacity)
        {ReleaseSRWLockExclusive(&self->lock);return {original,Status::CapacityExhausted};}
        const auto index=self->used++;
        auto& record=self->records[index];record.epoch=epoch;record.original=original;record.transform=transform;
        record.published.store(true,std::memory_order_release);
        ReleaseSRWLockExclusive(&self->lock);return {entries[index],Status::Wrapped};
    }
};
}
