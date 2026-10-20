#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>
#include <new>
#include <atomic>
static thread_local bool countAllocations=false;
static thread_local size_t allocations=0;
void* operator new(std::size_t n) {
    if(countAllocations) ++allocations;
    if(void* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
#if __has_include("nr/diagnostics/HostCost.h")
#include "nr/diagnostics/HostCost.h"
using namespace Neurotic::HostCost;
static uint64_t tick = 0;
static uint64_t Clock() noexcept { return tick; }
int main()
{
    // Disabled means no samples, even with an explicitly supplied clock.
    { Scope s(Kind::InputBegin, 1, Clock); tick = 50; }
    assert(Read(Kind::InputBegin).samples == 0);
    Activate();
    tick = 100;
    { Scope s(Kind::InputBegin, 1, Clock); tick = 120; s.Acquired(); tick = 170; }
    auto first = Read(Kind::InputBegin);
    assert(first.samples == 1 && first.totalNs == 70 && first.beforeLockNs == 20);
    assert(first.maxNs == 70 && first.maxBeforeLockNs == 20);
    // Sampling counts only measured calls; it never advertises extrapolated totals.
    for (int i = 0; i != 4096; ++i) { Scope s(Kind::TrackBinding, 64, Clock); tick += 10; }
    assert(Read(Kind::TrackBinding).samples > 32 && Read(Kind::TrackBinding).samples < 96);
    assert(Read(Kind::TrackBinding).totalNs == Read(Kind::TrackBinding).samples * 10);
    // Concurrent producers cannot lose completed measurements.
    std::vector<std::thread> workers;
    for (int t = 0; t != 4; ++t) workers.emplace_back([] {
        for (int i = 0; i != 10000; ++i) Record(Kind::TrackBarrier, 30, 7);
    });
    for (auto& t : workers) t.join();
    auto concurrent = Read(Kind::TrackBarrier);
    assert(concurrent.samples == 40000 && concurrent.totalNs == 1200000);
    assert(concurrent.beforeLockNs == 280000 && concurrent.maxNs == 30);
    // Reporter admission is bounded and cannot re-arm after the budget.
    ReportGate gate;
    assert(gate.Take(2000000000ull));
    assert(!gate.Take(2000000001ull));
    for (uint64_t i = 2; i <= ReportLimit; ++i) assert(gate.Take(i * 2000000000ull));
    assert(!gate.Take((ReportLimit + 1) * 2000000000ull));
    countAllocations=true;
    const auto sampledStart=Now();
    for (int i=0; i!=1000000; ++i) { Scope s(Kind::TrackOther,64); }
    const auto sampledTime=Now()-sampledStart;
    const auto fullStart=Now();
    for (int i=0; i!=100000; ++i) { Scope s(Kind::InputFeed); }
    const auto fullTime=Now()-fullStart;
    countAllocations=false;
    assert(allocations==0);
    std::cout << "Timing fixture: sampled million calls ms=" << sampledTime/1e6
              << " full 100k calls ms=" << fullTime/1e6 << " allocations=" << allocations << '\n';
    Stop(); Activate();
    tick = 1000; { Scope s(Kind::InputBegin, 1, Clock); tick += 100; }
    assert(Read(Kind::InputBegin).samples == first.samples);
    std::cout << "PASS disabled gate, split timing, sampling, concurrent counters, bounded reporting, no rearm\n";
}
#else
int main() { std::cerr << "FAIL missing bounded host timing implementation\n"; return 1; }
#endif
