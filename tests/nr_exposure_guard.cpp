#include "../OptiScaler/shaders/dlssnr/DlssNr_ExposureGuard.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Guard = DlssNr::ExposureGuard;

static bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

int main()
{
    Guard::WhitePointHold hold;
    float candidate = 0.0f;

    const bool firstInvalid = Guard::IsSupportedWhitePoint(102.217697f, 1.0f, 1.0f, candidate);
    assert(!firstInvalid);
    assert(Near(hold.Resolve(candidate, firstInvalid, 1.0f), 1.0f));
    assert(!hold.HasValue());

    const bool accepted = Guard::IsSupportedWhitePoint(99.425102f, 1.0f, 1.0f, candidate);
    assert(accepted);
    const float acceptedWhitePoint = hold.Resolve(candidate, accepted, 1.0f);
    assert(Near(acceptedWhitePoint, 0.0100578f));
    assert(hold.HasValue());

    const bool boundaryInvalid =
        Guard::IsSupportedWhitePoint(102.217697f, 1.0f, 1.0f, candidate);
    assert(!boundaryInvalid);
    assert(Near(hold.Resolve(candidate, boundaryInvalid, 1.0f), acceptedWhitePoint));

    const bool deepInvalid = Guard::IsSupportedWhitePoint(160.566589f, 1.0f, 1.0f, candidate);
    assert(!deepInvalid);
    assert(Near(hold.Resolve(candidate, deepInvalid, 1.0f), acceptedWhitePoint));

    const bool recovered = Guard::IsSupportedWhitePoint(80.0f, 1.0f, 1.0f, candidate);
    assert(recovered);
    assert(Near(hold.Resolve(candidate, recovered, 1.0f), 0.0125f));

    hold.Reset();
    assert(!hold.HasValue());
    assert(Near(hold.Resolve(0.0f, false, 1.0f), 1.0f));

    std::puts("Exposure guard hold tests passed.");
    return 0;
}
