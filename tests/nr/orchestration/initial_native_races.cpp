#include "InitialInvocationFixture.h"
#include <barrier>
#include <condition_variable>
#include <thread>
#include <vector>
using namespace NrTest002;
using namespace InitialNativeTests;
namespace
{
struct RaceOutcome { bool prepared=false,started=false,called=false,retained=false,secondRefused=false,continued=false; };
// The exterior remains synthetic. Both contenders call the actual coordinator
// and ledger, with the owner guard acquired and destroyed on the starter thread.
RaceOutcome RevokeStartRace(int orderedControl=0)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    auto owner=std::make_shared<SyntheticInvocation>(f.root.get());
    std::weak_ptr<SyntheticInvocation> retained=owner;
    auto admission=Access::Admit(f.root,owner);
    RaceOutcome result;result.prepared=admission&&admission->AcquirePrimary();
    if(!result.prepared)return result;
    owner.reset(); // Only the actual admitted operation now owns this facade.
    std::barrier gate(2);
    std::thread starter([&]{
        auto guard=Access::Action(*admission,P::NativeCheckPoint::BeforeModel);
        if(orderedControl>0&&guard)result.started=Access::Start(*admission,*guard);
        gate.arrive_and_wait();
        if(orderedControl<0)gate.arrive_and_wait();
        if(orderedControl<=0&&guard)result.started=Access::Start(*admission,*guard);
        if(orderedControl>0)gate.arrive_and_wait();
        result.called=Access::Called(*admission);
        if(auto current=retained.lock())
            result.retained=guard&&current->guards==1&&current->snapshot&&
                P::SameNativeInvocation(*current->snapshot,guard->Current());
        result.secondRefused=!Access::Called(*admission)&&(!guard||!Access::Start(*admission,*guard));
        if(result.called)Access::Returned(*admission);
        guard.reset();
        auto continuation=Access::Action(*admission,P::NativeCheckPoint::BeforeComposition);
        result.continued=continuation!=nullptr;
    });
    std::thread revoker([&]{
        gate.arrive_and_wait();f.coordinator.Revoke(O::RevocationCause::Disabled);
        if(orderedControl)gate.arrive_and_wait();
    });
    starter.join();revoker.join();return result;
}
bool Linearized(const RaceOutcome& result)
{
    return result.prepared&&result.started==result.called&&result.secondRefused&&
        result.continued==result.started&&(!result.started||result.retained);
}
}
int main(int argc,char** argv)
{
 return Guard([&]{Report r("initial-native-races",argc==2?std::string_view(argv[1]):std::string_view{});
    {
        Fixture f;std::mutex mutex;std::condition_variable cv;bool preparing=false,release=false;
        f.owner->beforePrepare=[&]{std::unique_lock lock(mutex);preparing=true;cv.notify_one();cv.wait(lock,[&]{return release;});};
        O::InitResult activated;
        std::thread worker([&]{activated=f.Activate();});
        {std::unique_lock lock(mutex);cv.wait(lock,[&]{return preparing;});}
        const auto during=f.coordinator.Inspect();const auto concurrent=f.Activate();
        r.Check("atomic_visibility",during.phase==O::RoutePhase::Preparing&&!f.coordinator.TryReadCurrent().current&&
            concurrent.status==O::InitStatus::Busy,true,"INIT-CPU-07 synchronized Prepare barrier");
        f.coordinator.Revoke(O::RevocationCause::SessionClose);
        {std::lock_guard lock(mutex);release=true;}cv.notify_one();worker.join();
        r.Check("preissue_revoke_no_issue",!activated.current&&f.coordinator.Inspect().successfulIssues==0,true,"Mandatory revoke wins preparation race");
    }
    {
        Fixture f;bool observed=false;
        f.owner->beforePrepare=[&]{observed=f.coordinator.Inspect().phase==O::RoutePhase::Preparing;};
        r.Check("lock_order_no_callback",f.Activate().current&&observed,true,"INIT-CPU-39 callback reenters read lock");
    }
    {
        std::optional<O::CurrentRouteView> view;std::weak_ptr<L::NativeSessionLifetime> root;std::weak_ptr<NrContextTests::Store> metadata;
        {Fixture f;view=f.Activate().current;root=f.root;metadata=f.owner->metadataLifetime;}
        r.Check("host_lifetime",view&&!root.expired(),true,"INIT-CPU-30 view retains root after facade destruction");
        r.Check("metadata_lifetime",view&&!metadata.expired(),true,"INIT-CPU-29 original metadata owner retained");
        view.reset();r.Check("no_root_ledger_cycle",root.expired()&&metadata.expired(),true,"No GPU owners in this synthetic scope");
    }
    {
        Fixture f;f.Activate();std::vector<P::NativeInvocationAdmission> handles;
        for(unsigned i=0;i<16;++i)
        {
            auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),10+i);auto a=Access::Admit(f.root,owner);
            if(a)handles.push_back(std::move(*a));
        }
        auto extra=std::make_shared<SyntheticInvocation>(f.root.get(),99);
        r.Check("capacity_fail_closed",handles.size()==16&&!Access::Admit(f.root,extra)&&extra->preparations==0,true,"INIT-CPU-37 no eviction/callback on full");
        handles.clear();unsigned reused=0;
        for(unsigned i=0;i<256;++i)
        {
            auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),100+i);auto a=Access::Admit(f.root,owner);
            if(a)++reused;
        }
        r.Check("sustained_bounded_reuse",reused==256&&f.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-36 exact synthetic unclaimed retirement");
    }
    {
        Fixture f;f.Activate();auto first=std::make_shared<SyntheticInvocation>(f.root.get());
        auto second=std::make_shared<SyntheticInvocation>(f.root.get());
        auto a=Access::Admit(f.root,first);auto b=Access::Admit(f.root,second);
        const bool primary=a&&a->AcquirePrimary();
        r.Check("existing_primary_not_fresh",primary&&!b,true,"AM05 Native sample already owned before a second finalizer claim");
        auto g=primary?Access::Action(*a,P::NativeCheckPoint::BeforeModel):nullptr;
        std::mutex mutex;std::condition_variable cv;bool revoked=false;
        std::thread revoker([&]{f.coordinator.Revoke(O::RevocationCause::Disabled);{std::lock_guard lock(mutex);revoked=true;}cv.notify_one();});
        {std::unique_lock lock(mutex);cv.wait(lock,[&]{return revoked;});}revoker.join();
        r.Check("threaded_revoke_before_start",g&&!Access::Start(*a,*g)&&!Access::Called(*a),true,"INIT-CPU-25 synchronized actual slot order");
    }
    {
        const auto startFirst=RevokeStartRace(1),revokeFirst=RevokeStartRace(-1);
        r.Check("winning_start_retains_exact_owner",Linearized(startFirst)&&startFirst.started,true,
            "N08 forced start-before-revoke preserves actual owner guard until one seam mark and continuation");
        r.Check("revoke_wins_no_new_call",Linearized(revokeFirst)&&!revokeFirst.started&&!revokeFirst.continued,true,
            "N08 forced revoke-before-start refuses the actual call gate");
        unsigned completed=0,invalid=0;
        for(unsigned i=0;i<64;++i){const auto value=RevokeStartRace();++completed;if(!Linearized(value))++invalid;}
        r.Check("simultaneous_revoke_start_linearized",completed==64&&invalid==0,true,
            "N08 common-barrier contenders: no call after losing start; winning work keeps its exact owner");
    }
    {
        Fixture a,b;auto one=a.Activate(),two=b.Activate();
        r.Check("new_genuine_scope",one.current&&two.current&&one.current->Scope().session!=two.current->Scope().session&&
            one.current->Plan().routeGeneration.KnownPart()->value!=two.current->Plan().routeGeneration.KnownPart()->value,true,
            "INIT-CPU-38 separate synthetic process bootstrap namespaces; no same-scope reopening");
    }
    {
        Fixture f;f.owner->cancelUnknown=true;f.SetFault(O::TestFault::IssuerBusy);auto refused=f.Activate();
        r.Check("unknown_preparation_pins_retained",!refused.current&&!f.owner->metadataLifetime.expired()&&
            f.coordinator.Inspect().phase==O::RoutePhase::Quarantined,true,"Review: Unknown cancellation cannot destroy preparation pins");
        r.Check("unknown_preparation_blocks_close",!f.root->AdvanceClosure().closed,true,"Review: aggregate cannot override unknown preparation");
        f.owner->cancelUnknown=false;
        r.Check("proven_preparation_cleanup",f.root->AdvanceClosure().closed&&f.owner->metadataLifetime.expired(),true,
            "Review: destroy preparation outside slot lock only after explicit retirement");
    }
    return r.Finish();});
}
