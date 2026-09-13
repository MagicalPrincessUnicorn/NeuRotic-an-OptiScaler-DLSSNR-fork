#include "../OptiScaler/dlssnr/NrAdvisorSampling.h"
#include "../OptiScaler/menu/input/MenuInputPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>
int main()
{
    using namespace DlssNr::AdvisorSampling;
    for (unsigned mask = 0; mask < 8; ++mask)
    {
        OptiInput::MenuInputPolicy p {bool(mask & 1), bool(mask & 2), bool(mask & 4)};
        assert(p.Mouse(true) == !(mask & 1));
        assert(p.Keyboard(true) == !(mask & 2));
        assert(p.Controller(true) == !(mask & 4));
        assert(!p.Mouse(false) && !p.Keyboard(false) && !p.Controller(false));
        p.captureKey = true;
        assert(p.Mouse(true) && p.Keyboard(true) && p.Controller(true));
        p.captureKey = false;
        assert(p.Controller(true) == !(mask & 4));
    }
    OptiInput::MenuInputPolicy defaults;
    assert(defaults.Mouse(true) && !defaults.Keyboard(true) && !defaults.Controller(true));
    Window w;
    Cadence c {1, 9, 5, 2, 16.0, false};
    assert(!w.Consume(c)); // Unknown FG cadence cannot qualify, even at a fast interval.
    c.native = true;
    assert(!w.Consume(c)); // Re-reading the same observation never creates a sample.
    c.sequence = 2;
    assert(w.Consume(c));
    assert(!w.Consume(c));
    c.sequence = 3; c.intervalMs = std::numeric_limits<double>::quiet_NaN();
    assert(!w.Consume(c));
    w.baselineMedianMs = 80;
    assert(!w.Stall(640) && w.Stall(641));
    w.baselineMedianMs = 16;
    assert(!w.Stall(500) && w.Stall(501));
    w.samples = 119; w.totalMs = 119 * 16.0;
    assert(!w.Complete(3.1));
    ++w.samples; w.totalMs += 16;
    assert(!w.Complete(2.99) && w.Complete(3));
    assert(w.Fps() == 62.5); // Already-native interval is never divided by an FG ratio.
    std::cout << "PASS eight input policies, capture ownership, defaults, native cadence freshness, unknown/invalid refusal, sample bounds and stall thresholds\n";
}
