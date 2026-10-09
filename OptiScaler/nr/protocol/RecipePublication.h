#pragma once
#include "NrExecutionRecipe.h"
#include <nr/contracts/C11_Diagnostics.h>

namespace Neurotic::Protocol
{
template<class Store>std::optional<C::ImmutableRecord<C::OwnerReceipt>> BuildPlanningReceipt(const RecipeProduct& product,
    const C::RecordHeader& header,std::uint64_t sequence,Store& store)
{
    if(!S::ValidValues(product)||!S::ValidValues(header)||header.contract!=C::ContractId::C11||
       header.owner!=C::OwnerDomain::RenderingProtocol||header.scope!=product.recipe.header.scope||sequence==0)return {};
    const auto* context=S::ResolveMetadata(product.inputContext,store);if(!context)return {};
    C::OwnerReceipt receipt;receipt.header=header;receipt.ownerSequence=sequence;receipt.operation=Symbol("RENDER.RecipePlanned");
    receipt.relevantGenerations=context->generations;
    C::BoundedList<C::RecordReference,32> causal;
    const auto add=[&](C::ContractId contract,const auto& ref){return causal.Push(C::RecordReference{contract,ref.recordType,ref.record,ref.revision});};
    if(!add(C::ContractId::C06,Reference<C::ContractId::C06>(product.recipe))||
       !add(C::ContractId::C04,Reference<C::ContractId::C04>(product.profileCertificate))||
       !add(C::ContractId::C02,product.recipe.context)||!add(C::ContractId::C05,product.recipe.committedPlan))return {};
    if(product.semanticCertificate&&!add(C::ContractId::C04,*product.semanticCertificate))return {};
    const C::BoundedList<C::ContractRef<C::ContractId::C12>,32>* representations=nullptr;
    if(!S::ResolveList(product.recipe.representations,store,representations))return {};
    if(representations)for(const auto& ref:*representations)if(!add(C::ContractId::C12,ref))return {};
    C::BoundedList<C::OwnerFact,16> facts;const C::EvidenceRef evidence{header.record};
    const auto fact=[&](std::string_view field,C::ScalarValue value){return facts.Push(C::OwnerFact{Symbol(field),C::OptionalFact<C::ScalarValue>::FromKnown(value,evidence)});};
    if(!fact("Disposition",product.disposition)||!fact("BindingCount",std::uint64_t(product.bindingPlan.bindings.Size()))||
       !fact("ModelEvaluationPlanned",product.bindingPlan.modelEvaluation)||!fact("ModelEvaluationExecuted",false)||
       !fact("HistoryAdvanced",false)||!fact("ResetAcknowledged",false)||!fact("SettingsRevision",product.recipe.settingsSnapshot.revision)||
       !S::PublishList(causal,store,receipt.causalRecords)||!S::PublishList(facts,store,receipt.facts)||!S::ValidValues(receipt))return {};
    return C::ImmutableRecord<C::OwnerReceipt>{receipt};
}
}
