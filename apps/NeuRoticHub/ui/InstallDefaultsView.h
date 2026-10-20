#pragma once
#include "HubViewModel.h"
#include "SleekWidgets.h"
#include "SavedControlLayout.h"
#include "imgui.h"
namespace nh {
inline void RenderInstallDefaults(HubModel& model){
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installdefaults.explanation","Used for new installations. Game-specific requirements take priority."));
 ImGui::Spacing();
 if(model.installDefaults.readOnly)ImGui::TextWrapped("%s",model.installDefaults.issue.c_str());
 ImGui::BeginDisabled(model.installDefaults.readOnly||model.showDataMaintenance||model.restartForMaintenance);
 const auto& preferences=model.installDefaults.preferences;
 auto* storage=ImGui::GetStateStorage();const auto pendingKey=ImGui::GetID("##PendingInstallMfg");
 const char* modeLabels[]={Neurotic::UiLiteral("desktop.installdefaults.present","Present"),Neurotic::UiLiteral("desktop.installdefaults.native","Native")};
 const int modeValues[]={2,0};
 auto choose=[&](const char* label,const char* key,const char* const* labels,int count,int current,auto apply,const char* preview=nullptr){
  ImGui::PushID(key);const float rowWidth=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemInnerSpacing.x;
  const auto resetLabel=Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset");
  const auto reset=MeasureSavedReviewAction(resetLabel,rowWidth*.30f);
  const float left=ImGui::GetCursorPosX(),labelWidth=(std::min)(ImGui::GetFontSize()*14.f,rowWidth*.30f);
  const bool inlineRow=rowWidth>=labelWidth+2*gap+reset.width+ImGui::GetFontSize()*8.f;
  ImGui::AlignTextToFramePadding();if(inlineRow)ImGui::PushTextWrapPos(left+labelWidth);
  ImGui::TextWrapped("%s",Neurotic::Translate(label).c_str());
  if(inlineRow){ImGui::PopTextWrapPos();ImGui::SameLine(left+labelWidth+gap);}
  const float choiceRoom=std::max(1.f,ImGui::GetContentRegionAvail().x-reset.width-gap);
  float choiceWidth=MeasureSavedChoiceWidth(choiceRoom,labels,count);
  // A preserved saved value or Automatic preview can be wider than the current
  // selectable values. Measure its real localized text before allocating space.
  if(preview)choiceWidth=std::min(choiceRoom,std::max(choiceWidth,ImGui::CalcTextSize(preview).x+2*ImGui::GetStyle().FramePadding.x+ImGui::GetFrameHeight()));
  ImGui::SetNextItemWidth(choiceWidth);
  if(ImGui::BeginCombo("##Value",preview?preview:(current>=0&&current<count?labels[current]:""))){for(int index=0;index<count;++index)if(ImGui::Selectable(labels[index],index==current))apply(index);ImGui::EndCombo();}
  ImGui::SameLine(0,gap);if(SavedReviewButton(resetLabel,reset)){if(std::string_view(key)=="mfgUnlock")storage->SetInt(pendingKey,0);model.SetInstallDefault(key,nullptr);}ImGui::PopID();
 };
 const int selectedMode=preferences.value("nrMode",0);int modeIndex=selectedMode==0?1:selectedMode==2?0:-1;
 choose(Neurotic::UiLiteral("desktop.installdefaults.nr_mode","NR Mode"),"nrMode",modeLabels,2,modeIndex,[&](int i){model.SetInstallDefault("nrMode",modeValues[i]);},selectedMode==3?Neurotic::UiLiteral("desktop.installdefaults.saved_anything","NR Anything (saved)"):nullptr);
 const char* presetLabels[]={Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c","Automatic"),Neurotic::UiLiteral("desktop.installdefaults.default_preset","Default"),Neurotic::UiLiteral("desktop.installdefaults.preset1","Preset 1"),Neurotic::UiLiteral("desktop.installdefaults.preset2","Preset 2"),Neurotic::UiLiteral("desktop.installdefaults.preset3","Preset 3")};
 const char* presetValues[]={"auto","0","1","2","3"};auto preset=preferences.value("modelPreset",std::string("auto"));int presetIndex=preset=="auto"?0:std::stoi(preset)+1;
 choose(Neurotic::UiLiteral("desktop.neurotic-hubsettings.model_preset_2776030c","Model preset"),"modelPreset",presetLabels,5,presetIndex,[&](int i){model.SetInstallDefault("modelPreset",presetValues[i]);});
 const char* styleLabels[]={Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.ef6691545d","Standard"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.d6acb6d51c","Natural"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.912d0988b0","Cinematic")};
 choose(Neurotic::UiLiteral("desktop.installdefaults.style","Style"),"style",styleLabels,3,preferences.value("style",0),[&](int i){model.SetInstallDefault("style",i);});
 ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
 ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.diagnostics_1be508a4","Diagnostics"));
 const char* logLabels[]={Neurotic::UiLiteral("desktop.installdefaults.off","Off"),Neurotic::UiLiteral("desktop.installdefaults.on","On")};
 const auto automatic=Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c","Automatic");
 choose(Neurotic::UiLiteral("desktop.neurotic-hubsettings.write_log_file_932751a9","Write log file"),"logToFile",logLabels,2,
        preferences.contains("logToFile")?(preferences.value("logToFile",false)?1:0):-1,
        [&](int i){model.SetInstallDefault("logToFile",i==1);},preferences.contains("logToFile")?nullptr:automatic);
 const char* levelLabels[]={
  Neurotic::UiLiteral("desktop.settings.log/loglevel/value.7e14c3d7dd","Trace"),Neurotic::UiLiteral("desktop.settings.log/loglevel/value.1a03bd2fd1","Debug"),
  Neurotic::UiLiteral("desktop.settings.log/loglevel/value.170322a32f","Info"),Neurotic::UiLiteral("desktop.settings.log/loglevel/value.e981ddae45","Warning"),
  Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf","Error"),Neurotic::UiLiteral("desktop.settings.log/loglevel/value.427dd2969b","Critical"),
  Neurotic::UiLiteral("desktop.settings.dlssg/nativemfgexperimental/value.ca7981b46e","Off")};
 choose(Neurotic::UiLiteral("desktop.neurotic-hubsettings.file_log_detail_0e56323c","File log detail"),"logLevel",levelLabels,7,
        preferences.value("logLevel",-1),[&](int i){model.SetInstallDefault("logLevel",i);},preferences.contains("logLevel")?nullptr:automatic);
 bool policyKnown=false;std::string loggingProfile;
 if(model.preflight.is_object()){
  const auto found=model.preflight.find("loggingProfile");
  if(found!=model.preflight.end()&&found->is_string()){policyKnown=true;loggingProfile=found->get<std::string>();}
 }
 if(loggingProfile=="internal-testing")ImGui::TextWrapped("%s",Neurotic::UiLiteral("desktop.installdefaults.test_trace","Test packages keep Trace logging enabled."));
 else if(!policyKnown||(!loggingProfile.empty()&&loggingProfile!="public"))ImGui::TextWrapped("%s",Neurotic::UiLiteral("desktop.installdefaults.logging_unknown","Package logging policy unavailable."));
 else ImGui::TextWrapped("%s",Neurotic::UiLiteral("desktop.installdefaults.logging_new_ini","Saved defaults apply to new INI files."));
 ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
 const char* mfgLabels[]={Neurotic::UiLiteral("desktop.installdefaults.off","Off"),Neurotic::UiLiteral("desktop.installdefaults.rtx40","RTX 40 series"),Neurotic::UiLiteral("desktop.installdefaults.rtx30","RTX 30 series"),Neurotic::UiLiteral("desktop.installdefaults.rtx20","RTX 20 series")};
 const char* mfgValues[]={"off","rtx40","rtx30","rtx20"};auto mfg=preferences.value("mfgUnlock",std::string("off"));int mfgIndex=mfg=="rtx40"?1:mfg=="rtx30"?2:mfg=="rtx20"?3:0;
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
