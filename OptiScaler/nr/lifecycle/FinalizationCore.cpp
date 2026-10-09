// Product compilation of the finalizer surface. Live owners are injected by the
// process/session composition root; no static frame identity or GPU owner here.
#include "FinalRealFrameFinalizer.h"
#include "FinalRealFrameReset.h"
#include "FinalRealFramePreFgAdapter.h"
static_assert(!std::is_default_constructible_v<Neurotic::Lifecycle::PrimaryNrClaimHandle>);
