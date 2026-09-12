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
// Wrap at word boundaries when a sentence will not fit alongside its selector.
inline bool SentenceCombo(const char* id, const char* prefix, const char* suffix, int* value,
                          const char* const* items, int count)
{
    float widest = 0;
    for (int i = 0; i < count; ++i) widest = (std::max)(widest, ImGui::CalcTextSize(items[i]).x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
    const bool fits = ImGui::GetContentRegionAvail().x >= width +
        ImGui::CalcTextSize(prefix).x + ImGui::CalcTextSize(suffix).x + spacing * 3;
    if (*prefix)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", prefix);
        if (fits) ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(fits ? width : -1.0f);
    const bool changed = ImGui::Combo(id, value, items, count);
    if (*suffix)
    {
        if (fits) ImGui::SameLine();
        ImGui::TextWrapped("%s", suffix);
    }
    return changed;
}

// Shared by the production page and the headless ImGui navigation/layout fixture.
template<class C> bool RenderControls(C& config, bool basicOwnsResolution = false)
{
    bool changed = false;
    auto snapshot = config.GetDlssNrConfigSnapshot();
    int stage = Stage(snapshot);
    if (SentenceCombo("##NrStage", "Neural Rendering Injection", "Upscaling", &stage, Stages, 2))
    {
        SelectStage(config, stage);
        changed = true;
    }
    snapshot = config.GetDlssNrConfigSnapshot();
    const int route = int(snapshot.DlssNrRoute.value_or_default());
    int method = route == 1 ? 2 : route == 2 ? 1 : 0;
    static const char* orderedMethods[] = { "Native Temporal", "Present Enhanced", "Present Compatibility" };
    ImGui::BeginDisabled(stage == 0);
    if (SentenceCombo("##NrMethod", "Use", "Neural Rendering Mode", &method, orderedMethods, stage == 0 ? 1 : 3))
    {
        SelectMethod(config, method == 1 ? 2 : method == 2 ? 1 : 0);
        changed = true;
    }
    ImGui::EndDisabled();
    if (stage == 0) ImGui::TextWrapped("Present methods require After. Your After method is remembered.");
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextWrapped("Native Temporal uses the game's upscaler inputs. Present (Compatibility) processes the final image without RR guides. Present (Enhanced) requires verified depth and motion guides; unavailable guides preserve the original image.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    snapshot = config.GetDlssNrConfigSnapshot();
    int resolution = basicOwnsResolution ? 1 : ResolutionSelection(snapshot);
    ImGui::BeginDisabled(basicOwnsResolution);
    if (SentenceCombo("##NrResolution", "", "Neural Rendering Resolution", &resolution, Resolutions,
                      resolution == 2 ? 3 : 2))
    {
        SelectResolution(config, resolution);
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}
}
