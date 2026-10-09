#include <menu/Localization.h>
#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include <cmath>

namespace DlssNr::MenuStatus
{
enum class State { Selectable, Attempting, Active, OriginalImageFallback, Recovering, Unsupported, RestartRequired };
inline const char* StateNames[] = { Neurotic::UiLiteral("ingame.option.f84a01b259a8", "Selectable"), Neurotic::UiLiteral("ingame.option.4b6d57ff3e79", "Attempting"), Neurotic::UiLiteral("ingame.menu-common.active_3203f178", "Active"), Neurotic::UiLiteral("ingame.dlssnr-menustatus.original_image_fallback_cbaceef7", "Original-image fallback"),
    Neurotic::UiLiteral("ingame.option.d90064dbdb11", "Recovering"), Neurotic::UiLiteral("ingame.option.54324658e2eb", "Unsupported"), Neurotic::UiLiteral("ingame.dlssnr-menustatus.restart_required_e1c4df74", "Restart required") };
// Optional, bounded display input. Only a telemetry producer may establish these facts.
// This UI experiment does not supply a provider or classify real/generated frames.
struct RuntimeStatus
{
    State state = State::Selectable;
    std::array<char, 48> provider {};
    std::array<char, 64> route {};
    std::array<char, 192> reason {};
    std::optional<uint64_t> realFrame;
    uint32_t workWidth = 0, workHeight = 0, outputWidth = 0, outputHeight = 0;
};
// UI-only observation fence: a selection change cannot reuse an earlier Present readout.
// No renderer state, history, logging or resources are changed here.
struct SelectionObservation
{
    uint64_t selection = 0;
    uint64_t attemptAtChange = 0;
    bool initialized = false;

    bool Fresh(uint64_t currentSelection, uint64_t attempt)
    {
        if (!initialized || currentSelection != selection)
        {
            initialized = true;
            selection = currentSelection;
            attemptAtChange = attempt;
            return false;
        }
        return attempt != attemptAtChange;
    }
};
// UI-only activity expires when the selected owner stops publishing fresh work.
struct ActivityObservation
{
    uint64_t selection=0, counter=0;
    uint64_t owner=0;
    double lastProgress=-10;
    bool initialized=false, progressed=false;
    bool Fresh(uint64_t selected, uint64_t completed, double now, uint64_t generation=0)
    {
        if(!initialized || selection!=selected || owner!=generation || completed<counter || now<lastProgress) {
            initialized=true; selection=selected; owner=generation; counter=completed; progressed=false; lastProgress=now;
            return false;
        }
        if(completed>counter) { counter=completed; lastProgress=now; progressed=true; }
        return progressed && now-lastProgress<=1.0;
    }
};
// Presentation only: asynchronous queries need not produce a new sample each UI frame.
// Never carries measurements across configuration/owner changes or inactive output.
struct TimingObservation
{
    uint64_t selection=0, owner=0;
    double sampledAt=0;
    std::optional<double> value;
    std::optional<double> Update(uint64_t selected, uint64_t generation, bool active,
                                 std::optional<double> sample, double now)
    {
        if (selection!=selected || owner!=generation || now<sampledAt || !active)
            value.reset();
        selection=selected; owner=generation;
        if (!active) return {};
        if (sample && std::isfinite(*sample) && *sample>=0) { value=sample; sampledAt=now; }
        if (now-sampledAt>1.0) value.reset();
        return value;
    }
};

}
