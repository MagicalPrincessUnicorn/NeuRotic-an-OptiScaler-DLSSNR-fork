#pragma once
#include "NativeNgxCreationParameters.h"
#include "NativeFeatureLifetime.h"
#include "NativeSrRetirementReceipt.h"
#include "NativeTemporalSource.h"
#include "StreamlineSourceScope.h"
#include "NativeNgxCallCapture.h"
#include <memory>

#include <cstdint>
#include <mutex>
#include <optional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace DlssNr
{
// Return values, never iterators, across NGX calls. A failed release keeps its
// mapping; a delayed release cannot erase a replacement with the same handle.
template<class Feature> class NativeFeatureRegistry
{
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeFeatureRegistryTestAccess;
#endif
  public:
    struct Snapshot
    {
        Feature feature {};
        uint64_t generation = 0;
        NativeNgxCreationParameters originalCreation;
        explicit operator bool() const { return generation != 0; }
    };
  private:
    struct Entry
    {
        Snapshot snapshot;
        std::shared_ptr<NativeFeatureLifetime> lifetime;
        uint64_t callbacks = 0;
        bool protectedSource = false, releasing = false;
        std::shared_ptr<const StreamlineSourceScope::Observation> temporalFrame;
        std::optional<NativeTemporalSource> temporalSource;
        std::shared_ptr<const NativeNgxCallCapture> temporalMetadata;
        bool temporalSrSucceeded=false;
    };
    mutable std::mutex mutex;
    std::unordered_map<unsigned int, Entry> entries;
    std::vector<std::shared_ptr<NativeFeatureLifetime>> epoch_;
    uint64_t generation = 0;
    uint64_t sourceOrdinal_ = 0;
    uint64_t temporalSerial_ = 0;
    bool stopped_ = false;
    bool shutdownActive_ = false;
  public:
    // A noncopyable borrow from this exact registry. It prevents physical release
    // through BeginRelease while alive, but is neither route nor resource authority.
    // No registry lock is held across an opaque provider/model call.
    class CallbackPin
    {
        friend class NativeFeatureRegistry;
        NativeFeatureRegistry* owner;
        unsigned int handle;
        Snapshot snapshot;
        uint64_t ordinal;
        std::shared_ptr<NativeFeatureLifetime> lifetime;
        bool entered=false;
        CallbackPin(NativeFeatureRegistry& source,unsigned int id,Snapshot value,uint64_t order,
            std::shared_ptr<NativeFeatureLifetime> retained):owner(&source),handle(id),snapshot(value),ordinal(order),lifetime(std::move(retained)){}
        void Drop()noexcept
        {
            if(!owner)return;
            std::lock_guard lock(owner->mutex);
            const auto it=owner->entries.find(handle);
            if(it!=owner->entries.end()&&it->second.callbacks)--it->second.callbacks;
            if(lifetime)--lifetime->callbacks_;
            owner=nullptr;
        }
      public:
        CallbackPin(const CallbackPin&)=delete;
        CallbackPin& operator=(const CallbackPin&)=delete;
        CallbackPin(CallbackPin&& other)noexcept:owner(std::exchange(other.owner,nullptr)),handle(other.handle),snapshot(other.snapshot),ordinal(other.ordinal),lifetime(std::move(other.lifetime)),entered(other.entered){}
        CallbackPin& operator=(CallbackPin&& other)noexcept
        {if(this!=&other){Drop();owner=std::exchange(other.owner,nullptr);handle=other.handle;snapshot=other.snapshot;ordinal=other.ordinal;lifetime=std::move(other.lifetime);entered=other.entered;}return *this;}
        ~CallbackPin(){Drop();}
        Snapshot Value()const noexcept{return snapshot;}
        uint64_t Ordinal()const noexcept{return ordinal;}
        unsigned int Handle()const noexcept{return handle;}
        std::shared_ptr<const NativeFeatureLifetime> Lifetime()const noexcept{return lifetime;}
        // Called at actual opaque provider entry, including API-failure paths.
        bool MarkOpaqueEntry()
        {
            if(!owner||!lifetime||entered)return false;
            std::lock_guard lock(owner->mutex);const auto it=owner->entries.find(handle);
            if(owner->stopped_||lifetime->closed_||it==owner->entries.end()||it->second.lifetime!=lifetime||it->second.releasing||
               lifetime->entries_==(std::numeric_limits<uint64_t>::max)())
            {lifetime->unknown_=true;return false;}
            entered=true;++lifetime->entries_;return true;
        }
        bool BelongsTo(const NativeFeatureRegistry& registry)const noexcept{return owner==&registry;}
        bool Current()const
        {
            if(!owner)return false;
            std::lock_guard lock(owner->mutex);const auto it=owner->entries.find(handle);
            return !owner->stopped_&&it!=owner->entries.end()&&!it->second.releasing&&
                it->second.snapshot.generation==snapshot.generation&&it->second.snapshot.feature==snapshot.feature;
        }
        // CPU publication tail only. Never run COM, GPU or provider calls here.
        template<class Action>bool WithCurrent(Action&& action)const
        {
            if(!owner)return false;
            std::lock_guard lock(owner->mutex);const auto it=owner->entries.find(handle);
            if(owner->stopped_||it==owner->entries.end()||it->second.releasing||
               it->second.snapshot.generation!=snapshot.generation||it->second.snapshot.feature!=snapshot.feature)return false;
            return action();
        }
    };
    class ReleaseGuard
    {
        friend class NativeFeatureRegistry;
        NativeFeatureRegistry* owner;
        unsigned int handle;
        Snapshot snapshot;
        ReleaseGuard(NativeFeatureRegistry& source,unsigned int id,Snapshot value):owner(&source),handle(id),snapshot(value){}
      public:
        ReleaseGuard(const ReleaseGuard&)=delete;
        ReleaseGuard& operator=(const ReleaseGuard&)=delete;
        ReleaseGuard(ReleaseGuard&& other)noexcept:owner(std::exchange(other.owner,nullptr)),handle(other.handle),snapshot(other.snapshot){}
        ReleaseGuard& operator=(ReleaseGuard&& other)noexcept
        {if(this!=&other){owner=std::exchange(other.owner,nullptr);handle=other.handle;snapshot=other.snapshot;}return *this;}
        // The guarded generation is closed but not physically released. Only
        // known CPU/provider coverage can enter a retained pending owner.
        bool CanRetainPending()const
        {
            if(!owner)return false;
            std::lock_guard lock(owner->mutex);const auto it=owner->entries.find(handle);
            return it!=owner->entries.end()&&it->second.snapshot.generation==snapshot.generation&&
                it->second.releasing&&!it->second.callbacks&&it->second.lifetime->closed_&&
                !it->second.lifetime->released_&&!it->second.lifetime->unknown_;
        }
        // Destruction without a definitive result keeps admission closed. It
        // cannot assert that an interrupted physical release had no effect.
        // Only the guarded owner which has NOT entered ReleaseProvider may
        // classify this result. Existing unknown coverage is never cleared.
        bool DeferBeforeProvider()
        {
            if(!owner)return false;
            auto* source=std::exchange(owner,nullptr);std::lock_guard lock(source->mutex);
            const auto it=source->entries.find(handle);
            if(it==source->entries.end()||it->second.snapshot.generation!=snapshot.generation||
               !it->second.releasing||it->second.callbacks)return false;
            it->second.lifetime->closed_=false;it->second.releasing=false;
            return !it->second.lifetime->unknown_.load();
        }
        bool Complete(bool success)
        {
            if(!owner)return false;
            auto* source=std::exchange(owner,nullptr);std::lock_guard lock(source->mutex);
            const auto it=source->entries.find(handle);
            if(it==source->entries.end()||it->second.snapshot.generation!=snapshot.generation||
               !it->second.releasing||it->second.callbacks)return false;
            if(success)
            {
                it->second.lifetime->closed_=true;
                it->second.lifetime->released_=true;
                source->entries.erase(it);
            }
            else
            {it->second.lifetime->unknown_=true;it->second.lifetime->closed_=false;it->second.releasing=false;}
            return success;
        }
    };
    std::optional<CallbackPin> Pin(unsigned int handle,const Snapshot& expected)
    {
        if(!expected)return {};
        std::lock_guard lock(mutex);const auto it=entries.find(handle);
        if(sourceOrdinal_==(std::numeric_limits<uint64_t>::max)()){stopped_=true;return {};}
        if(stopped_||it==entries.end()||it->second.releasing||it->second.snapshot.generation!=expected.generation||
           it->second.snapshot.feature!=expected.feature||it->second.callbacks==(std::numeric_limits<uint64_t>::max)())return {};
        ++it->second.callbacks;++it->second.lifetime->callbacks_;it->second.protectedSource=true;
        return CallbackPin(*this,handle,it->second.snapshot,++sourceOrdinal_,it->second.lifetime);
    }
    bool Protected(unsigned int handle)const
    {std::lock_guard lock(mutex);const auto it=entries.find(handle);return it!=entries.end()&&it->second.protectedSource;}
    void MarkUntrackedEvaluation(unsigned int handle,const Snapshot& expected)
    {
        std::lock_guard lock(mutex);const auto it=entries.find(handle);
        if(it!=entries.end()&&it->second.snapshot.generation==expected.generation)
            it->second.lifetime->unknown_=true;
    }
    class ShutdownGuard
    {
        friend class NativeFeatureRegistry;
        NativeFeatureRegistry* owner;
        explicit ShutdownGuard(NativeFeatureRegistry& value):owner(&value){}
      public:
        ShutdownGuard(const ShutdownGuard&)=delete;
        ShutdownGuard& operator=(const ShutdownGuard&)=delete;
        ShutdownGuard(ShutdownGuard&& other)noexcept:owner(std::exchange(other.owner,nullptr)){}
        ShutdownGuard& operator=(ShutdownGuard&& other)noexcept
        {if(this!=&other)owner=std::exchange(other.owner,nullptr);return *this;}
        // An abandoned opaque shutdown keeps exclusion. A definitive return
        // releases only this action; it never reopens Native source admission.
        void Complete()
        {
            if(!owner)return;
            auto* source=std::exchange(owner,nullptr);std::lock_guard lock(source->mutex);
            source->shutdownActive_=false;
        }
    };
    std::optional<ShutdownGuard> BeginShutdown()
    {
        std::lock_guard lock(mutex);stopped_=true;
        if(shutdownActive_)return {};
        for(const auto& [handle,entry]:entries)
            if(entry.callbacks||entry.releasing)return {};
        shutdownActive_=true;return ShutdownGuard(*this);
    }
    // Close ingress before checking outstanding callbacks. This is only source
    // callback drainage; it does not prove GPU completion or provider release.
    bool StopCallbacks()
    {
        std::lock_guard lock(mutex);
        stopped_=true;
        for(const auto& [handle,entry]:entries)
            if(entry.callbacks||entry.releasing)return false;
        return true;
    }
    // A new source epoch can reopen only after every actual feature generation
    // from this registry epoch is covered by the private SR terminal receipt.
    bool ReopenAfterRetirement(const NativeSrRetirementReceipt& receipt)
    {
        std::lock_guard lock(mutex);
        if(!stopped_||shutdownActive_||!entries.empty()||epoch_.empty())return false;
        for(const auto& lifetime:epoch_)if(!receipt.Covers(lifetime))return false;
        epoch_.clear();stopped_=false;return true;
    }
    std::optional<ReleaseGuard> BeginRelease(unsigned int handle,const Snapshot& expected)
    {
        if(!expected)return {};
        std::lock_guard lock(mutex);const auto it=entries.find(handle);
        if(shutdownActive_||it==entries.end()||it->second.releasing||it->second.callbacks||
           it->second.snapshot.generation!=expected.generation||it->second.snapshot.feature!=expected.feature)return {};
        it->second.releasing=true;it->second.lifetime->closed_=true;
        return ReleaseGuard(*this,handle,it->second.snapshot);
    }
    void Set(unsigned int handle, Feature feature,const NativeNgxCreationParameters& originalCreation={})
    {
        std::lock_guard lock(mutex);
        if(generation==(std::numeric_limits<uint64_t>::max)())
        {
            stopped_=true;
            const auto it=entries.find(handle);
            if(it!=entries.end())it->second.snapshot={};
            return;
        }
        ++generation;
        auto nextLifetime=std::shared_ptr<NativeFeatureLifetime>(new NativeFeatureLifetime(generation));
        epoch_.push_back(nextLifetime);
        auto& entry=entries[handle];
        if(entry.lifetime){entry.lifetime->unknown_=true;entry.lifetime->closed_=true;}
        entry.lifetime=std::move(nextLifetime);
        // Preserve old callback pins across a replacement; they become stale,
        // and no physical release can race their remaining callback lifetime.
        entry.snapshot = {feature, generation, originalCreation};
        entry.temporalFrame.reset();entry.temporalSource.reset();entry.temporalMetadata.reset();entry.temporalSrSucceeded=false;
        // Replacement does not prove an in-progress/indeterminate physical
        // release finished. Keep that handle closed until a definitive owner
        // result; a stale result below cannot certify the new incarnation.
    }
    Snapshot Read(unsigned int handle) const
    {
        std::lock_guard lock(mutex);
        const auto it = entries.find(handle);
        return it != entries.end() ? it->second.snapshot : Snapshot{};
    }
    // Called once at the original SR boundary, before entering the provider.
    // Frame arithmetic belongs to the documented Streamline frame-index domain,
    // never to the opaque identity returned here. Gaps/wrap/missing observations
    // break continuity. A duplicate retains its identity and cannot advance NR.
    NativeTemporalSourceObservation ObserveTemporalSource(const CallbackPin& pin,
        std::shared_ptr<const StreamlineSourceScope::Observation> frame,
        std::shared_ptr<const NativeNgxCallCapture> metadata={})
    {
        std::lock_guard lock(mutex);NativeTemporalSourceObservation result;
        const auto it=entries.find(pin.handle);
        if(pin.owner!=this||stopped_||it==entries.end()||it->second.releasing||
           it->second.snapshot.generation!=pin.snapshot.generation)return result;
        auto& entry=it->second;
        if(!frame||frame->Status()!=StreamlineSourceScope::ReturnStatus::Pending) {
            entry.temporalFrame.reset();entry.temporalSource.reset();entry.temporalMetadata.reset();entry.temporalSrSucceeded=false;return result;
        }
        result.viewport=frame->Viewport();
        if(entry.temporalFrame&&entry.temporalFrame->Viewport()==frame->Viewport()&&
           entry.temporalFrame->Frame()==frame->Frame()) {
            result.source=entry.temporalSource;result.duplicate=true;
            result.contradictoryDuplicate=bool(metadata)!=bool(entry.temporalMetadata)||
                (metadata&&entry.temporalMetadata&&!metadata->SameTemporalMetadata(*entry.temporalMetadata));
            if(result.contradictoryDuplicate)entry.temporalSrSucceeded=false;
            if(entry.temporalFrame!=frame) {
                // A separate enclosing call has its own outcome. Never discard
                // an unresolved/failed earlier call or certify the new one from
                // the first call's successful return.
                if(entry.temporalFrame->Status()!=StreamlineSourceScope::ReturnStatus::Succeeded)
                    entry.temporalSrSucceeded=false;
                entry.temporalFrame=std::move(frame);
            }
            return result;
        }
        if(entry.temporalFrame&&entry.temporalSrSucceeded&&
           entry.temporalFrame->Status()==StreamlineSourceScope::ReturnStatus::Succeeded&&
           entry.temporalFrame->Viewport()==frame->Viewport()&&
           entry.temporalFrame->Frame()!=(std::numeric_limits<unsigned>::max)()&&
           frame->Frame()==entry.temporalFrame->Frame()+1)result.predecessor=entry.temporalSource;
        if(temporalSerial_==(std::numeric_limits<uint64_t>::max)())return result;
        result.source=NativeTemporalSource(reinterpret_cast<std::uintptr_t>(this),pin.snapshot.generation,++temporalSerial_);
        entry.temporalFrame=std::move(frame);entry.temporalSource=result.source;entry.temporalSrSucceeded=false;
        entry.temporalMetadata=std::move(metadata);
        return result;
    }
    template<class Parameters,class Result>
    std::shared_ptr<const NativeNgxCallCapture> CaptureTemporalInputs(const CallbackPin& pin,
        ID3D12GraphicsCommandList* list,Parameters* parameters,Result success)
    {
        if(!pin.BelongsTo(*this)||!pin.Current())return {};
        return NativeNgxCallCapture::Capture(list,parameters,success);
    }
    void CompleteTemporalSource(const CallbackPin& pin,const NativeTemporalSourceObservation& source,bool succeeded)
    {
        std::lock_guard lock(mutex);const auto it=entries.find(pin.handle);
        if(pin.owner!=this||it==entries.end()||it->second.snapshot.generation!=pin.snapshot.generation||
           it->second.temporalSource!=source.source)return;
        // Repeated evaluation is not a new source, but a failed repeat poisons continuity.
        if(!source.duplicate||!succeeded)it->second.temporalSrSucceeded=succeeded;
    }
    bool Has(Feature feature) const
    {
        std::lock_guard lock(mutex);
        for (const auto& [handle, entry] : entries)
            if (entry.snapshot && entry.snapshot.feature == feature) return true;
        return false;
    }
    bool Released(unsigned int handle, const Snapshot& expected, bool success)
    {
        if (!success || !expected) return false;
        std::lock_guard lock(mutex);
        const auto it = entries.find(handle);
        if (it == entries.end() || it->second.snapshot.generation != expected.generation ||
            it->second.callbacks || it->second.releasing) return false;
        // This legacy notification lacks pre-call exclusion. It cannot issue
        // the guarded physical-release terminal fact.
        it->second.lifetime->unknown_=true;it->second.lifetime->closed_=true;
        entries.erase(it);
        return true;
    }
};
}
