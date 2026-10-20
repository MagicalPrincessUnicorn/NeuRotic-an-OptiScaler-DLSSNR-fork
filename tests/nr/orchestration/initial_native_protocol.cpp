#include "InitialInvocationFixture.h"
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
using namespace NrTest002;
using namespace InitialNativeTests;
struct NoRawRights:P::NativeOperationOwner
{
    P::NativeLeaseProof Refresh(P::NativeCheckPoint,const P::NativeResourceUse&)noexcept override{return {};}
};
struct RefusingEntryOwner:NoRawRights
{
    unsigned entries=0;
    bool ProviderEntry()noexcept override{++entries;return false;}
};
int main(int argc,char** argv)
{
 return Guard([&]{Report r("initial-native-protocol",argc==2?std::string_view(argv[1]):std::string_view{});
    {
        Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get());auto a=Access::Admit(f.root,owner);
        r.Precondition("admission",a&&owner->EstablishSyntheticLease(),true,"actual C03 BeginConsumption, synthetic exterior owner");
        if(!a||!owner->lease)return r.Finish();
        unsigned calls=0;bool held=false;NoRawRights raw;
        auto executor=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& observer)
        {
            auto uses=P::NativeResources({{Sym("ColorModelInput"),reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
            if(!observer.Check(P::NativeCheckPoint::BeforeModel,uses)||!observer.StartAuthorizedModel(true))return observer.Facts();
            ++calls;held=owner->guards==1;f.coordinator.Revoke(O::RevocationCause::Disabled);observer.ModelResult(true);
            if(!observer.Check(P::NativeCheckPoint::BeforeComposition,uses))return observer.Facts();
            observer.Composed();return observer.Facts();
        };
        auto header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,800);
        auto result=P::ExecuteNativeProtocol(*a,header,raw,*owner->store,executor);
        r.Check("producer_to_protocol_model",calls==1&&held&&result.claim&&result.facts.modelAttempted,true,"Actual producer/admission/primary/observer; synthetic model seam");
        r.Check("revoked_actual_not_notattempted",result.evaluation&&result.evaluation->stage!=C::OutcomeStage::NotAttempted&&
            result.evaluation->stage!=C::OutcomeStage::Bypassed&&!result.legacyAllowed,true,"INIT-CPU-26 actual attempt retained");
        r.Check("reset_not_acknowledged",result.evaluation&&result.facts.actualResetRequested==std::optional<bool>{true}&&
            result.evaluation->resetAcknowledgments.count==0,true,"Opaque reset argument is not owner-applied reset acknowledgment");
        auto repeat=P::ExecuteNativeProtocol(*a,header,raw,*owner->store,executor);
        r.Check("no_second_executor",calls==1&&!repeat.claim&&!repeat.legacyAllowed,true,"INIT-CPU-23 new primary required");
    }
    {
        Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),14);
        auto admission=Access::Admit(f.root,owner);
        r.Precondition("entry_refusal_admission",admission&&owner->EstablishSyntheticLease(),true,
            "Exact coordinator admission and controlled C03 lease");
        if(admission&&owner->lease&&admission->AcquirePrimary())
        {
            RefusingEntryOwner entryOwner;
            P::NativeExecutionObserver observer(*admission,entryOwner);
            const auto uses=P::NativeResources({{Sym("ColorModelInput"),
                reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
            const bool checked=observer.Check(P::NativeCheckPoint::BeforeModel,uses);
            const bool entered=checked&&observer.ProviderEntry(false);
            r.Check("refused_provider_activation_is_not_model_call",checked&&!entered&&
                entryOwner.entries==1&&!observer.Facts().modelAttempted,true,
                "A refused activation cannot report an opaque call");
        }
    }
    {
        Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get());auto a=Access::Admit(f.root,owner);
        owner->store->resolveHook=[&]{f.coordinator.Revoke(O::RevocationCause::Disabled);};
        const bool permitted=a&&a->AcquirePrimary();owner->store->resolveHook={};
        r.Check("revoke_before_claim_result",a&&!permitted&&a->Primary()&&!Access::Action(*a,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-24 revoke from actual finalizer reader");
        r.Check("unknown_claim_retained",!f.root->AdvanceClosure().finalizerStopped,true,"Claim exists after revocation; not relabeled absent");
    }
    {
        Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get());
        owner->changePreparation=[](auto& s){s.product.dependencies.structuralPlans.Get(0)->value+=100;};
        r.Check("resource_change_classification",!Access::Admit(f.root,owner),true,"INIT-CPU-22 changed stable C12 plan refused");
    }
    {
        Fixture f;f.Activate();auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),11);
        owner->changePreparation=[](auto& s){s.product.recipe.resetProjection=Known(C::ContractRef<C::ContractId::C08>{Sym("ResetImpactPlan"),Key("history-reset"),1});};
        auto a=Access::Admit(f.root,owner);
        r.Check("history_reset_not_route",a&&f.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-21 reset evidence alone does not issue route");
    }
    {
        Fixture native;native.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;native.Activate();
        auto owner=std::make_shared<SyntheticInvocation>(native.root.get(),12);
        auto nativeAdmission=Access::Admit(native.root,owner);
        r.Precondition("hostreturn_protocol_admission",nativeAdmission&&owner->EstablishSyntheticLease(),true,
            "AM05 HostReturn owner and exact synthetic C03 lease");
        if(nativeAdmission&&owner->lease)
        {
            unsigned modelCalls=0;NoRawRights raw;
            auto executor=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& observer)
            {
                const auto uses=P::NativeResources({{Sym("ColorModelInput"),
                    reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
                if(!observer.Check(P::NativeCheckPoint::BeforeModel,uses)||!observer.StartAuthorizedModel())
                    return observer.Facts();
                ++modelCalls;observer.ModelResult(true);observer.Composed();return observer.Facts();
            };
            const auto header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,810);
            const auto result=P::ExecuteNativeProtocol(*nativeAdmission,header,raw,*owner->store,executor);
            r.Check("hostreturn_protocol_no_c13_submission",modelCalls==1&&!result.claim&&
                !result.submittedToFinalizer&&result.reason.View()=="Native.HostReturnPending",true,
                "AM05 a local HostReturn result awaits actual caller-output proof, not C13");
        }
    }
    {
        Fixture native;native.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;native.Activate();
        auto owner=std::make_shared<SyntheticInvocation>(native.root.get(),13);
        auto admission=Access::Admit(native.root,owner);
        r.Precondition("local_undelivered_admission",admission&&owner->EstablishSyntheticLease(),true,
            "Synthetic C03 lease exercises the SP03 local closure policy");
        if(admission&&owner->lease)
        {
            L::NativeHistoryState history(Key("history.owner"),Key("history"));
            unsigned calls=0;NoRawRights raw;
            auto executor=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& observer)
            {
                const auto uses=P::NativeResources({{Sym("ColorModelInput"),
                    reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});
                if(!observer.Check(P::NativeCheckPoint::BeforeModel,uses)||!observer.StartAuthorizedModel())
                    return observer.Facts();
                ++calls;observer.ModelResult(true);observer.Composed();return observer.Facts();
            };
            const auto header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,811);
            const auto result=P::ExecuteNativeProtocol(*admission,header,raw,*owner->store,executor,&history,true);
            r.Check("local_undelivered_retains_reset",calls==1&&result.reason.View()=="Native.LocalUndelivered"&&
                !result.submittedToFinalizer&&history.RequiresReset()&&history.Pending(),true,
                "A successful local model call does not mint HostReturn or consume history");
        }
    }
    r.Check("default_off_unchanged",P::SelectNativePath(false,0,true,true)==P::NativePath::Legacy,true,"INIT-CPU-40");
    return r.Finish();});
}
