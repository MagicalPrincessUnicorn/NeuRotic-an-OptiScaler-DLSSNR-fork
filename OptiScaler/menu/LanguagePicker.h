#pragma once
#include "Localization.h"
#include "localization/LanguageRuntime.h"
#include <imgui/imgui.h>
namespace Neurotic {
inline bool LanguagePicker(const char* id){
 using namespace Localization;
 static std::vector<LanguageChoice> choices;
 static std::string failure;
 const auto selected=SelectedPackId();
 const auto preview=SelectedLanguageName()+" · "+LocaleDisplayCode(SelectedLocale());
 bool changed=false;
 if(ImGui::BeginCombo(id,preview.c_str())){
  if(ImGui::IsWindowAppearing()||choices.empty())choices=AvailableLanguages();
  for(const auto& choice:choices){
   ImGui::PushID(choice.id.c_str());const auto caption=choice.name+" · "+LocaleDisplayCode(choice.locale);
   if(ImGui::Selectable(caption.c_str(),selected==choice.id))changed=SelectSharedLanguage(choice.id,{},failure);
   if(selected==choice.id)ImGui::SetItemDefaultFocus();ImGui::PopID();
  }
  ImGui::EndCombo();
 }
 if(!failure.empty())ImGui::OpenPopup("##LanguageSelectionFailure");
 if(ImGui::BeginPopup("##LanguageSelectionFailure")){
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*24);ImGui::TextUnformatted(failure.c_str());ImGui::PopTextWrapPos();
  if(ImGui::Button(UiLiteral("desktop.hubshell.close_4bae39a9","Close"))||ImGui::IsKeyPressed(ImGuiKey_Escape)){failure.clear();ImGui::CloseCurrentPopup();}
  ImGui::EndPopup();
 }
 return changed;
}
}
