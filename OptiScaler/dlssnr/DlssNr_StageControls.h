#pragma once
#include "DlssNr_StageUi.h"
#include "DlssNr_MenuStatus.h"
#include "DlssNr_MenuControls.h"
#include "NrStatusPanel.h"
#include <menu/Localization.h>
#include <imgui/imgui.h>
#include <cstdio>

namespace DlssNr::StageUi
{
inline std::string DimensionText(uint32_t workW, uint32_t workH, uint32_t outputW, uint32_t outputH)
{
    char text[256] {};
    if (workW && workH && outputW && outputH)
        std::snprintf(text, sizeof(text), Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_u_x_u_output_u_x_u_bb716d99", "NR: %u x %u | Output: %u x %u")).c_str(), workW, workH, outputW, outputH);
    else if (workW && workH)
        std::snprintf(text, sizeof(text), Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_u_x_u_output_unavailable_65b83868", "NR: %u x %u | Output: unavailable")).c_str(), workW, workH);
    else if (outputW && outputH)
        std::snprintf(text, sizeof(text), Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_unavailable_output_u_x_u_000e3866", "NR: unavailable | Output: %u x %u")).c_str(), outputW, outputH);
    else
        return Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_unavailable_output_unavailable_92eece40", "NR: unavailable | Output: unavailable"));
    return text;
}
inline bool RenderRuntimeStatus(const std::optional<MenuStatus::RuntimeStatus>& status)
{
    if (!status) return false; // caller retains the existing generic status
    const auto& s = *status;
    const auto state = Neurotic::Translate(MenuStatus::StateNames[std::clamp(int(s.state), 0, 6)]);
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.status_s_82f12928", "Status: %s"),Neurotic::Translate(state.c_str()).c_str());
    if (s.provider[0]) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.provider_s_a69e3aef", "Provider: %.*s"), int(s.provider.size()), s.provider.data());
    if (s.route[0]) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.route_s_c6f82cce", "Route: %.*s"), int(s.route.size()), s.route.data());
    if (s.reason[0]) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.reason_s_079afb82", "Reason: %.*s"), int(s.reason.size()), s.reason.data());
    if (s.realFrame) ImGui::Text(Neurotic::UiLiteral("ingame.dlssnr-menu.real_frame_llu_cb1386c7", "Real frame: %llu"), static_cast<unsigned long long>(*s.realFrame));
    if (s.workWidth && s.workHeight && s.outputWidth && s.outputHeight)
        ImGui::Text(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_u_x_u_output_u_x_u_bb716d99", "NR: %u x %u | Output: %u x %u"), s.workWidth, s.workHeight, s.outputWidth, s.outputHeight);
    return true;
}
inline void RenderRuntimeSummary(const std::optional<MenuStatus::RuntimeStatus>& status)
{
    const auto muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const auto color = !status ? muted : status->state == MenuStatus::State::Active ?
        ImVec4(.43f,.84f,.64f,1) : ImVec4(1,.75f,.36f,1);
    StatusPanel::Line("##NrRuntimeSummary", status ?
        MenuStatus::StateNames[std::clamp(int(status->state), 0, 6)] : Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.status_unavailable_bf1b5390", "Status: unavailable"), color);
    StatusPanel::Linef("##NrRuntimeSummaryReason", muted, "%.*s", status ? int(status->reason.size()) : 0,
        status ? status->reason.data() : "");
    ImGui::Spacing();
}
inline void SentenceHelpMarker(const char*) {}

inline float ManualSliderWidth(float available, float resetAndHelpWidth, float spacing)
{
    return 0.5f * (std::max)(1.0f, available - resetAndHelpWidth - spacing * 3.0f);
}
inline float ResponsiveSliderWidth(float available, float menuScale, float resetWidth,
                                   float helpWidth, float spacing)
{
    const float reserved = resetWidth + helpWidth + spacing * 2.0f;
    return (std::max)(1.0f, (std::min)(440.0f * menuScale, available - reserved));
}

// Wrap at word boundaries when a sentence will not fit alongside its selector. Help stays beside
// the selector even when the prefix and suffix need their own lines.
inline bool ComboWithSavedPreview(const char* id, int* value, const char* const* items,
                                  int count, const char* previewOverride = nullptr)
{
    if (!previewOverride) return ImGui::Combo(id, value, items, count);
    bool changed = false;
    if (ImGui::BeginCombo(id, previewOverride))
    {
        for (int choice = 0; choice < count; ++choice)
        {
            if (ImGui::Selectable(items[choice], *value == choice))
            {
                *value = choice;
                changed = true;
            }
            if (*value == choice || (*value < 0 && choice == 0)) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

inline bool SentenceCombo(const char* id, const char* prefix, const char* suffix, int* value,
                          const char* const* items, int count, const char* help = nullptr,
                          float minimumWidth = 0.0f, bool selectorDisabled = false,
                          bool primary = false, bool trailingLabel = false,
                          const char* previewOverride = nullptr)
{
    float widest = 0;
    for (int i = 0; i < count; ++i) widest = (std::max)(widest, ImGui::CalcTextSize(items[i]).x);
    if (previewOverride) widest = (std::max)(widest, ImGui::CalcTextSize(previewOverride).x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float naturalWidth = widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
    const float width = (std::max)(naturalWidth, minimumWidth);
    const bool hasHelp = false;
    const float helpWidth = hasHelp ? ImGui::CalcTextSize("(?)").x : 0.0f;
    float required = width;
    if (*prefix) required += ImGui::CalcTextSize(prefix).x + spacing;
    if (hasHelp) required += helpWidth + spacing;
    if (*suffix) required += ImGui::CalcTextSize(suffix).x + spacing;
    const bool fits = ImGui::GetContentRegionAvail().x >= required;
    if (*prefix)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s",Neurotic::Translate(prefix).c_str());
        if (fits) ImGui::SameLine();
    }
    const float selectorRowWidth = ImGui::GetContentRegionAvail().x;
    const float wrappedWidth = hasHelp ?
        (std::max)(40.0f, selectorRowWidth - helpWidth - spacing) : -1.0f;
    ImGui::SetNextItemWidth(fits ? width : wrappedWidth);
    auto* controlDraw = ImGui::GetWindowDrawList();
    const ImVec2 controlMin = ImGui::GetCursorScreenPos();
    const ImVec2 controlMax(controlMin.x + (fits ? width :
        wrappedWidth > 0.0f ? wrappedWidth : selectorRowWidth),
        controlMin.y + ImGui::GetFrameHeight());
    if (selectorDisabled) ImGui::BeginDisabled();
    const bool changed = ComboWithSavedPreview(id, value, items, count, previewOverride);
    if (primary) MenuControls::HighlightItem(controlDraw, controlMin, controlMax,
        ImGui::IsItemFocused() || ImGui::IsItemActive());
    if (selectorDisabled) ImGui::EndDisabled();
    if (*suffix)
    {
        if (fits && trailingLabel) MenuControls::AlignTrailingLabel(suffix);
        else
        {
            if (fits) ImGui::SameLine();
            ImGui::TextWrapped("%s",Neurotic::Translate(suffix).c_str());
        }
    }
    if (hasHelp)
    {
        ImGui::SameLine();
        SentenceHelpMarker(help);
    }
    return changed;
}

// Shared by the production page and the headless ImGui navigation/layout fixture.
template<class C> bool RenderControls(C& config, bool basicOwnsResolution = false,
                                      bool nativeAfterOnly = false, bool showPlacementRefusal = true,
                                      bool stackedLabels = false)
{
    bool changed = false;
    auto snapshot = config.GetDlssNrConfigSnapshot();
    // Display choices map explicitly to existing method IDs. Route3 remains a
    // valid saved preference, but is unavailable as an in-game choice here.
    constexpr int methodChoices[] = {0, 1};
    const char* methodItems[] = {UnifiedMethods[methodChoices[0]], UnifiedMethods[methodChoices[1]]};
    const int savedMethod = UnifiedMethodSelection(snapshot);
    int method = savedMethod == methodChoices[0] ? 0 : savedMethod == methodChoices[1] ? 1 : -1;
    const char* savedPreview = method < 0 ?
        Neurotic::UiLiteral("ingame.nr-diagnostics.unavailable", "Unavailable") : nullptr;
    const char* methodHelp =
        Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.native_temporal_works_within_the_game_s_upscaler_1e91b62c", "Native Temporal works within the game's upscaler, usually before super resolution. "
        "Some setups place it after reconstruction. It is usually more stable during motion, "
        "but it can only change what the game and this stage allow.\n\n"
        "Present works on the final image after upscaling. It can add a stronger visual effect "
        "while remaining fast, but moving objects may flicker or ghost when the game does not "
        "provide usable motion information. It uses qualified depth and motion guides when available "
        "and image-only input otherwise.");
    bool methodChanged;
    if (stackedLabels)
    {
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_mode_c1e25296", "NR Mode"));
        ImGui::SetNextItemWidth(-1.0f);
        methodChanged = ComboWithSavedPreview("##NrMethod", &method, methodItems, 2, savedPreview);
    }
    else methodChanged = SentenceCombo("##NrMethod", "", Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_mode_c1e25296", "NR Mode"), &method, methodItems,
                      2, methodHelp, 0.0f, false, true, true, savedPreview);
    if (methodChanged)
    {
        NrConfigSynchronization::Transaction transaction;
        const auto inputPolicy = config.DlssNrPresentInputPolicy;
        method = methodChoices[method];
        SelectUnifiedMethod(config, method);
        config.DlssNrPresentInputPolicy = inputPolicy;
        changed = true;
    }
    snapshot = config.GetDlssNrConfigSnapshot();
    if(snapshot.DlssNrRoute.value_or_default()==3)return changed;
    int resolution = basicOwnsResolution ? ManualChoice : ResolutionChoiceSelection(snapshot);
    const auto* placementRefusal = NativePlacementRefusal(snapshot, nativeAfterOnly);
    ImGui::BeginDisabled(basicOwnsResolution);
    if (stackedLabels) ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_resolution_399e2689", "NR Resolution"));
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float labelWidth = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_resolution_399e2689", "NR Resolution")).x;
    const float helpWidth = ImGui::CalcTextSize("(?)").x;
    const float minimumComboWidth = ImGui::GetFontSize() * 7.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const bool sameRow = rowWidth >= minimumComboWidth + labelWidth + helpWidth + spacing * 2.0f;
    const float dropdownRoom = rowWidth - helpWidth - spacing -
        (sameRow ? labelWidth + spacing : 0.0f);
    ImGui::SetNextItemWidth((std::min)(ImGui::GetFontSize() * 14.0f,
        (std::max)(minimumComboWidth, dropdownRoom)));
    if (stackedLabels) ImGui::SetNextItemWidth(-1.0f);
    auto* controlDraw = ImGui::GetWindowDrawList();
    const ImVec2 controlMin = ImGui::GetCursorScreenPos();
    const ImVec2 controlMax(controlMin.x + ImGui::CalcItemWidth(),
                            controlMin.y + ImGui::GetFrameHeight());
    const bool resolutionOpen = ImGui::BeginCombo("##NrResolution", ResolutionChoices[resolution]);
    if (!stackedLabels) MenuControls::HighlightItem(controlDraw, controlMin, controlMax,
                                ImGui::IsItemFocused() || ImGui::IsItemActive());
    if (resolutionOpen)
    {
        const int resolutionDisplayOrder[] = { 1, 0, 2 };
        for (int choice : resolutionDisplayOrder)
        {
            const char* refusal = UnifiedResolutionRefusal(snapshot, choice, nativeAfterOnly);
            ImGui::BeginDisabled(refusal != nullptr);
            if (ImGui::Selectable(ResolutionChoices[choice], choice == resolution))
            {
                NrConfigSynchronization::Transaction transaction;
                const auto inputPolicy = config.DlssNrPresentInputPolicy;
                SelectUnifiedResolution(config, choice);
                config.DlssNrPresentInputPolicy = inputPolicy;
                changed = true;
            }
            ImGui::EndDisabled();
            if (refusal) ImGui::TextWrapped("%s",Neurotic::Translate(refusal).c_str());
        }
        ImGui::EndCombo();
    }
    if (!stackedLabels)
    {
        if (sameRow) MenuControls::AlignTrailingLabel(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_resolution_399e2689", "NR Resolution"));
        else ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_resolution_399e2689", "NR Resolution"));
    }
    ImGui::EndDisabled();
    if (showPlacementRefusal)
    {
        if (placementRefusal)
            StatusPanel::Linef("##NrPlacement", ImGui::GetStyleColorVec4(ImGuiCol_Text), Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.this_legacy_placement_is_preserved_ray_reconstru_a9c0abcb", "This legacy placement is preserved. Ray Reconstruction and native Vulkan use NR after reconstruction; choose Full output or Manual to update it."));
        else if (snapshot.DlssNrRoute.value_or_default() == 0 && Stage(snapshot) == 0 && Manual(snapshot))
            StatusPanel::Linef("##NrPlacement", ImGui::GetStyleColorVec4(ImGuiCol_Text), Neurotic::UiLiteral("ingame.dlssnr-menu.legacy_before_upscaling_manual_placement_is_pres_2163c958", "Legacy before-upscaling Manual placement is preserved until you choose a resolution."));
    }
    return changed;
}
}
