#define NR_NATIVE_PROTOCOL_FIXTURE_TESTING 1
#include "native_fixture.h"
#include "../finalizer/finalizer_fixture.h"
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
using namespace NrTest002;
namespace P=Neurotic::Protocol;
namespace L=Neurotic::Lifecycle;
struct IntegrationFixture
{
    NativeTests::Fixture native;
    FinalizerTests::Fixture life;
    L::PrimaryClaimRequest request;
    IntegrationFixture()
    {
        auto context=*native.store.Resolve(native.request.context).value;
        auto frame=*life.store.Resolve(life.request.recipe.frame).value;
        context.frame=native.store.Publish(frame);
        native.request.context=native.store.Publish(context);
        native.request.externalPlan=life.request.recipe.committedPlan;native.Rebuild();
        request=life.request;request.recipe=native.product->recipe;
        request.expectedConsumer=native.store.Publish(*life.store.Resolve(*life.request.expectedConsumer).value,C::OwnerDomain::FrameGeneration);
        request.generations=native.store.Publish(*life.store.Resolve(life.request.generations).value);
    }
};
struct Owner:P::NativeOperationOwner
{
    FinalizerTests::Fixture& fixture;
    P::NativeCheckPoint failAt=P::NativeCheckPoint::BeforeSubmission;
    bool fail=false;unsigned calls=0;
    explicit Owner(FinalizerTests::Fixture& f):fixture(f){}
    P::NativeLeaseProof Refresh(P::NativeCheckPoint point,const P::NativeResourceUse& use)noexcept override
    {
        ++calls;if(fail&&point==failAt)return {};
        return {use.resource,&*fixture.lease,&fixture.facts};
    }
};
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-lifecycle",argc==2?std::string_view(argv[1]):std::string_view{});
    auto f=std::make_unique<IntegrationFixture>();Owner owner(f->life);
    unsigned executions=0;
    const auto output=Known(f->native.store.Publish(f->native.source,C::OwnerDomain::Resource));
    auto executor=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& observer){
        ++executions;
        const auto uses=P::NativeResources({{f->life.binding.purpose,reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
        if(!observer.Check(P::NativeCheckPoint::BeforeRecord,uses))return observer.Facts();
        observer.Recorded();
        if(!observer.Check(P::NativeCheckPoint::BeforeModel,uses))return observer.Facts();
        observer.ModelAttempt();observer.ModelResult(true);
        if(!observer.Check(P::NativeCheckPoint::BeforeComposition,uses))return observer.Facts();
        observer.Composed();auto facts=observer.Facts();facts.composition=C::CompositionStatus::Accepted;
        facts.output=output;return facts;
    };
    auto events=[&]{auto e=f->life.Event();e.generations=f->request.generations;return e;};
    const auto run=[&]{return P::ExecuteNativeProtocol(*f->native.product,f->request,f->life.candidate.result.header,
        *f->life.finalizer,owner,f->native.store,events,executor);};
    const auto first=run();
    r.Check("T30.native-integrated-produced",first.evaluation&&first.evaluation->stage==C::OutcomeStage::Produced&&first.submittedToFinalizer,true,"ExecuteNativeProtocol");
    r.Check("T30.native-integrated-three-rechecks",owner.calls==3&&executions==1,true,"ExecuteNativeProtocol");
    r.Check("T28.native-contribution-not-seal",first.claim&&!f->life.finalizer->Snapshot(*first.claim),true,"ExecuteNativeProtocol");
    const auto duplicate=run();
    r.Check("T27.native-integrated-no-second-call",!duplicate.claim&&executions==1&&!duplicate.legacyAllowed,true,"ExecuteNativeProtocol");
    const auto uses=P::NativeResources({{f->life.binding.purpose,reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
    P::NativeExecutionObserver observer(&owner);
    r.Check("T16.native-order-before-cpu-completion",observer.Check(P::NativeCheckPoint::BeforeModel,uses),true,"ValidateNativeLease");
    f->life.facts.dependency.crossQueue=true;f->life.facts.dependency.nonReplayable=L::Fact::No;
    P::NativeExecutionObserver replay(&owner);
    r.Check("T17.native-replay-refused",!replay.Check(P::NativeCheckPoint::BeforeModel,uses),true,"ValidateNativeLease");
    f->life.facts.dependency.crossQueue=false;f->life.facts.dependency.nonReplayable=L::Fact::Yes;
    f->life.facts.dependency.sameDevice=L::Fact::No;P::NativeExecutionObserver foreign(&owner);
    r.Check("T18.native-cross-device-refused",!foreign.Check(P::NativeCheckPoint::BeforeModel,uses),true,"ValidateNativeLease");
    f->life.facts.dependency.sameDevice=L::Fact::Yes;
    auto writes=uses;writes.Get(0)->write=true;P::NativeExecutionObserver write(&owner);
    r.Check("T19.native-held-read-not-write",!write.Check(P::NativeCheckPoint::BeforeComposition,writes),true,"ValidateNativeLease");
    auto ordinary=f->life.binding;ordinary.description.use=C::UsageKind::OrderedGpuRead;
    auto ordinaryFacts=f->life.facts;ordinaryFacts.current=ordinary;ordinaryFacts.holds={};
    auto ordinaryLease=L::BeginConsumption(ordinary,ordinaryFacts);
    r.Check("T19.native-model-read-needs-provider-retention",ordinaryLease&&
        !P::ValidateNativeLease(*uses.Get(0),{uses.Get(0)->resource,&*ordinaryLease,&ordinaryFacts},P::NativeCheckPoint::BeforeModel),true,"ValidateNativeLease");
    auto callback=ordinary;callback.description.use=C::UsageKind::CallbackRead;callback.dependencyRequired=false;
    auto callbackFacts=ordinaryFacts;callbackFacts.current=callback;callbackFacts.callbackActive=L::Fact::Yes;
    auto callbackLease=L::BeginConsumption(callback,callbackFacts);
    r.Check("T16.native-gpu-operation-not-cpu-borrow",callbackLease&&
        !P::ValidateNativeLease(*uses.Get(0),{uses.Get(0)->resource,&*callbackLease,&callbackFacts},P::NativeCheckPoint::BeforeRecord),true,"ValidateNativeLease");
    auto modelWrite=ordinary;modelWrite.description.use=C::UsageKind::WriteExclusive;
    auto modelWriteFacts=ordinaryFacts;modelWriteFacts.current=modelWrite;
    modelWriteFacts.dependency.reusable=L::Fact::Yes;modelWriteFacts.consumerTicketsRetired=L::Fact::Yes;
    modelWriteFacts.ownerCanRecycle=L::Fact::Yes;
    auto modelWriteLease=L::BeginConsumption(modelWrite,modelWriteFacts);
    r.Check("T19.native-model-output-needs-provider-retention",modelWriteLease&&
        !P::ValidateNativeLease(*writes.Get(0),{writes.Get(0)->resource,&*modelWriteLease,&modelWriteFacts},P::NativeCheckPoint::BeforeModel),true,"ValidateNativeLease");
    auto failed=std::make_unique<IntegrationFixture>();Owner blocked(failed->life);blocked.fail=true;blocked.failAt=P::NativeCheckPoint::BeforeModel;
    auto failedEvents=[&]{auto e=failed->life.Event();e.generations=failed->request.generations;return e;};
    const auto failure=P::ExecuteNativeProtocol(*failed->native.product,failed->request,failed->life.candidate.result.header,
        *failed->life.finalizer,blocked,failed->native.store,failedEvents,executor);
    r.Check("T29.native-mid-call-revocation",failure.evaluation&&failure.evaluation->stage==C::OutcomeStage::Failed&&!failure.facts.modelAttempted&&!failure.legacyAllowed,true,"ExecuteNativeProtocol");
    auto fabricated=std::make_unique<IntegrationFixture>();Owner fabricatedOwner(fabricated->life);
    const auto fabricatedOutput=Known(fabricated->native.store.Publish(fabricated->native.source,C::OwnerDomain::Resource));
    auto fabricatedEvents=[&]{auto e=fabricated->life.Event();e.generations=fabricated->request.generations;return e;};
    auto fabricate=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& report){
        report.ModelAttempt();report.ModelResult(true);auto facts=report.Facts();
        facts.composition=C::CompositionStatus::Accepted;facts.output=fabricatedOutput;return facts;};
    const auto unobserved=P::ExecuteNativeProtocol(*fabricated->native.product,fabricated->request,fabricated->life.candidate.result.header,
        *fabricated->life.finalizer,fabricatedOwner,fabricated->native.store,fabricatedEvents,fabricate);
    r.Check("T30.native-unobserved-composition",unobserved.evaluation&&unobserved.evaluation->stage==C::OutcomeStage::Failed&&!unobserved.evaluation->output.IsKnown(),true,"ExecuteNativeProtocol");
    auto resetMismatch=std::make_unique<IntegrationFixture>();Owner resetOwner(resetMismatch->life);
    auto resetEvents=[&]{auto e=resetMismatch->life.Event();e.generations=resetMismatch->request.generations;return e;};
    auto misreport=[](const P::NativeBindingMap&,P::NativeExecutionObserver& report){
        report.ModelAttempt(true);report.ModelResult(true);auto facts=report.Facts();facts.actualResetRequested=false;return facts;};
    const auto resetResult=P::ExecuteNativeProtocol(*resetMismatch->native.product,resetMismatch->request,resetMismatch->life.candidate.result.header,
        *resetMismatch->life.finalizer,resetOwner,resetMismatch->native.store,resetEvents,misreport);
    r.Check("T29.native-reset-argument-not-overwritable",resetResult.evaluation&&resetResult.evaluation->stage==C::OutcomeStage::Failed&&
        resetResult.facts.actualResetRequested.value_or(false),true,"ExecuteNativeProtocol");
    return r.Finish();});
}
