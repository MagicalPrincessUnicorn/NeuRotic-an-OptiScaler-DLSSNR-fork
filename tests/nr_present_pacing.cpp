#include "../OptiScaler/dlssnr/DlssNr_PresentPacing.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace DlssNr::PresentPacing;

static bool Near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 0.000001;
}

int main()
{
    Window<4, 2> window;
    CallToken token {};

    assert(!window.beginCall(Route::PresentImageOnly, 1, token).has_value());
    assert(!token.eligible);
    assert(!window.beginCall(Route::PresentImageOnly, 2, token).has_value());
    assert(!token.eligible);

    for (std::uint64_t call = 3; call <= 6; ++call)
    {
        assert(!window.beginCall(Route::PresentImageOnly, call, token).has_value());
        assert(token.eligible);
        assert(window.recordCall(token, {static_cast<double>(call), 1.0, 2.0, 3.0, call == 6}));
        assert(window.expectGpu(token));
        if (call != 6)
            assert(window.recordGpu(token, static_cast<double>(call) + 10.0, 20.0, call - 2));
    }

    auto rollover = window.beginCall(Route::PresentImageOnly, 7, token);
    assert(rollover.has_value() && rollover->valid);
    assert(rollover->warmupDiscarded == 2);
    assert(rollover->frameInterval.samples == 4);
    assert(Near(rollover->frameInterval.average, 4.5));
    assert(Near(rollover->frameInterval.median, 4.5));
    assert(Near(rollover->frameInterval.p95, 6.0));
    assert(Near(rollover->frameInterval.maximum, 6.0));
    assert(rollover->failedPresents == 1);
    assert(rollover->expectedGpuSamples == 4);
    assert(rollover->presentGpu.samples == 3);
    assert(rollover->missingGpuSamples == 1);
    assert(token.eligible); // rollover does not introduce another warm-up period

    const auto oldToken = CallToken {Route::PresentImageOnly, rollover->serial, 6, true};
    assert(!window.recordGpu(oldToken, 16.0, 20.0, 4)); // a delayed old-window result is rejected
    assert(window.recordCall(token, {7.0, 1.0, 2.0, 3.0, false}));
    window.observePending(token, 3);

    auto routeChange = window.beginCall(Route::NativeTemporal, 8, token);
    assert(routeChange.has_value() && routeChange->valid);
    assert(routeChange->frameInterval.samples == 1);
    assert(routeChange->pendingSlotsHighWater == 3);
    assert(!token.eligible);
    assert(token.route == Route::NativeTemporal);

    assert(!window.beginCall(Route::NativeTemporal, 9, token).has_value());
    assert(!token.eligible);
    assert(!window.beginCall(Route::NativeTemporal, 10, token).has_value());
    assert(token.eligible);
    assert(window.recordCall(token, {10.0, 0.1, 0.2, 0.3, false}));

    std::puts("Present pacing statistics tests passed.");
    return 0;
}
