#pragma once
#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <string_view>
namespace nh::ui {
inline void GameInstallationStatus(std::string_view status,bool cached,bool light){
 const bool installed=status=="Installed",partial=status=="Partial";
 const auto color=cached?ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled):installed?
  (light?ImVec4(0,.37f,.14f,1):ImVec4(.20f,.86f,.42f,1)):partial?
  (light?ImVec4(.61f,.29f,0,1):ImVec4(1.f,.68f,.16f,1)):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
 const auto* label=installed?Neurotic::UiLiteral("desktop.installation.installed","Installed"):
  partial?Neurotic::UiLiteral("desktop.installation.partial","Partial"):
  Neurotic::UiLiteral("desktop.bulkuninstall.not_installed_40cdadc4","Not installed");
 ImGui::TextColored(color,"%s",Neurotic::Translate(label).c_str());
}
}
