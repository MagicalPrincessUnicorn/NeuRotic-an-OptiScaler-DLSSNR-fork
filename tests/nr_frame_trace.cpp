#include "dlssnr/FrameTraceContract.h"
#include <array>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace DlssNr::FrameTrace;
int main()
{
    assert(ValidSession("0123456789abcdef0123456789abcdef"));
    assert(!ValidSession(""));
    assert(!ValidSession("0123456789abcdef0123456789abcdeF"));
    assert(!ValidSession("0123456789abcdef0123456789abcdef0"));
    assert(!ValidSession("0123456789abcdef0123456789abcde\n"));
    Budget budget;
    std::array<std::atomic<unsigned>, Budget::Limit + 1> seen {};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i)
        workers.emplace_back([&] {
            for (unsigned j = 0; j < 20000; ++j)
                if (const auto sequence = budget.Take()) ++seen[sequence];
        });
    for (auto& worker : workers) worker.join();
    for (size_t i = 1; i < seen.size(); ++i) assert(seen[i] == 1);
    for (unsigned i = 0; i < 100; ++i) assert(budget.Take() == 0);
    {
        Context outer(nativeObservation, 42);
        { Context inner(nativeObservation, 99); assert(nativeObservation == 99); }
        assert(nativeObservation == 42);
        std::thread other([] { assert(nativeObservation == 0); Context c(nativeObservation, 13); });
        other.join();
        assert(nativeObservation == 42);
    }
    assert(nativeObservation == 0);
    assert(presentObservation == 0);
    std::cout << "PASS: session validation, concurrent bounded IDs, exhaustion, nested/thread-local contexts\n";
}
