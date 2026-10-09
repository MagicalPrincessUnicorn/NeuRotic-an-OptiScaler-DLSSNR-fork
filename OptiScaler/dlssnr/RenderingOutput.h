#pragma once
#include "RenderingOutputPolicy.h"
#include <string>
namespace DlssNr::RenderingOutput {
enum class Phase : uint32_t { Off, Waiting, Active, Quiescing, StartingFallback, FallbackActive, Blocked, FallbackPaused };
struct Snapshot {
    uint64_t sampledAt=0,completed=0,sourceFrames=0;
    uint32_t route=2;
    Producer producer=Producer::None;
    Phase phase=Phase::Off;
    bool requested=false,active=false,fallbackEligible=false,displayObserved=false,comparisonBypass=false;
    std::string reason;
};
// Render callbacks publish only a heartbeat and exact target. Supervision is off-thread.
void Signal(uintptr_t window,bool menuOpen,bool vulkan=false) noexcept;
Snapshot Query();
}
