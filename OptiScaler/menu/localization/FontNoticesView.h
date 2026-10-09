#pragma once
#include "../Localization.h"
#include "../BoundedPopup.h"
#include <imgui/imgui.h>
namespace Neurotic {
inline void FontNoticesView(bool inGame=false){
 if(ImGui::Button(UiLiteral(inGame?"ingame.languages.font_notices":"desktop.languages.font_notices","Font notices")))ImGui::OpenPopup("##FontNotices");
 SetBoundedPopupSize(39.f,29.f);
 if(ImGui::BeginPopupModal("##FontNotices",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize)){
  ImGui::TextUnformatted(UiLiteral(inGame?"ingame.languages.font_notices":"desktop.languages.font_notices","Font notices"));
  ImGui::BeginChild("##LicenseText",{0,-ImGui::GetFrameHeightWithSpacing()});
  const auto& text=FontNoticeText();const auto fallback=UiMessage(inGame?"ingame.languages.font_notice_location":"desktop.languages.font_notice_location","Font redistribution notices are supplied in licenses/.");ImGui::TextWrapped("%s",text.empty()?fallback.c_str():text.c_str());
  ImGui::EndChild();if(ImGui::Button(UiLiteral("desktop.hubshell.close_4bae39a9","Close"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();ImGui::EndPopup();
 }
}
}
