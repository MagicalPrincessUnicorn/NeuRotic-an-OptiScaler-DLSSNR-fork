#include <menu/Localization.h>
#pragma once
#include <mfg/MfgSetup.h>
#include <mfg/ExperimentalMfgPolicy.h>
#include <imgui/imgui.h>
#include <algorithm>
#include "Localization.h"

namespace Neurotic::Sleek {
// Presentation only. Never changes selection, runtime admission or provider ownership.
constexpr bool MfgRestartRequired(const Mfg::MfgSetupInput& native, bool rejected,
    Mfg::Experimental::Preferences requested, Mfg::Experimental::Preferences session) noexcept
{
    return requested != session || (native.supported && rejected) ||
        Mfg::ResolveMfgSetup(native) == Mfg::MfgSetupStatus::Restart;
}
// The title and warning share one permanently reserved row.
inline void DrawMfgRestartWarning(bool required)
{
    const auto pos = ImGui::GetCursorScreenPos();
    const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
    const float height = ImGui::GetTextLineHeight();
    const auto title = Neurotic::Translate(Neurotic::UiLiteral("ingame.mfgrestartwarning.gpu_compatibility_4f5af3b9", "GPU compatibility"));
    const float warningX = (std::min)(width, ImGui::CalcTextSize(title.c_str()).x + ImGui::GetStyle().ItemSpacing.x*2);
    ImGui::Dummy({width, height});
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(pos, {pos.x+warningX, pos.y+height}, true);
    draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    draw->PopClipRect();
    if (required)
    {
        const auto message = Neurotic::Translate(Neurotic::UiLiteral("ingame.mfgrestartwarning.save_settings_and_restart_the_game_eb6aa130", "SAVE SETTINGS AND RESTART THE GAME"));
        const ImVec2 start(pos.x+warningX, pos.y), end(pos.x+width,pos.y+height);
        const auto red = ImGui::GetColorU32(ImVec4(1,.18f,.16f,1));
        draw->PushClipRect(start, end, true);
        draw->AddText(start, red, message.c_str());
        draw->AddText({start.x+.5f,start.y}, red, message.c_str());
        draw->PopClipRect();
        if (ImGui::CalcTextSize(message.c_str()).x > width-warningX &&
            ImGui::IsMouseHoveringRect(start, end))
        { ImGui::BeginTooltip(); ImGui::TextUnformatted(message.c_str()); ImGui::EndTooltip(); }
    }
    ImGui::Spacing();
}
}
