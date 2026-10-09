#pragma once
// NR-LIFE-001 candidate. Canonical signature bodies, explicit selected axes, no cache or route policy.
#include "IdentityPrimitives.h"
#include "../contracts/SemanticDescriptions.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace Neurotic::Lifecycle
{
// A selector has no generation value. It names a required axis in the existing C14 vocabulary.
struct GenerationAxis
{
    C::IdentityKind kind=C::IdentityKind::ResourceGeneration;
    C::Symbol nameSpace {};
    C::Symbol issuer {};
    bool operator==(const GenerationAxis&) const=default;
};
inline bool SameAxis(const GenerationAxis& axis,const C::GenerationToken& token) noexcept
{
    return axis.kind==token.identity.kind && axis.nameSpace==token.identity.nameSpace && axis.issuer==token.identity.issuer;
}
inline bool ValidAxis(const GenerationAxis& axis) noexcept
{
    return C::IsGenerationKind(axis.kind) && !axis.nameSpace.Empty() && !axis.issuer.Empty();
}
struct SignatureSelection
{
    C::Symbol schema {};
    std::uint32_t ruleVersion=1;
    C::BoundedList<GenerationAxis,32> axes {};
    // Immutable structural owner records only (scope/profile/semantic policy versions).
    // Their meaning is declared by the consuming owner; never pass transient frame/evidence records.
    C::BoundedList<C::RecordKey,32> structuralRecords {};
};
struct SignaturePublication
{
    C::MetadataRef<C::GenerationVector> generations {};
    C::MetadataRef<C::BoundedList<C::RecordKey,32>> records {};
    C::MetadataRef<C::StructuralSignature> signature {};
};
// Native admission cannot assume the fixture codec has already checked a required enum.
// This validates descriptor structure only; it does not authenticate or discover an owner.
template<class T> bool ValidMetadataDescriptor(const C::MetadataRef<T>& reference)
{
    return !C::EnumName(reference.owner).empty() &&
           reference.Check()==C::Error::None && reference.record.Check()==C::Error::None &&
           reference.schemaVersion.Check()==C::Error::None;
}
// Construction bundle for existing canonical records, not a wire schema or metadata store.
// An absent outer optional means the body is unavailable. A present pair contains the exact
// supplied body publication and its values, including an explicitly established empty body.
// The empty canonical MetadataList has no backing ref, hence the inner dependency optional.
// Supplied references are owner assertions, not authentication. The owner must publish those
// exact immutable values under those revisions; no resolver, retention or resource right exists here.
struct ResolvedStructuralSignature
{
    C::MetadataRef<C::StructuralSignature> publication {};
    C::StructuralSignature record {};
    std::optional<std::pair<C::MetadataRef<C::GenerationVector>,C::GenerationVector>> generationBody;
    std::optional<std::pair<std::optional<C::MetadataRef<C::BoundedList<C::RecordKey,32>>>,
                            C::BoundedList<C::RecordKey,32>>> dependencyBody;
    bool operator==(const ResolvedStructuralSignature&) const=default;
};
inline IdentityStatus CheckSignature(const ResolvedStructuralSignature& s)
{
    if (!ValidMetadataDescriptor(s.publication) || !ValidMetadataDescriptor(s.record.generations) ||
        s.record.schema.Empty() || s.record.version==0 ||
        s.record.structuralDependencies.Check()!=C::Error::None) return IdentityStatus::InvalidRule;
    if (s.record.structuralDependencies.backing &&
        !ValidMetadataDescriptor(*s.record.structuralDependencies.backing)) return IdentityStatus::InvalidRule;
    // Availability is independent of collection length. Never interpret default storage as empty truth.
    if (!s.generationBody || !s.dependencyBody) return IdentityStatus::Unknown;
    if (!ValidMetadataDescriptor(s.generationBody->first) ||
        (s.dependencyBody->first && !ValidMetadataDescriptor(*s.dependencyBody->first)))
        return IdentityStatus::InvalidRule;
    // Exact descriptor equality includes owner, key/issuer/namespace, revision and schema version.
    if (s.generationBody->first!=s.record.generations ||
        s.dependencyBody->first!=s.record.structuralDependencies.backing) return IdentityStatus::InvalidRule;
    if (s.record.structuralDependencies.count!=s.dependencyBody->second.Size()) return IdentityStatus::InvalidRule;
    for (const auto& r:s.dependencyBody->second) if (r.Check()!=C::Error::None) return IdentityStatus::InvalidRule;
    return IdentityStatus::Ok;
}
inline IdentityStatus BuildSignature(const C::GenerationVector& available,const SignatureSelection& selection,
                                      const SignaturePublication& publication,ResolvedStructuralSignature& output)
{
    if (selection.schema.Empty() || selection.ruleVersion==0) return IdentityStatus::InvalidRule;
    for (std::size_t i=0;i<selection.axes.Size();++i)
    {
        const auto& axis=*selection.axes.Get(i);
        if (!C::IsGenerationKind(axis.kind)) return IdentityStatus::WrongKind;
        if (!ValidAxis(axis)) return IdentityStatus::InvalidRule;
        for (std::size_t j=0;j<i;++j) if (axis==*selection.axes.Get(j)) return IdentityStatus::InvalidRule;
        bool found=false;
        for (const auto& token:available.Entries()) if (SameAxis(axis,token)) { found=true;break; }
        if (!found) return IdentityStatus::Unknown;
    }
    for (std::size_t i=0;i<selection.structuralRecords.Size();++i)
    {
        const auto& r=*selection.structuralRecords.Get(i);
        if (r.Check()!=C::Error::None) return IdentityStatus::InvalidRule;
        for (std::size_t j=0;j<i;++j) if (r==*selection.structuralRecords.Get(j)) return IdentityStatus::InvalidRule;
    }
    if (!ValidMetadataDescriptor(publication.signature) || !ValidMetadataDescriptor(publication.generations))
        return IdentityStatus::InvalidRule;
    if (selection.structuralRecords.Size()!=0 && !ValidMetadataDescriptor(publication.records))
        return IdentityStatus::InvalidRule;
    // One bounded staging value; no heap, partially published destination, or metadata lookup.
    // Actual target size/stack behavior remains a machine measurement obligation.
    ResolvedStructuralSignature staged;
    staged.publication=publication.signature;
    staged.record.schema=selection.schema;staged.record.version=selection.ruleVersion;
    staged.record.generations=publication.generations;
    // Calling this builder supplies an established available vector and explicit selected records.
    // For descriptor resolution without those bodies, leave the corresponding outer optional absent.
    staged.generationBody.emplace();staged.generationBody->first=publication.generations;
    staged.dependencyBody.emplace();staged.dependencyBody->second=selection.structuralRecords;
    std::sort(staged.dependencyBody->second.begin(),staged.dependencyBody->second.end(),[](const C::RecordKey& a,const C::RecordKey& b) {
        return std::tuple{a.nameSpace.View(),a.issuer.View(),a.value}<std::tuple{b.nameSpace.View(),b.issuer.View(),b.value};
    });
    staged.record.structuralDependencies.count=static_cast<std::uint32_t>(staged.dependencyBody->second.Size());
    if (staged.dependencyBody->second.Size()!=0)
    {
        staged.record.structuralDependencies.backing=publication.records;
        staged.dependencyBody->first=publication.records;
    }
    for (const auto& axis:selection.axes)
        for (const auto& token:available.Entries()) if (SameAxis(axis,token))
        {
            if (staged.generationBody->second.Insert(token)!=C::Error::None) return IdentityStatus::InvalidRule;
            break;
        }
    const auto checked=CheckSignature(staged);
    if (checked!=IdentityStatus::Ok) return checked;
    output=staged; // A refusal never publishes a partial destination.
    return IdentityStatus::Ok;
}
inline IdentityStatus CompareSignatures(const ResolvedStructuralSignature& a,const ResolvedStructuralSignature& b)
{
    const auto aStatus=CheckSignature(a);if (aStatus!=IdentityStatus::Ok) return aStatus;
    const auto bStatus=CheckSignature(b);if (bStatus!=IdentityStatus::Ok) return bStatus;
    // Exact referents are bound above; re-publication does not change semantic identity.
    return a.record.schema==b.record.schema && a.record.version==b.record.version &&
           a.dependencyBody->second==b.dependencyBody->second && a.generationBody->second==b.generationBody->second
        ? IdentityStatus::Ok : IdentityStatus::Different;
}
struct ContentRequirements
{
    bool requireObject=true;
    bool requireView=true;
};
inline IdentityStatus CompareResourceIdentityFact(const C::OptionalFact<C::StableIdentity>& a,
                                                  const C::OptionalFact<C::StableIdentity>& b,C::IdentityKind kind)
{
    if (!a.IsKnown() || !b.IsKnown()) return IdentityStatus::Unknown;
    if (a.KnownPart()->value.kind!=kind || b.KnownPart()->value.kind!=kind) return IdentityStatus::WrongKind;
    return CompareIdentity(a,b);
}
// Contents only. This is NOT C03 admission: no queue/order/replay/retention/reuse fact is inspected.
// Resource structural generations are deliberately checked through a separate declared signature.
inline IdentityStatus CompareExactContent(const C::ResourceIdentityToken& expected,const C::ResourceIdentityToken& current,
                                           ContentRequirements requirements)
{
    if (expected.Check()!=C::Error::None || current.Check()!=C::Error::None) return IdentityStatus::WrongKind;
    auto status=CompareResourceIdentityFact(expected.resourceIncarnation,current.resourceIncarnation,C::IdentityKind::ResourceIncarnation);
    if (status!=IdentityStatus::Ok) return status;
    if (requirements.requireObject)
    {
        status=CompareResourceIdentityFact(expected.objectIncarnation,current.objectIncarnation,C::IdentityKind::ObjectIncarnation);
        if (status!=IdentityStatus::Ok) return status;
    }
    if (requirements.requireView)
    {
        status=CompareResourceIdentityFact(expected.resourceViewIncarnation,current.resourceViewIncarnation,C::IdentityKind::ResourceViewIncarnation);
        if (status!=IdentityStatus::Ok) return status;
    }
    if (!expected.contentRevision.IsKnown() || !current.contentRevision.IsKnown()) return IdentityStatus::Unknown;
    return expected.contentRevision.KnownPart()->value==current.contentRevision.KnownPart()->value
        ? IdentityStatus::Ok : IdentityStatus::Different;
}
inline IdentityStatus CompareHistoryDependencies(const C::HistoryKey& a,const ResolvedStructuralSignature& aBody,
                                                 const C::HistoryKey& b,const ResolvedStructuralSignature& bBody)
{
    if (a.owner.Check()!=C::Error::None || b.owner.Check()!=C::Error::None || a.slot.Empty() || b.slot.Empty() ||
        !ValidMetadataDescriptor(a.dependencies) || !ValidMetadataDescriptor(b.dependencies))
        return IdentityStatus::InvalidRule;
    // The caller must supply the exact resolved publication named by each HistoryKey, not
    // an unrelated compatible-looking body. Different publications may still have equal semantics.
    if (a.dependencies!=aBody.publication || b.dependencies!=bBody.publication) return IdentityStatus::InvalidRule;
    const auto aStatus=CheckSignature(aBody);if (aStatus!=IdentityStatus::Ok) return aStatus;
    const auto bStatus=CheckSignature(bBody);if (bStatus!=IdentityStatus::Ok) return bStatus;
    if (a.owner!=b.owner || a.slot!=b.slot) return IdentityStatus::Different;
    const auto lineage=CompareTypedIdentity(a.generation,b.generation);
    return lineage==IdentityStatus::Ok ? CompareSignatures(aBody,bBody) : lineage;
}
} // namespace Neurotic::Lifecycle
