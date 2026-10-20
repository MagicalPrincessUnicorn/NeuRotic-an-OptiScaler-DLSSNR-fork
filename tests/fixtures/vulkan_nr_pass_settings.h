#pragma once
#include <array>
#include "../../OptiScaler/dlssnr/DlssNr_BasicMultipass.h"
template<class T> struct VkPassValue { T value;T value_or_default() const{return value;} VkPassValue& operator=(T x){value=x;return *this;} };
struct VkPassLayer {
    VkPassValue<float> workingScale{1};
    VkPassValue<uint32_t> scalingDownscaler{4};
    VkPassValue<uint32_t> transfer{1};
    VkPassValue<uint32_t> preset{0};
    VkPassValue<float> intensity{1};
    VkPassValue<uint32_t> style{0};
    VkPassValue<float> localStructure{1};
    VkPassValue<float> localTone{1};
    VkPassValue<float> skinStructure{-1};
    VkPassValue<bool> autoMask{true};
    VkPassValue<float> transferStrength{1};
    VkPassValue<float> colourStrength{1};
    VkPassValue<float> maxRatio{2};
    VkPassValue<uint32_t> reversibleMode{0};
    VkPassValue<bool> applyModel{true};
};
struct VkPassTestSettings {
    VkPassValue<bool> DlssNrRunBeforeSr{false};
    VkPassValue<float> DlssNrWorkingScale{1},DlssNrSecondLayerWorkingScale{1};
    VkPassValue<uint32_t> DlssNrScalingDownscaler{4},DlssNrSecondLayerScalingDownscaler{4};
    VkPassValue<uint32_t> DlssNrTransfer{1},DlssNrSecondLayerTransfer{1};
    VkPassValue<uint32_t> DlssNrPreset{0},DlssNrSecondLayerPreset{0};
    VkPassValue<float> DlssNrIntensity{1},DlssNrSecondLayerIntensity{1};
    VkPassValue<uint32_t> DlssNrStyle{0},DlssNrSecondLayerStyle{0};
    VkPassValue<float> DlssNrLocalStructure{1},DlssNrSecondLayerLocalStructure{1};
    VkPassValue<float> DlssNrLocalTone{1},DlssNrSecondLayerLocalTone{1};
    VkPassValue<float> DlssNrSkinStructure{-1},DlssNrSecondLayerSkinStructure{-1};
    VkPassValue<bool> DlssNrAutoMask{true},DlssNrSecondLayerAutoMask{true};
    VkPassValue<float> DlssNrTransferStrength{1},DlssNrSecondLayerTransferStrength{1};
    VkPassValue<float> DlssNrColourStrength{1},DlssNrSecondLayerColourStrength{1};
    VkPassValue<float> DlssNrMaxRatio{2},DlssNrSecondLayerMaxRatio{2};
    VkPassValue<uint32_t> DlssNrReversibleMode{0},DlssNrSecondLayerReversibleMode{0};
    VkPassValue<bool> DlssNrApplyModel{true},DlssNrSecondLayerApplyModel{true};
    std::array<VkPassLayer,8> DlssNrExtraLayers{};
    VkPassValue<DlssNr::BasicMultipass::Profile> DlssNrBasicMultipass{{}};
    VkPassValue<bool> DlssNrMultipassEnabled{false},DlssNrSecondLayer{false},DlssNrUiManualResolution{false},DlssNrHoldFrame{false};
    VkPassValue<uint32_t> DlssNrPasses{1},DlssNrCompare{0},DlssNrDebugView{0};
};
