#pragma once

#include "DlssNr_BasicMultipass.h"
#include <algorithm>

namespace DlssNr::Multipass
{
template<class C> unsigned int RequestedCount(const C& cfg)
{
    if (!cfg.DlssNrMultipassEnabled.value_or_default()) return 1;
    if (BasicMultipass::Active(cfg))
        return BasicMultipass::Count(cfg.DlssNrBasicMultipass.value_or_default());
    return std::clamp(cfg.DlssNrPasses.value_or_default(), 1u, 10u);
}

// Consume the effective per-pass switches. Snapshot derivation has already
// applied Native Temporal's global visibility control to every child.
template<class C> bool AppliesModel(const C& cfg)
{
    const auto count = RequestedCount(cfg);
    if (!count) return false;
    if (cfg.DlssNrApplyModel.value_or_default()) return true;
    if (count > 1 && cfg.DlssNrSecondLayerApplyModel.value_or_default()) return true;
    for (unsigned int pass = 2; pass < count; ++pass)
        if (cfg.DlssNrExtraLayers[pass - 2].applyModel.value_or_default()) return true;
    return false;
}

// Native-style local delivery may retain a valid prefix. A pre-FG certificate
// must describe the whole requested chain, never a lifecycle-only call or stale output.
constexpr bool CanDeliver(unsigned int requested, unsigned int completed, bool requireComplete)
{
    return requested >= 1 && requested <= 10 && completed >= 1 && completed <= requested &&
        (!requireComplete || completed == requested);
}
}
