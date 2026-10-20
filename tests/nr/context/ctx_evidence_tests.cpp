#include "../contracts/Test002Support.h"
#if __has_include(<nr/context/EvidenceQualification.h>)
#include <nr/context/EvidenceQualification.h>
#define NR_CTX_EVIDENCE 1
#endif
using namespace NrTest002;
int main(int argc, char** argv)
{
    return Guard([&] {
        Report r("ctx-evidence", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
#ifdef NR_CTX_EVIDENCE
        namespace S = Neurotic::Context;
        C::EvidenceVector a;
        a.provenance = Known(C::SourceClass::Native);
        a.semanticCertainty = Known(C::SemanticCertainty::Qualified);
        a.ageInRealFrames = Known(uint64_t{0}); a.coverage = Known(C::CoverageKind::Complete);
        a.access = Known(C::DescriptiveAccess::CallbackBorrow); a.lineage.Push(Key("native.observation", 10));
        C::EvidenceVector b = a;
        std::array<S::FactCandidate<double>,2> input{{{Key("candidate",1),Known(0.0),&a},
                                                    {Key("candidate",2),Known(0.0),&b}}};
        auto result = S::ResolveEvidence<double>(input);
        r.Check("T04.known-zero", result.value.IsKnown() && result.value.KnownPart()->value == 0, true, "ResolveEvidence");
        r.Equal("T05.alias-single", result.independentSources, 1, "ResolveEvidence");
        b.lineage = {}; b.lineage.Push(Key("other.observation", 11));
        result = S::ResolveEvidence<double>(input);
        r.Equal("T05.independence-not-inferred", result.independentSources, 1, "ResolveEvidence");
        input[1].value = Known(1.0);
        r.Check("T05.equal-conflict", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        std::swap(input[0],input[1]);
        r.Check("T05.equal-conflict-permuted", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        b.semanticCertainty = Known(C::SemanticCertainty::Claimed);
        result = S::ResolveEvidence<double>(input);
        r.Check("T05.qualified-dominates-claim", result.value.IsKnown() && result.value.KnownPart()->value == 0, true, "ResolveEvidence");
        a.ageInRealFrames = Known(uint64_t{2});
        r.Check("T05.stale-cannot-dominate", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        a.ageInRealFrames = Known(uint64_t{0});
        input[0].value = {}; input[1].value = {};
        r.Check("T04.no-default", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        r.Check("T05.source-disappeared", S::ResolveEvidence<double>({}).value.IsKnown(), false, "ResolveEvidence");
        input[0]={Key("candidate",1),Known(0.0),&a};
        input[1]={Key("candidate",2),Known(1.0),&b};
        a.semanticCertainty=Known(C::SemanticCertainty::Contradictory);
        r.Check("T05.owner-contradiction-retained", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        a.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        b=a;
        input[0].value=Known(std::numeric_limits<double>::quiet_NaN());
        r.Check("T06.nonfinite-fact-refused", S::ResolveEvidence<double>(std::span(input).first(1)).value.IsKnown(), false, "ResolveEvidence");
        input[0].value=Known(0.0); input[1].key=input[0].key;
        r.Check("T05.same-record-conflict", S::ResolveEvidence<double>(input).value.IsKnown(), false, "ResolveEvidence");
        input[1].value=Known(0.0);
        r.Equal("T05.same-record-deduplicated", S::ResolveEvidence<double>(input).sources.Size(), 1, "ResolveEvidence");
        input[1].key=Key("candidate",2);
        r.Equal("T05.lineage-alias-deduplicated", S::ResolveEvidence<double>(input).sources.Size(), 1, "ResolveEvidence");
        auto absent=S::Missing("CTX.StaleContent",C::UnknownReason::Expired);
        absent.scope.key=Key("source.scope",5);absent.detail.Assign("source owner withdrew this observation");
        input[0].value=C::OptionalFact<double>::FromUnknown(absent);
        const auto retained=S::ResolveEvidence<double>(std::span(input).first(1));
        r.Check("T04.unknown-reason-scope-retained",!retained.value.IsKnown() && *retained.value.UnknownPart()==absent,true,"ResolveEvidence");
        input[0].value=C::OptionalFact<double>::FromUnknown(S::Missing("CTX.ContradictoryEvidence",C::UnknownReason::ContradictoryEvidence));
        r.Check("T05.unknown-conflict-not-erased",S::ResolveEvidence<double>(input).value.IsKnown(),false,"ResolveEvidence");
        C::FrameIdentity frame;
        frame.sessionId=Known(Id<C::SessionId>("session",1));
        frame.renderStreamId=Known(Id<C::RenderStreamId>("stream",1));
        frame.viewId=Known(Id<C::ViewId>("view",1)); frame.episodeId=Known(Id<C::EpisodeId>("episode",1));
        frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("base",1)); frame.associationEvidence.Push(Ev(22));
        auto other=frame;
        r.Check("T07.same-base", S::SameScope(frame,other), true, "SameScope");
        other.presentId=Known(Id<C::PresentId>("present",999));
        r.Check("T07.present-not-base", S::SameScope(frame,other), true, "SameScope");
        other.baseRealFrameId={};
        r.Check("T07.present-cannot-repair", S::SameScope(frame,other), false, "SameScope");
        other=frame; other.viewId=Known(Id<C::ViewId>("other-view",1));
        r.Check("T08.namespace-not-number", S::SameScope(frame,other), false, "SameScope");
        other=frame; other.viewId=Known(Id<C::ViewId>("view",2));
        r.Check("T08.view-separate", S::SameScope(frame,other), false, "SameScope");
        C::ResourceIdentityToken resource;
        resource.objectIncarnation=Known(C::StableIdentity{C::IdentityKind::ObjectIncarnation,Sym("object"),Sym("owner"),1});
        resource.resourceIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceIncarnation,Sym("resource"),Sym("owner"),1});
        resource.resourceViewIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceViewIncarnation,Sym("resource-view"),Sym("owner"),1});
        resource.resourceGeneration=Known(Gen(C::IdentityKind::ResourceGeneration,"resource",1));
        resource.representationGeneration=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",1));
        resource.contentRevision=Known(C::ContentRevision{1});
        auto changed=resource; changed.contentRevision=Known(C::ContentRevision{2});
        r.Check("T09.content-not-structure", S::SameResourceStructure(resource,changed), true, "SameResourceStructure");
        r.Check("T09.exact-content-refused", S::SameContent(resource.contentRevision,changed.contentRevision), false, "SameContent");
        changed.resourceIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceIncarnation,Sym("resource"),Sym("owner"),2});
        r.Check("T08.reincarnation-refused", S::SameResourceStructure(resource,changed), false, "SameResourceStructure");
#else
        r.Check("CTX.evidence.production-binding", false, true, "Missing EvidenceQualification implementation");
#endif
        return r.Finish();
    });
}
