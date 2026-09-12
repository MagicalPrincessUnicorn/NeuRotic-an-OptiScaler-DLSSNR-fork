#pragma once
#include "../SynchronizedOptional.h"
#include <algorithm>
#include <cmath>

namespace DlssNr::BasicMultipass
{
struct Profile
{
    bool advanced = false;
    uint32_t maximum = 1;
    float resolution = 1.0f;
    uint32_t downscaler = 4;
    float model = 1.0f;
    float detail = 1.0f;
    bool operator==(const Profile&) const = default;
};
inline Profile Normalize(Profile p)
{
    p.maximum = std::clamp(p.maximum, 1u, 10u);
    const auto bounded = [](float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : 1.0f; };
    p.resolution = bounded(p.resolution, 0.25f, 2.0f);
    p.downscaler = (std::min)(p.downscaler, 7u);
    p.model = bounded(p.model, 0.0f, float(p.maximum));
    p.detail = bounded(p.detail, 0.0f, float(p.maximum));
    return p;
}
inline float Strength(float total, uint32_t pass) { return std::clamp(total - float(pass), 0.0f, 1.0f); }
inline uint32_t Count(Profile p)
{
    p = Normalize(p);
    return uint32_t(std::ceil((std::max)(p.model, p.detail)));
}
template<class C> bool Active(const C& c)
{
    return c.DlssNrMultipassEnabled.value_or_default() && !c.DlssNrBasicMultipass.value_or_default().advanced;
}
template<class C, class Edit> void Update(C& c, Edit edit)
{
    NrConfigSynchronization::Transaction transaction;
    auto p = c.DlssNrBasicMultipass.value_or_default();
    edit(p);
    c.DlssNrBasicMultipass = Normalize(p);
}
template<class Reader> Profile Load(Reader read, bool existingMultipass)
{
    Profile p;
    const auto number = [&](const char* key, float fallback) {
        const float v = read(key).value_or(fallback);
        return std::isfinite(v) ? v : fallback;
    };
    p.advanced = number("Advanced", existingMultipass ? 1.0f : 0.0f) != 0;
    p.maximum = uint32_t(std::clamp(number("MaximumPasses", 1.0f), 1.0f, 10.0f));
    p.resolution = read("Resolution").value_or(1.0f);
    p.downscaler = uint32_t(std::clamp(number("Downscaler", 4.0f), 0.0f, 7.0f));
    p.model = read("ModelStrength").value_or(1.0f);
    p.detail = read("DetailStrength").value_or(1.0f);
    return Normalize(p);
}
template<class Ini> void Save(Ini& ini, Profile p)
{
    p = Normalize(p);
    ini.SetLongValue("DlssNrBasic", "Advanced", p.advanced ? 1 : 0);
    ini.SetLongValue("DlssNrBasic", "MaximumPasses", p.maximum);
    ini.SetDoubleValue("DlssNrBasic", "Resolution", p.resolution);
    ini.SetLongValue("DlssNrBasic", "Downscaler", p.downscaler);
    ini.SetDoubleValue("DlssNrBasic", "ModelStrength", p.model);
    ini.SetDoubleValue("DlssNrBasic", "DetailStrength", p.detail);
}

// Operates exclusively on an owned render snapshot, never on Config or stored profiles.
template<class S> void Derive(S& c, bool basicSupported = true)
{
    if (basicSupported && Active(c))
    {
        const auto p = Normalize(c.DlssNrBasicMultipass.value_or_default());
        c.DlssNrPasses = Count(p);
        c.DlssNrSecondLayer = Count(p) > 1;
        c.DlssNrWorkingScale = p.resolution;
        c.DlssNrUiManualResolution = true;
        using ScalerType = decltype(c.DlssNrScalingDownscaler.value_or_default());
        c.DlssNrScalingDownscaler = ScalerType(p.downscaler);
        // First copy all inherited tuning, then distribute the two independent totals.
        c.DlssNrSecondLayerWorkingScale = p.resolution;
        c.DlssNrSecondLayerScalingDownscaler = ScalerType(p.downscaler);
        c.DlssNrSecondLayerPreset = c.DlssNrPreset.value_or_default();
        c.DlssNrSecondLayerStyle = c.DlssNrStyle.value_or_default();
        c.DlssNrSecondLayerLocalStructure = c.DlssNrLocalStructure.value_or_default();
        c.DlssNrSecondLayerLocalTone = c.DlssNrLocalTone.value_or_default();
        c.DlssNrSecondLayerSkinStructure = c.DlssNrSkinStructure.value_or_default();
        c.DlssNrSecondLayerAutoMask = c.DlssNrAutoMask.value_or_default();
        c.DlssNrSecondLayerTransfer = c.DlssNrTransfer.value_or_default();
        c.DlssNrSecondLayerColourStrength = c.DlssNrColourStrength.value_or_default();
        c.DlssNrSecondLayerMaxRatio = c.DlssNrMaxRatio.value_or_default();
        c.DlssNrSecondLayerReversibleMode = c.DlssNrReversibleMode.value_or_default();
        c.DlssNrSecondLayerApplyModel = c.DlssNrApplyModel.value_or_default();
        c.DlssNrSecondLayerIntensity = Strength(p.model, 1);
        c.DlssNrSecondLayerTransferStrength = Strength(p.detail, 1);
        for (uint32_t i = 0; i < 8; ++i)
        {
            auto& layer = c.DlssNrExtraLayers[i];
            layer.workingScale = p.resolution;
            layer.scalingDownscaler = ScalerType(p.downscaler);
            layer.preset = c.DlssNrPreset.value_or_default();
            layer.style = c.DlssNrStyle.value_or_default();
            layer.localStructure = c.DlssNrLocalStructure.value_or_default();
            layer.localTone = c.DlssNrLocalTone.value_or_default();
            layer.skinStructure = c.DlssNrSkinStructure.value_or_default();
            layer.autoMask = c.DlssNrAutoMask.value_or_default();
            layer.transfer = c.DlssNrTransfer.value_or_default();
            layer.colourStrength = c.DlssNrColourStrength.value_or_default();
            layer.maxRatio = c.DlssNrMaxRatio.value_or_default();
            layer.reversibleMode = c.DlssNrReversibleMode.value_or_default();
            layer.applyModel = c.DlssNrApplyModel.value_or_default();
            layer.intensity = Strength(p.model, i + 2);
            layer.transferStrength = Strength(p.detail, i + 2);
        }
        c.DlssNrIntensity = Strength(p.model, 0);
        c.DlssNrTransferStrength = Strength(p.detail, 0);
    }
    // Global effect visibility is above every Advanced per-pass switch.
    if (!c.DlssNrApplyModel.value_or_default())
    {
        c.DlssNrSecondLayerApplyModel = false;
        for (auto& layer : c.DlssNrExtraLayers) layer.applyModel = false;
        c.DlssNrCompare = 0u;
        c.DlssNrDebugView = 0u;
        c.DlssNrHoldFrame = false;
    }
}
}
