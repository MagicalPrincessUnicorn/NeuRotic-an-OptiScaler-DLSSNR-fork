#pragma once
// NR-LIFE-001 authored CPU fixture support. NOT_RUN; not an operational issuer.
#include <nr/lifecycle/IdentityIssuers.h>
#include <nr/lifecycle/IdentityAssociations.h>
#include <nr/lifecycle/IdentityDependencies.h>
#include <nr/lifecycle/IdentityTransitions.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Engineering ceilings only. Target compiler sizes and automatic-storage paths remain NOT_MEASURED.
static_assert(sizeof(Neurotic::Lifecycle::HostIdentityDomain)<=256*1024);
static_assert(sizeof(Neurotic::Lifecycle::ResolvedStructuralSignature)<=32*1024);
static_assert(sizeof(Neurotic::Lifecycle::AssociationProjection)<=32*1024);
namespace LifeFixture
{
namespace C = Neurotic::Contracts;
namespace L = Neurotic::Lifecycle;
inline C::Symbol Sym(std::string_view text)
{
    C::Symbol s;
    if (!s.Assign(text)) std::abort(); // Invalid literal is a fixture-author error.
    return s;
}
inline C::RecordKey Key(std::string_view ns, std::uint64_t value = 1)
{
    return {Sym(ns), Sym("fixture.owner"), value};
}
inline C::EvidenceRef Ev(std::uint64_t value)
{
    return {Key("fixture.evidence", value)};
}
template<class T> T Id(std::string_view ns, std::uint64_t value)
{
    return {Sym(ns), Sym("fixture.owner"), value};
}
template<class T> C::OptionalFact<T> Known(const T& value, std::uint64_t ev = 1)
{
    return C::OptionalFact<T>::FromKnown(value, Ev(ev));
}
template<class T> const T& Value(const L::Outcome<T>& result)
{
    if (result.status != L::IdentityStatus::Ok || !result.value.IsKnown()) std::abort();
    return result.value.KnownPart()->value;
}
inline L::OwnerEvent Event(C::OwnerDomain owner, const C::RecordKey& subject, std::uint64_t serial)
{
    return {owner, Key("fixture.publisher"), subject, Ev(serial)};
}
// The fixture's owner-authorized boundary explicitly binds a separate evidence namespace.
inline L::OwnerBinding Binding(C::OwnerDomain owner, std::string_view subject)
{
    return {owner, Key("fixture.publisher"), Key(subject), Sym("fixture.evidence")};
}
inline L::ScopeSnapshot Scope(std::string_view stream = "stream.A", std::string_view view = "view.main",
                             std::uint64_t incarnation = 1)
{
    return {Known(Id<C::SessionId>("session", 1)), Known(Id<C::RenderStreamId>(stream, 1)),
            Known(Id<C::ViewId>(view, 1)), Known(Id<C::ObjectIncarnation>("swapchain", incarnation))};
}
inline L::AssociationAuthority Authority(C::OwnerDomain owner, const L::ScopeSnapshot& scope)
{
    return {Binding(owner, "association.scope"), scope, true, {}, std::nullopt};
}
inline L::AssociationClaim Edge(L::AssociationRelation relation, const C::StableIdentity& source,
                               const C::StableIdentity& target, const L::AssociationAuthority& authority)
{
    return {relation, Known(source), Known(target), authority.currentScope, authority.currentScope,
            Event(authority.binding.owner, authority.binding.subject, 10), std::nullopt};
}
struct Checks
{
    unsigned failures = 0;
    unsigned cases = 0;
    void Equal(std::string_view id, std::uint64_t actual, std::uint64_t expected)
    {
        ++cases;
        std::cout << id << "\texpected=" << expected << "\tactual=" << actual << '\n';
        if (actual != expected) ++failures;
    }
    void Is(std::string_view id, bool actual, bool expected = true)
    {
        Equal(id, actual ? 1u : 0u, expected ? 1u : 0u);
    }
    void Status(std::string_view id, L::IdentityStatus actual, L::IdentityStatus expected)
    {
        ++cases;
        std::cout << id << "\texpected=" << L::StatusName(expected)
                  << "\tactual=" << L::StatusName(actual) << '\n';
        if (actual != expected) ++failures;
    }
    int Finish() const
    {
        std::cout << "cases=" << cases << "\tmismatches=" << failures
                  << "\tevidence=CPU_FIXTURE_ONLY\n" << std::flush;
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
};
// Source-only 03B assertions. Expectations are supplied by the fixture, never by the SUT.
template<class T> bool HasValue(const L::Outcome<T>& result) noexcept
{
    return result.status==L::IdentityStatus::Ok && result.value.IsKnown();
}
// Shared literal envelope only: there is deliberately no endpoint-kind projection here.
inline L::AssociationProjection ExpectedProjection(L::AssociationRelation relation,const L::ScopeSnapshot& scope)
{
    L::AssociationProjection expected;
    expected.relation=relation;
    expected.frame.sessionId=scope.session;
    expected.frame.renderStreamId=scope.stream;
    expected.frame.viewId=scope.view;
    if (!expected.frame.associationEvidence.Push(Ev(10))) std::abort();
    return expected;
}
// Each canonical OptionalFact equality compares its discriminant, value and evidence. Typed
// values retain kind/namespace/issuer/value. Default expected fields explicitly remain Unknown.
// These are field comparisons, not a second implementation of ProjectFrameEndpoint.
inline bool CheckProjection(Checks& c,std::string_view prefix,
    const L::Outcome<L::AssociationProjection>& result,const L::AssociationProjection& expected)
{
    const std::string id(prefix);
    c.Status(id+".status",result.status,L::IdentityStatus::Ok);
    c.Is(id+".known",result.value.IsKnown());
    if (!HasValue(result)) return false;
    const auto& actual=result.value.KnownPart()->value;
    c.Is(id+".relation",actual.relation==expected.relation);
    c.Is(id+".sessionId",actual.frame.sessionId==expected.frame.sessionId);
    c.Is(id+".renderStreamId",actual.frame.renderStreamId==expected.frame.renderStreamId);
    c.Is(id+".viewId",actual.frame.viewId==expected.frame.viewId);
    c.Is(id+".episodeId",actual.frame.episodeId==expected.frame.episodeId);
    c.Is(id+".baseRealFrameId",actual.frame.baseRealFrameId==expected.frame.baseRealFrameId);
    c.Is(id+".renderId",actual.frame.renderId==expected.frame.renderId);
    c.Is(id+".evaluationId",actual.frame.evaluationId==expected.frame.evaluationId);
    c.Is(id+".presentId",actual.frame.presentId==expected.frame.presentId);
    c.Is(id+".generatedFrameId",actual.frame.generatedFrameId==expected.frame.generatedFrameId);
    c.Is(id+".finalSealId",actual.frame.finalSealId==expected.frame.finalSealId);
    c.Is(id+".associationEvidence",actual.frame.associationEvidence==expected.frame.associationEvidence);
    c.Is(id+".repeatedFrom",actual.repeatedFrom==expected.repeatedFrom);
    c.Is(id+".sourceRealFrame",actual.sourceRealFrame==expected.sourceRealFrame);
    c.Is(id+".resource",actual.resource==expected.resource);
    return true; // A readable result, not a claim that every field assertion matched.
}
inline C::GenerationToken Gen(C::IdentityKind kind, std::string_view ns, std::uint64_t value)
{
    return {{kind, Sym(ns), Sym("fixture.owner"), value}};
}
inline C::GenerationVector Generations(std::uint64_t resource, std::uint64_t representation,
                                       std::uint64_t route, std::uint64_t fg)
{
    C::GenerationVector g;
    for (const auto& t : {Gen(C::IdentityKind::ResourceGeneration,"resource.family",resource),
                          Gen(C::IdentityKind::RepresentationGeneration,"representation",representation),
                          Gen(C::IdentityKind::RouteGeneration,"route",route),
                          Gen(C::IdentityKind::FgPolicyGeneration,"fg.policy",fg)})
        if (g.Insert(t) != C::Error::None) std::abort();
    return g;
}
inline L::SignatureSelection Selection(bool includeResource = true, bool includeRepresentation = true)
{
    L::SignatureSelection s;
    s.schema = Sym("LIFE.fixture.route-dependencies");
    if (includeResource && !s.axes.Push({C::IdentityKind::ResourceGeneration, Sym("resource.family"), Sym("fixture.owner")})) std::abort();
    if (includeRepresentation && !s.axes.Push({C::IdentityKind::RepresentationGeneration, Sym("representation"), Sym("fixture.owner")})) std::abort();
    if (!s.axes.Push({C::IdentityKind::RouteGeneration, Sym("route"), Sym("fixture.owner")})) std::abort();
    if (!s.structuralRecords.Push(Key("stable.stream-view-profile", 1))) std::abort();
    return s;
}
inline L::SignaturePublication Publication(std::uint64_t revision = 1)
{
    return {{C::OwnerDomain::StreamCoordinator,Key("metadata.generations"),revision,{}},
            {C::OwnerDomain::StreamCoordinator,Key("metadata.dependencies"),revision,{}},
            {C::OwnerDomain::StreamCoordinator,Key("metadata.signature"),revision,{}}};
}
inline C::ResourceIdentityToken Resource(std::uint64_t incarnation, std::uint64_t content)
{
    C::ResourceIdentityToken r;
    r.objectIncarnation = Known(Id<C::ObjectIncarnation>("objects",incarnation).Describe());
    r.resourceIncarnation = Known(Id<C::ResourceIncarnation>("resources",incarnation).Describe());
    r.resourceViewIncarnation = Known(Id<C::ResourceViewIncarnation>("resource.views",3).Describe());
    r.resourceGeneration = Known(Gen(C::IdentityKind::ResourceGeneration,"resource.family",42));
    r.representationGeneration = Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",7));
    r.contentRevision = Known(C::ContentRevision{content});
    return r;
}
} // namespace LifeFixture
