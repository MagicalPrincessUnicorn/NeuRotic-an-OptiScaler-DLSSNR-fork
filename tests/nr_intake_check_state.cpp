#include "../OptiScaler/dlssnr/NrIntakeCheckState.h"
#include <cassert>
#include <cstdio>
int main() {
    using R = DlssNr::IntakeCheckState::Result;
    DlssNr::IntakeCheckState state;
    DlssNr::Capability::RefreshState context;
    assert(state.Observe(context, false, 1, true) == R::Idle);
    state.Request(context, 1);
    assert(state.Observe(context, true, 1, true) == R::Checking);
    assert(state.Observe(context, false, 2, true) == R::Complete);
    assert(state.Observe(context, false, 3, false) == R::Complete); // later auto refresh cannot erase manual outcome
    context.configurationRevision = 1;
    assert(state.Observe(context, false, 3, false) == R::Changed);
    state.Request(context, 3);
    assert(state.Observe(context, false, 4, false) == R::Unavailable); // automatic report is not manual success
    assert(state.Observe(context, false, 5, true) == R::Complete);
    ++context.native.lifecycleGeneration;
    assert(state.Observe(context, false, 5, true) == R::Changed);
    state.Request(context, 5);
    ++context.menuVisibilityGeneration;
    assert(state.Observe(context, false, 6, true) == R::Changed);
    std::puts("PASS: manual refresh completion, unavailable outcome, automatic report retention and config/owner/reopen invalidation");
}
