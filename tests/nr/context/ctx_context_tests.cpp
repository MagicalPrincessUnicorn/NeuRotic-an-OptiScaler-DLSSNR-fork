#include "../contracts/Test002Support.h"
#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<nr/context/ContextBuilder.h>)
#include <nr/context/ContextBuilder.h>
#include "ContextFixtureStore.h"
#define NR_CTX_BUILDER 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{
        Report r("ctx-context",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_CTX_BUILDER
        namespace S=Neurotic::Context;
        using namespace NrContextTests;
        Store store;
        S::ContextRequest request;
        request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
        request.frame=store.Publish(Frame(),C::OwnerDomain::IdentityRegistry);
        request.generations=store.Publish(C::GenerationVector{},C::OwnerDomain::IdentityRegistry);
        request.renderRaster=store.Publish(Analytic::Raster({0,0,800,400},{1024,512}));
        request.outputRaster=request.renderRaster;
        request.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);
        request.boundary.episode=Frame().episodeId;
        C::EvidenceVector evidence;
        evidence.provenance=Known(C::SourceClass::Native);evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        evidence.ageInRealFrames=Known(uint64_t{0});evidence.coverage=Known(C::CoverageKind::Complete);
        evidence.access=Known(C::DescriptiveAccess::CallbackBorrow);evidence.lineage.Push(Key("native",1));
        request.continuity=store.Publish(evidence,C::OwnerDomain::ColorContinuity);
        C::AcquisitionCandidate candidate;
        candidate.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        candidate.provider=Key("provider",1);candidate.sourceSchema=Sym("ctx.fixture.source");
        candidate.sourceVersion=Known(C::VersionNumber{1});candidate.semantic=C::SemanticKind::Jitter;
        candidate.frame=request.frame;candidate.boundary=request.boundary;candidate.evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        candidate.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);candidate.jitter=Known(C::Vec2{0,0});
        candidate.rrActive=Known(false);
        DeclareJitterBasis(store,candidate,Analytic::Raster({0,0,800,400},{1024,512}));
        std::array<C::AcquisitionCandidate,1> candidates{candidate};
        C::ObservationSet observations;observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);
        observations.boundary=request.boundary;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;
        refs.Push({Sym(C::AcquisitionCandidate::WireName),candidate.header.record,candidate.header.revision});
        observations.candidates=PublishList(store,refs);
        auto built=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T04.context-known-zero",built.context && built.context->Value().jitter.IsKnown() && built.context->Value().jitter.KnownPart()->value==C::Vec2{0,0},true,"BuildCanonicalFrameContext");
        r.Check("T04.context-known-false",built.context && built.context->Value().rrActive.IsKnown() && !built.context->Value().rrActive.KnownPart()->value,true,"BuildCanonicalFrameContext");
        r.Check("T04.context-missing-exposure",built.context && !built.context->Value().exposure.IsKnown(),true,"BuildCanonicalFrameContext");
        candidates[0].claims={};
        const auto unknownBasis=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T11.jitter-missing-basis-unknown",unknownBasis.context && !unknownBasis.context->Value().jitter.IsKnown() && candidates[0].jitter.IsKnown(),true,"BuildCanonicalFrameContext");
        candidates[0]=candidate;candidates[0].jitter=Known(C::Vec2{4,-2});
        const auto pixelJitter=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T11.jitter-pixels-normalized",pixelJitter.context && pixelJitter.context->Value().jitter.IsKnown() && pixelJitter.context->Value().jitter.KnownPart()->value==C::Vec2{.005,-.005},true,"BuildCanonicalFrameContext");
        candidates[0]=candidate;
        auto changed=request;changed.frame=store.Publish(C::FrameIdentity{},C::OwnerDomain::IdentityRegistry);
        const auto unproven=S::BuildCanonicalFrameContext(observations,candidates,changed,store);
        r.Check("T07.unproven-scope-no-admission",unproven.context && !unproven.context->Value().jitter.IsKnown() && unproven.context->Value().coherentCandidates.count==0,true,"BuildCanonicalFrameContext");
        candidates[0].header.revision=2;
        const auto revisionMismatch=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T06.candidate-revision-mismatch",revisionMismatch.context && revisionMismatch.context->Value().coherentCandidates.count==0,true,"BuildCanonicalFrameContext");
        candidates[0]=candidate;store.Withdraw(candidate.evidence);
        const auto evidenceMissing=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T06.evidence-disappeared",evidenceMissing.context && evidenceMissing.context->Value().coherentCandidates.count==0,true,"BuildCanonicalFrameContext");
        candidates[0].evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        candidates[0].jitter={};
        const auto incomplete=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        r.Check("T04.incomplete-observation-preserved",incomplete.context && !incomplete.context->Value().jitter.IsKnown(),true,"BuildCanonicalFrameContext");
        C::SemanticClaim claim;claim.field=Sym("exposure");claim.raw=Known(C::ScalarValue{2.0});claim.effective=Known(C::ScalarValue{3.0});
        C::BoundedList<C::SemanticClaim,16> claims;claims.Push(claim);candidates[0].claims=NrContextTests::PublishList(store,claims);
        C::MaskSet masks;C::TypedMask<C::MaskPurpose::Reactive> reactive;
        reactive.source={C::ContractId::C01,Sym(C::AcquisitionCandidate::WireName),candidate.header.record,1};
        reactive.coverage=Known(C::Rectangle{0,0,800,400});masks.reactive=Known(reactive);
        candidates[0].masks=store.Publish(masks,C::OwnerDomain::Provider);
        const auto rich=S::BuildCanonicalFrameContext(observations,candidates,request,store);
        const C::BoundedList<C::SemanticClaim,16>* outputFields=nullptr;
        r.Check("T04.raw-effective-preserved",rich.context && S::ResolveList(rich.context->Value().fields,store,outputFields) && outputFields && outputFields->Size()==1 && outputFields->Get(0)->raw==claim.raw && outputFields->Get(0)->effective==claim.effective,true,"BuildCanonicalFrameContext");
        const auto* outputMasks=rich.context?S::ResolveMetadata(rich.context->Value().masks,store):nullptr;
        r.Check("T12.typed-mask-preserved",outputMasks && outputMasks->reactive.IsKnown() && !outputMasks->nrApplication.IsKnown(),true,"BuildCanonicalFrameContext");
        auto second=candidates[0];second.header.record=Key("ctx.fixture.record",4);
        const auto colorResource=store.Publish(Resource(1,Analytic::Raster({0,0,800,400},{1024,512})),C::OwnerDomain::Resource);
        candidates[0].payload=Known(C::CandidateValue{colorResource});second.payload=candidates[0].payload;
        candidates[0].semantic=C::SemanticKind::Color;second.semantic=C::SemanticKind::Color;
        C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);
        candidates[0].color=Known(store.Publish(color,C::OwnerDomain::Provider));
        color.domain=C::OptionalFact<C::ColorDomain>::FromKnown(C::ColorDomain::SceneLinear,Ev(100));
        second.color=Known(store.Publish(color,C::OwnerDomain::Provider));
        std::array<C::AcquisitionCandidate,2> pair{candidates[0],second};
        refs.Push({Sym(C::AcquisitionCandidate::WireName),second.header.record,1});observations.candidates=NrContextTests::PublishList(store,refs);
        const auto equal=S::BuildCanonicalFrameContext(observations,pair,request,store);
        r.Check("T05.equivalent-description-provenance",equal.context && equal.context->Value().color.IsKnown(),true,"BuildCanonicalFrameContext");
        pair[1].color=C::OptionalFact<C::MetadataRef<C::ColorDescription>>::FromUnknown(S::Missing("CTX.ContradictoryEvidence",C::UnknownReason::ContradictoryEvidence));
        const auto ownerConflict=S::BuildCanonicalFrameContext(observations,pair,request,store);
        r.Check("T05.owner-descriptor-conflict-retained",ownerConflict.context && !ownerConflict.context->Value().color.IsKnown() && ownerConflict.context->Value().color.UnknownPart()->reason.code==C::UnknownReason::ContradictoryEvidence,true,"BuildCanonicalFrameContext");
        color.domain=Known(C::ColorDomain::DisplayLinear);pair[1].color=Known(store.Publish(color,C::OwnerDomain::Provider));
        const auto conflict=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const auto* conflictColor=conflict.context?S::ResolveOptional(conflict.context->Value().color,store):nullptr;
        r.Check("T05.description-conflict-retained",conflictColor && !conflictColor->domain.IsKnown() && conflict.context->Value().contradictions.count==1,true,"BuildCanonicalFrameContext");
        std::swap(pair[0],pair[1]);
        const auto permuted=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const auto* permutedColor=permuted.context?S::ResolveOptional(permuted.context->Value().color,store):nullptr;
        r.Check("T05.description-conflict-permuted",permutedColor && !permutedColor->domain.IsKnown() && permuted.context->Value().contradictions.count==1,true,"BuildCanonicalFrameContext");
        C::ColorDescription leftColor;leftColor.domain=Known(C::ColorDomain::SceneLinear);leftColor.alpha=Known(C::AlphaMeaning::Opaque);
        C::ColorDescription rightColor=leftColor;rightColor.alpha=Known(C::AlphaMeaning::Straight);
        pair[0].color=Known(store.Publish(leftColor));pair[1].color=Known(store.Publish(rightColor));
        const auto alphaConflict=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const auto* separated=alphaConflict.context?S::ResolveOptional(alphaConflict.context->Value().color,store):nullptr;
        r.Check("T05.alpha-conflict-keeps-domain",separated && separated->domain.IsKnown() && !separated->alpha.IsKnown(),true,"BuildCanonicalFrameContext");
        C::ResourceView guide;guide.raster=Analytic::Raster({100,50,800,400},{1024,512});
        pair[0].semantic=C::SemanticKind::Depth;
        const auto guideRef=store.Publish(guide,C::OwnerDomain::Resource);
        pair[0].payload=Known(C::CandidateValue{guideRef});
        const auto withGuide=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const C::BoundedList<C::GuideDescription,8>* guides=nullptr;
        bool matchedGuide=false;
        if (withGuide.context && S::ResolveList(withGuide.context->Value().guides,store,guides) && guides)
            for (const auto& entry:*guides) if (entry.kind==C::SemanticKind::Depth && entry.raster==guide.raster) matchedGuide=true;
        r.Check("T11.guide-raster-preserved",matchedGuide,true,"BuildCanonicalFrameContext");
        store.Withdraw(guideRef);
        const auto missingGuide=S::BuildCanonicalFrameContext(observations,pair,request,store);
        r.Check("T06.missing-resource-body-no-guide",missingGuide.context && missingGuide.context->Value().guides.count==1,true,"BuildCanonicalFrameContext");
        pair[0]=candidates[0];pair[1]=second;
        pair[0].jitter=Known(C::Vec2{0,0});DeclareJitterBasis(store,pair[0],Analytic::Raster({0,0,800,400},{1024,512}));
        auto otherFrame=Frame();otherFrame.viewId=Known(Id<C::ViewId>("other-view",1));
        pair[1].frame=store.Publish(otherFrame,C::OwnerDomain::IdentityRegistry);
        const auto unrelated=S::BuildCanonicalFrameContext(observations,pair,request,store);
        r.Check("T08.unrelated-view-cannot-poison",unrelated.context && unrelated.context->Value().jitter.IsKnown() && unrelated.context->Value().coherentCandidates.count==1,true,"BuildCanonicalFrameContext");
        pair[1].frame=request.frame;pair[0].semantic=C::SemanticKind::Depth;pair[1].semantic=C::SemanticKind::Depth;
        C::DepthDescription rawDepth;rawDepth.kind=Known(C::DepthKind::Device);
        auto projection=Analytic::Cases().at(10).input.depth;projection.kind={};
        pair[0].depth=Known(store.Publish(rawDepth));pair[1].depth=Known(store.Publish(projection));
        pair[0].payload=Known(C::CandidateValue{colorResource});
        pair[1].payload=Known(C::CandidateValue{store.Publish(Resource(2,Analytic::Raster({0,0,800,400},{1024,512})),C::OwnerDomain::Resource)});
        const auto separate=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const auto* separateDepth=separate.context?S::ResolveOptional(separate.context->Value().depth,store):nullptr;
        r.Check("T12.unrelated-resource-projection-not-combined",separate.context && (!separateDepth || !separateDepth->kind.IsKnown() || !separateDepth->projection.IsKnown()),true,"BuildCanonicalFrameContext");
        pair[1].payload=pair[0].payload;
        const auto joined=S::BuildCanonicalFrameContext(observations,pair,request,store);
        const auto* joinedDepth=joined.context?S::ResolveOptional(joined.context->Value().depth,store):nullptr;
        r.Check("T12.same-subject-description-combined",joinedDepth && joinedDepth->kind.IsKnown() && joinedDepth->projection.IsKnown(),true,"BuildCanonicalFrameContext");
        store.Withdraw(pair[0].depth.KnownPart()->value);
        const auto missingDescription=S::BuildCanonicalFrameContext(observations,pair,request,store);
        r.Check("T06.missing-optional-body-keeps-context",missingDescription.context && !missingDescription.context->Value().depth.IsKnown() && missingDescription.context->Value().jitter.IsKnown(),true,"BuildCanonicalFrameContext");
#else
        r.Check("CTX.builder.production-binding",false,true,"Missing ContextBuilder implementation");
#endif
        return r.Finish();
    });
}
