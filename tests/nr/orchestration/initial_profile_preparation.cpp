#include "../vertical-slice/native_fixture.h"
#if __has_include(<nr/protocol/NativeProfilePreparation.h>)
#include <nr/protocol/NativeProfilePreparation.h>
#define NR_INIT_PREPARATION 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("initial-profile-preparation",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_INIT_PREPARATION
        namespace P=Neurotic::Protocol;
        auto f=std::make_unique<NativeTests::Fixture>(false);
        f->request.externalPlan={};f->request.recipeHeader={};
        const P::NativeProfilePreparationRequest request=f->request;
        const auto prepared=P::PrepareNativeProfile(request,f->store);
        r.Check("preparation_without_fake_commit",prepared.projection.has_value(),true,"INIT-CPU-41: no C05/C06 required");
        if(prepared.projection)
        {
            const auto& p=prepared.projection->Value();
            r.Check("profile_owner_preserved",p.profileCertificate.header.owner==C::OwnerDomain::Strategy,true,"Existing profile C04 owner");
            r.Check("complete_prepared_bindings",p.bindingPlan.bindings.Size()>20,true,"Late binding path executed");
        }
        r.Check("executable_guard_preserved",!P::BuildRecipe(f->request,f->store).product,true,"Executable still requires its C06 and plan");
        f->request.recipeHeader=NrContextTests::Header(C::ContractId::C06,C::OwnerDomain::RenderingProtocol,70);
        f->request.externalPlan={Sym(C::PlanCommit::WireName),Key("actual.commit.fixture"),1};
        const auto executable=P::BuildRecipe(f->request,f->store);
        r.Check("postcommit_build",executable.product.has_value(),true,"Synthetic C05 ref only in postcommit fixture");
        if(prepared.projection&&executable.product)
        {
            const auto& p=prepared.projection->Value();const auto& e=executable.product->Value();
            r.Check("preparation_binding_equivalence",p.bindingPlan.bindings.Size()==e.bindingPlan.bindings.Size()&&
                p.expectedOutput==e.recipe.expectedOutput&&p.dependencies==e.dependencies,true,"Same shared complete preparation");
        }
        auto invalid=request;invalid.representations.Get(0)->consumerContract=Sym("unsupported");
        r.Check("preparation_semantic_refusal",!P::PrepareNativeProfile(invalid,f->store).projection,true,"Required representation semantics unchanged");
        invalid=request;
        auto settings=*Neurotic::Context::ResolveMetadata(invalid.settings,f->store);
        settings.controls.Get(0)->effective={};invalid.settings=f->store.Publish(settings,C::OwnerDomain::Configuration);
        const auto late=P::PrepareNativeProfile(invalid,f->store);
        r.Check("preparation_late_binding_refusal",!late.projection&&late.reason.View()=="RENDER.Profile.ControlBindingInvalid",true,"Early Eligible cannot bypass late checks");
#else
        r.Check("preparation_without_fake_commit",false,true,"INIT-CPU-41: production nonexecuting profile preparation absent");
#endif
        return r.Finish();});
}
