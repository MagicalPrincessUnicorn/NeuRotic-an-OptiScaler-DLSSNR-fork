#include "../contracts/Test002Support.h"
#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/orchestration/RoutingSelector.h>)
#include <nr/orchestration/RoutingSelector.h>
#include "RoutingFixture.h"
#include <nr/context/CapabilityQualification.h>
#include "../oracles/Test002AnalyticCases.h"
#define NR_ORCH_IMPLEMENTED 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("routing-policy",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_ORCH_IMPLEMENTED
    using namespace NrRoutingTests;namespace O=Neurotic::Orchestration;
    const auto select=[](Fixture& f){f.Publish();return O::Select(f.input,f.store);};
    Fixture f;
    r.Check("T22.stable-tie",Chose(select(f),1),true,"Select");
    auto first=O::Select(f.input,f.store);std::swap(*f.input.offers.Get(0),*f.input.offers.Get(1));
    r.Check("T22.permutation",O::Select(f.input,f.store)==first,true,"Select");
    f.Set(f.factsA,"implementation",Known(false));r.Check("T22.missing-implementation",Chose(select(f),2),true,"Select");
    f.Set(f.factsA,"implementation",Known(true));f.Set(f.factsA,"lifetime",{});
    r.Check("T22.unknown-lifetime",Chose(select(f),2),true,"Select");
    f.Set(f.factsA,"lifetime",Known(true));f.Set(f.factsA,"handoff",Known(false));
    r.Check("T22.handoff-hard",Chose(select(f),2),true,"Select");f.Set(f.factsA,"handoff",Known(true));
    f.a.degradation.optionalFeatureLoss=100;f.b.degradation.outputAge=1;
    r.Check("T24.lexicographic-degradation",Chose(select(f),1),true,"Select");
    f.input.intent.prefer.Push(Sym("Present"));
    r.Check("T24.preference-after-degradation",Chose(select(f),1),true,"Select");
    f.a.degradation={};f.b.degradation={};r.Check("T24.preference",Chose(select(f),2),true,"Select");
    f.input.intent.prefer={};f.input.incumbent.current=f.b.anchor;
    r.Check("T22.healthy-incumbent",Chose(select(f),2),true,"Select");f.input.incumbent.current.reset();
    f.input.intent.require.Push(Sym("missing"));r.Check("T24.unsatisfied-require",!select(f).proposed,true,"Select");
    f.input.intent.require={};f.input.intent.forbid.Push(Sym("Native"));r.Check("T24.forbid",Chose(select(f),2),true,"Select");
    f.input.intent.forbid={};f.input.intent.mode=Sym("FixedLegacy");f.input.intent.fixedStrategy=Sym("Native");
    f.Set(f.factsA,"api",Known(false));r.Check("T24.fixed-no-fallback",!select(f).proposed,true,"Select");
    f.Set(f.factsA,"api",Known(true));f.input.intent.mode=Sym("Auto");r.Check("T24.auto-unavailable",!select(f).valid,true,"Select");
    f.input.intent.mode=Sym("LockCurrent");f.input.incumbent.current.reset();
    r.Check("T24.LC01-no-incumbent",select(f).valid&&!select(f).proposed,true,"Select");
    f.input.legacyEffective=f.a.anchor;r.Check("T24.LC01-no-legacy-inference",!select(f).proposed,true,"Select");
    f.Lock();f.b.measuredCost=Known(0.01);f.input.intent.prefer.Push(Sym("Present"));
    r.Check("T24.LC02-exact-incumbent",Chose(select(f),1),true,"Select");
    f.Set(f.factsA,"api",Known(false));auto invalid=select(f);
    r.Check("T24.LC03-hard-clear",invalid.valid&&!invalid.proposed&&!invalid.next.current,true,"Select");
    f.Set(f.factsA,"api",Known(true));f.Set(f.factsA,"availability",{});auto absent=select(f);
    r.Check("T24.LC04-unavailable-retain",absent.valid&&!absent.proposed&&absent.next.current==f.input.incumbent.current,true,"Select");
    f.input.incumbent=absent.next;f.Set(f.factsA,"availability",Known(true));
    r.Check("T24.LC05-same-returns",Chose(select(f),1),true,"Select");
    f.input.incumbent.structurallyCurrent=Known(false);auto reincarnated=select(f);
    r.Check("T24.LC06-no-rebind",reincarnated.valid&&!reincarnated.proposed&&!reincarnated.next.current,true,"Select");
    f.input.incumbent.structurallyCurrent=Known(true);f.a.currentContent=Known(C::ContentRevision{2});
    r.Check("T24.LC07-content-not-identity",Chose(select(f),1),true,"Select");
    f.Set(f.factsA,"freshness",Known(false));auto stale=select(f);
    r.Check("T24.LC07-stale-retain",stale.valid&&!stale.proposed&&stale.next.current.has_value(),true,"Select");
    f.Set(f.factsA,"freshness",Known(true));f.input.intent.mode=Sym("Shadow");f.input.incumbent.streak=100;
    auto released=select(f);r.Check("T24.LC08-release",released.valid&&!released.next.wasLocked&&
        Reason(released,"ORCH.Intent.LockCurrent.ReleasedByIntentChange")&&released.next.streak<100,true,"Select");
    f.Lock();f.input.frame.generatedFrameId=Known(Id<C::GeneratedFrameId>("generated",1));f.input.provenRealFrame=Known(false);
    r.Check("T24.LC09-generated-no-promotion",Chose(select(f),1)&&select(f).next.streak==0,true,"Select");
    r.Check("T24.no-execution-effect",select(f).executionEffect==Sym("None"),true,"Select");
    {
        auto fixture=std::make_unique<Fixture>();auto& h=*fixture;
        h.input.incumbent.current=h.a.anchor;h.a.degradation.optionalFeatureLoss=1;h.input.intent.promotionFrames=3;
        auto decision=select(h);r.Check("T23.first-real-waits",Chose(decision,1)&&decision.next.streak==1,true,"Select");
        h.input.incumbent=decision.next;decision=select(h);
        r.Check("T23.same-base-no-increment",Chose(decision,1)&&decision.next.streak==1,true,"Select");
        h.input.incumbent=decision.next;h.input.frame.generatedFrameId=Known(Id<C::GeneratedFrameId>("gen",1));
        h.input.frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));decision=select(h);
        r.Check("T23.generated-no-increment",Chose(decision,1)&&decision.next.streak==1,true,"Select");
        h.input.incumbent=decision.next;h.input.frame.generatedFrameId={};h.input.repeatedPresent=Known(true);decision=select(h);
        r.Check("T23.repeated-no-increment",Chose(decision,1)&&decision.next.streak==1,true,"Select");
        h.input.incumbent=decision.next;h.input.repeatedPresent=Known(false);decision=select(h);
        r.Check("T23.second-real-waits",Chose(decision,1)&&decision.next.streak==2,true,"Select");
        h.input.incumbent=decision.next;h.input.frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",12));decision=select(h);
        r.Check("T23.third-real-promotes",Chose(decision,2)&&decision.next.streak==3,true,"Select");
        h.input.incumbent=decision.next;h.input.frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));decision=select(h);
        r.Check("T23.old-base-no-increment",decision.next.streak==3,true,"Select");
        h.input.incumbent=decision.next;h.a.degradation={};decision=select(h);
        h.input.incumbent=decision.next;h.a.degradation.optionalFeatureLoss=1;decision=select(h);
        r.Check("T23.same-base-after-reset",decision.next.streak==0,true,"Select");
        h.Set(h.factsA,"resource",Known(false));decision=select(h);
        r.Check("T23.immediate-hard-revoke",!Chose(decision,1)&&!decision.next.current,true,"Select");
    }
    {
        auto fixture=std::make_unique<Fixture>();auto& t=*fixture;
        C::PolicyReason soft;soft.rule=Sym("quality.unqualified");soft.reason.category=C::ReasonCategory::QualityUnqualified;
        t.a.softRestrictions.Push(soft);r.Check("T24.basic-soft-refusal",Chose(select(t),2),true,"Select");
        t.input.intent.tier=Sym("Advanced");r.Check("T24.advanced-needs-scoped-optin",Chose(select(t),2),true,"Select");
        t.input.intent.optIns.Push({soft.rule,t.input.scope,t.generations});r.Check("T24.scoped-optin",Chose(select(t),1),true,"Select");
        t.input.intent.optIns.Get(0)->scope.key=Key("other.scope",2);r.Check("T24.wrong-optin-scope",Chose(select(t),2),true,"Select");
        t.input.intent.optIns.Get(0)->scope=t.input.scope;t.input.intent.tier=Sym("Unrestricted");t.Set(t.factsA,"lifetime",Known(false));
        r.Check("T24.unrestricted-keeps-hard-invariants",Chose(select(t),2),true,"Select");
        t.Set(t.factsA,"lifetime",Known(true));t.input.mandatoryFG=Known(true);t.Set(t.factsA,"fg",Known(false));
        r.Check("T24.mandatory-fg",Chose(select(t),2),true,"Select");
        t.input.mandatoryFG=Known(false);t.input.mandatoryRR=Known(true);t.Set(t.factsA,"rr",Known(false));
        r.Check("T24.mandatory-rr",Chose(select(t),2),true,"Select");
        t.input.mandatoryRR={};r.Check("T24.unknown-host-demand",!select(t).valid,true,"Select");
    }
    {
        auto fixture=std::make_unique<Fixture>();auto& g=*fixture;g.Lock();
        const auto old=g.a.anchor.signature;auto signature=*g.store.Resolve(old).value;
        C::GenerationVector vector;vector.Insert(Gen(C::IdentityKind::ProviderIncarnation,"provider",2));
        signature.generations=g.store.Publish(vector,C::OwnerDomain::IdentityRegistry);
        g.a.anchor.signature=g.store.Publish(signature,C::OwnerDomain::Strategy);auto changed=select(g);
        r.Check("T24.structural-generation-clears",changed.valid&&!changed.proposed&&!changed.next.current,true,"Select");
        g.a.anchor.signature=old;g.a.currentContent={};g.Set(g.factsA,"freshness",{});
        r.Check("T24.unknown-freshness-retains",!select(g).proposed&&select(g).next.current.has_value(),true,"Select");
        g.Set(g.factsA,"freshness",Known(true));g.factsA.Get(0)->source.owner=C::OwnerDomain::Diagnostics;
        r.Check("T22.diagnostics-not-authority",!select(g).proposed&&!select(g).next.current,true,"Select");
        g.input.schemaVersion=2;r.Check("T22.future-schema-refused",!select(g).valid,true,"Select");
    }
    {
        auto fixture=std::make_unique<Fixture>();auto& q=*fixture;
        q.a.measuredCost={};q.b.measuredCost=Known((std::numeric_limits<double>::max)());
        r.Check("T22.unknown-cost-not-known-maximum",Chose(select(q),2),true,"Select");
        q.a.measuredCost=Known(1.0);q.b.measuredCost=Known(1.0);q.a.evidenceRank={};
        q.b.evidenceRank=Known((std::numeric_limits<uint32_t>::max)());
        r.Check("T22.unknown-evidence-not-known-maximum",Chose(select(q),2),true,"Select");
#ifdef NR_ORCH_PROGRESS_PROVENANCE
        q.input.incumbent.current=q.a.anchor;q.a.degradation.optionalFeatureLoss=1;
        q.input.frameSource.owner=C::OwnerDomain::Diagnostics;auto wrongOwner=select(q);
        r.Check("T23.progress-owner-required",wrongOwner.valid&&wrongOwner.next.streak==0,true,"Select");
        q.input.frameSource.owner=C::OwnerDomain::IdentityRegistry;q.input.frameScope.key=Key("other.scope",1);
        auto wrongScope=select(q);r.Check("T23.progress-scope-required",wrongScope.valid&&wrongScope.next.streak==0,true,"Select");
        q.input.frameScope=q.input.scope;q.input.contractVersion.major=2;
        r.Check("T22.contract-version-refused",!select(q).valid,true,"Select");
#else
        r.Check("T23.progress-owner-required",false,true,"Select");
#endif
    }
