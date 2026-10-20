#include "native_fixture.h"
#if __has_include(<nr/protocol/NativeTemporalBindingMapper.h>)
#include <nr/protocol/NativeTemporalBindingMapper.h>
#define NR_NATIVE_MAPPER 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-mapper",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_MAPPER
    namespace P=Neurotic::Protocol;
    auto f=std::make_unique<NativeTests::Fixture>();
    const auto map=[&]{return P::BuildNativeBindingMap(*f->product,f->store);};
    auto first=map();
    r.Check("T30.native-current-map",first&&first->resources.Size()==4,true,"BuildNativeBindingMap");
    r.Check("T30.native-current-exposure-null",first&&first->exposureExplicitNull,true,"BuildNativeBindingMap");
    r.Check("T30.native-create-evaluate-separate",first&&first->create.Size()!=first->evaluate.Size(),true,"BuildNativeBindingMap");
    auto saved=*f->product;f->product->bindingPlan.evaluation.value++;
    r.Check("T30.native-wrong-evaluation",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    f->product->recipe.placement=C::Placement::UnifiedPresent;
    r.Check("T30.native-no-present-route",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    f->product->inputRepresentations.Get(0)->currentContent=Known(C::ContentRevision{2});
    r.Check("T30.native-stale-content",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    f->product->inputRepresentations.Get(0)->plan.revision++;
    r.Check("T30.native-vanished-plan",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    f->product->bindingPlan.bindings.Push(*f->product->bindingPlan.bindings.Get(0));
    r.Check("T30.native-duplicate-binding",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    for(auto& b:f->product->bindingPlan.bindings)if(b.key==Sym("DLSSNR.UI")){b.mode=Sym("Representation");b.representation=saved.bindingPlan.bindings.Get(0)->representation;}
    r.Check("T30.native-ui-not-inferred",!map(),true,"BuildNativeBindingMap");*f->product=saved;
    f->product->bindingPlan.bindings.Get(0)->effective=Known(C::ScalarValue{Sym("not-a-number")});
    r.Check("T30.native-scalar-type-refused",!map(),true,"BuildNativeBindingMap");
    auto& use=*f->request.representations.Get(1);
    auto deferredPlan=*Neurotic::Context::ResolveMetadata(use.plan,f->store);
    deferredPlan.output.format=Known(Sym("R8G8B8A8_UNORM"));
    use.plan=f->store.Publish(deferredPlan);f->Rebuild();
    const auto deferred=map();
    r.Check("T30.native-output-plan-not-prepared",deferred.has_value(),true,"BuildNativeBindingMap");
#else
    r.Check("T30.native-mapper-production",false,true,"BuildNativeBindingMap");
#endif
    return r.Finish();});
}
