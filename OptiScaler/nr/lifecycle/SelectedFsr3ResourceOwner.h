#pragma once
#include "Fsr3FinalConsumerPublication.h"
#include "NativeLeaseBinding.h"
#include "NativeResourceRegistry.h"
#include "SourceBoundConsumerTypes.h"
#include <functional>
namespace Neurotic::Lifecycle
{
// A live SDK service borrow, implemented by the authenticated context owner.
// The wire ticket is descriptive and cannot implement this interface itself.
class SelectedFsr3DispatchAdmission
{
  public:
    virtual ~SelectedFsr3DispatchAdmission()=default;
    // Inspect an already acquired SDK borrow without taking SDK lifecycle or
    // pool locks: Resource holds its action lock during this recheck.
    virtual bool Current(ID3D12Resource*,ID3D12GraphicsCommandList*,ID3D12CommandQueue*,
        const DlssNr::GpuSafety::Ticket&)const noexcept=0;
};
// Concrete facade over one retained NativeLeaseRegistration. Construction is
// restricted to existing owners; a SelectedOutputRead cannot construct it.
class SelectedFsr3ResourceOwner final:public Fsr3FinalResourceOwner,
    public std::enable_shared_from_this<SelectedFsr3ResourceOwner>
{
    friend class NativeProcessBootstrap;
    friend class NativeResourceRegistry;
    friend class SelectedFsr3NativeLeaseOwner;
    std::shared_ptr<const void> lifetime_;
    NativeResourceRegistry& resources_;
    NativeLeaseRegistration action_;
    ID3D12Resource* const resource_;
    ID3D12GraphicsCommandList* const list_;
    ID3D12CommandQueue* const queue_;
    const DlssNr::GpuSafety::Ticket producer_,consumer_;
    const C::ResourceView endpoint_;
    const C::DependencyProof dependency_;
    const C::RecordKey consumerRecording_;
    const bool crossQueue_=false;
    C::RecordKey dispatchCall_;
    std::function<std::optional<SourceBoundSubmissionOutcome>()> observeSubmission_;
    bool begun_=false,closed_=false,active_=false;
    SelectedFsr3ResourceOwner(std::shared_ptr<const void> lifetime,NativeResourceRegistry& resources,
        std::shared_ptr<NativeLeaseOwner> owner,ID3D12Resource* resource,ID3D12GraphicsCommandList* list,
        ID3D12CommandQueue* queue,DlssNr::GpuSafety::Ticket producer,DlssNr::GpuSafety::Ticket consumer,
        C::ResourceView endpoint,C::DependencyProof dependency,C::RecordKey consumerRecording,bool crossQueue=false):lifetime_(std::move(lifetime)),resources_(resources),
        action_(std::move(owner)),resource_(resource),list_(list),queue_(queue),producer_(std::move(producer)),
        consumer_(std::move(consumer)),endpoint_(std::move(endpoint)),dependency_(std::move(dependency)),consumerRecording_(consumerRecording),crossQueue_(crossQueue){}
  public:
    const C::ResourceView& Endpoint()const noexcept{return endpoint_;}
    const C::DependencyProof& Dependency()const noexcept{return dependency_;}
    const C::RecordKey& ConsumerRecording()const noexcept{return consumerRecording_;}
    const C::RecordKey& DispatchCall()const noexcept{return dispatchCall_;}
    std::optional<SourceBoundSubmissionOutcome> ObserveSubmission()
    {auto lock=resources_.LockNativeAction();return observeSubmission_?observeSubmission_():std::nullopt;}
    const C::ObjectIncarnation* QueueIdentity()const noexcept
    {return dependency_.consumerQueue.IsKnown()?&dependency_.consumerQueue.KnownPart()->value:nullptr;}
    class Guard
    {
        friend class SelectedFsr3ResourceOwner;
        // Lock is destroyed before lifetime_; a final release may reenter.
        std::shared_ptr<const void> lifetime_;
        std::unique_lock<std::recursive_mutex> lock_;
        const LiveConsumptionLease* lease_;
        OwnerFacts facts_;
        C::DependencyProof dependency_;
        SelectedFsr3ResourceOwner& owner_;
        Guard(SelectedFsr3ResourceOwner& owner,std::shared_ptr<const void> lifetime,std::unique_lock<std::recursive_mutex> lock,
            const LiveConsumptionLease* lease,OwnerFacts facts,C::DependencyProof dependency)
            :lifetime_(std::move(lifetime)),lock_(std::move(lock)),lease_(lease),facts_(std::move(facts)),dependency_(std::move(dependency)),owner_(owner)
        {owner_.active_=true;}
      public:
        ~Guard(){owner_.active_=false;}
        FinalFrameRuntimeProof Proof()const noexcept{return {lease_,&facts_,&dependency_,&owner_.consumerRecording_};}
    };
    // A source-bound action never manufactures a canonical Frame/ExcludePrimary.
    std::unique_ptr<Fsr3FinalResourceGuard> Acquire(ID3D12Resource*,ID3D12GraphicsCommandList*,const DlssNr::GpuSafety::Ticket&)override
    {return {};}
    std::unique_ptr<Guard> AcquireSourceBound(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,
        const DlssNr::GpuSafety::Ticket& ticket,const SelectedFsr3DispatchAdmission& sdk,std::uint32_t* refusal=nullptr)
    {
        const auto refuse=[&](std::uint32_t code)->std::unique_ptr<Guard>{if(refusal&&!*refusal)*refusal=code;return {};};
        auto lock=resources_.LockNativeAction();
        if(!lifetime_||closed_||active_||resource!=resource_||list!=list_||ticket!=consumer_||
           !sdk.Current(resource,list,queue_,ticket))return refuse(101);
        const auto current=resources_.CurrentRegisteredView(resource);
        if(!current||*current!=endpoint_)return refuse(102);
        if(!begun_)
        {
            LeaseReason reason=LeaseReason::None;
            if(!action_.Begin(resource,list,queue_,producer_,consumer_,crossQueue_,&reason))
            {closed_=true;return refuse(400+static_cast<std::uint32_t>(reason));}
            begun_=true;
        }
        LeaseReason reason=LeaseReason::None;
        if(!action_.ValidateSubmit(resource,list,queue_,producer_,consumer_,crossQueue_,&reason))
            return refuse(500+static_cast<std::uint32_t>(reason));
        auto facts=action_.RefreshAction(resource,list,queue_,producer_,consumer_,false,crossQueue_);
        if(!facts)return refuse(105);
        if(!action_.Lease())return refuse(106);
        const auto validated=ValidateForSubmit(*action_.Lease(),*facts);
        if(!validated.allowed)return refuse(300+static_cast<std::uint32_t>(validated.reason));
        return std::unique_ptr<Guard>(new Guard(*this,shared_from_this(),std::move(lock),action_.Lease(),std::move(*facts),dependency_));
    }
    void CloseAdmission()
    {auto lock=resources_.LockNativeAction();closed_=true;action_.Close();}
    std::unique_ptr<Guard> AcquireRetirement()
    {
        auto lock=resources_.LockNativeAction();if(!lifetime_||!begun_||!closed_||active_)return {};
        auto facts=action_.RefreshAction(resource_,nullptr,nullptr,producer_,consumer_,true);
        if(!facts||!action_.Lease()||!CanOwnerReuse(*action_.Lease(),*facts).allowed)return {};
        return std::unique_ptr<Guard>(new Guard(*this,shared_from_this(),std::move(lock),action_.Lease(),std::move(*facts),dependency_));
    }
    std::unique_ptr<Guard> AcquireTerminalObservation()
    {
        auto lock=resources_.LockNativeAction();if(!lifetime_||!begun_||!closed_||active_)return {};
        auto facts=action_.RefreshAction(resource_,nullptr,nullptr,producer_,consumer_,true);
        if(!facts||!action_.Lease())return {};
        return std::unique_ptr<Guard>(new Guard(*this,shared_from_this(),std::move(lock),action_.Lease(),std::move(*facts),dependency_));
    }
};
}
