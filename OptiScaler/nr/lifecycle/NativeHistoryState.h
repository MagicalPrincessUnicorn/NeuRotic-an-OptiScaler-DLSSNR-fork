#pragma once
#include "FinalRealFrameReset.h"
#include <nr/contracts/NativeStageDelivery.h>

namespace Neurotic::Lifecycle
{
// Persistent companion of one existing opaque model history. All calls occur
// under that model owner's lock. No pixels or replacement temporal cache live
// here. An unresolved attempt survives metadata/diagnostic publication failure.
class NativeHistoryState
{
    friend class NativeReturnTransaction;
    FinalizationResetState reset_;
    C::RecordKey owner_;
    C::OptionalFact<C::GenerationToken> lineage_;
    C::OptionalFact<C::GenerationToken> postAttempt_;
    bool returned_=false,modelSucceeded_=false;
    std::optional<C::NativeSampleIdentityV1> activeSample_;
    std::optional<C::EvaluationId> latest_,active_;
    void Unconsumed()noexcept
    {
        reset_.Observe(C::OutcomeStage::Failed,{},
            C::OptionalFact<bool>::FromKnown(false,C::EvidenceRef{owner_}),postAttempt_);
    }
    // The retained authenticated return transaction prepares all reader work
    // first. The final owner-serialized check and closure allocate nothing and
    // call no foreign code in the API return tail.
    bool CanCommitReturn(const C::EvaluationId& evaluation,const C::NativeSampleIdentityV1& sample)const noexcept
    {return active_&&activeSample_&&returned_&&modelSucceeded_&&*active_==evaluation&&*activeSample_==sample;}
    void CommitReturn()noexcept{active_.reset();activeSample_.reset();}
  public:
    NativeHistoryState(const C::RecordKey& owner,const C::RecordKey& history,
        const C::OptionalFact<C::GenerationToken>& lineage={}):reset_(owner,history),owner_(owner),lineage_(lineage)
    {
        reset_.Observe(C::OutcomeStage::NotAttempted,{}, {},lineage_);
    }
    NativeHistoryState(const NativeHistoryState&)=delete;
    NativeHistoryState& operator=(const NativeHistoryState&)=delete;
    bool Pending()const noexcept{return reset_.State().pending;}
    bool RequiresReset()const noexcept{return Pending()||active_.has_value();}
    bool InvocationClosed()const noexcept{return !active_.has_value();}
    bool Attempt(const C::EvaluationId& evaluation,bool resetRequested,
        const C::OptionalFact<C::GenerationToken>& currentLineage={},
        std::optional<C::NativeSampleIdentityV1> sample={})noexcept
    {
        if(evaluation.Check()!=C::Error::None||owner_.Check()!=C::Error::None||
           (sample&&sample->Check()!=C::Error::None)||
           (latest_&&(evaluation.nameSpace!=latest_->nameSpace||evaluation.issuer!=latest_->issuer||evaluation.value<=latest_->value))||
           (RequiresReset()&&!resetRequested))return false;
        if(active_)Unconsumed();
        // Refresh from the affected owner for THIS invocation. Cached reset
        // acknowledgments cannot describe a newer, unacknowledged reset.
        lineage_=currentLineage;
        reset_.Observe(C::OutcomeStage::NotAttempted,{}, {},lineage_);
        latest_=evaluation;active_=evaluation;activeSample_=std::move(sample);
        postAttempt_={};returned_=modelSucceeded_=false;return true;
    }
    // Fresh owner snapshot at this exact opaque-call return, not a later reset
    // acknowledgment. Unknown remains Unknown through delayed final rejection.
    bool ModelReturned(const C::EvaluationId& evaluation,const C::OptionalFact<C::GenerationToken>& lineage,
        bool succeeded=false)noexcept
    {
        if(!active_||*active_!=evaluation||returned_)return false;
        postAttempt_=lineage;returned_=true;modelSucceeded_=succeeded;return true;
    }
    void Abandon(const C::EvaluationId& evaluation)noexcept
    {if(active_&&*active_==evaluation){Unconsumed();active_.reset();activeSample_.reset();}}
    // Only the enrolled outer Native return owner supplies this typed event.
    // It follows a successful model return and proves actual caller-output
    // content through the current Resource/Provider metadata catalog.
#ifdef NR_NATIVE_RETURN_TRANSACTION_TESTING
    // Historical metadata fixture surface. Production closure is private to
    // NativeReturnTransaction; raw public stage values carry no authority.
    template<class Reader>bool HostReturned(const C::NativeStageDeliveryV1& stage,const Reader& reader)
    {
        if(!active_||!activeSample_||!returned_||!modelSucceeded_||*active_!=stage.evaluation||
           stage.outcome!=C::NativeStageOutcome::HostReturnRecorded||stage.Check()!=C::Error::None||
           !stage.nativeOutput||!stage.returnedOutput)return false;
        const auto* obligation=Context::ResolveMetadata(stage.delivery,reader);
        const auto* sample=Context::ResolveMetadata(stage.sample,reader);
        const auto* native=Context::ResolveMetadata(*stage.nativeOutput,reader);
        const auto* returned=Context::ResolveMetadata(*stage.returnedOutput,reader);
        const auto* nativeView=native?Context::ResolveMetadata(native->view,reader):nullptr;
        const auto* returnedView=returned?Context::ResolveMetadata(returned->view,reader):nullptr;
        if(!obligation||obligation->Check()!=C::Error::None||obligation->kind!=C::NativeDeliveryKind::HostReturn||
           obligation->placement!=stage.placement||!sample||!sample->SameSample(*activeSample_)||
           !native||native->Check()!=C::Error::None||!returned||returned->Check()!=C::Error::None||
           !nativeView||!returnedView||!Context::CompleteContent(*nativeView)||
           !Context::CompleteContent(*returnedView)||stage.lastRecording!=returned->recording||
           stage.lastRecordedOrdinal<returned->producerOrdinal)return false;
        active_.reset();activeSample_.reset();return true;
    }
#endif
    // Only the actual final consumer owner calls this, after matching the exact
    // candidate/evaluation. C07 Produced, composition, and model success cannot.
    bool Consumed(const C::EvaluationId& evaluation)noexcept
    {if(!active_||*active_!=evaluation)return false;active_.reset();activeSample_.reset();return true;}
    template<class Reader>bool Acknowledge(const C::ResetPlan& plan,const C::ResetAcknowledgment& ack,const Reader& reader)
    {
        // The affected History owner issues resultingLineage through its injected
        // HistoryIssuer after proving the reset effect. A requested reset or a
        // general opaque-evaluate success is never manufactured into this ack.
        if(!reset_.Acknowledge(plan,ack,reader))return false;
        lineage_=ack.resultingLineage;return true;
    }
    const FinalizationResetProjection& Projection()const noexcept{return reset_.State();}
};
}
