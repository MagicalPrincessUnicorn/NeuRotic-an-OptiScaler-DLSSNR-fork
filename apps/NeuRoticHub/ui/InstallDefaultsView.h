#pragma once
#include "HubViewModel.h"
#include "SleekWidgets.h"
#include "imgui.h"
namespace nh {
inline void RenderInstallDefaults(HubModel& model){
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installdefaults.explanation","Used for new installations. Game-specific requirements take priority."));
 ImGui::Spacing();
 if(model.installDefaults.readOnly)ImGui::TextWrapped("%s",model.installDefaults.issue.c_str());
 ImGui::BeginDisabled(model.installDefaults.readOnly||model.showDataMaintenance||model.restartForMaintenance);
 const auto& preferences=model.installDefaults.preferences;
 const char* modeLabels[]={Neurotic::UiLiteral("desktop.installdefaults.present","Present"),Neurotic::UiLiteral("desktop.installdefaults.native","Native"),Neurotic::UiLiteral("desktop.anythingview.nr_anything_331a981c","NR Anything")};
 const int modeValues[]={2,0,3};
 auto choose=[&](const char* label,const char* key,const char* const* labels,int count,int current,auto apply){
  ImGui::PushID(key);const bool inlineRow=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*34;
  ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);
  if(inlineRow)ImGui::SameLine(ImGui::GetFontSize()*14);
  ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize()*22,ImGui::GetContentRegionAvail().x));
  if(ImGui::BeginCombo("##Value",labels[current])){for(int index=0;index<count;++index)if(ImGui::Selectable(labels[index],index==current))apply(index);ImGui::EndCombo();}ImGui::PopID();
 };
 const int selectedMode=preferences.value("nrMode",2);int modeIndex=selectedMode==0?1:selectedMode==3?2:0;
 choose(Neurotic::UiLiteral("desktop.installdefaults.nr_mode","NR Mode"),"nrMode",modeLabels,3,modeIndex,[&](int i){model.SetInstallDefault("nrMode",modeValues[i]);});
 const char* presetLabels[]={Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c","Automatic"),Neurotic::UiLiteral("desktop.installdefaults.default_preset","Default"),Neurotic::UiLiteral("desktop.installdefaults.preset1","Preset 1"),Neurotic::UiLiteral("desktop.installdefaults.preset2","Preset 2"),Neurotic::UiLiteral("desktop.installdefaults.preset3","Preset 3")};
 const char* presetValues[]={"auto","0","1","2","3"};auto preset=preferences.value("modelPreset",std::string("auto"));int presetIndex=preset=="auto"?0:std::stoi(preset)+1;
 choose(Neurotic::UiLiteral("desktop.neurotic-hubsettings.model_preset_2776030c","Model preset"),"modelPreset",presetLabels,5,presetIndex,[&](int i){model.SetInstallDefault("modelPreset",presetValues[i]);});
 const char* styleLabels[]={Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.ef6691545d","Standard"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.d6acb6d51c","Natural"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.912d0988b0","Cinematic")};
 choose(Neurotic::UiLiteral("desktop.installdefaults.style","Style"),"style",styleLabels,3,preferences.value("style",0),[&](int i){model.SetInstallDefault("style",i);});
 const char* mfgLabels[]={Neurotic::UiLiteral("desktop.installdefaults.off","Off"),Neurotic::UiLiteral("desktop.installdefaults.rtx40","RTX 40 series"),Neurotic::UiLiteral("desktop.installdefaults.rtx30","RTX 30 series"),Neurotic::UiLiteral("desktop.installdefaults.rtx20","RTX 20 series")};
 const char* mfgValues[]={"off","rtx40","rtx30","rtx20"};auto mfg=preferences.value("mfgUnlock",std::string("off"));int mfgIndex=mfg=="rtx40"?1:mfg=="rtx30"?2:mfg=="rtx20"?3:0;
 auto* storage=ImGui::GetStateStorage();auto pendingKey=ImGui::GetID("##PendingInstallMfg");
 choose(Neurotic::UiLiteral("desktop.installdefaults.mfg_unlock","MFG Unlock"),"mfgUnlock",mfgLabels,4,mfgIndex,[&](int i){if(i==0)model.SetInstallDefault("mfgUnlock","off");else storage->SetInt(pendingKey,i);});
 if(storage->GetInt(pendingKey,0)>0)ImGui::OpenPopup("##ConfirmInstallMfg");
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installdefaults.mfg_help","MFG Unlock saves a startup request. Game, GPU and provider compatibility checks still apply."));
 if(ui::Button(Neurotic::UiLiteral("desktop.installdefaults.reset","Reset defaults")))model.ResetInstallDefaults();
 ImGui::EndDisabled();
 if(ImGui::BeginPopupModal("##ConfirmInstallMfg",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(520.f,ImGui::GetMainViewport()->Size.x-80.f));
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.installdefaults.confirm_title","Enable experimental MFG defaults?"));
  ImGui::TextWrapped(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.enable_the_selected_experimental_options_these_p_fc7015ea","Enable the selected experimental options? These paths are untested or still under development and may cause instability or crashes."));
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installdefaults.confirm_safety","GPU safety checks remain active. This saves a preference for new installations; it does not enable FG or prove generated frame delivery. Do not combine external unlockers."));
  ImGui::PopTextWrapPos();
  if(ui::Button(Neurotic::UiLiteral("ingame.experimentalmfgcontrols.i_agree_c8a66b1b","I agree"))){int index=storage->GetInt(pendingKey,0);if(index>0&&index<4)model.SetInstallDefault("mfgUnlock",mfgValues[index]);storage->SetInt(pendingKey,0);ImGui::CloseCurrentPopup();}
  ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d","Cancel"))){storage->SetInt(pendingKey,0);ImGui::CloseCurrentPopup();}
  ImGui::EndPopup();
 }
}
}