#else
    r.Check("T22.production-selector",false,true,"Select");
#endif
    {
        auto fixture=std::make_unique<NrRoutingTests::Fixture>();auto& q=*fixture;
        q.Lock();auto cert=*q.store.Resolve(q.a.semantic).value;cert.eligibility=C::Eligibility::Unqualified;
        q.a.semantic=q.store.Publish(cert,C::OwnerDomain::Context);q.input.intent.forbid.Push(Sym("Native"));q.Publish();
        auto forbidden=Neurotic::Orchestration::Select(q.input,q.store);
        r.Check("T24.unqualified-cannot-hide-hard-intent",forbidden.valid&&!forbidden.proposed&&!forbidden.next.current,true,"Select");
        q.input.intent.forbid={};q.Set(q.factsA,"handoff",Known(false));q.Publish();
        auto unsafe=Neurotic::Orchestration::Select(q.input,q.store);
        r.Check("T24.unqualified-cannot-hide-host-failure",unsafe.valid&&!unsafe.proposed&&!unsafe.next.current,true,"Select");
        cert.eligibility=C::Eligibility::Eligible;q.a.semantic=q.store.Publish(cert,C::OwnerDomain::Context);q.Set(q.factsA,"handoff",Known(true));
        C::PolicyReason soft;soft.rule=Sym("quality.temporary");soft.reason.category=C::ReasonCategory::QualityUnqualified;q.a.softRestrictions.Push(soft);
        C::PolicyReason hard;hard.rule=Sym("owner.hard");hard.hard=true;q.a.softRestrictions.Push(hard);q.Publish();
        auto mixed=Neurotic::Orchestration::Select(q.input,q.store);
        r.Check("T24.temporary-cannot-hide-later-hard",mixed.valid&&!mixed.proposed&&!mixed.next.current,true,"Select");
    }
    {
        auto fixture=std::make_unique<NrRoutingTests::Fixture>();auto& q=*fixture;
        namespace S=Neurotic::Context;auto& store=q.store;
        S::ContextRequest request;request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
        request.header.scope=q.input.scope;request.header.record=q.a.context.record;
        request.frame=store.Publish(NrContextTests::Frame(),C::OwnerDomain::IdentityRegistry);request.generations=q.generations;
        const auto raster=Analytic::Raster({0,0,800,400},{1024,512});request.renderRaster=store.Publish(raster);request.outputRaster=request.renderRaster;
        request.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);request.boundary.episode=q.input.frame.episodeId;
        C::EvidenceVector evidence;evidence.provenance=Known(C::SourceClass::Native);evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        evidence.ageInRealFrames=Known(uint64_t{0});evidence.coverage=Known(C::CoverageKind::Complete);evidence.access=Known(C::DescriptiveAccess::CallbackBorrow);
        evidence.lineage.Push(Key("native",1));request.continuity=store.Publish(evidence,C::OwnerDomain::ColorContinuity);
        C::AcquisitionCandidate source;source.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        source.provider=Key("provider",1);source.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
        source.sourceSchema=Sym("ctx.fixture.source");source.sourceVersion=Known(C::VersionNumber{1,0});source.semantic=C::SemanticKind::Jitter;
        source.frame=request.frame;source.boundary=request.boundary;source.evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        source.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);source.jitter=Known(C::Vec2{0,0});source.contentRevision=Known(C::ContentRevision{1});
        NrContextTests::DeclareJitterBasis(store,source,raster);source.rrActive=Known(true);
        std::array<C::AcquisitionCandidate,2> sources{source,source};sources[1].header.record=Key("other.source",2);sources[1].rrActive=Known(false);
        evidence.lineage={};evidence.lineage.Push(Key("second.native",2));sources[1].evidence=store.Publish(evidence,C::OwnerDomain::Provider);
        C::ObservationSet observations;observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);observations.boundary=request.boundary;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;
        for(const auto& s:sources)refs.Push({Sym(C::AcquisitionCandidate::WireName),s.header.record,1});observations.candidates=NrContextTests::PublishList(store,refs);
        const auto built=S::BuildCanonicalFrameContext(observations,sources,request,store);
        S::SemanticProfile profile;profile.key=q.a.anchor.profile;profile.purpose=Sym("test.jitter");profile.rule=Sym("CTX.Semantics");profile.publication=Key("profile.policy",1);
        profile.maximumAge=Known(uint64_t{0});profile.acceptedEvidence.Push(C::SourceClass::Native);profile.sourceSchemas.Push({Sym("ctx.fixture.source"),{1,0}});
        profile.requirements.Push({Sym("jitter"),true});profile.boundary=C::BoundaryKind::BeforeUpscale;
        auto header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,4);header.scope=q.input.scope;
        const auto eligible=built.context?S::EvaluateSemanticCapability(built.context->Value(),sources,profile,header,store):S::CapabilityResult{};
        const bool ready=eligible.certificate&&eligible.certificate->Value().eligibility==C::Eligibility::Eligible&&eligible.certificate->Value().contradictions.count==1;
        r.Check("T22.ctx-unrelated-conflict-fixture",ready,true,"EvaluateSemanticCapability");
        if(ready){q.a.semantic=store.Publish(eligible.certificate->Value(),C::OwnerDomain::Context);q.Lock();q.Publish();}
        r.Check("T22.unrelated-conflict-not-global-veto",ready&&NrRoutingTests::Chose(Neurotic::Orchestration::Select(q.input,store),1),true,"Select");
        profile.requirements.Push({Sym("rrActive"),true});
        const auto required=built.context?S::EvaluateSemanticCapability(built.context->Value(),sources,profile,header,store):S::CapabilityResult{};
        if(required.certificate){q.a.semantic=store.Publish(required.certificate->Value(),C::OwnerDomain::Context);q.Publish();}
        auto rejected=Neurotic::Orchestration::Select(q.input,store);
        r.Check("T22.required-conflict-hard-clear",required.certificate&&required.certificate->Value().eligibility==C::Eligibility::Ineligible&&rejected.valid&&!rejected.proposed&&!rejected.next.current,true,"Select");
    }
    return r.Finish();});
}
