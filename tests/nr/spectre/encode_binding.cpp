#include "../vertical-slice/native_fixture.h"
#include <nr/protocol/NativeTemporalBindingMapper.h>
#include <iostream>
#include <limits>
using namespace NrTest002;
namespace P=Neurotic::Protocol;
namespace S=Neurotic::Context;
using Params=C::BoundedList<C::SemanticClaim,8>;
int main()
{
    int count=0,failed=0;
    const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<NativeTests::Fixture>();
    auto original=*f->request.representations.Get(0);
    auto plan=*S::ResolveMetadata(original.plan,f->store);
    const auto* mapping=S::ResolveMetadata(plan.raster,f->store);
    const auto rect=mapping->target.active.KnownPart()->value;
    Params parameters;
    const auto add=[&](const char* key,C::ScalarValue value){C::SemanticClaim p;p.field=Sym(key);p.effective=Known(value);parameters.Push(p);};
    add("Native.Encode.Passthrough",std::uint64_t{0});add("Native.Encode.WhitePoint",203.0);
    add("Native.Encode.UseGameExposure",std::uint64_t{0});add("Native.Encode.ExposurePreMul",0.0);
    add("Native.Encode.ReversibleMode",std::uint64_t{3});
    add("Native.Encode.Width",std::uint64_t{rect.width});add("Native.Encode.Height",std::uint64_t{rect.height});
    C::TransformStep step;step.kind=C::TransformKind::ColorTransfer;step.operation=Sym("LegacyNative.Encode.v1");step.version=1;
    step.sourceUnits=Known(Sym("QualifiedColor"));step.targetUnits=Known(Sym("LegacyNative.ModelEncoded.v1"));
    step.qualification=Known(f->meaning);step.alreadyApplied=Known(false);
    const auto make=[&](const Params& params,const C::TransformStep& value){
        auto current=value;current.parameters=NrContextTests::PublishList(f->store,params);
        S::TransformLineage lineage;lineage.Push(current);auto use=original;auto p=plan;
        use.required=NrContextTests::PublishList(f->store,lineage);use.consumerDeferred=use.required;p.transforms=use.required;
        use.plan=f->store.Publish(p);*f->request.representations.Get(0)=use;
    };
    const auto build=[&]{return P::PrepareNativeProfile(f->request,f->store).projection.has_value();};
    make(parameters,step);check(build(),"exact deferred color encoder prepares profile");
    if(build())
    {
        f->Rebuild();const auto map=P::BuildNativeBindingMap(*f->product,f->store);
        check(map.has_value(),"exact deferred encoder maps current recipe");
        const auto* binding=P::FindBinding(f->product->bindingPlan,"Native.Encode.WhitePoint");
        check(binding&&binding->stage==Sym("Frame")&&P::Number(binding->effective)&&*P::Number(binding->effective)==203.0,
              "exact encoder stays internal to Frame bindings");
        auto* width=f->product->bindingPlan.bindings.Get(f->product->bindingPlan.bindings.Size()-1);
        for(auto& b:f->product->bindingPlan.bindings)if(b.key==Sym("Native.Encode.Width"))width=&b;
        width->effective=Known(C::ScalarValue{std::uint64_t{rect.width+8}});
        check(!P::BuildNativeBindingMap(*f->product,f->store),"mapper rejects substituted encoder dimension");
    }
    for(std::size_t i=0;i<7;++i)
    {
        Params missing;for(std::size_t j=0;j<7;++j)if(i!=j)missing.Push(*parameters.Get(j));
        make(missing,step);check(!build(),"missing encode constant refused");
    }
    auto invalid=parameters;*invalid.Get(6)=*invalid.Get(5);make(invalid,step);check(!build(),"duplicate encode constant refused");
    invalid=parameters;invalid.Get(0)->field=Sym("Unknown");make(invalid,step);check(!build(),"unknown encode constant refused");
    invalid=parameters;invalid.Get(1)->effective=Known(C::ScalarValue{std::numeric_limits<double>::infinity()});make(invalid,step);check(!build(),"nonfinite white point refused");
    invalid=parameters;invalid.Get(1)->effective=Known(C::ScalarValue{0.0});make(invalid,step);check(!build(),"zero white point refused");
    invalid=parameters;invalid.Get(0)->effective=Known(C::ScalarValue{std::uint64_t{2}});make(invalid,step);check(!build(),"invalid passthrough refused");
    invalid=parameters;invalid.Get(2)->effective=Known(C::ScalarValue{std::uint64_t{1}});make(invalid,step);check(!build(),"exposure texture unsupported by initial slice");
    invalid=parameters;invalid.Get(3)->effective=Known(C::ScalarValue{1.0});make(invalid,step);check(!build(),"inactive exposure multiplier must match actual zero");
    invalid=parameters;invalid.Get(4)->effective=Known(C::ScalarValue{std::uint64_t{5}});make(invalid,step);check(!build(),"unknown reversible mode refused");
    invalid=parameters;invalid.Get(5)->effective=Known(C::ScalarValue{double(rect.width)});make(invalid,step);check(!build(),"dimension scalar type exact");
    invalid=parameters;invalid.Get(5)->effective=Known(C::ScalarValue{std::uint64_t{rect.width+8}});make(invalid,step);check(!build(),"encode dimension must match C12 raster");
    auto bad=step;bad.version=2;make(parameters,bad);check(!build(),"unknown ABI version refused");
    bad=step;bad.kind=C::TransformKind::ExposureUndo;make(parameters,bad);check(!build(),"wrong transform kind refused");
    bad=step;bad.operation=Sym("LegacyNative.Binding.v1");make(parameters,bad);check(!build(),"wrong operation refused");
    make(parameters,step);f->request.representations.Get(0)->purpose=P::Purpose::DepthGuide;check(!build(),"wrong purpose refused");
    make(parameters,step);auto applied=step;applied.parameters=NrContextTests::PublishList(f->store,parameters);applied.alreadyApplied=Known(true);
    S::TransformLineage done;done.Push(applied);f->request.representations.Get(0)->applied=NrContextTests::PublishList(f->store,done);
    check(!build(),"already applied encode cannot be deferred again");
    make(parameters,step);f->Rebuild();
    auto bindings=std::make_unique<P::BindingPlan>(f->product->bindingPlan);
    const auto parsed=P::ReadNativeEncodeBindings(*bindings);
    check(parsed&&parsed->whitePoint==203.0f&&parsed->width==rect.width&&parsed->height==rect.height,
          "runtime decode retains exact approved constants");
    for(auto& b:bindings->bindings)if(b.key==Sym("Native.Encode.WhitePoint"))b.stage=Sym("Evaluate");
    check(!P::ReadNativeEncodeBindings(*bindings),"runtime rejects vendor-stage encode bindings");
    *bindings=f->product->bindingPlan;
    for(auto& b:bindings->bindings)if(b.key==Sym("Native.Encode.WhitePoint"))b.rule=Sym("Unknown");
    check(!P::ReadNativeEncodeBindings(*bindings),"runtime rejects wrong encode operation");
    *bindings=f->product->bindingPlan;
    bindings->bindings.Push(*P::FindBinding(*bindings,"Native.Encode.Width"));
    check(!P::ReadNativeEncodeBindings(*bindings),"runtime rejects duplicated internal binding");
    f->product->inputRepresentations.Get(0)->consumerDeferred={};
    check(!P::BuildNativeBindingMap(*f->product,f->store),"bindings cannot substitute for declared C12 transform");
    bad=step;bad.sourceUnits=Known(Sym("Unknown"));make(parameters,bad);check(!build(),"wrong encoder source units refused");
    bad=step;bad.targetUnits=Known(Sym("Unknown"));make(parameters,bad);check(!build(),"wrong encoder target units refused");
    invalid=parameters;invalid.Get(1)->effective=Known(C::ScalarValue{0.1});make(invalid,step);check(!build(),"unrepresentable encoder constant refused");
    std::cout<<"encode-binding checks="<<count<<" failed="<<failed<<'\n';return failed?1:0;
}
