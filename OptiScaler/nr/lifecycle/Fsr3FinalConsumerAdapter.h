#pragma once
#include "NativeFinalConsumerAction.h"
#include "NativeSourceTransactionObservation.h"
#include <dlssnr/NrGpuSafety.h>
#include <array>
#include <mutex>

namespace Neurotic::Lifecycle
{
// Implemented by the retained source/Resource owner. A callback token locates
// this association; only the canonical source owner can create its frame facts.
class Fsr3FinalConsumerSource:public NativeFinalConsumerTail
{
  public:
    virtual bool Valid()const noexcept=0;
    virtual bool Offer(ID3D12Resource*,ID3D12GraphicsCommandList*,const DlssNr::GpuSafety::Ticket&)=0;
    virtual bool DispatchCurrent()const noexcept=0;
    virtual void DispatchReturned(bool accepted)=0;
    virtual void Interrupt()=0;
    virtual bool Retired()const noexcept=0;
};
struct Fsr3RecordingOperations
{
    static DlssNr::GpuSafety::Ticket Record(ID3D12GraphicsCommandList* list)
    {return DlssNr::GpuSafety::Record(list);}
};
// Local bounded index in the EXISTING FSR feature. No frame or provider IDs are
// issued here; no entry is reclaimed from a callback ordinal, timeout or fence.
template<class RecordingOperations=Fsr3RecordingOperations>class BasicFsr3FinalConsumerAdapter
{
  public:
    static constexpr std::size_t Capacity=16;
    enum class Callback { Unenrolled,Offered,Refused };
    class Invocation
    {
        friend class BasicFsr3FinalConsumerAdapter;
        std::shared_ptr<Fsr3FinalConsumerSource> source_;
        std::uint64_t epoch_=0,frame_=0;
        Invocation(Callback value,std::shared_ptr<Fsr3FinalConsumerSource> source={},std::uint64_t epoch=0,std::uint64_t frame=0)
            :source_(std::move(source)),epoch_(epoch),frame_(frame),status(value){}
      public:
        const Callback status;
        Invocation(const Invocation&)=delete;
        Invocation& operator=(const Invocation&)=delete;
        Invocation(Invocation&&)=default;
        ~Invocation()
        {
            // An exceptional/abandoned call still closes its CPU borrow. The
            // separate provider/GPU tail remains rooted until real retirement.
            if(status==Callback::Offered&&source_)
                try{source_->DispatchReturned(false);}catch(...){}
        }
    };
  private:
    struct Entry
    {
        std::shared_ptr<Fsr3FinalConsumerSource> source;
        std::uint64_t epoch=0,frame=0;
        bool started=false,offered=false,returned=false;
    };
    struct SourceAssociation
    {
        std::shared_ptr<NativeSourceTransactionObservation> source;
        std::uint64_t epoch=0,frame=0;
        bool ambiguous=false,observed=false;
    };
    std::array<SourceAssociation,Capacity> sourceAssociations_{};
    std::uint64_t observedSourceThrough_=0;
    bool sourceContextSeen_=false,sourceContextRevoked_=false;
    mutable std::mutex mutex_;
    std::array<Entry,Capacity> entries_{};
    void* context_=nullptr;
    std::uint64_t epoch_=0,excludedThrough_=0;
    Entry* Find(std::uint64_t frame)
    {for(auto& e:entries_)if(e.source&&e.epoch==epoch_&&e.frame==frame)return &e;return nullptr;}
  public:
    void ObserveContext(void* context)
    {
        std::array<std::shared_ptr<Fsr3FinalConsumerSource>,Capacity> interrupted;
        {
            std::lock_guard lock(mutex_);
            if(context_==context)return;
            context_=context;excludedThrough_=0;
            // These entries carry CPU observations only, never leases/claims or
            // GPU resources. Existing effectful strict entries drain separately.
            // The callback carries `this` and a frame key, not an authenticated
            // old-context generation. Until real callback quiescence is supplied,
            // a recreated context cannot safely enroll this optional association.
            sourceContextRevoked_=sourceContextRevoked_||sourceContextSeen_;
            sourceContextSeen_=sourceContextSeen_||context!=nullptr;
            sourceAssociations_={};observedSourceThrough_=0;
            if(epoch_==(std::numeric_limits<std::uint64_t>::max)())context_=nullptr;
            else ++epoch_;
            for(std::size_t i=0;i<Capacity;++i)if(entries_[i].source&&!entries_[i].started)
            {entries_[i].started=true;interrupted[i]=entries_[i].source;}
        }
        for(const auto& source:interrupted)if(source)source->Interrupt();
    }
    // Called at the actual upscaler's StartNewFrame scheduling operation,
    // before SR Evaluate. It observes an explicit reference, never token matches.
    bool ObserveSource(void* context,std::uint64_t frame,std::shared_ptr<NativeSourceTransactionObservation> source)
    {
        std::lock_guard lock(mutex_);
        if(!context||context!=context_||!frame||!source||sourceContextRevoked_)return false;
        for(auto& e:sourceAssociations_)if(e.source&&e.epoch==epoch_&&e.frame==frame)
        {e.ambiguous=true;return false;} // never guess which live same-key record won
        if(frame<=observedSourceThrough_)return false;
        for(auto& e:sourceAssociations_)if(!e.source||e.observed)
        {e={std::move(source),epoch_,frame};return true;}
        return false; // bounded metadata; no effectful ownership is evicted
    }
    SourceAssociationObservation ObserveSourceCallback(std::uint64_t frame)
    {
        std::lock_guard lock(mutex_);
        for(auto& e:sourceAssociations_)if(e.source&&e.epoch==epoch_&&e.frame==frame)
        {
            if(e.ambiguous)return {SourceAssociationReason::Ambiguous,e.source};
            if(e.observed)return {SourceAssociationReason::AlreadyObserved,e.source};
            e.observed=true;observedSourceThrough_=(std::max)(observedSourceThrough_,frame);
            return {e.source->Status(),e.source};
        }
        return {SourceAssociationReason::Missing,{}};
    }
    bool Enroll(void* context,std::uint64_t frame,std::shared_ptr<Fsr3FinalConsumerSource> source)
    {
        std::lock_guard lock(mutex_);
        if(!context||context!=context_||!frame||frame<=excludedThrough_||!source||!source->Valid()||Find(frame))return false;
        for(auto& e:entries_)if(!e.source){e={std::move(source),epoch_,frame};return true;}
        return false;
    }
    Invocation BeginCall(std::uint64_t frame,ID3D12Resource* resource,ID3D12GraphicsCommandList* list)
    {
        std::shared_ptr<Fsr3FinalConsumerSource> source;std::uint64_t epoch=0;
        {
            std::lock_guard lock(mutex_);auto* e=Find(frame);
            if(!e)return {excludedThrough_!=0&&frame<=excludedThrough_?Callback::Refused:Callback::Unenrolled};
            if(e->started||!context_||!resource||!list)return {Callback::Refused};
            e->started=true;source=e->source;epoch=e->epoch;
        }
        const auto ticket=RecordingOperations::Record(list);
        if(!ticket||!source->Offer(resource,list,ticket)){source->Interrupt();return {Callback::Refused};}
        {
            std::lock_guard lock(mutex_);
            for(auto& e:entries_)if(e.source==source&&e.epoch==epoch&&e.frame==frame)e.offered=true;
        }
        return {Callback::Offered,std::move(source),epoch,frame};
    }
    Callback Begin(std::uint64_t frame,ID3D12Resource* resource,ID3D12GraphicsCommandList* list)
    {return BeginCall(frame,resource,list).status;}
    void End(const Invocation& invocation,bool accepted)
    {
        std::shared_ptr<Fsr3FinalConsumerSource> source;
        {
            std::lock_guard lock(mutex_);
            for(auto& e:entries_)if(e.source&&e.source==invocation.source_&&e.epoch==invocation.epoch_&&e.frame==invocation.frame_)
            {
                if(!e.offered||e.returned)return;
                e.returned=true;source=e.source;break;
            }
        }
        if(source)source->DispatchReturned(accepted);
    }
    bool DispatchCurrent(const Invocation& invocation)const noexcept
    {
        {
            std::lock_guard lock(mutex_);
            if(invocation.status!=Callback::Offered||!context_||epoch_!=invocation.epoch_||!invocation.source_)return false;
        }
        return invocation.source_->DispatchCurrent();
    }
    void Skip(std::uint64_t frame)
    {
        std::shared_ptr<Fsr3FinalConsumerSource> source;
        {std::lock_guard lock(mutex_);auto* e=Find(frame);if(e&&!e->started){e->started=true;source=e->source;}}
        if(source)source->Interrupt();
    }
    void Advance()
    {
        std::array<std::shared_ptr<Fsr3FinalConsumerSource>,Capacity> sources,retired;
        {std::lock_guard lock(mutex_);for(std::size_t i=0;i<Capacity;++i)sources[i]=entries_[i].source;}
        for(std::size_t i=0;i<Capacity;++i)if(sources[i])
        {
            sources[i]->Advance();
            if(sources[i]->Retired())
            {
                std::lock_guard lock(mutex_);auto& e=entries_[i];if(e.source!=sources[i])continue;
                if(e.epoch==epoch_)excludedThrough_=(std::max)(excludedThrough_,e.frame);
                retired[i]=std::move(e.source);
                e={};
            }
        }
    }
};
using Fsr3FinalConsumerAdapter=BasicFsr3FinalConsumerAdapter<>;
}
