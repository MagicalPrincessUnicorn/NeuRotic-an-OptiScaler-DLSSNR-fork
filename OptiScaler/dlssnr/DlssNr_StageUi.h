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
inline constexpr const char* Stages[] = { "Before", "After" };
inline constexpr const char* Methods[] = { "Native Temporal", "Present", "Present" };
inline constexpr const char* NativeResolutions[] = { "Automatic", "Manual" };
inline constexpr const char* Resolutions[] = { "Automatic", "Manual", "Legacy" };
enum class NrResolutionPreference { AlwaysFullOutput, MatchGameRender, Manual };
inline constexpr int ManualChoice = 2, LegacyChoice = 3;
inline constexpr const char* ResolutionChoices[] = {
    "Full output", "Match Native", "Manual"
};
inline constexpr const char* UnifiedMethods[] = {"Present", "Native", "NR Anything"};
// Existing NR percentages; these do not alter the game's DLSS SR selection.
inline constexpr int ResolutionPercentages[] = {100, 100, 67, 58, 50, 33};
template<class C> auto& ResolutionPresetHint(C& c)
{
    return c.DlssNrRoute.value_or_default() == 0 ? c.DlssNrUiResolutionPreset :
        c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrUiEnhancedResolutionPreset : c.DlssNrUiPresentResolutionPreset;
}
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
    c.DlssNrUiResolutionPreset = 0u;
    if (!manual && Manual(c)) c.DlssNrUiManualScale = c.DlssNrWorkingScale.value_or_default();
    c.DlssNrUiManualResolution = manual;
    c.DlssNrWorkingScale = manual ? c.DlssNrUiManualScale.value_or_default() : 1.0f;
}
template<class C> void SelectScale(C& c, float scale)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    c.DlssNrUiResolutionPreset = 0u;
    c.DlssNrWorkingScale = std::clamp(scale, 0.25f, 2.0f);
    c.DlssNrUiManualScale = c.DlssNrWorkingScale.value_or_default();
    c.DlssNrUiManualResolution = true;
}
template<class C> void SelectPreset(C& c, int preset)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    ResolutionPresetHint(c) = 0u;
    auto& mode = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedResolution : c.DlssNrPresentResolution;
    auto& scale = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedCustomScale : c.DlssNrPresentCustomScale;
    preset = std::clamp(preset, 0, 6);
    mode = preset == 0 ? PresentResolution::FollowNative :
        preset == 1 ? PresentResolution::FullOutput : PresentResolution::Custom;
    if (preset > 1) scale = uint32_t(preset - 1);
}
template<class C> int ResolutionSelection(const C& c)
{
    if (c.DlssNrRoute.value_or_default() == 0) return Manual(c) ? 1 : 0;
    const auto p = PresentResolution::Selected(c);
    return p.mode == PresentResolution::FollowNative ? 2 :
        p.mode == PresentResolution::Custom || p.mode == PresentResolution::Manual ? 1 : 0;
}
template<class C> float ResolutionScale(const C& c)
{
    if (c.DlssNrRoute.value_or_default() == 0) return c.DlssNrWorkingScale.value_or_default();
    return ResolutionSelection(c) == 1 ? PresentResolution::ManualPercent(PresentResolution::Selected(c)) / 100.0f : 1.0f;
}
template<class C> void SelectResolution(C& c, int selection)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    ResolutionPresetHint(c) = 0u;
    if (c.DlssNrRoute.value_or_default() == 0) { SelectManual(c, selection == 1); return; }
    const auto p = PresentResolution::Selected(c);
    auto& mode = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedResolution : c.DlssNrPresentResolution;
    auto& scale = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedCustomScale : c.DlssNrPresentCustomScale;
    scale = PresentResolution::ManualPercent(p);
    mode = selection == 1 ? PresentResolution::Manual : PresentResolution::Automatic;
}
template<class C> void SelectResolutionScale(C& c, float value)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    ResolutionPresetHint(c) = 0u;
    if (c.DlssNrRoute.value_or_default() == 0) { SelectScale(c, value); return; }
    auto& mode = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedResolution : c.DlssNrPresentResolution;
    auto& scale = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedCustomScale : c.DlssNrPresentCustomScale;
    scale = uint32_t(DisplayPercent(value));
    mode = PresentResolution::Manual;
}
template<class C> int ResolutionChoiceSelection(const C& c)
{
    const int selection = ResolutionSelection(c);
    if (selection == 1) return ManualChoice;
    if (selection == 2 || (c.DlssNrRoute.value_or_default() == 0 && Stage(c) == 0)) return 1;
    return 0;
}
template<class C> const char* ResolutionRefusal(const C& c, int choice)
{
    if (choice < 0 || choice > 2) return "Unknown NR resolution preference";
    if (c.DlssNrRoute.value_or_default() != 0) return nullptr;
    if (Stage(c) == 0 && choice == 0)
        return "Full output resolution requires an After route. Before placement is preserved.";
    if (Stage(c) == 1 && choice == 1)
        return "Match Game Render is unavailable for Native Temporal After. Select Before or a Present route explicitly.";
    return nullptr;
}
template<class C> const char* NativePlacementRefusal(const C& c, bool forcedAfter)
{
    return forcedAfter && c.DlssNrRoute.value_or_default() == 0 && Stage(c) == 0 && Manual(c)
        ? "Native Temporal is currently forced After reconstruction. Select After explicitly to edit or test its resolution."
        : nullptr;
}
template<class C> void SelectResolutionChoice(C& c, int choice)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    if (ResolutionRefusal(c, choice)) return;
    if (c.DlssNrRoute.value_or_default() == 0) { SelectManual(c, choice == ManualChoice); return; }
    const auto old = PresentResolution::Selected(c);
    auto& memory = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrUiEnhancedManualScale : c.DlssNrUiPresentManualScale;
    if (old.mode == PresentResolution::Manual || old.mode == PresentResolution::Custom)
        memory = PresentResolution::ManualPercent(old) / 100.0f;
    auto& mode = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedResolution : c.DlssNrPresentResolution;
    auto& scale = c.DlssNrRoute.value_or_default() == 2 ? c.DlssNrEnhancedCustomScale : c.DlssNrPresentCustomScale;
    mode = choice == 1 ? PresentResolution::FollowNative : choice == 0 ? PresentResolution::Automatic : PresentResolution::Manual;
    scale = uint32_t(DisplayPercent(memory.value_or_default()));
}
// The ordinary three-mode UI projects onto existing route/placement owners. Historical
// helpers above remain available to the archived Advisor and old-profile tests.
template<class C> int UnifiedMethodSelection(const C& c)
{
    return c.DlssNrRoute.value_or_default() == 3 ? 2 : c.DlssNrRoute.value_or_default() == 0 ? 1 : 0;
}
template<class C> void SelectUnifiedMethod(C& c, int method)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    if (method < 0 || method > 2) return;
    if (method == 2)
    {
        const auto current=c.DlssNrRoute.value_or_default();
        if(current==1||current==2)c.DlssNrUiAfterMethod=current;
        c.DlssNrRoute=3u;
        return;
    }
    if (method == 1)
    {
        if (c.DlssNrRoute.value_or_default() == 1 || c.DlssNrRoute.value_or_default() == 2)
            c.DlssNrUiAfterMethod = c.DlssNrRoute.value_or_default();
        c.DlssNrRoute = 0u;
        return;
    }
    const uint32_t current = c.DlssNrRoute.value_or_default();
    const uint32_t remembered = c.DlssNrUiAfterMethod.value_or_default();
    const uint32_t route = current == 1 || current == 2 ? current :
        remembered == 1 || remembered == 2 ? remembered : 2u;
    c.DlssNrRoute = route;
    c.DlssNrUiAfterMethod = route;
    c.DlssNrPresentInputPolicy = 2u;
}
template<class C> const char* UnifiedResolutionRefusal(const C&, int choice, bool)
{
    if (choice < 0 || choice > 2) return "Unknown NR resolution preference";
    // RR keeps After placement and resolves Match Native into its private raster.
    return nullptr;
}
template<class C> void SelectUnifiedResolution(C& c, int choice)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    if (choice < 0 || choice > 2) return;
    if (c.DlssNrRoute.value_or_default() != 0)
    {
        c.DlssNrPresentInputPolicy = 2u;
        SelectResolutionChoice(c, choice);
        return;
    }
    if (Manual(c) && choice != ManualChoice)
        c.DlssNrUiManualScale = c.DlssNrWorkingScale.value_or_default();
    c.DlssNrUiResolutionPreset = 0u;
    c.DlssNrUiManualResolution = choice == ManualChoice;
    c.DlssNrWorkingScale = choice == ManualChoice ? c.DlssNrUiManualScale.value_or_default() : 1.0f;
    NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr,
        choice == 1 ? 1 : 0);
}
template<class C> void SelectUnifiedResolutionScale(C& c, float scale)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    SelectResolutionScale(c, scale);
    if (c.DlssNrRoute.value_or_default() == 0)
        NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr, 0);
    else
        c.DlssNrPresentInputPolicy = 2u;
}
template<class C, class Read> void LoadResolutionPresets(C& c, Read read)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    const auto load = [&](auto& field, const char* key) {
        const auto value = read(key).value_or(0u);
        field.set_from_config(value < ManualChoice ? value : 0u);
    };
    load(c.DlssNrUiResolutionPreset, "UiResolutionPreset");
    load(c.DlssNrUiPresentResolutionPreset, "UiPresentResolutionPreset");
    load(c.DlssNrUiEnhancedResolutionPreset, "UiEnhancedResolutionPreset");
}
// Unknown future/corrupt routes must not authorize the external capture worker.
template<class C> void LoadRoute(C& c, std::optional<uint32_t> saved)
{
    if(saved)c.DlssNrRoute.set_from_config(*saved<=3?*saved:2u);
    else c.DlssNrRoute.reset();
}
template<class C> void LoadHints(C& c, std::optional<bool> manual, std::optional<float> scale,
                                  std::optional<uint32_t> afterMethod)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    c.DlssNrUiManualResolution.set_from_config(manual.value_or(false));
    const auto working = c.DlssNrWorkingScale.value_or_default();
    const auto remembered = scale && std::isfinite(*scale) && *scale >= 0.25f && *scale <= 2.0f ? *scale : 0.25f;
    c.DlssNrUiManualScale.set_from_config(working != 1.0f || manual.value_or(false) ? DisplayPercent(working) / 100.0f : remembered);
    c.DlssNrUiAfterMethod.set_from_config(Stage(c) == 1 && c.DlssNrRoute.value_or_default()!=3 ? c.DlssNrRoute.value_or_default() :
        afterMethod && *afterMethod <= 2 ? *afterMethod : 0u);
}
template<class Ini, class C> void SaveHints(Ini& ini, const C& c)
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    ini.SetBoolValue("DlssNr", "UiManualResolution", Manual(c));
    ini.SetDoubleValue("DlssNr", "UiManualScale", c.DlssNrUiManualScale.value_or_default());
    ini.SetLongValue("DlssNr", "UiAfterMethod", c.DlssNrUiAfterMethod.value_or_default());
    ini.SetDoubleValue("DlssNr", "UiPresentManualScale", c.DlssNrUiPresentManualScale.value_or_default());
    ini.SetDoubleValue("DlssNr", "UiEnhancedManualScale", c.DlssNrUiEnhancedManualScale.value_or_default());
    ini.SetLongValue("DlssNr", "UiResolutionPreset", c.DlssNrUiResolutionPreset.value_or_default());
    ini.SetLongValue("DlssNr", "UiPresentResolutionPreset", c.DlssNrUiPresentResolutionPreset.value_or_default());
    ini.SetLongValue("DlssNr", "UiEnhancedResolutionPreset", c.DlssNrUiEnhancedResolutionPreset.value_or_default());
}
}
