#include "dlssnr/NrPreflightSignals.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    NrPreflightSignals::NativeInputs inputs;
    assert(inputs.observations == 0);
    assert(!inputs.jitter && !inputs.motionScale && !inputs.preExposure);

    // A missing component must not turn an effective default into an observed pair.
    inputs.Record(true, 0.125f, false, 0.0f,
                  true, 1706.0f, true, 960.0f,
                  false, 1.0f, false);
    assert(inputs.observations == 1);
    assert(!inputs.jitter);
    assert(inputs.motionScale && inputs.motionScale->x == 1706.0f && inputs.motionScale->y == 960.0f);
    assert(!inputs.preExposure && !inputs.exposureTexture);

    // Zero jitter is a real value when both source components were supplied.
    inputs.Record(true, 0.0f, true, 0.0f,
                  true, 1.0f, true, -1.0f,
                  true, 1.0f, true);
    assert(inputs.observations == 2);
    assert(inputs.jitter && inputs.jitter->x == 0.0f && inputs.jitter->y == 0.0f);
    assert(inputs.motionScale && inputs.motionScale->x == 1.0f && inputs.motionScale->y == -1.0f);
    assert(inputs.preExposure && *inputs.preExposure == 1.0f && inputs.exposureTexture);

    // Each callback replaces the last observation; old values cannot masquerade as current inputs.
    inputs.Record(true, INFINITY, true, 0.25f,
                  false, 1.0f, false, 1.0f,
                  true, 0.0f, false);
    assert(inputs.observations == 3);
    assert(!inputs.jitter && !inputs.motionScale);
    assert(inputs.preExposure && *inputs.preExposure==0.0f);
    assert(!inputs.exposureTexture);
    assert(inputs.jitterSupplied && !inputs.motionScaleSupplied && inputs.preExposureSupplied);
    inputs.Record(false,0,false,0,true,NAN,true,1,true,INFINITY,false);
    assert(!inputs.jitterSupplied && inputs.motionScaleSupplied && inputs.preExposureSupplied);
    assert(!inputs.jitter && !inputs.motionScale && !inputs.preExposure);
    inputs.Record(false,0,false,0,false,0,false,0,false,0,false);
    assert(!inputs.jitterSupplied && !inputs.motionScaleSupplied && !inputs.preExposureSupplied);

    std::cout << "PASS: Preflight distinguishes supplied values from defaults and stale inputs\n";
}
