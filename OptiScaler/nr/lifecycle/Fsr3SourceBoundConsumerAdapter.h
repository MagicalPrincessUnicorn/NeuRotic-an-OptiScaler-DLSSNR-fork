#pragma once
#include "NativeSourceBoundConsumerAction.h"
#include "SelectedFsr3ResourceOwner.h"
#include <array>
#include <mutex>
namespace Neurotic::Lifecycle
{
class Fsr3SourceBoundConsumerSource:public NativeSourceBoundConsumerTail
{
  public:
    virtual bool Offer(ID3D12Resource*,ID3D12GraphicsCommandList*,const DlssNr::GpuSafety::Ticket&,
        const SelectedFsr3DispatchAdmission&)=0;
    virtual bool DispatchCurrent()const noexcept=0;
    virtual void DispatchReturned(SourceBoundDispatchEffect,std::optional<std::int32_t> actualResult)=0;
    virtual bool Retired()const noexcept=0;
};
// The key locates an already enrolled operation; it never originates identity.
// Explicit selection refuses missing enrollment rather than entering legacy FG.
class Fsr3SourceBoundConsumerAdapter
{
  public:
    enum class Callback { Unselected,Offered,Refused };
    class Invocation
    {
        friend class Fsr3SourceBoundConsumerAdapter;
        std::shared_ptr<Fsr3SourceBoundConsumerSource> source_;
        bool entered_=false,finished_=false;
        explicit Invocation(Callback status,std::shared_ptr<Fsr3SourceBoundConsumerSource> source={})
            :source_(std::move(source)),status(status){}
      public:
        const Callback status;
        Invocation(const Invocation&)=delete;
        Invocation& operator=(const Invocation&)=delete;
        Invocation(Invocation&& other)noexcept:source_(std::move(other.source_)),entered_(other.entered_),
            finished_(other.finished_),status(other.status){other.finished_=true;}
        ~Invocation(){if(source_&&!finished_)try{source_->DispatchReturned(entered_?
            SourceBoundDispatchEffect::InterruptedUnknown:SourceBoundDispatchEffect::NotEntered,{});}catch(...){}}
        bool Enter()
        {if(!source_||finished_||entered_||!source_->DispatchCurrent())return false;entered_=true;return true;}
        void Returned(std::int32_t actualResult,bool accepted)
        {
            if(!source_||finished_||!entered_)return;
            finished_=true;source_->DispatchReturned(accepted?SourceBoundDispatchEffect::ReturnedAccepted:
                SourceBoundDispatchEffect::ReturnedRejected,actualResult);
        }
    };
  private:
    struct Entry {void* context=nullptr;std::uint64_t frame=0;bool started=false;std::shared_ptr<Fsr3SourceBoundConsumerSource> source;};
    std::array<Entry,16> entries_{};
    std::mutex mutex_;
  public:
    bool Enroll(void* context,std::uint64_t frame,std::shared_ptr<Fsr3SourceBoundConsumerSource> source)
    {
        if(!context||!frame||!source)return false;std::lock_guard lock(mutex_);
        for(const auto& e:entries_)if(e.source&&e.context==context&&e.frame==frame)return false;
        for(auto& e:entries_)if(!e.source){e={context,frame,false,std::move(source)};return true;}
        return false;
    }
    Invocation Begin(bool selected,void* context,std::uint64_t frame,ID3D12Resource* resource,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::Ticket& ticket,
        const SelectedFsr3DispatchAdmission* sdk)
    {
        if(!selected)return Invocation(Callback::Unselected);
        std::shared_ptr<Fsr3SourceBoundConsumerSource> source;
        {
            std::lock_guard lock(mutex_);
            for(auto& e:entries_)if(e.source&&e.context==context&&e.frame==frame)
            {if(e.started)return Invocation(Callback::Refused);e.started=true;source=e.source;break;}
        }
        if(!source||!sdk||!resource||!list||!ticket)return Invocation(Callback::Refused);
        if(!source->Offer(resource,list,ticket,*sdk))return Invocation(Callback::Refused);
        return Invocation(Callback::Offered,std::move(source));
    }
    void Advance()
    {
        std::array<std::shared_ptr<Fsr3SourceBoundConsumerSource>,16> sources;
        {std::lock_guard lock(mutex_);for(std::size_t i=0;i<entries_.size();++i)sources[i]=entries_[i].source;}
        for(const auto& source:sources)if(source)source->Advance();
        // Keep bounded tombstones; callback/context pointer reuse cannot reclaim.
    }
};
}
