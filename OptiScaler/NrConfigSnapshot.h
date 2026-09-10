#pragma once

#include "NrConfigState.h"
#include <new>

// Keep every Config::DlssNr* option here. The result owns its storage, including strings.
// The explicit list also keeps unrelated config out of the render snapshot.
#define NR_CONFIG_SNAPSHOT_FIELDS(X) \
    X(DlssNrEnabled) \
    X(DlssNrSecondLayer) \
    X(DlssNrRoute) \
    X(DlssNrPresentWorkload) \
    X(DlssNrRunBeforeSr) \
    X(DlssNrRenderingMode) \
    X(DlssNrPreDlaa) \
    X(DlssNrToggleKey) \
    X(DlssNrPreset) \
    X(DlssNrIntensity) \
    X(DlssNrStyle) \
    X(DlssNrLocalStructure) \
    X(DlssNrLocalTone) \
    X(DlssNrSkinStructure) \
    X(DlssNrAutoMask) \
    X(DlssNrTransferStrength) \
    X(DlssNrColourStrength) \
    X(DlssNrReversibleMode) \
    X(DlssNrApplyModel) \
    X(DlssNrHoldFrame) \
    X(DlssNrMaxRatio) \
    X(DlssNrTransfer) \
    X(DlssNrWhitePointFromExposure) \
    X(DlssNrProbeD3D11) \
    X(DlssNrDebugView) \
    X(DlssNrCompare) \
    X(DlssNrCompareSplit) \
    X(DlssNrCompareZoom) \
    X(DlssNrCompareSwap) \
    X(DlssNrCompareTags) \
    X(DlssNrTagScale) \
    X(DlssNrWorkingScale) \
    X(DlssNrScalingDownscaler) \
    X(DlssNrProxyProbe) \
    X(DlssNrUseProxy) \
    X(DlssNrScanExposure) \
    X(DlssNrWhitePointSource) \
    X(DlssNrScanMeter) \
    X(DlssNrScanAnchorValue) \
    X(DlssNrScanAnchorWhitePoint) \
    X(DlssNrScanAnchors) \
    X(DlssNrScanInverted) \
    X(DlssNrWhitePointTrim) \
    X(DlssNrScanTrim) \
    X(DlssNrPasses) \
    X(DlssNrAutoCapture) \
    X(DlssNrWhitePointScale)

// Source is Config in production; a CPU-only source can exercise the exact capture in tests.
template <class Source> struct NrConfigSnapshot
{
#define NR_DECLARE_SNAPSHOT(name) \
    decltype(std::declval<const Source&>().name.CopyForSnapshot( \
        std::declval<const NrConfigSynchronization::Transaction&>())) name;
    NR_CONFIG_SNAPSHOT_FIELDS(NR_DECLARE_SNAPSHOT)
#undef NR_DECLARE_SNAPSHOT

    NrConfigState::RuntimeSnapshot GetDlssNrRuntimeSnapshot() const noexcept { return _runtime; }

    explicit NrConfigSnapshot(const Source& source)
        : NrConfigSnapshot(source, NrConfigSynchronization::Transaction {}) {}

  private:
    NrConfigState::RuntimeSnapshot _runtime;

    // The temporary transaction survives all member initializers, then unlocks before return.
    NrConfigSnapshot(const Source& source, const NrConfigSynchronization::Transaction& transaction)
        :
#define NR_COPY_SNAPSHOT(name) name(source.name.CopyForSnapshot(transaction)),
          NR_CONFIG_SNAPSHOT_FIELDS(NR_COPY_SNAPSHOT)
#undef NR_COPY_SNAPSHOT
          _runtime(source.GetDlssNrRuntimeSnapshot()) {}
};

#undef NR_CONFIG_SNAPSHOT_FIELDS

// Allocation failure while copying an anchor string must bypass NR at the host boundary,
// not unwind through the game's rendering callback. The transaction unlocks before this returns.
template<class Source> std::optional<NrConfigSnapshot<Source>> TryNrConfigSnapshot(const Source& source)
{
    try { return std::optional<NrConfigSnapshot<Source>>(std::in_place, source); }
    catch (const std::bad_alloc&) { return std::nullopt; }
}
