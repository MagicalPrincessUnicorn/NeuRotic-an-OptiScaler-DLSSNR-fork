#pragma once

namespace DlssNr
{
struct PrimaryPlacementGeneration
{
    bool known = false;
    bool preSr = false;

    void Created(bool createdForPreSr)
    {
        known = true;
        preSr = createdForPreSr;
    }

    void Retired() { known = false; }

    bool RequiresRetirement(bool requestedPreSr) const
    {
        return known && preSr != requestedPreSr;
    }
};
} // namespace DlssNr
