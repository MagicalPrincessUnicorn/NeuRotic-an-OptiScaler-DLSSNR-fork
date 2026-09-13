#pragma once
#include "DlssNr_StageUi.h"
#include "DlssNr_MenuStatus.h"
#include <menu/Localization.h>
#include <imgui/imgui.h>
#include <cstdio>

namespace DlssNr::StageUi
{
inline std::string DimensionText(uint32_t workW, uint32_t workH, uint32_t outputW, uint32_t outputH)
{
    char text[256] {};
    if (workW && workH && outputW && outputH)
        std::snprintf(text, sizeof(text), Neurotic::Translate("NR: %u x %u | Output: %u x %u").c_str(), workW, workH, outputW, outputH);
    else if (workW && workH)
        std::snprintf(text, sizeof(text), Neurotic::Translate("NR: %u x %u | Output: unavailable").c_str(), workW, workH);
    else if (outputW && outputH)
        std::snprintf(text, sizeof(text), Neurotic::Translate("NR: unavailable | Output: %u x %u").c_str(), outputW, outputH);
    else
        return Neurotic::Translate("NR: unavailable | Output: unavailable");
    return text;
}
inline bool RenderRuntimeStatus(const std::optional<MenuStatus::RuntimeStatus>& status)
{
    if (!status) return false; // caller retains the existing generic status
    const auto& s = *status;
    const auto state = Neurotic::Translate(MenuStatus::StateNames[std::clamp(int(s.state), 0, 6)]);
    ImGui::TextWrapped("Status: %s", state.c_str());
    if (s.provider[0]) ImGui::TextWrapped("Provider: %.*s", int(s.provider.size()), s.provider.data());
    if (s.route[0]) ImGui::TextWrapped("Route: %.*s", int(s.route.size()), s.route.data());
    if (s.reason[0]) ImGui::TextWrapped("Reason: %.*s", int(s.reason.size()), s.reason.data());
    if (s.realFrame) ImGui::Text("Real frame: %llu", static_cast<unsigned long long>(*s.realFrame));
    if (s.workWidth && s.workHeight && s.outputWidth && s.outputHeight)
        ImGui::Text("NR: %u x %u | Output: %u x %u", s.workWidth, s.workHeight, s.outputWidth, s.outputHeight);
    return true;
}
inline void SentenceHelpMarker(const char* tip)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

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
inline bool SentenceCombo(const char* id, const char* prefix, const char* suffix, int* value,
                          const char* const* items, int count, const char* help = nullptr,
                          float minimumWidth = 0.0f, bool selectorDisabled = false)
{
    float widest = 0;
    for (int i = 0; i < count; ++i) widest = (std::max)(widest, ImGui::CalcTextSize(items[i]).x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float naturalWidth = widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
    const float width = (std::max)(naturalWidth, minimumWidth);
    const bool hasHelp = help && *help;
    const float helpWidth = hasHelp ? ImGui::CalcTextSize("(?)").x : 0.0f;
    float required = width;
    if (*prefix) required += ImGui::CalcTextSize(prefix).x + spacing;
    if (hasHelp) required += helpWidth + spacing;
    if (*suffix) required += ImGui::CalcTextSize(suffix).x + spacing;
    const bool fits = ImGui::GetContentRegionAvail().x >= required;
    if (*prefix)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", prefix);
        if (fits) ImGui::SameLine();
    }
    const float selectorRowWidth = ImGui::GetContentRegionAvail().x;
    const float wrappedWidth = hasHelp ?
        (std::max)(40.0f, selectorRowWidth - helpWidth - spacing) : -1.0f;
    ImGui::SetNextItemWidth(fits ? width : wrappedWidth);
    if (selectorDisabled) ImGui::BeginDisabled();
    const bool changed = ImGui::Combo(id, value, items, count);
    if (selectorDisabled) ImGui::EndDisabled();
    if (hasHelp)
    {
        ImGui::SameLine();
        SentenceHelpMarker(help);
    }
    if (*suffix)
    {
        if (fits) ImGui::SameLine();
        ImGui::TextWrapped("%s", suffix);
    }
    return changed;
}

// Shared by the production page and the headless ImGui navigation/layout fixture.
template<class C> bool RenderControls(C& config, bool basicOwnsResolution = false, bool nativeAfterOnly = false)
{
    bool changed = false;
    auto snapshot = config.GetDlssNrConfigSnapshot();
    int stage = Stage(snapshot);
    constexpr const char* stageHelp =
        "Before runs Neural Rendering on the game's render input before upscaling and permits "
        "Native Temporal only. After runs on the final upscaled output and enables the Present "
        "methods. Your last selected After method is remembered.";
    if (SentenceCombo("##NrStage", "Neural Rendering Injection", "upscaling", &stage, Stages, 2,
                      stageHelp, ImGui::GetFontSize() * 8.0f))
    {
        SelectStage(config, stage);
        changed = true;
    }
    snapshot = config.GetDlssNrConfigSnapshot();
    const int route = int(snapshot.DlssNrRoute.value_or_default());
    int method = route == 1 ? 2 : route == 2 ? 1 : 0;
    static const char* orderedMethods[] = { "Native Temporal", "Present Enhanced", "Present Compatibility" };
    constexpr const char* methodHelp =
        "Native Temporal uses the game's upscaler inputs. Present Compatibility processes the "
        "final image without RR guides. Present Enhanced requires verified depth and motion "
        "guides; unavailable or invalid guides preserve the original image.";
    if (SentenceCombo("##NrMethod", "Use", "Neural Rendering Mode", &method, orderedMethods,
                      stage == 0 ? 1 : 3, methodHelp, 0.0f, stage == 0))
    {
        SelectMethod(config, method == 1 ? 2 : method == 2 ? 1 : 0);
        changed = true;
    }
    if (stage == 0) ImGui::TextWrapped("Present methods require After. Your After method is remembered.");
    snapshot = config.GetDlssNrConfigSnapshot();
    int resolution = basicOwnsResolution ? ManualChoice : ResolutionChoiceSelection(snapshot);
    const auto* placementRefusal = NativePlacementRefusal(snapshot, nativeAfterOnly);
    ImGui::BeginDisabled(basicOwnsResolution || placementRefusal != nullptr);
    ImGui::TextUnformatted("Neural Rendering Resolution");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Always Full Output uses the final game output dimensions.\n"
                          "Match Game Render - Recommended follows the game's render-input dimensions.\n"
                          "Manual - Advanced / Low-end uses the selected percentage; very low values substantially reduce detail and the NR effect.");
    if (ImGui::BeginCombo("##NrResolution", ResolutionChoices[resolution]))
    {
        for (int choice = 0; choice < 3; ++choice)
        {
            const char* refusal = ResolutionRefusal(snapshot, choice);
            ImGui::BeginDisabled(refusal != nullptr);
            if (ImGui::Selectable(ResolutionChoices[choice], choice == resolution))
            {
                SelectResolutionChoice(config, choice);
                changed = true;
            }
            ImGui::EndDisabled();
            if (refusal && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", refusal);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (placementRefusal) ImGui::TextWrapped("%s", placementRefusal);
    return changed;
}
}
