#include "../OptiScaler/nr/diagnostics/capability/CapabilityStore.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

using namespace DlssNr::Capability;
namespace { int allocationBudget=-1; }
void* operator new(std::size_t size) {
    if(allocationBudget==0) throw std::bad_alloc();
    if(allocationBudget>0) --allocationBudget;
    if(auto memory=std::malloc(size?size:1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory,std::size_t) noexcept { std::free(memory); }

int main(int argc,char**) {
    std::set_terminate([] {
        std::fputs("FAIL: allocation refusal escaped the diagnostics noexcept boundary\n",stderr);
        std::_Exit(86);
    });
    Store store("session");
    if(argc==1) {
        // Refuse each allocation in Capture, including the Snapshot, shared_ptr
        // control block and stream storage. Failed reason-string allocation
        // must also stay inside the diagnostics exception boundary.
        for(int budget=0;budget<4;++budget) {
            allocationBudget=budget;
            auto capture=store.Capture({});
            allocationBudget=-1;
            if(budget<3) assert(capture.state==CaptureState::Unavailable&&!capture.snapshot);
        }
    }
    // Failure must release its snapshot reservation. Exhausting the legitimate
    // four-snapshot limit under simultaneous allocator refusal must also survive.
    std::array<CaptureResult,4> retained;
    for(auto& capture:retained) { capture=store.Capture({}); assert(capture.snapshot); }
    auto ordinaryCapacity=store.Capture({});
    assert(!ordinaryCapacity.snapshot&&ordinaryCapacity.reason=="CAP_SNAPSHOT_CAPACITY");
    allocationBudget=0;
    auto refusedCapacity=store.Capture({});
    allocationBudget=-1;
    assert(refusedCapacity.state==CaptureState::Unavailable&&!refusedCapacity.snapshot);
    retained={};
    assert(store.Capture({}).snapshot);
    std::puts("PASS: allocation-refused capture/capacity survive and snapshot reservations recover");
}
