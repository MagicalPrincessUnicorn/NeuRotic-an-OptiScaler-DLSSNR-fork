#pragma once
// NR-LIFE-001 candidate. C08 description/projection primitives only; no reset execution/acknowledgment.
#include "IdentityDependencies.h"
#include "../contracts/C08_Reset.h"

namespace Neurotic::Lifecycle
{
inline Outcome<C::GenerationChange> DescribeGenerationChange(const GenerationAxis& axis,
    const C::OptionalFact<C::GenerationToken>& before,const C::OptionalFact<C::GenerationToken>& after,
    const C::EvidenceRef& ownerEvidence)
{
    if (!ValidAxis(axis)) return Refuse<C::GenerationChange>(IdentityStatus::WrongKind);
    if (!ValidEvidence(ownerEvidence)) return Refuse<C::GenerationChange>(IdentityStatus::InvalidEvidence);
    for (const auto* value : {&before,&after}) if (value->IsKnown())
    {
        const auto& token=value->KnownPart()->value;
        if (token.identity.kind!=axis.kind) return Refuse<C::GenerationChange>(IdentityStatus::WrongKind);
        if (!ValidIdentity(token.identity)) return Refuse<C::GenerationChange>(IdentityStatus::InvalidScope);
        if (!SameAxis(axis,token)) return Refuse<C::GenerationChange>(IdentityStatus::Different);
    }
    return Established(C::GenerationChange{before,after},ownerEvidence);
}
inline Outcome<bool> ChangeAffects(const ResolvedStructuralSignature& dependent,const GenerationAxis& axis,
                                   const C::GenerationChange& change,const C::EvidenceRef& ownerEvidence)
{
    const auto dependencyStatus=CheckSignature(dependent);
    if (dependencyStatus!=IdentityStatus::Ok) return Refuse<bool>(dependencyStatus);
    const auto checked=DescribeGenerationChange(axis,change.before,change.after,ownerEvidence);
    if (checked.status!=IdentityStatus::Ok) return Refuse<bool>(checked.status);
    const C::GenerationToken* selected=nullptr;
    for (const auto& token:dependent.generationBody->second.Entries()) if (SameAxis(axis,token)) { selected=&token;break; }
    if (!selected) return Established(false,ownerEvidence);
    if (!change.before.IsKnown()) return PreserveUnknown<bool>(*change.before.UnknownPart());
    if (!change.after.IsKnown()) return PreserveUnknown<bool>(*change.after.UnknownPart());
    const auto& before=change.before.KnownPart()->value;const auto& after=change.after.KnownPart()->value;
    if (*selected==after) return Established(false,ownerEvidence); // Already reflects this transition.
    if (*selected!=before) return Refuse<bool>(IdentityStatus::Different); // Not this transition's lineage.
    return Established(before!=after,ownerEvidence);
}
} // namespace Neurotic::Lifecycle
