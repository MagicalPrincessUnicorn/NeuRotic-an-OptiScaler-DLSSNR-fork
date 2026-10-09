#include <menu/Localization.h>
#pragma once
#include <imgui/imgui.h>

namespace Neurotic::Sleek
{
// UI intent only: callers retain ownership of route changes, persistence and admission.
inline unsigned ExperimentalMfgControls(bool& rtx40, bool& rtx30, bool& rtx20, float scale)
{
    bool* choices[] = {&rtx40, &rtx30, &rtx20};
    const char* labels[] = {
        Neurotic::UiLiteral("ingame.experimentalmfgcontrols.native_mfg_unlock_for_rtx_40_series_e6808664", "Native MFG unlock for RTX 40 series###ExperimentalMfgRTX40"),
        Neurotic::UiLiteral("ingame.experimentalmfgcontrols.experimental_mfg_unlock_for_rtx_30_series_9716d91d", "Experimental MFG unlock for RTX 30 series###ExperimentalMfgRTX30"),
        Neurotic::UiLiteral("ingame.experimentalmfgcontrols.experimental_mfg_unlock_for_rtx_20_series_b1e33bdd", "Experimental MFG unlock for RTX 20 series###ExperimentalMfgRTX20")};
    auto* storage = ImGui::GetStateStorage();
    const auto pendingKey = ImGui::GetID("##PendingMfgCompatibility");
    unsigned changed = 0;
    for (int i = 0; i < 3; ++i)
    {
        bool selected = *choices[i];
        if (!ImGui::Checkbox(labels[i], &selected)) continue;
        if (selected && i != 0)
        {
            storage->SetInt(pendingKey, i);
            ImGui::OpenPopup(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.confirm_experimental_gpu_compatibility_673dc4f5", "Confirm experimental GPU compatibility"));
        }
        else
        {
            *choices[i] = selected;
            changed |= 1u << i;
        }
    }
    if (ImGui::BeginPopupModal(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.confirm_experimental_gpu_compatibility_673dc4f5", "Confirm experimental GPU compatibility"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        const int pending = storage->GetInt(pendingKey, -1);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f * scale);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.enable_the_selected_experimental_options_these_p_fc7015ea", "Enable the selected experimental options? These paths are untested or still under development and may cause instability or crashes."));
        if (pending >= 1 && pending < 3)
            ImGui::BulletText(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.experimental_mfg_unlock_for_rtx_d_series_039f158c", "Experimental MFG unlock for RTX %d series"), 40 - pending * 10);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.confirmed_gpu_corruption_safety_checks_remain_ac_a0d2454c", "Confirmed GPU-corruption safety checks remain active. This choice applies only after you agree. Save settings and restart the game; this does not prove provider support or displayed frame delivery. Do not combine external unlockers."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.i_agree_c8a66b1b", "I agree")))
        {
            if (pending >= 1 && pending < 3)
            {
                *choices[pending] = true;
                changed |= 1u << pending;
            }
            storage->SetInt(pendingKey, -1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d", "Cancel")))
        {
            storage->SetInt(pendingKey, -1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return changed;
}
}
