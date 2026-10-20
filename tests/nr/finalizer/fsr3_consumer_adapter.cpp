#define NR_ORCH_INIT_TESTING 1
#define NR_NATIVE_FINALIZER_TESTING 1
#include <nr/lifecycle/Fsr3FinalConsumerPublication.h>
#include "finalizer_fixture.h"
#include "../orchestration/InitialInvocationFixture.h"
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace Neurotic::Lifecycle
{
struct NativeUnsealedTestAccess
{
    static NativeUnsealedRetirement Receipt(PrimaryNrClaimHandle claim,const C::NrExecutionRecipe& recipe,
        const C::NativeSampleIdentityV1& sample,const C::RecordKey& recording)
    {return NativeUnsealedRetirement(claim,recipe,sample,recording);}
};
}
struct ControlledRecording
{
    inline static bool terminal=false;
    static DlssNr::GpuSafety::Ticket Record(ID3D12GraphicsCommandList*)
    {auto anchor=std::make_shared<int>(0);return {anchor,reinterpret_cast<DlssNr::GpuSafety::Recording*>(anchor.get())};}
    static bool Terminal(const DlssNr::GpuSafety::Ticket& ticket){return ticket&&terminal;}
};
struct ResourceOwner final:L::Fsr3FinalResourceOwner
{
    FinalizerTests::Fixture& physical;C::ResourceView view;
    bool release=false,exclude=false,closed=false,live=true;
    unsigned activeGuards=0;
    std::function<void()> onDrop;
    bool TryRevoke(){if(activeGuards)return false;live=false;return true;}
    std::optional<L::NativeUnsealedRetirement> unsealed;
    explicit ResourceOwner(FinalizerTests::Fixture& f,C::ResourceView v):physical(f),view(std::move(v)){}
    struct Guard final:L::Fsr3FinalResourceGuard
    {
        ResourceOwner& owner;
        explicit Guard(ResourceOwner& o):owner(o){++owner.activeGuards;}
        ~Guard(){--owner.activeGuards;if(owner.onDrop)owner.onDrop();}
        const C::ResourceView& CurrentView()const noexcept override{return owner.view;}
        L::FinalFrameRuntimeProof Proof()const noexcept override{return owner.physical.Proof();}
        bool ProviderReleaseObserved()const noexcept override{return owner.release;}
        bool ExcludePrimary(const C::FrameIdentity&)override{if(owner.exclude)owner.closed=true;return owner.closed;}
        std::optional<L::NativeUnsealedRetirement> UnsealedTerminal()const override{return owner.unsealed;}
    };
    std::unique_ptr<L::Fsr3FinalResourceGuard> Acquire(ID3D12Resource*,ID3D12GraphicsCommandList*,const DlssNr::GpuSafety::Ticket&)override
    {return live?std::make_unique<Guard>(*this):nullptr;}
    void Complete()
    {
        L::CloseAdmission(*physical.lease);
        physical.facts.dependency.completed=L::Fact::Yes;physical.facts.dependency.nonReplayable=L::Fact::Yes;
        physical.facts.dependency.reusable=L::Fact::Yes;physical.facts.consumerTicketsRetired=L::Fact::Yes;physical.facts.ownerCanRecycle=L::Fact::Yes;
        auto hold=*physical.facts.holds.Get(0);hold.released=L::Fact::Yes;hold.releaseEvidence=Key("fsr.release");
        physical.facts.holds={};physical.facts.holds.Push(hold);
    }
};
void Connected(Report& r,int scenario)
{
    const bool dispatchSuccess=scenario!=1;
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
    owner->committedStage=stage;
    if(!session.root->SubmitCommittedStage(claim))throw std::runtime_error("stage join");
    physical->boundary.frame=recipe.frame;physical->boundary.generations=snapshot.generations;physical->boundary.output=outputRef;
    physical->boundary.candidate=stage.executionResult;physical->boundary.handoffContract=Sym("fixture.before-fg");
    physical->boundary.requiredHudIncluded=false;physical->boundary.boundary.hudIncluded=Known(false);physical->boundary.boundary.toneMapped=Known(false);
    physical->binding.plan=recipe.committedPlan.record;physical->binding.route=C::GenerationToken{snapshot.generation.Describe()};
    physical->binding.description.generations=snapshot.generations;physical->facts.current=physical->binding;
    physical->dependency.generations=snapshot.generations;physical->lease=L::BeginConsumption(physical->binding,physical->facts);
    auto resources=std::make_shared<ResourceOwner>(*physical,output);
    using Publication=L::Fsr3FinalConsumerPublication<InvocationStore,ControlledRecording>;
    auto* native=reinterpret_cast<ID3D12Resource*>(std::uintptr_t{1});
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(std::uintptr_t{2});
    auto* context=reinterpret_cast<void*>(std::uintptr_t{3});
    const auto prefix="fsr.case"+std::to_string(scenario)+".";
    auto check=[&](const char* name,bool value){r.Check(std::string(prefix)+name,value,true,"actual session/kernel/finalizer and controlled external Resource/recording owners");};
    auto unknown=snapshot.frame;unknown.baseRealFrameId={};
    check("unknown-base-refused",!Publication::Create(session.root,claim,unknown,physical->boundary,*stage.returnedOutput,owner->store,resources,native));
    auto foreignFrame=snapshot.frame;foreignFrame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",111));
    check("foreign-frame-refused",!Publication::Create(session.root,claim,foreignFrame,physical->boundary,*stage.returnedOutput,owner->store,resources,native));
    Fixture foreign;foreign.Activate();
    check("foreign-root-refused",!Publication::Create(foreign.root,claim,snapshot.frame,physical->boundary,*stage.returnedOutput,owner->store,resources,native));
    auto publication=Publication::Create(session.root,claim,snapshot.frame,physical->boundary,*stage.returnedOutput,owner->store,resources,native);
    if(!publication)throw std::runtime_error("connected publication");
    check("duplicate-tail-refused",!Publication::Create(session.root,claim,snapshot.frame,physical->boundary,*stage.returnedOutput,owner->store,resources,native));
    using Adapter=L::BasicFsr3FinalConsumerAdapter<ControlledRecording>;
    auto adapter=std::make_unique<Adapter>();
    adapter->ObserveContext(context);
    using Callback=Adapter::Callback;
    check("never-enrolled-ordinary-callback",adapter->Begin(92,native,list)==Callback::Unenrolled);
    check("never-enrolled-zero-after-ordinary-callback",adapter->Begin(0,native,list)==Callback::Unenrolled);
    check("wrong-context",!adapter->Enroll(reinterpret_cast<void*>(std::uintptr_t{4}),91,publication));
    if(scenario==8)
    {
        owner->history.Attempt(recipe.evaluation,true,{},snapshot.Sample());
        owner->history.ModelReturned(recipe.evaluation,{},true);owner->consumptionAllowed=true;
        // Match the real FSR owner failure path: root retention precedes Enroll,
        // and failed Enroll interrupts without adding a feature-local entry.
        publication->Interrupt();
        check("failed-enrollment-closes-history",owner->history.InvocationClosed());
        resources->Complete();resources->release=true;resources->exclude=true;
        resources->unsealed=L::NativeUnsealedTestAccess::Receipt(claim,recipe,*snapshot.Sample(),snapshot.recording);
        admission.reset();
        auto nextOwner=std::make_shared<SyntheticInvocation>(session.root.get(),11);
        auto next=Access::Admit(session.root,nextOwner);
        check("next-active-admission-polls-unindexed-tail",publication->Retired());
        check("next-active-admission-succeeds",next.has_value());
        check("old-claim-retired-without-closure",!Access::Finalizer(*session.root).Snapshot(claim));
        next.reset();check("failed-enrollment-root-drained",session.root->AdvanceClosure().closed);return;
    }
    check("enrolled",adapter->Enroll(context,91,publication));
    check("duplicate-frame",!adapter->Enroll(context,91,publication));
    check("unassociated-does-not-consume",adapter->Begin(92,native,list)==Callback::Unenrolled);
    owner->history.Attempt(recipe.evaluation,true,{},snapshot.Sample());owner->history.ModelReturned(recipe.evaluation,{},true);
    owner->consumptionAllowed=true;
    if(scenario>=2&&scenario<=4)
    {
        if(scenario==2)
            check("foreign-resource-refused",adapter->Begin(91,reinterpret_cast<ID3D12Resource*>(std::uintptr_t{99}),list)==Callback::Refused);
        if(scenario==3)
        {
            resources->view.identity.contentRevision=Known(C::ContentRevision{999});
            check("changed-content-refused",adapter->Begin(91,native,list)==Callback::Refused);
            resources->view=output;
        }
        if(scenario==4)
        {
            adapter->ObserveContext(nullptr);
            check("context-change-excludes-old-offer",adapter->Begin(91,native,list)==Callback::Unenrolled);
        }
        check("refusal-no-seal",!Access::Finalizer(*session.root).Snapshot(claim));
        check("refusal-closes-history",owner->history.InvocationClosed()&&owner->consumedCalls==0);
        ControlledRecording::terminal=true;resources->Complete();resources->release=true;resources->exclude=true;
        adapter->Advance();check("no-guessed-unsealed-terminal",!publication->Retired());
        resources->unsealed=L::NativeUnsealedTestAccess::Receipt(claim,recipe,*snapshot.Sample(),snapshot.recording);
        adapter->Advance();check("unsealed-exact-terminal-retirement",publication->Retired());
        admission.reset();check("unsealed-root-drained",session.root->AdvanceClosure().closed);return;
    }
    auto invocation=std::make_optional(adapter->BeginCall(91,native,list));
    check("authentic-callback-offered",invocation->status==Callback::Offered);
    check("live-borrow-spans-provider-entry",resources->activeGuards==1);
    check("revoke-between-offer-and-dispatch-blocked",!resources->TryRevoke());
    resources->live=true;
    check("dispatch-current-with-live-borrow",adapter->DispatchCurrent(*invocation));
    physical->facts.current.description.revoked=Known(true);
    check("reentrant-revocation-refuses-dispatch",!adapter->DispatchCurrent(*invocation));
    physical->facts.current.description.revoked=Known(false);
    bool dropReentered=false;
    resources->onDrop=[&]{adapter->Skip(92);session.root->FinalConsumerLogicalClosed(claim);dropReentered=true;};
    check("duplicate-callback-refused",adapter->Begin(91,native,list)==Callback::Refused);
    auto& finalizer=Access::Finalizer(*session.root);
    check("sole-finalizer-sealed",finalizer.Snapshot(claim).has_value());
    check("offer-not-consumption",!owner->history.InvocationClosed());
    if(scenario==5)adapter->ObserveContext(nullptr);
    if(scenario==7)owner->consumptionAllowed=false;
    adapter->End(*invocation,dispatchSuccess);adapter->End(*invocation,!dispatchSuccess);invocation.reset();
    resources->onDrop={};
    check("borrow-released-after-real-return",resources->activeGuards==0);
    check("borrow-destruction-outside-owner-locks",dropReentered);
    check("real-return-controls-consumption",owner->consumedCalls==(dispatchSuccess&&scenario!=7?1u:0u));
    check("return-closes-history",owner->history.InvocationClosed()==(scenario!=7));
    if(scenario==6)
    {
        const std::weak_ptr<Publication> tail=publication;
        adapter.reset();publication.reset();
        check("root-retains-after-feature-destruction",!tail.expired());
        ControlledRecording::terminal=true;resources->Complete();resources->release=false;resources->exclude=true;
        admission.reset();check("unknown-release-root-quarantines",!session.root->AdvanceClosure().closed&&!tail.expired());
        resources->release=true;
        check("root-advances-retained-consumer",session.root->AdvanceClosure().closed);
        check("drained-root-releases-tail",tail.expired());return;
    }
    ControlledRecording::terminal=false;resources->Complete();resources->release=true;resources->exclude=true;
    adapter->Advance();check("gpu-pending-retained",!publication->Retired());
    ControlledRecording::terminal=true;resources->release=false;
    adapter->Advance();check("terminal-not-provider-release",!publication->Retired());
    resources->release=true;resources->exclude=false;
    adapter->Advance();check("release-not-exclusion",!publication->Retired());
    resources->exclude=true;adapter->Advance();
    if(scenario==7)
    {
        check("history-owner-pending-retains-seal",!publication->Retired()&&finalizer.Snapshot(claim).has_value());
        owner->consumptionAllowed=true;adapter->Advance();
        check("history-owner-retry-acknowledged",owner->history.InvocationClosed()&&owner->consumedCalls==1);
    }
    check("exact-retirement",publication->Retired()&&resources->closed);
    check("retired-frame-no-reenroll",!adapter->Enroll(context,91,publication));
    if(scenario!=5)check("retired-callback-refused",adapter->Begin(91,native,list)==Callback::Refused);
    admission.reset();
    check("root-finalizer-acknowledged",session.root->AdvanceClosure().closed);
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("fsr3-consumer-adapter",argc==2?std::string_view(argv[1]):std::string_view{});
        for(int scenario=0;scenario!=9;++scenario)Connected(r,scenario);return r.Finish();});
}
