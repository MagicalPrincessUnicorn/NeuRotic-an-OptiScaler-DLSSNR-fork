#pragma once
#include "RecipePublication.h"
#include <nr/contracts/C07_ProtocolOutput.h>
#include <dlssnr/NativeDispatchOutcome.h>

namespace Neurotic::Protocol
{
// Ephemeral strategy-owner milestones, never decoded as operational authority.
// The renderer fills these at the actual operation, not from aggregate counters.
struct NativeExecutionFacts
{
    bool modelAttempted=false,modelSucceeded=false,commandsRecorded=false;
    bool legacyFallback=false,originalBypass=false;
    // Failure-only CPU parameter integrity fact. False does not attest a GPU
    // write, restoration of command state, or delivery to the caller.
    bool outputRestoreFailed=false;
    bool commandStateRestoreFailed=false;
    // Ephemeral actual resolve-pass observation, not a Resource write receipt.
    std::optional<DlssNr::NativeDispatchOutcome> outputPass;
    std::optional<bool> actualResetRequested; // argument at the real opaque call; not an applied reset
    C::OptionalFact<bool> submitted,originalPreserved;
    C::CompositionStatus composition=C::CompositionStatus::NotAttempted;
    C::OptionalFact<C::HistoryAdvancement> advancement;
    C::OptionalFact<C::MetadataRef<C::ResourceView>> output;
    C::OptionalFact<C::ContractRef<C::ContractId::C03>> dependency;
    C::BoundedList<C::Symbol,32> actualPurposes;
    C::BoundedList<C::ContractRef<C::ContractId::C08>,16> resetAcknowledgments;
    C::Symbol failure;
};
inline bool NativeMayContinueOuterEvaluation(const NativeExecutionFacts& facts)noexcept
{return !facts.outputRestoreFailed&&!facts.commandStateRestoreFailed;}
inline void NativeMarkCommandStateRestoreFailed(NativeExecutionFacts& facts)noexcept
{
    facts.commandStateRestoreFailed=true;
    if(facts.failure.Empty())facts.failure.Assign("Native.CommandStateRestoreFailed");
}
inline bool NativeYes(const C::OptionalFact<bool>& value)
{return S::Established(value)&&value.KnownPart()->value;}
inline bool NativeFactsCoherent(const NativeExecutionFacts& f)
{
    if(!S::ValidValues(f.submitted)||!S::ValidValues(f.originalPreserved)||!S::ValidValues(f.advancement)||
       !S::ValidValues(f.output)||!S::ValidValues(f.dependency)||!S::ValidValues(f.composition)||
       !S::ValidValues(f.actualPurposes)||!S::ValidValues(f.resetAcknowledgments))return false;
    if(f.modelSucceeded&&!f.modelAttempted)return false;
    if(f.outputPass)
    {
        const auto& pass=*f.outputPass;
        if(!pass.CommandList()||!pass.Target()||!pass.Width()||!pass.Height())return false;
        if(pass.Effect()>=DlssNr::NativeDispatchEffect::DispatchPossible&&!f.commandsRecorded)return false;
    }
    if((f.outputRestoreFailed||f.commandStateRestoreFailed)&&f.failure.Empty())return false;
    if(f.actualResetRequested&&!f.modelAttempted)return false;
    if(f.modelAttempted&&!f.commandsRecorded)return false;
    if(NativeYes(f.submitted)&&!f.commandsRecorded)return false;
    if((f.legacyFallback||f.originalBypass)&&(f.modelAttempted||f.commandsRecorded))return false;
    const bool derived=f.outputPass&&f.outputPass->Derivation()==DlssNr::NativeOutputDerivation::AlternateFrameCarry;
    if(derived&&(f.modelAttempted||f.modelSucceeded||f.actualResetRequested||f.resetAcknowledgments.Size()!=0))return false;
    if(f.composition!=C::CompositionStatus::NotAttempted&&!f.modelSucceeded&&
       !(derived&&f.outputPass->Effect()==DlssNr::NativeDispatchEffect::DispatchRecorded))return false;
    if(f.composition==C::CompositionStatus::Accepted&&(!S::Established(f.output)||!f.failure.Empty()))return false;
    if(S::Established(f.output)&&f.composition!=C::CompositionStatus::Accepted)return false;
    if(!f.modelAttempted&&S::Established(f.advancement)&&f.advancement.KnownPart()->value!=C::HistoryAdvancement::NotAdvanced)return false;
    for(std::size_t i=0;i<f.actualPurposes.Size();++i)
    {
        if(f.actualPurposes.Get(i)->Empty())return false;
        for(std::size_t j=0;j<i;++j)if(*f.actualPurposes.Get(i)==*f.actualPurposes.Get(j))return false;
    }
    return true;
}
template<class Store>std::optional<C::EvaluationResult> BuildNativeEvaluationResult(const C::NrExecutionRecipe& recipe,
    const C::RecordHeader& header,const NativeExecutionFacts& facts,Store& store)
{
    if(!S::ValidValues(recipe)||!S::ValidValues(header)||header.contract!=C::ContractId::C07||
       header.owner!=C::OwnerDomain::Strategy||header.scope!=recipe.header.scope||!NativeFactsCoherent(facts)||
       !S::ResolveMetadata(recipe.frame,store))return {};
    if(S::Established(facts.output)&&!S::ResolveMetadata(facts.output.KnownPart()->value,store))return {};
    C::EvaluationResult result;result.header=header;result.evaluation=recipe.evaluation;result.originalLineage=recipe.frame;
    const C::EvidenceRef evidence{header.record};
    result.output=facts.output;result.dependency=facts.dependency;result.originalPreserved=facts.originalPreserved;
    result.composition=C::OptionalFact<C::CompositionStatus>::FromKnown(facts.composition,evidence);
    result.historyAdvancement=S::Established(facts.advancement)?facts.advancement:
        C::OptionalFact<C::HistoryAdvancement>::FromKnown(facts.modelAttempted?C::HistoryAdvancement::MayHaveAdvanced:C::HistoryAdvancement::NotAdvanced,evidence);
    if(facts.legacyFallback||facts.originalBypass)result.stage=C::OutcomeStage::Bypassed;
    else if(!facts.failure.Empty()||facts.composition==C::CompositionStatus::Failed||
            (facts.modelAttempted&&!facts.modelSucceeded))
        result.stage=NativeYes(facts.submitted)?C::OutcomeStage::Interrupted:C::OutcomeStage::Failed;
    else if(facts.composition==C::CompositionStatus::Accepted&&S::Established(facts.output))result.stage=C::OutcomeStage::Produced;
    else if(NativeYes(facts.submitted))result.stage=C::OutcomeStage::Submitted;
    else if(facts.commandsRecorded)result.stage=C::OutcomeStage::Recorded;
    if(!facts.failure.Empty())
    {
        C::ReasonId reason;reason.category=C::ReasonCategory::CoverageRestriction;
        reason.code=C::UnknownReason::NotObserved;reason.ownerCode=facts.failure;
        result.failure=C::OptionalFact<C::ReasonId>::FromKnown(reason,evidence);
    }
    if(!S::PublishList(facts.actualPurposes,store,result.actualFieldsUsed)||
       !S::PublishList(facts.resetAcknowledgments,store,result.resetAcknowledgments)||!S::ValidValues(result))return {};
    return result;
}

// Every detailed binding chunk is a separate canonical C11 owner receipt. These
// references close over the actual fields without truncating C07's purpose list.
template<class Store>std::optional<C::OwnerReceipt> BuildNativeExecutionReceipt(const C::NrExecutionRecipe& recipe,
    const C::EvaluationResult& result,const NativeExecutionFacts& facts,const C::RecordHeader& header,
    std::uint64_t sequence,const C::MetadataRef<C::GenerationVector>& generations,
    const C::BoundedList<C::RecordReference,16>& bindingReceipts,Store& store)
{
    if(!S::ValidValues(header)||header.contract!=C::ContractId::C11||header.owner!=C::OwnerDomain::Strategy||
       header.scope!=result.header.scope||result.evaluation!=recipe.evaluation||!NativeFactsCoherent(facts)||sequence==0||
       !S::ResolveMetadata(generations,store)||!S::ValidValues(bindingReceipts))return {};
    C::OwnerReceipt receipt;receipt.header=header;receipt.ownerSequence=sequence;
    receipt.operation=Symbol("Native.Execution");receipt.relevantGenerations=generations;
    C::BoundedList<C::RecordReference,32> causal;
    causal.Push({C::ContractId::C06,Symbol(C::NrExecutionRecipe::WireName),recipe.header.record,recipe.header.revision});
    causal.Push({C::ContractId::C07,Symbol(C::EvaluationResult::WireName),result.header.record,result.header.revision});
    for(const auto& ref:facts.resetAcknowledgments)
        if(!causal.Push({C::ContractId::C08,ref.recordType,ref.record,ref.revision}))return {};
    for(const auto& ref:bindingReceipts)if(ref.contract!=C::ContractId::C11||!causal.Push(ref))return {};
    C::BoundedList<C::OwnerFact,16> values;const C::EvidenceRef evidence{header.record};
    const auto add=[&](std::string_view key,bool value){return values.Push({Symbol(key),C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{value},evidence)});};
    if(!add("ModelAttempted",facts.modelAttempted)||!add("ModelSucceeded",facts.modelSucceeded)||
       !add("CommandsRecorded",facts.commandsRecorded)||!add("LegacyFallback",facts.legacyFallback)||
       !add("OriginalBypass",facts.originalBypass)||!add("OutputRestoreFailed",facts.outputRestoreFailed)||
       !add("CommandStateRestoreFailed",facts.commandStateRestoreFailed))return {};
    C::OwnerFact reset;reset.field=Symbol("ActualResetRequested");
    if(facts.actualResetRequested)reset.value=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{*facts.actualResetRequested},evidence);
    if(!values.Push(reset))return {};
    C::OwnerFact submitted;submitted.field=Symbol("SubmissionObserved");
    if(S::Established(facts.submitted))submitted.value=C::OptionalFact<C::ScalarValue>::FromKnown(
        C::ScalarValue{facts.submitted.KnownPart()->value},facts.submitted.KnownPart()->evidence);
    if(!values.Push(submitted)||!S::PublishList(causal,store,receipt.causalRecords)||
       !S::PublishList(values,store,receipt.facts)||!S::ValidValues(receipt))return {};
    return receipt;
}
}
