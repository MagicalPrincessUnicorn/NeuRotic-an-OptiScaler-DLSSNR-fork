#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/protocol/BindingPlan.h>)
#include <nr/protocol/BindingPlan.h>
#define NR_PROTOCOL_BINDINGS 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("protocol-freshness",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_PROTOCOL_BINDINGS
    namespace P=Neurotic::Protocol;
    const auto evidence=Ev(20);NrContextTests::Store store;
    C::CanonicalFrameContext context;
    C::BoundedList<C::SemanticClaim,16> fields;
    C::SemanticClaim x;x.field=Sym("MV.Scale.X");x.raw=Known(C::ScalarValue{2.5});x.effective=x.raw;fields.Push(x);
    context.fields=NrContextTests::PublishList(store,fields);
    auto first=std::make_unique<P::BindingPlan>();
    r.Check("T25.scalar-build",P::BuildFrameControls(context,evidence,store,*first),true,"BuildFrameControls");
    const auto binding=[&](const P::BindingPlan& b,std::string_view key){return P::FindBinding(b,key);};
    const auto number=[&](const P::BindingPlan& b,std::string_view key){auto v=binding(b,key);return v&&v->effective.IsKnown()?std::get_if<double>(&v->effective.KnownPart()->value):nullptr;};
    r.Check("T25.mv-axis-independent",number(*first,"DLSSNR.MVecScaleX")&&*number(*first,"DLSSNR.MVecScaleX")==2.5&&number(*first,"DLSSNR.MVecScaleY")&&*number(*first,"DLSSNR.MVecScaleY")==1,true,"BuildFrameControls");
    r.Check("T25.raw-mv-unknown-preserved",binding(*first,"DLSSNR.MVecScaleY")&&!binding(*first,"DLSSNR.MVecScaleY")->raw.IsKnown(),true,"BuildFrameControls");
    r.Check("T25.raw-jitter-unknown-preserved",binding(*first,"Jitter.Offset.X")&&!binding(*first,"Jitter.Offset.X")->raw.IsKnown()&&number(*first,"Jitter.Offset.X")&&*number(*first,"Jitter.Offset.X")==0,true,"BuildFrameControls");
    fields={};context.fields={};auto next=std::make_unique<P::BindingPlan>();
    r.Check("T25.previous-call-independent",P::BuildFrameControls(context,evidence,store,*next)&&number(*next,"DLSSNR.MVecScaleX")&&*number(*next,"DLSSNR.MVecScaleX")==1,true,"BuildFrameControls");
    C::SemanticClaim exposure;exposure.field=Sym("DLSS.Pre.Exposure");exposure.raw=Known(C::ScalarValue{0.0});exposure.effective=exposure.raw;fields.Push(exposure);
    context.fields=NrContextTests::PublishList(store,fields);auto pre=std::make_unique<P::BindingPlan>();
    r.Check("T25.nonpositive-preexposure-default",P::BuildFrameControls(context,evidence,store,*pre)&&number(*pre,"DLSS.Pre.Exposure")&&*number(*pre,"DLSS.Pre.Exposure")==1&&binding(*pre,"DLSS.Pre.Exposure")->raw==exposure.raw,true,"BuildFrameControls");
    fields={};x.effective=Known(C::ScalarValue{3.0});x.overrideSource=Known(C::OwnerValueReference{C::OwnerDomain::Configuration,Sym("override"),{1,0},Key("override"),1});fields.Push(x);
    context.fields=NrContextTests::PublishList(store,fields);auto overridden=std::make_unique<P::BindingPlan>();
    r.Check("T25.raw-effective-separated",P::BuildFrameControls(context,evidence,store,*overridden)&&number(*overridden,"DLSSNR.MVecScaleX")&&*number(*overridden,"DLSSNR.MVecScaleX")==3&&binding(*overridden,"DLSSNR.MVecScaleX")->raw==x.raw,true,"BuildFrameControls");
    fields.Push(x);context.fields=NrContextTests::PublishList(store,fields);P::BindingPlan duplicate;
    r.Check("T25.duplicate-control-refused",!P::BuildFrameControls(context,evidence,store,duplicate),true,"BuildFrameControls");
    P::BindingPlan extras;
    r.Check("T25.explicit-clears",P::ClearOptionalResources(evidence,extras)&&binding(extras,"DLSSNR.UI")&&binding(extras,"DLSSNR.UI")->mode==Sym("ExplicitNull")&&binding(extras,"DLSSNR.UIAlpha")&&binding(extras,"DLSSNR.Backbuffer"),true,"ClearOptionalResources");
    r.Check("T25.zero-ui-rect",binding(extras,"DLSSNR.UISubrectWidth")&&std::get<std::uint64_t>(binding(extras,"DLSSNR.UISubrectWidth")->effective.KnownPart()->value)==0,true,"ClearOptionalResources");
#else
    r.Check("T25.bindings-production",false,true,"BuildFrameControls");
#endif
    return r.Finish();});
}
