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
// Shared by the production page and the headless ImGui navigation/layout fixture.
// Stable IDs and full-width selectors accommodate translated preset names.
template<class C> bool RenderControls(C& config)
{
    bool changed = false;
    auto snapshot = config.GetDlssNrConfigSnapshot();
    int stage = Stage(snapshot);
    ImGui::TextUnformatted("Processing stage");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##NrStage", &stage, Stages, 2))
    {
        SelectStage(config, stage);
        changed = true;
    }
    snapshot = config.GetDlssNrConfigSnapshot();
    int method = int(snapshot.DlssNrRoute.value_or_default());
    ImGui::TextUnformatted("Rendering method");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextWrapped("Native Temporal uses the game's upscaler inputs. Present (Compatibility) processes the final image without RR guides. Present (Enhanced) requires verified depth and motion guides; unavailable guides preserve the original image.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(stage == 0);
    if (ImGui::Combo("##NrMethod", &method, Methods, stage == 0 ? 1 : 3))
    {
        SelectMethod(config, method);
        changed = true;
    }
    ImGui::EndDisabled();
    snapshot = config.GetDlssNrConfigSnapshot();
    ImGui::TextUnformatted("Model resolution");
    ImGui::SetNextItemWidth(-1.0f);
    if (method == 0)
    {
        int manual = Manual(snapshot) ? 1 : 0;
        if (ImGui::Combo("##NrResolution", &manual, NativeResolutions, 2))
        {
            SelectManual(config, manual != 0);
            changed = true;
        }
    }
    else
    {
        int preset = Preset(PresentResolution::Selected(snapshot));
        if (ImGui::Combo("##NrResolution", &preset, PresentPresets, 7))
        {
            SelectPreset(config, preset);
            changed = true;
        }
    }
    return changed;
}
}
