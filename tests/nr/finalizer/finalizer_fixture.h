#pragma once
#include "../context/ContextFixtureStore.h"
#include "../identity/IdentityTestSupport.h"
#include <nr/lifecycle/FinalRealFrameFinalizer.h>
#include <nr/lifecycle/FinalRealFrameReset.h>
namespace FinalizerTests
{
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace X=Neurotic::Context;
struct Fixture
{
    NrContextTests::Store store;
    std::unique_ptr<L::HostIdentityDomain> host=std::make_unique<L::HostIdentityDomain>(Sym("finalizer.fixture"));
    L::OwnerBinding owner=LifeFixture::Binding(C::OwnerDomain::Finalizer,"finalizer");
    C::SessionId session;
    std::unique_ptr<L::FinalRealFrameFinalizer> finalizer;
    L::PrimaryClaimRequest request;
    L::FinalCandidateSubmission candidate;
    L::FinalBoundaryObservation boundary;
    L::LeaseBinding binding;
    L::OwnerFacts facts;
    std::optional<L::LiveConsumptionLease> lease;
    C::DependencyProof dependency;
    std::uint64_t serial=10;
    L::FinalizerEvent Event()
    {
        return {LifeFixture::Event(owner.owner,owner.subject,++serial),request.generations};
    }
    explicit Fixture(std::size_t capacity=8)
    {
        session=LifeFixture::Value(host->OpenSession(LifeFixture::Event(C::OwnerDomain::Session,LifeFixture::Key("session"),1)));
        auto issuer=host->BindFinalizer(owner);if(!issuer)throw std::runtime_error("fixture bind failed");
        finalizer=std::make_unique<L::FinalRealFrameFinalizer>(session,owner,*issuer,capacity);
        auto frame=NrContextTests::Frame();frame.sessionId=Known(session);
        frame.evaluationId=Known(Id<C::EvaluationId>("evaluation",1));
        request.handoff=Id<C::HandoffContractGeneration>("handoff",1);
        request.observation=L::RenderObservation::NewGameContent;
        request.consumerBoundary=C::BoundaryKind::BeforeFg;
        auto& recipe=request.recipe;
        recipe.header=NrContextTests::Header(C::ContractId::C06,C::OwnerDomain::RenderingProtocol,1);
        recipe.evaluation=frame.evaluationId.KnownPart()->value;recipe.frame=store.Publish(frame);
        recipe.context={Sym(C::CanonicalFrameContext::WireName),Key("context"),1};
        recipe.profile={Sym("Native"),Sym("Legacy"),1};recipe.strategy=Sym("NativeTemporal");
        recipe.settingsSnapshot={C::OwnerDomain::Configuration,Sym("Settings"),{1,0},Key("settings"),1};
        C::StructuralSignature signature;signature.schema=Sym("history.signature");signature.version=1;
        signature.generations=store.Publish(C::GenerationVector{});
        C::HistoryKey history;history.owner=Key("history.owner");history.slot=Sym("native");history.dependencies=store.Publish(signature);
        recipe.history=store.Publish(history,C::OwnerDomain::History);recipe.masks=store.Publish(C::MaskSet{});
        recipe.expectedOutput.allocation=Known(C::Extent{1920,1080});
        recipe.expectedOutput.format=Known(Sym("RGBA8"));recipe.expectedOutput.sampleCount=Known(std::uint32_t{1});
        recipe.expectedOutput.api=Known(C::GraphicsApi::D3D12);
        auto& plan=request.plan;plan.header=NrContextTests::Header(C::ContractId::C05,C::OwnerDomain::StreamCoordinator,2);
        plan.decision={Sym(C::RoutingDecision::WireName),Key("decision"),1};plan.committed=Known(true);
        plan.routeGeneration=Known(Id<C::RouteGeneration>("route",1));
        recipe.committedPlan={Sym(C::PlanCommit::WireName),plan.header.record,plan.header.revision};
        C::GenerationVector gen;gen.Insert({plan.routeGeneration.KnownPart()->value.Describe()});gen.Insert({request.handoff.Describe()});
        request.generations=store.Publish(gen,C::OwnerDomain::StreamCoordinator);
        auto& result=candidate.result;result.header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,3);
        result.evaluation=recipe.evaluation;result.originalLineage=recipe.frame;result.stage=C::OutcomeStage::Produced;
        result.composition=Known(C::CompositionStatus::Accepted);result.historyAdvancement=Known(C::HistoryAdvancement::Advanced);
        result.originalPreserved=Known(false);
        auto output=NrContextTests::Resource(10,{});output.descriptor=recipe.expectedOutput;
        output.device=Known(Id<C::ObjectIncarnation>("device",1));
        result.output=Known(store.Publish(output,C::OwnerDomain::Resource));
        candidate.acceptedFinalCandidate=Known(true);candidate.generations=request.generations;
        boundary.owner=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::Presentation,4);
        boundary.frame=store.Publish(frame,C::OwnerDomain::Presentation);
        boundary.boundary.kind=Known(C::BoundaryKind::BeforeFg);boundary.boundary.episode=frame.episodeId;
        boundary.boundary.afterRequiredHostRendering=Known(true);boundary.boundary.hudIncluded=Known(true);
        boundary.boundary.toneMapped=Known(true);
        boundary.handoff=request.handoff;boundary.provider=Id<C::ProviderIncarnation>("provider",1);
        boundary.handoffContract=Sym("NativeFG.FullFrame");boundary.version=1;
        boundary.output=result.output.KnownPart()->value;boundary.generations=request.generations;
        boundary.lineageIncludesCandidate=Known(true);
        boundary.candidate={Sym(C::EvaluationResult::WireName),result.header.record,result.header.revision};
        boundary.originalPreserved=Known(false);boundary.color.domain=Known(C::ColorDomain::SceneLinear);
        boundary.requiredHudIncluded=true;
        C::NativeFinalConsumerContractV1 expected;expected.provider=boundary.provider;expected.handoff=boundary.handoff;
        expected.contract=boundary.handoffContract;expected.contractVersion=1;expected.color=C::ColorDomain::SceneLinear;
        expected.hudIncluded=true;expected.toneMapped=true;expected.extent={1920,1080};
        request.expectedConsumer=store.Publish(expected,C::OwnerDomain::FrameGeneration);
        binding.description.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,5);
        binding.description.context=recipe.context;binding.description.candidate={Sym(C::AcquisitionCandidate::WireName),Key("candidate"),1};
        binding.description.resource=output.identity;binding.description.use=C::UsageKind::RetainedProviderConsumption;
        binding.description.commandScope.key=Key("provider.scope");binding.description.device=output.device;
        binding.description.queue=Known(Id<C::ObjectIncarnation>("queue",1));
        binding.description.revoked=Known(false);binding.description.revocationEpoch=Known(Key("revocation"));
        binding.description.expirationBoundary=Known(Key("expiration"));binding.description.submissionRecheckRequired=Known(true);
        binding.description.generations=request.generations;binding.consumer=Key("fg.consumer");binding.purpose=Sym("ExternalProviderConsume");
        binding.plan=plan.header.record;binding.profile=Key("profile");binding.route=C::GenerationToken{plan.routeGeneration.KnownPart()->value.Describe()};
        facts.current=binding;facts.permission=L::Fact::Yes;facts.apiCompatible=L::Fact::Yes;
        facts.registryHealthy=L::Fact::Yes;facts.nativeBindingMatches=L::Fact::Yes;facts.externalRegistrationsComplete=L::Fact::Yes;
        facts.recordingTracked=L::Fact::Yes;facts.resourcesRegistered=L::Fact::Yes;facts.descriptorsProtected=L::Fact::Yes;
        facts.dependency.ordered=L::Fact::Yes;facts.dependency.committed=L::Fact::Yes;facts.dependency.uniqueSubmission=L::Fact::Yes;
        facts.dependency.sameDevice=L::Fact::Yes;facts.dependency.queueKnown=L::Fact::Yes;facts.dependency.supportedType=L::Fact::Yes;
        facts.dependency.completed=L::Fact::No;facts.dependency.nonReplayable=L::Fact::Yes;
        L::ExternalHold hold;hold.consumer=binding.consumer;hold.registration=Key("retention");hold.target=output.identity;
        hold.kind=L::HoldKind::Provider;hold.provider=boundary.provider;hold.handoff=C::GenerationToken{request.handoff.Describe()};hold.released=L::Fact::No;facts.holds.Push(hold);
        dependency.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,6);
        dependency.producerDevice=output.device;dependency.producerQueue=binding.description.queue;dependency.consumerQueue=binding.description.queue;
        dependency.recording=Known(Key("recording"));dependency.submission=Known(Key("submission"));dependency.ownerDependency=Known(Key("dependency"));
        dependency.orderedForConsumer=Known(true);dependency.gpuCompleted=Known(false);dependency.recordingNonReplayable=Known(true);
        dependency.generations=request.generations;
        binding.description.producerDependency=Known(C::ContractRef<C::ContractId::C03>{Sym(C::DependencyProof::WireName),dependency.header.record,1});
        facts.current=binding;lease=L::BeginConsumption(binding,facts);if(!lease)throw std::runtime_error("fixture C03 admission failed");
    }
    L::FinalFrameRuntimeProof Proof(){return {&*lease,&facts,&dependency};}
    L::FinalizationResult Claim(){return finalizer->TryClaimPrimary(request,store,Event());}
    L::FinalizationResult Produce(L::PrimaryNrClaimHandle handle){return finalizer->SubmitEvaluationResult(handle,candidate,store,Event());}
    L::FinalizationResult Prepare(L::PrimaryNrClaimHandle handle){return finalizer->PrepareSeal(handle,boundary,Proof(),store,Event());}
    L::FinalizationResult Commit(L::PrimaryNrClaimHandle handle){return finalizer->CommitSeal(handle,request.generations,Proof(),store,Event());}
    void Frame(C::FrameIdentity frame)
    {
        request.recipe.frame=store.Publish(frame);++request.recipe.header.revision;
        request.recipe.evaluation=frame.evaluationId.KnownPart()->value;
        candidate.result.originalLineage=request.recipe.frame;candidate.result.evaluation=request.recipe.evaluation;
        ++candidate.result.header.record.value;candidate.result.header.revision=1;
        boundary.frame=store.Publish(frame,C::OwnerDomain::Presentation);
        boundary.candidate={Sym(C::EvaluationResult::WireName),candidate.result.header.record,1};
    }
};
inline void ExactlyOnce(Report& r)
{
    auto f=std::make_unique<Fixture>();auto claim=f->Claim();
    r.Check("T27.primary",claim.accepted,true,"TryClaimPrimary");if(!claim.claim)return;
    auto duplicate=f->Claim();r.Check("T27.duplicate-claim",duplicate.claim==claim.claim,true,"TryClaimPrimary");
    auto competing=f->request;competing.recipe.evaluation.value=2;
    auto competingFrame=*f->store.Resolve(competing.recipe.frame).value;competingFrame.evaluationId=Known(competing.recipe.evaluation);
    competing.recipe.frame=f->store.Publish(competingFrame);
    r.Check("T27.competing-evaluation",f->finalizer->TryClaimPrimary(competing,f->store,f->Event()).accepted,false,"TryClaimPrimary");
    auto result=f->Produce(*claim.claim);r.Check("T27.produced",result.accepted,true,"SubmitEvaluationResult");
    r.Check("T27.prepare",f->Prepare(*claim.claim).accepted,true,"PrepareSeal");
    auto sealed=f->Commit(*claim.claim);r.Check("T27.accept-one",sealed.accepted&&sealed.packet.has_value(),true,"CommitSeal");if(!sealed.packet)return;
    auto again=f->Commit(*claim.claim);r.Check("T27.duplicate-seal",again.packet&&again.packet->seal==sealed.packet->seal,true,"CommitSeal");
    r.Check("T27.literal-base",sealed.packet->baseRealFrame.value==10,true,"FinalRealFramePacket");
    r.Check("T27.literal-seal",sealed.packet->seal.value==1,true,"FinalizerIssuer");
    const char* nonrealIds[]={"T27.generated-rejected","T27.repeated-rejected","T27.unknown-rejected"};
    std::size_t nonrealIndex=0;
    for(auto observation:{L::RenderObservation::GeneratedOutput,L::RenderObservation::RepeatedContent,L::RenderObservation::Unknown})
    {auto other=std::make_unique<Fixture>();other->request.observation=observation;r.Check(nonrealIds[nonrealIndex++],other->Claim().accepted,false,"TryClaimPrimary");}
    auto changed=f->request;changed.plan.routeGeneration=Known(Id<C::RouteGeneration>("route",2));
    r.Check("T27.route-does-not-reopen",f->finalizer->TryClaimPrimary(changed,f->store,f->Event()).accepted,false,"TryClaimPrimary");
    auto missing=std::make_unique<Fixture>();auto frame=*missing->store.Resolve(missing->request.recipe.frame).value;frame.baseRealFrameId={};
    missing->request.recipe.frame=missing->store.Publish(frame);r.Check("T27.unknown-base",missing->Claim().accepted,false,"TryClaimPrimary");
    auto full=std::make_unique<Fixture>(1);auto first=full->Claim();auto frame2=*full->store.Resolve(full->request.recipe.frame).value;
    frame2.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));full->request.recipe.frame=full->store.Publish(frame2);
    r.Check("T27.capacity-no-eviction",full->Claim().accepted,false,"TryClaimPrimary");
    r.Check("T27.first-claim-retained",first.claim.has_value(),true,"TryClaimPrimary");
    L::ProvisionalReleaseProof foreign;foreign.owner=f->candidate.result.header;
    foreign.notSubmitted=Known(true);foreign.nonReplayable=Known(true);foreign.notAdvanced=Known(true);
    r.Check("T27.foreign-cancel-rejected",full->finalizer->ReleaseUncommittedClaim(*first.claim,foreign,full->Event()).accepted,false,"ReleaseUncommittedClaim");
    auto released=std::make_unique<Fixture>();auto provisional=released->Claim();
    released->candidate.result.stage=C::OutcomeStage::NotAttempted;released->candidate.result.historyAdvancement=Known(C::HistoryAdvancement::NotAdvanced);
    released->Produce(*provisional.claim);foreign.owner=released->candidate.result.header;
    r.Check("T27.proven-transfer",released->finalizer->ReleaseUncommittedClaim(*provisional.claim,foreign,released->Event()).accepted,true,"ReleaseUncommittedClaim");
    r.Check("T27.reclaim",released->Claim().accepted,true,"TryClaimPrimary");
    r.Check("T27.stale-handle",released->Produce(*provisional.claim).accepted,false,"SubmitEvaluationResult");
    auto scoped=std::make_unique<Fixture>();
    for(std::uint64_t i=0;i<3;++i)
    {
        auto next=*scoped->store.Resolve(scoped->request.recipe.frame).value;
        next.viewId=Known(Id<C::ViewId>("view",i==1?2:1));next.renderStreamId=Known(Id<C::RenderStreamId>("stream",i==2?2:1));
        next.evaluationId=Known(Id<C::EvaluationId>("evaluation",i+1));scoped->Frame(next);
        auto c=scoped->Claim();if(!c.claim)throw std::runtime_error("scope fixture refused");
        scoped->Produce(*c.claim);scoped->Prepare(*c.claim);auto s=scoped->Commit(*c.claim);
        const char* ids[]={"T27.scope-normal","T27.scope-view","T27.scope-stream"};
        r.Check(ids[i],s.packet&&s.packet->seal.value==i+1&&s.packet->baseRealFrame.value==10,true,"CommitSeal");
    }
    for(std::uint64_t nodes:{1ULL,2ULL,10ULL})
    {
        auto multi=std::make_unique<Fixture>();multi->candidate.result.header.owner=C::OwnerDomain::Multipass;
        auto c=multi->Claim();multi->Produce(*c.claim);multi->Prepare(*c.claim);auto s=multi->Commit(*c.claim);
        for(std::uint64_t i=0;i<nodes;++i)multi->Commit(*c.claim);
        std::string id="T27.multipass-"+std::to_string(nodes);
        const auto repeated=multi->Commit(*c.claim);
        r.Check(id,s.packet&&repeated.packet&&repeated.packet->seal.value==1,true,"CommitSeal");
    }
    auto reuse=std::make_unique<Fixture>(2);auto old=reuse->Claim();
    reuse->candidate.result.stage=C::OutcomeStage::NotAttempted;reuse->candidate.result.historyAdvancement=Known(C::HistoryAdvancement::NotAdvanced);
    reuse->Produce(*old.claim);foreign.owner=reuse->candidate.result.header;
    reuse->finalizer->ReleaseUncommittedClaim(*old.claim,foreign,reuse->Event());
    auto next=*reuse->store.Resolve(reuse->request.recipe.frame).value;next.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));
    reuse->Frame(next);reuse->candidate.result.stage=C::OutcomeStage::Produced;reuse->candidate.result.historyAdvancement=Known(C::HistoryAdvancement::Advanced);
    auto fresh=reuse->Claim();reuse->Produce(*fresh.claim);reuse->Prepare(*fresh.claim);auto sealedNew=reuse->Commit(*fresh.claim);
    if(!sealedNew.packet)throw std::runtime_error("retirement fixture failed");
    reuse->finalizer->OfferToFg(*fresh.claim,reuse->Proof(),reuse->Event());
    C::FgHandoffReceipt ack;ack.header=NrContextTests::Header(C::ContractId::C13,C::OwnerDomain::FrameGeneration,96);
    ack.packet={Sym(C::FinalRealFramePacket::WireName),sealedNew.packet->header.record,1};ack.provider=reuse->boundary.provider;ack.accepted=Known(true);ack.providerReleased=Known(true);
    C::RetentionRegistration registration;registration.consumer=reuse->binding.consumer;registration.registration=Known(Key("retention"));registration.released=Known(true);
    C::BoundedList<C::RetentionRegistration,16> registrations;registrations.Push(registration);ack.retentions=NrContextTests::PublishList(reuse->store,registrations);
    reuse->finalizer->ObserveFgReceipt(sealedNew.packet->seal,ack,reuse->store,reuse->Event());
    L::CloseAdmission(*reuse->lease);reuse->facts.dependency.reusable=L::Fact::Yes;reuse->facts.consumerTicketsRetired=L::Fact::Yes;reuse->facts.ownerCanRecycle=L::Fact::Yes;
    auto releasedHold=*reuse->facts.holds.Get(0);releasedHold.released=L::Fact::Yes;releasedHold.releaseEvidence=Key("release.proof");
    reuse->facts.holds={};reuse->facts.holds.Push(releasedHold);
    reuse->finalizer->ObserveResourceRetirement(sealedNew.packet->seal,*reuse->lease,reuse->facts,reuse->Event());
    auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,97);
    r.Check("T27.canceled-prefix-retires",reuse->finalizer->RetireScope(sealedNew.packet->seal,registry,next,Known(true),reuse->Event()).accepted,true,"RetireScope");
    r.Check("T27.retired-no-reclaim",reuse->Claim().reason==L::FinalizationReason::RetiredScope,true,"TryClaimPrimary");
    next.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",12));reuse->Frame(next);
    r.Check("T27.retired-capacity-reused",reuse->Claim().accepted,true,"TryClaimPrimary");
    r.Check("T27.retired-handle-not-reused",reuse->Produce(*fresh.claim).accepted,false,"SubmitEvaluationResult");
}
template<class Finalizer> void Retirement(Report& r,Fixture& f,Finalizer& finalizer,const C::FinalRealFramePacket& packet)
{
    auto owner=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,95);
    auto frame=*f.store.Resolve(f.request.recipe.frame).value;
    if constexpr(requires {finalizer.RetireScope(packet.seal,owner,frame,Known(true),f.Event());})
    {
        r.Check("T28.no-inferred-exclusion",finalizer.RetireScope(packet.seal,owner,frame,C::OptionalFact<bool>{},f.Event()).accepted,false,"RetireScope");
        r.Check("T28.owner-exclusion-retirement",finalizer.RetireScope(packet.seal,owner,frame,Known(true),f.Event()).accepted,true,"RetireScope");
    }
    else r.Check("T28.retirement-api",false,true,"RetireScope");
}
template<class Finalizer> void Handoff(Report& r,Fixture& f,Finalizer& finalizer,const L::PrimaryNrClaimHandle& claim,const C::FinalRealFramePacket& packet)
{
    if constexpr(requires {finalizer.OfferToFg(claim,f.Proof(),f.Event());finalizer.StopAdmissions();finalizer.DrainStatus();})
    {
        r.Check("T28.offer",finalizer.OfferToFg(claim,f.Proof(),f.Event()).accepted,true,"OfferToFg");
        r.Check("T28.offer-idempotent",finalizer.OfferToFg(claim,f.Proof(),f.Event()).accepted,true,"OfferToFg");
        f.facts.current.description.revoked=Known(true);
        r.Check("T28.reoffer-rechecks-rights",finalizer.OfferToFg(claim,f.Proof(),f.Event()).accepted,false,"OfferToFg");
        f.facts.current.description.revoked=Known(false);
        C::FgHandoffReceipt ack;ack.header=NrContextTests::Header(C::ContractId::C13,C::OwnerDomain::FrameGeneration,80);
        ack.packet={Sym(C::FinalRealFramePacket::WireName),packet.header.record,packet.header.revision};
        ack.provider=f.boundary.provider;ack.accepted=Known(true);ack.providerReleased={};
        C::BoundedList<C::RetentionRegistration,16> holds;C::RetentionRegistration hold;
        hold.consumer=f.binding.consumer;hold.registration=Known(Key("retention"));hold.released=Known(false);holds.Push(hold);
        ack.retentions=NrContextTests::PublishList(f.store,holds);
        auto wrong=ack;wrong.provider.value=2;
        r.Check("T28.wrong-provider",finalizer.ObserveFgReceipt(packet.seal,wrong,f.store,f.Event()).accepted,false,"ObserveFgReceipt");
        r.Check("T28.consume",finalizer.ObserveFgReceipt(packet.seal,ack,f.store,f.Event()).accepted,true,"ObserveFgReceipt");
        finalizer.StopAdmissions();auto status=finalizer.DrainStatus();
        r.Check("T28.unknown-release-blocks-drain",status&&!status->safelyDrained&&status->retentionUnknown==1,true,"DrainStatus");
        auto present=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::Presentation,82);
        finalizer.RecordPresentDisposition(packet.seal,present,Known(true),f.Event());
        r.Check("T28.present-not-release",!finalizer.DrainStatus()->safelyDrained,true,"DrainStatus");
        ack.header.revision=2;ack.providerReleased=Known(true);
        r.Check("T28.incomplete-release-refused",finalizer.ObserveFgReceipt(packet.seal,ack,f.store,f.Event()).accepted,false,"ObserveFgReceipt");
        holds={};hold.released=Known(true);holds.Push(hold);ack.retentions=NrContextTests::PublishList(f.store,holds);
        r.Check("T28.exact-release",finalizer.ObserveFgReceipt(packet.seal,ack,f.store,f.Event()).accepted,true,"ObserveFgReceipt");
        r.Check("T28.release-not-gpu-retirement",!finalizer.DrainStatus()->safelyDrained,true,"DrainStatus");
        r.Check("T28.resource-unretired-tail-pending",!finalizer.DrainStatus()->tailsRetired,true,"DrainStatus");
        L::CloseAdmission(*f.lease);
        r.Check("T28.inflight-no-retirement",finalizer.ObserveResourceRetirement(packet.seal,*f.lease,f.facts,f.Event()).accepted,false,"ObserveResourceRetirement");
        f.facts.dependency.completed=L::Fact::Yes;f.facts.dependency.nonReplayable=L::Fact::Yes;
        f.facts.dependency.reusable=L::Fact::Yes;f.facts.consumerTicketsRetired=L::Fact::Yes;f.facts.ownerCanRecycle=L::Fact::Yes;
        auto ownerHold=*f.facts.holds.Get(0);ownerHold.released=L::Fact::Yes;ownerHold.releaseEvidence=Key("release.proof");
        f.facts.holds={};f.facts.holds.Push(ownerHold);
        r.Check("T28.owner-retirement",finalizer.ObserveResourceRetirement(packet.seal,*f.lease,f.facts,f.Event()).accepted,true,"ObserveResourceRetirement");
        r.Check("T28.drained",finalizer.DrainStatus()->safelyDrained,true,"DrainStatus");
        r.Check("T28.all-owner-tails-retired",finalizer.DrainStatus()->tailsRetired,true,"DrainStatus");
        ack.header.revision=3;ack.providerReleased={};
        r.Check("T28.release-cannot-regress",finalizer.ObserveFgReceipt(packet.seal,ack,f.store,f.Event()).accepted,false,"ObserveFgReceipt");
        ack.providerReleased=Known(false);
        r.Check("T28.release-cannot-rehold",finalizer.ObserveFgReceipt(packet.seal,ack,f.store,f.Event()).accepted,false,"ObserveFgReceipt");
        r.Check("T28.stopped",f.Claim().accepted,false,"TryClaimPrimary");
        Retirement(r,f,finalizer,packet);
    }
    else r.Check("T28.handoff-api",false,true,"FinalRealFrameFinalizer");
}
inline void Ordering(Report& r)
{
    auto f=std::make_unique<Fixture>();auto c=f->Claim();if(!c.claim)throw std::runtime_error("claim failed");f->Produce(*c.claim);
    f->boundary.boundary.afterRequiredHostRendering=Known(false);
    r.Check("T28.intermediate",f->Prepare(*c.claim).accepted,false,"PrepareSeal");
    f->boundary.boundary.afterRequiredHostRendering=Known(true);f->facts.dependency.ordered=L::Fact::Unknown;
    r.Check("T28.order-unknown",f->Prepare(*c.claim).accepted,false,"PrepareSeal");
    f->facts.dependency.ordered=L::Fact::Yes;f->facts.dependency.sameDevice=L::Fact::No;
    r.Check("T28.device-mismatch",f->Prepare(*c.claim).accepted,false,"PrepareSeal");
    f->facts.dependency.sameDevice=L::Fact::Yes;f->boundary.lineageIncludesCandidate=Known(false);
    r.Check("T28.no-lineage",f->Prepare(*c.claim).accepted,false,"PrepareSeal");
    f->boundary.lineageIncludesCandidate=Known(true);
    r.Check("T28.ordered-not-complete",f->Prepare(*c.claim).accepted,true,"PrepareSeal");
    auto savedBinding=f->binding;auto savedFacts=f->facts;
    f->binding.consumer=Key("other.consumer");f->facts.current=f->binding;
    auto otherHold=*f->facts.holds.Get(0);otherHold.consumer=f->binding.consumer;f->facts.holds={};f->facts.holds.Push(otherHold);
    f->lease=L::BeginConsumption(f->binding,f->facts);
    r.Check("T28.consumer-substitution",f->Commit(*c.claim).accepted,false,"CommitSeal");
    f->binding=savedBinding;f->facts=savedFacts;f->lease=L::BeginConsumption(f->binding,f->facts);
    f->binding.profile=Key("other.profile");f->facts.current=f->binding;f->lease=L::BeginConsumption(f->binding,f->facts);
    r.Check("T28.profile-substitution",f->Commit(*c.claim).accepted,false,"CommitSeal");
    f->binding=savedBinding;f->facts=savedFacts;f->lease=L::BeginConsumption(f->binding,f->facts);
    auto holdBefore=*f->facts.holds.Get(0);auto substitute=holdBefore;substitute.registration=Key("replacement.hold");
    f->facts.holds={};f->facts.holds.Push(substitute);
    r.Check("T28.retention-substitution",f->Commit(*c.claim).accepted,false,"CommitSeal");
    f->facts.holds={};f->facts.holds.Push(holdBefore);
    f->facts.current.description.revoked=Known(true);
    r.Check("T28.submit-recheck",f->Commit(*c.claim).accepted,false,"CommitSeal");
    f->facts.current.description.revoked=Known(false);auto seal=f->Commit(*c.claim);
    r.Check("T28.before-present",seal.packet.has_value(),true,"CommitSeal");
    if(!seal.packet)return;
    auto present=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::Presentation,55);
    r.Check("T28.present-failure-separate",f->finalizer->RecordPresentDisposition(seal.packet->seal,present,Known(false),f->Event()).accepted,true,"RecordPresentDisposition");
    auto still=f->Commit(*c.claim);r.Check("T28.present-failure-no-unseal",still.packet&&still.packet->seal==seal.packet->seal,true,"CommitSeal");
    Handoff(r,*f,*f->finalizer,*c.claim,*seal.packet);
    auto noOutput=std::make_unique<Fixture>();auto noClaim=noOutput->Claim();noOutput->candidate.result.output={};noOutput->Produce(*noClaim.claim);
    r.Check("T28.produced-without-output",noOutput->Prepare(*noClaim.claim).accepted,false,"PrepareSeal");
    auto stale=std::make_unique<Fixture>();auto staleClaim=stale->Claim();
    C::GenerationVector replaced;replaced.Insert({Id<C::RouteGeneration>("route",2).Describe()});
    stale->candidate.generations=stale->store.Publish(replaced,C::OwnerDomain::StreamCoordinator);
    r.Check("T28.stale-candidate",stale->Produce(*staleClaim.claim).accepted,false,"SubmitEvaluationResult");
    auto unknown=std::make_unique<Fixture>();auto uc=unknown->Claim();unknown->Produce(*uc.claim);
    unknown->boundary.boundary.hudIncluded={};
    r.Check("T28.unknown-hud",unknown->Prepare(*uc.claim).accepted,false,"PrepareSeal");
    unknown->boundary.boundary.hudIncluded=Known(true);unknown->boundary.color.domain={};
    r.Check("T28.unknown-color",unknown->Prepare(*uc.claim).accepted,false,"PrepareSeal");
    unknown->boundary.color.domain=Known(C::ColorDomain::SceneLinear);unknown->dependency.submission={};
    r.Check("T28.unpublished-dependency",unknown->Prepare(*uc.claim).accepted,false,"PrepareSeal");
    auto bypass=std::make_unique<Fixture>();auto bc=bypass->Claim();bypass->boundary.originalBypass=true;
    r.Check("T28.unproven-original",bypass->Prepare(*bc.claim).accepted,false,"PrepareSeal");
    bypass->boundary.originalPreserved=Known(true);bypass->boundary.bypassReason={};
    r.Check("T28.original-qualified",bypass->Prepare(*bc.claim).accepted,true,"PrepareSeal");
    auto bs=bypass->Commit(*bc.claim);
    r.Check("T28.original-not-model-success",bs.packet&&bs.packet->original.KnownPart()->value,true,"CommitSeal");
    auto snap=bypass->finalizer->Snapshot(bs.packet->seal);
    r.Check("T28.packet-closure",snap&&X::ResolveMetadata(*snap->packet.retentions.backing,*snap)&&X::ResolveMetadata(*snap->packet.upstreamReceipts.backing,*snap),true,"Snapshot");
}
template<class State,class Reader> void DelayedResetAck(Report& r,State& state,const C::ResetPlan& plan,const C::ResetAcknowledgment& ack,const Reader& reader)
{
    if constexpr(requires {state.Observe(C::OutcomeStage::Submitted,Known(C::HistoryAdvancement::Advanced),Known(false),Known(Gen(C::IdentityKind::HistoryGeneration,"history",2)));})
    {
        state.Observe(C::OutcomeStage::Submitted,Known(C::HistoryAdvancement::Advanced),Known(false),Known(Gen(C::IdentityKind::HistoryGeneration,"history",2)));
        r.Check("T29.delayed-first-ack",state.Acknowledge(plan,ack,reader),false,"FinalizationResetState");
        r.Check("T29.delayed-ack-pending",state.State().pending,true,"FinalizationResetState");
    }
    else r.Check("T29.observed-generation-api",false,true,"FinalizationResetState");
}
template<class Reader> void ResetAcknowledgments(Report& r,const Reader& store,const L::FinalizationResetProjection& pending,const C::ResetAcknowledgment& ack,const C::ResetPlan& plan)
{
    if constexpr(requires { L::AcknowledgesFinalizationReset(pending,ack,plan,store); })
    {
        r.Check("T29.owner-ack",L::AcknowledgesFinalizationReset(pending,ack,plan,store),true,"AcknowledgesFinalizationReset");
        auto wrong=pending;wrong.history=Key("other.history");
        r.Check("T29.other-history",L::AcknowledgesFinalizationReset(wrong,ack,plan,store),false,"AcknowledgesFinalizationReset");
        auto failed=ack;failed.applied=Known(false);
        r.Check("T29.failed-reset",L::AcknowledgesFinalizationReset(pending,failed,plan,store),false,"AcknowledgesFinalizationReset");
        failed=ack;failed.affectedOwner=Key("other.owner");
        r.Check("T29.other-owner",L::AcknowledgesFinalizationReset(pending,failed,plan,store),false,"AcknowledgesFinalizationReset");
        auto cpu=plan;cpu.header.owner=C::OwnerDomain::Finalizer;
        r.Check("T29.finalizer-not-reset-owner",L::AcknowledgesFinalizationReset(pending,ack,cpu,store),false,"AcknowledgesFinalizationReset");
        failed=ack;failed.resultingLineage=Known(Gen(C::IdentityKind::RouteGeneration,"route",1));
        r.Check("T29.wrong-generation",L::AcknowledgesFinalizationReset(pending,failed,plan,store),false,"AcknowledgesFinalizationReset");
    }
    else r.Check("T29.exact-plan-ack-api",false,true,"AcknowledgesFinalizationReset");
}
inline void Resets(Report& r)
{
    L::OpaqueHistoryProjection input;input.owner=Key("history.owner");input.history=Key("history");
    input.invocation=C::OutcomeStage::Submitted;input.advancement={};input.consumed=Known(false);
    r.Check("T29.submitted-unknown-pending",L::ProjectFinalizationReset(input).pending,true,"ProjectFinalizationReset");
    input.invocation=C::OutcomeStage::Bypassed;input.priorPending=true;
    r.Check("T29.bypass-retains",L::ProjectFinalizationReset(input).pending,true,"ProjectFinalizationReset");
    input.priorPending=false;input.invocation=C::OutcomeStage::NotAttempted;
    r.Check("T29.not-attempted-no-new-reset",L::ProjectFinalizationReset(input).pending,false,"ProjectFinalizationReset");
    input.invocation=C::OutcomeStage::Produced;input.advancement=Known(C::HistoryAdvancement::Advanced);input.consumed=Known(true);
    r.Check("T29.consumed-present-failure-no-new-reset",L::ProjectFinalizationReset(input).pending,false,"ProjectFinalizationReset");
    auto f=std::make_unique<Fixture>();auto claim=f->Claim();if(!claim.claim)throw std::runtime_error("claim failed");
    f->candidate.result.stage=C::OutcomeStage::Failed;f->candidate.result.historyAdvancement={};f->candidate.result.composition=Known(C::CompositionStatus::Failed);
    r.Check("T29.failed-recorded",f->Produce(*claim.claim).accepted,true,"SubmitEvaluationResult");
    r.Check("T29.failed-no-success-seal",f->Prepare(*claim.claim).accepted,false,"PrepareSeal");
    L::ProvisionalReleaseProof release;release.owner=f->candidate.result.header;release.notSubmitted=Known(true);release.nonReplayable=Known(true);release.notAdvanced=Known(true);
    r.Check("T29.unknown-advance-no-transfer",f->finalizer->ReleaseUncommittedClaim(*claim.claim,release,f->Event()).accepted,false,"ReleaseUncommittedClaim");
    C::ResetPlan plan;plan.header=NrContextTests::Header(C::ContractId::C08,C::OwnerDomain::ResetRules,90);
    C::BoundedList<C::RecordKey,32> affected;affected.Push(input.owner);plan.affectedOwners=NrContextTests::PublishList(f->store,affected);
    affected={};affected.Push(input.history);plan.affectedHistories=NrContextTests::PublishList(f->store,affected);
    plan.actions.Push(C::ResetAction::ResetHistory);plan.boundary.kind=Known(C::BoundaryKind::BeforeFg);
    input.priorPending=true;input.plan=C::ContractRef<C::ContractId::C08>{Sym(C::ResetPlan::WireName),plan.header.record,1};
    C::ResetAcknowledgment ack;ack.header=NrContextTests::Header(C::ContractId::C08,C::OwnerDomain::History,91);
    ack.plan=*input.plan;ack.affectedOwner=input.owner;ack.accepted=Known(true);ack.applied=Known(true);
    ack.acceptedBoundary=plan.boundary;ack.resultingLineage=Known(Gen(C::IdentityKind::HistoryGeneration,"history",2));ack.appliedActions.Push(C::ResetAction::ResetHistory);
    ResetAcknowledgments(r,f->store,L::ProjectFinalizationReset(input),ack,plan);
    L::FinalizationResetState state(input.owner,input.history);
    state.Observe(C::OutcomeStage::Failed,Known(C::HistoryAdvancement::MayHaveAdvanced),Known(false),Known(Gen(C::IdentityKind::HistoryGeneration,"history",1)));
    r.Check("T29.may-advance-pending",state.State().pending,true,"FinalizationResetState");
    state.Observe(C::OutcomeStage::Bypassed,Known(C::HistoryAdvancement::NotAdvanced),Known(false),Known(Gen(C::IdentityKind::HistoryGeneration,"history",1)));
    r.Check("T29.skip-not-ack",state.State().pending,true,"FinalizationResetState");
    auto bad=ack;bad.applied=Known(false);state.Acknowledge(plan,bad,f->store);
    r.Check("T29.failed-ack-preserves",state.State().pending,true,"FinalizationResetState");
    r.Check("T29.legal-ack-clears",state.Acknowledge(plan,ack,f->store)&&!state.State().pending,true,"FinalizationResetState");
    state.Observe(C::OutcomeStage::Submitted,Known(C::HistoryAdvancement::Advanced),Known(false),Known(Gen(C::IdentityKind::HistoryGeneration,"history",2)));
    r.Check("T29.old-ack-not-new-reset",state.Acknowledge(plan,ack,f->store),false,"FinalizationResetState");
    r.Check("T29.new-reset-still-pending",state.State().pending,true,"FinalizationResetState");
    L::FinalizationResetState delayed(input.owner,input.history);DelayedResetAck(r,delayed,plan,ack,f->store);
}
}
