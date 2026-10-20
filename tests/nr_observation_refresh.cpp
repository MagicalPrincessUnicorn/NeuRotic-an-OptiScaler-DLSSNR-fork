#include "../OptiScaler/nr/diagnostics/capability/ObservationRefreshControl.h"
#include <cassert>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <iostream>
using namespace DlssNr::Capability;
int main() {
    ObservationRefreshControl control;RefreshState state;
    control.Observe(state,false);control.Activation(false);
    auto first=control.Request(true,0);assert(first && control.Busy());
    for(unsigned i=0;i<10000;i++)assert(!control.Request(true,i%1000));
    assert(!control.Request(false,500));
    assert(!ObservationRefreshControl::CollectionDone(*first,1999));
    assert(ObservationRefreshControl::CollectionDone(*first,2000));
    std::atomic<std::shared_ptr<const std::string>> report;
    report.store(std::make_shared<const std::string>("old:whole"));
    std::atomic<bool> release{false};
    std::thread worker([&] {while(!release.load())std::this_thread::yield();
        assert(control.Finish(*first,2000,[&] {report.store(std::make_shared<const std::string>("new:whole"));}));
    });
    for(unsigned i=0;i<10000;i++)assert(*report.load()=="old:whole");
    release.store(true);worker.join();assert(*report.load()=="new:whole");
    assert(!control.Busy());
    control.Activation(true);assert(!control.Request(true,5000)); // held key cannot replay
    control.Activation(false);assert(!control.Request(true,2499)); // cooldown
    control.Activation(false);auto second=control.Request(true,2500);assert(second);
    state.requestedEnabled=true;control.Observe(state,false);
    state.requestedEnabled=false;control.Observe(state,false); // ABA still cancels
    assert(control.Cancelled(*second));
    bool published=false;assert(!control.Finish(*second,4500,[&] {published=true;}));assert(!published);
    assert(*report.load()=="new:whole");
    auto automatic=control.Request(false,4500);assert(automatic && !automatic->manual);
    assert(control.Finish(*automatic,4501,[]{}));assert(!control.Request(false,5001)); // clean auto idle
    auto visible=state;visible.menuVisibilityGeneration=4;
    control.Observe(visible,false);auto reopened=control.Request(false,5001);assert(reopened);
    control.Observe(state,false,false);assert(!control.Cancelled(*reopened));
    assert(control.Finish(*reopened,5002,[]{}));
    control.Observe(visible,false);assert(!control.Request(false,5502)); // worker did not dirty visibility
    ObservationRefreshControl parallel;parallel.Observe({},false);std::atomic<unsigned> accepted=0;
    std::vector<std::thread> callers;
    for(unsigned i=0;i<16;i++)callers.emplace_back([&] {if(parallel.Request(true,0))accepted++;});
    for(auto& caller:callers)caller.join();assert(accepted==1);
    parallel.Close();assert(!parallel.Request(false,9999));
    std::cout<<"single operation, copy isolation, held-input, cooldown and ABA checks passed\n";
}
