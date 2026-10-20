#include "../OptiScaler/nr/diagnostics/candidate/ManualSession.h"
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>
using namespace DlssNr::CandidateObserver;
int main() {
    ManualRouter router;
    auto a=std::make_unique<ManualSession>(1);
    auto b=std::make_unique<ManualSession>(2);
    assert(router.Open(*a));
    assert(!router.Open(*b));
    auto held=router.Pin(); assert(held);
    Event e{};e.kind=Kind::Committed;e.source=1;e.resourceBits=11;e.deviceBits=22;
    e.descriptorKnown=true;e.descriptor.dimension=3;e.descriptor.width=1920;e.descriptor.height=1080;
    e.descriptor.format=40;
    assert(held.Emit(e)==EmitResult::Recorded);
    a->Advance(1999);a->Close();
    assert(!router.Retire(*a));
    assert(!router.Open(*b));
    assert(held.Emit(e)==EmitResult::Closing);
    held={};a->Advance(2000);assert(router.Retire(*a));
    auto result=a->Read();assert(result.count==1 && result.candidates[0].depthSupported);
    assert(!result.candidates[0].mayAdmit && result.coverage.enqueued==1);
    assert(router.Open(*b));
    assert(b->Read().count==0 && b->Read().coverage.enqueued==0);
    std::vector<std::thread> writers;
    for(unsigned i=0;i<8;i++) writers.emplace_back([&] {
        for(unsigned j=0;j<1000;j++) {auto lease=router.Pin();if(lease) (void)lease.Emit(e);}
    });
    for(auto& writer:writers)writer.join();
    b->Close();b->Advance(2000);assert(router.Retire(*b));
    auto final=b->Read();assert(final.coverage.enqueued+final.coverage.dropped>0);
    for(unsigned i=0;i<final.count;i++)assert(!final.candidates[i].mayAdmit);
    for(unsigned i=3;i<100;i++) {
        auto next=std::make_unique<ManualSession>(i);assert(router.Open(*next));
        assert(!router.Pin().SessionMatches(i-1));next->Close();next->Advance(2000);assert(router.Retire(*next));
    }
    std::cout<<"manual pinned-session and concurrent producer checks passed\n";
}
