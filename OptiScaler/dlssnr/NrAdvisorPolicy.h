#pragma once
#include "DlssNr_StageUi.h"

namespace DlssNr::AdvisorPolicy
{
inline constexpr int Before = 0, After = 1;
inline constexpr int RouteCount(int stage) { return stage == Before ? 1 : 3; }
inline constexpr bool Contains(int stage, int route)
{ return (stage == Before || stage == After) && route >= 0 && route < RouteCount(stage); }

// Used by both preflight and execution. Never inherit Native's previous placement.
template<class C> void SelectPlacement(C& config, int stage, int route)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    config.DlssNrRoute = uint32_t(route);
    NrConfigState::SetRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr,
                                 stage == Before ? 1 : 0);
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
