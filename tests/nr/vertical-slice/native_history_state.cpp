#define NR_NATIVE_RETURN_TRANSACTION_TESTING 1
#include "../finalizer/finalizer_fixture.h"
#include <nr/protocol/NativeTemporalRuntime.h>
#include <nr/contracts/NativeStageDelivery.h>
#include "../representations/RepresentationFixture.h"
#if __has_include(<nr/lifecycle/NativeHistoryState.h>)
#include <nr/lifecycle/NativeHistoryState.h>
#define NR_NATIVE_HISTORY 1
#endif
#if __has_include(<nr/lifecycle/NativeReturnTransaction.h>)
#include <nr/lifecycle/NativeReturnTransaction.h>
#define NR_NATIVE_RETURN_TRANSACTION 1
#endif
#include <cstdlib>
#include <new>
namespace {bool countReturnAllocations=false;std::size_t returnAllocations=0;}
void* operator new(std::size_t size)
{if(countReturnAllocations)++returnAllocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc{};}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p)noexcept{::operator delete(p);}
using namespace NrTest002;
#ifdef NR_NATIVE_RETURN_TRANSACTION
namespace Neurotic::Lifecycle
{
class NativeReturnTransactionTestAccess
{
  public:
    template<class... Args>static auto Reserve(Args&&... args)
    {return NativeReturnTransaction::Reserve(std::forward<Args>(args)...);}
    template<class Reader>static bool Native(NativeReturnTransaction& t,const C::EvaluationResult& value,
        const C::MetadataRef<C::NativeOutputContentV1>& output,const Reader& reader)
    {return t.ObserveNative(value,output,reader);}
    template<class Reader>static bool Output(NativeReturnTransaction& t,
        const C::MetadataRef<C::NativeOutputContentV1>& output,const C::ResourceView& current,const Reader& reader)
    {return t.ObserveOutput(output,current,reader);}
    static bool Sr(NativeReturnTransaction& t,std::uint32_t result,bool succeeded,
        std::optional<C::MetadataRef<C::NativeOutputContentV1>> input={})
    {return t.ObserveSr(result,succeeded,input);}
    static bool Restore(NativeReturnTransaction& t,bool cpu,bool commands)
    {return t.ObserveRestoration(cpu,commands);}
    template<class Reader>static bool Prepare(NativeReturnTransaction& t,const Reader& reader)
    {return t.Prepare(reader);}
    static bool Commit(NativeReturnTransaction& t)noexcept{return t.Commit();}
    static bool Reject(NativeReturnTransaction& t,std::string_view reason)noexcept{return t.Reject(reason);}
    static bool FinishRejected(NativeReturnTransaction& t)noexcept
    {
#ifdef NR_NATIVE_DEFERRED_RETURN_FAILURE_V1
        return t.FinishRejected();
#else
        return false;
#endif
    }
};
}
namespace ReturnTests
{
namespace L=Neurotic::Lifecycle;
using Access=L::NativeReturnTransactionTestAccess;
struct Fixture
{
    NrContextTests::Store store;
    std::shared_ptr<L::OwnerMetadataArena> arena=std::make_shared<L::OwnerMetadataArena>(Key("return.arena"),262144,16);
    std::shared_ptr<L::NativeHistoryState> history=std::make_shared<L::NativeHistoryState>(Key("history.owner"),Key("history"));
    C::NativeSampleIdentityV1 sample;
    C::ResourceView target;
    C::EvaluationResult result;
    C::NativeStageDeliveryV1 seed;
    C::MetadataRef<C::NativeOutputContentV1> native,output;
    mutable std::size_t reads=0;
    template<class T>auto Resolve(const C::MetadataRef<T>& ref)const
    {++reads;auto found=arena->Resolve(ref);return found.value?found:store.Resolve(ref);}
    explicit Fixture(C::Placement placement=C::Placement::NativeAfter,bool strict=false)
    {
        auto image=std::make_unique<NrRepresentationTests::Fixture>();target=image->target;
        sample.session=Id<C::SessionId>("session",1);sample.stream=Id<C::RenderStreamId>("stream",1);
        sample.view=Id<C::ViewId>("view",1);sample.feature=Id<C::ObjectIncarnation>("feature",1);
        sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;sample.callback=Key("callback");
        seed.plan={Sym("PlanCommit"),Key("plan"),1};seed.route=Id<C::RouteGeneration>("route",1);
        seed.recipe={Sym("NrExecutionRecipe"),Key("recipe"),1};seed.evaluation=Id<C::EvaluationId>("evaluation",1);
        seed.admissionEpoch=Key("admission");seed.placement=placement;
        seed.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("bindings"),1};
        seed.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;seed.caller=sample.ingress;
        seed.lastRecording=Key("recording");seed.sample=store.Publish(sample,C::OwnerDomain::Provider);
        C::NativeDeliveryContractV1 delivery;delivery.kind=strict?C::NativeDeliveryKind::FinalConsumerRequired:C::NativeDeliveryKind::HostReturn;
        delivery.profile=image->profile;delivery.placement=placement;delivery.returnBoundary=seed.boundary;
        delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
        if(strict){C::NativeFinalConsumerContractV1 final;final.provider=Id<C::ProviderIncarnation>("provider",1);
            final.handoff=Id<C::HandoffContractGeneration>("handoff",1);final.contract=Sym("fixture.final");
            final.contractVersion=1;final.extent={1024,512};delivery.finalConsumer=store.Publish(final,C::OwnerDomain::FrameGeneration);}
        seed.delivery=store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
        C::FrameIdentity frame;frame.nativeSample=seed.sample;
        result.header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,6);
        result.evaluation=seed.evaluation;result.originalLineage=store.Publish(frame,C::OwnerDomain::Context);
        result.stage=C::OutcomeStage::Produced;
        C::NativeOutputContentV1 content;content.view=store.Publish(placement==C::Placement::NativeBefore?image->source:target,C::OwnerDomain::Resource);
        content.recording=seed.lastRecording;content.producerOrdinal=4;content.semanticDomain=Sym("SceneLinear");
        native=store.Publish(content,C::OwnerDomain::Resource);output=native;result.output=Known(content.view);
        if(placement==C::Placement::NativeBefore)
        {
            content.view=store.Publish(target,C::OwnerDomain::Resource);content.producerOrdinal=6;
            output=store.Publish(content,C::OwnerDomain::Resource);
        }
        seed.executionResult={Sym("EvaluationResult"),result.header.record,result.header.revision};
    }
    auto Reserve(){return Access::Reserve(*arena,seed,sample,target,history,std::shared_ptr<const void>(arena));}
    bool Model(){return history->Attempt(seed.evaluation,false,{},sample)&&history->ModelReturned(seed.evaluation,{},true);}
    bool Observe(L::NativeReturnTransaction& transaction)
    {
        return Model()&&Access::Native(transaction,result,native,*this)&&Access::Output(transaction,output,target,*this)&&
            Access::Sr(transaction,0x12345678u,true,seed.placement==C::Placement::NativeBefore?std::optional{native}:std::nullopt)&&
            Access::Restore(transaction,true,true);
    }
};
void Cases(Report& r)
{
    static_assert(!std::is_default_constructible_v<L::NativeReturnTransaction>);
    static_assert(!std::is_copy_constructible_v<L::NativeReturnTransaction>);
    auto positive=std::make_unique<Fixture>();auto transaction=positive->Reserve();
    r.Check("W03.return-reserve-before-effects",transaction&&transaction->HistoryAwaitSafe()&&!positive->arena->Resolve(transaction->StageReference()).value,true,"unpublished reserved stage");
    const bool prepared=transaction&&positive->Observe(*transaction)&&Access::Prepare(*transaction,*positive);
    r.Check("W03.return-prepare-not-delivered",prepared&&!transaction->CommittedStage()&&positive->history->RequiresReset(),true,"fallible checks precede return commit");
    const auto reads=positive->reads;returnAllocations=0;countReturnAllocations=true;
    const bool committed=prepared&&Access::Commit(*transaction);countReturnAllocations=false;
    r.Check("W03.return-pure-commit",committed&&returnAllocations==0&&positive->reads==reads,true,"commit performs no allocation or reader call");
    const auto* stage=transaction->CommittedStage();
    r.Check("W03.return-actual-result-history",stage&&stage->hostResult==0x12345678u&&!positive->history->RequiresReset()&&positive->history->InvocationClosed(),true,"exact result and existing history closure");
    r.Check("W03.return-duplicate",!Access::Commit(*transaction)&&transaction->CommittedStage()==stage,true,"one-shot return");
#ifdef NR_NATIVE_HOST_RETURN_OBSERVATION_V1
    DlssNr::NativeHostReturnObservationV1 observation;
    transaction->ObserveForHost(observation);
    r.Check("W03.return-observer-actual-commit",observation.status==4&&observation.stageOutcome==1&&
        observation.evaluation==stage->evaluation.value&&observation.hostResult==0x12345678u&&
        observation.lastRecordedOrdinal==stage->lastRecordedOrdinal&&(observation.restorationFlags&3)==3,
        true,"read-only observer projects actual committed transaction");
    observation.sequence=8;observation.featureHandle=7;observation.commandList=11;observation.outputResource=13;
#ifdef NR_NATIVE_HOST_RETURN_RESOURCE_OBSERVATION_V1
    r.Check("W03.return-observer-resource",transaction->ObserveOutputForHost(*positive,positive->target,9,observation)&&
        observation.provenanceFlags==3&&observation.outputContentRevision==positive->target.identity.contentRevision.KnownPart()->value.value&&
        observation.recordingIncarnation==9&&observation.recordingIdentity.value==stage->lastRecording.value,
        true,"actual committed returned Resource and recording metadata projected");
    auto staleView=positive->target;staleView.identity.contentRevision=Known(C::ContentRevision{999});
    r.Check("W03.return-observer-resource-stale",!transaction->ObserveOutputForHost(*positive,staleView,9,observation)&&
        observation.provenanceFlags==0&&observation.outputContentRevision==0,true,"stale current Resource cannot reuse earlier known observation");
    r.Check("W03.return-observer-recording-unknown",!transaction->ObserveOutputForHost(*positive,positive->target,0,observation)&&
        observation.provenanceFlags==0,true,"missing actual recording stays unknown");
#else
    r.Check("W03.return-observer-resource",false,true,"exact Resource observation unavailable");
#endif
    r.Check("W03.return-observer-fresh-binding",DlssNr::MatchesNativeReturnObservation(observation,7,7,11,13)&&
        !DlssNr::MatchesNativeReturnObservation(observation,8,7,11,13)&&
        !DlssNr::MatchesNativeReturnObservation(observation,7,9,11,13)&&
        !DlssNr::MatchesNativeReturnObservation(observation,7,7,12,13)&&
        !DlssNr::MatchesNativeReturnObservation(observation,7,7,11,14),true,"fresh exact caller binding required");
#else
    r.Check("W03.return-observer-actual-commit",false,true,"read-only actual transaction observation unavailable");
#endif
    for(unsigned fault=0;fault<9;++fault)
    {
        auto f=std::make_unique<Fixture>();auto t=f->Reserve();f->Model();bool accepted=true;
        auto result=f->result;auto target=f->target;auto native=f->native;
        if(fault==0){auto sample=f->sample;++sample.producerOrdinal;f->store.Replace(f->seed.sample,sample);}
        if(fault==1)target.identity.objectIncarnation=Known(C::StableIdentity{C::IdentityKind::ObjectIncarnation,Sym("object"),Sym("owner"),99});
        if(fault==2)target.identity.contentRevision=Known(C::ContentRevision{9});
        if(fault==3)++result.evaluation.value;
        if(fault==4)++native.revision;
        if(fault==5){auto frame=*f->store.Resolve(result.originalLineage).value;auto altered=f->sample;altered.callback=Key("other.callback");
            frame.nativeSample=f->store.Publish(altered,C::OwnerDomain::Provider);result.originalLineage=f->store.Publish(frame);}
        accepted=Access::Native(*t,result,native,*f)&&accepted;
        accepted=Access::Output(*t,f->output,target,*f)&&accepted;
        accepted=Access::Sr(*t,7,fault!=6)&&accepted;
        accepted=Access::Restore(*t,fault!=7,fault!=8)&&accepted;
        const bool preparedFault=Access::Prepare(*t,*f);
        const char* names[]={"W03.return-altered-sample","W03.return-altered-target","W03.return-stale-content","W03.return-altered-evaluation",
            "W03.return-stale-metadata","W03.return-altered-callback","W03.return-failed-sr","W03.return-failed-parameters","W03.return-failed-command-state"};
        r.Check(names[fault],(!accepted||!preparedFault)&&!Access::Commit(*t)&&!t->CommittedStage()&&f->history->InvocationClosed()&&f->history->Pending(),true,"refusal retains unconsumed history");
    }
    auto abandoned=std::make_unique<Fixture>();auto lost=abandoned->Reserve();abandoned->Observe(*lost);lost.reset();
    r.Check("W03.return-destructor-abandons",abandoned->history->InvocationClosed()&&abandoned->history->Pending(),true,"no dangling AwaitHostReturn");
    auto exhausted=std::make_unique<Fixture>();L::OwnerMetadataArena full(Key("return.full"),8,1);
    auto refused=Access::Reserve(full,exhausted->seed,exhausted->sample,exhausted->target,exhausted->history,std::shared_ptr<const void>(exhausted->arena));
    r.Check("W03.return-reservation-exhaustion",!refused&&!exhausted->history->RequiresReset(),true,"capacity refuses before effects");
    auto caller=std::make_unique<Fixture>();++caller->seed.caller.value;
    r.Check("W03.return-altered-caller",!caller->Reserve()&&!caller->history->RequiresReset(),true,"exact authentic ingress caller");
    auto duplicate=std::make_unique<Fixture>();auto duplicateTx=duplicate->Reserve();duplicate->Observe(*duplicateTx);
    r.Check("W03.return-repeated-sr",!Access::Sr(*duplicateTx,9,true)&&duplicate->history->Pending(),true,"SR operation cannot replay");
    Access::FinishRejected(*duplicateTx);
    const auto* rejected=duplicate->arena->Resolve(duplicateTx->StageReference()).value;
#ifdef NR_NATIVE_HOST_RETURN_OBSERVATION_V1
    duplicateTx->ObserveForHost(observation);
    r.Check("W03.return-observer-rejection",observation.status==3&&observation.stageOutcome==2&&
        observation.hostResult==0x12345678u,true,"rejected transaction cannot reuse previous success observation");
    auto pendingObservation=std::make_unique<Fixture>();auto pendingTx=pendingObservation->Reserve();
    pendingTx->ObserveForHost(observation);
    r.Check("W03.return-observer-partial",observation.status==2&&observation.stageOutcome==0&&
        observation.lastRecordedOrdinal==0,true,"unpublished partial attempt stays pending");
#endif
    r.Check("W03.return-reserved-failure",rejected&&rejected->outcome==C::NativeStageOutcome::DeliveryRejected&&rejected->hostResult==0x12345678u,true,"first observed result retained on duplicate rejection");
    auto lateSr=std::make_unique<Fixture>();auto lateTx=lateSr->Reserve();lateSr->Model();
    Access::Reject(*lateTx,"Native.EarlierFailure");
    const bool pendingFailure=!lateSr->arena->Resolve(lateTx->StageReference()).value;
    Access::Sr(*lateTx,0x81234567u,false);Access::FinishRejected(*lateTx);
    const auto* failed=lateSr->arena->Resolve(lateTx->StageReference()).value;
    r.Check("W03.return-late-sr-result",pendingFailure&&failed&&failed->hostResult==0x81234567u&&failed->reason==Sym("Native.EarlierFailure")&&
        lateSr->history->InvocationClosed()&&lateSr->history->Pending(),true,"late real SR word and first failure retained before rejection publication");
    auto changedHistory=std::make_unique<Fixture>();auto changedTx=changedHistory->Reserve();changedHistory->Observe(*changedTx);
    Access::Prepare(*changedTx,*changedHistory);auto later=changedHistory->seed.evaluation;++later.value;
    changedHistory->history->Attempt(later,true,{},changedHistory->sample);
    r.Check("W03.return-history-recheck",!Access::Commit(*changedTx)&&!changedHistory->history->InvocationClosed(),true,"commit cannot consume a newer attempt");
    changedHistory->history->Abandon(later);
    auto before=std::make_unique<Fixture>(C::Placement::NativeBefore);auto beforeTx=before->Reserve();
    r.Check("W03.return-before-consumption",before->Observe(*beforeTx)&&Access::Prepare(*beforeTx,*before)&&Access::Commit(*beforeTx),true,"actual SR consumption chain");
    auto missing=std::make_unique<Fixture>(C::Placement::NativeBefore);auto missingTx=missing->Reserve();missing->Model();
    Access::Native(*missingTx,missing->result,missing->native,*missing);Access::Output(*missingTx,missing->output,missing->target,*missing);
    Access::Sr(*missingTx,1,true);Access::Restore(*missingTx,true,true);
    r.Check("W03.return-before-input-required",!Access::Prepare(*missingTx,*missing)&&missing->history->Pending(),true,"SR success alone does not prove Native consumption");
    auto strict=std::make_unique<Fixture>(C::Placement::NativeAfter,true);auto strictTx=strict->Reserve();
    const bool strictCommitted=strict->Observe(*strictTx)&&Access::Prepare(*strictTx,*strict)&&Access::Commit(*strictTx);strictTx.reset();
    r.Check("W03.return-strict-history-pending",strictCommitted&&!strict->history->InvocationClosed()&&!strict->history->Pending(),true,"strict history remains with final consumer");
    strict->history->Abandon(strict->seed.evaluation);
}
}
#endif
template<class State>bool CurrentAttempt(State& state,const C::EvaluationId& evaluation,bool reset,const C::OptionalFact<C::GenerationToken>& lineage)
{
    if constexpr(requires{state.Attempt(evaluation,reset,lineage);})return state.Attempt(evaluation,reset,lineage);
    else return state.Attempt(evaluation,reset);
}
template<class State>void Returned(State& state,const C::EvaluationId& evaluation,const C::OptionalFact<C::GenerationToken>& lineage)
{if constexpr(requires{state.ModelReturned(evaluation,lineage);})state.ModelReturned(evaluation,lineage);}
template<class State>bool HostAttempt(State& state,const C::EvaluationId& evaluation,const C::NativeSampleIdentityV1& sample)
{if constexpr(requires{state.Attempt(evaluation,false,{},sample);})return state.Attempt(evaluation,false,{},sample);
 else return state.Attempt(evaluation,false,{});}
