#pragma once
#include "IdentityIssuers.h"
#include "ConsumptionLeaseAdapter.h"
#include <nr/contracts/C05_Routing.h>
#include <nr/contracts/C06_ProtocolInput.h>
#include <nr/contracts/C07_ProtocolOutput.h>
#include <nr/contracts/C08_Reset.h>
#include <nr/contracts/C11_Diagnostics.h>
#include <nr/contracts/C13_FinalFrame.h>
#include <nr/contracts/NativeStageDelivery.h>

namespace Neurotic::Lifecycle
{
// Owner-local specializations. C03/C06/C07/C08/C11/C13/C14 remain the only contracts.
struct FinalizationScopeKey
{
    C::SessionId session; C::RenderStreamId stream; C::ViewId view; C::BaseRealFrameId base;
    C::BoundaryKind boundary=C::BoundaryKind::BeforeFg;
    C::HandoffContractGeneration handoff;
    bool operator==(const FinalizationScopeKey&)const=default;
};
enum class FinalizationPhase { Unclaimed,PrimaryClaimed,EvaluationPending,EvaluationProduced,CandidateQualified,SealedNr,SealedBypass,InterruptedNoSeal };
enum class HandoffPhase { NotOffered,OfferedToFG,ConsumedByFG,ProviderHeld,Released,RejectedByFG,RetentionUnknown };
enum class FinalizationReason { None,Busy,Stopped,UnknownScope,NonReal,InvalidOwner,InvalidRecipe,InvalidPlan,Stale,Capacity,CompetingPrimary,InvalidHandle,InvalidCandidate,InvalidBoundary,MissingLineage,InvalidRights,RetentionUnknown,InvalidState,IssuerRefused,InvalidAcknowledgment,RetiredScope };
struct PrimaryClaimRequest
{
    C::NrExecutionRecipe recipe; C::PlanCommit plan;
    C::MetadataRef<C::GenerationVector> generations;
    C::HandoffContractGeneration handoff;
    C::BoundaryKind consumerBoundary=C::BoundaryKind::BeforeFg;
    RenderObservation observation=RenderObservation::Unknown;
    // Non-Native users provide the same independently published FG contract.
    // Native requests resolve it from their immutable delivery obligation.
    std::optional<C::MetadataRef<C::NativeFinalConsumerContractV1>> expectedConsumer;
    bool operator==(const PrimaryClaimRequest&)const=default;
};
struct FinalCandidateSubmission
{
    C::EvaluationResult result;
    C::MetadataRef<C::GenerationVector> generations;
    C::OptionalFact<bool> acceptedFinalCandidate;
    bool operator==(const FinalCandidateSubmission&)const=default;
};
struct FinalBoundaryObservation
{
    C::RecordHeader owner; // authenticated Presentation/FG owner publication
    C::MetadataRef<C::FrameIdentity> frame;
    C::BoundaryDescription boundary;
    C::ContractRef<C::ContractId::C07> candidate;
    C::MetadataRef<C::ResourceView> output;
    C::MetadataRef<C::GenerationVector> generations;
    C::ProviderIncarnation provider;
    C::HandoffContractGeneration handoff;
    C::Symbol handoffContract;
    std::uint32_t version=1;
    C::ColorDescription color;
    C::OptionalFact<bool> lineageIncludesCandidate,originalPreserved;
    bool originalBypass=false,requiredHudIncluded=true,requireToneMapped=false;
    C::ReasonId bypassReason;
    bool operator==(const FinalBoundaryObservation&)const=default;
};
struct FinalizerEvent { OwnerEvent event; C::MetadataRef<C::GenerationVector> generations; };
struct ProvisionalReleaseProof
{
    C::RecordHeader owner;
    C::OptionalFact<bool> notSubmitted,nonReplayable,notAdvanced;
};
// Non-owning, call-scoped Resource-owner realization. Hold the owner's lock through
// validation and action. The finalizer never serializes or retains these pointers.
struct FinalFrameRuntimeProof
{
    const LiveConsumptionLease* lease=nullptr;
    const OwnerFacts* facts=nullptr;
    const C::DependencyProof* dependency=nullptr;
    const C::RecordKey* consumerRecording=nullptr; // source-bound action owner; never the producer key
};
class FinalRealFrameFinalizer;
class PrimaryNrClaimHandle
{
    const FinalRealFrameFinalizer* host=nullptr;
    std::size_t slot=0;std::uint64_t revision=0;
    PrimaryNrClaimHandle(const FinalRealFrameFinalizer* h,std::size_t s,std::uint64_t r):host(h),slot(s),revision(r){}
    friend class FinalRealFrameFinalizer;
  public:
    bool operator==(const PrimaryNrClaimHandle&)const=default;
};
// Exact operational acknowledgment. C11 diagnostics and physical completion
// cannot construct it; only the sole finalizer issues it after exclusion.
class FinalizerRetirementAcknowledgment
{
    friend class FinalRealFrameFinalizer;
    const FinalRealFrameFinalizer* owner_;
    PrimaryNrClaimHandle claim_;
    FinalizerRetirementAcknowledgment(const FinalRealFrameFinalizer* owner,PrimaryNrClaimHandle claim)
        :owner_(owner),claim_(claim){}
  public:
    bool Matches(const FinalRealFrameFinalizer& owner,const PrimaryNrClaimHandle& claim)const noexcept
    {return owner_==&owner&&claim_==claim;}
};
class FinalizerConsumptionAcknowledgment
{
    friend class FinalRealFrameFinalizer;
    const FinalRealFrameFinalizer* owner_;
    PrimaryNrClaimHandle claim_;
    C::EvaluationId evaluation_;
    FinalizerConsumptionAcknowledgment(const FinalRealFrameFinalizer* owner,PrimaryNrClaimHandle claim,C::EvaluationId evaluation)
        :owner_(owner),claim_(claim),evaluation_(evaluation){}
  public:
    bool Matches(const FinalRealFrameFinalizer& owner,const PrimaryNrClaimHandle& claim,const C::EvaluationId& evaluation)const noexcept
    {return owner_==&owner&&claim_==claim&&evaluation_==evaluation;}
};
class FinalizerInterruptionAcknowledgment
{
    friend class FinalRealFrameFinalizer;
    const FinalRealFrameFinalizer* owner_;
    PrimaryNrClaimHandle claim_;
    C::EvaluationId evaluation_;
    FinalizerInterruptionAcknowledgment(const FinalRealFrameFinalizer* owner,PrimaryNrClaimHandle claim,C::EvaluationId evaluation)
        :owner_(owner),claim_(claim),evaluation_(evaluation){}
  public:
    bool Matches(const FinalRealFrameFinalizer& owner,const PrimaryNrClaimHandle& claim,const C::EvaluationId& evaluation)const noexcept
    {return owner_==&owner&&claim_==claim&&evaluation_==evaluation;}
};
// Canonical C11 plus its bounded immutable metadata closure. Caller retains this
// result before discarding it; diagnostic delivery is never an admission condition.
struct FinalizerReceipt
{
    C::OwnerReceipt record;
    C::BoundedList<C::RecordReference,32> causal;
    C::BoundedList<C::OwnerFact,16> facts;
};
struct FinalPacketSnapshot
{
    C::FinalRealFramePacket packet;
    C::BoundedList<C::RetentionRegistration,16> retentions;
    C::BoundedList<C::RecordReference,32> upstream;
    template<class T> Context::MetadataView<T> Resolve(const C::MetadataRef<T>& ref)const
    {
        if constexpr(std::is_same_v<T,C::BoundedList<C::RetentionRegistration,16>>)
        {if(packet.retentions.backing&&ref==*packet.retentions.backing)return {ref,&retentions};}
        if constexpr(std::is_same_v<T,C::BoundedList<C::RecordReference,32>>)
        {if(packet.upstreamReceipts.backing&&ref==*packet.upstreamReceipts.backing)return {ref,&upstream};}
        return {};
    }
};
struct FinalizationResult
{
    bool accepted=false;FinalizationReason reason=FinalizationReason::InvalidState;
    bool newPrimaryClaim=false; // Operational result only; duplicates never authorize execution.
    std::optional<PrimaryNrClaimHandle> claim;
    std::optional<C::FinalRealFramePacket> packet;
    std::optional<FinalizerReceipt> receipt;
    std::optional<FinalizerRetirementAcknowledgment> retirement;
    std::optional<FinalizerConsumptionAcknowledgment> consumption;
    std::optional<FinalizerInterruptionAcknowledgment> interruption;
};
struct FinalizerDrainStatus
{
    bool admissionsStopped=false;
    std::size_t unsealed=0,sealed=0,providerHeld=0,retentionUnknown=0;
      bool safelyDrained=false;
      bool tailsRetired=false;
};
inline bool FinalizerTrue(const C::OptionalFact<bool>& value)
{return Context::Established(value)&&value.KnownPart()->value;}
template<class T> C::RecordReference FinalizerReference(const T& record)
{
    C::Symbol type;type.Assign(T::WireName);
    return {record.header.contract,type,record.header.record,record.header.revision};
}
inline bool ScopeFrameMatches(const FinalizationScopeKey& s,const C::FrameIdentity& f)
{
    return Context::ValidValues(f)&&Context::Established(f.sessionId)&&f.sessionId.KnownPart()->value==s.session&&
        Context::Established(f.renderStreamId)&&f.renderStreamId.KnownPart()->value==s.stream&&
        Context::Established(f.viewId)&&f.viewId.KnownPart()->value==s.view&&
        Context::Established(f.baseRealFrameId)&&f.baseRealFrameId.KnownPart()->value==s.base&&
        !f.generatedFrameId.IsKnown()&&f.associationEvidence.Size()!=0;
}
}
