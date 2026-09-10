#include "dlssnr/NrToggleBurst.h"

#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    DlssNr::ToggleBurstTracker tracker(0x12345678u);
    Check(!tracker.Click(0.0, 25), "click 1 is quiet");
    Check(!tracker.Click(0.4, 25), "click 2 is quiet");
    Check(!tracker.Click(0.8, 25), "click 3 is quiet");
    const auto fourth = tracker.Click(1.2, 25);
    Check(fourth && *fourth < 25, "click 4 emits one approved message");
    Check(!tracker.Click(1.4, 25), "click 5 is quiet");
    const auto sixth = tracker.Click(1.6, 25);
    Check(sixth && *sixth < 25, "click 6 emits while burst continues");
    Check(*sixth != *fourth, "immediate message repeat is prevented");
    Check(!tracker.Click(4.0, 25), "rolling window resets after two seconds");
    Check(!tracker.Click(4.2, 25) && !tracker.Click(4.4, 25), "new burst starts quietly");
    Check(tracker.Click(4.6, 25).has_value(), "new burst emits on its fourth click");
    Check(!tracker.Click(4.7, 0), "empty message list remains safe");
    std::cout << "PASS: NR checkbox burst cadence, reset, bounds, and no immediate repeats\n";
}
