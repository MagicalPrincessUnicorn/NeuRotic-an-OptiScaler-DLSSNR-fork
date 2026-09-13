#include "../OptiScaler/dlssnr/NrReadiness.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace DlssNr;

    const PreSrEventInput compatibleReset { true, false, true, true, false };
    assert(ClassifyPreSrEvent(compatibleReset) == PreSrEvent::SoftReset); // one-frame reset
    assert(ClassifyPreSrEvent(compatibleReset) == PreSrEvent::SoftReset); // held reset, frame two
    assert(ClassifyPreSrEvent({ false, false, true, true, false }) == PreSrEvent::None); // falling edge
    assert(ClassifyPreSrEvent({ true, true, true, true, false }) ==
           PreSrEvent::StructuralTransition); // reset plus resize/format/quality/config change
    assert(ClassifyPreSrEvent({ true, false, true, true, true }) ==
           PreSrEvent::StructuralTransition); // same-size native feature replacement remains hard
    assert(ClassifyPreSrEvent({ true, false, true, false, false }) ==
           PreSrEvent::FrameFailure); // missing resources never become a soft reset
    assert(ClassifyPreSrEvent({ true, false, false, true, false }) ==
           PreSrEvent::StructuralTransition); // incompatible model/pass session

    PreSrResetPolicyState resetPolicy;
    auto policy = AdvancePreSrResetPolicy(resetPolicy, true, false);
    assert(policy.resetStarted && !policy.softResetForBurst && policy.conservativeTransition);
    policy = AdvancePreSrResetPolicy(resetPolicy, true, true);
    assert(!policy.resetStarted && !policy.softResetForBurst && !policy.conservativeTransition);
    policy = AdvancePreSrResetPolicy(resetPolicy, false, true);
    assert(policy.resetEnded && !policy.softResetForBurst && policy.conservativeTransition);

    policy = AdvancePreSrResetPolicy(resetPolicy, true, true);
    assert(policy.resetStarted && policy.softResetForBurst && !policy.conservativeTransition);
    policy = AdvancePreSrResetPolicy(resetPolicy, true, false);
    assert(!policy.resetStarted && policy.softResetForBurst && !policy.conservativeTransition);
    policy = AdvancePreSrResetPolicy(resetPolicy, false, false);
    assert(policy.resetEnded && policy.softResetForBurst && !policy.conservativeTransition);
    policy = AdvancePreSrResetPolicy(resetPolicy, true, false);
    assert(policy.resetStarted && !policy.softResetForBurst && policy.conservativeTransition);

    bool passReset[10] = {};
    for (bool& reset : passReset)
        reset = PassResetForFrame(true, reset);
    for (bool reset : passReset)
        assert(reset); // every active multipass layer receives the game's Reset
    passReset[0] = ResetPendingAfterPass(passReset[0], false);
    assert(passReset[0]); // failed evaluation retains Reset
    passReset[0] = ResetPendingAfterPass(passReset[0], true);
    assert(!passReset[0]); // successful evaluation consumes Reset for this pass only
    assert(ResetPendingAfterPass(true, false)); // skipped/failed evaluation
    assert(!ResetPendingAfterPass(true, true));

    ReadinessInput state;
    assert(!GetReadiness(state).running && !GetReadiness(state).transitionPending);
    state.enabled = state.sessionOpen = true;
    assert(GetReadiness(state).transitionPending && !GetReadiness(state).running);
    state.featureLoaded = true;
    assert(!GetReadiness(state).running); // creation alone is not evaluation
    state.evaluated = true;
    state.resetPending = false;
    assert(GetReadiness(state).running);
    state.enabled = false;
    assert(!GetReadiness(state).running && !GetReadiness(state).transitionPending);
    state.enabled = true;
    ++state.requestedGeneration;
    assert(!GetReadiness(state).running); // retained history from before the toggle
    state.evaluatedGeneration = state.requestedGeneration;
    assert(GetReadiness(state).running);
    state.preSrRequested = true;
    assert(!GetReadiness(state).running); // still reflects the previous Post-SR route
    state.lastEvaluationWasPreSr = true;
    state.awaitingEvaluation = true;
    assert(!GetReadiness(state).preSrDisplayReady && GetReadiness(state).outputQuarantined);
    state.scratchPrimed = true;
    state.awaitingEvaluation = false;
    assert(GetReadiness(state).running && GetReadiness(state).preSrDisplayReady);
    state.failed = true;
    assert(!GetReadiness(state).running && !GetReadiness(state).preSrDisplayReady &&
           !GetReadiness(state).transitionPending);
    state.failed = false;
    state.sessionOpen = false;
    assert(!GetReadiness(state).running);
    state.sessionOpen = true;
    state.resetPending = true;
    assert(!GetReadiness(state).running);
    for (unsigned i = 0; i < 10000; ++i)
    {
        state.enabled = false;
        assert(!GetReadiness(state).running);
        state.enabled = true;
        ++state.requestedGeneration;
        assert(!GetReadiness(state).running);
        state.evaluatedGeneration = state.requestedGeneration;
        state.resetPending = false;
        assert(GetReadiness(state).running);
        state.resetPending = true;
    }
    std::puts("PASS: default-off burst-latched soft reset policy, multipass reset delivery, readiness, failure, shutdown, 10000 toggles");
}
