#include "../contracts/Test002Support.h"
#include "../representations/RepresentationFixture.h"
#include <dlssnr/NativeParameterOverride.h>
#include <nr/context/NativeStageQualification.h>
#include <nr/lifecycle/NativeHistoryState.h>
#include <nr/protocol/NativeTemporalRuntime.h>
#include <limits>

using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace X=Neurotic::Context;
namespace P=Neurotic::Protocol;
namespace
{
// Synthetic external parameters. The tested restore/qualification/history
// methods are production code; this fixture is not an NGX return publisher.
struct Parameters
{
    void* value=nullptr;
    bool ignore=false,throwSet=false;
    unsigned sets=0;
    int Get(const char*,void** out){*out=value;return 0;}
    void Set(const char*,void* next)
    {++sets;if(!ignore)value=next;if(throwSet)throw std::runtime_error("controlled Set after mutation");}
};
struct ReturnFixture
{
    NrRepresentationTests::Fixture image;
    C::NativeSampleIdentityV1 sample;
    C::NativeStageDeliveryV1 stage;
    C::NativeOutputContentV1 content;
    ReturnFixture()
    {
        auto& store=image.store;
        sample.session=Id<C::SessionId>("session",1);sample.stream=Id<C::RenderStreamId>("stream",1);
        sample.view=Id<C::ViewId>("view",1);sample.feature=Id<C::ObjectIncarnation>("feature",1);
        sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;sample.callback=Key("callback",1);
        C::NativeDeliveryContractV1 obligation;obligation.kind=C::NativeDeliveryKind::HostReturn;
        obligation.profile=image.profile;obligation.placement=C::Placement::NativeBefore;
        obligation.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
        obligation.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
        content.view=store.Publish(image.target,C::OwnerDomain::Resource);
        content.recording=Key("recording",1);content.producerOrdinal=4;content.semanticDomain=Sym("SceneLinear");
        stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage",1),1};
        stage.plan={Sym("PlanCommit"),Key("plan"),1};stage.route=Id<C::RouteGeneration>("route",1);
        stage.delivery=store.Publish(obligation,C::OwnerDomain::StreamCoordinator);
        stage.recipe={Sym("NrExecutionRecipe"),Key("recipe"),1};
        stage.executionResult={Sym("EvaluationResult"),Key("execution"),1};
        stage.sample=store.Publish(sample,C::OwnerDomain::Provider);stage.evaluation=Id<C::EvaluationId>("evaluation",1);
        stage.admissionEpoch=Key("epoch",1);stage.placement=C::Placement::NativeBefore;
        stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("bindings"),1};
        stage.nativeOutput=store.Publish(content,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
        stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=Id<C::ObjectIncarnation>("caller",1);
        stage.lastRecording=content.recording;stage.lastRecordedOrdinal=5;
        stage.outcome=C::NativeStageOutcome::HostReturnRecorded;stage.reason=Sym("Native.HostReturnRecorded");
        C::BoundedList<C::OwnerValueReference,16> causes;
        causes.Push({C::OwnerDomain::Provider,Sym("Host.ReturnOperation"),{1,0},Key("operation"),1});
        stage.causes.count=1;stage.causes.backing=store.Publish(causes,C::OwnerDomain::Provider);
    }
    void Next()
    {
        ++sample.producerOrdinal;++sample.callback.value;++stage.evaluation.value;++stage.publication.record.value;
        stage.sample=image.store.Publish(sample,C::OwnerDomain::Provider);
    }
};
void Restoration(Report& r)
{
    int original=0,scratch=0;
    // Return before/after the controlled call and exception unwinding all pass
    // through the actual one-attempt RAII guard. No GPU rollback is modeled.
    const auto exercise=[&](unsigned exitPoint)
    {
        Parameters parameters;parameters.value=&original;
        unsigned calls=0;bool bound=false,returned=false,threw=false;
        const auto body=[&]
        {
            DlssNr::NativeParameterOverride restore(&parameters,"Output",&original,0);
            bound=restore.Bind(&scratch);if(!bound)return;
            if(exitPoint==0)return;
            ++calls;
            if(exitPoint==2)throw std::runtime_error("controlled caller throws");
            if(exitPoint==1)return;
            returned=restore.Restore();
        };
        try{body();}catch(const std::runtime_error&){threw=true;}
        return bound&&parameters.value==&original&&parameters.sets==2&&calls==(exitPoint==0?0u:1u)&&
            threw==(exitPoint==2)&&(exitPoint!=3||returned);
    };
    r.Check("O07.early-before-call",exercise(0),true,"actual RAII restoration before controlled early return");
    r.Check("O07.early-after-call",exercise(1),true,"actual RAII restoration after controlled call before return");
    r.Check("O07.throwing-call",exercise(2),true,"exception unwinding restores exact original CPU binding once");
    r.Check("O07.normal-explicit-restore",exercise(3),true,"normal explicit restoration precedes return; destructor does not repeat");
    Parameters parameters;parameters.value=&original;P::NativeExecutionObserver observer;
    bool committed=false;
    {
        DlssNr::NativeParameterOverride restore(&parameters,"Output",&original,0);
        if(!restore.Bind(&scratch))throw std::runtime_error("restore fixture Bind failed");
        parameters.ignore=true;
        if(!restore.Restore())observer.OutputRestoreFailed();
        committed=P::NativeMayContinueOuterEvaluation(observer.Facts());
    }
    r.Check("O07.failed-restore-blocks-continuation",!committed&&parameters.value==&scratch&&observer.Facts().outputRestoreFailed,
        true,"failed restoration remains explicit; no subsequent outer evaluation permission");
}
void ExactOutput(Report& r)
{
    auto f=std::make_unique<ReturnFixture>();
    r.Check("O06.exact-content-control",X::SameNativeOutputContent(f->content,f->content,f->image.store),true,
        "production exact-content predicate with synthetic Resource-owner metadata; not actual HostReturn");
    const auto refused=[&](auto change)
    {
        auto changed=f->image.target;change(changed);auto other=f->content;
        other.view=f->image.store.Publish(changed,C::OwnerDomain::Resource);
        return !X::SameNativeOutputContent(f->content,other,f->image.store);
    };
    r.Check("O06.wrong-allocation",refused([](auto& view){auto value=view.identity.objectIncarnation.KnownPart()->value;++value.value;view.identity.objectIncarnation=Known(value);}),true,"exact allocation identity");
    r.Check("O06.wrong-generation",refused([](auto& view){auto value=view.identity.resourceGeneration.KnownPart()->value;++value.identity.value;view.identity.resourceGeneration=Known(value);}),true,"resource generation participates");
    r.Check("O06.stale-content",refused([](auto& view){view.identity.contentRevision=Known(C::ContentRevision{2});}),true,"current content revision participates");
    r.Check("O06.wrong-subresource",refused([](auto& view){view.mip=Known(std::uint32_t{1});}),true,"subresource and its allocation must match");
    r.Check("O06.wrong-region",refused([](auto& view){auto active=view.raster.active.KnownPart()->value;--active.width;view.raster.active=Known(active);}),true,"full active region participates");
    auto overflow=f->image.target;
    overflow.raster.active=Known(C::Rectangle{(std::numeric_limits<std::uint32_t>::max)(),0,2,2});
    auto impossible=f->content;
    impossible.view=f->image.store.Publish(overflow,C::OwnerDomain::Resource);
    r.Check("O06.overflow-region",!X::ResolveNativeOutput(impossible,f->image.store)&&
        !X::SameNativeOutputContent(impossible,impossible,f->image.store),true,
        "overflow cannot qualify even when both compared content records are the same invalid value");
    auto changed=f->content;changed.recording=Key("recording",2);
    r.Check("O06.wrong-recording",!X::SameNativeOutputContent(f->content,changed,f->image.store),true,"recording incarnation is exact");
    changed=f->content;changed.view=f->image.store.Publish(f->image.target,C::OwnerDomain::Provider);
    r.Check("O06.substituted-owner",!X::ResolveNativeOutput(changed,f->image.store),true,"Provider metadata cannot issue Resource physical content");
}
void OrderedHistory(Report& r)
{
    auto f=std::make_unique<ReturnFixture>();L::NativeHistoryState history(Key("history.owner"),Key("history"));
    bool ordered=true,modelOnlyRejected=true,exact=true;
    for(unsigned i=0;i<3;++i)
    {
        const auto evaluation=f->stage.evaluation;
        ordered=history.Attempt(evaluation,false,{},f->sample)&&ordered;
        exact=history.ModelReturned(evaluation,{},true)&&exact;
        modelOnlyRejected=history.RequiresReset()&&modelOnlyRejected;
        exact=history.HostReturned(f->stage,f->image.store)&&exact;
        ordered=!history.RequiresReset()&&history.InvocationClosed()&&ordered;
        f->Next();
    }
    r.Check("L02.ordered-local-history-without-final-consumption",ordered&&exact,true,
        "three typed fixture local events use actual History companion without any C13/Present/completion event");
    r.Check("L02.model-result-not-local-consumption",modelOnlyRejected,true,"model return alone cannot clear active history");
    r.Check("L11.next-after-local-return-no-present",history.Attempt(f->stage.evaluation,false,{},f->sample),true,
        "actual History companion admits next sample after local return with no display/final signal supplied");
    history.Abandon(f->stage.evaluation);
    r.Check("L03.abandoned-predecessor-requires-reset",history.RequiresReset()&&history.Pending(),true,"abandoned possible advancement remains reset debt");
    f->Next();
    r.Check("L03.no-reset-successor-refused",!history.Attempt(f->stage.evaluation,false,{},f->sample),true,"ordered sample labels cannot erase predecessor debt");
    r.Check("L03.reset-request-not-ack",history.Attempt(f->stage.evaluation,true,{},f->sample)&&history.Pending(),true,"a requested reset is not owner acknowledgment");
    history.Abandon(f->stage.evaluation);
}
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("spectre-return-history",argc==2?std::string_view(argv[1]):std::string_view{});
        Restoration(r);ExactOutput(r);OrderedHistory(r);return r.Finish();});
}
