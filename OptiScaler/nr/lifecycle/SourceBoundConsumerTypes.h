#pragma once
#include "FinalRealFrameTypes.h"
#include "FinalInputAncestry.h"
#include "SourceTransactionExclusion.h"
#include "NativeEvaluationAssociation.h"
#include <nr/contracts/SourceBoundConsumer.h>

namespace Neurotic::Lifecycle
{
// Observation, reservation, authorization and execution evidence are distinct.
// CPU return, actual SDK submission and physical retirement remain independent.
enum class SourceBoundPhase { Reserved,CandidateBound,ConsumerAuthorized,DispatchReturned,FailedRetained,Retired };
enum class SourceBoundDispatchEffect { NotEntered,ReturnedAccepted,ReturnedRejected,InterruptedUnknown };
struct SourceBoundDispatchOutcome
{
    C::RecordKey call,recording;
    C::ObjectIncarnation queue;
    SourceBoundDispatchEffect effect=SourceBoundDispatchEffect::InterruptedUnknown;
    std::optional<std::int32_t> actualResult;
    bool operator==(const SourceBoundDispatchOutcome&)const=default;
};
struct SourceBoundSubmissionOutcome
{
    C::RecordKey call,recording,submission;
    C::ObjectIncarnation queue;
    // Unknown is retained. Signal failure cannot undo Execute invocation.
    Fact executeInvoked=Fact::Unknown,signalSucceeded=Fact::Unknown,dependencyWaitsSucceeded=Fact::Unknown;
    bool operator==(const SourceBoundSubmissionOutcome&)const=default;
};
struct SourceBoundTerminalReceipt
{
    inline static constexpr std::string_view WireName="SourceBoundConsumerTerminal-v1";
    C::InterceptedSourceTransactionIdV1 transaction;
    C::RecordKey event;
    C::ResourceView returnedOutput,consumerOutput;
    SourceBoundDispatchOutcome dispatch;
    SourceBoundSubmissionOutcome submission;
    bool consumed=false;
    bool operator==(const SourceBoundTerminalReceipt&)const=default;
};
struct SourceBoundPrimaryRequest
{
    C::MetadataRef<C::InterceptedSourceTransactionV1> transaction;
    C::MetadataRef<C::SourceBoundConsumerContractV1> contract;
    C::NrExecutionRecipe recipe;
    C::PlanCommit plan;
    C::MetadataRef<C::GenerationVector> generations;
    std::optional<NativeEvaluationAssociation> evaluationAssociation;
    // The authoritative Resource lineage owner must carry a known generated/NR
    // ancestry edge here. Absence is Unknown, never certified non-generated.
    std::optional<C::RecordReference> knownDerivedFrom;
    bool operator==(const SourceBoundPrimaryRequest&)const=default;
};
class SourceBoundClaimHandle
{
    friend class FinalRealFrameFinalizer;
    const FinalRealFrameFinalizer* owner_=nullptr;
    std::size_t slot_=0;
    std::uint64_t revision_=0;
    SourceBoundClaimHandle(const FinalRealFrameFinalizer* owner,std::size_t slot,std::uint64_t revision)
        :owner_(owner),slot_(slot),revision_(revision){}
  public:
    bool operator==(const SourceBoundClaimHandle&)const=default;
};
// A call-scoped pre-consumption capability. Serialized metadata, an API success
// and an already recorded action cannot construct one. It issues no FinalSealId.
class SourceBoundConsumerAuthorization
{
    friend class FinalRealFrameFinalizer;
    SourceBoundClaimHandle claim_;
    std::uint64_t event_=0;
    SourceBoundConsumerAuthorization(SourceBoundClaimHandle claim,std::uint64_t event):claim_(claim),event_(event){}
  public:
    bool operator==(const SourceBoundConsumerAuthorization&)const=default;
};
class SourceBoundLogicalAcknowledgment
{
    friend class FinalRealFrameFinalizer;
    const FinalRealFrameFinalizer* owner_;
    SourceBoundClaimHandle claim_;
    C::EvaluationId evaluation_;
    bool consumed_;
    SourceBoundLogicalAcknowledgment(const FinalRealFrameFinalizer* owner,SourceBoundClaimHandle claim,
        C::EvaluationId evaluation,bool consumed):owner_(owner),claim_(claim),evaluation_(evaluation),consumed_(consumed){}
  public:
    bool Matches(const FinalRealFrameFinalizer& owner,const SourceBoundClaimHandle& claim,C::EvaluationId evaluation)const
    {return owner_==&owner&&claim_==claim&&evaluation_==evaluation;}
    bool Consumed()const noexcept{return consumed_;}
};
struct SourceBoundResult
{
    bool accepted=false,newPrimaryClaim=false,admissionsClosed=false;
    FinalizationReason reason=FinalizationReason::InvalidState;
    SourceBoundPhase phase=SourceBoundPhase::Reserved;
    std::optional<SourceBoundClaimHandle> claim;
    std::optional<SourceBoundConsumerAuthorization> authorization;
    std::optional<SourceBoundTerminalReceipt> terminal;
    std::optional<SourceBoundLogicalAcknowledgment> logical;
};
// Storage remains inside the existing sole finalizer and its bounded lifetime.
// Entries are never recycled from reset, timeout, failure, callback return or GPU
// completion. Typed retirement preserves these transaction tombstones.
struct SourceBoundFinalizerEntry
{
    bool used=false,closed=false;
    std::uint64_t revision=0,authorizationEvent=0;
    C::RecordKey consumerRecording;
    SourceBoundPrimaryRequest request;
    C::InterceptedSourceTransactionV1 transaction;
    C::NativeFinalConsumerContractV1 consumer;
    C::GenerationVector generations;
    SourceBoundPhase phase=SourceBoundPhase::Reserved;
    std::optional<FinalCandidateSubmission> candidate;
    std::optional<C::NativeStageDeliveryV1> stage;
    std::optional<C::ResourceView> output;
    std::optional<FinalInputAncestry> ancestry;
    std::optional<FinalBoundaryObservation> boundary;
    std::optional<LeaseBinding> binding;
    C::BoundedList<C::RetentionRegistration,16> retentions;
    std::optional<SourceBoundDispatchOutcome> dispatch;
    std::optional<SourceBoundSubmissionOutcome> submission;
    bool providerReleased=false,resourceRetired=false,logicalClosed=false;
    std::optional<SourceBoundTerminalReceipt> terminal;
};
}
