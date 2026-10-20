#include "../contracts/Test002Support.h"
#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<nr/context/CapabilityQualification.h>)
#include <nr/context/CapabilityQualification.h>
#include "ContextFixtureStore.h"
#define NR_CTX_CAPABILITY 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{
        Report r("ctx-capability",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_CTX_CAPABILITY
        namespace S=Neurotic::Context;using namespace NrContextTests;
        Store store;S::ContextRequest request;
        request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
        request.frame=store.Publish(Frame(),C::OwnerDomain::IdentityRegistry);
        request.generations=store.Publish(C::GenerationVector{},C::OwnerDomain::IdentityRegistry);
        request.renderRaster=store.Publish(Analytic::Raster({0,0,800,400},{1024,512}));request.outputRaster=request.renderRaster;
        request.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);request.boundary.episode=Frame().episodeId;
        C::EvidenceVector evidence;evidence.provenance=Known(C::SourceClass::Native);
        evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);evidence.ageInRealFrames=Known(uint64_t{0});
        evidence.coverage=Known(C::CoverageKind::Complete);evidence.access=Known(C::DescriptiveAccess::CallbackBorrow);
        evidence.lineage.Push(Key("native",1));request.continuity=store.Publish(evidence,C::OwnerDomain::ColorContinuity);
        C::AcquisitionCandidate source;source.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        source.provider=Key("provider",1);source.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
        source.sourceSchema=Sym("ctx.fixture.source");source.sourceVersion=Known(C::VersionNumber{1,0});
        source.semantic=C::SemanticKind::Jitter;source.frame=request.frame;source.boundary=request.boundary;
        source.evidence=store.Publish(evidence,C::OwnerDomain::Provider);source.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);
        source.jitter=Known(C::Vec2{0,0});source.contentRevision=Known(C::ContentRevision{1});
        DeclareJitterBasis(store,source,Analytic::Raster({0,0,800,400},{1024,512}));
        C::DepthDescription depth;depth.kind=Known(C::DepthKind::Device);source.depth=Known(store.Publish(depth));
        std::array<C::AcquisitionCandidate,1> sources{source};
        C::ObservationSet observations;observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);observations.boundary=request.boundary;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;refs.Push({Sym(C::AcquisitionCandidate::WireName),source.header.record,1});
        observations.candidates=NrContextTests::PublishList(store,refs);
        const auto built=S::BuildCanonicalFrameContext(observations,sources,request,store);
        r.Check("C04.fixture-context",bool(built.context),true,"BuildCanonicalFrameContext");
        if (!built.context) return r.Finish();
        const auto& context=built.context->Value();
        S::SemanticProfile profile;profile.key={Sym("test.consumer"),Sym("test.jitter"),1};profile.purpose=Sym("test.jitter");
        profile.rule=Sym("CTX.Semantics");profile.ruleVersion=1;profile.publication=Key("profile.policy",1);
        profile.maximumAge=Known(uint64_t{0});profile.acceptedEvidence.Push(C::SourceClass::Native);
        profile.sourceSchemas.Push({Sym("ctx.fixture.source"),{1,0}});
        profile.requirements.Push({Sym("jitter"),true});profile.boundary=C::BoundaryKind::BeforeUpscale;
        auto header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,4);
        const auto qualify=[&](const S::SemanticProfile& p){return S::EvaluateSemanticCapability(context,sources,p,header,store);};
        const auto eligible=qualify(profile);
        r.Check("C04.known-zero-eligible",eligible.certificate && eligible.certificate->Value().eligibility==C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        auto missing=profile;missing.requirements.Push({Sym("exposure.preExposure"),true});
        const auto missingResult=qualify(missing);
        r.Check("C04.required-exposure-unqualified",missingResult.certificate && missingResult.certificate->Value().eligibility==C::Eligibility::Ineligible && missingResult.certificate->Value().missingFields.count==1,true,"EvaluateSemanticCapability");
        auto optional=profile;optional.requirements.Push({Sym("exposure.preExposure"),false});
        const auto optionalResult=qualify(optional);
        r.Check("C04.optional-exposure-keeps-eligibility",optionalResult.certificate && optionalResult.certificate->Value().eligibility==C::Eligibility::Eligible && optionalResult.certificate->Value().reasons.count==1,true,"EvaluateSemanticCapability");
        auto raw=profile;raw.requirements.Push({Sym("depth.device"),true});const auto rawResult=qualify(raw);
        r.Check("C04.raw-depth-without-projection",rawResult.certificate && rawResult.certificate->Value().eligibility==C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        auto view=profile;view.requirements.Push({Sym("depth.view"),true});const auto viewResult=qualify(view);
        r.Check("C04.view-depth-needs-projection",viewResult.certificate && viewResult.certificate->Value().eligibility==C::Eligibility::Ineligible,true,"EvaluateSemanticCapability");
        auto unsupported=profile;unsupported.ruleVersion=2;const auto unsupportedResult=qualify(unsupported);
        r.Check("C04.unknown-rule-refused",!unsupportedResult.certificate || unsupportedResult.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        auto differentContext=context;++differentContext.header.revision;
        r.Check("C04.context-revision-invalidates",eligible.certificate && !S::CertificateMatches(eligible.certificate->Value(),differentContext,profile,store),true,"CertificateMatches");
        auto differentProfile=profile;++differentProfile.key.version;
        r.Check("C04.profile-version-invalidates",eligible.certificate && !S::CertificateMatches(eligible.certificate->Value(),context,differentProfile,store),true,"CertificateMatches");
        sources[0].sourceVersion=Known(C::VersionNumber{2,0});
        const auto futureSource=qualify(profile);
        r.Check("C04.future-source-schema",!futureSource.certificate || futureSource.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        sources[0]=source;evidence.ageInRealFrames=Known(uint64_t{1});
        sources[0].evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        const auto stale=qualify(profile);r.Check("C04.stale-source-refused",!stale.certificate || stale.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        const C::BoundedList<C::PolicyReason,16>* staleReasons=nullptr;
        r.Check("C04.stale-reason-retained",stale.certificate && S::ResolveList(stale.certificate->Value().reasons,store,staleReasons) && staleReasons && staleReasons->Get(0)->reason.ownerCode==S::Missing("CTX.StaleContent").reason.ownerCode,true,"EvaluateSemanticCapability");
        evidence.ageInRealFrames={};sources[0].evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        const auto unknownAge=qualify(profile);r.Check("C04.unknown-age-refused",!unknownAge.certificate || unknownAge.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        sources[0]=source;auto exact=profile;exact.requireExactContent=true;sources[0].contentRevision={};
        const auto unknownContent=qualify(exact);r.Check("C04.unknown-exact-content-refused",!unknownContent.certificate || unknownContent.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        sources[0]=source;++sources[0].header.revision;
        const auto replaced=qualify(profile);r.Check("C04.source-revision-invalidates",!replaced.certificate || replaced.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        sources[0]=source;
        auto unknownContext=context;auto ownerUnknown=S::Missing("CTX.OwnerProvidedAbsence",C::UnknownReason::OwnerUnpublished);
        ownerUnknown.detail.Assign("owner observation unavailable");
        unknownContext.jitter=C::OptionalFact<C::Vec2>::FromUnknown(ownerUnknown);
        const auto retainedUnknown=S::EvaluateSemanticCapability(unknownContext,sources,profile,header,store);
        const C::BoundedList<C::PolicyReason,16>* retainedReasons=nullptr;
        r.Check("C04.original-unknown-reason",retainedUnknown.certificate && S::ResolveList(retainedUnknown.certificate->Value().reasons,store,retainedReasons) && retainedReasons && retainedReasons->Get(0)->reason==ownerUnknown.reason,true,"EvaluateSemanticCapability");
        sources[0].contentRevision=Known(C::ContentRevision{2});
        const auto contentAdvanced=qualify(profile);
        r.Check("T09.content-keeps-structural-signature",eligible.certificate && contentAdvanced.certificate && S::CertificateMatches(contentAdvanced.certificate->Value(),context,profile,store) && S::CertificateMatches(eligible.certificate->Value(),context,profile,store),true,"EvaluateSemanticCapability/CertificateMatches");
        sources[0]=source;
        auto altered=context;altered.jitter=Known(C::Vec2{99,99});
        const auto fabricated=S::EvaluateSemanticCapability(altered,sources,profile,header,store);
        r.Check("C04.source-fact-binding",!fabricated.certificate || fabricated.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        auto oldExposure=source;oldExposure.header.record=Key("ctx.fixture.record",5);oldExposure.jitter={};oldExposure.motion={};oldExposure.claims={};oldExposure.depth={};
        oldExposure.semantic=C::SemanticKind::Exposure;
        auto oldEvidence=evidence;oldEvidence.ageInRealFrames=Known(uint64_t{1});oldEvidence.lineage={};oldEvidence.lineage.Push(Key("old-exposure",1));
        oldExposure.evidence=store.Publish(oldEvidence,C::OwnerDomain::Provider);
        C::ExposureDescription exposure;exposure.preExposure=Known(4.0);exposure.preExposureRelation=Known(Sym("stored.preExposure.times.radiance"));
        oldExposure.exposure=Known(store.Publish(exposure));
        std::array<C::AcquisitionCandidate,2> pair{source,oldExposure};
        auto pairObservations=observations;auto pairRefs=refs;pairRefs.Push({Sym(C::AcquisitionCandidate::WireName),oldExposure.header.record,1});
        pairObservations.candidates=NrContextTests::PublishList(store,pairRefs);
        const auto oldOptional=S::BuildCanonicalFrameContext(pairObservations,pair,request,store);
        const auto oldOptionalResult=oldOptional.context?S::EvaluateSemanticCapability(oldOptional.context->Value(),pair,profile,header,store):S::CapabilityResult{};
        r.Check("C04.unused-stale-source-not-veto",oldOptionalResult.certificate && oldOptionalResult.certificate->Value().eligibility==C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        pair[1]=source;pair[1].header.record=oldExposure.header.record;pair[1].jitter=Known(C::Vec2{8,4});
        auto claimed=oldEvidence;claimed.ageInRealFrames=Known(uint64_t{0});claimed.semanticCertainty=Known(C::SemanticCertainty::Claimed);
        pair[1].evidence=store.Publish(claimed,C::OwnerDomain::Provider);
        const auto dominated=S::BuildCanonicalFrameContext(pairObservations,pair,request,store);
        const auto dominatedResult=dominated.context?S::EvaluateSemanticCapability(dominated.context->Value(),pair,profile,header,store):S::CapabilityResult{};
        r.Check("C04.dominated-source-not-veto",dominatedResult.certificate && dominatedResult.certificate->Value().eligibility==C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        pair[0].rrActive=Known(true);pair[1].rrActive=Known(false);pair[1].evidence=oldExposure.evidence;
        auto rrProfile=profile;rrProfile.requirements={};rrProfile.requirements.Push({Sym("rrActive"),true});
        const auto allRr=S::BuildCanonicalFrameContext(pairObservations,pair,request,store);
        r.Check("C04.all-observations-keep-conflict",allRr.context && !allRr.context->Value().rrActive.IsKnown(),true,"BuildCanonicalFrameContext");
#ifdef NR_CTX_PURPOSE_CONTEXT
        auto rrRequest=request;++rrRequest.header.revision;
        const auto selectedRr=S::BuildContextForPurpose(pairObservations,pair,rrRequest,rrProfile,store);
        const auto selectedResult=selectedRr.context?S::EvaluateSemanticCapability(selectedRr.context->Value(),pair,rrProfile,header,store):S::CapabilityResult{};
        r.Check("C04.purpose-context-excludes-stale-conflict",selectedResult.certificate && selectedResult.certificate->Value().eligibility==C::Eligibility::Eligible,true,"BuildContextForPurpose/EvaluateSemanticCapability");
        auto retainedSource=oldExposure;retainedSource.rrActive=Known(true);
        auto previousFrame=Frame();previousFrame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",9));
        retainedSource.frame=store.Publish(previousFrame,C::OwnerDomain::IdentityRegistry);
        std::array<C::AcquisitionCandidate,1> retainedSources{retainedSource};
        auto ageProfile=rrProfile;ageProfile.maximumAge=Known(uint64_t{1});++ageProfile.key.version;ageProfile.publication=Key("profile.policy",2);
        auto agedRequest=rrRequest;++agedRequest.header.revision;
        const auto retainedContext=S::BuildContextForPurpose(pairObservations,retainedSources,agedRequest,ageProfile,store);
        const auto retainedResult=retainedContext.context?S::EvaluateSemanticCapability(retainedContext.context->Value(),retainedSources,ageProfile,header,store):S::CapabilityResult{};
        r.Check("C04.declared-one-frame-retention",retainedResult.certificate && retainedResult.certificate->Value().eligibility==C::Eligibility::Eligible,true,"BuildContextForPurpose/EvaluateSemanticCapability");
#else
        r.Check("C04.purpose-context-excludes-stale-conflict",false,true,"Missing BuildContextForPurpose");
#endif
        auto motionProfile=profile;motionProfile.requirements.Push({Sym("motion.canonical"),true});
        auto motion=Analytic::MotionInput().motion;motion.units=Known(C::MotionUnits::ActiveRasterUV);
        auto motionContext=context;motionContext.motion=Known(store.Publish(motion));
        sources[0].motion=motionContext.motion;
        const auto validMotion=S::EvaluateSemanticCapability(motionContext,sources,motionProfile,header,store);
        r.Check("C04.canonical-motion-eligible",validMotion.certificate && validMotion.certificate->Value().eligibility==C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        motion.currentRaster.axisSigns=Known(C::Vec2{1,-1});motion.previousRaster.axisSigns=Known(C::Vec2{1,-1});
        motionContext.motion=Known(store.Publish(motion));
        const auto wrongAxes=S::EvaluateSemanticCapability(motionContext,sources,motionProfile,header,store);
        r.Check("C04.motion-noncanonical-axis-refused",!wrongAxes.certificate || wrongAxes.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        motion=Analytic::MotionInput().motion;motion.units=Known(C::MotionUnits::ActiveRasterUV);
        motion.framePair.current=Known(Id<C::BaseRealFrameId>("stream.main",11));motionContext.motion=Known(store.Publish(motion));
        const auto wrongFrame=S::EvaluateSemanticCapability(motionContext,sources,motionProfile,header,store);
        r.Check("C04.motion-current-frame-refused",!wrongFrame.certificate || wrongFrame.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        motion=Analytic::MotionInput().motion;motion.units=Known(C::MotionUnits::ActiveRasterUV);
        motion.currentRaster=Analytic::Raster({0,0,400,200},{512,256});motion.previousRaster=motion.currentRaster;
        motion.validCoverage=Known(C::Rectangle{0,0,400,200});motionContext.motion=Known(store.Publish(motion));
        const auto wrongRaster=S::EvaluateSemanticCapability(motionContext,sources,motionProfile,header,store);
        r.Check("C04.motion-context-raster-refused",!wrongRaster.certificate || wrongRaster.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
        store.Withdraw(source.evidence);const auto disappeared=qualify(profile);
        r.Check("C04.source-disappearance-refuses",!disappeared.certificate || disappeared.certificate->Value().eligibility!=C::Eligibility::Eligible,true,"EvaluateSemanticCapability");
#else
        r.Check("CTX.capability.production-binding",false,true,"Missing CapabilityQualification implementation");
#endif
        return r.Finish();
    });
}
