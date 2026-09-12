#pragma once
#include "../NrConfigState.h"
#include "DlssNr_PresentResolution.h"
#include <cmath>

// Semantic UI adapter only. The renderer continues consuming the 0.9.5 keys and math.
namespace DlssNr::StageUi
{
inline int DisplayPercent(float scale)
{
    return std::isfinite(scale) ? int(std::lround(std::clamp(scale, 0.25f, 2.0f) * 100.0f)) : 100;
}
inline constexpr const char* Stages[] = { "Before game upscaling", "After game upscaling" };
inline constexpr const char* Methods[] = { "Native Temporal", "Present (Compatibility)", "Present (Enhanced)" };
inline constexpr const char* NativeResolutions[] = { "Automatic", "Manual" };
inline constexpr const char* PresentPresets[] = {
    "Follow Game Render Resolution (Automatic)", "Full Output (100%)", "Ultra Quality (77%)",
    "Quality (67%)", "Balanced (58%)", "Performance (50%)", "Ultra Performance (33%)"
};
template<class C> int Stage(const C& c)
{
    return c.DlssNrRoute.value_or_default() == 0 && c.DlssNrRenderingMode.value_or_default() != 0 ? 0 : 1;
}
template<class C> bool Manual(const C& c)
{
    // A semantic hint never overrides an existing working scale, including legacy profiles.
    return c.DlssNrWorkingScale.value_or_default() != 1.0f || c.DlssNrUiManualResolution.value_or_default();
}
inline int Preset(PresentResolution::Policy p)
{
    return p.mode == PresentResolution::FollowNative ? 0 :
        p.mode == PresentResolution::FullOutput ? 1 : int((std::min)(p.scale, 5u)) + 1;
}
template<class C> void SelectStage(C& c, int stage)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    if (stage == Stage(c)) return;
    if (stage == 0)
    {
        c.DlssNrUiAfterMethod = c.DlssNrRoute.value_or_default();
        c.DlssNrRoute = 0u;
        NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr, 1);
    }
    else
    {
        c.DlssNrRoute = (std::min)(c.DlssNrUiAfterMethod.value_or_default(), 2u);
        NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr, 0);
    }
}
template<class C> void SelectMethod(C& c, int method)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    c.DlssNrRoute = uint32_t(std::clamp(method, 0, 2));
    c.DlssNrUiAfterMethod = c.DlssNrRoute.value_or_default();
    NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr, 0);
}
template<class C> void SelectManual(C& c, bool manual)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    if (!manual) c.DlssNrUiManualScale = c.DlssNrWorkingScale.value_or_default();
    c.DlssNrUiManualResolution = manual;
    c.DlssNrWorkingScale = manual ? c.DlssNrUiManualScale.value_or_default() : 1.0f;
}
template<class C> void SelectScale(C& c, float scale)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    c.DlssNrWorkingScale = std::clamp(scale, 0.25f, 2.0f);
    c.DlssNrUiManualScale = c.DlssNrWorkingScale.value_or_default();
    c.DlssNrUiManualResolution = true;
}
template<class C> void SelectPreset(C& c, int preset)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    auto& mode = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedResolution : c.DlssNrPresentResolution;
    auto& scale = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedCustomScale : c.DlssNrPresentCustomScale;
    preset = std::clamp(preset, 0, 6);
    mode = preset == 0 ? PresentResolution::FollowNative :
        preset == 1 ? PresentResolution::FullOutput : PresentResolution::Custom;
    if (preset > 1) scale = uint32_t(preset - 1);
}
template<class C> void LoadHints(C& c, std::optional<bool> manual, std::optional<float> scale,
                                  std::optional<uint32_t> afterMethod)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    c.DlssNrUiManualResolution.set_from_config(manual.value_or(false));
    const auto working = c.DlssNrWorkingScale.value_or_default();
    const auto remembered = scale && std::isfinite(*scale) && *scale >= 0.25f && *scale <= 2.0f ? *scale : 1.0f;
    c.DlssNrUiManualScale.set_from_config(working != 1.0f ? DisplayPercent(working) / 100.0f : remembered);
    c.DlssNrUiAfterMethod.set_from_config(Stage(c) == 1 ? c.DlssNrRoute.value_or_default() :
        afterMethod && *afterMethod <= 2 ? *afterMethod : 0u);
}
template<class Ini, class C> void SaveHints(Ini& ini, const C& c)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    ini.SetBoolValue("DlssNr", "UiManualResolution", Manual(c));
    ini.SetDoubleValue("DlssNr", "UiManualScale", c.DlssNrUiManualScale.value_or_default());
    ini.SetLongValue("DlssNr", "UiAfterMethod", c.DlssNrUiAfterMethod.value_or_default());
}
}
