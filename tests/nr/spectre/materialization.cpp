#include "../orchestration/InitialInvocationFixture.h"
#include <iostream>
using namespace InitialNativeTests;
int main()
{
    int count=0,failed=0;
    const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<Fixture>();const auto activated=f->Activate();
    check(activated.status==O::InitStatus::Activated&&activated.current.has_value(),
          "N01 actual coordinator activation control");
    if(!activated.current)return 1;
    check(activated.current->Plan().nativeDelivery.has_value(),
          "N02 actual committed plan retains immutable delivery");
    auto owner=std::make_shared<SyntheticInvocation>(f->root.get());
    auto admission=Access::Admit(f->root,owner);
    check(admission.has_value(),"N01 fresh actual admission control");
    check(admission&&admission->Product()&&admission->Product()->recipe.nativeDelivery&&
          admission->Product()->recipe.nativeDelivery==activated.current->Plan().nativeDelivery,
          "N02 fresh recipe retains exact committed delivery");
    auto missing=std::make_unique<Fixture>();missing->owner->missingDelivery=true;
    check(!missing->Activate().current&&missing->coordinator.Inspect().successfulIssues==0,
          "F08 missing delivery refuses before canonical issue");
    auto incoherent=std::make_unique<Fixture>();incoherent->owner->selectionFrameMismatch=true;
    check(!incoherent->Activate().current&&incoherent->coordinator.Inspect().successfulIssues==0,
          "N03 selected source must match actual prepared context sample before issue");
    auto local=std::make_unique<Fixture>();local->owner->deliveryKind=C::NativeDeliveryKind::HostReturn;
    check(local->Activate().current.has_value(),"N03 genuine local readiness needs no final handoff fact");
    auto unavailable=std::make_unique<Fixture>();unavailable->owner->deliveryKind=C::NativeDeliveryKind::HostReturn;
    unavailable->owner->returnUnavailable=true;
    check(!unavailable->Activate().current&&unavailable->coordinator.Inspect().successfulIssues==0,
          "N03 unknown local readiness never defaults to ready");
    auto wrongOwner=std::make_unique<Fixture>();wrongOwner->owner->deliveryKind=C::NativeDeliveryKind::HostReturn;
    wrongOwner->owner->returnWrongOwner=true;
    check(!wrongOwner->Activate().current&&wrongOwner->coordinator.Inspect().successfulIssues==0,
          "N03 diagnostic local readiness cannot replace actual return owner");
    auto different=std::make_shared<SyntheticInvocation>(f->root.get(),12);
    different->changePreparation=[&different](P::NativeInvocationSnapshot& s)
    {
        const auto* value=S::ResolveMetadata(*s.product.recipe.nativeDelivery,*different->store);
        if(!value)throw std::runtime_error("missing fixture contract");
        s.product.recipe.nativeDelivery=different->store->Publish(*value,C::OwnerDomain::StreamCoordinator);
    };
    check(!Access::Admit(f->root,different),"N02 equal-value foreign delivery cannot replace committed publication");
    auto otherFeature=std::make_shared<SyntheticInvocation>(f->root.get(),13);otherFeature->featureIncarnation=2;
    check(!Access::Admit(f->root,otherFeature),"N08 fresh sample from another feature cannot join retained route");
    auto otherIngress=std::make_shared<SyntheticInvocation>(f->root.get(),14);otherIngress->ingressIncarnation=2;
    check(!Access::Admit(f->root,otherIngress),"N08 fresh sample from another ingress cannot join retained route");
    f->owner->deliveryKind=C::NativeDeliveryKind::HostReturn;
    check(f->coordinator.TryActivate().status==O::InitStatus::Refused,
          "F08 incompatible repeated initialization refuses strict downgrade");
    const auto retained=f->coordinator.TryReadCurrent();
    check(retained.current&&retained.current->Plan().nativeDelivery==activated.current->Plan().nativeDelivery&&
          f->coordinator.Inspect().successfulIssues==1,
          "F08 rejected repeated initialization never mutates committed meaning");
    auto racing=std::make_unique<Fixture>();
    check(racing->Activate().current.has_value(),"N01 repeated-init revocation control");
    racing->owner->beforeMatches=[&]{racing->coordinator.Revoke(O::RevocationCause::OwnerLost);};
    check(!racing->coordinator.TryActivate().current&&racing->coordinator.Inspect().successfulIssues==1,
          "F08 owner revocation during repeated-init check cannot return current authority");
    std::cout<<"Materialization: "<<count<<" checks, "<<failed<<" failures; synthetic exterior owners, actual kernel only\n";
    return failed?1:0;
}
