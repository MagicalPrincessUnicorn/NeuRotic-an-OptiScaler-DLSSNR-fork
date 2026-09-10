#include "dlssnr/NrToggleBurst.h"
#include "dlssnr/NrToggleNotes.h"

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
    constexpr auto count = DlssNr::ToggleBurstMessages.size();
    static_assert(count == 41);
    Check(!tracker.Click(0.0, count), "checkbox toggle 1 is quiet");
    Check(!tracker.Click(0.4, count), "hotkey toggle 2 shares the burst and is quiet");
    Check(!tracker.Click(0.8, count), "checkbox toggle 3 is quiet");
    const auto fourth = tracker.Click(1.2, count);
    Check(fourth && *fourth < count, "mixed-input toggle 4 emits one approved message");
    Check(!tracker.Click(1.4, count), "toggle 5 is quiet");
    const auto sixth = tracker.Click(1.6, count);
    Check(sixth && *sixth < count, "toggle 6 emits while burst continues");
    Check(*sixth != *fourth, "immediate message repeat is prevented");
    Check(!tracker.Click(4.0, count), "rolling window resets after two seconds");
    Check(!tracker.Click(4.2, count) && !tracker.Click(4.4, count), "new burst starts quietly");
    Check(tracker.Click(4.6, count).has_value(), "new burst emits on its fourth click");
    Check(!tracker.Click(4.7, 0), "empty message list remains safe");

    // Non-user state changes deliberately do not call Click. Their timestamps therefore cannot
    // advance the cadence between the two explicit user entry points.
    DlssNr::ToggleBurstTracker userOnly(0x87654321u);
    Check(!userOnly.Click(10.0, count), "user checkbox begins a new burst");
    // INI reload, route change, programmatic configuration, and another control happen here.
    Check(!userOnly.Click(10.5, count), "user hotkey remains only toggle 2 after non-user changes");
    Check(!userOnly.Click(11.0, count), "next checkbox is toggle 3");
    Check(userOnly.Click(11.5, count).has_value(), "next hotkey is shared toggle 4");

    DlssNr::ToggleBurstTracker sustained(0x01020304u);
    Check(!sustained.Click(0.0, count) && !sustained.Click(0.5, count) &&
          !sustained.Click(1.0, count) && sustained.Click(1.5, count).has_value(),
          "rolling two-second threshold emits at toggle 4");
    Check(!sustained.Click(2.0, count), "sustained burst toggle 5 is quiet even as old entries age out");
    Check(sustained.Click(2.5, count).has_value(), "sustained burst toggle 6 emits after rolling-window aging");
    Check(!sustained.Click(3.0, count), "sustained burst toggle 7 is quiet");
    Check(sustained.Click(3.5, count).has_value(), "sustained burst toggle 8 emits");

    DlssNr::ToggleBurstTracker repetition(0xabcdef01u);
    std::optional<std::size_t> previous;
    for (int i = 0; i < 80; ++i)
    {
        const auto selected = repetition.Click(20.0 + i * 0.01, count);
        if (selected)
        {
            Check(!previous || *selected != *previous, "random selection never immediately repeats");
            previous = selected;
        }
    }
    std::cout << "PASS: shared NR user-toggle cadence, reset, non-user exclusion, 41-message bounds, and no immediate repeats\n";
}
