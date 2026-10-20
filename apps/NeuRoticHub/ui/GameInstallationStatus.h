#pragma once
#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <menu/ItemHelp.h>
#include <algorithm>
#include <string>
#include <string_view>
namespace nh::ui {
inline const char* InstallationStatusLabel(std::string_view status,bool cached,bool known){
 if(!known)return cached?Neurotic::UiLiteral("desktop.installation.unknown_saved","Status unknown (saved)"):Neurotic::UiLiteral("desktop.installation.unknown","Status unknown");
 if(status=="Installed")return cached?Neurotic::UiLiteral("desktop.installation.installed_saved","Installed (saved)"):Neurotic::UiLiteral("desktop.installation.installed","Installed");
 if(status=="Partial")return cached?Neurotic::UiLiteral("desktop.installation.partial_saved","Partial (saved)"):Neurotic::UiLiteral("desktop.installation.partial","Partial");
 if(status.empty())return cached?Neurotic::UiLiteral("desktop.installation.not_installed_saved","Not installed (saved)"):Neurotic::UiLiteral("desktop.bulkuninstall.not_installed_40cdadc4","Not installed");
 return cached?Neurotic::UiLiteral("desktop.installation.unknown_saved","Status unknown (saved)"):Neurotic::UiLiteral("desktop.installation.unknown","Status unknown");
}
inline void GameInstallationStatus(std::string_view status,bool cached,bool light,bool known=true,bool alignRight=false,const std::string& failure={}){
 const bool installed=status=="Installed",partial=status=="Partial";
 const auto color=!failure.empty()?(light?ImVec4(.61f,.29f,0,1):ImVec4(1.f,.68f,.16f,1)):cached?ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled):installed?
  (light?ImVec4(0,.37f,.14f,1):ImVec4(.20f,.86f,.42f,1)):partial?
  (light?ImVec4(.61f,.29f,0,1):ImVec4(1.f,.68f,.16f,1)):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
 const auto* label=failure.empty()?InstallationStatusLabel(status,cached,known):Neurotic::UiLiteral("desktop.installation.check_failed","Check failed");
 const auto text=Neurotic::Translate(label);const float font=ImGui::GetFontSize();
 const auto at=ImGui::GetCursorScreenPos();const float textWidth=ImGui::CalcTextSize(text.c_str()).x;
 const ImVec2 size{(std::max)(1.f,(std::min)(ImGui::GetContentRegionAvail().x,textWidth+font)),font*1.35f};
 if(alignRight)ImGui::SetCursorScreenPos({at.x+std::max(0.f,ImGui::GetContentRegionAvail().x-size.x),at.y});
 const auto markerAt=ImGui::GetCursorScreenPos();ImGui::Dummy(size);auto* draw=ImGui::GetWindowDrawList();
 draw->PushClipRect(markerAt,{markerAt.x+size.x,markerAt.y+size.y},true);
 draw->AddCircleFilled({markerAt.x+font*.2f,markerAt.y+size.y*.5f},font*.16f,ImGui::GetColorU32(color));
 draw->AddText({markerAt.x+font*.65f,markerAt.y+(size.y-font)*.5f},ImGui::GetColorU32(color),text.c_str());draw->PopClipRect();
 if(Neurotic::ItemHelpHovered(ImGui::GetID("##InstallationStatusHelp"))){
  if(!failure.empty())ImGui::SetTooltip("%s\n%s",Neurotic::Translate(InstallationStatusLabel(status,cached,known)).c_str(),failure.c_str());
  else if(textWidth+font>size.x)ImGui::SetTooltip("%s",text.c_str());
 }
}
}
