#pragma once
// NR-LIFE-001 candidate. An evidence edge is not an identity allocator or GPU admission.
#include "IdentityPrimitives.h"
#include <cstdint>
#include <optional>

namespace Neurotic::Lifecycle
{
// Local edge labels, not new canonical identity kinds. No timing, dimensions, or pointer fields.
enum class AssociationRelation
{
    RenderToBase, EvaluationToRender, EvaluationToBase, PresentToBase, PresentToGenerated,
    GeneratedToSourceReal, RepeatedPresentToPresent, ResourceViewToResource, EpisodeToBase,
    FinalSealToBase
};

struct GeneratedIdentityAuthority
{
    OwnerBinding binding {};
    ScopeSnapshot currentScope {};
    C::ProviderIncarnation currentProvider {};
    C::Symbol generatedNamespace {};
    bool requireTopologyIncarnation=true;
};
struct GeneratedIdentityClaim
{
    C::OptionalFact<C::GeneratedFrameId> identity {};
    C::OptionalFact<C::ProviderIncarnation> provider {};
    ScopeSnapshot scope {};
    OwnerEvent ownerEvidence {};
};
class ImportedGeneratedFact
{
    C::GeneratedFrameId identity_;
    C::ProviderIncarnation provider_;
    ScopeSnapshot scope_;
    C::EvidenceRef evidence_;
    ImportedGeneratedFact(const C::GeneratedFrameId& id,const C::ProviderIncarnation& provider,
                      const ScopeSnapshot& scope,const C::EvidenceRef& evidence)
        : identity_(id),provider_(provider),scope_(scope),evidence_(evidence) {}
    friend Outcome<ImportedGeneratedFact> ImportGeneratedIdentity(const GeneratedIdentityAuthority&,const GeneratedIdentityClaim&);
  public:
    const C::GeneratedFrameId& Identity() const noexcept { return identity_; }
    const C::ProviderIncarnation& Provider() const noexcept { return provider_; }
    const ScopeSnapshot& Scope() const noexcept { return scope_; }
    const C::EvidenceRef& Evidence() const noexcept { return evidence_; }
};
// Imports an already FG-owner-issued canonical token. The trusted FG seam supplies authority;
// this function only checks its explicit claim. Strings/serialization are not issuer authentication.
inline Outcome<ImportedGeneratedFact> ImportGeneratedIdentity(const GeneratedIdentityAuthority& authority,
                                                         const GeneratedIdentityClaim& claim)
{
    const C::ScopeRef scope{authority.binding.subject};
    if (authority.binding.owner!=C::OwnerDomain::FrameGeneration)
        return Refuse<ImportedGeneratedFact>(IdentityStatus::WrongOwner,scope);
    auto status=CheckOwnerEvent(authority.binding,claim.ownerEvidence);
    if (status!=IdentityStatus::Ok) return Refuse<ImportedGeneratedFact>(status,scope);
    for (const auto* requiredScope : {&authority.currentScope,&claim.scope})
    {
        status=CheckEstablishedScope(*requiredScope,authority.requireTopologyIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<ImportedGeneratedFact>(status,scope);
    }
    status=CompareScope(authority.currentScope,claim.scope,authority.requireTopologyIncarnation);
    if (status!=IdentityStatus::Ok) return Refuse<ImportedGeneratedFact>(status,scope);
    if (!claim.identity.IsKnown()) return PreserveUnknown<ImportedGeneratedFact>(*claim.identity.UnknownPart());
    if (!claim.provider.IsKnown()) return PreserveUnknown<ImportedGeneratedFact>(*claim.provider.UnknownPart());
    const auto& id=claim.identity.KnownPart()->value;const auto& provider=claim.provider.KnownPart()->value;
    if (id.Check()!=C::Error::None || provider.Check()!=C::Error::None || authority.currentProvider.Check()!=C::Error::None ||
        authority.generatedNamespace.Empty()) return Refuse<ImportedGeneratedFact>(IdentityStatus::InvalidScope,scope);
    if (provider!=authority.currentProvider) return Refuse<ImportedGeneratedFact>(IdentityStatus::Different,scope);
    if (id.nameSpace!=authority.generatedNamespace || id.issuer!=authority.binding.publisher.issuer)
        return Refuse<ImportedGeneratedFact>(IdentityStatus::WrongOwner,scope);
    if (claim.identity.KnownPart()->evidence!=claim.ownerEvidence.evidence ||
        !ValidEvidence(claim.provider.KnownPart()->evidence)) return Refuse<ImportedGeneratedFact>(IdentityStatus::InvalidEvidence,scope);
    return Established(ImportedGeneratedFact(id,provider,claim.scope,claim.ownerEvidence.evidence),claim.ownerEvidence.evidence);
}

struct AssociationAuthority
{
    OwnerBinding binding {};
    ScopeSnapshot currentScope {};
    bool requireTopologyIncarnation=true;
    C::OptionalFact<C::ProviderIncarnation> currentGeneratedProvider {};
    std::optional<C::ResourceIdentityToken> currentResource;
};
struct AssociationClaim
{
    AssociationRelation relation=AssociationRelation::RenderToBase;
    C::OptionalFact<C::StableIdentity> source {};
    C::OptionalFact<C::StableIdentity> target {};
    ScopeSnapshot sourceScope {};
    ScopeSnapshot targetScope {};
    OwnerEvent ownerEvidence {};
    std::optional<ImportedGeneratedFact> generatedOwnerFact;
};
// Values-only edge projection. The C14 body remains the canonical FrameIdentity.
// A generated frame's source real ID is NOT placed in frame.baseRealFrameId.
struct AssociationProjection
{
    AssociationRelation relation=AssociationRelation::RenderToBase;
    C::FrameIdentity frame {};
    C::OptionalFact<C::PresentId> repeatedFrom {};
    C::OptionalFact<C::BaseRealFrameId> sourceRealFrame {};
    C::ResourceIdentityToken resource {};
};
inline IdentityStatus AssociationKinds(AssociationRelation relation,C::OwnerDomain owner,
                                        C::IdentityKind source,C::IdentityKind target) noexcept
{
    using K=C::IdentityKind;using O=C::OwnerDomain;
    bool authority=false;K left=K::RenderId,right=K::BaseRealFrameId;
    switch (relation)
    {
    case AssociationRelation::RenderToBase: authority=owner==O::IdentityRegistry;break;
    case AssociationRelation::EvaluationToRender:
        authority=owner==O::Strategy || owner==O::Multipass;left=K::EvaluationId;right=K::RenderId;break;
    case AssociationRelation::EvaluationToBase:
        authority=owner==O::Strategy || owner==O::Multipass;left=K::EvaluationId;break;
    case AssociationRelation::PresentToBase: authority=owner==O::Presentation;left=K::PresentId;break;
    case AssociationRelation::PresentToGenerated:
        authority=owner==O::Presentation;left=K::PresentId;right=K::GeneratedFrameId;break;
    case AssociationRelation::GeneratedToSourceReal: authority=owner==O::FrameGeneration;left=K::GeneratedFrameId;break;
    case AssociationRelation::RepeatedPresentToPresent:
        authority=owner==O::Presentation;left=K::PresentId;right=K::PresentId;break;
    case AssociationRelation::ResourceViewToResource:
        authority=owner==O::Resource;left=K::ResourceViewIncarnation;right=K::ResourceIncarnation;break;
    case AssociationRelation::EpisodeToBase:
        authority=owner==O::IdentityRegistry || owner==O::Context;left=K::EpisodeId;break;
    case AssociationRelation::FinalSealToBase: authority=owner==O::Finalizer;left=K::FinalSealId;break;
    default: return IdentityStatus::InvalidRule;
    }
    if (!authority) return IdentityStatus::WrongOwner;
    return source==left && target==right ? IdentityStatus::Ok : IdentityStatus::WrongKind;
}
namespace Detail
{
template<class T> C::OptionalFact<T> ProjectIdentity(const C::StableIdentity& value,const C::EvidenceRef& evidence)
{
    return C::OptionalFact<T>::FromKnown(T{value.nameSpace,value.issuer,value.value},evidence);
}
inline void ProjectFrameEndpoint(C::FrameIdentity& frame,const C::StableIdentity& value,const C::EvidenceRef& evidence)
{
    switch (value.kind)
    {
    case C::IdentityKind::RenderId: frame.renderId=ProjectIdentity<C::RenderId>(value,evidence);break;
    case C::IdentityKind::EvaluationId: frame.evaluationId=ProjectIdentity<C::EvaluationId>(value,evidence);break;
    case C::IdentityKind::PresentId: frame.presentId=ProjectIdentity<C::PresentId>(value,evidence);break;
    case C::IdentityKind::GeneratedFrameId: frame.generatedFrameId=ProjectIdentity<C::GeneratedFrameId>(value,evidence);break;
    case C::IdentityKind::EpisodeId: frame.episodeId=ProjectIdentity<C::EpisodeId>(value,evidence);break;
    case C::IdentityKind::FinalSealId: frame.finalSealId=ProjectIdentity<C::FinalSealId>(value,evidence);break;
    default: break;
    }
}
}
inline Outcome<AssociationProjection> MakeAssociation(const AssociationAuthority& authority,
                                                       const AssociationClaim& claim)
{
    const C::ScopeRef scope{authority.binding.subject};
    auto status=CheckOwnerEvent(authority.binding,claim.ownerEvidence);
    if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    for (const auto* requiredScope : {&authority.currentScope,&claim.sourceScope,&claim.targetScope})
    {
        status=CheckEstablishedScope(*requiredScope,authority.requireTopologyIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    }
    status=CompareScope(authority.currentScope,claim.sourceScope,authority.requireTopologyIncarnation);
    if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    status=CompareScope(authority.currentScope,claim.targetScope,authority.requireTopologyIncarnation);
    if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    if (!claim.source.IsKnown()) return PreserveUnknown<AssociationProjection>(*claim.source.UnknownPart());
    if (!claim.target.IsKnown()) return PreserveUnknown<AssociationProjection>(*claim.target.UnknownPart());
    const auto& from=claim.source.KnownPart()->value;const auto& to=claim.target.KnownPart()->value;
    if (!ValidIdentity(from) || !ValidIdentity(to)) return Refuse<AssociationProjection>(IdentityStatus::InvalidScope,scope);
    if (!ValidEvidence(claim.source.KnownPart()->evidence) || !ValidEvidence(claim.target.KnownPart()->evidence))
        return Refuse<AssociationProjection>(IdentityStatus::InvalidEvidence,scope);
    status=AssociationKinds(claim.relation,authority.binding.owner,from.kind,to.kind);
    if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    if (from.kind==C::IdentityKind::GeneratedFrameId || to.kind==C::IdentityKind::GeneratedFrameId)
    {
        if (!claim.generatedOwnerFact || !authority.currentGeneratedProvider.IsKnown())
            return Refuse<AssociationProjection>(IdentityStatus::Unknown,scope);
        const auto& imported=*claim.generatedOwnerFact;
        const auto& endpoint=from.kind==C::IdentityKind::GeneratedFrameId ? from : to;
        if (imported.Identity().Describe()!=endpoint || imported.Provider()!=authority.currentGeneratedProvider.KnownPart()->value)
            return Refuse<AssociationProjection>(IdentityStatus::Different,scope);
        status=CheckEstablishedScope(imported.Scope(),authority.requireTopologyIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
        status=CompareScope(authority.currentScope,imported.Scope(),authority.requireTopologyIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    }
    if (claim.generatedOwnerFact && from.kind!=C::IdentityKind::GeneratedFrameId && to.kind!=C::IdentityKind::GeneratedFrameId)
        return Refuse<AssociationProjection>(IdentityStatus::InvalidRule,scope);
    if (claim.relation==AssociationRelation::ResourceViewToResource)
    {
        if (!authority.currentResource) return Refuse<AssociationProjection>(IdentityStatus::Unknown,scope);
        const auto& current=*authority.currentResource;
        if (current.Check()!=C::Error::None) return Refuse<AssociationProjection>(IdentityStatus::WrongKind,scope);
        status=CompareIdentity(claim.target,current.resourceIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
        status=CompareIdentity(claim.source,current.resourceViewIncarnation);
        if (status!=IdentityStatus::Ok) return Refuse<AssociationProjection>(status,scope);
    }
    if (claim.relation==AssociationRelation::RepeatedPresentToPresent && from==to)
        return Refuse<AssociationProjection>(IdentityStatus::InvalidRule,scope);
    AssociationProjection result;
    result.relation=claim.relation;
    result.frame.sessionId=claim.sourceScope.session;
    result.frame.renderStreamId=claim.sourceScope.stream;
    result.frame.viewId=claim.sourceScope.view;
    const auto& evidence=claim.ownerEvidence.evidence;
    if (!result.frame.associationEvidence.Push(evidence))
        return Refuse<AssociationProjection>(IdentityStatus::Exhausted,scope);
    Detail::ProjectFrameEndpoint(result.frame,from,claim.source.KnownPart()->evidence);
    if (claim.relation==AssociationRelation::RepeatedPresentToPresent)
        result.repeatedFrom=Detail::ProjectIdentity<C::PresentId>(to,claim.target.KnownPart()->evidence);
    else if (claim.relation==AssociationRelation::GeneratedToSourceReal)
        result.sourceRealFrame=Detail::ProjectIdentity<C::BaseRealFrameId>(to,claim.target.KnownPart()->evidence);
    else if (claim.relation==AssociationRelation::ResourceViewToResource)
    {
        result.resource.resourceViewIncarnation=claim.source;
        result.resource.resourceIncarnation=claim.target;
    }
    else if (to.kind==C::IdentityKind::BaseRealFrameId)
        result.frame.baseRealFrameId=Detail::ProjectIdentity<C::BaseRealFrameId>(to,claim.target.KnownPart()->evidence);
    else
        Detail::ProjectFrameEndpoint(result.frame,to,claim.target.KnownPart()->evidence);
    if (claim.generatedOwnerFact)
    {
        // Preserve original generated-output provenance; the Present owner only associates it.
        result.frame.generatedFrameId=C::OptionalFact<C::GeneratedFrameId>::FromKnown(
            claim.generatedOwnerFact->Identity(),claim.generatedOwnerFact->Evidence());
        if (!result.frame.associationEvidence.Push(claim.generatedOwnerFact->Evidence()))
            return Refuse<AssociationProjection>(IdentityStatus::Exhausted,scope);
    }
    return Established(result,evidence);
}
} // namespace Neurotic::Lifecycle
