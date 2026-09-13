#pragma once

#include <cstdint>

namespace DlssNr
{
enum class PreSrEvent
{
    None,
    SoftReset,
    StructuralTransition,
    FrameFailure,
};

// The experimental choice is sampled only when Reset rises. Keeping it fixed until Reset falls
// prevents an in-menu edit from switching lifecycle policy halfway through a transition.
struct PreSrResetPolicyState
{
    bool resetWasRequested = false;
    bool softResetForBurst = false;
};

struct PreSrResetPolicyDecision
{
    bool resetStarted = false;
    bool resetEnded = false;
    bool softResetForBurst = false;
    bool conservativeTransition = false;
};

constexpr PreSrResetPolicyDecision AdvancePreSrResetPolicy(
    PreSrResetPolicyState& state, bool resetRequested, bool experimentalSoftResetEnabled)
{
    PreSrResetPolicyDecision decision;
    decision.resetStarted = resetRequested && !state.resetWasRequested;
    decision.resetEnded = !resetRequested && state.resetWasRequested;

    if (decision.resetStarted)
        state.softResetForBurst = experimentalSoftResetEnabled;

    decision.softResetForBurst = state.softResetForBurst;
    decision.conservativeTransition =
        !decision.softResetForBurst && (decision.resetStarted || decision.resetEnded);

    state.resetWasRequested = resetRequested;
    if (decision.resetEnded)
        state.softResetForBurst = false;

    return decision;
}

// Internal Pre-SR continuity policy. Structural evidence always wins over Reset, while a reset-only
// frame is soft only when the existing model session and all required resources remain compatible.
struct PreSrEventInput
{
    bool resetRequested = false;
    bool structuralChange = false;
    bool sessionCompatible = false;
    bool resourcesReady = false;
    bool structuralResetHeld = false;
};

constexpr PreSrEvent ClassifyPreSrEvent(const PreSrEventInput& input)
{
    if (input.structuralChange || (input.resetRequested && input.structuralResetHeld))
        return PreSrEvent::StructuralTransition;
    if (!input.resourcesReady)
        return PreSrEvent::FrameFailure;
    if (input.resetRequested)
        return input.sessionCompatible ? PreSrEvent::SoftReset
                                       : PreSrEvent::StructuralTransition;
    return PreSrEvent::None;
}

constexpr bool PassResetForFrame(bool gameReset, bool resetPending)
{
    return gameReset || resetPending;
}

// A pass consumes its reset only by successfully evaluating. A skipped or failed evaluation must
// submit Reset again on the next attempt.
constexpr bool ResetPendingAfterPass(bool resetSubmitted, bool evaluationSucceeded)
{
    return resetSubmitted && !evaluationSucceeded;
}

// Value-only reporting policy. A retained model handle is not evidence that the currently
// requested route has evaluated. Inputs are copied while the backend state is locked.
struct ReadinessInput
{
    bool enabled = false;
    bool sessionOpen = false;
    bool failed = false;
    bool featureLoaded = false;
    bool evaluated = false;
    bool resetPending = true;
    uint64_t requestedGeneration = 0;
    uint64_t evaluatedGeneration = 0;
    bool preSrRequested = false;
    bool lastEvaluationWasPreSr = false;
    bool scratchPrimed = false;
    bool awaitingEvaluation = false;
};

struct Readiness
{
    bool running = false;
    bool preSrDisplayReady = false;
    bool transitionPending = false;
    bool outputQuarantined = false;
};

inline Readiness GetReadiness(const ReadinessInput& input)
{
    Readiness result;
    const bool admitted = input.enabled && input.sessionOpen && !input.failed;
    const bool evaluated = admitted && input.featureLoaded && input.evaluated && !input.resetPending &&
                           input.requestedGeneration == input.evaluatedGeneration &&
                           input.preSrRequested == input.lastEvaluationWasPreSr;
    result.preSrDisplayReady = evaluated && input.preSrRequested && input.scratchPrimed &&
                               !input.awaitingEvaluation;
    result.running = evaluated && (!input.preSrRequested || result.preSrDisplayReady);
    result.transitionPending = admitted && !result.running;
    result.outputQuarantined = admitted && input.preSrRequested && input.awaitingEvaluation;
    return result;
}
}
