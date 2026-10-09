#pragma once
#include <nr/contracts/C03_Consumption.h>
#include <nr/contracts/C02_Context.h>
#include <nr/context/EvidenceQualification.h>
#include <nr/context/ContextMetadata.h>
#include "SameRecordingUse.h"
#include <optional>

namespace Neurotic::Lifecycle
{
namespace C=Contracts;
enum class HoldKind { Provider, Capture, FrameGeneration, Finalizer, NamedOwner };
enum class ContentBasis { PhysicalRevision, InvocationPublication, WriteDestination };
enum class LeaseReason
{
    None, Inactive, InvalidBinding, IdentityChanged, ContentChanged, ConsumerChanged,
    ContextChanged, PlanChanged, DeviceChanged, QueueUnknown, Revoked, BorrowExpired,
    DependencyUnknown, ProducerUncommitted, ReplayPossible, RecordingUnavailable,
    RetentionUnknown, ExternalHeld, CompletionUnknown, OwnerUnavailable
};
struct LeaseDecision { bool allowed=false; LeaseReason reason=LeaseReason::OwnerUnavailable; };
// Owner-local specialization, not a replacement C03 wire schema. These values are never
// decoded as authority. The resource owner supplies and synchronizes every boundary call.
struct LeaseBinding
{
    C::ConsumptionLease description;
    C::RecordKey consumer;
    C::Symbol purpose;
    std::optional<C::RecordKey> plan,profile;
    std::optional<C::GenerationToken> route;
    ContentBasis contentBasis=ContentBasis::PhysicalRevision;
    bool cpuReadback=false;
    bool callbackScoped=false;
    bool dependencyRequired=true;
    std::optional<SameRecordingUse> sameRecording;
};
struct DependencyFacts
{
    Fact ordered=Fact::Unknown,committed=Fact::Unknown,uniqueSubmission=Fact::Unknown;
    Fact completed=Fact::Unknown,nonReplayable=Fact::Unknown,reusable=Fact::Unknown;
    Fact sameDevice=Fact::Unknown,queueKnown=Fact::Unknown,supportedType=Fact::Unknown;
    bool crossQueue=false;
};
struct ExternalHold
{
    C::RecordKey registration,consumer;
    C::ResourceIdentityToken target;
    HoldKind kind=HoldKind::NamedOwner;
    std::optional<C::ProviderIncarnation> provider;
    std::optional<C::GenerationToken> handoff;
    Fact released=Fact::Unknown;
    std::optional<C::RecordKey> releaseEvidence;
};
// Ephemeral operational-owner snapshot. Never feed C11/serialized records back here.
// The same resource-owner lock must cover refresh, validation and the admitted action.
struct OwnerFacts
{
    // A pre-entry reservation protects storage but asserts no provider use.
    struct ProviderReservation
    {
        C::RecordKey registration;
        C::ResourceIdentityToken target;
        C::ProviderIncarnation feature;
    };
    std::optional<ProviderReservation> providerReservation;
    LeaseBinding current;
    DependencyFacts dependency;
    Fact permission=Fact::Unknown,callbackActive=Fact::Unknown,apiCompatible=Fact::Unknown;
    // Reissued by the authentic callback owner at each C03 use checkpoint.
    Fact invocationPublicationCurrent=Fact::Unknown;
    Fact registryHealthy=Fact::Unknown,nativeBindingMatches=Fact::Unknown;
    Fact recordingTracked=Fact::Unknown,resourcesRegistered=Fact::Unknown,descriptorsProtected=Fact::Unknown;
    Fact consumerTicketsRetired=Fact::Unknown,externalRegistrationsComplete=Fact::Unknown;
    Fact ownerCanRecycle=Fact::Unknown;
    C::BoundedList<ExternalHold,16> holds;
    std::optional<SameRecordingFacts> sameRecording;
};
class LiveConsumptionLease
{
    LeaseBinding binding;
    bool closed=false;
    explicit LiveConsumptionLease(const LeaseBinding& value):binding(value){}
    friend std::optional<LiveConsumptionLease> BeginConsumption(const LeaseBinding&,const OwnerFacts&);
    friend void CloseAdmission(LiveConsumptionLease&);
  public:
    LiveConsumptionLease(const LiveConsumptionLease&)=delete;
    LiveConsumptionLease& operator=(const LiveConsumptionLease&)=delete;
    LiveConsumptionLease(LiveConsumptionLease&& other) noexcept :binding(std::move(other.binding)),closed(other.closed)
    { other.closed=true; }
    LiveConsumptionLease& operator=(LiveConsumptionLease&& other) noexcept
    { if(this!=&other){binding=std::move(other.binding);closed=other.closed;other.closed=true;}return *this; }
    const LeaseBinding& Binding() const { return binding; }
    bool Closed() const { return closed; }
    // No GPU object, callback borrow, resource, descriptor or external hold is released here.
};
inline bool IsYes(Fact fact) { return fact==Fact::Yes; }
inline LeaseDecision Refuse(LeaseReason reason) { return {false,reason}; }
inline bool ValidBinding(const LeaseBinding& b)
{
    const auto& d=b.description;
    if(b.sameRecording&&(!b.dependencyRequired||b.cpuReadback||
       (d.use!=C::UsageKind::OrderedGpuRead&&d.use!=C::UsageKind::WriteExclusive&&d.use!=C::UsageKind::RetainedProviderConsumption)||
       (d.use==C::UsageKind::RetainedProviderConsumption)!=b.sameRecording->providerRegistration.has_value()||
       (b.sameRecording->providerRegistration&&b.sameRecording->providerRegistration->Check()!=C::Error::None)||
       b.sameRecording->recording.Check()!=C::Error::None||b.sameRecording->reservation.Check()!=C::Error::None||
       b.sameRecording->producerOrdinal==0||b.sameRecording->producerOrdinal>=b.sameRecording->consumerOrdinal))return false;
    return Context::ValidValues(d) && d.Check()==C::Error::None && d.header.record.Check()==C::Error::None &&
        d.context.record.Check()==C::Error::None && d.context.recordType.View()==C::CanonicalFrameContext::WireName &&
        d.candidate.record.Check()==C::Error::None && d.candidate.recordType.View()==C::AcquisitionCandidate::WireName &&
        b.consumer.Check()==C::Error::None && !b.purpose.Empty() && d.commandScope.key &&
        d.commandScope.key->Check()==C::Error::None &&
        Context::SameResourceStructure(d.resource,d.resource) &&
        (b.contentBasis==ContentBasis::PhysicalRevision?
            Context::Established(d.resource.contentRevision):
         b.contentBasis==ContentBasis::InvocationPublication?
            (b.callbackScoped&&d.use!=C::UsageKind::WriteExclusive):
            d.use==C::UsageKind::WriteExclusive) &&
        (b.dependencyRequired || d.use==C::UsageKind::CallbackRead) &&
        Context::Established(d.device) && Context::Established(d.revoked) && !d.revoked.KnownPart()->value &&
        Context::Established(d.revocationEpoch) && Context::Established(d.expirationBoundary) &&
        Context::Established(d.submissionRecheckRequired) && d.submissionRecheckRequired.KnownPart()->value &&
        (!b.plan || b.plan->Check()==C::Error::None) && (!b.profile || b.profile->Check()==C::Error::None) &&
        (!b.route || (b.route->Check()==C::Error::None && b.route->identity.Check()==C::Error::None && b.route->identity.kind==C::IdentityKind::RouteGeneration));
}
inline LeaseDecision CheckBinding(const LeaseBinding& expected,const OwnerFacts& f)
{
    if(!ValidBinding(expected))return Refuse(LeaseReason::InvalidBinding);
    const auto& a=expected.description;const auto& b=f.current.description;
    if(!IsYes(f.permission)||!IsYes(f.registryHealthy)||!IsYes(f.nativeBindingMatches)||!IsYes(f.apiCompatible))
        return Refuse(LeaseReason::OwnerUnavailable);
    if(!Context::Established(b.revoked)||b.revoked.KnownPart()->value||
       !Context::SameFact(a.revocationEpoch,b.revocationEpoch))return Refuse(LeaseReason::Revoked);
    if(a.header!=b.header || a.context!=b.context || a.candidate!=b.candidate || a.generations!=b.generations)
        return Refuse(LeaseReason::ContextChanged);
    if(expected.consumer!=f.current.consumer || expected.purpose!=f.current.purpose || a.use!=b.use ||
       expected.contentBasis!=f.current.contentBasis ||
       expected.cpuReadback!=f.current.cpuReadback ||
       expected.callbackScoped!=f.current.callbackScoped || expected.dependencyRequired!=f.current.dependencyRequired)
        return Refuse(LeaseReason::ConsumerChanged);
    if(expected.plan!=f.current.plan || expected.profile!=f.current.profile || expected.route!=f.current.route ||
       expected.sameRecording!=f.current.sameRecording)
        return Refuse(LeaseReason::PlanChanged);
    if(!Context::SameResourceStructure(a.resource,b.resource))return Refuse(LeaseReason::IdentityChanged);
    if(expected.contentBasis==ContentBasis::PhysicalRevision&&
       !Context::SameContent(a.resource.contentRevision,b.resource.contentRevision))
        return Refuse(LeaseReason::ContentChanged);
    if(!Context::SameFact(a.device,b.device))return Refuse(LeaseReason::DeviceChanged);
    if(a.commandScope!=b.commandScope || !Context::SameFact(a.expirationBoundary,b.expirationBoundary))
        return Refuse(LeaseReason::BorrowExpired);
    // An exact local recording/reservation can precede assignment of its host
    // queue. Missing queue metadata is not queue identity or submission proof.
    const bool localQueueUnassigned=expected.sameRecording&&
        a.queue.UnknownPart()&&b.queue.UnknownPart()&&
        *a.queue.UnknownPart()==C::UnknownFact{}&&*b.queue.UnknownPart()==C::UnknownFact{};
    if(expected.dependencyRequired && !localQueueUnassigned && !Context::SameFact(a.queue,b.queue))
        return Refuse(LeaseReason::QueueUnknown);
    return {true,LeaseReason::None};
}
inline bool ValidHold(const ExternalHold& h,const C::ResourceIdentityToken& target)
{
    if(h.registration.Check()!=C::Error::None || h.consumer.Check()!=C::Error::None ||
       !Context::SameResourceStructure(h.target,target))return false;
    if(h.kind==HoldKind::Provider || h.kind==HoldKind::FrameGeneration)
        return h.provider && h.provider->Check()==C::Error::None && h.handoff &&
            h.handoff->Check()==C::Error::None && h.handoff->identity.Check()==C::Error::None && h.handoff->identity.kind==C::IdentityKind::HandoffContractGeneration;
    return true;
}
inline LeaseDecision CheckUse(const LeaseBinding& b,const OwnerFacts& f)
{
    auto result=CheckBinding(b,f);if(!result.allowed)return result;
    if(!IsYes(f.externalRegistrationsComplete))return Refuse(LeaseReason::RetentionUnknown);
    bool provider=false;
    for(const auto& hold:f.holds)
    {
        if(!ValidHold(hold,b.description.resource) || hold.released==Fact::Unknown)
            return Refuse(LeaseReason::RetentionUnknown);
        if(hold.consumer==b.consumer && hold.kind==HoldKind::Provider && hold.released==Fact::No)provider=true;
    }
    if((b.callbackScoped||b.description.use==C::UsageKind::CallbackRead)&&!IsYes(f.callbackActive))
        return Refuse(LeaseReason::BorrowExpired);
    if(b.contentBasis==ContentBasis::InvocationPublication&&!IsYes(f.invocationPublicationCurrent))
        return Refuse(LeaseReason::ContentChanged);
    if(!b.dependencyRequired)return {true,LeaseReason::None};
    const auto& d=f.dependency;
    if(!IsYes(d.sameDevice)||!IsYes(d.supportedType))return Refuse(LeaseReason::QueueUnknown);
    if(b.sameRecording)
    {
        if(Context::Established(b.description.queue)?!IsYes(d.queueKnown):d.queueKnown!=Fact::Unknown)
            return Refuse(LeaseReason::QueueUnknown);
        // Local execution permission is not a fabricated producer submission.
        // The ordinary submitted-ticket / cross-queue / provider path below is unchanged.
        if(!f.sameRecording||f.sameRecording->use!=*b.sameRecording||!IsYes(f.callbackActive)||
           !IsYes(f.sameRecording->activeRecording)||!IsYes(f.sameRecording->orderEstablished)||
           !IsYes(f.sameRecording->reservationRetained)||!IsYes(f.sameRecording->conflictingUsesExcluded)||
           !IsYes(d.ordered)||d.crossQueue||d.committed!=Fact::No||d.uniqueSubmission!=Fact::No||
           d.completed!=Fact::No||d.nonReplayable!=Fact::No||d.reusable!=Fact::No)
            return Refuse(LeaseReason::DependencyUnknown);
        const auto& compatible=f.sameRecording->compatibleHolds;
        for(std::size_t i=0;i<compatible.Size();++i)
        {
            if(compatible.Get(i)->Check()!=C::Error::None)return Refuse(LeaseReason::RetentionUnknown);
            for(std::size_t j=0;j<i;++j)if(*compatible.Get(i)==*compatible.Get(j))return Refuse(LeaseReason::RetentionUnknown);
            std::size_t matches=0;for(const auto& hold:f.holds)
                if(hold.registration==*compatible.Get(i)&&hold.released==Fact::No)++matches;
            if(matches!=1)return Refuse(LeaseReason::RetentionUnknown);
        }
        bool exactProvider=false;
        for(const auto& hold:f.holds)
        {
            if(hold.released==Fact::Yes)
            {if(!hold.releaseEvidence||hold.releaseEvidence->Check()!=C::Error::None)return Refuse(LeaseReason::RetentionUnknown);continue;}
            bool admitted=false;for(const auto& registration:compatible)if(registration==hold.registration)admitted=true;
            if(!admitted)return Refuse(LeaseReason::ExternalHeld);
            if(b.sameRecording->providerRegistration&&hold.registration==*b.sameRecording->providerRegistration&&
               hold.kind==HoldKind::Provider&&hold.consumer==b.consumer)exactProvider=true;
        }
        if(f.providerReservation&&b.sameRecording->providerRegistration&&
           f.providerReservation->registration==*b.sameRecording->providerRegistration&&
           f.providerReservation->feature.Check()==C::Error::None&&
           Context::SameResourceStructure(f.providerReservation->target,b.description.resource))exactProvider=true;
        if(b.description.use==C::UsageKind::RetainedProviderConsumption&&!exactProvider)return Refuse(LeaseReason::RetentionUnknown);
        return {true,LeaseReason::None};
    }
    if(!IsYes(d.queueKnown))return Refuse(LeaseReason::QueueUnknown);
    if(!IsYes(d.committed))return Refuse(LeaseReason::ProducerUncommitted);
    if(!IsYes(d.uniqueSubmission)||!IsYes(d.ordered))return Refuse(LeaseReason::DependencyUnknown);
    if(d.crossQueue&&!IsYes(d.nonReplayable))return Refuse(LeaseReason::ReplayPossible);
    if(b.cpuReadback&&(!IsYes(d.completed)||!IsYes(d.nonReplayable)))return Refuse(LeaseReason::CompletionUnknown);
    if(b.description.use==C::UsageKind::RetainedProviderConsumption&&!provider)return Refuse(LeaseReason::RetentionUnknown);
    if(b.description.use==C::UsageKind::WriteExclusive&&
       (!IsYes(d.reusable)||!IsYes(f.consumerTicketsRetired)||!IsYes(f.ownerCanRecycle)))
        return Refuse(LeaseReason::CompletionUnknown);
    if(b.description.use==C::UsageKind::WriteExclusive)
        for(const auto& hold:f.holds)
            if(!IsYes(hold.released)||!hold.releaseEvidence||hold.releaseEvidence->Check()!=C::Error::None)
                return Refuse(LeaseReason::ExternalHeld);
    return {true,LeaseReason::None};
}
inline std::optional<LiveConsumptionLease> BeginConsumption(const LeaseBinding& b,const OwnerFacts& f)
{
    if(!CheckUse(b,f).allowed)return std::nullopt;
    return LiveConsumptionLease(b);
}
inline LeaseDecision ValidateForRecording(const LiveConsumptionLease& lease,const OwnerFacts& f)
{
    if(lease.Closed())return Refuse(LeaseReason::Inactive);
    auto result=CheckUse(lease.Binding(),f);if(!result.allowed)return result;
    if(!IsYes(f.recordingTracked)||!IsYes(f.resourcesRegistered)||!IsYes(f.descriptorsProtected))
        return Refuse(LeaseReason::RecordingUnavailable);
    return {true,LeaseReason::None};
}
inline LeaseDecision ValidateForSubmit(const LiveConsumptionLease& lease,const OwnerFacts& f)
{
    if(lease.Binding().dependencyRequired&&
       (!Context::Established(lease.Binding().description.queue)||!IsYes(f.dependency.queueKnown)))
        return Refuse(LeaseReason::QueueUnknown);
    return ValidateForRecording(lease,f); // fresh owner facts, never a cached planning result
}
inline void CloseAdmission(LiveConsumptionLease& lease) { lease.closed=true; }
inline LeaseDecision CanOwnerReuse(const LiveConsumptionLease& lease,const OwnerFacts& f)
{
    if(!lease.Closed())return Refuse(LeaseReason::Inactive);
    // Admission may have expired or been revoked. Retirement instead requires the
    // allocation owner to still own the exact storage and explicitly permit recycling.
    if(!ValidBinding(lease.Binding())||!IsYes(f.registryHealthy)||!IsYes(f.nativeBindingMatches)||
       !IsYes(f.apiCompatible)||!IsYes(f.ownerCanRecycle))return Refuse(LeaseReason::OwnerUnavailable);
    const auto& original=lease.Binding().description;const auto& current=f.current.description;
    if(!Context::SameResourceStructure(original.resource,current.resource)||!Context::SameFact(original.device,current.device))
        return Refuse(LeaseReason::IdentityChanged);
    if(!IsYes(f.dependency.nonReplayable))return Refuse(LeaseReason::ReplayPossible);
    if(!IsYes(f.dependency.reusable)||!IsYes(f.consumerTicketsRetired)||!IsYes(f.descriptorsProtected)||
       !IsYes(f.ownerCanRecycle))return Refuse(LeaseReason::CompletionUnknown);
    if(!IsYes(f.externalRegistrationsComplete))return Refuse(LeaseReason::RetentionUnknown);
    for(const auto& hold:f.holds)
    {
        if(!ValidHold(hold,lease.Binding().description.resource)||hold.released==Fact::Unknown)
            return Refuse(LeaseReason::RetentionUnknown);
        if(!IsYes(hold.released))return Refuse(LeaseReason::ExternalHeld);
        if(!hold.releaseEvidence||hold.releaseEvidence->Check()!=C::Error::None)
            return Refuse(LeaseReason::RetentionUnknown);
    }
    return {true,LeaseReason::None};
}
}
