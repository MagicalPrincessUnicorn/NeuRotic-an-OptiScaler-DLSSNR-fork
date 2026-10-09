#pragma once
#include "RoutingRules.h"
namespace Neurotic::Orchestration
{
template<class Store> ShadowDecision Select(const RoutingInput& input,const Store& store)
{
    ShadowDecision result;
    if(!Context::ValidValues(input)||!input.scope.key||!ValidIntent(input.intent)||
       !Context::Established(input.mandatoryRR)||!Context::Established(input.mandatoryFG))return result;
    if(input.incumbent.current&&(input.incumbent.source.owner!=C::OwnerDomain::StreamCoordinator||
       input.incumbent.policyVersion!=PolicyVersion||input.incumbent.current->scope!=input.scope||!ValidSignature(*input.incumbent.current,store)))return result;
    struct Entry{const CompletePlan* plan=nullptr;AdmissionResult admission;};
    std::array<Entry,16> entries{};std::size_t count=0;
    for(const auto& offer:input.offers)
    {
        const auto* plan=Context::ResolveMetadata(offer,store);if(!plan||offer.owner!=C::OwnerDomain::Strategy)return result;
        for(std::size_t i=0;i<count;++i)if(entries[i].plan->anchor.plan==plan->anchor.plan)return result;
        entries[count++]={plan,Admit(input,*plan,store)};
    }
    std::sort(entries.begin(),entries.begin()+count,[](const auto& a,const auto& b){return Context::KeyLess(a.plan->anchor.plan,b.plan->anchor.plan);});
    result.next=input.incumbent;result.valid=true;
    const Entry* incumbent=nullptr;bool structuralChange=No(input.incumbent.structurallyCurrent);
    for(std::size_t i=0;i<count;++i)
    {
        const auto& entry=entries[i];
        if(entry.admission.state!=Admission::Eligible&&!result.rejected.Push({entry.plan->anchor.plan,entry.admission.reason}))return {};
        if(input.incumbent.current&&entry.plan->anchor.plan==input.incumbent.current->plan)
        {
            if(SameAnchor(entry.plan->anchor,*input.incumbent.current,store))incumbent=&entry;
            else structuralChange=true;
        }
    }
    const auto healthyIncumbent=[&](const Entry* e){return e==incumbent&&!structuralChange&&e->admission.healthy;};
    std::array<const Entry*,16> ranked{};std::size_t rankedCount=0;
    for(std::size_t i=0;i<count;++i)if(entries[i].admission.state==Admission::Eligible)ranked[rankedCount++]=&entries[i];
    std::sort(ranked.begin(),ranked.begin()+rankedCount,[&](const Entry* a,const Entry* b){
        const auto& x=*a->plan;const auto& y=*b->plan;
        if(x.degradation.Tuple()!=y.degradation.Tuple())return x.degradation.Tuple()<y.degradation.Tuple();
        const auto px=Preference(input.intent,x),py=Preference(input.intent,y);if(px!=py)return px<py;
        if(healthyIncumbent(a)!=healthyIncumbent(b))return healthyIncumbent(a);
        const auto evidence=[](const auto& fact){const bool known=Context::Established(fact);return std::pair{!known,known?fact.KnownPart()->value:std::uint32_t{0}};};
        if(evidence(x.evidenceRank)!=evidence(y.evidenceRank))return evidence(x.evidenceRank)<evidence(y.evidenceRank);
        const auto cost=[](const auto& fact){const bool known=Context::Established(fact)&&fact.KnownPart()->value>=0;return std::pair{!known,known?fact.KnownPart()->value:0.0};};
        if(cost(x.measuredCost)!=cost(y.measuredCost))return cost(x.measuredCost)<cost(y.measuredCost);
        return Context::KeyLess(x.anchor.plan,y.anchor.plan);
    });
    for(std::size_t i=0;i<rankedCount;++i)result.ranked.Push(ranked[i]->plan->anchor.plan);
    // Reset progress without forgetting already-counted owner real frames.
    const auto clearPromotion=[&]{result.next.promotion.reset();result.next.streak=0;};
    if(input.intent.mode.View()=="LockCurrent")
    {
        result.alternateSelectionAllowed=false;result.next.wasLocked=true;clearPromotion();
        result.rules.Push(Symbol("ORCH.Intent.LockCurrent.Active"));
        if(!input.incumbent.current){result.lockDisposition=Symbol("None");result.rules.Push(Symbol("ORCH.Intent.LockCurrent.NoIncumbent"));}
        else if(structuralChange||(incumbent&&incumbent->admission.state==Admission::HardInvalid))
        {
            result.next.current.reset();result.lockDisposition=Symbol("Clear");
            result.rules.Push(Symbol(structuralChange?"ORCH.Intent.LockCurrent.StructuralIdentityChanged":"ORCH.Intent.LockCurrent.IncumbentHardInvalid"));
        }
        else if(incumbent&&incumbent->admission.state==Admission::Eligible&&Yes(input.incumbent.structurallyCurrent))
        {
            result.proposed=incumbent->plan->anchor;result.lockDisposition=Symbol("Retain");
            result.rules.Push(Symbol("ORCH.Intent.LockCurrent.IncumbentSelected"));
        }
        else{result.lockDisposition=Symbol("Retain");result.rules.Push(Symbol("ORCH.Intent.LockCurrent.IncumbentTemporarilyUnavailable"));}
        if(rankedCount&&(!result.proposed||rankedCount>1))result.rules.Push(Symbol("ORCH.Intent.LockCurrent.AlternateSuppressed"));
        return result;
    }
    result.next.wasLocked=false;
    if(input.incumbent.wasLocked){clearPromotion();result.rules.Push(Symbol("ORCH.Intent.LockCurrent.ReleasedByIntentChange"));}
    if(structuralChange||(incumbent&&incumbent->admission.state==Admission::HardInvalid))
    {result.next.current.reset();clearPromotion();incumbent=nullptr;result.rules.Push(Symbol("ORCH.Safety.Revoked"));}
    if(!rankedCount){clearPromotion();result.rules.Push(Symbol("ORCH.NoQualifiedPlan"));return result;}
    const auto* best=ranked[0];
    if(!result.next.current||best==incumbent)
    {result.proposed=best->plan->anchor;clearPromotion();result.rules.Push(Symbol("ORCH.R1-R7.Selected"));return result;}
    // Proposals only. The live coordinator owns establishment of the next authoritative incumbent.
    if(!result.next.promotion||!SameAnchor(*result.next.promotion,best->plan->anchor,store))
    {clearPromotion();result.next.promotion=best->plan->anchor;}
    bool newReal=input.frameSource.owner==C::OwnerDomain::IdentityRegistry&&input.frameSource.recordType.View()=="NR.C14.FrameProgress"&&
        input.frameScope==input.scope&&Yes(input.provenRealFrame)&&No(input.repeatedPresent)&&
        !input.frame.generatedFrameId.IsKnown()&&Context::SameScope(input.frame,input.frame);
    if(newReal&&result.next.lastCounted)
    {
        const auto& now=input.frame.baseRealFrameId.KnownPart()->value;const auto& last=*result.next.lastCounted;
        newReal=now.nameSpace==last.nameSpace&&now.issuer==last.issuer&&now.value>last.value;
    }
    if(newReal){result.next.lastCounted=input.frame.baseRealFrameId.KnownPart()->value;if(result.next.streak<1000000)++result.next.streak;}
    const auto threshold=(std::max)(input.intent.promotionFrames,input.intent.warmupFrames);
    if(result.next.streak>=threshold){result.proposed=best->plan->anchor;result.rules.Push(Symbol("ORCH.Hysteresis.PromoteProposal"));}
    else{if(incumbent&&incumbent->admission.state==Admission::Eligible)result.proposed=incumbent->plan->anchor;result.rules.Push(Symbol("ORCH.Hysteresis.Wait"));}
    return result;
}
}
