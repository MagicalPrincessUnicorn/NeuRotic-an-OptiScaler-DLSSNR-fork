#pragma once
#include "../OptiScaler/NrConfigSnapshot.h"
#include "../OptiScaler/dlssnr/AutomaticSetupRequirements.h"
#include <array>

// CPU-only source with the production NR field types/defaults. Production Config does not
// need Windows/NGX/ImGui initialization to test the actual wrapper and snapshot template.
constexpr int UnboundKey = -1;
enum class Scaler : uint32_t { Lanczos3 = 4 };
struct TestConfig
{
    NrOptional<int> DlssNrColorSource { 0 };
    NrOptional<int> DlssNrDepthSource { 0 };
    NrOptional<int> DlssNrMotionSource { 0 };
    NrOptional<bool> DlssNrSignalStrict { false };
    struct Layer
    {
        CustomOptional<float> workingScale {1.0f}, intensity {1.0f}, localStructure {1.0f}, localTone {1.0f},
            skinStructure {-1.0f}, transferStrength {1.0f}, colourStrength {1.0f}, maxRatio {2.0f};
        CustomOptional<Scaler> scalingDownscaler {Scaler::Lanczos3};
        CustomOptional<uint32_t> transfer {1}, preset {0}, style {0}, reversibleMode {0};
        CustomOptional<bool> autoMask {true}, applyModel {true};
    };
    struct ExtraLayers
    {
        std::array<Layer, 8> values;
        auto CopyForSnapshot(const NrConfigSynchronization::Transaction&) const { return values; }
    };
    NrConfigSnapshot<TestConfig> GetDlssNrConfigSnapshot() const;
    NrOptional<bool> DlssNrEnabled { false };
    NrOptional<bool> DlssNrVulkanPrepare { true };
    NrOptional<bool> DlssNrVulkanPresentSameFrameInit { false };
    NrOptional<bool> DlssNrAlternateFrame { false };
    NrOptional<bool> DlssNrMultipassEnabled { false };
    NrOptional<bool> DlssNrExperimentalMode { false };
    NrOptional<bool> DlssNrPreparedDepth { false };
    NrOptional<int> DlssNrNativeDepthDirection { -1 };
    NrOptional<int> DlssNrInputSource { DlssNr::SetupRequirements::InputSource };
    NrOptional<int> DlssNrInputTransport { DlssNr::SetupRequirements::InputTransport };
    NrOptional<bool> DlssNrAllowCpuFallback { DlssNr::SetupRequirements::AllowCpuFallback };
    NrOptional<bool> DlssNrNativeFrameGeneration { false };
    NrOptional<bool> DlssNrNativeVulkanRenderer { false };
    NrOptional<bool> DlssNrNativeGuides { false };
    NrOptional<bool> DlssNrOverrideMultipassGuardrails { false };
    NrOptional<bool> DlssNrOverrideHdrGuardrails { false };
    NrOptional<bool> DlssNrOverrideFgGuardrails { false };
    NrOptional<DlssNr::BasicMultipass::Profile> DlssNrBasicMultipass { {} };
    NrOptional<bool> DlssNrSecondLayer { false };
    NrOptional<float> DlssNrSecondLayerWorkingScale { 1.0f };
    NrOptional<Scaler> DlssNrSecondLayerScalingDownscaler { Scaler::Lanczos3 };
    NrOptional<uint32_t> DlssNrSecondLayerTransfer { 1 };
    NrOptional<uint32_t> DlssNrSecondLayerPreset { 0 };
    NrOptional<float> DlssNrSecondLayerIntensity { 1.0f };
    NrOptional<uint32_t> DlssNrSecondLayerStyle { 0 };
    NrOptional<float> DlssNrSecondLayerLocalStructure { 1.0f };
    NrOptional<float> DlssNrSecondLayerLocalTone { 1.0f };
    NrOptional<float> DlssNrSecondLayerSkinStructure { -1.0f };
    NrOptional<bool> DlssNrSecondLayerAutoMask { true };
    NrOptional<float> DlssNrSecondLayerTransferStrength { 1.0f };
    NrOptional<float> DlssNrSecondLayerColourStrength { 1.0f };
    NrOptional<float> DlssNrSecondLayerMaxRatio { 2.0f };
    NrOptional<uint32_t> DlssNrSecondLayerReversibleMode { 0 };
    NrOptional<bool> DlssNrSecondLayerApplyModel { true };
    ExtraLayers DlssNrExtraLayers;
    NrOptional<uint32_t> DlssNrRoute { DlssNr::SetupRequirements::Route };
    NrOptional<uint32_t> DlssNrAnythingScale { 100u };
    NrOptional<uint32_t> DlssNrUiAnythingResolutionPreset { 0u };
    NrOptional<uint32_t> DlssNrUiAnythingManualScale { 75u };
    NrOptional<uint32_t> DlssNrPresentInputPolicy { DlssNr::SetupRequirements::PresentInputPolicy };
    NrOptional<bool> DlssNrNativeProtocol { false };
    NrOptional<bool> DlssNrUiManualResolution { false };
    NrOptional<uint32_t> DlssNrNativeResolution {0};
    NrOptional<float> DlssNrUiManualScale { 0.25f };
    NrOptional<float> DlssNrUiPresentManualScale { 0.25f };
    NrOptional<float> DlssNrUiEnhancedManualScale { 0.25f };
    NrOptional<uint32_t> DlssNrUiResolutionPreset { 0 };
    NrOptional<uint32_t> DlssNrUiPresentResolutionPreset { 0 };
    NrOptional<uint32_t> DlssNrUiEnhancedResolutionPreset { 0 };
    NrOptional<uint32_t> DlssNrUiAfterMethod { 0 };
    NrOptional<uint32_t> DlssNrPresentResolution { 0 };
    NrOptional<uint32_t> DlssNrPresentCustomScale { 0 };
    NrOptional<uint32_t> DlssNrEnhancedResolution { 0 };
    NrOptional<uint32_t> DlssNrEnhancedCustomScale { 0 };
    NrOptional<bool> DlssNrRunBeforeSr { false }; // experimental: run NR before DLSS SR
    NrOptional<int32_t> DlssNrRenderingMode { 1 };
    NrOptional<bool> DlssNrPreDlaa { false }; // v10: private native-resolution DLAA resolve before NR, then re-jitter before SR
    NrOptional<bool> DlssNrPreSrSoftReset { false };
    NrOptional<int> DlssNrToggleKey { UnboundKey };
    NrOptional<uint32_t> DlssNrPreset { 0 };
    NrOptional<float> DlssNrIntensity { 1.0f };
    NrOptional<uint32_t> DlssNrStyle { DlssNr::SetupRequirements::Style };
    NrOptional<float> DlssNrLocalStructure { 1.0f };
    NrOptional<float> DlssNrLocalTone { 1.0f };
    NrOptional<float> DlssNrSkinStructure { -1.0f };
    NrOptional<bool> DlssNrAutoMask { true };
    NrOptional<float> DlssNrTransferStrength { 1.0f };
    NrOptional<float> DlssNrColourStrength { 1.0f };
    NrOptional<uint32_t> DlssNrReversibleMode { 0 };
    NrOptional<bool> DlssNrApplyModel { true };
    NrOptional<bool> DlssNrHoldFrame { false };
    NrOptional<float> DlssNrMaxRatio { 2.0f };
    NrOptional<uint32_t> DlssNrTransfer { 1 };
    NrOptional<bool> DlssNrWhitePointFromExposure { true };
    NrOptional<bool> DlssNrProbeD3D11 { false };
    NrOptional<uint32_t> DlssNrDebugView { 0 };
    NrOptional<uint32_t> DlssNrCompare { 0 };
    NrOptional<float> DlssNrCompareSplit { 0.5f };
    NrOptional<float> DlssNrCompareZoom { 1.0f };
    NrOptional<bool> DlssNrCompareSwap { false };
    NrOptional<bool> DlssNrCompareTags { false };
    NrOptional<float> DlssNrTagScale { 1.5f };
    NrOptional<float> DlssNrWorkingScale { 1.0f };
    NrOptional<Scaler> DlssNrScalingDownscaler { Scaler::Lanczos3 };
    NrOptional<bool> DlssNrProxyProbe { false };
    NrOptional<bool> DlssNrUseProxy { false };
    NrOptional<bool> DlssNrScanExposure { false };
    NrOptional<uint32_t> DlssNrWhitePointSource { 1 };
    NrOptional<bool> DlssNrScanMeter { false };
    NrOptional<float> DlssNrScanAnchorValue { 0.0f };       // legacy single anchor, migrated then unused
    NrOptional<float> DlssNrScanAnchorWhitePoint { 0.0f };  // legacy single anchor, migrated then unused
    NrOptional<std::string> DlssNrScanAnchors { std::string() };
    NrOptional<bool> DlssNrScanInverted { false };
    NrOptional<float> DlssNrWhitePointTrim { 1.0f };
    NrOptional<float> DlssNrScanTrim { 1.0f };
    NrOptional<uint32_t> DlssNrPasses { 1 };
    NrOptional<bool> DlssNrAutoCapture { true };
    NrOptional<float> DlssNrWhitePointScale { 1.0f };
    NrConfigState state;
    void SetDlssNrEnabled(bool enabled) { state.SetEnabled(DlssNrEnabled,enabled); }
    NrConfigState::RuntimeSnapshot GetDlssNrRuntimeSnapshot() const noexcept { return state.Snapshot(); }
};

inline NrConfigSnapshot<TestConfig> TestConfig::GetDlssNrConfigSnapshot() const
{
    return NrConfigSnapshot<TestConfig>(*this);
}
