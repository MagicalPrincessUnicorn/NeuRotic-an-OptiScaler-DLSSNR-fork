#define NR_NATIVE_FINALIZER_TESTING 1
#include "finalizer_fixture.h"
#include "../orchestration/InitialInvocationFixture.h"

using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace O=Neurotic::Orchestration;
namespace X=Neurotic::Context;
namespace Neurotic::Lifecycle
{
struct NativeUnsealedTestAccess
{
    static NativeUnsealedRetirement Receipt(PrimaryNrClaimHandle claim,const C::NrExecutionRecipe& recipe,
        const C::NativeSampleIdentityV1& sample,const C::RecordKey& recording)
    {return NativeUnsealedRetirement(claim,recipe,sample,recording);}
};
}
template<class Root,class Result>bool DeliverAck(Root& root,const Result& result)
{
    if constexpr(requires{result.retirement;root.AcknowledgeFinalizerRetirement(*result.retirement);})
        return result.retirement&&root.AcknowledgeFinalizerRetirement(*result.retirement);
    return false;
}
template<class Root,class Result>bool DeliverInterruption(Root& root,const Result& result)
{
    if constexpr(requires{result.interruption;root.AcknowledgeFinalInterruption(*result.interruption);})
        return result.interruption&&root.AcknowledgeFinalInterruption(*result.interruption);
    return false;
}
template<class Finalizer,class Reader>bool DeliverStage(Finalizer& finalizer,L::PrimaryNrClaimHandle claim,
    const C::NativeStageDeliveryV1& stage,const Reader& reader,const L::FinalizerEvent& event)
{
    if constexpr(requires{finalizer.SubmitStageDelivery(claim,stage,reader,event);})
        return finalizer.SubmitStageDelivery(claim,stage,reader,event).accepted;
    return false;
}
template<class Result>bool HasConsumption(const Result& result,L::FinalRealFrameFinalizer& finalizer,
    const L::PrimaryNrClaimHandle& claim,const C::EvaluationId& evaluation)
{
    if constexpr(requires{result.consumption;result.consumption->Matches(finalizer,claim,evaluation);})
        return result.consumption&&result.consumption->Matches(finalizer,claim,evaluation);
    return false;
}
void Consumption(Report& r)
{
    auto f=std::make_unique<FinalizerTests::Fixture>();auto claim=f->Claim();
    f->Produce(*claim.claim);f->Prepare(*claim.claim);auto seal=f->Commit(*claim.claim);
    if(!seal.packet)throw std::runtime_error("consumption seal fixture");
    const auto eval=f->request.recipe.evaluation;
    r.Check("strict.seal-is-not-consumption",!HasConsumption(seal,*f->finalizer,*claim.claim,eval),true,"sealing does not close Native history");
    f->finalizer->OfferToFg(*claim.claim,f->Proof(),f->Event());
    C::FgHandoffReceipt receipt;receipt.header=NrContextTests::Header(C::ContractId::C13,C::OwnerDomain::FrameGeneration,80);
    receipt.packet={Sym(C::FinalRealFramePacket::WireName),seal.packet->header.record,seal.packet->header.revision};
    receipt.provider=f->boundary.provider;receipt.accepted=Known(false);receipt.providerReleased=Known(false);
    auto packet=f->finalizer->Snapshot(*claim.claim);receipt.retentions=NrContextTests::PublishList(f->store,packet->retentions);
    auto refused=f->finalizer->ObserveFgReceipt(seal.packet->seal,receipt,f->store,f->Event());
    r.Check("strict.rejected-handoff-not-consumed",!HasConsumption(refused,*f->finalizer,*claim.claim,eval),true,"FG refusal retains history obligation");
    ++receipt.header.revision;receipt.accepted=Known(true);
    auto accepted=f->finalizer->ObserveFgReceipt(seal.packet->seal,receipt,f->store,f->Event());
    r.Check("strict.accepted-handoff-ack",HasConsumption(accepted,*f->finalizer,*claim.claim,eval),true,"sole finalizer authenticates exact accepted FG receipt");
    auto wrong=eval;++wrong.value;
    r.Check("strict.consumption-exact-evaluation",!HasConsumption(accepted,*f->finalizer,*claim.claim,wrong),true,"acknowledgment cannot close a different history attempt");
}
void Retirement(Report& r)
{
    using namespace InitialNativeTests;
    Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get());
    auto admission=Access::Admit(f.root,owner);
    if(!admission||!admission->AcquirePrimary()||!admission->Primary())throw std::runtime_error("strict admission");
    const auto claim=*admission->Primary();auto& finalizer=Access::Finalizer(*f.root);
    const auto snapshot=owner->snapshot;
    const auto terminal=L::NativeUnsealedTestAccess::Receipt(claim,snapshot->product.recipe,*snapshot->Sample(),snapshot->recording);
    auto interruption=finalizer.InterruptUnsealed(claim,Access::Event(*f.root,snapshot->generations));
    r.Check("strict.interruption-is-not-terminal",!DeliverAck(*f.root,interruption),true,"only successful finalizer exclusion returns terminal authority");
    admission.reset();owner->terminal=false;
    auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,800);
    auto retired=finalizer.RetireInterrupted(claim,terminal,registry,snapshot->frame,Known(true),*owner->store,
        Access::Event(*f.root,snapshot->generations));
    if(!retired.accepted)throw std::runtime_error("synthetic exact exclusion");
    Fixture foreign;foreign.Activate();
    r.Check("strict.foreign-session-ack",!DeliverAck(*foreign.root,retired),true,"opaque finalizer acknowledgment is exact to session and claim");
    r.Check("strict.genuine-ack-consumed",DeliverAck(*f.root,retired),true,"coordinator consumes sole finalizer terminal acknowledgment after callback handle drops");
    r.Check("strict.physical-tail-still-required",!f.root->AdvanceClosure().closed,true,"claim acknowledgment does not replace actual owner physical retirement");
    owner->terminal=true;
    r.Check("strict.coordinator-drains-after-both-owners",f.root->AdvanceClosure().closed,true,"coordinator retires operation only after finalizer and invocation owners agree");
    r.Check("strict.retired-ack-replay",!DeliverAck(*f.root,retired),true,"retired slot cannot be resurrected by acknowledgment replay");
}
void Stage(Report& r)
{
    auto f=std::make_unique<FinalizerTests::Fixture>();
    auto output=*f->store.Resolve(f->boundary.output).value;
    output.descriptor.mipLevels=Known(std::uint32_t{1});output.descriptor.arrayLayers=Known(std::uint32_t{1});
    output.mip=Known(std::uint32_t{0});output.arrayLayer=Known(std::uint32_t{0});output.plane=Known(std::uint32_t{0});
    output.adapter=Known(Sym("adapter"));output.raster.allocation=Known(C::Extent{1920,1080});
    output.raster.active=Known(C::Rectangle{0,0,1920,1080});output.raster.origin=Known(C::RasterOrigin::TopLeft);
    output.raster.pixelCenter=Known(C::Vec2{0.5,0.5});output.raster.axisSigns=Known(C::Vec2{1,1});
    f->boundary.output=f->store.Publish(output,C::OwnerDomain::Resource);f->candidate.result.output=Known(f->boundary.output);
    if(!X::CompleteContent(output))throw std::runtime_error("complete StageDelivery resource fixture");
    auto frame=*f->store.Resolve(f->request.recipe.frame).value;
    C::NativeSampleIdentityV1 sample;sample.session=f->session;sample.stream=frame.renderStreamId.KnownPart()->value;
    sample.view=frame.viewId.KnownPart()->value;sample.feature=Id<C::ObjectIncarnation>("feature",1);
    sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;sample.callback=Key("callback",1);
    frame.nativeSample=f->store.Publish(sample,C::OwnerDomain::Provider);f->Frame(frame);
    f->request.recipe.nativeSample=frame.nativeSample;
    C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::FinalConsumerRequired;
    delivery.profile=f->request.recipe.profile;delivery.placement=f->request.recipe.placement;
    delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
    delivery.finalConsumer=f->request.expectedConsumer;
    f->request.recipe.nativeDelivery=f->store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
    auto claim=f->Claim();if(!claim.claim||!f->Produce(*claim.claim).accepted)throw std::runtime_error("native finalizer claim");
    r.Check("strict.c07-is-not-stage-lineage",!f->Prepare(*claim.claim).accepted,true,"C07 and presentation boolean cannot replace actual Native StageDelivery");
    C::NativeStageDeliveryV1 stage;
    stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage"),1};
    stage.plan=f->request.recipe.committedPlan;stage.route=f->request.plan.routeGeneration.KnownPart()->value;
    stage.delivery=*f->request.recipe.nativeDelivery;stage.recipe={Sym("NrExecutionRecipe"),f->request.recipe.header.record,f->request.recipe.header.revision};
    stage.executionResult=f->boundary.candidate;stage.sample=*frame.nativeSample;stage.evaluation=f->request.recipe.evaluation;
    stage.admissionEpoch=Key("epoch");stage.placement=f->request.recipe.placement;
    stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("binding"),1};
    C::NativeOutputContentV1 content;content.view=f->boundary.output;content.recording=Key("recording");
    content.producerOrdinal=2;content.semanticDomain=Sym("SceneLinear");
    stage.nativeOutput=f->store.Publish(content,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
    stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=Id<C::ObjectIncarnation>("caller",1);
    stage.lastRecording=content.recording;stage.lastRecordedOrdinal=2;stage.outcome=C::NativeStageOutcome::HostReturnRecorded;
    stage.reason=Sym("Native.HostReturnRecorded");C::BoundedList<C::OwnerValueReference,16> causes;
    causes.Push({C::OwnerDomain::Provider,Sym("ReturnOperation"),{1,0},Key("operation"),1});
    stage.causes.count=1;stage.causes.backing=f->store.Publish(causes,C::OwnerDomain::Provider);
    r.Check("strict.unpublished-stage-refused",!DeliverStage(*f->finalizer,*claim.claim,stage,f->store,f->Event()),true,"raw stage value is not owner publication");
    auto ref=f->store.Publish(stage,C::OwnerDomain::Provider);stage.publication.record=ref.record;stage.publication.revision=++ref.revision;
    f->store.Replace(ref,stage);
    r.Check("strict.published-stage-joins-reservation",DeliverStage(*f->finalizer,*claim.claim,stage,f->store,f->Event()),true,"exact retained Provider stage joins sole reserved finalizer");
    r.Check("strict.stage-idempotent",DeliverStage(*f->finalizer,*claim.claim,stage,f->store,f->Event()),true,"exact metadata duplicate is read idempotent");
    auto forged=stage;++forged.evaluation.value;
    r.Check("strict.foreign-evaluation-refused",!DeliverStage(*f->finalizer,*claim.claim,forged,f->store,f->Event()),true,"another evaluation cannot replace retained stage");
    auto revised=f->candidate;++revised.result.header.revision;
    r.Check("strict.no-c07-rewrite-after-stage",!f->finalizer->SubmitEvaluationResult(*claim.claim,revised,f->store,f->Event()).accepted,true,"stage freezes exact C07 dependency");
    const auto actualOutput=f->boundary.output;
    auto changed=output;changed.identity.contentRevision=Known(C::ContentRevision{2});
    f->boundary.output=f->store.Publish(changed,C::OwnerDomain::Resource);
    r.Check("strict.unproven-later-content-refused",!f->Prepare(*claim.claim).accepted,true,"later content version requires a real owned lineage edge");
    f->boundary.output=actualOutput;
    r.Check("strict.exact-returned-content-qualifies",f->Prepare(*claim.claim).accepted,true,"actual StageDelivery and exact final content qualify together");
}
void SessionStageAndHistory(Report& r,bool interrupt)
{
    using namespace InitialNativeTests;
    Fixture session;session.Activate();auto owner=std::make_shared<SyntheticInvocation>(session.root.get());
    auto admission=Access::Admit(session.root,owner);
    if(!admission||!admission->AcquirePrimary())throw std::runtime_error("session stage admission");
    const auto claim=*admission->Primary();const auto& snapshot=*owner->snapshot;auto& store=*owner->store;
    const auto& recipe=snapshot.product.recipe;
    auto physical=std::make_unique<FinalizerTests::Fixture>();
    auto output=*physical->store.Resolve(physical->boundary.output).value;
    output.descriptor.mipLevels=Known(std::uint32_t{1});output.descriptor.arrayLayers=Known(std::uint32_t{1});
    output.mip=Known(std::uint32_t{0});output.arrayLayer=Known(std::uint32_t{0});output.plane=Known(std::uint32_t{0});
    output.adapter=Known(Sym("adapter"));output.raster.allocation=Known(C::Extent{1920,1080});
    output.raster.active=Known(C::Rectangle{0,0,1920,1080});output.raster.origin=Known(C::RasterOrigin::TopLeft);
    output.raster.axisSigns=Known(C::Vec2{1,1});
    const auto outputRef=store.Publish(output,C::OwnerDomain::Resource);
    auto result=physical->candidate.result;result.originalLineage=recipe.frame;result.evaluation=recipe.evaluation;result.output=Known(outputRef);
    if(!admission->SubmitEvaluation(result))throw std::runtime_error("session stage C07");
    C::NativeStageDeliveryV1 stage;
    stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage"),1};
    stage.plan=recipe.committedPlan;stage.route=snapshot.generation;stage.delivery=*recipe.nativeDelivery;
    stage.recipe={Sym("NrExecutionRecipe"),recipe.header.record,recipe.header.revision};
    stage.executionResult={Sym("EvaluationResult"),result.header.record,result.header.revision};
    stage.sample=*recipe.nativeSample;stage.evaluation=recipe.evaluation;stage.admissionEpoch=snapshot.reservation;stage.placement=recipe.placement;
    stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("binding"),1};
    C::NativeOutputContentV1 content;content.view=outputRef;content.recording=snapshot.recording;
    content.producerOrdinal=2;content.semanticDomain=Sym("SceneLinear");
    stage.nativeOutput=store.Publish(content,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
    stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=Id<C::ObjectIncarnation>("caller",1);
    stage.lastRecording=content.recording;stage.lastRecordedOrdinal=2;stage.outcome=C::NativeStageOutcome::HostReturnRecorded;
    stage.reason=Sym("Native.HostReturnRecorded");C::BoundedList<C::OwnerValueReference,16> causes;
    causes.Push({C::OwnerDomain::Provider,Sym("ReturnOperation"),{1,0},Key("operation"),1});
    stage.causes.count=1;stage.causes.backing=store.Publish(causes,C::OwnerDomain::Provider);
    auto ref=store.Publish(stage,C::OwnerDomain::Provider);stage.publication.record=ref.record;stage.publication.revision=++ref.revision;store.Replace(ref,stage);
    auto foreign=stage;++foreign.admissionEpoch.value;
    r.Check(interrupt?"strict.interrupt-session-foreign-admission":"strict.session-rejects-foreign-admission",!session.root->SubmitStageDelivery(foreign),true,"stage cannot move between recording reservations");
    r.Check(interrupt?"strict.interrupt-session-publication-required":"strict.session-owner-publication-required",!session.root->SubmitCommittedStage(claim),true,"session reads genuine retained return owner");
    owner->committedStage=stage;admission.reset();
    r.Check(interrupt?"strict.interrupt-session-stage-joined":"strict.session-retained-stage-joined",session.root->SubmitCommittedStage(claim),true,"actual retained session joins owner stage after callback handle drops");
    auto& finalizer=Access::Finalizer(*session.root);auto event=[&]{return Access::Event(*session.root,snapshot.generations);};
    owner->history.Attempt(recipe.evaluation,true,{},snapshot.Sample());owner->history.ModelReturned(recipe.evaluation,{},true);
    if(interrupt)
    {
        const auto interrupted=finalizer.InterruptUnsealed(claim,event());
        owner->consumptionAllowed=true;
        r.Check("strict.interruption-closes-undelivered-history",DeliverInterruption(*session.root,interrupted)&&owner->history.InvocationClosed(),true,"genuine unsealed finalizer failure abandons locally delivered history");
        r.Check("strict.interruption-replay-refused",!DeliverInterruption(*session.root,interrupted),true,"failure acknowledgment is consumed once");
        const auto terminal=L::NativeUnsealedTestAccess::Receipt(claim,recipe,*snapshot.Sample(),snapshot.recording);
        auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,850);
        const auto retired=owner->history.InvocationClosed()?finalizer.RetireInterrupted(claim,terminal,registry,snapshot.frame,Known(true),store,event()):L::FinalizationResult{};
        r.Check("strict.locally-delivered-never-fg-retirement",DeliverAck(*session.root,retired)&&session.root->AdvanceClosure().closed,true,"history abandonment then owner terminality and genuine exclusion close strict invocation");
        return;
    }
    physical->boundary.frame=recipe.frame;physical->boundary.generations=snapshot.generations;physical->boundary.output=outputRef;
    physical->boundary.candidate=stage.executionResult;physical->boundary.handoffContract=Sym("fixture.before-fg");
    physical->boundary.requiredHudIncluded=false;physical->boundary.boundary.hudIncluded=Known(false);physical->boundary.boundary.toneMapped=Known(false);
    physical->binding.plan=recipe.committedPlan.record;physical->binding.route=C::GenerationToken{snapshot.generation.Describe()};
    physical->binding.description.generations=snapshot.generations;physical->facts.current=physical->binding;
    physical->dependency.generations=snapshot.generations;physical->lease=L::BeginConsumption(physical->binding,physical->facts);
    if(!physical->lease||!finalizer.PrepareSeal(claim,physical->boundary,physical->Proof(),store,event()).accepted)
        throw std::runtime_error("session strict PrepareSeal");
    const auto sealed=finalizer.CommitSeal(claim,snapshot.generations,physical->Proof(),store,event());
    if(!sealed.packet||!finalizer.OfferToFg(claim,physical->Proof(),event()).accepted)throw std::runtime_error("session strict seal/offer");
    C::FgHandoffReceipt receipt;receipt.header=NrContextTests::Header(C::ContractId::C13,C::OwnerDomain::FrameGeneration,80);
    receipt.packet={Sym(C::FinalRealFramePacket::WireName),sealed.packet->header.record,sealed.packet->header.revision};
    receipt.provider=physical->boundary.provider;receipt.accepted=Known(true);receipt.providerReleased=Known(false);
    receipt.retentions=NrContextTests::PublishList(store,finalizer.Snapshot(claim)->retentions);
    const auto observed=finalizer.ObserveFgReceipt(sealed.packet->seal,receipt,store,event());
    if(!observed.consumption)throw std::runtime_error("session consumption acknowledgment");
    Fixture other;other.Activate();
    r.Check("strict.consumption-foreign-session",!other.root->AcknowledgeFinalConsumption(*observed.consumption),true,"exact session finalizer scope");
    r.Check("strict.consumption-owner-pending",!session.root->AcknowledgeFinalConsumption(*observed.consumption)&&!owner->history.InvocationClosed(),true,"owner refusal does not close history");
    owner->consumptionAllowed=true;
    r.Check("strict.consumption-owner-history-closed",session.root->AcknowledgeFinalConsumption(*observed.consumption)&&owner->history.InvocationClosed()&&owner->consumedCalls==1,true,"sole finalizer receipt reaches exact existing history owner");
    r.Check("strict.consumption-duplicate-no-second-call",!session.root->AcknowledgeFinalConsumption(*observed.consumption)&&owner->consumedCalls==1,true,"replayed receipt cannot close another attempt");
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("strict-native-integration",argc==2?std::string_view(argv[1]):std::string_view{});
        Retirement(r);Stage(r);Consumption(r);SessionStageAndHistory(r,false);SessionStageAndHistory(r,true);return r.Finish();});
}
