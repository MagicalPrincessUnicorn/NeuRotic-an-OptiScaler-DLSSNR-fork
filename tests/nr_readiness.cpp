#include "../OptiScaler/dlssnr/NrReadiness.h"
#include <cassert>
#include <cstdio>
#include <array>

// Multi-frame fault-injection harness for production value policies. GPU/resource operations are
// doubles, not a D3D12 runtime test; source wiring checks separately enforce early observation.
struct ResetSequence
{
    DlssNr::PreSrResetPolicyState policy {};
    std::array<bool, 10> pending {};
    bool held = false;
    bool primed = true;
    unsigned int retired = 0;
    unsigned int published = 0;
    DlssNr::PreSrResetFrameResult last {};

    void Frame(bool reset, bool enabled = true, bool entryReady = true,
               bool prepareReady = true, bool structural = false,
               int failedPass = -1, unsigned int passes = 2)
    {
        using namespace DlssNr;
        const auto decision = AdvancePreSrResetPolicy(policy, reset, enabled);
        last = {};
        if (reset && decision.softResetForBurst)
            for (auto& p : pending) p = PassResetForFrame(true, p);
        if (decision.resetEnded) held = false;
        if (!entryReady) return;
        const bool transition = PreSrStructuralChange(structural || decision.conservativeTransition,
                                                       decision.softResetForBurst, false, false);
        // primed is intentionally not part of session compatibility.
        const auto event = ClassifyPreSrEvent({reset, transition, true, true, held});
        held = HoldPreSrStructuralReset(held, reset, event);
        if (transition)
        {
            ++retired;
            primed = false;
            pending.fill(true);
        }
        if (reset && (!decision.softResetForBurst || event != PreSrEvent::SoftReset)) return;
        if (!prepareReady) { primed = false; return; }
        for (unsigned int pass = 0; pass < passes; ++pass)
        {
            const bool success = static_cast<int>(pass) != failedPass;
            last.RecordPass(pass, success);
            pending[pass] = ResetPendingAfterPass(pending[pass], success);
            if (!success) return;
        }
        ++published;
        primed = true;
    }
};

void TestResetSequences()
{
    using namespace DlssNr;
    for (unsigned int length : {1u, 2u, 30u})
    {
        ResetSequence s;
        for (unsigned int frame = 0; frame < length; ++frame)
        {
            s.Frame(true, true, true, true, false, -1, 10);
            assert(s.last.AllPassesSucceeded(10));
            for (bool pending : s.pending) assert(!pending);
        }
        s.Frame(false);
        assert(s.retired == 0 && s.published == length + 1 && !s.held);
    }
    // A one-frame Reset on a skipped entry survives into the following ordinary frame.
    ResetSequence skipped;
    skipped.Frame(true, true, false);
    assert(skipped.pending[0] && skipped.pending[1]);
    skipped.Frame(false);
    assert(!skipped.pending[0] && skipped.published == 1 && skipped.retired == 0);

    ResetSequence busy;
    busy.Frame(true, true, true, false);
    assert(!busy.primed && busy.pending[0]);
    busy.Frame(true);
    assert(busy.published == 1 && busy.retired == 0 && !busy.held);
    busy.Frame(false);
    assert(busy.published == 2 && busy.retired == 0);

    // Every pass owns its own debt. Failure circuits remain a separate backend safety authority.
    for (int failed = 0; failed < 10; ++failed)
    {
        ResetSequence s;
        s.Frame(true, true, true, true, false, failed, 10);
        for (int pass = 0; pass < 10; ++pass) assert(s.pending[pass] == (pass >= failed));
        assert(!s.last.AllPassesSucceeded(10));
        s.Frame(false, true, true, true, false, -1, 10);
        assert(s.last.AllPassesSucceeded(10) && s.retired == 0);
    }
    ResetSequence changed;
    changed.Frame(true);
    changed.Frame(true, true, true, true, true); // resize/format/quality/release/restart evidence
    changed.Frame(true);
    assert(changed.retired == 1 && changed.published == 1 && changed.held);
    changed.Frame(false);
    assert(changed.retired == 1 && changed.published == 2 && !changed.held);

    ResetSequence off;
    off.Frame(true, false);
    off.Frame(true, false);
    off.Frame(false, false);
    assert(off.retired == 2 && off.published == 1); // legacy rising/falling transitions
    assert(!PreSrStructuralChange(false, false, true, true));
    assert(PreSrStructuralChange(false, true, true, false));
    assert(PreSrStructuralChange(false, true, false, true));
    assert(!HoldPreSrStructuralReset(false, true, PreSrEvent::FrameFailure));
    assert(HoldPreSrStructuralReset(true, true, PreSrEvent::FrameFailure));

    PreSrResetFrameResult partial;
    partial.RecordPass(0, true);
    partial.RecordPass(1, false);
    assert(partial.attempts == 2 && partial.successes == 1 && !partial.AllPassesSucceeded(2));
}

int main()
{
    using namespace DlssNr;
    TestResetSequences();

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
