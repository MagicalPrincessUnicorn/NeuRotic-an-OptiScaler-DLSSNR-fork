#pragma once
#include "SemanticMath.h"
#include <span>

namespace Neurotic::Context
{
template<class T> bool SameFact(const C::OptionalFact<T>& a, const C::OptionalFact<T>& b)
{
    return Established(a) && Established(b) && a.KnownPart()->value == b.KnownPart()->value;
}
inline bool SameScope(const C::FrameIdentity& a, const C::FrameIdentity& b,bool requireBaseFrame=true)
{
    if (a.associationEvidence.Size() == 0 || b.associationEvidence.Size() == 0) return false;
    for (const auto& evidence : a.associationEvidence) if (!Lifecycle::ValidEvidence(evidence)) return false;
    for (const auto& evidence : b.associationEvidence) if (!Lifecycle::ValidEvidence(evidence)) return false;
    return Lifecycle::CompareTypedIdentity(a.sessionId,b.sessionId) == Lifecycle::IdentityStatus::Ok &&
        Lifecycle::CompareTypedIdentity(a.renderStreamId,b.renderStreamId) == Lifecycle::IdentityStatus::Ok &&
        Lifecycle::CompareTypedIdentity(a.viewId,b.viewId) == Lifecycle::IdentityStatus::Ok &&
        Lifecycle::CompareTypedIdentity(a.episodeId,b.episodeId) == Lifecycle::IdentityStatus::Ok &&
        (!requireBaseFrame || Lifecycle::CompareTypedIdentity(a.baseRealFrameId,b.baseRealFrameId) == Lifecycle::IdentityStatus::Ok) &&
        Established(a.sessionId) && Established(b.sessionId) && Established(a.renderStreamId) && Established(b.renderStreamId) &&
        Established(a.viewId) && Established(b.viewId) && Established(a.episodeId) && Established(b.episodeId) &&
        (!requireBaseFrame || (Established(a.baseRealFrameId) && Established(b.baseRealFrameId)));
}
inline bool SameResourceStructure(const C::ResourceIdentityToken& a, const C::ResourceIdentityToken& b)
{
    return a.Check() == C::Error::None && b.Check() == C::Error::None &&
        SameFact(a.objectIncarnation,b.objectIncarnation) && SameFact(a.resourceIncarnation,b.resourceIncarnation) &&
        SameFact(a.resourceViewIncarnation,b.resourceViewIncarnation) &&
        SameFact(a.resourceGeneration,b.resourceGeneration) && SameFact(a.representationGeneration,b.representationGeneration);
}
inline bool SameContent(const C::OptionalFact<C::ContentRevision>& a, const C::OptionalFact<C::ContentRevision>& b)
{
    return SameFact(a,b);
}
inline bool Aliases(const C::EvidenceVector& a, const C::EvidenceVector& b)
{
    if (SameFact(a.aliasOf,b.aliasOf)) return true;
    for (const auto& left : a.lineage)
    {
        if (Established(b.aliasOf) && left == b.aliasOf.KnownPart()->value) return true;
        for (const auto& right : b.lineage) if (left.Check() == C::Error::None && left == right) return true;
    }
    for (const auto& right : b.lineage)
        if (Established(a.aliasOf) && right == a.aliasOf.KnownPart()->value) return true;
    return false;
}
// A narrowly declared partial order. Source names and provider scores have no ranking authority.
inline bool Dominates(const C::EvidenceVector& a, const C::EvidenceVector& b)
{
    return Established(a.semanticCertainty) && Established(b.semanticCertainty) &&
        a.semanticCertainty.KnownPart()->value == C::SemanticCertainty::Qualified &&
        b.semanticCertainty.KnownPart()->value == C::SemanticCertainty::Claimed &&
        Established(a.ageInRealFrames) && Established(b.ageInRealFrames) &&
        a.ageInRealFrames.KnownPart()->value <= b.ageInRealFrames.KnownPart()->value &&
        SameFact(a.coverage,b.coverage) && SameFact(a.access,b.access);
}
inline bool KeyLess(const C::RecordKey& a, const C::RecordKey& b)
{
    return std::tuple{a.nameSpace.View(),a.issuer.View(),a.value} <
           std::tuple{b.nameSpace.View(),b.issuer.View(),b.value};
}
template<class T> struct FactCandidate
{
    C::RecordKey key;
    C::OptionalFact<T> value;
    const C::EvidenceVector* evidence = nullptr; // callback-scoped CPU metadata, never retained
};
template<class T> struct ResolvedFact
{
    C::OptionalFact<T> value;
    C::BoundedList<C::RecordKey,64> sources;
    C::BoundedList<C::RecordKey,64> rejected;
    C::BoundedList<std::pair<C::RecordKey,C::UnknownFact>,64> unknowns;
    std::size_t independentSources = 0;
    bool contradiction = false;
};
template<class T> bool FiniteFactValue(const T& value)
{
    if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
    else if constexpr (std::is_same_v<T,C::Vec2>) return Finite(value);
    else if constexpr (std::is_same_v<T,C::ScalarValue>)
        return std::visit([](const auto& part){return FiniteFactValue(part);},value);
    else return true;
}
template<class T> bool Related(const FactCandidate<T>& a, const FactCandidate<T>& b)
{
    return a.key==b.key || Aliases(*a.evidence,*b.evidence) ||
        (Established(a.evidence->aliasOf) && a.evidence->aliasOf.KnownPart()->value==b.key) ||
        (Established(b.evidence->aliasOf) && b.evidence->aliasOf.KnownPart()->value==a.key);
}
template<class T,class Equal=std::equal_to<T>> ResolvedFact<T> ResolveEvidence(std::span<const FactCandidate<T>> candidates,Equal equal={})
{
    ResolvedFact<T> result;
    result.value = Reject<T>("CTX.UnknownRequiredFact");
    if (candidates.size() > 64) { result.value=Reject<T>("CTX.MalformedObservation"); return result; }
    std::array<const FactCandidate<T>*,64> ordered{};
    std::size_t count=0;
    for (const auto& candidate : candidates)
    {
        if (candidate.key.Check()!=C::Error::None) continue;
        if (!candidate.value.IsKnown())
        {
            const auto& unknown=*candidate.value.UnknownPart();
            result.unknowns.Push({candidate.key,unknown});
            if (unknown.reason.code==C::UnknownReason::ContradictoryEvidence) result.contradiction=true;
            continue;
        }
        if (!candidate.evidence || !Established(candidate.value)) continue;
        if (!FiniteFactValue(candidate.value.KnownPart()->value)) {result.rejected.Push(candidate.key);continue;}
        if (Established(candidate.evidence->semanticCertainty) &&
            candidate.evidence->semanticCertainty.KnownPart()->value==C::SemanticCertainty::Contradictory)
        { result.rejected.Push(candidate.key); result.contradiction=true; continue; }
        ordered[count++]=&candidate;
    }
    std::sort(ordered.begin(),ordered.begin()+count,[](auto* a, auto* b){return KeyLess(a->key,b->key);});
    std::sort(result.unknowns.begin(),result.unknowns.end(),[](const auto& a,const auto& b){return KeyLess(a.first,b.first);});
    if (result.unknowns.Size()) result.value=C::OptionalFact<T>::FromUnknown(result.unknowns.Get(0)->second);
    const FactCandidate<T>* selected=nullptr;
    for (std::size_t i=0;i<count;++i)
    {
        const auto* candidate=ordered[i];
        bool dominated=false;
        for (std::size_t j=0;j<count;++j)
            if (i!=j && !Related(*ordered[j],*candidate) &&
                Dominates(*ordered[j]->evidence,*candidate->evidence)) dominated=true;
        if (dominated) {result.rejected.Push(candidate->key);continue;}
        bool duplicate=false;
        for (std::size_t j=0;j<i;++j)
            if (Related(*ordered[j],*candidate) &&
                equal(ordered[j]->value.KnownPart()->value,candidate->value.KnownPart()->value)) duplicate=true;
        if (!duplicate) result.sources.Push(candidate->key);
        if (!selected) selected=candidate;
        else if (!equal(candidate->value.KnownPart()->value,selected->value.KnownPart()->value))
            result.contradiction=true;
    }
    if (result.contradiction)
    {
        result.value=C::OptionalFact<T>::FromUnknown(Missing("CTX.ContradictoryEvidence",C::UnknownReason::ContradictoryEvidence));
        for (const auto& unknown:result.unknowns) if (unknown.second.reason.code==C::UnknownReason::ContradictoryEvidence)
        {result.value=C::OptionalFact<T>::FromUnknown(unknown.second);break;}
    }
    else if (selected)
    {
        result.value=selected->value;
        // Disjoint lineage keys alone do not prove independent acquisition paths.
        // Preserve distinct surviving sources, collapse proven aliases, and never manufacture strength.
        result.independentSources=1;
    }
    return result;
}
} // namespace Neurotic::Context
