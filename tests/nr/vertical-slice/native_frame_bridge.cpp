#include "native_fixture.h"
#if __has_include(<nr/protocol/NativeFrameBridge.h>)
#include <nr/protocol/NativeFrameBridge.h>
#define NR_NATIVE_FRAME 1
#endif
using namespace NrTest002;
#ifdef NR_NATIVE_FRAME
#include <dlssnr/connections/MixedInputSelection.h>
namespace
{
namespace P=Neurotic::Protocol;
namespace S=Neurotic::Context;
using Fields=C::BoundedList<C::SemanticClaim,16>;
C::SemanticClaim ScalarClaim(std::string_view key,double value,bool observed=true)
{
    C::SemanticClaim c;c.field=Sym(key);c.effective=Known(C::ScalarValue{value});
    if(observed)c.raw=c.effective;return c;
}
Fields Controls(std::optional<double> x,std::optional<double> y,std::optional<double> pre,bool observed=true)
{
    Fields fields;
    if(x)fields.Push(ScalarClaim("Jitter.Offset.X",*x,observed));
    if(y)fields.Push(ScalarClaim("Jitter.Offset.Y",*y,observed));
    if(pre)fields.Push(ScalarClaim("DLSS.Pre.Exposure",*pre,observed));
    for(const auto& [key,value]:std::array{std::pair{"jitter.units","Pixels"},std::pair{"jitter.basis","motion.currentRaster"}})
    {C::SemanticClaim c;c.field=Sym(key);c.raw=Known(C::ScalarValue{Sym(value)});c.effective=c.raw;fields.Push(c);}
    return fields;
}
void PublishFields(NativeTests::Fixture& f,const Fields& fields)
{
    auto context=*S::ResolveMetadata(f.request.context,f.store);
    context.fields=NrContextTests::PublishList(f.store,fields);f.request.context=f.store.Publish(context);
}
std::optional<DlssNrFrameInfo> Project(NativeTests::Fixture& f)
{
    const auto built=P::BuildRecipe(f.request,f.store);if(!built.product)return {};
    f.product=std::make_unique<P::RecipeProduct>(built.product->Value());
    const auto map=P::BuildNativeBindingMap(*f.product,f.store);
    return map?P::BuildNativeFrameInfo(*map,*f.product,f.store):std::optional<DlssNrFrameInfo>{};
}
void ConsumerLineage(NativeTests::Fixture& f,bool applied)
{
    for(auto& use:f.request.representations)
    {
        if(use.purpose!=P::Purpose::MotionVectorGuide&&use.purpose!=P::Purpose::ColorModelInput)continue;
        C::TransformStep step;step.kind=use.purpose==P::Purpose::MotionVectorGuide?C::TransformKind::JitterRemoval:C::TransformKind::ExposureUndo;
        step.operation=Sym("LegacyNative.Binding.v1");step.version=1;
        step.sourceUnits=Known(Sym("Original"));step.targetUnits=Known(Sym("Consumer"));
        step.qualification=Known(f.meaning);step.alreadyApplied=Known(false);
        C::BoundedList<C::SemanticClaim,8> params;
        if(use.purpose==P::Purpose::MotionVectorGuide)
        {params.Push(ScalarClaim("Jitter.Offset.X",0.5));params.Push(ScalarClaim("Jitter.Offset.Y",-0.75));}
        else params.Push(ScalarClaim("DLSS.Pre.Exposure",3));
        step.parameters=NrContextTests::PublishList(f.store,params);
        S::TransformLineage required;required.Push(step);use.required=NrContextTests::PublishList(f.store,required);
        if(applied)
        {step.alreadyApplied=Known(true);S::TransformLineage done;done.Push(step);use.applied=NrContextTests::PublishList(f.store,done);}
        else
        {use.consumerDeferred=use.required;auto plan=*S::ResolveMetadata(use.plan,f.store);plan.transforms=use.required;use.plan=f.store.Publish(plan);}
    }
}
void MetadataRegressions(Report& r)
{
    unsigned ordinal=0;
    const auto check=[&](std::string_view name,bool value){r.Check(std::string("T30.metadata-")+std::string(name)+"-"+std::to_string(++ordinal),value,true,"BuildNativeFrameInfo");};
    for(bool before:{false,true})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false,before);PublishFields(*f,Controls(0,0,1));
        auto frame=Project(*f);check(before?"before-zero-observed":"after-zero-observed",frame&&frame->ColorDomainKnown&&frame->JitterSupplied&&frame->PreExposureSupplied&&frame->JitterX==0&&frame->JitterY==0&&frame->PreExposure==1);
        PublishFields(*f,Controls(0.25,-0.5,2));frame=Project(*f);
        check(before?"before-nonzero":"after-nonzero",frame&&frame->JitterSupplied&&frame->PreExposureSupplied&&frame->JitterX==0.25f&&frame->JitterY==-0.5f&&frame->PreExposure==2);
    }
    for(bool onlyX:{false,true})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);PublishFields(*f,Controls(onlyX?std::optional<double>{0.25}:std::nullopt,onlyX?std::nullopt:std::optional<double>{-0.5},{}));
        const auto frame=Project(*f);check(onlyX?"only-x":"only-y",frame&&!frame->JitterSupplied&&!frame->PreExposureSupplied&&frame->JitterX==0&&frame->JitterY==0);
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);PublishFields(*f,Controls(0.25,-0.5,2,false));
        const auto frame=Project(*f);check("effective-is-not-source",frame&&!frame->JitterSupplied&&!frame->PreExposureSupplied&&frame->JitterX==0.25f&&frame->PreExposure==2);
    }
    for(double raw:{0.0,-1.0,1e-8,static_cast<double>(1e-6f)})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);PublishFields(*f,Controls({}, {},raw));
        const auto frame=Project(*f);check("unusable-original-exposure",frame&&!frame->PreExposureSupplied&&frame->PreExposure==1);
    }
    for(bool y:{false,true})for(double raw:{(std::numeric_limits<double>::max)(),-(std::numeric_limits<double>::max)()})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);
        fields.Get(y?1:0)->raw=Known(C::ScalarValue{raw});PublishFields(*f,fields);
        const auto frame=Project(*f);check("overflow-original-axis",frame&&!frame->JitterSupplied&&frame->PreExposureSupplied);
    }
    for(double raw:{(std::numeric_limits<double>::max)(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);
        fields.Get(2)->raw=Known(C::ScalarValue{raw});PublishFields(*f,fields);const auto frame=Project(*f);
        check("invalid-original-exposure",std::isfinite(raw)?frame&&!frame->PreExposureSupplied:!frame);
    }
    for(bool y:{false,true})for(double raw:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);
        fields.Get(y?1:0)->raw=Known(C::ScalarValue{raw});PublishFields(*f,fields);
        check("nonfinite-original-axis-owner-refusal",!Project(*f));
    }
    for(bool rawWrongType:{false,true})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);
        if(rawWrongType)fields.Get(0)->raw=Known(C::ScalarValue{std::uint64_t{1}});
        else fields.Get(0)->raw={};
        PublishFields(*f,fields);const auto frame=Project(*f);
        check(rawWrongType?"wrong-original-type":"unknown-original",frame&&!frame->JitterSupplied&&frame->PreExposureSupplied);
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);
        fields.Get(0)->effective=Known(C::ScalarValue{std::uint64_t{1}});PublishFields(*f,fields);
        check("wrong-effective-type-owner-refusal",!Project(*f));
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);const double raw=std::nextafter(static_cast<double>(1e-6f),(std::numeric_limits<double>::max)());
        PublishFields(*f,Controls({}, {},raw));const auto frame=Project(*f);
        check("float-threshold-boundary",frame&&!frame->PreExposureSupplied&&frame->PreExposure==1e-6f);
    }
    for(bool applied:{false,true})
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);PublishFields(*f,Controls(0.25,-0.5,2));ConsumerLineage(*f,applied);
        const auto frame=Project(*f);const auto* jitter=f->product?P::FindBinding(f->product->bindingPlan,"Jitter.Offset.X"):nullptr;
        const auto* exposure=f->product?P::FindBinding(f->product->bindingPlan,"DLSS.Pre.Exposure"):nullptr;
        check(applied?"already-applied-original-supply":"deferred-original-supply",frame&&frame->JitterSupplied&&frame->PreExposureSupplied&&
            frame->JitterX==(applied?0:0.5f)&&frame->JitterY==(applied?0:-0.75f)&&frame->PreExposure==(applied?1:3));
        check(applied?"already-applied-lineage":"deferred-lineage",jitter&&exposure&&P::Number(jitter->raw)&&*P::Number(jitter->raw)==0.25&&
            P::Number(exposure->raw)&&*P::Number(exposure->raw)==2&&jitter->effectiveSource.View()==(applied?"C12.AlreadyApplied":"C12.ConsumerTransform")&&exposure->effectiveSource==jitter->effectiveSource);
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);auto fields=Controls(0.25,-0.5,2);fields.Push(*fields.Get(0));PublishFields(*f,fields);
        check("duplicate-owner-refusal",!Project(*f));
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>(false);PublishFields(*f,Controls(0.25,-0.5,2));const auto frame=Project(*f);
        check("malformed-context-precondition",frame.has_value());
        if(!frame||!f->product)return;
        const auto map=P::BuildNativeBindingMap(*f->product,f->store);
        auto context=*S::ResolveMetadata(f->product->inputContext,f->store);auto fields=Controls(0.25,-0.5,2);fields.Push(*fields.Get(0));
        context.fields=NrContextTests::PublishList(f->store,fields);f->product->inputContext=f->store.Publish(context);
        check("duplicate-bridge-refusal",map&&!P::BuildNativeFrameInfo(*map,*f->product,f->store));
        PublishFields(*f,Controls(0.25,-0.5,2));const auto restored=Project(*f);check("dangling-precondition",restored.has_value());
        if(!restored||!f->product)return;
        const auto liveMap=P::BuildNativeBindingMap(*f->product,f->store);context=*S::ResolveMetadata(f->product->inputContext,f->store);
        f->store.Withdraw(*context.fields.backing);check("dangling-bridge-refusal",liveMap&&!P::BuildNativeFrameInfo(*liveMap,*f->product,f->store));
    }
    {
        auto f=std::make_unique<NativeTests::Fixture>();const auto map=P::BuildNativeBindingMap(*f->product,f->store);
        check("map-malformation-precondition",map.has_value());if(!map)return;
        auto duplicate=*map;for(const auto& command:map->evaluate)if(command.key.View()=="Jitter.Offset.X"){duplicate.evaluate.Push(command);break;}
        check("duplicate-command-refusal",!P::BuildNativeFrameInfo(duplicate,*f->product,f->store));
        auto nonfinite=*map;for(auto& command:nonfinite.evaluate)if(command.key.View()=="Jitter.Offset.X")command.number=std::numeric_limits<float>::infinity();
        check("nonfinite-effective-command-refusal",!P::BuildNativeFrameInfo(nonfinite,*f->product,f->store));
        auto context=*S::ResolveMetadata(f->product->inputContext,f->store);auto color=*S::ResolveOptional(context.color,f->store);
        color.domain={};context.color=Known(f->store.Publish(color));f->product->inputContext=f->store.Publish(context);
        check("unknown-domain-refusal",!P::BuildNativeFrameInfo(*map,*f->product,f->store));
        color.domain=Known(C::ColorDomain::SceneLinear);color.transfer=Known(Sym("sRGB"));context.color=Known(f->store.Publish(color));f->product->inputContext=f->store.Publish(context);
        check("incompatible-transfer-refusal",!P::BuildNativeFrameInfo(*map,*f->product,f->store));
    }
    for(auto domain:{C::ColorDomain::EncodedDisplay,C::ColorDomain::SceneLinear,C::ColorDomain::DisplayLinear,C::ColorDomain::Control})
    {
        auto f=std::make_unique<NativeTests::Fixture>();const auto map=P::BuildNativeBindingMap(*f->product,f->store);
        auto context=*S::ResolveMetadata(f->product->inputContext,f->store);auto color=*S::ResolveOptional(context.color,f->store);
        color.domain=Known(domain);context.color=Known(f->store.Publish(color));f->product->inputContext=f->store.Publish(context);
        const auto frame=map?P::BuildNativeFrameInfo(*map,*f->product,f->store):std::optional<DlssNrFrameInfo>{};
        check("qualified-color-domain",domain==C::ColorDomain::Control?!frame:frame&&frame->ColorDomainKnown&&frame->ColourIsLinearHdr==(domain!=C::ColorDomain::EncodedDisplay));
        if(frame)
        {
            namespace M=DlssNr::Connections::Mixed;M::Policy policy;policy.color=DlssNr::Connections::Source::Native;policy.strict=true;
            const auto selected=M::SelectNativeBindings(policy,{frame->ColorDomainKnown?M::Reason::Ready:M::Reason::UnknownConvention,M::Reason::Ready,M::Reason::Ready});
            const auto unknown=M::SelectNativeBindings(policy,{M::Reason::UnknownConvention,M::Reason::Ready,M::Reason::Ready});
            check("qualified-color-policy",selected.ready&&!unknown.ready&&unknown.colorReason==M::Reason::UnknownConvention);
        }
    }
}
}
#endif
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-frame",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_FRAME
    namespace P=Neurotic::Protocol;auto f=std::make_unique<NativeTests::Fixture>();
    const auto map=P::BuildNativeBindingMap(*f->product,f->store);
    r.Check("T30.native-frame-map",map.has_value(),true,"BuildNativeFrameInfo");
    if(map)
    {
        const auto frame=P::BuildNativeFrameInfo(*map,*f->product,f->store);
        r.Check("T30.native-frame-scalars",frame&&frame->MvScaleX==1&&frame->MvScaleY==1&&frame->PreExposure==1,true,"BuildNativeFrameInfo");
        r.Check("T30.native-frame-no-stale-exposure",frame&&frame->ExposureTexture==nullptr,true,"BuildNativeFrameInfo");
        r.Check("T30.native-frame-depth-hdr",frame&&!frame->DepthInverted&&frame->ColourIsLinearHdr,true,"BuildNativeFrameInfo");
        r.Check("T30.native-frame-baseline-metadata",frame&&frame->ColorDomainKnown&&!frame->JitterSupplied&&!frame->PreExposureSupplied,true,"BuildNativeFrameInfo");
        // Analytic fixture declares an 800x400 active rect inside a 1024x512 allocation.
        r.Check("T30.native-frame-guide-rects",frame&&frame->DepthSubrectWidth==800&&frame->MotionSubrectWidth==800&&frame->RenderSubrectHeight==400,true,"BuildNativeFrameInfo");
        auto stale=*map;stale.recipe.value++;
        r.Check("T30.native-frame-recipe-mismatch",!P::BuildNativeFrameInfo(stale,*f->product,f->store),true,"BuildNativeFrameInfo");
    }
    MetadataRegressions(r);
#else
    r.Check("T30.native-frame-production",false,true,"BuildNativeFrameInfo");
#endif
    return r.Finish();});
}
