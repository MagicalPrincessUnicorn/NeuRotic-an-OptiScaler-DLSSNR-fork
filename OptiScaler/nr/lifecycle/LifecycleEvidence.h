#pragma once
#include "ConsumptionLeaseAdapter.h"
#include <nr/contracts/C11_Diagnostics.h>

namespace Neurotic::Lifecycle
{
enum class LeaseEvent
{
    LeaseIssued,LeaseRejected,LeaseRevoked,DependencyAccepted,DependencyRejected,OrderedForConsumer,
    ConsumerReadable,RecordedReferenced,SubmittedInFlight,Completed,ReuseBlockedByReplay,ProviderHeld,
    CaptureHeld,FrameGenerationHeld,CompletionUnknown,RetentionUnknown,Released,Retired,StorageReusable,Invalidated
};
inline std::string_view LeaseEventName(LeaseEvent event)
{
    constexpr std::string_view names[]={"LeaseIssued","LeaseRejected","LeaseRevoked","DependencyAccepted",
        "DependencyRejected","OrderedForConsumer","ConsumerReadable","RecordedReferenced","SubmittedInFlight",
        "Completed","ReuseBlockedByReplay","ProviderHeld","CaptureHeld","FrameGenerationHeld","CompletionUnknown",
        "RetentionUnknown","Released","Retired","StorageReusable","Invalidated"};
    const auto index=static_cast<std::size_t>(event);return index<std::size(names)?names[index]:std::string_view{};
}
inline C::OptionalFact<bool> DescribeFact(Fact fact,const C::EvidenceRef& evidence)
{
    return fact==Fact::Unknown?C::OptionalFact<bool>::FromUnknown(Missing(IdentityStatus::Unknown)):
        C::OptionalFact<bool>::FromKnown(fact==Fact::Yes,evidence);
}
inline std::optional<C::ImmutableRecord<C::DependencyProof>> PublishDependencyProof(
    const LiveConsumptionLease& lease,const OwnerFacts& f,const C::RecordHeader& header,
    const C::OptionalFact<C::RecordKey>& recording,const C::OptionalFact<C::RecordKey>& submission,
    const C::OptionalFact<C::RecordKey>& dependency,const C::EvidenceRef& evidence)
{
    if(header.contract!=C::ContractId::C03||header.owner!=C::OwnerDomain::Resource||!Context::ValidValues(header)||
       !ValidEvidence(evidence)||!CheckBinding(lease.Binding(),f).allowed ||
       !Context::ValidValues(recording)||!Context::ValidValues(submission)||!Context::ValidValues(dependency))return {};
    if((IsYes(f.recordingTracked)&&!Context::Established(recording))||
       (IsYes(f.dependency.committed)&&!Context::Established(submission))||
       (IsYes(f.dependency.ordered)&&!Context::Established(dependency)))return {};
    if(lease.Binding().sameRecording)
    {
        const auto& local=*lease.Binding().sameRecording;
        if(!Context::Established(recording)||
           recording.KnownPart()->value!=local.recording||!Context::Established(dependency)||
           dependency.KnownPart()->value!=local.reservation||!f.sameRecording||
           f.sameRecording->use!=local||!IsYes(f.sameRecording->reservationRetained))return {};
        if(f.dependency.committed==Fact::No)
        {
            // Reset/cancellation may retire a never-submitted recording. Report
            // that owner's positive retirement proof without reopening admission.
            const bool canceled=lease.Closed()&&CanOwnerReuse(lease,f).allowed&&
                f.dependency.uniqueSubmission==Fact::No&&f.dependency.completed==Fact::No&&
                f.dependency.ordered==Fact::No;
            if((!ValidateForRecording(lease,f).allowed&&!canceled)||submission.IsKnown())return {};
        }
        else
        {
            // Factual publication after submission/closure does not re-open
            // admission. The queue owner supplies the real submission identity.
            if(!IsYes(f.dependency.committed)||!Context::Established(submission)||
               !IsYes(f.dependency.sameDevice)||!IsYes(f.dependency.queueKnown)||
               !IsYes(f.dependency.supportedType)||
               (IsYes(f.dependency.ordered)&&!IsYes(f.dependency.uniqueSubmission)))return {};
        }
    }
    C::DependencyProof result;result.header=header;
    const auto& description=lease.Binding().description;
    if(IsYes(f.dependency.sameDevice))result.producerDevice=description.device;
    if(IsYes(f.dependency.queueKnown))result.consumerQueue=description.queue;
    if(IsYes(f.dependency.ordered)&&!f.dependency.crossQueue)result.producerQueue=description.queue;
    result.recording=recording;result.submission=submission;result.ownerDependency=dependency;
    result.orderedForConsumer=DescribeFact(f.dependency.ordered,evidence);
    result.gpuCompleted=DescribeFact(f.dependency.completed,evidence);
    result.recordingNonReplayable=DescribeFact(f.dependency.nonReplayable,evidence);
    // Provider release is not derivable from queue execution. Remains Unknown here;
    // explicit retention registrations are published on the separate lease description.
    result.storageReusable=DescribeFact(CanOwnerReuse(lease,f).allowed?Fact::Yes:Fact::No,evidence);
    result.generations=description.generations;
    if(!Context::ValidValues(result))return {};return C::ImmutableRecord<C::DependencyProof>{result};
}
template<class T,std::size_t N,class Store> C::MetadataList<T,N> PublishOwnerList(const C::BoundedList<T,N>& values,Store& store)
{
    C::MetadataList<T,N> result;result.count=static_cast<std::uint32_t>(values.Size());
    if(result.count)result.backing=store.Publish(values,C::OwnerDomain::Resource);return result;
}
inline bool EventEstablished(LeaseEvent event,const LiveConsumptionLease& lease,const OwnerFacts& f)
{
    const auto admitted=CheckUse(lease.Binding(),f).allowed&&!lease.Closed();
    const auto retired=CanOwnerReuse(lease,f).allowed;
    switch(event)
    {
    case LeaseEvent::LeaseIssued:case LeaseEvent::ConsumerReadable:return admitted;
    case LeaseEvent::LeaseRejected:return !admitted;
    case LeaseEvent::LeaseRevoked:case LeaseEvent::Invalidated:return !CheckBinding(lease.Binding(),f).allowed;
    case LeaseEvent::DependencyAccepted:case LeaseEvent::OrderedForConsumer:return IsYes(f.dependency.ordered);
    case LeaseEvent::DependencyRejected:return !IsYes(f.dependency.ordered);
    case LeaseEvent::RecordedReferenced:return IsYes(f.recordingTracked)&&IsYes(f.resourcesRegistered);
    case LeaseEvent::SubmittedInFlight:return IsYes(f.dependency.committed);
    case LeaseEvent::Completed:return IsYes(f.dependency.completed);
    case LeaseEvent::ReuseBlockedByReplay:return !IsYes(f.dependency.nonReplayable);
    case LeaseEvent::CompletionUnknown:return f.dependency.completed==Fact::Unknown;
    case LeaseEvent::RetentionUnknown:
        if(!IsYes(f.externalRegistrationsComplete))return true;
        for(const auto& hold:f.holds)if(hold.released==Fact::Unknown)return true;return false;
    case LeaseEvent::ProviderHeld:case LeaseEvent::CaptureHeld:case LeaseEvent::FrameGenerationHeld:
        for(const auto& hold:f.holds)
            if(hold.released==Fact::No && ((event==LeaseEvent::ProviderHeld&&hold.kind==HoldKind::Provider)||
               (event==LeaseEvent::CaptureHeld&&hold.kind==HoldKind::Capture)||
               (event==LeaseEvent::FrameGenerationHeld&&hold.kind==HoldKind::FrameGeneration)))return true;
        return false;
    case LeaseEvent::Released:case LeaseEvent::Retired:case LeaseEvent::StorageReusable:return retired;
    }
    return false;
}
// Output only. No caller passes this receipt back to admission or storage owners.
template<class Store> std::optional<C::ImmutableRecord<C::OwnerReceipt>> PublishLifecycleEvidence(
    const LiveConsumptionLease& lease,const OwnerFacts& f,LeaseEvent event,const C::RecordHeader& header,
    std::uint64_t sequence,const C::EvidenceRef& evidence,Store& store)
{
    if(header.contract!=C::ContractId::C11||header.owner!=C::OwnerDomain::Resource||!Context::ValidValues(header)||
       !ValidEvidence(evidence)||!EventEstablished(event,lease,f)||LeaseEventName(event).empty())return {};
    C::OwnerReceipt receipt;receipt.header=header;receipt.ownerSequence=sequence;
    if(!receipt.operation.Assign(LeaseEventName(event)))return {};
    C::BoundedList<C::RecordReference,32> causal;
    const auto& d=lease.Binding().description;
    C::Symbol type;if(!type.Assign(C::ConsumptionLease::WireName))return {};
    causal.Push({C::ContractId::C03,type,d.header.record,d.header.revision});
    receipt.causalRecords=PublishOwnerList(causal,store);receipt.relevantGenerations=d.generations;
    receipt.resourceIdentity=C::OptionalFact<C::MetadataRef<C::ResourceIdentityToken>>::FromKnown(
        store.Publish(d.resource,C::OwnerDomain::Resource),evidence);
    C::BoundedList<C::OwnerFact,16> facts;
    const auto add=[&](std::string_view name,Fact value){C::OwnerFact fact;fact.field.Assign(name);
        if(value==Fact::Unknown)fact.value=C::OptionalFact<C::ScalarValue>::FromUnknown(Missing(IdentityStatus::Unknown));
        else fact.value=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{value==Fact::Yes},evidence);
        return facts.Push(fact);};
    if(!add("OrderedForConsumer",f.dependency.ordered)||!add("Completed",f.dependency.completed)||
       !add("RecordingNonReplayable",f.dependency.nonReplayable)||!add("ExternalRegistrationsComplete",f.externalRegistrationsComplete)||
       !add("AdmissionClosed",lease.Closed()?Fact::Yes:Fact::No)||
       !add("StorageReusable",CanOwnerReuse(lease,f).allowed?Fact::Yes:Fact::No))return {};
    receipt.facts=PublishOwnerList(facts,store);
    if(!Context::ValidValues(receipt))return {};return C::ImmutableRecord<C::OwnerReceipt>{receipt};
}
template<class Store> std::optional<C::ImmutableRecord<C::ConsumptionLease>> PublishLeaseDescription(
    const LiveConsumptionLease& lease,const OwnerFacts& f,const C::EvidenceRef& evidence,Store& store)
{
    if(!ValidEvidence(evidence)||!CheckBinding(lease.Binding(),f).allowed||!IsYes(f.externalRegistrationsComplete))return {};
    auto description=lease.Binding().description;C::BoundedList<C::RetentionRegistration,16> retentions;
    for(const auto& hold:f.holds)
    {
        if(!ValidHold(hold,description.resource))return {};
        C::RetentionRegistration registration;registration.consumer=hold.consumer;
        registration.registration=C::OptionalFact<C::RecordKey>::FromKnown(hold.registration,evidence);
        registration.released=DescribeFact(hold.released,evidence);
        if(hold.releaseEvidence)registration.releaseToken=C::OptionalFact<C::RecordKey>::FromKnown(*hold.releaseEvidence,evidence);
        if(!retentions.Push(registration))return {};
    }
    description.retentions=PublishOwnerList(retentions,store);
    if(!Context::ValidValues(description))return {};return C::ImmutableRecord<C::ConsumptionLease>{description};
}
}
