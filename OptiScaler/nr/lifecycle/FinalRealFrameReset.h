#pragma once
#include "FinalRealFrameTypes.h"
namespace Neurotic::Lifecycle
{
struct OpaqueHistoryProjection
{
    C::RecordKey owner,history;
    C::OutcomeStage invocation=C::OutcomeStage::NotAttempted;
    C::OptionalFact<C::HistoryAdvancement> advancement;
    C::OptionalFact<bool> consumed;
    bool priorPending=false;
    std::optional<C::ContractRef<C::ContractId::C08>> plan;
};
struct FinalizationResetProjection
{
    C::RecordKey owner,history;
    bool pending=false;
    C::Symbol reason;
    std::optional<C::ContractRef<C::ContractId::C08>> plan;
};
inline FinalizationResetProjection ProjectFinalizationReset(const OpaqueHistoryProjection& input)
{
    FinalizationResetProjection result;result.owner=input.owner;result.history=input.history;result.plan=input.plan;
    const bool attempted=input.invocation!=C::OutcomeStage::NotAttempted&&input.invocation!=C::OutcomeStage::Bypassed;
    const bool notAdvanced=Context::Established(input.advancement)&&input.advancement.KnownPart()->value==C::HistoryAdvancement::NotAdvanced;
    result.pending=input.priorPending||(attempted&&!notAdvanced&&!FinalizerTrue(input.consumed));
    result.reason.Assign(result.pending?"PostSubmitNoValidFinalConsume":"NoNewUnconsumedAdvancement");
    return result;
}
// Only the affected history owner supplies this acknowledgment; a flag or bypass
// is never sufficient. The caller retains pending state when this returns false.
template<class Reader> bool AcknowledgesFinalizationReset(const FinalizationResetProjection& pending,const C::ResetAcknowledgment& ack,const C::ResetPlan& plan,const Reader& reader)
{
    if(!pending.pending||!pending.plan||!Context::ValidValues(plan)||!Context::ValidValues(ack)||ack.header.owner!=C::OwnerDomain::History||
       pending.plan->recordType.View()!=C::ResetPlan::WireName||pending.plan->record!=plan.header.record||pending.plan->revision!=plan.header.revision||
       ack.plan!=*pending.plan||ack.affectedOwner!=pending.owner||!FinalizerTrue(ack.accepted)||!FinalizerTrue(ack.applied)||
       !Context::Established(ack.resultingLineage)||ack.resultingLineage.KnownPart()->value.identity.kind!=C::IdentityKind::HistoryGeneration||
       !Context::Established(ack.acceptedBoundary.kind)||!Context::SemanticEqual(ack.acceptedBoundary,plan.boundary))return false;
    const C::BoundedList<C::RecordKey,32>* owners=nullptr;const C::BoundedList<C::RecordKey,32>* histories=nullptr;
    if(!Context::ResolveList(plan.affectedOwners,reader,owners)||!owners||
       !Context::ResolveList(plan.affectedHistories,reader,histories)||!histories)return false;
    bool ownerFound=false,historyFound=false,actionFound=false;
    for(const auto& owner:*owners)if(owner==pending.owner)ownerFound=true;
    for(const auto& history:*histories)if(history==pending.history)historyFound=true;
    for(const auto action:plan.actions)if(action==C::ResetAction::ResetHistory)actionFound=true;
    if(!ownerFound||!historyFound||!actionFound)return false;
    for(const auto action:ack.appliedActions)if(action==C::ResetAction::ResetHistory)return true;
    return false;
}
// Owned by the affected history adapter. No public setter can clear pending state.
class FinalizationResetState
{
    FinalizationResetProjection state_;
    std::optional<C::StableIdentity> observedLineage_;
    bool currentLineageKnown_=false;
  public:
    FinalizationResetState(const C::RecordKey& owner,const C::RecordKey& history)
    {state_.owner=owner;state_.history=history;}
    const FinalizationResetProjection& State()const{return state_;}
    // The affected History owner supplies its generation at this invocation,
    // including resets whose acknowledgments have not reached this adapter yet.
    void Observe(C::OutcomeStage stage,const C::OptionalFact<C::HistoryAdvancement>& advancement,const C::OptionalFact<bool>& consumed,
        const C::OptionalFact<C::GenerationToken>& currentLineage={})
    {
        currentLineageKnown_=Context::Established(currentLineage)&&Context::ValidValues(currentLineage)&&
            currentLineage.KnownPart()->value.identity.kind==C::IdentityKind::HistoryGeneration;
        if(currentLineageKnown_)
        {
            const auto& current=currentLineage.KnownPart()->value.identity;
            if(observedLineage_&&(current.nameSpace!=observedLineage_->nameSpace||current.issuer!=observedLineage_->issuer||current.value<observedLineage_->value))currentLineageKnown_=false;
            else observedLineage_=current;
        }
        state_=ProjectFinalizationReset({state_.owner,state_.history,stage,advancement,consumed,state_.pending,state_.plan});
    }
    template<class Reader> bool Acknowledge(const C::ResetPlan& plan,const C::ResetAcknowledgment& ack,const Reader& reader)
    {
        auto pending=state_;pending.plan=C::ContractRef<C::ContractId::C08>{ack.plan.recordType,plan.header.record,plan.header.revision};
        if(!AcknowledgesFinalizationReset(pending,ack,plan,reader))return false;
        const auto& lineage=ack.resultingLineage.KnownPart()->value.identity;
        if(!currentLineageKnown_||!observedLineage_||lineage.nameSpace!=observedLineage_->nameSpace||lineage.issuer!=observedLineage_->issuer||
           lineage.value<=observedLineage_->value)return false;
        observedLineage_=lineage;state_.plan=pending.plan;state_.pending=false;return true;
    }
};
}