template<class State>void HostModelReturned(State& state,const C::EvaluationId& evaluation)
{if constexpr(requires{state.ModelReturned(evaluation,{},true);})state.ModelReturned(evaluation,{},true);
 else state.ModelReturned(evaluation,{});}
template<class State,class Reader>bool HostReturned(State& state,const C::NativeStageDeliveryV1& delivery,const Reader& reader)
{if constexpr(requires{state.HostReturned(delivery,reader);})return state.HostReturned(delivery,reader);
 else return false;}
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-history",argc==2?std::string_view(argv[1]):std::string_view{});
#ifndef NR_NATIVE_RETURN_TRANSACTION
    r.Check("W03.return-transaction-required",false,true,"retained authenticated caller-return owner");
#else
    ReturnTests::Cases(r);
#endif
#ifdef NR_NATIVE_HISTORY
    namespace L=Neurotic::Lifecycle;namespace P=Neurotic::Protocol;
    const auto evaluation=Id<C::EvaluationId>("evaluation",1);
    L::NativeHistoryState history(Key("history.owner"),Key("history"));
    r.Check("T29.native-history-clean",!history.RequiresReset(),true,"NativeHistoryState");
    {
        P::NativeExecutionObserver observer(nullptr,&history,evaluation);
        r.Check("T29.native-history-attempt-latched",observer.ModelAttempt(false)&&history.RequiresReset(),true,"ModelAttempt");
        observer.ModelResult(true);
        // Simulate publication throwing after successful opaque evaluation.
    }
    r.Check("T29.native-history-publication-failure-sticky",history.Pending(),true,"NativeExecutionObserver destructor");
    auto next=evaluation;next.value++;
    r.Check("T29.native-history-reset-request-required",!history.Attempt(next,false),true,"NativeHistoryState.Attempt");
    r.Check("T29.native-history-reset-request-allowed",history.Attempt(next,true),true,"NativeHistoryState.Attempt");
    r.Check("T29.native-history-request-not-applied",history.Pending(),true,"NativeHistoryState");
    r.Check("T29.native-history-duplicate-refused",!history.Attempt(next,true),true,"NativeHistoryState.Attempt");
    history.Abandon(next);
    r.Check("T29.native-history-abandon-sticky",history.Pending(),true,"NativeHistoryState.Abandon");
    L::NativeHistoryState successful(Key("history.owner"),Key("history"));
    {
        P::NativeExecutionObserver observer(nullptr,&successful,evaluation);
        observer.ModelAttempt(false);observer.ModelResult(true);observer.AwaitFinalConsumption();
    }
    r.Check("T29.native-history-await-not-consume",successful.RequiresReset()&&!successful.Pending(),true,"NativeHistoryState");
    r.Check("T29.native-history-wrong-consume-refused",!successful.Consumed(next),true,"NativeHistoryState.Consumed");
    r.Check("T29.native-history-actual-consume",successful.Consumed(evaluation)&&!successful.RequiresReset(),true,"NativeHistoryState.Consumed");
    r.Check("T29.native-history-consume-not-reset",!history.Consumed(next)&&history.Pending(),true,"NativeHistoryState.Consumed");
    r.Check("T29.native-history-retired-evaluation-refused",!successful.Attempt(evaluation,false),true,"NativeHistoryState.Attempt");
    {
        NrContextTests::Store localStore;L::NativeHistoryState local(Key("history.owner"),Key("history"));
        C::NativeSampleIdentityV1 sample;sample.session=Id<C::SessionId>("session",1);
        sample.stream=Id<C::RenderStreamId>("stream",1);sample.view=Id<C::ViewId>("view",1);
        sample.feature=Id<C::ObjectIncarnation>("feature",1);sample.ingress=Id<C::ObjectIncarnation>("ingress",1);
        sample.producerOrdinal=1;sample.callback=Key("callback",1);
        auto image=std::make_unique<NrRepresentationTests::Fixture>();
        C::NativeDeliveryContractV1 obligation;obligation.kind=C::NativeDeliveryKind::HostReturn;
        obligation.profile=image->profile;
        obligation.placement=C::Placement::NativeAfter;
        obligation.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
        obligation.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
        C::NativeOutputContentV1 output;output.view=localStore.Publish(image->target,C::OwnerDomain::Resource);
        output.recording=Key("recording",1);output.producerOrdinal=4;output.semanticDomain=Sym("SceneLinear");
        C::NativeStageDeliveryV1 stage;
        stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage",1),1};
        stage.plan={Sym("PlanCommit"),Key("plan"),1};stage.route=Id<C::RouteGeneration>("route",1);
        stage.delivery=localStore.Publish(obligation,C::OwnerDomain::StreamCoordinator);
        stage.recipe={Sym("NrExecutionRecipe"),Key("recipe"),1};
        stage.executionResult={Sym("EvaluationResult"),Key("execution"),1};
        stage.sample=localStore.Publish(sample,C::OwnerDomain::Provider);stage.evaluation=evaluation;
        stage.admissionEpoch=Key("epoch",1);
        stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("bindings"),1};
        stage.nativeOutput=localStore.Publish(output,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
        stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;
        stage.caller=Id<C::ObjectIncarnation>("caller",1);
        stage.lastRecording=output.recording;stage.lastRecordedOrdinal=5;
        stage.outcome=C::NativeStageOutcome::HostReturnRecorded;stage.reason=Sym("Native.HostReturnRecorded");
        C::BoundedList<C::OwnerValueReference,16> causes;
        causes.Push({C::OwnerDomain::Provider,Sym("Host.ReturnOperation"),{1,0},Key("operation"),1});
        stage.causes.count=1;stage.causes.backing=localStore.Publish(causes,C::OwnerDomain::Provider);
        r.Check("T29.hostreturn-stage-fixture",stage.Check()==C::Error::None,true,"typed stage precondition");
        const bool attempted=HostAttempt(local,evaluation,sample);HostModelReturned(local,evaluation);
        r.Check("T29.hostreturn-c07-not-consumed",attempted&&local.RequiresReset(),true,"C07/model success alone does not clear history");
        auto wrong=stage;auto foreign=sample;++foreign.producerOrdinal;
        wrong.sample=localStore.Publish(foreign,C::OwnerDomain::Provider);
        r.Check("T29.hostreturn-wrong-sample",!HostReturned(local,wrong,localStore)&&local.RequiresReset(),true,"exact Native sample");
        r.Check("T29.hostreturn-local-delivery",HostReturned(local,stage,localStore)&&!local.RequiresReset(),true,"owned local return consumes history");
    }
    NrContextTests::Store store;
    C::ResetPlan plan;plan.header=NrContextTests::Header(C::ContractId::C08,C::OwnerDomain::ResetRules,90);
    C::BoundedList<C::RecordKey,32> affected;affected.Push(Key("history.owner"));plan.affectedOwners=NrContextTests::PublishList(store,affected);
    affected={};affected.Push(Key("history"));plan.affectedHistories=NrContextTests::PublishList(store,affected);
    plan.actions.Push(C::ResetAction::ResetHistory);plan.boundary.kind=Known(C::BoundaryKind::BeforeFg);
    C::ResetAcknowledgment ack;ack.header=NrContextTests::Header(C::ContractId::C08,C::OwnerDomain::History,91);
    ack.plan={Sym(C::ResetPlan::WireName),plan.header.record,1};ack.affectedOwner=Key("history.owner");
    ack.accepted=Known(true);ack.applied=Known(true);ack.acceptedBoundary=plan.boundary;
    const auto g1=Known(Gen(C::IdentityKind::HistoryGeneration,"history",1));
    const auto g2=Known(Gen(C::IdentityKind::HistoryGeneration,"history",2));
    ack.resultingLineage=g2;ack.appliedActions.Push(C::ResetAction::ResetHistory);
    L::NativeHistoryState delayed(Key("history.owner"),Key("history"),g1);
    CurrentAttempt(delayed,evaluation,false,g1);Returned(delayed,evaluation,g1);delayed.Abandon(evaluation);
    CurrentAttempt(delayed,next,true,g2);Returned(delayed,next,g2);delayed.Abandon(next);
    r.Check("T29.native-history-delayed-ack-new-failure",!delayed.Acknowledge(plan,ack,store)&&delayed.Pending(),true,"NativeHistoryState.Acknowledge");
    L::NativeHistoryState applied(Key("history.owner"),Key("history"),g1);
    CurrentAttempt(applied,evaluation,false,g1);Returned(applied,evaluation,g1);applied.Abandon(evaluation);
    r.Check("T29.native-history-fresh-applied-ack",applied.Acknowledge(plan,ack,store)&&!applied.Pending(),true,"NativeHistoryState.Acknowledge");
    L::NativeHistoryState inside(Key("history.owner"),Key("history"),g1);
    CurrentAttempt(inside,evaluation,true,g1);Returned(inside,evaluation,g2);inside.Abandon(evaluation);
    r.Check("T29.native-history-reset-inside-call",!inside.Acknowledge(plan,ack,store)&&inside.Pending(),true,"NativeHistoryState.Acknowledge");
    auto laterAck=ack;laterAck.resultingLineage=Known(Gen(C::IdentityKind::HistoryGeneration,"history",3));
    r.Check("T29.native-history-later-reset-clears",inside.Acknowledge(plan,laterAck,store)&&!inside.Pending(),true,"NativeHistoryState.Acknowledge");
    L::NativeHistoryState unknown(Key("history.owner"),Key("history"),g1);
    CurrentAttempt(unknown,evaluation,true,g1);Returned(unknown,evaluation,{});unknown.Abandon(evaluation);
    r.Check("T29.native-history-unknown-post-call",!unknown.Acknowledge(plan,ack,store)&&unknown.Pending(),true,"NativeHistoryState.Acknowledge");
    P::NativeExecutionObserver actual;actual.ModelAttempt(true);actual.ModelResult(false);
    auto fixture=std::make_unique<FinalizerTests::Fixture>();
    const auto result=P::BuildNativeEvaluationResult(fixture->request.recipe,fixture->candidate.result.header,actual.Facts(),fixture->store);
    auto receiptHeader=fixture->candidate.result.header;receiptHeader.contract=C::ContractId::C11;
    const auto receipt=P::BuildNativeExecutionReceipt(fixture->request.recipe,*result,actual.Facts(),receiptHeader,1,fixture->request.generations,{},fixture->store);
    const C::BoundedList<C::OwnerFact,16>* values=nullptr;bool requested=false;
    if(receipt&&Neurotic::Context::ResolveList(receipt->facts,fixture->store,values)&&values)
        for(const auto& value:*values)if(value.field==Sym("ActualResetRequested")&&value.value.IsKnown())
            if(const auto* flag=std::get_if<bool>(&value.value.KnownPart()->value))requested=*flag;
    r.Check("T29.native-history-actual-reset-receipt",requested,true,"BuildNativeExecutionReceipt");
#else
    r.Check("T29.native-history-production",false,true,"NativeHistoryState");
#endif
    return r.Finish();});
}
