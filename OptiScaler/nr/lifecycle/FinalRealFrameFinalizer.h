#pragma once
#include "FinalRealFrameRuntime.h"
#include "FinalRealFrameReset.h"
#include "NativeUnsealedRetirement.h"
#include "SourceBoundConsumerTypes.h"
#include <nr/context/NativeStageQualification.h>
#include <atomic>
#include <memory>
#include <type_traits>

namespace Neurotic::Orchestration { class StreamCoordinatorKernel; }
namespace Neurotic::Protocol { class NativeInvocationOwnerPort; }
namespace Neurotic::Lifecycle
{
// The owning session supplies one already-bound issuer. No host, queue, model,
// GPU ticket or resource owner is created here. All methods are nonblocking.
class FinalRealFrameFinalizer
{
    friend class Orchestration::StreamCoordinatorKernel;
    friend class NativeSourceBoundConsumerAction;
    friend class Protocol::NativeInvocationOwnerPort;
#ifdef NR_SOURCE_BOUND_CONSUMER_TESTING
    friend struct SourceBoundConsumerTestAccess;
#endif
    struct Entry
    {
        bool used=false,committed=false,releasedClaim=false,resourceRetired=false,consumptionRejected=false;
        std::uint64_t revision=0;
        FinalizationScopeKey scope;
        PrimaryClaimRequest request;
        C::GenerationVector generations;
        C::NativeFinalConsumerContractV1 expectedConsumer;
        std::optional<FinalCandidateSubmission> candidate;
        std::optional<C::NativeStageDeliveryV1> stage;
        std::optional<C::ResourceView> returnedOutput;
        std::optional<FinalBoundaryObservation> boundary;
        std::optional<C::ResourceView> output;
        std::optional<LeaseBinding> binding;
        std::optional<C::FinalRealFramePacket> packet;
        C::BoundedList<C::RetentionRegistration,16> retentions;
        C::BoundedList<C::RecordReference,32> upstream;
        FinalizationPhase phase=FinalizationPhase::Unclaimed;
        HandoffPhase handoff=HandoffPhase::NotOffered;
        std::optional<FinalizerReceipt> lastReceipt,sealReceipt;
        std::optional<C::FgHandoffReceipt> consume,release;
    };
    C::SessionId session_;OwnerBinding owner_;FinalizerIssuer issuer_;
    std::unique_ptr<Entry[]> entries_;std::size_t capacity_;
    std::unique_ptr<std::optional<FinalizationScopeKey>[]> exclusions_;
    std::unique_ptr<SourceBoundFinalizerEntry[]> sourceBound_;
    std::atomic_flag entered_=ATOMIC_FLAG_INIT;
    bool stopped_=false;std::uint64_t lastEvent_=0;
    struct Guard
    {
        std::atomic_flag& flag;bool entered;
        explicit Guard(std::atomic_flag& f):flag(f),entered(!f.test_and_set(std::memory_order_acquire)){}
        ~Guard(){if(entered)flag.clear(std::memory_order_release);}
    };
    static FinalizationResult Reject(FinalizationReason reason){FinalizationResult r;r.reason=reason;return r;}
    Entry* Find(const PrimaryNrClaimHandle& handle)
    {
        if(handle.host!=this||handle.slot>=capacity_)return nullptr;
        auto& e=entries_[handle.slot];return e.used&&e.revision==handle.revision&&!e.releasedClaim?&e:nullptr;
    }
    Entry* Find(const C::FinalSealId& seal)
    {for(std::size_t i=0;i<capacity_;++i)if(entries_[i].packet&&entries_[i].packet->seal==seal)return &entries_[i];return nullptr;}
    bool ValidEvent(const FinalizerEvent& event)const
    {return CheckOwnerEvent(owner_,event.event)==IdentityStatus::Ok&&event.event.evidence.record.value>lastEvent_&&Lifecycle::ValidMetadataDescriptor(event.generations);}
    std::optional<FinalizerReceipt> Receipt(const Entry& e,const FinalizerEvent& event,std::string_view operation)const
    {
        if(!ValidEvent(event))return {};
        FinalizerReceipt result;auto& r=result.record;
        r.header.contract=C::ContractId::C11;r.header.owner=C::OwnerDomain::Finalizer;
        r.header.record=event.event.evidence.record;r.header.revision=1;r.header.scope.key=owner_.subject;
        r.ownerSequence=event.event.evidence.record.value;r.operation.Assign(operation);r.relevantGenerations=event.generations;
        result.causal.Push(FinalizerReference(e.request.recipe));result.causal.Push(FinalizerReference(e.request.plan));
        if(e.candidate)result.causal.Push(FinalizerReference(e.candidate->result));
        if(e.packet)result.causal.Push(FinalizerReference(*e.packet));
        r.causalRecords.count=static_cast<std::uint32_t>(result.causal.Size());
        r.causalRecords.backing=C::MetadataRef<C::BoundedList<C::RecordReference,32>>{C::OwnerDomain::Finalizer,r.header.record,1,{}};
        C::OwnerFact fact;fact.field.Assign("BaseRealFrameValue");
        fact.value=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{e.scope.base.value},event.event.evidence);result.facts.Push(fact);
        r.facts.count=1;r.facts.backing=C::MetadataRef<C::BoundedList<C::OwnerFact,16>>{C::OwnerDomain::Finalizer,r.header.record,1,{}};
        if(!Context::ValidValues(r))return {};return result;
    }
    FinalizationResult Accept(Entry& e,const PrimaryNrClaimHandle& handle,FinalizerReceipt receipt)
    {
        lastEvent_=receipt.record.ownerSequence;e.lastReceipt=std::move(receipt);
        FinalizationResult result;result.accepted=true;result.reason=FinalizationReason::None;
        result.claim=handle;result.packet=e.packet;result.receipt=e.lastReceipt;
        if(e.consume&&FinalizerTrue(e.consume->accepted))
            result.consumption=FinalizerConsumptionAcknowledgment(this,handle,e.request.recipe.evaluation);
        if(e.phase==FinalizationPhase::InterruptedNoSeal||e.consumptionRejected)
            result.interruption=FinalizerInterruptionAcknowledgment(this,handle,e.request.recipe.evaluation);
        return result;
    }
    FinalizationResult Existing(const Entry& e,const PrimaryNrClaimHandle& handle)const
    {
        FinalizationResult result;result.accepted=true;result.reason=FinalizationReason::None;
        result.claim=handle;result.packet=e.packet;result.receipt=e.packet?e.sealReceipt:e.lastReceipt;
        if(e.consume&&FinalizerTrue(e.consume->accepted))
            result.consumption=FinalizerConsumptionAcknowledgment(this,handle,e.request.recipe.evaluation);
        if(e.phase==FinalizationPhase::InterruptedNoSeal||e.consumptionRejected)
            result.interruption=FinalizerInterruptionAcknowledgment(this,handle,e.request.recipe.evaluation);
        return result;
    }
    template<class Reader> bool Current(const Entry& e,const C::MetadataRef<C::GenerationVector>& ref,const Reader& reader)const
    {const auto* values=Context::ResolveMetadata(ref,reader);return values&&*values==e.generations;}
    static bool MayAdvance(const C::EvaluationResult& result)
    {
        if(result.stage==C::OutcomeStage::Submitted||result.stage==C::OutcomeStage::Produced)return true;
        return result.stage!=C::OutcomeStage::NotAttempted&&result.stage!=C::OutcomeStage::Bypassed&&
            (!Context::Established(result.historyAdvancement)||result.historyAdvancement.KnownPart()->value!=C::HistoryAdvancement::NotAdvanced);
    }
    static bool SameScopeFamily(FinalizationScopeKey a,FinalizationScopeKey b)
    {a.base.value=1;b.base.value=1;return a==b;}
    static bool CaptureRetentions(const FinalFrameRuntimeProof& proof,const FinalBoundaryObservation& boundary,const C::EvidenceRef& evidence,C::BoundedList<C::RetentionRegistration,16>& output)
    {
        for(const auto& hold:proof.facts->holds)
        {
            if(!FinalFrameProviderHold(hold,proof.lease->Binding(),boundary.provider,boundary.handoff))continue;
            for(const auto& prior:output)if(prior.registration.KnownPart()->value==hold.registration)return false;
            C::RetentionRegistration registration;registration.consumer=hold.consumer;
            registration.registration=C::OptionalFact<C::RecordKey>::FromKnown(hold.registration,evidence);
            registration.released=C::OptionalFact<bool>::FromKnown(false,evidence);
            if(!output.Push(registration))return false;
        }
        return output.Size()!=0;
    }
    static bool SameRetentions(const C::BoundedList<C::RetentionRegistration,16>& a,const C::BoundedList<C::RetentionRegistration,16>& b)
    {
        if(a.Size()!=b.Size())return false;
        for(const auto& left:a)
        {std::size_t count=0;for(const auto& right:b)if(left.consumer==right.consumer&&Context::SameFact(left.registration,right.registration)&&Context::SameFact(left.released,right.released))++count;
         if(count!=1)return false;}
        return true;
    }
    static bool Terminal(const Entry& e)
    {
        return e.releasedClaim||(e.resourceRetired&&
            (e.packet?e.handoff==HandoffPhase::Released:e.phase==FinalizationPhase::InterruptedNoSeal));
    }
    // Guard is already held. Reserve the persistent exclusion before reclaiming
    // anything. An unresolved earlier reservation prevents advancing the prefix.
    FinalizationResult RetireExcluded(Entry& e,const C::RecordHeader& registry,const C::FrameIdentity& through,
        const C::OptionalFact<bool>& noFuturePrimaryThrough,const FinalizerEvent& event)
    {
        if(!Terminal(e)||!Context::ValidValues(registry)||registry.contract!=C::ContractId::C11||
           registry.owner!=C::OwnerDomain::IdentityRegistry||!ScopeFrameMatches(e.scope,through)||
           !FinalizerTrue(noFuturePrimaryThrough))return Reject(FinalizationReason::InvalidRights);
        std::size_t target=capacity_,empty=capacity_;
        for(std::size_t i=0;i<capacity_;++i)
        {
            if(exclusions_[i]&&SameScopeFamily(*exclusions_[i],e.scope)){target=i;break;}
            if(!exclusions_[i]&&empty==capacity_)empty=i;
        }
        if(target==capacity_)target=empty;if(target==capacity_)return Reject(FinalizationReason::Capacity);
        for(std::size_t i=0;i<capacity_;++i)
            if(entries_[i].used&&SameScopeFamily(entries_[i].scope,e.scope)&&entries_[i].scope.base.value<=e.scope.base.value&&
               (!Terminal(entries_[i])||(&entries_[i]!=&e&&!entries_[i].packet&&!entries_[i].releasedClaim)))
                return Reject(FinalizationReason::InvalidRights);
        auto receipt=Receipt(e,event,"ScopeRetiredByOwnerExclusion");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        const auto scope=e.scope;const auto handle=PrimaryNrClaimHandle{this,static_cast<std::size_t>(&e-entries_.get()),e.revision};
        auto result=Accept(e,handle,std::move(*receipt));
        result.retirement=FinalizerRetirementAcknowledgment(this,handle);
        if(!exclusions_[target]||exclusions_[target]->base.value<scope.base.value)exclusions_[target]=scope;
        // Other terminal scopes keep their own acknowledgment opportunity.
        // Prefix exclusion alone must not lose a coordinator's exact claim tail.
        for(std::size_t i=0;i<capacity_;++i)if(entries_[i].used&&SameScopeFamily(entries_[i].scope,scope)&&
            entries_[i].scope.base.value<=scope.base.value&&(&entries_[i]==&e||entries_[i].releasedClaim))
        {const auto revision=entries_[i].revision;entries_[i]=Entry{};entries_[i].revision=revision;}
        return result;
    }
  public:
    FinalRealFrameFinalizer(const C::SessionId& session,const OwnerBinding& owner,FinalizerIssuer issuer,std::size_t capacity)
        :session_(session),owner_(owner),issuer_(issuer),
         entries_(capacity?std::make_unique<Entry[]>(capacity):nullptr),capacity_(entries_?capacity:0),
         exclusions_(capacity?std::make_unique<std::optional<FinalizationScopeKey>[]>(capacity):nullptr),
         sourceBound_(capacity?std::make_unique<SourceBoundFinalizerEntry[]>(capacity):nullptr)
    {if(!ValidIdentity(session.Describe())||!ValidBinding(owner)||owner.owner!=C::OwnerDomain::Finalizer)stopped_=true;}
    FinalRealFrameFinalizer(const FinalRealFrameFinalizer&)=delete;
    FinalRealFrameFinalizer& operator=(const FinalRealFrameFinalizer&)=delete;
    template<class Reader> FinalizationResult TryClaimPrimary(const PrimaryClaimRequest& request,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);
        if(stopped_)return Reject(FinalizationReason::Stopped);
        if(request.observation!=RenderObservation::NewGameContent)return Reject(FinalizationReason::NonReal);
        const auto* frame=Context::ResolveMetadata(request.recipe.frame,reader);
        const auto* gens=Context::ResolveMetadata(request.generations,reader);
        if(!frame||!gens||!Context::Established(frame->sessionId)||!Context::Established(frame->renderStreamId)||
           !Context::Established(frame->viewId)||!Context::Established(frame->baseRealFrameId)||
           !ValidIdentity(request.handoff.Describe())||request.consumerBoundary!=C::BoundaryKind::BeforeFg)return Reject(FinalizationReason::UnknownScope);
        FinalizationScopeKey scope{frame->sessionId.KnownPart()->value,frame->renderStreamId.KnownPart()->value,
            frame->viewId.KnownPart()->value,frame->baseRealFrameId.KnownPart()->value,request.consumerBoundary,request.handoff};
        if(scope.session!=session_||!ScopeFrameMatches(scope,*frame))return Reject(FinalizationReason::UnknownScope);
        for(std::size_t i=0;i<capacity_;++i)
            if(exclusions_[i]&&SameScopeFamily(*exclusions_[i],scope)&&scope.base.value<=exclusions_[i]->base.value)return Reject(FinalizationReason::RetiredScope);
        // Lookup precedes route/plan checks: these cannot widen exactly-once scope.
        for(std::size_t i=0;i<capacity_;++i)if(entries_[i].used&&entries_[i].scope==scope&&!entries_[i].releasedClaim)
        {auto& e=entries_[i];return e.request==request?Existing(e,{this,i,e.revision}):Reject(FinalizationReason::CompetingPrimary);}
        if(!Context::ValidValues(request.recipe)||!Context::ValidValues(request.plan)||!FinalizerTrue(request.plan.committed)||
           !Context::Established(request.plan.routeGeneration)||request.recipe.committedPlan.recordType.View()!=C::PlanCommit::WireName||
           request.recipe.committedPlan.record!=request.plan.header.record||request.recipe.committedPlan.revision!=request.plan.header.revision||
           !Context::ResolveMetadata(request.recipe.history,reader)||!Context::ResolveMetadata(request.recipe.masks,reader))return Reject(FinalizationReason::InvalidPlan);
        if(!Context::Established(frame->evaluationId)||frame->evaluationId.KnownPart()->value!=request.recipe.evaluation)return Reject(FinalizationReason::InvalidRecipe);
        auto expectedRef=request.expectedConsumer;
        if(request.recipe.nativeDelivery)
        {
            const auto* delivery=Context::ResolveMetadata(*request.recipe.nativeDelivery,reader);
            if(!delivery||!Context::ValidValues(*delivery)||delivery->kind!=C::NativeDeliveryKind::FinalConsumerRequired||
               !delivery->finalConsumer||(expectedRef&&expectedRef!=delivery->finalConsumer))return Reject(FinalizationReason::InvalidBoundary);
            expectedRef=delivery->finalConsumer;
        }
        const auto* expected=expectedRef?Context::ResolveMetadata(*expectedRef,reader):nullptr;
        if(!expectedRef||expectedRef->owner!=C::OwnerDomain::FrameGeneration||!expected||!Context::ValidValues(*expected)||
           expected->handoff!=request.handoff||expected->boundary!=request.consumerBoundary)return Reject(FinalizationReason::InvalidBoundary);
        for(std::size_t i=0;i<capacity_;++i)
        {
            auto& e=entries_[i];if(e.used&&!(e.releasedClaim&&e.scope==scope))continue;
            // All preparation is stack/value-only. No allocation is performed on this path.
            Entry prepared;prepared.used=true;prepared.scope=scope;prepared.request=request;prepared.generations=*gens;
            prepared.expectedConsumer=*expected;
            prepared.revision=e.revision+1;if(prepared.revision==0)return Reject(FinalizationReason::Capacity);
            prepared.phase=FinalizationPhase::PrimaryClaimed;
            auto receipt=Receipt(prepared,event,"PrimaryClaimAccepted");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
            e=std::move(prepared);auto result=Accept(e,{this,i,e.revision},std::move(*receipt));
            result.newPrimaryClaim=true;return result;
        }
        return Reject(FinalizationReason::Capacity);
    }
    template<class Reader> FinalizationResult SubmitEvaluationResult(const PrimaryNrClaimHandle& handle,const FinalCandidateSubmission& candidate,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);
        auto* e=Find(handle);if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->phase==FinalizationPhase::InterruptedNoSeal)return Reject(FinalizationReason::InvalidState);
        if(e->candidate&&*e->candidate==candidate)return Existing(*e,handle);
        if(e->packet||e->boundary||e->stage)return Reject(FinalizationReason::InvalidState);
        const auto& result=candidate.result;const auto* frame=Context::ResolveMetadata(result.originalLineage,reader);
        if(!Context::ValidValues(result)||result.evaluation!=e->request.recipe.evaluation||!frame||!ScopeFrameMatches(e->scope,*frame)||
           !Current(*e,candidate.generations,reader))return Reject(FinalizationReason::InvalidCandidate);
        if(e->candidate&&(result.header.record!=e->candidate->result.header.record||result.header.revision<=e->candidate->result.header.revision))return Reject(FinalizationReason::InvalidCandidate);
        auto receipt=Receipt(*e,event,"CandidateReceived");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        receipt->causal.Push(FinalizerReference(result));receipt->record.causalRecords.count=static_cast<std::uint32_t>(receipt->causal.Size());
        e->committed=e->committed||MayAdvance(result);e->candidate=candidate;
        e->phase=result.stage==C::OutcomeStage::Produced?FinalizationPhase::EvaluationProduced:FinalizationPhase::EvaluationPending;
        return Accept(*e,handle,std::move(*receipt));
    }
    FinalizationResult ReleaseUncommittedClaim(const PrimaryNrClaimHandle& handle,const ProvisionalReleaseProof& proof,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->committed||e->packet||!e->candidate||proof.owner!=e->candidate->result.header||
           e->candidate->result.stage!=C::OutcomeStage::NotAttempted||
           !Context::Established(e->candidate->result.historyAdvancement)||e->candidate->result.historyAdvancement.KnownPart()->value!=C::HistoryAdvancement::NotAdvanced||
           !Context::ValidValues(proof.owner)||proof.owner.owner!=C::OwnerDomain::Strategy||
           !FinalizerTrue(proof.notSubmitted)||!FinalizerTrue(proof.nonReplayable)||!FinalizerTrue(proof.notAdvanced))return Reject(FinalizationReason::CompetingPrimary);
        auto receipt=Receipt(*e,event,"PrimaryClaimReleasedBeforeCommit");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->releasedClaim=true;return Accept(*e,handle,std::move(*receipt));
    }
    // Called by the retained Native session at the actual final boundary, after
    // the no-throw outer return has published its reserved Provider record.
    // This supports unchanged exact returned content. Later transforms require
    // their own enrolled lineage owner; a presentation boolean cannot add edges.
    template<class Reader> FinalizationResult SubmitStageDelivery(const PrimaryNrClaimHandle& handle,
        const C::NativeStageDeliveryV1& stage,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->phase==FinalizationPhase::InterruptedNoSeal)return Reject(FinalizationReason::InvalidState);
        if(e->stage)return *e->stage==stage?Existing(*e,handle):Reject(FinalizationReason::InvalidCandidate);
        if(e->boundary||e->packet||!e->candidate||!e->request.recipe.nativeDelivery||!e->request.recipe.nativeSample||
           !Context::ValidValues(stage)||stage.outcome!=C::NativeStageOutcome::HostReturnRecorded||
           stage.delivery!=*e->request.recipe.nativeDelivery||stage.sample!=*e->request.recipe.nativeSample||
           stage.plan!=e->request.recipe.committedPlan||stage.route!=e->request.plan.routeGeneration.KnownPart()->value||
           stage.recipe.recordType.View()!=C::NrExecutionRecipe::WireName||stage.recipe.record!=e->request.recipe.header.record||
           stage.recipe.revision!=e->request.recipe.header.revision||stage.evaluation!=e->request.recipe.evaluation||
           stage.placement!=e->request.recipe.placement||stage.executionResult.recordType.View()!=C::EvaluationResult::WireName||
           stage.executionResult.record!=e->candidate->result.header.record||stage.executionResult.revision!=e->candidate->result.header.revision||
           e->candidate->result.stage!=C::OutcomeStage::Produced||!stage.nativeOutput||!stage.returnedOutput)
            return Reject(FinalizationReason::InvalidCandidate);
        const C::MetadataRef<C::NativeStageDeliveryV1> ref{stage.publication.owner,stage.publication.record,stage.publication.revision,{}};
        const auto* published=Context::ResolveMetadata(ref,reader);
        const auto* native=Context::ResolveMetadata(*stage.nativeOutput,reader);
        const auto* returned=Context::ResolveMetadata(*stage.returnedOutput,reader);
        const auto* nativeView=native?Context::ResolveNativeOutput(*native,reader):nullptr;
        const auto* returnedView=returned?Context::ResolveNativeOutput(*returned,reader):nullptr;
        const auto* produced=Context::ResolveOptional(e->candidate->result.output,reader);
        const auto* sample=Context::ResolveMetadata(stage.sample,reader);
        const C::BoundedList<C::OwnerValueReference,16>* causes=nullptr;
        if(!published||*published!=stage||!nativeView||!returnedView||!produced||!sample||sample->Check()!=C::Error::None||
           *nativeView!=*produced||stage.lastRecording!=returned->recording||stage.lastRecordedOrdinal<returned->producerOrdinal||
           !Context::ResolveList(stage.causes,reader,causes)||!causes||!causes->Size())return Reject(FinalizationReason::MissingLineage);
        auto receipt=Receipt(*e,event,"NativeStageDeliveryReceived");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->stage=stage;e->returnedOutput=*returnedView;return Accept(*e,handle,std::move(*receipt));
    }
    template<class Reader> FinalizationResult PrepareSeal(const PrimaryNrClaimHandle& handle,const FinalBoundaryObservation& boundary,const FinalFrameRuntimeProof& proof,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->phase==FinalizationPhase::InterruptedNoSeal)return Reject(FinalizationReason::InvalidState);
        if(e->packet)return Existing(*e,handle);
        if(e->boundary)return *e->boundary==boundary?Existing(*e,handle):Reject(FinalizationReason::InvalidBoundary);
        if(!Context::ValidValues(boundary.owner)||(boundary.owner.owner!=C::OwnerDomain::Presentation&&boundary.owner.owner!=C::OwnerDomain::FrameGeneration)||
           !Context::ValidValues(boundary.boundary)||!FinalizerTrue(boundary.boundary.afterRequiredHostRendering)||
           !Context::Established(boundary.boundary.kind)||boundary.boundary.kind.KnownPart()->value!=e->scope.boundary||
           !Context::Established(boundary.boundary.hudIncluded)||boundary.boundary.hudIncluded.KnownPart()->value!=boundary.requiredHudIncluded||
           (boundary.requireToneMapped&&!FinalizerTrue(boundary.boundary.toneMapped))||
           !Context::ValidValues(boundary.color)||!Context::Established(boundary.color.domain)||
           boundary.handoff!=e->scope.handoff||!ValidIdentity(boundary.provider.Describe())||boundary.handoffContract.Empty()||!boundary.version)return Reject(FinalizationReason::InvalidBoundary);
        const auto& expected=e->expectedConsumer;
        if(boundary.provider!=expected.provider||boundary.handoff!=expected.handoff||boundary.handoffContract!=expected.contract||
           boundary.version!=expected.contractVersion||boundary.color.domain.KnownPart()->value!=expected.color||
           boundary.boundary.hudIncluded.KnownPart()->value!=expected.hudIncluded||
           !Context::Established(boundary.boundary.toneMapped)||boundary.boundary.toneMapped.KnownPart()->value!=expected.toneMapped)
            return Reject(FinalizationReason::InvalidBoundary);
        const auto* frame=Context::ResolveMetadata(boundary.frame,reader);const auto* output=Context::ResolveMetadata(boundary.output,reader);
        if(!frame||!ScopeFrameMatches(e->scope,*frame)||!output||!Current(*e,boundary.generations,reader))return Reject(FinalizationReason::Stale);
        if(e->request.recipe.nativeDelivery&&(!e->stage||!e->returnedOutput||*output!=*e->returnedOutput))
            return Reject(FinalizationReason::MissingLineage);
        if(boundary.originalBypass)
        {
            if(e->request.recipe.failureDisposition!=C::FailureDisposition::PreserveOriginal||!FinalizerTrue(boundary.originalPreserved))return Reject(FinalizationReason::MissingLineage);
        }
        else
        {
            if(!e->candidate||e->candidate->result.stage!=C::OutcomeStage::Produced||!Context::ResolveOptional(e->candidate->result.output,reader)||
               !Context::Established(e->candidate->result.composition)||e->candidate->result.composition.KnownPart()->value!=C::CompositionStatus::Accepted||
               !FinalizerTrue(e->candidate->acceptedFinalCandidate)||!FinalizerTrue(boundary.lineageIncludesCandidate)||
               boundary.candidate.recordType.View()!=C::EvaluationResult::WireName||boundary.candidate.record!=e->candidate->result.header.record||
               boundary.candidate.revision!=e->candidate->result.header.revision)return Reject(FinalizationReason::MissingLineage);
        }
        if(!Context::Established(output->descriptor.allocation)||output->descriptor.allocation.KnownPart()->value!=expected.extent||!Context::Established(output->descriptor.format)||
           !Context::Established(output->descriptor.sampleCount)||!Context::Established(output->descriptor.api))return Reject(FinalizationReason::InvalidBoundary);
        if(!ValidateFinalFrameRuntime(proof,*output,boundary.provider,boundary.handoff,e->request.plan.header.record))return Reject(FinalizationReason::InvalidRights);
        C::BoundedList<C::RetentionRegistration,16> retentions;
        if(!CaptureRetentions(proof,boundary,event.event.evidence,retentions))return Reject(FinalizationReason::RetentionUnknown);
        auto receipt=Receipt(*e,event,"BoundaryQualified");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->boundary=boundary;e->output=*output;e->binding=proof.lease->Binding();e->retentions=retentions;e->phase=FinalizationPhase::CandidateQualified;
        return Accept(*e,handle,std::move(*receipt));
    }
    template<class Reader> FinalizationResult CommitSeal(const PrimaryNrClaimHandle& handle,const C::MetadataRef<C::GenerationVector>& current,const FinalFrameRuntimeProof& proof,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->packet)return Existing(*e,handle); // Must precede issuer call, including route changes.
        if(stopped_||!e->boundary||!e->output||!e->binding||e->phase!=FinalizationPhase::CandidateQualified)return Reject(FinalizationReason::InvalidState);
        const auto& b=*e->boundary;
        if(!Current(*e,current,reader))return Reject(FinalizationReason::Stale);
        if(!ValidateFinalFrameRuntime(proof,*e->output,b.provider,b.handoff,e->request.plan.header.record)||
           !SameFinalFrameBinding(proof.lease->Binding(),*e->binding))return Reject(FinalizationReason::InvalidRights);
        auto receipt=Receipt(*e,event,b.originalBypass?"OriginalBypassSealed":"FinalSealAccepted");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        C::FinalRealFramePacket packet;packet.header=receipt->record.header;packet.header.contract=C::ContractId::C13;
        packet.baseRealFrame=e->scope.base;packet.stream=e->scope.stream;packet.view=e->scope.view;
        packet.consumerBoundary=b.boundary;packet.output=b.output;
        packet.dependency={C::Symbol{},proof.dependency->header.record,proof.dependency->header.revision};packet.dependency.recordType.Assign(C::DependencyProof::WireName);
        packet.handoffContract=b.handoffContract;packet.handoffVersion=b.version;packet.generations=current;packet.masks=e->request.recipe.masks;
        packet.original=C::OptionalFact<bool>::FromKnown(b.originalBypass,event.event.evidence);
        if(b.originalBypass)packet.bypass=C::OptionalFact<C::ReasonId>::FromKnown(b.bypassReason,event.event.evidence);
        C::BoundedList<C::RetentionRegistration,16> retentions;
        if(!CaptureRetentions(proof,b,event.event.evidence,retentions)||!SameRetentions(e->retentions,retentions))return Reject(FinalizationReason::RetentionUnknown);
        packet.retentions.count=static_cast<std::uint32_t>(retentions.Size());
        packet.retentions.backing=C::MetadataRef<C::BoundedList<C::RetentionRegistration,16>>{C::OwnerDomain::Finalizer,packet.header.record,1,{}};
        packet.upstreamReceipts=receipt->record.causalRecords;
        // Validate the complete packet field-by-field, excluding only the unissued seal.
        const bool valid=std::apply([&](const auto&... field){return ([&]{
            if constexpr(std::is_same_v<std::remove_cvref_t<decltype(packet.*(field.pointer))>,C::FinalSealId>)return true;
            else return Context::ValidValues(packet.*(field.pointer));}()&&...);},C::FinalRealFramePacket::Fields());
        if(!valid||packet.Check()!=C::Error::None)return Reject(FinalizationReason::InvalidBoundary);
        static_assert(std::is_nothrow_copy_assignable_v<C::FinalRealFramePacket>);
        static_assert(std::is_nothrow_copy_constructible_v<C::FinalRealFramePacket>);
        static_assert(std::is_nothrow_move_assignable_v<FinalizerReceipt>);
        // No fallible work after canonical issuance: reserved entry and value-only copies.
        const auto issued=issuer_.FinalSeal(event.event);
        if(issued.status!=IdentityStatus::Ok||!issued.value.IsKnown())return Reject(FinalizationReason::IssuerRefused);
        packet.seal=issued.value.KnownPart()->value;e->retentions=retentions;e->upstream=receipt->causal;
        e->packet=packet;e->phase=b.originalBypass?FinalizationPhase::SealedBypass:FinalizationPhase::SealedNr;
        e->sealReceipt=*receipt;return Accept(*e,handle,std::move(*receipt));
    }
    FinalizationResult RecordPresentDisposition(const C::FinalSealId& seal,const C::RecordHeader& owner,const C::OptionalFact<bool>& accepted,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(seal);
        if(!e||!Context::ValidValues(owner)||owner.owner!=C::OwnerDomain::Presentation||!Context::ValidValues(accepted))return Reject(FinalizationReason::InvalidOwner);
        auto receipt=Receipt(*e,event,"PresentDispositionObserved");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        C::Symbol type;type.Assign(C::OwnerReceipt::WireName);receipt->causal.Push({owner.contract,type,owner.record,owner.revision});
        receipt->record.causalRecords.count=static_cast<std::uint32_t>(receipt->causal.Size());
        C::OwnerFact disposition;disposition.field.Assign("PresentAccepted");
        if(const auto* known=accepted.KnownPart())disposition.value=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{known->value},known->evidence);
        else disposition.value=C::OptionalFact<C::ScalarValue>::FromUnknown(*accepted.UnknownPart());
        receipt->facts.Push(disposition);receipt->record.facts.count=static_cast<std::uint32_t>(receipt->facts.Size());
        return Accept(*e,{this,static_cast<std::size_t>(e-entries_.get()),e->revision},std::move(*receipt));
    }
    std::optional<FinalPacketSnapshot> Snapshot(const C::FinalSealId& seal)
    {
        Guard guard(entered_);if(!guard.entered)return {};auto* e=Find(seal);if(!e)return {};
        return FinalPacketSnapshot{*e->packet,e->retentions,e->upstream};
    }
    std::optional<FinalPacketSnapshot> Snapshot(const PrimaryNrClaimHandle& claim)
    {
        Guard guard(entered_);if(!guard.entered)return {};auto* e=Find(claim);if(!e||!e->packet)return {};
        return FinalPacketSnapshot{*e->packet,e->retentions,e->upstream};
    }
    FinalizationResult OfferToFg(const PrimaryNrClaimHandle& handle,const FinalFrameRuntimeProof& proof,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e||!e->packet||!e->boundary||!e->binding||!e->output)return Reject(FinalizationReason::InvalidState);
        if(!ValidateFinalFrameRuntime(proof,*e->output,e->boundary->provider,e->scope.handoff,e->request.plan.header.record)||
           !SameFinalFrameBinding(proof.lease->Binding(),*e->binding))return Reject(FinalizationReason::InvalidRights);
        std::size_t matched=0;
        for(const auto& hold:proof.facts->holds)if(FinalFrameProviderHold(hold,*e->binding,e->boundary->provider,e->scope.handoff))
        {
            std::size_t registrations=0;
            for(const auto& sealed:e->retentions)
                if(sealed.consumer==hold.consumer&&Context::Established(sealed.registration)&&sealed.registration.KnownPart()->value==hold.registration)++registrations;
            if(registrations!=1)return Reject(FinalizationReason::InvalidRights);++matched;
        }
        if(matched!=e->retentions.Size())return Reject(FinalizationReason::InvalidRights);
        if(e->handoff!=HandoffPhase::NotOffered)return Existing(*e,handle);
        auto receipt=Receipt(*e,event,"OfferedToFG");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->handoff=HandoffPhase::OfferedToFG;return Accept(*e,handle,std::move(*receipt));
    }
    template<class Reader> FinalizationResult ObserveFgReceipt(const C::FinalSealId& seal,const C::FgHandoffReceipt& observation,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(seal);
        if(!e||!e->boundary||e->handoff==HandoffPhase::NotOffered||!Context::ValidValues(observation)||
           observation.packet.recordType.View()!=C::FinalRealFramePacket::WireName||observation.packet.record!=e->packet->header.record||
           observation.packet.revision!=e->packet->header.revision||observation.provider!=e->boundary->provider)return Reject(FinalizationReason::InvalidAcknowledgment);
        const auto handle=PrimaryNrClaimHandle{this,static_cast<std::size_t>(e-entries_.get()),e->revision};
        if(e->consume&&*e->consume==observation)return Existing(*e,handle);
        if(e->release&&*e->release==observation)return Existing(*e,handle);
        if(e->release)return Reject(FinalizationReason::InvalidAcknowledgment); // Exact release is terminal.
        if(e->consumptionRejected&&FinalizerTrue(observation.accepted))return Reject(FinalizationReason::InvalidAcknowledgment);
        const auto* prior=e->release?&*e->release:e->consume?&*e->consume:nullptr;
        if(prior&&(observation.header.record!=prior->header.record||observation.header.revision<=prior->header.revision||
           (FinalizerTrue(prior->accepted)&&!FinalizerTrue(observation.accepted))))return Reject(FinalizationReason::InvalidAcknowledgment);
        const C::BoundedList<C::RetentionRegistration,16>* registrations=nullptr;
        if(!Context::ResolveList(observation.retentions,reader,registrations)||!registrations||registrations->Size()!=e->retentions.Size())return Reject(FinalizationReason::InvalidAcknowledgment);
        for(const auto& expected:e->retentions)
        {
            std::size_t matches=0;
            for(const auto& actual:*registrations)
                if(actual.consumer==expected.consumer&&Context::SameFact(actual.registration,expected.registration))
                {++matches;if(FinalizerTrue(observation.providerReleased)&&!FinalizerTrue(actual.released))return Reject(FinalizationReason::InvalidAcknowledgment);}
            if(matches!=1)return Reject(FinalizationReason::InvalidAcknowledgment);
        }
        auto receipt=Receipt(*e,event,"FgHandoffObserved");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        receipt->causal.Push(FinalizerReference(observation));receipt->record.causalRecords.count=static_cast<std::uint32_t>(receipt->causal.Size());
        e->consume=observation;
        if(FinalizerTrue(observation.providerReleased)){e->release=observation;e->handoff=HandoffPhase::Released;}
        else if(!Context::Established(observation.providerReleased))e->handoff=HandoffPhase::RetentionUnknown;
        else if(FinalizerTrue(observation.accepted))e->handoff=HandoffPhase::ProviderHeld;
        else e->handoff=HandoffPhase::RejectedByFG;
        return Accept(*e,handle,std::move(*receipt));
    }
    // A real consumer dispatch failure closes the logical history obligation,
    // while the sealed packet and every physical/provider hold remain retained.
    FinalizationResult RejectFgConsumption(const C::FinalSealId& seal,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(seal);
        if(!e||!e->consume||!Context::Established(e->consume->accepted)||FinalizerTrue(e->consume->accepted))
            return Reject(FinalizationReason::InvalidAcknowledgment);
        const auto handle=PrimaryNrClaimHandle{this,static_cast<std::size_t>(e-entries_.get()),e->revision};
        if(e->consumptionRejected)return Existing(*e,handle);
        auto receipt=Receipt(*e,event,"FgConsumptionRejected");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->consumptionRejected=true;return Accept(*e,handle,std::move(*receipt));
    }
    FinalizationResult ObserveResourceRetirement(const C::FinalSealId& seal,const LiveConsumptionLease& lease,const OwnerFacts& facts,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(seal);
        if(!e||!e->binding||e->handoff!=HandoffPhase::Released||!SameFinalFrameBinding(lease.Binding(),*e->binding)||
           !CanOwnerReuse(lease,facts).allowed)return Reject(FinalizationReason::InvalidRights);
        auto receipt=Receipt(*e,event,"ResourceOwnerRetirementObserved");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->resourceRetired=true;
        // Preserve the exact scope/seal tombstone for this session; no time/age eviction.
        return Accept(*e,{this,static_cast<std::size_t>(e-entries_.get()),e->revision},std::move(*receipt));
    }
    bool StopAdmissions()
    {Guard guard(entered_);if(!guard.entered)return false;stopped_=true;return true;}
    // Failure is immediate and irreversible, even while physical work is still
    // outstanding. This method issues no seal and releases no storage or slot.
    FinalizationResult InterruptUnsealed(const PrimaryNrClaimHandle& handle,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->packet)return Reject(FinalizationReason::InvalidState);
        if(e->phase==FinalizationPhase::InterruptedNoSeal)return Existing(*e,handle);
        auto receipt=Receipt(*e,event,"PrimaryInterruptedWithoutSeal");if(!receipt)return Reject(FinalizationReason::InvalidOwner);
        e->committed=true;e->phase=FinalizationPhase::InterruptedNoSeal;
        return Accept(*e,handle,std::move(*receipt));
    }
    template<class Reader> FinalizationResult RetireInterrupted(const PrimaryNrClaimHandle& handle,
        const NativeUnsealedRetirement& terminal,const C::RecordHeader& registry,const C::FrameIdentity& through,
        const C::OptionalFact<bool>& noFuturePrimaryThrough,const Reader& reader,const FinalizerEvent& event,
        const FinalFrameRuntimeProof* boundaryTerminal=nullptr)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(handle);
        if(!e)return Reject(FinalizationReason::InvalidHandle);
        if(e->packet||e->phase!=FinalizationPhase::InterruptedNoSeal||terminal.claim_!=handle||
           terminal.recipe_!=e->request.recipe||terminal.recording_.Check()!=C::Error::None)
            return Reject(FinalizationReason::InvalidRights);
        const auto* frame=Context::ResolveMetadata(e->request.recipe.frame,reader);
        const auto* sample=frame&&frame->nativeSample?Context::ResolveMetadata(*frame->nativeSample,reader):nullptr;
        if(!frame||!ScopeFrameMatches(e->scope,*frame)||!sample||sample->Check()!=C::Error::None||
           *sample!=terminal.sample_)return Reject(FinalizationReason::InvalidRights);
        // Preparing a seal may already have registered a downstream consumer.
        // The Native invocation's terminal receipt cannot close those holds.
        if(e->binding)
        {
            if(!boundaryTerminal||!boundaryTerminal->lease||!boundaryTerminal->facts||!e->boundary||
               !SameFinalFrameBinding(boundaryTerminal->lease->Binding(),*e->binding)||
               !CanOwnerReuse(*boundaryTerminal->lease,*boundaryTerminal->facts).allowed)
                return Reject(FinalizationReason::InvalidRights);
            for(const auto& expected:e->retentions)
            {
                std::size_t matches=0;
                for(const auto& actual:boundaryTerminal->facts->holds)
                    if(Context::Established(expected.registration)&&actual.registration==expected.registration.KnownPart()->value&&
                       actual.consumer==expected.consumer&&actual.kind==HoldKind::Provider&&
                       actual.provider==e->boundary->provider&&actual.handoff==C::GenerationToken{e->boundary->handoff.Describe()}&&
                       actual.released==Fact::Yes&&actual.releaseEvidence)++matches;
                if(matches!=1)return Reject(FinalizationReason::InvalidRights);
            }
        }
        // Authentic terminal receipt is sticky even when exclusion is Busy or
        // another earlier scope has not drained. It never enables another claim.
        e->resourceRetired=true;
        return RetireExcluded(*e,registry,through,noFuturePrimaryThrough,event);
    }
    // Called only by the authenticated IdentityRegistry owner. Its explicit proof
    // excludes future primary admissions through this canonical base ordinal for
    // this exact stream/view/handoff namespace. Never infer this from frame age.
    FinalizationResult RetireScope(const C::FinalSealId& seal,const C::RecordHeader& registry,const C::FrameIdentity& through,
        const C::OptionalFact<bool>& noFuturePrimaryThrough,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return Reject(FinalizationReason::Busy);auto* e=Find(seal);
        if(!e||!e->resourceRetired||e->handoff!=HandoffPhase::Released)return Reject(FinalizationReason::InvalidRights);
        return RetireExcluded(*e,registry,through,noFuturePrimaryThrough,event);
    }
    std::optional<FinalizerDrainStatus> DrainStatus()
    {
        Guard guard(entered_);if(!guard.entered)return {};FinalizerDrainStatus status;status.admissionsStopped=stopped_;
        bool outstanding=false;
        for(std::size_t i=0;i<capacity_;++i)
        {
            const auto& e=entries_[i];if(!e.used||e.releasedClaim)continue;
            if(e.packet)++status.sealed;else ++status.unsealed;
            if(e.handoff==HandoffPhase::RetentionUnknown)++status.retentionUnknown;
            if(e.packet&&e.handoff!=HandoffPhase::Released)++status.providerHeld;
            // An interrupted claim still belongs to its invocation until this
            // finalizer acknowledges its own exclusion retirement. Physical
            // terminality alone must not report aggregate session drain.
            outstanding=outstanding||!e.resourceRetired||!e.packet||e.handoff!=HandoffPhase::Released;
        }
        // Source-bound reservations have no Strict packet. Only their own exact
        // terminal receipt closes this tail; tombstones stay in bounded storage.
        for(std::size_t i=0;i<capacity_;++i)if(sourceBound_[i].used&&!sourceBound_[i].terminal){++status.unsealed;outstanding=true;}
        status.tailsRetired=!outstanding;status.safelyDrained=stopped_&&status.tailsRetired;return status;
    }
  private:
    static SourceBoundResult SourceReject(FinalizationReason reason)
    {SourceBoundResult r;r.reason=reason;return r;}
    SourceBoundFinalizerEntry* FindSource(const SourceBoundClaimHandle& claim)
    {
        if(claim.owner_!=this||claim.slot_>=capacity_)return nullptr;
        auto& e=sourceBound_[claim.slot_];return e.used&&e.revision==claim.revision_?&e:nullptr;
    }
    SourceBoundResult SourceResult(const SourceBoundFinalizerEntry& e,const SourceBoundClaimHandle& claim)const
    {
        SourceBoundResult r;r.accepted=true;r.reason=FinalizationReason::None;r.phase=e.phase;r.claim=claim;r.admissionsClosed=e.closed;
        r.terminal=e.terminal;
        if(e.dispatch&&e.submission)
            r.logical=SourceBoundLogicalAcknowledgment(this,claim,e.request.recipe.evaluation,
                e.phase==SourceBoundPhase::DispatchReturned||(e.terminal&&e.terminal->consumed));
        if(e.phase==SourceBoundPhase::ConsumerAuthorized&&e.authorizationEvent)
            r.authorization=SourceBoundConsumerAuthorization(claim,e.authorizationEvent);
        return r;
    }
    template<class Reader>bool SourceCurrent(const SourceBoundFinalizerEntry& e,const C::MetadataRef<C::GenerationVector>& ref,const Reader& reader)const
    {const auto* g=Context::ResolveMetadata(ref,reader);return g&&*g==e.generations;}
    template<class Reader>static bool SourceQualification(const C::MetadataRef<C::QualificationCertificate>& ref,
        C::OwnerDomain owner,std::string_view purpose,const SourceBoundPrimaryRequest& request,const C::FrameIdentity& frame,const Reader& reader)
    {
        const auto* q=Context::ResolveMetadata(ref,reader);
        if(ref.owner!=owner||!q||!Context::ValidValues(*q)||q->header.owner!=owner||
           q->purpose.View()!=purpose||q->context!=request.recipe.context||q->profile!=request.recipe.profile||
           q->eligibility!=C::Eligibility::Eligible||q->missingFields.count||q->contradictions.count||
           !Context::ResolveMetadata(q->signature,reader))return false;
        C::EvidenceVector freshness;freshness.nativeFreshness=q->nativeFreshness;freshness.ageInRealFrames=q->maximumAge;
        return Context::NativeEvidenceFresh(freshness,frame,reader);
    }
    // Only the existing coordinator may enter this lane after its normal Native
    // preparation. The new contract is reserved BEFORE any NR work. A serialized
    // transaction observation, C04 descriptor or FSR token cannot call this port.
    template<class Reader>SourceBoundResult ReserveSourceBoundPrimary(const SourceBoundPrimaryRequest& request,
        const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        if(stopped_)return SourceReject(FinalizationReason::Stopped);
        const auto* transaction=Context::ResolveMetadata(request.transaction,reader);
        if(request.transaction.owner!=C::OwnerDomain::IdentityRegistry||!transaction||!Context::ValidValues(*transaction)||
           transaction->sample.session!=session_)return SourceReject(FinalizationReason::UnknownScope);
        // Compare the immutable subject before route, context and plan checks.
        // Positively linked repeats can never acquire another primary opportunity.
        for(std::size_t i=0;i<capacity_;++i)if(sourceBound_[i].used&&
            (sourceBound_[i].transaction.id==transaction->id||sourceBound_[i].transaction.sample.SameSample(transaction->sample)))
        {
            const auto& e=sourceBound_[i];
            if(e.closed)return SourceReject(FinalizationReason::RetiredScope);
            return e.request==request&&e.transaction==*transaction?SourceResult(e,{this,i,e.revision}):
                SourceReject(FinalizationReason::CompetingPrimary);
        }
        const auto* frame=Context::ResolveMetadata(request.recipe.frame,reader);
        const auto* sample=frame?Context::ResolveNativeSample(*frame,reader):nullptr;
        const auto* generations=Context::ResolveMetadata(request.generations,reader);
        const auto* contract=Context::ResolveMetadata(request.contract,reader);
        const auto* delivery=request.recipe.nativeDelivery?Context::ResolveMetadata(*request.recipe.nativeDelivery,reader):nullptr;
        if(!frame||!sample||*sample!=transaction->sample||frame->nativeSample!=request.recipe.nativeSample||
           !Context::Established(frame->episodeId)||frame->episodeId.KnownPart()->value!=transaction->episode||
           (frame->evaluationId.IsKnown()&&(!Context::Established(frame->evaluationId)||
               frame->evaluationId.KnownPart()->value!=request.recipe.evaluation))||
           (!frame->evaluationId.IsKnown()&&!request.evaluationAssociation)||
           (request.evaluationAssociation&&!request.evaluationAssociation->Matches(*transaction,request.recipe.evaluation))||
           !Context::Established(transaction->device)||!generations)return SourceReject(FinalizationReason::UnknownScope);
        if(frame->generatedFrameId.IsKnown()||request.knownDerivedFrom)return SourceReject(FinalizationReason::NonReal);
        if(!Context::ValidValues(request.recipe)||request.recipe.placement!=C::Placement::NativeAfter||
           !Context::ValidValues(request.plan)||!FinalizerTrue(request.plan.committed)||
           !Context::Established(request.plan.routeGeneration)||request.recipe.committedPlan.recordType.View()!=C::PlanCommit::WireName||
           request.recipe.committedPlan.record!=request.plan.header.record||request.recipe.committedPlan.revision!=request.plan.header.revision||
           !Context::ResolveMetadata(request.recipe.history,reader)||!Context::ResolveMetadata(request.recipe.masks,reader))
            return SourceReject(FinalizationReason::InvalidPlan);
        if(request.contract.owner!=C::OwnerDomain::FrameGeneration||!contract||!Context::ValidValues(*contract)||
           contract->profile!=request.recipe.profile||!delivery||!Context::ValidValues(*delivery)||
           delivery->kind!=C::NativeDeliveryKind::FinalConsumerRequired||!delivery->finalConsumer||
           *delivery->finalConsumer!=contract->consumer||delivery->profile!=request.recipe.profile||delivery->placement!=request.recipe.placement||
           !SourceQualification(contract->profileQualification,C::OwnerDomain::Strategy,"RENDER.NativeBindings",request,*frame,reader)||
           !SourceQualification(contract->consumerQualification,C::OwnerDomain::FrameGeneration,C::SourceBoundConsumerContractV1::WireName,request,*frame,reader))
            return SourceReject(FinalizationReason::InvalidBoundary);
        const auto* consumer=Context::ResolveMetadata(contract->consumer,reader);
        if(!consumer||!Context::ValidValues(*consumer))return SourceReject(FinalizationReason::InvalidBoundary);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        for(std::size_t i=0;i<capacity_;++i)if(!sourceBound_[i].used)
        {
            SourceBoundFinalizerEntry prepared;prepared.used=true;prepared.request=request;prepared.transaction=*transaction;
            prepared.consumer=*consumer;prepared.generations=*generations;prepared.revision=sourceBound_[i].revision+1;
            if(!prepared.revision)return SourceReject(FinalizationReason::Capacity);
            sourceBound_[i]=std::move(prepared);lastEvent_=event.event.evidence.record.value;
            auto result=SourceResult(sourceBound_[i],{this,i,sourceBound_[i].revision});result.newPrimaryClaim=true;return result;
        }
        return SourceReject(FinalizationReason::Capacity);
    }
    // Exact C07 plus the actual retained Provider return publication. Neither a
    // model success nor the unchanged target pointer is an output-write receipt.
    template<class Reader>SourceBoundResult SubmitSourceBoundCandidate(const SourceBoundClaimHandle& claim,
        const FinalCandidateSubmission& candidate,const C::NativeStageDeliveryV1& stage,const Reader& reader,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        // Closure stops new consumer authority, not publication of effects from
        // a primary that this finalizer already admitted before closure.
        if(e->phase==SourceBoundPhase::FailedRetained)return SourceReject(FinalizationReason::InvalidState);
        if(e->candidate)return *e->candidate==candidate&&e->stage&&*e->stage==stage?SourceResult(*e,claim):SourceReject(FinalizationReason::CompetingPrimary);
        const auto& recipe=e->request.recipe;const auto& result=candidate.result;
        if(!Context::ValidValues(result)||result.evaluation!=recipe.evaluation||result.originalLineage!=recipe.frame||
           !SourceCurrent(*e,candidate.generations,reader)||!Context::ValidValues(stage)||
           stage.delivery!=*recipe.nativeDelivery||stage.sample!=*recipe.nativeSample||stage.plan!=recipe.committedPlan||
           stage.route!=e->request.plan.routeGeneration.KnownPart()->value||stage.evaluation!=recipe.evaluation||
           stage.recipe.recordType.View()!=C::NrExecutionRecipe::WireName||stage.recipe.record!=recipe.header.record||stage.recipe.revision!=recipe.header.revision||
           stage.placement!=recipe.placement||stage.executionResult.recordType.View()!=C::EvaluationResult::WireName||
           stage.executionResult.record!=result.header.record||stage.executionResult.revision!=result.header.revision||
           stage.caller!=e->transaction.sample.ingress)return SourceReject(FinalizationReason::InvalidCandidate);
        const C::MetadataRef<C::NativeStageDeliveryV1> ref{stage.publication.owner,stage.publication.record,stage.publication.revision,{}};
        const auto* published=Context::ResolveMetadata(ref,reader);
        if(!published||*published!=stage)return SourceReject(FinalizationReason::MissingLineage);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        if(stage.outcome!=C::NativeStageOutcome::HostReturnRecorded||result.stage!=C::OutcomeStage::Produced)
        {
            e->candidate=candidate;e->stage=stage;e->closed=true;e->phase=SourceBoundPhase::FailedRetained;
            lastEvent_=event.event.evidence.record.value;
            auto rejected=SourceResult(*e,claim);rejected.accepted=false;rejected.reason=FinalizationReason::InvalidCandidate;return rejected;
        }
        const auto* native=stage.nativeOutput?Context::ResolveMetadata(*stage.nativeOutput,reader):nullptr;
        const auto* returned=stage.returnedOutput?Context::ResolveMetadata(*stage.returnedOutput,reader):nullptr;
        const auto* nativeView=native?Context::ResolveNativeOutput(*native,reader):nullptr;
        const auto* output=returned?Context::ResolveNativeOutput(*returned,reader):nullptr;
        const auto* produced=Context::ResolveOptional(result.output,reader);
        const C::BoundedList<C::OwnerValueReference,16>* causes=nullptr;
        if(!FinalizerTrue(candidate.acceptedFinalCandidate)||!Context::Established(result.composition)||
           result.composition.KnownPart()->value!=C::CompositionStatus::Accepted||!nativeView||!output||!produced||*nativeView!=*produced||
           !Context::SameNativeOutputContent(*native,*returned,reader)||stage.lastRecording!=returned->recording||
           stage.lastRecordedOrdinal<returned->producerOrdinal||!Context::SameFact(output->device,e->transaction.device)||
           !Context::ResolveList(stage.causes,reader,causes)||!causes||!causes->Size())return SourceReject(FinalizationReason::MissingLineage);
        e->candidate=candidate;e->stage=stage;e->output=*output;e->phase=SourceBoundPhase::CandidateBound;
        lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    template<class Reader>SourceBoundResult AuthorizeSourceBoundConsumer(const SourceBoundClaimHandle& claim,
        const FinalBoundaryObservation& b,const FinalFrameRuntimeProof& proof,const Reader& reader,const FinalizerEvent& event,
        const FinalInputAncestry* ancestry=nullptr)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(stopped_||e->closed||e->phase==SourceBoundPhase::FailedRetained||!e->candidate||!e->stage||!e->output)
            return SourceReject(FinalizationReason::InvalidState);
        if(e->boundary&&*e->boundary!=b)return SourceReject(FinalizationReason::InvalidBoundary);
        const auto* frame=Context::ResolveMetadata(b.frame,reader);
        const auto* original=Context::ResolveMetadata(e->request.recipe.frame,reader);
        const auto* output=Context::ResolveMetadata(b.output,reader);const auto& expected=e->consumer;
        if(!frame||!original||*frame!=*original||!output||!SourceCurrent(*e,b.generations,reader)||
           (ancestry?(ancestry->Source()!=*e->output||ancestry->Endpoint()!=*output):*output!=*e->output))
            return SourceReject(FinalizationReason::MissingLineage);
        if(!Context::ValidValues(b.owner)||b.owner.owner!=C::OwnerDomain::FrameGeneration||!Context::ValidValues(b.boundary)||
           !FinalizerTrue(b.boundary.afterRequiredHostRendering)||!Context::Established(b.boundary.kind)||
           b.boundary.kind.KnownPart()->value!=expected.boundary||!Context::Established(b.boundary.hudIncluded)||
           b.boundary.hudIncluded.KnownPart()->value!=expected.hudIncluded||b.requiredHudIncluded!=expected.hudIncluded||
           !Context::Established(b.boundary.toneMapped)||b.boundary.toneMapped.KnownPart()->value!=expected.toneMapped||
           (b.requireToneMapped&&!expected.toneMapped)||!Context::ValidValues(b.color)||!Context::Established(b.color.domain)||
           b.color.domain.KnownPart()->value!=expected.color||b.provider!=expected.provider||b.handoff!=expected.handoff||
           b.handoffContract!=expected.contract||b.version!=expected.contractVersion||b.originalBypass||
           !FinalizerTrue(b.lineageIncludesCandidate)||b.candidate!=e->stage->executionResult||
           !Context::Established(output->descriptor.allocation)||output->descriptor.allocation.KnownPart()->value!=expected.extent)
            return SourceReject(FinalizationReason::InvalidBoundary);
        if(!proof.consumerRecording||proof.consumerRecording->Check()!=C::Error::None||
           !ValidateFinalFrameRuntime(proof,*output,b.provider,b.handoff,e->request.plan.header.record)||
           !Context::Established(proof.dependency->recording)||proof.dependency->recording.KnownPart()->value!=(ancestry?ancestry->Recording():e->stage->lastRecording)||
           (e->binding&&!SameFinalFrameBinding(*e->binding,proof.lease->Binding())))return SourceReject(FinalizationReason::InvalidRights);
        C::BoundedList<C::RetentionRegistration,16> retentions;
        if(!CaptureRetentions(proof,b,event.event.evidence,retentions)||
           (e->binding&&!SameRetentions(e->retentions,retentions)))return SourceReject(FinalizationReason::RetentionUnknown);
        if(e->authorizationEvent)return SourceResult(*e,claim);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        // Admission closes at the same owner linearization as authorization.
        // The already admitted consumer may drain; no later selection or claim
        // can enter while this capability or its physical work survives.
        e->closed=true;e->boundary=b;e->binding=proof.lease->Binding();e->retentions=retentions;
        e->consumerRecording=*proof.consumerRecording;
        if(ancestry)e->ancestry=*ancestry;
        e->authorizationEvent=event.event.evidence.record.value;
        e->phase=SourceBoundPhase::ConsumerAuthorized;lastEvent_=e->authorizationEvent;return SourceResult(*e,claim);
    }
    bool SourceBoundAuthorizationCurrent(const SourceBoundConsumerAuthorization& authorization,const FinalFrameRuntimeProof& proof)
    {
        Guard guard(entered_);if(!guard.entered)return false;const auto* e=FindSource(authorization.claim_);
        // Closing new admission does not revoke an already admitted consumer.
        // Failure does. Both cases retain every independent physical hold.
        if(!(e&&e->phase==SourceBoundPhase::ConsumerAuthorized&&e->authorizationEvent==authorization.event_&&
            e->output&&e->boundary&&e->binding&&proof.consumerRecording&&*proof.consumerRecording==e->consumerRecording&&
            ValidateFinalFrameRuntime(proof,e->ancestry?e->ancestry->Endpoint():*e->output,e->boundary->provider,e->boundary->handoff,e->request.plan.header.record)&&
            Context::Established(proof.dependency->recording)&&proof.dependency->recording.KnownPart()->value==(e->ancestry?e->ancestry->Recording():e->stage->lastRecording)&&
            SameFinalFrameBinding(*e->binding,proof.lease->Binding())))return false;
        C::BoundedList<C::RetentionRegistration,16> retentions;
        return CaptureRetentions(proof,*e->boundary,C::EvidenceRef{e->request.plan.header.record},retentions)&&
            SameRetentions(e->retentions,retentions);
    }
    template<class Reader>SourceBoundResult AuthorizeSourceBoundConsumerWithAncestryV1(const SourceBoundClaimHandle& claim,
        const FinalBoundaryObservation& boundary,const FinalInputAncestry& ancestry,const FinalFrameRuntimeProof& proof,
        const Reader& reader,const FinalizerEvent& event)
    {return AuthorizeSourceBoundConsumer(claim,boundary,proof,reader,event,&ancestry);}
    SourceBoundResult ObserveSourceBoundDispatch(const SourceBoundClaimHandle& claim,
        const SourceBoundDispatchOutcome& outcome,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->dispatch)return *e->dispatch==outcome?SourceResult(*e,claim):SourceReject(FinalizationReason::InvalidState);
        const bool returned=outcome.effect==SourceBoundDispatchEffect::ReturnedAccepted||outcome.effect==SourceBoundDispatchEffect::ReturnedRejected;
        if(!e->authorizationEvent||!e->binding||!e->boundary||outcome.call.Check()!=C::Error::None||outcome.recording.Check()!=C::Error::None||
           returned!=outcome.actualResult.has_value()||
           outcome.recording!=e->consumerRecording||
           !Context::Established(e->binding->description.queue)||
           outcome.queue!=e->binding->description.queue.KnownPart()->value||
           !ValidEvent(event))return SourceReject(FinalizationReason::InvalidRights);
        e->dispatch=outcome;e->closed=true;
        // Missing/exceptional return preserves possible effects. It cannot be
        // rewritten as a rejected/no-effect call by a destructor or retry.
        e->phase=outcome.effect==SourceBoundDispatchEffect::ReturnedAccepted&&e->phase!=SourceBoundPhase::FailedRetained?
            SourceBoundPhase::DispatchReturned:SourceBoundPhase::FailedRetained;
        lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    SourceBoundResult ObserveSourceBoundSubmission(const SourceBoundClaimHandle& claim,
        const SourceBoundSubmissionOutcome& outcome,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->submission)return *e->submission==outcome?SourceResult(*e,claim):SourceReject(FinalizationReason::InvalidState);
        if(!e->dispatch||outcome.submission.Check()!=C::Error::None||outcome.call!=e->dispatch->call||
           outcome.recording!=e->dispatch->recording||outcome.queue!=e->dispatch->queue||
           !ValidEvent(event))return SourceReject(FinalizationReason::InvalidRights);
        e->submission=outcome;
        if(outcome.executeInvoked!=Fact::Yes||outcome.signalSucceeded!=Fact::Yes||outcome.dependencyWaitsSucceeded!=Fact::Yes)
            e->phase=SourceBoundPhase::FailedRetained;
        lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    SourceBoundResult ObserveSourceBoundProviderRelease(const SourceBoundClaimHandle& claim,
        const LiveConsumptionLease& lease,const OwnerFacts& facts,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->providerReleased)return SourceResult(*e,claim);
        if(!e->dispatch||!e->binding||!e->boundary||!SameFinalFrameBinding(*e->binding,lease.Binding())||
           !IsYes(facts.externalRegistrationsComplete))return SourceReject(FinalizationReason::RetentionUnknown);
        for(const auto& expected:e->retentions)
        {
            std::size_t matches=0;
            for(const auto& hold:facts.holds)
                if(Context::Established(expected.registration)&&hold.registration==expected.registration.KnownPart()->value&&
                   hold.consumer==expected.consumer&&hold.provider==e->boundary->provider&&
                   hold.handoff==C::GenerationToken{e->boundary->handoff.Describe()}&&hold.kind==HoldKind::Provider&&
                   hold.target==e->binding->description.resource&&hold.released==Fact::Yes&&hold.releaseEvidence)++matches;
            if(matches!=1)return SourceReject(FinalizationReason::RetentionUnknown);
        }
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        e->providerReleased=true;lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    SourceBoundResult ObserveSourceBoundResourceRetirement(const SourceBoundClaimHandle& claim,
        const LiveConsumptionLease& lease,const OwnerFacts& facts,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->resourceRetired)return SourceResult(*e,claim);
        if(!e->dispatch||!e->submission||!e->providerReleased||!e->binding||
           !SameFinalFrameBinding(*e->binding,lease.Binding())||!CanOwnerReuse(lease,facts).allowed)
            return SourceReject(FinalizationReason::InvalidRights);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        e->resourceRetired=true;lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    bool AcknowledgeSourceBoundLogicalClosed(const SourceBoundClaimHandle& claim,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return false;
        auto* e=FindSource(claim);if(!e||!e->dispatch||!e->submission)return false;
        if(e->logicalClosed)return true;
        if(!ValidEvent(event))return false;
        e->logicalClosed=true;lastEvent_=event.event.evidence.record.value;return true;
    }
    SourceBoundResult RetireSourceBound(const SourceBoundClaimHandle& claim,
        const C::RecordHeader& registry,const SourceTransactionExclusion& exclusion,
        const C::OptionalFact<bool>& excluded,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        const auto& transaction=exclusion.Transaction();
        if(e->terminal)return transaction==e->transaction?SourceResult(*e,claim):SourceReject(FinalizationReason::UnknownScope);
        if(!e->logicalClosed||!e->resourceRetired||!e->dispatch||!e->submission||!e->output||
           transaction!=e->transaction||!Context::ValidValues(registry)||registry.contract!=C::ContractId::C11||
           registry.owner!=C::OwnerDomain::IdentityRegistry||registry.scope!=e->request.recipe.header.scope||
           !FinalizerTrue(excluded)||excluded.KnownPart()->evidence.record!=registry.record)return SourceReject(FinalizationReason::InvalidRights);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        SourceBoundTerminalReceipt receipt;receipt.transaction=e->transaction.id;receipt.event=event.event.evidence.record;
        receipt.returnedOutput=*e->output;receipt.consumerOutput=e->ancestry?e->ancestry->Endpoint():*e->output;receipt.dispatch=*e->dispatch;receipt.submission=*e->submission;
        receipt.consumed=e->phase==SourceBoundPhase::DispatchReturned&&e->dispatch->effect==SourceBoundDispatchEffect::ReturnedAccepted&&
            e->submission->executeInvoked==Fact::Yes&&e->submission->signalSucceeded==Fact::Yes&&e->submission->dependencyWaitsSucceeded==Fact::Yes;
        e->terminal=receipt;e->phase=SourceBoundPhase::Retired;e->closed=true;
        lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    SourceBoundResult CloseSourceBoundAdmission(const SourceBoundClaimHandle& claim,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->closed)return SourceResult(*e,claim);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        e->closed=true;lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
    SourceBoundResult FailSourceBound(const SourceBoundClaimHandle& claim,const FinalizerEvent& event)
    {
        Guard guard(entered_);if(!guard.entered)return SourceReject(FinalizationReason::Busy);
        auto* e=FindSource(claim);if(!e)return SourceReject(FinalizationReason::InvalidHandle);
        if(e->terminal)return SourceResult(*e,claim);
        if(e->phase==SourceBoundPhase::FailedRetained)return SourceResult(*e,claim);
        if(!ValidEvent(event))return SourceReject(FinalizationReason::InvalidOwner);
        e->closed=true;e->phase=SourceBoundPhase::FailedRetained;lastEvent_=event.event.evidence.record.value;return SourceResult(*e,claim);
    }
};
}
