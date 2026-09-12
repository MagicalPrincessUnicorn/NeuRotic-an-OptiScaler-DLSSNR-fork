#include "../OptiScaler/dlssnr/NrPlacementLifecycle.h"
#include <cassert>
#include <cstdio>

int main()
{
    DlssNr::PrimaryPlacementGeneration placement;
    assert(!placement.RequiresRetirement(false));
    assert(!placement.RequiresRetirement(true));

    placement.Created(true);
    assert(!placement.RequiresRetirement(true));
    assert(placement.RequiresRetirement(false));

    // Beginning retirement removes the old generation's route identity immediately. The caller may
    // poll real GPU completion for any number of frames without retiring each replacement again.
    placement.Retired();
    for (int frame = 0; frame < 10000; ++frame)
        assert(!placement.RequiresRetirement(false));

    placement.Created(false);
    assert(!placement.RequiresRetirement(false));
    assert(placement.RequiresRetirement(true));
    placement.Retired();
    placement.Created(true);
    assert(!placement.RequiresRetirement(true));

    std::puts("PASS: placement ownership is scoped to one primary feature generation");
}
