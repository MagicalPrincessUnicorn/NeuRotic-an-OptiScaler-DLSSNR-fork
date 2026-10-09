#pragma once
#include "RoutingSelector.h"
#include <charconv>
#include <string>

namespace Neurotic::Orchestration
{
template<class T,std::size_t N,class Store>C::MetadataList<T,N> PublishList(const C::BoundedList<T,N>& body,Store& store)
{
    C::MetadataList<T,N> result;result.count=static_cast<std::uint32_t>(body.Size());
    if(result.count)result.backing=store.Publish(body,C::OwnerDomain::OrchestratorPolicy);return result;
}
inline std::optional<C::Symbol> TieBreakKey(const C::RecordKey& key)
{
    // Length-delimited full key, never a lossy hash. Refuse the canonical Symbol bound.
    const auto text=std::to_string(key.nameSpace.View().size())+":"+std::string(key.nameSpace.View())+":"+
        std::to_string(key.issuer.View().size())+":"+std::string(key.issuer.View())+":"+std::to_string(key.value);
    C::Symbol result;if(!result.Assign(text))return {};return result;
}
template<class Store>std::optional<C::RoutingDecision> PublishDecision(const RoutingInput& input,const ShadowDecision& decision,
    const C::RecordHeader& header,std::uint64_t planningEpoch,Store& store)
{
    if(!decision.valid||!decision.proposed||decision!=Select(input,store)||!Context::ValidValues(header)||
       header.contract!=C::ContractId::C05||header.owner!=C::OwnerDomain::OrchestratorPolicy||header.scope!=input.scope||!planningEpoch)return {};
    const CompletePlan* plan=nullptr;
    for(const auto& reference:input.offers)
    {
        const auto* candidate=Context::ResolveMetadata(reference,store);
        if(candidate&&SameAnchor(candidate->anchor,*decision.proposed,store)){if(plan)return {};plan=candidate;}
    }
    if(!plan)return {};
    const auto tie=TieBreakKey(plan->anchor.plan);if(!tie)return {};
    const auto* certificate=Context::ResolveMetadata(plan->semantic,store);if(!certificate)return {};
    C::RoutingDecision result;result.header=header;result.planningEpoch=planningEpoch;
    result.userIntent=input.intent.source;result.context=plan->context;result.sourceBundle=plan->sourceBundle;
    result.strategy=plan->anchor.strategy;result.profile=plan->anchor.profile;result.signature=plan->anchor.signature;
    result.preparations=plan->preparations;result.multipass=plan->multipass;result.fgSubplan=plan->fgSubplan;
    result.commitBoundary=plan->boundary;result.tieBreakKey=*tie;result.ruleVersion=PolicyVersion;
    C::BoundedList<C::RecordReference,32> inputs;
    inputs.Push({C::ContractId::C04,Symbol(C::QualificationCertificate::WireName),certificate->header.record,certificate->header.revision});
    result.inputSnapshots=PublishList(inputs,store);
    C::BoundedList<C::OwnerValueReference,16> owners;owners.Push(plan->offer);owners.Push(input.intent.source);
    const C::BoundedList<OperationalFact,16>* facts=nullptr;
    if(!Context::ResolveList(plan->facts,store,facts)||!facts)return {};
    for(const auto& fact:*facts)
        if(std::find(owners.begin(),owners.end(),fact.source)==owners.end()&&!owners.Push(fact.source))return {};
    result.ownerInputs=PublishList(owners,store);
    C::BoundedList<C::PolicyReason,16> selected;
    for(const auto& rule:decision.rules)if(!selected.Push(Reason(rule.View(),false,C::ReasonCategory::Degraded)))return {};
    result.selectedReasons=PublishList(selected,store);
    C::BoundedList<C::PolicyReason,32> rejected;
    for(const auto& item:decision.rejected)if(!rejected.Push(item.reason))return {};
    result.rejectedReasons=PublishList(rejected,store);
    C::BoundedList<C::Degradation,16> degradation;const C::EvidenceRef evidence{header.record};
    const std::array<std::pair<std::string_view,std::uint32_t>,4> ranks={{{"mandatoryFeatureLoss",plan->degradation.mandatoryFeatureLoss},
        {"requiredEvidenceLoss",plan->degradation.requiredEvidenceLoss},{"outputAge",plan->degradation.outputAge},{"optionalFeatureLoss",plan->degradation.optionalFeatureLoss}}};
    for(const auto& [name,value]:ranks){C::Degradation item;item.dimension=Symbol(name);item.level=C::OptionalFact<std::uint32_t>::FromKnown(value,evidence);degradation.Push(item);}
    result.degradation=PublishList(degradation,store);
    if(!Context::ValidValues(result))return {};return result;
}
template<class Store>std::optional<C::OwnerReceipt> PublishReceipt(const RoutingInput& input,const ShadowDecision& decision,
    const C::RecordHeader& header,const C::MetadataRef<C::GenerationVector>& generations,Store& store)
{
    if(!decision.valid||decision!=Select(input,store)||!Context::ValidValues(header)||header.contract!=C::ContractId::C11||
       header.owner!=C::OwnerDomain::OrchestratorPolicy||header.scope!=input.scope||!Context::ResolveMetadata(generations,store))return {};
    C::OwnerReceipt result;result.header=header;result.ownerSequence=header.revision;result.operation=Symbol("ORCH.ShadowDecision");
    result.relevantGenerations=generations;C::BoundedList<C::OwnerFact,16> facts;const C::EvidenceRef evidence{header.record};
    for(const auto& [name,value]:std::array<std::pair<std::string_view,C::ScalarValue>,4>{{
        {"proposed",C::ScalarValue{decision.proposed.has_value()}},{"executionEffect",C::ScalarValue{decision.executionEffect}},
        {"lockDisposition",C::ScalarValue{decision.lockDisposition}},{"policyVersion",C::ScalarValue{std::uint64_t{PolicyVersion}}}}})
    {C::OwnerFact fact;fact.field=Symbol(name);fact.value=C::OptionalFact<C::ScalarValue>::FromKnown(value,evidence);facts.Push(fact);}
    result.facts=PublishList(facts,store);
    if(!Context::ValidValues(result))return {};return result;
}
}
