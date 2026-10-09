#pragma once
#include "DlssNr_StageUi.h"
#include "NrAdvisorSampling.h"
#include "NrExperimentalPolicy.h"

namespace DlssNr::AdvisorPolicy
{
inline constexpr int Before = 0, After = 1;
inline constexpr int RouteCount(int stage) { return stage == Before ? 1 : 3; }
inline constexpr bool Contains(int stage, int route)
{ return (stage == Before || stage == After) && route >= 0 && route < RouteCount(stage); }

inline bool CurrentFailure(int route, uint64_t trialGeneration, uint64_t startAttempt,
                           const AdvisorSampling::Cadence& cadence, uint64_t fallbackAttempt)
{
    return (route == 1 || route == 2) && cadence.sequence != 0 && cadence.route == unsigned(route) &&
        cadence.configurationGeneration == trialGeneration && fallbackAttempt > startAttempt;
}

inline const char* MissingFeedback(bool receivedOutput)
{
    return receivedOutput
        ? "Model feedback arrived, but matching native frame timing was not verified within five seconds. Unmeasured."
        : "No model feedback within five seconds. Check the route status and retry in live gameplay.";
}

// Used by both preflight and execution. Never inherit Native's previous placement.
template<class C> void SelectPlacement(C& config, int stage, int route)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    config.DlssNrRoute = uint32_t(route);
    // Live Config uses synchronized optionals; immutable preflight snapshots use
    // CustomOptional. Assign the same pair under the shared lock for either type.
    config.DlssNrRenderingMode = int32_t(stage == Before ? 1 : 0);
    config.DlssNrRunBeforeSr = stage == Before;
}

template<class C> const char* Refusal(C proposed, int stage, int route, int resolution,
                                     bool vulkan, bool rayReconstruction)
{
    if (!Contains(stage, route)) return "This route is unavailable at the selected Advisor stage.";
    if (vulkan && route != 0) return "Present routes do not support Vulkan.";
    SelectPlacement(proposed, stage, route);
    if (const auto* reason = StageUi::NativePlacementRefusal(proposed, vulkan || rayReconstruction))
        return reason;
    return StageUi::ResolutionRefusal(proposed, resolution);
}
}
