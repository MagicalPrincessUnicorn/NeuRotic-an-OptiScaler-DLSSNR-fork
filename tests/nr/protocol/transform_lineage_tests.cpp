#include "../representations/RepresentationFixture.h"
#include <nr/protocol/NrExecutionRecipe.h>
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("protocol-lineage",argc==2?std::string_view(argv[1]):std::string_view{});
    namespace P=Neurotic::Protocol;namespace S=Neurotic::Context;
    auto fixture=std::make_unique<NrRepresentationTests::Fixture>();auto plan=fixture->Plan();
    P::RepresentationUse use;use.source={C::OwnerDomain::Context,Sym("C12.ConsumerUse"),{1,0},Key("use"),1};
    for(const auto kind:{C::TransformKind::MotionScale,C::TransformKind::JitterRemoval,C::TransformKind::ExposureUndo,C::TransformKind::ColorTransfer})
    {
        C::TransformStep step;step.kind=kind;step.operation=Sym("LegacyNative.Binding.v1");step.version=1;
        step.sourceUnits=Known(Sym("Original"));step.targetUnits=Known(Sym("Consumer"));step.qualification=Known(fixture->meaning);step.alreadyApplied=Known(false);
        S::TransformLineage required;required.Push(step);auto applied=required;applied.Get(0)->alreadyApplied=Known(true);
        use.required=NrContextTests::PublishList(fixture->store,required);use.applied=NrContextTests::PublishList(fixture->store,applied);
        use.consumerDeferred={};plan.transforms={};use.prepared={};
        const std::string prefix=kind==C::TransformKind::MotionScale?"T14.motion":kind==C::TransformKind::JitterRemoval?"T14.jitter":kind==C::TransformKind::ExposureUndo?"T14.exposure":"T14.color";
        r.Check(prefix+"-already-applied",P::LineageMatches(use,plan,fixture->store),true,"LineageMatches");
        use.consumerDeferred=use.required;
        r.Check(prefix+"-no-double-transform",!P::LineageMatches(use,plan,fixture->store),true,"LineageMatches");
        use.consumerDeferred={};required.Push(step);use.required=NrContextTests::PublishList(fixture->store,required);
        r.Check(prefix+"-duplicate-request-refused",!P::LineageMatches(use,plan,fixture->store),true,"LineageMatches");
    }
    C::CanonicalFrameContext context;auto bindings=std::make_unique<P::BindingPlan>();
    r.Check("T14.binding-precondition",P::BuildFrameControls(context,Ev(),fixture->store,*bindings),true,"BuildFrameControls");
    C::TransformStep scale;scale.kind=C::TransformKind::MotionScale;scale.operation=Sym("LegacyNative.Binding.v1");scale.version=1;
    scale.sourceUnits=Known(Sym("Raw"));scale.targetUnits=Known(Sym("Pixels"));scale.qualification=Known(fixture->meaning);scale.alreadyApplied=Known(false);
    C::BoundedList<C::SemanticClaim,8> parameters;
    for(const auto key:{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY"})
    {C::SemanticClaim parameter;parameter.field=Sym(key);parameter.effective=Known(C::ScalarValue{2.0});parameters.Push(parameter);}
    scale.parameters=NrContextTests::PublishList(fixture->store,parameters);S::TransformLineage delta;delta.Push(scale);
    use.applied={};use.required=NrContextTests::PublishList(fixture->store,delta);use.consumerDeferred=use.required;
    use.purpose=P::Purpose::MotionVectorGuide;plan.transforms=use.required;
    r.Check("T14.exact-consumer-transform",P::LineageMatches(use,plan,fixture->store)&&P::ApplyConsumerControls(use,fixture->store,*bindings)&&std::get<double>(P::FindBinding(*bindings,"DLSSNR.MVecScaleX")->effective.KnownPart()->value)==2,true,"ApplyConsumerControls");
    use.applied=use.required; // false applied flag cannot attest actual preparation
    r.Check("T14.false-applied-flag",!P::LineageMatches(use,plan,fixture->store),true,"LineageMatches");
    return r.Finish();});
}
