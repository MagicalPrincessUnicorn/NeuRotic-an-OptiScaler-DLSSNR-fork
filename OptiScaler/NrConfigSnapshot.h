#pragma once

#include "NrConfigState.h"
#include "dlssnr/DlssNr_BasicMultipass.h"
#include <new>
#include <sstream>
#include <iomanip>
#include <locale>
#include <type_traits>

template<class T> void NrDescribeValue(std::ostream& out, const T& value)
{
    if constexpr (requires { value.value_or_default(); }) NrDescribeValue(out, value.value_or_default());
    else if constexpr (requires { value.has_value(); value.value(); })
    {
        if (value.has_value()) NrDescribeValue(out, value.value());
        else out << "unset";
    }
    else if constexpr (requires { out << value; }) out << value;
    else if constexpr (std::is_enum_v<T>) out << static_cast<std::underlying_type_t<T>>(value);
    else if constexpr (requires { value.advanced; value.maximum; })
    {
        out << "{advanced=" << value.advanced << ";maximum=" << value.maximum
            << ";resolution=" << value.resolution << ";downscaler=" << value.downscaler
            << ";model=" << value.model << ";detail=" << value.detail << '}';
    }
    else if constexpr (requires { value.workingScale; })
    {
        out << '{';
#define NR_DESCRIBE_LAYER(name) out << #name << '='; NrDescribeValue(out, value.name); out << ';';
        NR_DESCRIBE_LAYER(workingScale) NR_DESCRIBE_LAYER(scalingDownscaler)
        NR_DESCRIBE_LAYER(transfer) NR_DESCRIBE_LAYER(preset) NR_DESCRIBE_LAYER(intensity)
        NR_DESCRIBE_LAYER(style) NR_DESCRIBE_LAYER(localStructure) NR_DESCRIBE_LAYER(localTone)
        NR_DESCRIBE_LAYER(skinStructure) NR_DESCRIBE_LAYER(autoMask) NR_DESCRIBE_LAYER(transferStrength)
        NR_DESCRIBE_LAYER(colourStrength) NR_DESCRIBE_LAYER(maxRatio) NR_DESCRIBE_LAYER(reversibleMode)
        NR_DESCRIBE_LAYER(applyModel)
#undef NR_DESCRIBE_LAYER
        out << '}';
    }
    else
    {
        out << '[';
        for (const auto& entry : value) { NrDescribeValue(out, entry); out << ';'; }
        out << ']';
    }
}

// Compare effective immutable render values without changing CustomOptional semantics.
template<class T> bool NrSnapshotEqual(const T& a, const T& b)
{
    if constexpr (requires { a.value_or_default(); }) return a.value_or_default() == b.value_or_default();
    else if constexpr (requires { a.has_value(); a.value(); })
        return a.has_value() == b.has_value() && (!a.has_value() || a.value() == b.value());
    else if constexpr (requires { a.workingScale; })
    {
#define NR_COMPARE_LAYER(name) if (!NrSnapshotEqual(a.name, b.name)) return false;
        NR_COMPARE_LAYER(workingScale) NR_COMPARE_LAYER(scalingDownscaler)
        NR_COMPARE_LAYER(transfer) NR_COMPARE_LAYER(preset) NR_COMPARE_LAYER(intensity)
        NR_COMPARE_LAYER(style) NR_COMPARE_LAYER(localStructure) NR_COMPARE_LAYER(localTone)
        NR_COMPARE_LAYER(skinStructure) NR_COMPARE_LAYER(autoMask) NR_COMPARE_LAYER(transferStrength)
        NR_COMPARE_LAYER(colourStrength) NR_COMPARE_LAYER(maxRatio) NR_COMPARE_LAYER(reversibleMode)
        NR_COMPARE_LAYER(applyModel)
#undef NR_COMPARE_LAYER
        return true;
    }
    else
    {
        for (size_t i = 0; i < a.size(); ++i) if (!NrSnapshotEqual(a[i], b[i])) return false;
        return true;
    }
}

// Keep every Config::DlssNr* option here. The result owns its storage, including strings.
// The explicit list also keeps unrelated config out of the render snapshot.
#define NR_CONFIG_SNAPSHOT_FIELDS(X) \
    X(DlssNrEnabled) \
    X(DlssNrMultipassEnabled) \
    X(DlssNrExperimentalMode) \
    X(DlssNrOverrideMultipassGuardrails) \
    X(DlssNrOverrideHdrGuardrails) \
    X(DlssNrOverrideFgGuardrails) \
    X(DlssNrBasicMultipass) \
    X(DlssNrSecondLayer) \
    X(DlssNrSecondLayerWorkingScale) \
    X(DlssNrSecondLayerScalingDownscaler) \
    X(DlssNrSecondLayerTransfer) \
    X(DlssNrSecondLayerPreset) \
    X(DlssNrSecondLayerIntensity) \
    X(DlssNrSecondLayerStyle) \
    X(DlssNrSecondLayerLocalStructure) \
    X(DlssNrSecondLayerLocalTone) \
    X(DlssNrSecondLayerSkinStructure) \
    X(DlssNrSecondLayerAutoMask) \
    X(DlssNrSecondLayerTransferStrength) \
    X(DlssNrSecondLayerColourStrength) \
    X(DlssNrSecondLayerMaxRatio) \
    X(DlssNrSecondLayerReversibleMode) \
    X(DlssNrSecondLayerApplyModel) \
    X(DlssNrExtraLayers) \
    X(DlssNrRoute) \
    X(DlssNrUiManualResolution) \
    X(DlssNrUiManualScale) \
    X(DlssNrUiPresentManualScale) \
    X(DlssNrUiEnhancedManualScale) \
    X(DlssNrUiResolutionPreset) \
    X(DlssNrUiPresentResolutionPreset) \
    X(DlssNrUiEnhancedResolutionPreset) \
    X(DlssNrUiAfterMethod) \
    X(DlssNrPresentResolution) \
    X(DlssNrPresentCustomScale) \
    X(DlssNrEnhancedResolution) \
    X(DlssNrEnhancedCustomScale) \
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

    std::string Describe() const
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9) << "runtime.enabled=" << _runtime.enabled
            << ";runtime.resumeGeneration=" << _runtime.resumeGeneration << '\n';
#define NR_DESCRIBE_SNAPSHOT(name) out << #name << '='; NrDescribeValue(out, name); out << '\n';
        NR_CONFIG_SNAPSHOT_FIELDS(NR_DESCRIBE_SNAPSHOT)
#undef NR_DESCRIBE_SNAPSHOT
        return out.str();
    }

    bool SameConfiguration(const NrConfigSnapshot& other) const
    {
        if (_runtime.enabled != other._runtime.enabled || _runtime.resumeGeneration != other._runtime.resumeGeneration)
            return false;
#define NR_COMPARE_SNAPSHOT(name) if (!NrSnapshotEqual(name, other.name)) return false;
        NR_CONFIG_SNAPSHOT_FIELDS(NR_COMPARE_SNAPSHOT)
#undef NR_COMPARE_SNAPSHOT
        return true;
    }

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
template<class Source> std::optional<NrConfigSnapshot<Source>> TryNrConfigSnapshot(const Source& source,
                                                                               bool deriveMultipass = true)
{
    try
    {
        std::optional<NrConfigSnapshot<Source>> result(std::in_place, source);
        DlssNr::BasicMultipass::Derive(*result, deriveMultipass);
        return result;
    }
    catch (const std::bad_alloc&) { return std::nullopt; }
}
