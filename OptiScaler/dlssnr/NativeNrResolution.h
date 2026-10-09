#pragma once
#include "DlssNr_PresentResolution.h"

namespace DlssNr::NativeResolution
{
// Match Native describes NR's private raster. RR still owns its original inputs
// and must complete before this path; a pre-SR scratch dispatch is a different path.
template<class C> bool MatchAfterReconstruction(const C& cfg, bool forcedAfter, bool preSrScratch)
{
    return forcedAfter && !preSrScratch && cfg.DlssNrRoute.value_or_default() == 0 &&
        cfg.DlssNrRunBeforeSr.value_or_default() && !cfg.DlssNrUiManualResolution.value_or_default() &&
        cfg.DlssNrWorkingScale.value_or_default() == 1.0f;
}
inline PresentResolution::Size MatchWorkSize(unsigned renderWidth, unsigned renderHeight,
                                            unsigned outputWidth, unsigned outputHeight)
{
    // Only current evaluation subrect dimensions are accepted. Never substitute
    // an old frame, a texture allocation capacity, or an assumed percentage.
    if (renderWidth < 8 || renderHeight < 8 || renderWidth > outputWidth || renderHeight > outputHeight)
        return {0, 0, "Match Native awaits valid current render-input dimensions"};
    return {renderWidth & ~7u, renderHeight & ~7u, nullptr};
}
inline void PublishWorkSize(PresentResolution::Size work, const char*& refusal, bool& resetPending)
{
    refusal = work.reason ? work.reason : "";
    // Recovery needs a real evaluation before it can be reported as running.
    // A temporary metadata gap never permanently fails the model.
    if (work.reason) resetPending = true;
}
}
