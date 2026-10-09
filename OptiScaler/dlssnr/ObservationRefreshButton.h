#pragma once
#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <cmath>
#include <menu/RefreshIcon.h>
#include <menu/SleekUi.h>
namespace DlssNr {
inline float ObservationRefreshButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2.0f+
        ImGui::GetFontSize()*2.0f;
}
inline bool ObservationRefreshButton(const char* label,bool busy,float width) {
    ImGui::BeginDisabled(busy);
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    const bool pressed=ImGui::Button(label,ImVec2(width,0));
    ImGui::PopStyleColor();
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    const float font=ImGui::GetFontSize();
    const ImVec2 center(min.x+ImGui::GetStyle().FramePadding.x+font*0.5f,(min.y+max.y)*0.5f);
    auto* draw=ImGui::GetWindowDrawList();
    Neurotic::DrawRefreshIcon(draw,{center.x-font*.5f,center.y-font*.5f},font,ink,busy,Neurotic::Sleek::reducedMotion);
    const auto caption=Neurotic::Translate(label);
    const ImVec4 clip(min.x,min.y,max.x-ImGui::GetStyle().FramePadding.x,max.y);
    draw->AddText(nullptr,0.0f,{center.x+font,center.y-font*0.5f},ink,caption.c_str(),nullptr,0.0f,&clip);
    ImGui::EndDisabled();return pressed;
}
inline bool ObservationActivationHeld() {
    return ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsKeyDown(ImGuiKey_Enter) ||
        ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
}
}
