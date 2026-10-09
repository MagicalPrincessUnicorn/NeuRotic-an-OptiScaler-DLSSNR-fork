#pragma once
#include "ConsumptionLeaseAdapter.h"
#include <dlssnr/NrGpuSafety.h>
#include <memory>

namespace Neurotic::Lifecycle
{
// A host-provided resource-owner facade, not a provider discovery or ownership service.
// Its lifetime covers registrations. Describe/Refresh authenticate C14 against the exact
// native resource at the existing owner's locked boundary; they must not derive IDs from
// addresses. They retain no callback-borrowed COM objects. No serialized input implements it.
class NativeLeaseOwner
{
  public:
    virtual ~NativeLeaseOwner()=default;
    virtual std::optional<LeaseBinding> Describe(ID3D12Resource* resource)=0;
    virtual OwnerFacts Refresh(const LeaseBinding&,ID3D12Resource*,ID3D12GraphicsCommandList*,ID3D12CommandQueue*)=0;
    // Explicit opt-in by the allocation owner. It authenticates the reservation's
    // canonical recording key against this exact existing ticket, allocation and
    // preparation milestone under its lock. The legacy Refresh cannot implicitly
    // authorize a local recording. Default is fail closed.
    virtual OwnerFacts RefreshSameRecording(const LeaseBinding&,ID3D12Resource*,ID3D12GraphicsCommandList*,
        ID3D12CommandQueue*,const DlssNr::GpuSafety::Ticket&){return {};}
};
// CPU-only registration retained by the allocation owner. Native pointers are transient
// comparison inputs on calls below, never identity, serialization or retained COM ownership.
class NativeLeaseRegistration
{
    std::shared_ptr<NativeLeaseOwner> owner;
    std::optional<LiveConsumptionLease> lease;
    C::BoundedList<ExternalHold,16> observedRegistrations;
    bool ownerFailed=false;
    // Remember registration identity, never infer release. A later owner snapshot must
    // include the same registration and explicit release evidence; absence is Unknown.
    bool CompleteHoldSnapshot(const OwnerFacts& f)
    {
        if(ownerFailed||!IsYes(f.externalRegistrationsComplete))return false;
        for(const auto& previous:observedRegistrations)
        {
            bool found=false;
            for(const auto& current:f.holds)
                if(current.registration==previous.registration && current.consumer==previous.consumer &&
                   current.kind==previous.kind && current.provider==previous.provider && current.handoff==previous.handoff &&
                   Context::SameResourceStructure(current.target,previous.target))found=true;
            if(!found)return false;
        }
        for(const auto& current:f.holds)
        {
            bool found=false;
            for(const auto& previous:observedRegistrations)if(current.registration==previous.registration)found=true;
            if(!found&&!observedRegistrations.Push(current)){ownerFailed=true;return false;}
        }
        return true;
    }
    static Fact YesNo(bool value){return value?Fact::Yes:Fact::No;}
    static bool RecordingMatches(const LeaseBinding& binding,ID3D12Resource* resource,
        ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,const DlssNr::GpuSafety::Ticket& ticket)
    {
        if(queue)return DlssNr::GpuSafety::MatchesRecording(ticket,list,queue);
        return binding.sameRecording&&DlssNr::GpuSafety::MatchesLocalRecording(ticket,list,resource);
    }
    static void ApplyProducer(OwnerFacts& f,const DlssNr::GpuSafety::RecordingSnapshot& p)
    {
        if(IsYes(f.registryHealthy))f.registryHealthy=YesNo(p.registryHealthy);
        if(!p.valid||!p.registryHealthy){f.dependency={};return;}
        f.dependency.ordered=YesNo(p.orderedForConsumer);f.dependency.committed=YesNo(p.submitted);
        f.dependency.uniqueSubmission=YesNo(p.uniqueSubmission);f.dependency.completed=YesNo(p.completed);
        f.dependency.nonReplayable=YesNo(p.nonReplayable);f.dependency.reusable=YesNo(p.reusable);
        f.dependency.queueKnown=YesNo(p.queueKnown);f.dependency.sameDevice=YesNo(p.sameDevice);
        f.dependency.supportedType=YesNo(p.supportedType);f.dependency.crossQueue=p.queueKnown&&!p.sameQueue;
    }
    OwnerFacts RefreshUse(const LeaseBinding& binding,ID3D12Resource* resource,ID3D12GraphicsCommandList* list,
        ID3D12CommandQueue* queue,const DlssNr::GpuSafety::Ticket& producer,
        const DlssNr::GpuSafety::Ticket& consumer,bool explicitCrossQueue)
    {
        if(!binding.sameRecording)
        {
            // Refresh is side-effect free. Authenticate it before asking the
            // queue owner to establish any explicit cross-queue dependency.
            return owner->Refresh(binding,resource,list,queue);
        }
        if(explicitCrossQueue||producer!=consumer||!RecordingMatches(binding,resource,list,queue,consumer))return {};
        auto f=owner->RefreshSameRecording(binding,resource,list,queue,consumer);
        const auto p=DlssNr::GpuSafety::InspectRecording(consumer);
        if(!p.valid||!p.registryHealthy||p.submitted||p.nonReplayable||!f.sameRecording)return {};
        // RecordingMatches checked the exact native recording/device/type. The preparation
        // owner independently establishes order and continued exclusive reservation.
        f.dependency.committed=YesNo(p.submitted);f.dependency.uniqueSubmission=YesNo(p.uniqueSubmission);
        f.dependency.completed=YesNo(p.completed);f.dependency.nonReplayable=YesNo(p.nonReplayable);
        f.dependency.reusable=YesNo(p.reusable);f.dependency.sameDevice=Fact::Yes;
        f.dependency.queueKnown=queue?Fact::Yes:Fact::Unknown;
        f.dependency.supportedType=Fact::Yes;f.dependency.crossQueue=false;
        f.dependency.ordered=f.sameRecording->orderEstablished;
        return f;
    }
  public:
    explicit NativeLeaseRegistration(std::shared_ptr<NativeLeaseOwner> source):owner(std::move(source)){}
    NativeLeaseRegistration(const NativeLeaseRegistration&)=delete;
    NativeLeaseRegistration& operator=(const NativeLeaseRegistration&)=delete;
    bool Begin(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
               const DlssNr::GpuSafety::Ticket& producer,const DlssNr::GpuSafety::Ticket& consumer,
               bool explicitCrossQueue,LeaseReason* refusal=nullptr) noexcept
    try
    {
        const auto refuse=[&](LeaseReason reason){if(refusal)*refusal=reason;return false;};
        if(!owner||lease||!resource||!list||!producer||!consumer)return refuse(LeaseReason::OwnerUnavailable);
        const auto binding=owner->Describe(resource);if(!binding)return refuse(LeaseReason::InvalidBinding);
        if(!RecordingMatches(*binding,resource,list,queue,consumer))return refuse(LeaseReason::RecordingUnavailable);
        auto facts=RefreshUse(*binding,resource,list,queue,producer,consumer,explicitCrossQueue);
        if(!CompleteHoldSnapshot(facts))return refuse(LeaseReason::RetentionUnknown);
        const auto checked=CheckBinding(*binding,facts);if(!checked.allowed)return refuse(checked.reason);
        if(!binding->sameRecording)
            ApplyProducer(facts,DlssNr::GpuSafety::InspectRecording(producer,queue,explicitCrossQueue));
        const auto recording=DlssNr::GpuSafety::InspectRecording(consumer);
        facts.recordingTracked=YesNo(recording.valid&&recording.registryHealthy&&!recording.nonReplayable);
        auto candidate=BeginConsumption(*binding,facts);
        // CheckUse is a pure projection of these same captured owner facts.
        if(!candidate)return refuse(CheckUse(*binding,facts).reason);
        const auto validated=ValidateForRecording(*candidate,facts);if(!validated.allowed)return refuse(validated.reason);
        lease=std::move(candidate);return true;
    }
    catch(...){ownerFailed=true;if(refusal)*refusal=LeaseReason::OwnerUnavailable;return false;}
    bool ValidateRecording(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
                        const DlssNr::GpuSafety::Ticket& producer,const DlssNr::GpuSafety::Ticket& consumer,
                        bool explicitCrossQueue) noexcept
    try
    {
        if(!owner||!lease||!resource||!list||!producer||!consumer||
           !RecordingMatches(lease->Binding(),resource,list,queue,consumer))return false;
        auto facts=RefreshUse(lease->Binding(),resource,list,queue,producer,consumer,explicitCrossQueue);
        if(!CompleteHoldSnapshot(facts)||!CheckBinding(lease->Binding(),facts).allowed)return false;
        if(!lease->Binding().sameRecording)
            ApplyProducer(facts,DlssNr::GpuSafety::InspectRecording(producer,queue,explicitCrossQueue));
        const auto recording=DlssNr::GpuSafety::InspectRecording(consumer);
        facts.recordingTracked=YesNo(recording.valid&&recording.registryHealthy&&!recording.nonReplayable);
        return ValidateForRecording(*lease,facts).allowed;
    }
    catch(...){ownerFailed=true;return false;}
    bool ValidateSubmit(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
                        const DlssNr::GpuSafety::Ticket& producer,const DlssNr::GpuSafety::Ticket& consumer,
                        bool explicitCrossQueue,LeaseReason* refusal=nullptr) noexcept
    try
    {
        const auto refuse=[&](LeaseReason reason){if(refusal)*refusal=reason;return false;};
        if(!owner||!lease||!resource||!list||!queue||!producer||!consumer ||
           !DlssNr::GpuSafety::MatchesRecording(consumer,list,queue))return refuse(LeaseReason::RecordingUnavailable);
        auto facts=RefreshUse(lease->Binding(),resource,list,queue,producer,consumer,explicitCrossQueue);
        if(!CompleteHoldSnapshot(facts))return refuse(LeaseReason::RetentionUnknown);
        const auto checked=CheckBinding(lease->Binding(),facts);if(!checked.allowed)return refuse(checked.reason);
        if(!lease->Binding().sameRecording)
            ApplyProducer(facts,DlssNr::GpuSafety::InspectRecording(producer,queue,explicitCrossQueue));
        const auto recording=DlssNr::GpuSafety::InspectRecording(consumer);
        facts.recordingTracked=YesNo(recording.valid&&recording.registryHealthy&&!recording.nonReplayable);
        const auto validated=ValidateForSubmit(*lease,facts);
        return validated.allowed?true:refuse(validated.reason);
    }
    catch(...){ownerFailed=true;if(refusal)*refusal=LeaseReason::OwnerUnavailable;return false;}
    void Close(){if(lease)CloseAdmission(*lease);}
    // Borrowed only while the Resource action lock is held. This exposes the
    // same registered C03 action; repeated guards cannot create another lease.
    const LiveConsumptionLease* Lease()const noexcept{return lease?&*lease:nullptr;}
    std::optional<OwnerFacts> RefreshAction(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,
        ID3D12CommandQueue* queue,const DlssNr::GpuSafety::Ticket& producer,const DlssNr::GpuSafety::Ticket& consumer,
        bool retirement=false,bool explicitCrossQueue=false)noexcept
    try
    {
        if(!owner||!lease||!resource||!producer||!consumer)return {};
        auto facts=retirement?owner->Refresh(lease->Binding(),resource,nullptr,nullptr):
            RefreshUse(lease->Binding(),resource,list,queue,producer,consumer,explicitCrossQueue);
        if(!CompleteHoldSnapshot(facts))return {};
        if(!lease->Binding().sameRecording||retirement)
            ApplyProducer(facts,DlssNr::GpuSafety::InspectRecording(producer,queue,explicitCrossQueue&&!retirement));
        const auto recording=DlssNr::GpuSafety::InspectRecording(consumer);
        facts.recordingTracked=YesNo(recording.valid&&recording.registryHealthy&&!recording.nonReplayable);
        if(retirement)facts.consumerTicketsRetired=recording.valid&&recording.registryHealthy?YesNo(recording.reusable):Fact::Unknown;
        return facts;
    }
    catch(...){ownerFailed=true;return {};}
    bool CanRecycle(ID3D12Resource* resource,const DlssNr::GpuSafety::Ticket& producer,
                    const DlssNr::GpuSafety::Ticket& consumer) noexcept
    try
    {
        if(!owner||!lease||!resource||!producer||!consumer)return false;
        auto facts=owner->Refresh(lease->Binding(),resource,nullptr,nullptr);
        if(!CompleteHoldSnapshot(facts))return false;
        ApplyProducer(facts,DlssNr::GpuSafety::InspectRecording(producer));
        const auto reader=DlssNr::GpuSafety::InspectRecording(consumer);
        facts.consumerTicketsRetired=reader.valid&&reader.registryHealthy?YesNo(reader.reusable):Fact::Unknown;
        return CanOwnerReuse(*lease,facts).allowed;
    }
    catch(...){ownerFailed=true;return false;}
};
}
