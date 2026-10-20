#include "../OptiScaler/nr/diagnostics/capability/ObservationRefresh.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityStore.h"
#include "../OptiScaler/nr/diagnostics/candidate/Observer.h"
#include <windows.h>
#include <atomic>
#include <cassert>
#include <iostream>
using namespace DlssNr::Capability;
namespace CandidateObserver=DlssNr::CandidateObserver;
std::atomic<unsigned> collections=0;
std::atomic<bool> blockCollection=false,blocked=false,changed=false;
std::atomic<bool> usableOwner=false;
std::atomic<uint64_t> completedUses=0;
std::atomic<bool> blockRead=false,readBlocked=false,nativeOwner=false,scalarsValid=true,invalidateAtCollection=false;
std::atomic<unsigned> invalidateOnCall=0;
namespace DlssNr::Capability::ObservationRefresh {
BackendSample ReadTestBackend(bool) {
    if(blockRead.load() && CandidateObserver::ManualBusy()) {
        readBlocked.store(true);while(blockRead.load())Sleep(1);
    }
    BackendSample sample;sample.state.requestedEnabled=changed.load();
    if(usableOwner.load()) {
        sample.state.requestedEnabled=true;sample.state.requestedRoute=1;
        sample.evidence.requestedEnabled=true;sample.evidence.ownerRunning=true;
        sample.evidence.presentGuided=true;sample.evidence.resources=1;
        sample.evidence.completedAfter=++completedUses;
        using S=DlssNr::SignalReceipts::Signal;using R=DlssNr::SignalReceipts::State;
        auto complete=[&](S signal){auto& r=sample.evidence.signals[size_t(signal)];r.state=R::Consumed;r.sequence=completedUses.load();r.fact.present=true;r.fact.admitted=true;};
        if(!nativeOwner.load()){complete(S::Depth);complete(S::Motion);}
        if(nativeOwner.load()) {
            if(scalarsValid.load()){complete(S::Jitter);complete(S::PreExposure);}
            sample.state.requestedRoute=0;sample.evidence.nativeRoute=true;
            sample.evidence.lifecycleOpen=true;sample.evidence.lifecycle=1;
            sample.evidence.parametersAfter=completedUses.load();
            sample.evidence.finiteJitter=sample.evidence.finitePreExposure=scalarsValid.load();
        }
    }
    return sample;
}
CollectionResult CollectTestBackend(bool) {
    const auto call=++collections;
    if(invalidateAtCollection.load() && call>=invalidateOnCall.load())scalarsValid.store(false);
    if(blockCollection.load()) {blocked.store(true);while(blockCollection.load())Sleep(1);}
    static Store store("actual-worker-test");CollectionResult result;result.capture=store.Capture({});return result;
}
}
int main() {
    namespace Refresh=ObservationRefresh;
    CandidateObserver::StartCold(false);
    RefreshState state;Refresh::Update(state,false,false,false);
    for(unsigned i=0;i<10000 && Refresh::Busy();i++)Sleep(1);
    auto old=Refresh::Completed();assert(old && !old->utf8.empty());
    Sleep(550);blockCollection.store(true);
    Refresh::Update(state,false,true,false);assert(Refresh::Busy());
    for(unsigned i=0;i<10000 && !blocked.load();i++)Sleep(1);assert(blocked.load());
    const auto calls=collections.load();const auto start=GetTickCount64();
    for(unsigned i=0;i<10000;i++) {
        assert(Refresh::Completed()==old);
        Refresh::Update(state,false,true,true);
    }
    assert(collections.load()==calls && GetTickCount64()-start<1000);
    blockCollection.store(false);
    for(unsigned i=0;i<10000 && Refresh::Busy();i++)Sleep(1);
    auto next=Refresh::Completed();assert(next && next->operation>old->operation && next->manual && next->hasDiscovery);
    assert(next->completed-next->started>=2000);
    Sleep(550);Refresh::Update(state,false,true,true);assert(!Refresh::Busy()); // held activation
    Refresh::Update(state,false,true,false);assert(Refresh::Busy());
    changed.store(true);
    for(unsigned i=0;i<10000 && Refresh::Busy();i++)Sleep(1);
    assert(Refresh::Completed()==next && !Refresh::CurrentOwner().Any());
    old.reset();next.reset(); // release completed capture leases before subsequent captures
    Sleep(550);usableOwner.store(true);state.requestedEnabled=true;state.requestedRoute=1;
    Refresh::Update(state,false,true,false);
    for(unsigned i=0;i<10000 && Refresh::Busy();i++)Sleep(1);
    assert(Refresh::CurrentOwner().depth && Refresh::CurrentOwner().motion);
    Sleep(550);blockRead.store(true);readBlocked.store(false);
    Refresh::Update(state,false,true,false);
    while(!readBlocked.load())Sleep(1);
    Sleep(2100);
    {CandidateObserver::CreationScope scope;CandidateObserver::Event event{};
     event.kind=CandidateObserver::Kind::Committed;event.source=1;
     assert(scope.Emit(event)!=CandidateObserver::EmitResult::Recorded);}
    assert(Refresh::Busy());blockRead.store(false);
    while(Refresh::Busy())Sleep(1);
    Sleep(550);nativeOwner.store(true);state.requestedRoute=0;
    Refresh::Update(state,false,true,false);assert(Refresh::Busy());while(Refresh::Busy())Sleep(1);
    auto historical=Refresh::Completed();
    assert(historical->owner.jitter && historical->owner.preExposure);
    assert(Refresh::CurrentOwner(true,true).jitter);
    assert(!Refresh::CurrentOwner(false,true).jitter && Refresh::CurrentOwner(false,true).preExposure);
    assert(!Refresh::CurrentOwner(true,false).preExposure);
    assert(!Refresh::CurrentOwner(false,false).Any()); // missing, partial and non-finite latest scalars
    assert(Refresh::Completed()==historical); // historical Copy is unchanged even without UI updates
    Sleep(550);invalidateAtCollection.store(true);invalidateOnCall.store(collections.load()+2);
    Refresh::Update(state,false,true,false);assert(Refresh::Busy());while(Refresh::Busy())Sleep(1);
    assert(!Refresh::Completed()->owner.Any()); // changed during final collection
    Refresh::RequestClose();CandidateObserver::RequestClose(CandidateObserver::Reason::SourceDetach);
    std::cout<<"actual worker blocked-Copy, click spam, timing, rearm and cancellation checks passed\n";
}
