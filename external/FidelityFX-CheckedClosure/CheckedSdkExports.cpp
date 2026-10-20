// No production capability is published until the full failure/coverage review is closed.
// Internal owner functions are exercised only by SDK-local CPU/WARP mechanics tests.
#include "ffx_neurotic_checked_closure.h"
extern "C" FfxNrStatusV1 __cdecl ffxNeuRoticQueryClosureServiceV1(FfxNrClosureServiceV1* service) {
    if(!service || service->size!=sizeof(*service) || service->version!=1) return FFX_NR_INVALID_ARGUMENT;
    return FFX_NR_UNSUPPORTED;
}
extern "C" FfxNrStatusV1 __cdecl ffxNeuRoticQueryDispatchServiceV1(FfxNrDispatchServiceV1* service) {
    if(!service || service->size!=sizeof(*service) || service->version!=1) return FFX_NR_INVALID_ARGUMENT;
    return FFX_NR_UNSUPPORTED;
}

