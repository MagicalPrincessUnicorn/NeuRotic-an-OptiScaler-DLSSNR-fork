#pragma once
#include <cstdint>

namespace DlssNr::MenuStatus
{
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
