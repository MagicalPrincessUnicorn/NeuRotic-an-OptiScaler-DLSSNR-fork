#pragma once
#include <cstdint>
#include <array>
#include <optional>

namespace DlssNr::MenuStatus
{
enum class State { Selectable, Attempting, Active, OriginalImageFallback, Recovering, Unsupported, RestartRequired };
inline constexpr const char* StateNames[] = { "Selectable", "Attempting", "Active", "Original-image fallback",
    "Recovering", "Unsupported", "Restart required" };
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
}
