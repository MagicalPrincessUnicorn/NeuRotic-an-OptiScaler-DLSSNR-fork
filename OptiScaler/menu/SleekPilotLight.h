#pragma once
#include <imgui/imgui.h>

namespace Neurotic::Sleek
{
enum class PilotState { Off, On, Degraded, Blocked };
inline float PilotReserve() { return ImGui::GetFontSize() * 1.1f; }
inline ImVec4 PilotColor(PilotState state)
{
    return state == PilotState::On ? ImVec4(.2f,.8f,.35f,1) :
        state == PilotState::Degraded ? ImVec4(1,.58f,.12f,1) :
        state == PilotState::Blocked ? ImVec4(.95f,.2f,.2f,1) : ImVec4(.48f,.5f,.54f,1);
}
inline void DrawPilotLight(ImVec2 center, PilotState state)
{
    ImGui::GetWindowDrawList()->AddCircleFilled(center, ImGui::GetFontSize()*.17f,
                                               ImGui::GetColorU32(PilotColor(state)));
}
// Draw only: the containing tab keeps its existing selection behavior.
inline void TabPilotLight(PilotState state)
{
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    DrawPilotLight({hi.x-ImGui::GetStyle().FramePadding.x-ImGui::GetFontSize()*.25f,
                    (lo.y+hi.y)*.5f}, state);
}
}
