#pragma once
#include "NrExecutionRecipe.h"

namespace Neurotic::Protocol
{
// Canonical profile-domain C04, including a refusal. A malformed/unresolvable
// context/header cannot be authenticated and therefore cannot mint a certificate.
template<class Store>std::optional<C::ImmutableRecord<C::QualificationCertificate>> QualifyProfile(const RecipeRequest& input,Store& store)
{
    const auto result=BuildRecipe(input,store);
    if(result.product)return C::ImmutableRecord<C::QualificationCertificate>{result.product->Value().profileCertificate};
    const auto* context=S::ResolveMetadata(input.context,store);
    if(!context||input.context.owner!=C::OwnerDomain::Context||input.currentContext!=Reference<C::ContractId::C02>(*context)||
       !S::ValidValues(input.profileHeader)||input.profileHeader.contract!=C::ContractId::C04||
       input.profileHeader.owner!=C::OwnerDomain::Strategy||input.profileHeader.scope!=context->header.scope||
       !S::ValidValues(input.requestedProfile)||result.reason.Empty())return {};
    C::QualificationCertificate certificate;certificate.header=input.profileHeader;certificate.context=input.currentContext;
    certificate.profile=input.requestedProfile;certificate.purpose=Symbol("RENDER.NativeBindings");certificate.eligibility=C::Eligibility::Ineligible;
    C::PolicyReason reason;reason.rule=Symbol("RENDER.Profile.v1");reason.hard=true;
    reason.reason.category=C::ReasonCategory::CoverageRestriction;reason.reason.code=C::UnknownReason::NotObserved;reason.reason.ownerCode=result.reason;
    C::BoundedList<C::PolicyReason,16> reasons;reasons.Push(reason);
    if(!S::PublishList(reasons,store,certificate.reasons))return {};
    C::StructuralSignature signature;signature.schema=Symbol("RENDER.Refusal");signature.version=1;signature.generations=context->generations;
    certificate.signature=store.Publish(signature);
    if(!S::ValidValues(certificate))return {};
    return C::ImmutableRecord<C::QualificationCertificate>{certificate};
}
}
