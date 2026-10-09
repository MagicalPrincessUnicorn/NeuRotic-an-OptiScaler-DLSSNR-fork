#include <menu/localization/LanguageRuntime.h>
#include <menu/Localization.h>
#include "AnythingView.h"
#include "AnythingController.h"
#include "AnythingPreferences.h"
#include "../../NeuRoticWindowWorker/control/SnapshotLocation.h"
#include "artwork/ArtworkService.h"
#include <cctype>
#include <cstdio>
#include "ui/HubViewModel.h"
#include "ui/SleekWidgets.h"
#include "ui/ActionFont.h"
#include "imgui.h"
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <array>
#include <algorithm>
namespace nh {
namespace {
bool diagnosticsDialogRequested=false;
std::string Lower(std::string text);
bool SameWindow(const Json& a,const Json& b){if(a.is_null()||b.is_null())return a.is_null()&&b.is_null();for(auto key:{"hwnd","pid","processCreation","executable","windowClass"})if(!a.contains(key)||!b.contains(key)||a[key]!=b[key])return false;return true;}
void RememberAnythingTarget(AnythingUiState& page,const AnythingSnapshot& state){
 if(page.clearTargetPending){if(!state.active&&!state.busy&&!state.stopping)page.clearTargetPending=false;return;}
 if(state.active&&!state.stopping&&state.status.value("window",Json()).is_object()){
  page.selected=state.status["window"];page.activeTarget=page.selected;
 }
}
void StopAnythingTarget(AnythingUiState& page,AnythingController& controller,const AnythingSnapshot& state,bool clear){
 RememberAnythingTarget(page,state);
 page.pendingStart=Json();page.awaitingCountdown=false;
 controller.Stop();
 if(clear){
  page.selected=page.activeTarget=Json();page.selectRequested=page.windowListPending=false;
  page.clearTargetPending=true;page.notice.clear();
 }
}
uint64_t Count(const nlohmann::json& j,const char* key){auto it=j.find(key);return it!=j.end()&&it->is_number_unsigned()?it->get<uint64_t>():0;}
void EnsureController(HubModel& model){if(model.showDataMaintenance||model.restartForMaintenance)return;if(!model.anythingUi.preferencesLoaded){try{LoadAnythingPreferences(model.anythingUi,UserRoot()/L"Runtime"/L"anything-profiles.json");}catch(const std::exception& e){model.anythingUi.notice=e.what();}}if(model.loaded&&!model.anything&&!model.readiness.busy){model.anything=std::make_shared<AnythingController>(AnythingController::FindWorker(AppRoot()),SharedRuntimeRoot());model.anything->Connect();}}
}
Json AnythingStartRequest(const AnythingUiState& page){
 Json result={{"mode",page.selected.is_null()?"countdown":"selected"},{"guides","off"},{"cursor",page.cursor},{"split",page.split},{"transferStrength",page.transfer},{"colourStrength",page.colour},{"outputMode",page.fullscreen?"fullscreen":page.overlay?"overlay":"preview"},
 {"superResolution",page.superResolution?"fsr1":"off"},{"neuralRendering",page.neuralRendering}};
 const int resolutionPercent[]={100,75,67,50};
 result["modelStyle"]=page.look>=2?page.look-1:0;
 result["frameGeneration"]=page.frameGeneration?"2x":"off";
 result["stripes"]=page.comparison==3?page.stripes:0;result["comparisonDirection"]=page.comparisonDirection;
 result["split"]=page.comparison==1?1.f:page.comparison==2?page.split:0.f;
 result["nrScalePercent"]=page.resolution==4?std::clamp(page.manualPercent,25,100):resolutionPercent[page.resolution>=0&&page.resolution<4?page.resolution:2];
 if(page.selected.is_null())result["seconds"]=static_cast<unsigned>(std::clamp(page.countdownSeconds,1,30));else result["window"]=page.selected;
 return result;
}
std::string AnythingActionLabel(const AnythingUiState& page,const AnythingSnapshot& state){
 if(state.stopping)return Neurotic::UiMessage("desktop.anything.stopping_action","Stopping...");
 if(state.active){
  if(state.phase==Neurotic::UiLiteral("desktop.anythingview.countdown_58c4583f", "Countdown")){auto ms=std::min(Count(state.status,"countdownRemainingMs"),uint64_t(30000));auto seconds=std::max((ms+999)/1000,uint64_t(1));return Neurotic::Localization::Format(Neurotic::Localization::SharedText("desktop.anything.countdown_action"),{{"arg1",int64_t(seconds)}});}
  if(page.awaitingCountdown&&state.status.empty())return Neurotic::Localization::Format(Neurotic::Localization::SharedText("desktop.anything.countdown_action"),{{"arg1",int64_t(page.activeCountdownSeconds)}});
  return Neurotic::UiMessage("desktop.anything.stop_action","Stop");
 }
 if(state.busy)return Neurotic::UiMessage("desktop.anythingview.please_wait_6e4ccfa9", "Please wait...");
 return Neurotic::UiMessage("desktop.anythingview.nr_anything_331a981c", "NR Anything");
}
void ToggleAnythingSelected(HubModel& model) try{
 EnsureController(model);auto& page=model.anythingUi;
 if(!model.anything){page.notice=Neurotic::UiMessage("desktop.anythingview.open_nr_anything_and_connect_the_worker_before_u_43ac3747", "Open NR Anything and connect the worker before using its shortcut.");return;}
 auto state=model.anything->Snapshot();
 if(state.stopping)return;
 if(state.active){StopAnythingTarget(page,*model.anything,state,false);return;}
 if(page.selected.is_null()){page.notice=Neurotic::UiMessage("desktop.anythingview.choose_a_specific_window_before_using_the_nr_any_7c0003c4", "Choose a specific window before using the NR Anything shortcut.");return;}
 if(!AnythingReadyForStages(state,page.neuralRendering)||state.busy){page.notice=Neurotic::UiMessage("desktop.anythingview.nr_anything_is_not_ready_check_its_model_and_con_335712bf", "NR Anything is not ready. Check its model and connection before starting.");return;}
 if(model.anything->Start(AnythingStartRequest(page))){model.page=2;page.activeTarget=page.selected;page.awaitingCountdown=false;page.notice.clear();}
 else page.notice=Neurotic::UiMessage("desktop.anythingview.the_selected_window_is_no_longer_available_choos_9a70c010", "The selected window is no longer available. Choose a window again.");
}catch(const std::exception& error){model.anythingUi.notice=error.what();}
bool CanCaptureAnythingScreenshot(const AnythingSnapshot& state){
 return state.connected&&state.active&&!state.stopping&&!state.busy&&state.phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")&&!state.status.value(Neurotic::UiLiteral("desktop.anythingview.sourcepaused_96a59408", "sourcePaused"),true)&&
  state.status.value("capabilities",Json::object()).value("outputSnapshots",false)&&state.status.value("snapshot",Json::object()).value("state",std::string{})!=Neurotic::UiLiteral("desktop.anythingview.pending_399dd91d", "pending");
}
bool CaptureAnythingScreenshot(HubModel& model)try{
 if(model.showDataMaintenance||model.restartForMaintenance)return false;
 if(!model.anything||!CanCaptureAnythingScreenshot(model.anything->Snapshot())||!model.anything->TakeSnapshot()){
  model.anythingUi.notice=Neurotic::UiMessage("desktop.anythingview.screenshot_unavailable_wait_for_active_rendering_1df0528f", "Screenshot unavailable. Wait for active rendering and any pending capture to finish.");return false;
 }
 // The existing worker snapshots its current comparison output; this does not
 // change target, comparison, foreground focus, or the selected App page.
 model.anythingUi.notice.clear();return true;
}catch(const std::exception& error){model.anythingUi.notice=error.what();return false;}
void RenderAnythingAction(HubModel& model,float dpi,float width,float height) try{
 EnsureController(model);auto& page=model.anythingUi;
 auto state=model.anything?model.anything->Snapshot():AnythingSnapshot{};
 RememberAnythingTarget(page,state);
 if(model.anything){auto& controller=*model.anything;
 const bool stageReady=AnythingReadyForStages(state,page.neuralRendering);
 if(stageReady&&!page.wasReady&&!state.busy){controller.RefreshWindows();state=controller.Snapshot();}page.wasReady=stageReady;
 for(auto& candidate:state.windows)if(!page.selected.is_null()&&SameWindow(candidate,page.selected)){page.selected=candidate;break;}
 if(!page.pendingStart.is_null()&&!state.busy&&!state.stopping&&AnythingReadyForStages(state,page.pendingStart.value("neuralRendering",true))){
  if(controller.Start(page.pendingStart)){page.activeTarget=page.pendingStart.value("window",Json());page.awaitingCountdown=page.activeTarget.is_null();page.activeCountdownSeconds=page.pendingStart.value("seconds",3);page.notice.clear();}
  else page.notice=Neurotic::UiMessage("desktop.anythingview.the_selected_window_is_no_longer_available_refre_8254133d", "The selected window is no longer available. Refresh the list and choose a window again.");
  page.pendingStart=Json();state=controller.Snapshot();
 }
 }
 if(!state.active||!state.status.empty())page.awaitingCountdown=false;
 bool validWindow=page.selected.is_null();for(auto& candidate:state.windows)if(candidate==page.selected)validWindow=true;
 const float arrowWidth=32*dpi,controlHeight=height>0?height:42*dpi;ImGui::BeginGroup();
 ImGui::BeginDisabled(state.stopping||(!state.active&&state.busy)||(!state.active&&AnythingReadyForStages(state,page.neuralRendering)&&!validWindow));
 auto label=AnythingActionLabel(page,state);auto color=state.active?ui::StatusColor(ui::ReadyState::Error,model.light):(model.light?ImVec4(.10f,.35f,.64f,1):ImVec4(.39f,.71f,1,1));
 const auto fill=state.active?ui::Mix(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),color,.18f):ImVec4(.28f,.60f,1,1);
 const auto actionText=state.active?ImVec4(1,1,1,1):ImVec4(.04f,.10f,.20f,1);
 auto pushActionColors=[&]{ImGui::PushStyleColor(ImGuiCol_Button,fill);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ui::Mix(fill,{1,1,1,1},.10f));ImGui::PushStyleColor(ImGuiCol_ButtonActive,ui::Mix(fill,{0,0,0,1},.12f));ImGui::PushStyleColor(ImGuiCol_Text,actionText);};
 ImGui::PushFont(ui::ActionFont(),18);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);pushActionColors();
 bool pressed=ui::Button("##NRAnythingControl",{std::max(60*dpi,width-arrowWidth),controlHeight});auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();auto draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(state.active?color:actionText);draw->AddRect(min,max,ImGui::GetColorU32(ImGuiCol_Border),0);float x=min.x+20*dpi,y=(min.y+max.y)*.5f;
 std::string countdown;auto split=label.find(" · ");if(split!=std::string::npos){countdown=label.substr(split+4);label.resize(split);}
 if(state.active&&countdown.empty())draw->AddRectFilled({x-5*dpi,y-5*dpi},{x+5*dpi,y+5*dpi},ink,1*dpi);
 else draw->AddTriangleFilled({x-5*dpi,y-7*dpi},{x-5*dpi,y+7*dpi},{x+7*dpi,y},ink);
 // Reserve the leading play/stop icon and only the needed countdown space.
 float left=min.x+34*dpi,right=max.x-(countdown.empty()?8.f:28.f)*dpi,textWidth=ImGui::CalcTextSize(label.c_str()).x;
 float textX=std::max(left,(left+right-textWidth)*.5f);ImGui::RenderTextEllipsis(draw,{textX,y-ImGui::GetTextLineHeight()*.5f},{right,y+ImGui::GetTextLineHeight()*.5f},right,label.c_str(),nullptr,nullptr);
 if(!countdown.empty()){auto size=ImGui::CalcTextSize(countdown.c_str());draw->AddText({max.x-18*dpi-size.x*.5f,y-size.y*.5f},ink,countdown.c_str());}
 ImGui::PopStyleColor(4);ImGui::PopStyleVar();ImGui::PopFont();
 if(pressed){
  model.page=2;
  if(!state.active&&page.neuralRendering&&!state.ready){page.pendingStart=Json();page.selectRequested=false;model.OpenComponentFolder(false);}
  else if(!model.anything){page.notice=Neurotic::UiMessage("desktop.anything.sr_connect_worker","Connect the worker before starting.");}
  else {auto& controller=*model.anything;
   if(state.active){StopAnythingTarget(page,controller,state,false);}
   else if(!state.connected)controller.Restart();
   else {auto request=AnythingStartRequest(page);if(controller.Start(request)){page.activeTarget=page.selected;page.awaitingCountdown=page.selected.is_null();page.activeCountdownSeconds=request.value("seconds",3);page.notice.clear();}else if(page.neuralRendering&&!controller.Snapshot().ready)model.OpenComponentFolder(false);else page.notice=controller.Snapshot().lastError;}
  }
 }
 ImGui::EndDisabled();
 // Refresh before opening, including when the previous catalog is already cached.
 const bool editing=state.active||state.busy||state.stopping;
 bool openWindows=false;
 if(page.windowListPending&&!state.busy){
  openWindows=state.connected&&!state.active&&!state.stopping&&state.catalogRevision>page.requestedCatalogRevision;
  if(!openWindows)page.notice=Neurotic::UiMessage("desktop.anythingview.window_list_could_not_be_refreshed_try_the_windo_2f54e613", "Window list could not be refreshed. Try the window arrow again.");
  page.windowListPending=false;
 }
 ImGui::SameLine(0,0);ImGui::BeginDisabled(editing||!state.connected||page.windowListPending);
 ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);pushActionColors();
 if(ui::Button("##WindowDropdown",{arrowWidth,controlHeight})&&!editing&&state.connected&&!page.windowListPending&&model.anything){
  page.requestedCatalogRevision=state.catalogRevision;
  page.windowListPending=model.anything->RefreshWindows();
 }
 const auto selectorMin=ImGui::GetItemRectMin(),selectorMax=ImGui::GetItemRectMax();
 ImGui::GetWindowDrawList()->AddRect(selectorMin,selectorMax,ImGui::GetColorU32(ImGuiCol_Border));
 ImGui::RenderArrow(ImGui::GetWindowDrawList(),{selectorMin.x+(arrowWidth-ImGui::GetFontSize())*.5f,selectorMin.y+(controlHeight-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),ImGuiDir_Down);
 ImGui::PopStyleColor(4);ImGui::PopStyleVar();ImGui::EndDisabled();
 ImGui::EndGroup();
 if(openWindows)ImGui::OpenPopup(Neurotic::UiLiteral("desktop.anythingview.window_list_33312957", "Window list"));
 auto viewport=ImGui::GetMainViewport();const float popupWidth=std::min(600*dpi,viewport->WorkSize.x-16*dpi);
 ImGui::SetNextWindowPos({std::max(viewport->WorkPos.x+8*dpi,selectorMax.x-popupWidth),selectorMax.y});ImGui::SetNextWindowSize({popupWidth,0});
 ImGui::SetNextWindowSizeConstraints({0,0},{viewport->WorkSize.x-16*dpi,viewport->WorkSize.y-16*dpi});
 if(ImGui::BeginPopup(Neurotic::UiLiteral("desktop.anythingview.window_list_33312957", "Window list"))){
  ImGui::BeginDisabled(editing);
  if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##WindowSearch",Neurotic::UiLiteral("desktop.anythingview.search_title_or_application_61f2cd31", "Search title or application"),page.windowSearch.data(),page.windowSearch.size());
  const auto search=Lower(page.windowSearch.data());bool any=false;
  if(ImGui::Selectable(Neurotic::UiLiteral("desktop.anythingview.none_72e83de2", "None"),page.selected.is_null())){page.selected=Json();ImGui::CloseCurrentPopup();}
  const float remaining=viewport->WorkPos.y+viewport->WorkSize.y-ImGui::GetCursorScreenPos().y-ImGui::GetStyle().WindowPadding.y-8*dpi;
  ImGui::BeginChild("WindowResults",{0,std::min(220*dpi,std::max(1.f,remaining))});
  for(const auto& window:state.windows){if(!search.empty()&&Lower(window.value(Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),std::string{})+" "+window.value("executable",std::string{})).find(search)==std::string::npos)continue;
   any=true;ImGui::PushID(std::to_string(Count(window,"hwnd")).c_str());
   if(ImGui::Selectable(window.value(Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),std::string{}).c_str(),SameWindow(window,page.selected))){page.selected=window;ImGui::CloseCurrentPopup();}ImGui::PopID();
  }
  if(!any)ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.no_matching_windows_e60df99f", "No matching windows."));ImGui::EndChild();ImGui::EndDisabled();ImGui::EndPopup();
 }
}catch(const std::exception& error){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.nr_anything_s_0967ef6c", "NR Anything: %s"),Neurotic::Translate(error.what()).c_str());}
namespace {
bool AnythingDialog(const char* title,float dpi){
 auto viewport=ImGui::GetMainViewport();
 ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Appearing,{.5f,.5f});
 ImGui::SetNextWindowSize({std::min(610*dpi,viewport->WorkSize.x-32*dpi),std::min(430*dpi,viewport->WorkSize.y-48*dpi)},ImGuiCond_Appearing);
 ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,{.08f,.12f,.22f,.72f});
 bool open=ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_NoSavedSettings);ImGui::PopStyleColor();return open;
}
void EndAnythingDialog(){
 ImGui::EndChild();ImGui::Separator();
 if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.done_3598b18e", "Done"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
 ImGui::EndPopup();
}
void DialogBody(){ImGui::BeginChild("DialogBody",{0,-ImGui::GetFrameHeightWithSpacing()-8},ImGuiChildFlags_None);}
std::string Lower(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
void AnythingMuted(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",Neurotic::Translate(text).c_str());ImGui::PopStyleColor();}
void AnythingCard(const char* id,const char* title,float width,float dpi,bool light,bool heading=true){
 ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,10*dpi);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16*dpi,12*dpi});
 ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,6*dpi});
 ImGui::BeginChild(id,{width,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysAutoResize,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
 if(heading){ImGui::PushFont(nullptr,26);ImGui::TextUnformatted(title);ImGui::PopFont();}
}
void EndAnythingCard(){ImGui::EndChild();ImGui::PopStyleVar(3);}
void AnythingRule(float dpi){ImGui::Dummy({0,3*dpi});ImGui::Separator();ImGui::Dummy({0,3*dpi});}
bool AnythingSegment(const char* label,bool selected,float width,float dpi){
 ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*dpi);
 ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(selected?ImGuiCol_ButtonActive:ImGuiCol_FrameBg));
 bool pressed=ui::Button(label,{width,34*dpi});ImGui::PopStyleColor();ImGui::PopStyleVar();return pressed;
}
bool AnythingStrength(const char* label,float& value,const char* helper,float dpi){
 ImGui::PushID(label);bool changed=false;const float width=ImGui::GetContentRegionAvail().x;
 ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::SameLine(std::max(0.f,width-80*dpi));
 ImGui::SetNextItemWidth(80*dpi);if(ImGui::InputFloat("##Value",&value,0,0,"%.2f")){if(!std::isfinite(value))value=1;value=std::clamp(value,0.f,2.f);changed=true;}
 const auto resetLabel=Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset");
 const float resetWidth=std::max(52*dpi,ImGui::CalcTextSize(Neurotic::Translate(resetLabel).c_str()).x+ImGui::GetStyle().FramePadding.x*2);
 ImGui::SetNextItemWidth(std::max(1.f,width-resetWidth-ImGui::GetStyle().ItemSpacing.x));changed|=ImGui::SliderFloat("##Strength",&value,0,2,"",ImGuiSliderFlags_AlwaysClamp);
 ImGui::SameLine();if(ui::Button(resetLabel,{resetWidth,0})){value=1;changed=true;}
 AnythingMuted(helper);ImGui::PopID();return changed;
}

}
float AnythingCountdownWidth(float dpi){
 const char* format=Neurotic::UiLiteral("desktop.anything.countdown_seconds_short", "%d s");
 Neurotic::LocalizedFormat localized(format);char label[128]{};snprintf(label,sizeof(label),format,30);
 return ImGui::CalcTextSize(label).x+12*dpi+ImGui::GetFrameHeight();
}
void RenderAnythingCountdown(HubModel& model,float dpi){
 EnsureController(model);auto& page=model.anythingUi;
 const auto state=model.anything?model.anything->Snapshot():AnythingSnapshot{};
 const char* format=Neurotic::UiLiteral("desktop.anything.countdown_seconds_short", "%d s");
 const auto delayText=[&](int seconds){const char* value=format;Neurotic::LocalizedFormat localized(value);char label[128]{};snprintf(label,sizeof(label),value,seconds);return std::string(label);};
 ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{6*dpi,ImGui::GetStyle().FramePadding.y});
 ImGui::BeginDisabled(state.active||state.busy||state.stopping);ImGui::SetNextItemWidth(AnythingCountdownWidth(dpi));
 const auto countdownLabel=delayText(page.countdownSeconds);
 if(ImGui::BeginCombo("##Countdown",countdownLabel.c_str())){
  ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.choose_foreground_after_delay_23769432", "Choose foreground after delay"));
  for(int seconds=1;seconds<=30;++seconds){const auto label=delayText(seconds);
   if(ImGui::Selectable(label.c_str(),page.selected.is_null()&&page.countdownSeconds==seconds)){
    page.countdownSeconds=seconds;
    try{SaveAnythingPreferences(page,UserRoot()/L"Runtime"/L"anything-profiles.json");}catch(const std::exception& error){page.notice=error.what();}
    model.Save();
   }
  }
  ImGui::EndCombo();
 }
 ImGui::EndDisabled();ImGui::PopStyleVar();
 if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.anythingview.choose_foreground_after_delay_23769432", "Choose foreground after delay"));
}
void RequestAnythingDiagnosticsDialog(){diagnosticsDialogRequested=true;}
void RenderAnything(HubModel& model,float dpi) try{
 ImGui::BeginChild("AnythingPage",{0,0},ImGuiChildFlags_None);
 struct PageScope{~PageScope(){ImGui::EndChild();}} pageScope;
 EnsureController(model);auto& page=model.anythingUi;
 const auto preferences=UserRoot()/L"Runtime"/L"anything-profiles.json";
 if(!page.preferencesLoaded){try{LoadAnythingPreferences(page,preferences);}catch(const std::exception& e){page.notice=e.what();}}
 auto save=[&]{try{SaveAnythingPreferences(page,preferences);}catch(const std::exception& e){page.notice=e.what();}};
 auto state=model.anything?model.anything->Snapshot():AnythingSnapshot{};
 const bool editing=state.active||state.busy||state.stopping;
 auto capability=[&](const char* name){return state.status.value("capabilities",Json::object()).value(name,false);};
 const bool processingBlocked=state.stopping||(state.active?!capability(Neurotic::UiLiteral("desktop.anythingview.liveprocessing_ac48aaa6", "liveProcessing")):state.busy);
 auto applyProcessing=[&]{save();if(state.active&&model.anything){if(capability("fsrFrameGeneration2x"))model.anything->SetFrameGeneration(page.frameGeneration);if(page.neuralRendering){auto request=AnythingStartRequest(page);Json settings;for(const char* key:{"nrScalePercent","modelStyle","transferStrength","colourStrength"})settings[key]=request[key];if(!model.anything->SetProcessing(settings))page.notice=Neurotic::UiMessage("desktop.anythingview.live_processing_change_was_not_accepted_stop_the_17ed5f6a", "Live processing change was not accepted. Stop the session and try again.");}}};
 const char* openDialog=nullptr;
 if(diagnosticsDialogRequested){diagnosticsDialogRequested=false;openDialog=Neurotic::UiLiteral("desktop.anythingview.nr_anything_diagnostics_18e0216a", "NR Anything diagnostics");}
 if(page.selectRequested){page.selectRequested=false;model.OpenComponentFolder(false);}
 const float available=ImGui::GetContentRegionAvail().x,gap=16*dpi;
 const bool wide=available>=760*dpi;const float cardWidth=wide?(available-gap)*.5f:available;
 auto identity=!page.clearTargetPending&&state.active&&state.status.value("window",Json()).is_object()?state.status["window"]:page.selected;
 const auto performance=state.status.value("performance",Json::object());const auto capture=performance.value("capture",Json::object());
 const bool running=state.phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")&&!state.status.value(Neurotic::UiLiteral("desktop.anythingview.sourcepaused_96a59408", "sourcePaused"),false);
 const bool countersAvailable=capture.contains("arrivalCallbacks")&&capture["arrivalCallbacks"].is_number_unsigned()&&state.status.contains("presentAccepted")&&state.status["presentAccepted"].is_number_unsigned();
 UpdateAnythingRates(page,ImGui::GetTime(),Count(state.status,"session"),Count(capture,"arrivalCallbacks"),Count(state.status,state.status.contains("processedCompleted")?"processedCompleted":"nrCompleted"),running&&countersAvailable,Count(state.status,"presentAccepted"));
 const auto processingState=state.status.value("processing",Json::object());auto processingPhase=processingState.value("state",std::string{});
 const bool healthy=running&&page.ratesKnown&&page.incomingFps>0&&page.processedFps>0&&page.deficitSeconds==0;
 auto lookCard=[&]{
  AnythingCard(Neurotic::UiLiteral("desktop.anythingview.anythinglookcard_8a879e8f", "AnythingLookCard"),Neurotic::UiLiteral("desktop.anything.look_heading","Look"),cardWidth,dpi,model.light);ImGui::BeginDisabled(processingBlocked);bool changed=false;
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.profile_59ccf293", "Profile"));const auto saveLabel=Neurotic::UiLiteral("desktop.anythingview.save_profile_1a567671", "Save profile...");const float saveWidth=std::max(112*dpi,ImGui::CalcTextSize(Neurotic::Translate(saveLabel).c_str()).x+ImGui::GetStyle().FramePadding.x*2);
  ImGui::SetNextItemWidth(std::max(90*dpi,ImGui::GetContentRegionAvail().x-saveWidth-ImGui::GetStyle().ItemSpacing.x));
  const auto defaultProfile=Neurotic::UiMessage("desktop.installdefaults.default_preset","Default");
  if(ImGui::BeginCombo("##Profile",page.profile=="Default"?defaultProfile.c_str():page.profile.c_str())){
   if(ImGui::Selectable(defaultProfile.c_str(),page.profile=="Default")){ApplyAnythingDefaultProfile(page);changed=true;}
   if(!page.profiles.empty())ImGui::Separator();for(const auto& item:page.profiles.items())if(!ProtectedAnythingProfile(item.key())&&ImGui::Selectable(item.key().c_str(),page.profile==item.key())){if(ApplyAnythingSessionProfile(page,item.value(),state.active)){page.profile=item.key();changed=true;}else page.notice=Neurotic::UiMessage("desktop.anythingview.this_profile_could_not_be_loaded_c6325469", "This profile could not be loaded.");}
   ImGui::EndCombo();
  }
  ImGui::SameLine();if(ui::Button(saveLabel,{saveWidth,0})){page.selectedUserProfile=page.profiles.contains(page.profile)&&!ProtectedAnythingProfile(page.profile)?page.profile:std::string{};snprintf(page.profileName.data(),page.profileName.size(),"%s",page.selectedUserProfile.c_str());openDialog=Neurotic::UiLiteral("desktop.anythingview.nr_anything_profiles_fdecfc2f", "NR Anything profiles");}
  AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.last_loaded_profile_remembered_cd8493f1", "Last loaded profile remembered"));ImGui::Dummy({0,4*dpi});
  AnythingRule(dpi);
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.nr_quality_a7e7948e", "NR quality"));ImGui::BeginDisabled(processingBlocked||!page.neuralRendering);ImGui::SetNextItemWidth(-1);
  if(ImGui::Combo("##NRQuality",&page.resolution,Neurotic::UiOptions("desktop.anythingview.full_resolution_100_43c681f2|desktop.anythingview.quality_75_8f8e3665|desktop.anythingview.balanced_67_ef85811d|desktop.anythingview.performance_50_9c9083bd|desktop.anythingview.manual_9ca08eb3|", "Full resolution - 100%\0Quality - 75%\0Balanced - 67%\0Performance - 50%\0Manual\0")))applyProcessing();
  if(page.resolution==4){
   ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.manual_resolution_332a66fe", "Manual resolution"));
   ImGui::SameLine(std::max(ImGui::GetCursorPosX(),(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x)-ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset")).x-ImGui::GetStyle().FramePadding.x*2));
   if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset##ManualResolution"))){page.manualPercent=100;applyProcessing();}
   ImGui::SetNextItemWidth(-1);ImGui::SliderInt("##ManualResolution",&page.manualPercent,25,100,"%d%%",ImGuiSliderFlags_AlwaysClamp);
   if(ImGui::IsItemDeactivatedAfterEdit())applyProcessing();
  }
  ImGui::EndDisabled();
  ImGui::Dummy({0,5*dpi});AnythingMuted(Neurotic::UiLiteral("desktop.anything.sr_nr_quality_help", "Changes NR processing size. Game rendering resolution is unchanged."));
  AnythingRule(dpi);
  ImGui::EndDisabled();
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anything.frame_generation","Frame generation"));
  const bool fgKnown=state.connected&&state.status.contains("capabilities")&&state.status["capabilities"].is_object();
  const bool supportedFg=fgKnown&&capability("fsrFrameGeneration2x");
  const bool fgChecking=!state.connected&&state.busy&&state.phase=="Connecting";
  const char* fgUnavailable=Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800","Unavailable");
  const char* fgOff=Neurotic::UiLiteral("desktop.anything.fg_off","Off");
  const char* fg2x=Neurotic::UiLiteral("desktop.anything.fg_2x","FSR 2x");
  ImGui::BeginDisabled(state.stopping||(state.busy&&!state.active)||(!supportedFg&&!page.frameGeneration));
  ImGui::SetNextItemWidth(-1);
  if(ImGui::BeginCombo("##AnythingFG",supportedFg?(page.frameGeneration?fg2x:fgOff):fgUnavailable)){
   for(int generation=0;generation<(supportedFg?2:1);++generation){
    const auto label=Neurotic::Translate(generation?fg2x:fgOff)+"##AnythingFGChoice";
    if(ImGui::Selectable(label.c_str(),page.frameGeneration==(generation==1))){page.frameGeneration=generation==1;save();if(supportedFg&&state.active&&model.anything)model.anything->SetFrameGeneration(page.frameGeneration);}
   }
   ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  const char* fgMessages[]={
   Neurotic::UiLiteral("desktop.anything.fg_worker_unavailable","Unavailable with this worker"),
   Neurotic::UiLiteral("desktop.anything.fg_comparison_paused","Paused for comparison"),
   Neurotic::UiLiteral("desktop.anything.fg_start","Applies when rendering starts"),
   Neurotic::UiLiteral("desktop.anything.fg_runtime_unavailable","FSR unavailable - showing real frames"),
   Neurotic::UiLiteral("desktop.anything.fg_late","Generation is behind - showing real frames"),
   Neurotic::UiLiteral("desktop.anything.fg_warming_up","Warming up"),
   Neurotic::UiLiteral("desktop.anything.fg_estimated","FSR 2x - estimated motion"),
   Neurotic::UiLiteral("desktop.anything.stage_checking","Checking availability..."),fgUnavailable};
  const float fgStatusY=ImGui::GetCursorPosY();float fgStatusHeight=ImGui::GetTextLineHeight();
  for(const char* message:fgMessages)fgStatusHeight=std::max(fgStatusHeight,ImGui::CalcTextSize(Neurotic::Translate(message).c_str(),nullptr,false,ImGui::GetContentRegionAvail().x).y);
  if(!supportedFg)AnythingMuted(fgMessages[fgChecking?7:fgKnown?0:8]);
  else if(page.frameGeneration){
   const auto fg=state.status.value("frameGeneration",Json::object());
   const bool availableFg=fg.value("available",false),lateFg=fg.value("state",std::string{})=="late",activeFg=fg.value("state",std::string{})=="active";
   AnythingMuted(fgMessages[page.comparison!=0?1:!state.active?2:!availableFg?3:lateFg?4:!activeFg?5:6]);
  }
  ImGui::SetCursorPosY(fgStatusY+fgStatusHeight+ImGui::GetStyle().ItemSpacing.y);
  ImGui::BeginDisabled(processingBlocked);
  ImGui::BeginDisabled(!page.neuralRendering);
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.nr_style_ecdbf958", "NR style"));
  const int looks[]={0,2,3};const float styleWidth=ImGui::GetContentRegionAvail().x,styleGap=ImGui::GetStyle().ItemSpacing.x;
  float minimumStyleWidth=0;for(int look:looks)minimumStyleWidth=std::max(minimumStyleWidth,ImGui::CalcTextSize(Neurotic::Translate(AnythingLooks[look]).c_str()).x+ImGui::GetStyle().FramePadding.x*2);
  const int styleColumns=std::clamp(int((styleWidth+styleGap)/(minimumStyleWidth+styleGap)),1,3);
  const float segment=(styleWidth-(styleColumns-1)*styleGap)/styleColumns;
  for(int i=0;i<3;++i){if(i%styleColumns)ImGui::SameLine();ImGui::BeginDisabled(i>0&&state.connected&&!capability("modelStyles"));if(AnythingSegment(AnythingLooks[looks[i]],page.look==looks[i]||(page.look==1&&i==0),segment,dpi)){page.look=looks[i];changed=true;}ImGui::EndDisabled();}
  ImGui::Dummy({0,5*dpi});
  changed|=AnythingStrength(Neurotic::UiLiteral("desktop.anythingview.detail_strength_9908dd0e", "Detail strength"),page.transfer,Neurotic::UiLiteral("desktop.anythingview.adjusts_the_detail_contribution_from_nr_7fdabf64", "Adjusts the detail contribution from NR."),dpi);
  changed|=AnythingStrength(Neurotic::UiLiteral("desktop.anythingview.color_strength_8cc291c4", "Color strength"),page.colour,Neurotic::UiLiteral("desktop.anythingview.adjusts_the_color_contribution_from_nr_bf325926", "Adjusts the color contribution from NR."),dpi);
  ImGui::EndDisabled();ImGui::EndDisabled();if(changed)applyProcessing();
  AnythingMuted(state.active?(capability(Neurotic::UiLiteral("desktop.anythingview.liveprocessing_ac48aaa6", "liveProcessing"))?Neurotic::UiLiteral("desktop.anythingview.changes_apply_at_completed_frame_boundaries_30bb0d89", "Changes apply at completed-frame boundaries."):Neurotic::UiLiteral("desktop.anythingview.this_worker_requires_stop_before_changing_the_lo_ff67f9bd", "This worker requires Stop before changing the look.")):Neurotic::UiLiteral("desktop.anythingview.your_look_will_be_used_when_rendering_starts_0591f7bb", "Your look will be used when rendering starts."));
  if(page.look==1)AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.neutral_standard_with_both_transfer_strengths_at_daaebef8", "Neutral: Standard with both transfer strengths at zero."));
  AnythingRule(dpi);if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.advanced_look_3e5e42ec", "Advanced look...")))openDialog=Neurotic::UiLiteral("desktop.anythingview.advanced_nr_anything_5d1d237f", "Advanced NR Anything");
  EndAnythingCard();
 };
 auto processingCard=[&]{
  AnythingCard(Neurotic::UiLiteral("desktop.anythingview.anythingprocessingcard_514c4208", "AnythingProcessingCard"),Neurotic::UiLiteral("desktop.anything.processing_heading","Processing"),cardWidth,dpi,model.light);
 ImGui::PushStyleColor(ImGuiCol_Text,page.deficit?ImVec4(1.f,.08f,.08f,1):healthy?ui::StatusColor(ui::ReadyState::Ready,model.light):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.processing_s_29541e6b", "Processing: %s"),Neurotic::Translate(page.deficit?Neurotic::UiLiteral("desktop.anythingview.output_is_behind_incoming_capture_dcd006c7", "Output is behind incoming capture."):!state.connected?Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800", "Unavailable"):state.phase==Neurotic::UiLiteral("desktop.anythingview.countdown_58c4583f", "Countdown")?Neurotic::UiLiteral("desktop.anythingview.waiting_for_countdown_9fe282f0", "Waiting for countdown"):processingPhase==Neurotic::UiLiteral("desktop.anythingview.pending_399dd91d", "pending")||processingPhase==Neurotic::UiLiteral("desktop.anythingview.awaiting_frame_dc1e21a1", "awaiting-frame")?Neurotic::UiLiteral("desktop.anythingview.applying_settings_on_the_next_completed_frame_7421b684", "Applying settings on the next completed frame..."):state.phase==Neurotic::UiLiteral("desktop.anythingview.starting_7e573dfb", "Starting")?Neurotic::UiLiteral("desktop.anythingview.starting_7e573dfb", "Starting"):state.phase==Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error")?Neurotic::UiLiteral("desktop.anythingview.error_see_session_details_08445823", "Error - see session details"):state.phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")?(state.status.value(Neurotic::UiLiteral("desktop.anythingview.sourcepaused_96a59408", "sourcePaused"),false)?Neurotic::UiLiteral("desktop.anythingview.paused_source_unavailable_5dd42200", "Paused - source unavailable"):healthy?Neurotic::UiLiteral("desktop.anythingview.keeping_up_754e40fa", "Keeping up"):page.ratesKnown?Neurotic::UiLiteral("desktop.anythingview.measuring_output_ae29bbc6", "Measuring output"):Neurotic::UiLiteral("desktop.anythingview.running_rates_unavailable_cfe0008e", "Running - rates unavailable")):Neurotic::UiLiteral("desktop.anythingview.stopped_6c16cb30", "Stopped")).c_str());ImGui::PopStyleColor();
 if(page.ratesKnown)ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.incoming_1f_s_output_1f_s_64cf8be1", "Incoming: %.1f/s   Output: %.1f/s"),page.incomingFps,page.outputFps);
 else ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.incoming_unavailable_output_unavailable_8f2cd24d", "Incoming: Unavailable   Output: Unavailable"));
 if(state.status.value(Neurotic::UiLiteral("desktop.anythingview.workwidth_d258bdc6", "workWidth"),0u))ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.neural_rendering_size_u_x_u_6081622f", "Neural Rendering size: %u x %u"),state.status.value(Neurotic::UiLiteral("desktop.anythingview.workwidth_d258bdc6", "workWidth"),0u),state.status.value(Neurotic::UiLiteral("desktop.anythingview.workheight_6220a8cc", "workHeight"),0u));
 else ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.neural_rendering_size_unavailable_8d2b5e5e", "Neural Rendering size: Unavailable"));
 if(state.status.value(Neurotic::UiLiteral("desktop.anythingview.outputwidth_b32d54f2", "outputWidth"),0u))ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.output_resolution_u_x_u_4cca2ecf", "Output resolution: %u x %u"),state.status.value(Neurotic::UiLiteral("desktop.anythingview.outputwidth_b32d54f2", "outputWidth"),0u),state.status.value(Neurotic::UiLiteral("desktop.anythingview.outputheight_0aed7a99", "outputHeight"),0u));
 else ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.output_resolution_unavailable_ef61cd5d", "Output resolution: Unavailable"));
  AnythingRule(dpi);
  ImGui::BeginDisabled(editing);
  const bool stagesKnown=state.connected&&state.status.contains("capabilities")&&state.status["capabilities"].is_object();
  const bool stagesChecking=!state.connected&&state.busy&&state.phase=="Connecting";
  const char* availabilityNotes[]={Neurotic::UiLiteral("desktop.anything.stage_unavailable_package","Unavailable in this package."),Neurotic::UiLiteral("desktop.anything.stage_required_package","Required by this package."),Neurotic::UiLiteral("desktop.anything.stage_checking","Checking availability..."),Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800","Unavailable"),Neurotic::UiLiteral("desktop.anything.stage_stop","Stop to change.")};
  auto availability=[&](bool supported,bool required){
   const float y=ImGui::GetCursorPosY(),width=ImGui::GetContentRegionAvail().x;float height=ImGui::GetTextLineHeight();
   for(const char* note:availabilityNotes)height=std::max(height,ImGui::CalcTextSize(Neurotic::Translate(note).c_str(),nullptr,false,width).y);
   if(!stagesKnown)AnythingMuted(availabilityNotes[stagesChecking?2:3]);
   else if(!supported)AnythingMuted(availabilityNotes[required?1:0]);
   else if(editing)AnythingMuted(availabilityNotes[4]);
   ImGui::SetCursorPosY(y+height+ImGui::GetStyle().ItemSpacing.y);
  };
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anything.sr_label","Super resolution"));
  int sr=page.superResolution?1:0;ImGui::SetNextItemWidth(-1);
  ImGui::BeginDisabled(!stagesKnown);
  const char* srOptions[]={Neurotic::UiLiteral("desktop.anything.fg_off","Off"),Neurotic::UiLiteral("desktop.anything.sr_fsr1","FSR 1")};
  if(ImGui::BeginCombo("##AnythingSR",srOptions[sr])){for(int option=0;option<2;++option){ImGui::BeginDisabled(option==1&&!capability("spatialSuperResolution"));const auto choice=Neurotic::Translate(srOptions[option])+"##AnythingSRChoice";if(ImGui::Selectable(choice.c_str(),option==sr)){page.superResolution=option==1;if(!page.superResolution)page.fullscreen=false;save();}ImGui::EndDisabled();}ImGui::EndCombo();}
  ImGui::EndDisabled();
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",Neurotic::Translate(Neurotic::UiLiteral("desktop.anything.sr_help","Spatial upscaling includes the captured HUD. Lower the resolution in the game's settings to reduce game rendering work.")).c_str());
  ImGui::EndDisabled();
  availability(capability("spatialSuperResolution"),false);
  ImGui::BeginDisabled(editing);
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anything.sr_neural_rendering","Neural rendering"));
  int nr=page.neuralRendering?1:0;ImGui::SetNextItemWidth(-1);
  ImGui::BeginDisabled(!stagesKnown);
  const char* nrOptions[]={Neurotic::UiLiteral("desktop.anything.fg_off","Off"),Neurotic::UiLiteral("desktop.anything.sr_on","On")};
  if(ImGui::BeginCombo("##AnythingNR",nrOptions[nr])){for(int option=0;option<2;++option){ImGui::BeginDisabled(option==0&&!capability("optionalNeuralRendering"));const auto choice=Neurotic::Translate(nrOptions[option])+"##AnythingNRChoice";if(ImGui::Selectable(choice.c_str(),option==nr)){page.neuralRendering=option==1;save();}ImGui::EndDisabled();}ImGui::EndCombo();}
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  availability(capability("optionalNeuralRendering"),true);
  ImGui::BeginDisabled(editing);
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anything.sr_output","Output"));ImGui::SetNextItemWidth(-1);
  const char* outputs[]={Neurotic::UiLiteral("desktop.anything.sr_overlay","Source overlay"),Neurotic::UiLiteral("desktop.anything.sr_preview","Preview"),Neurotic::UiLiteral("desktop.anything.sr_fullscreen","Fullscreen")};
  if(ImGui::BeginCombo("##AnythingOutput",outputs[page.fullscreen?2:page.overlay?0:1])){
   for(int mode=0;mode<3;++mode){ImGui::BeginDisabled(mode==2&&(!page.superResolution||(state.connected&&!capability("fullscreenSuperResolution"))));if(ImGui::Selectable(outputs[mode],mode==(page.fullscreen?2:page.overlay?0:1))){page.fullscreen=mode==2;page.overlay=mode!=1;save();}ImGui::EndDisabled();}
   ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  const char* stageNotes[]={Neurotic::UiLiteral("desktop.anything.stage_restart","Stop to change supported stages or output."),Neurotic::UiLiteral("desktop.anything.sr_pointer","Source view for pointer control."),Neurotic::UiLiteral("desktop.anything.sr_fullscreen_help","Fullscreen requires hidden, confined game input.")};
  const auto stageY=ImGui::GetCursorPosY();float stageHeight=ImGui::GetTextLineHeight();for(const char* note:stageNotes)stageHeight=std::max(stageHeight,ImGui::CalcTextSize(Neurotic::Translate(note).c_str(),nullptr,false,ImGui::GetContentRegionAvail().x).y);
  if(state.status.value("interactionReason",std::string{})=="fullscreen-pointer-source-view")AnythingMuted(stageNotes[1]);
  else if(editing&&stagesKnown)AnythingMuted(stageNotes[0]);
  else if(page.fullscreen&&stagesKnown&&capability("fullscreenSuperResolution"))AnythingMuted(stageNotes[2]);
  ImGui::SetCursorPosY(stageY+stageHeight+ImGui::GetStyle().ItemSpacing.y);
  const auto srStatus=state.status.value("superResolution",Json::object()),nrStatus=state.status.value("neuralRendering",Json::object());
  const bool srActive=running&&srStatus.value("effective",std::string("off"))=="fsr1"&&srStatus.value("state",std::string{})=="active";
  const bool nrActive=running&&(nrStatus.value("effective",false)||(!state.status.contains("neuralRendering")&&Count(state.status,"nrCompleted")>0));
  const bool srWaiting=state.active&&page.superResolution&&srStatus.value("state",std::string{})!="source-size";
  ui::StatusDot(srActive?ui::ReadyState::Ready:srWaiting?ui::ReadyState::Attention:ui::ReadyState::Checking,model.light,dpi);
  ImGui::TextDisabled("%s",Neurotic::Translate(srActive?Neurotic::UiLiteral("desktop.anything.sr_active","SR: Active"):srWaiting?Neurotic::UiLiteral("desktop.anything.sr_waiting","SR: Waiting"):Neurotic::UiLiteral("desktop.anything.sr_off","SR: Off")).c_str());
  if(ImGui::IsItemHovered()&&srStatus.contains("reason"))ImGui::SetTooltip("%s",srStatus.value("reason",std::string{}).c_str());
  ui::StatusDot(nrActive?ui::ReadyState::Ready:state.active&&page.neuralRendering?ui::ReadyState::Attention:ui::ReadyState::Checking,model.light,dpi);
  ImGui::TextDisabled("%s",Neurotic::Translate(nrActive?Neurotic::UiLiteral("desktop.anything.sr_nr_active","NR: Active"):state.active&&page.neuralRendering?Neurotic::UiLiteral("desktop.anything.sr_nr_waiting","NR: Waiting"):Neurotic::UiLiteral("desktop.anything.sr_nr_off","NR: Off")).c_str());
  if(state.status.value("captureWidth",0u))ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anything.sr_capture_size","Captured: %u x %u"),state.status.value("captureWidth",0u),state.status.value("captureHeight",0u));
  else ImGui::TextDisabled("%s",Neurotic::Translate(Neurotic::UiLiteral("desktop.anything.sr_capture_unknown","Captured: Unavailable")).c_str());
  EndAnythingCard();
 };
 auto targetCard=[&]{
  const char* heading=Neurotic::UiLiteral("desktop.anythingview.targeted_window_4e518b23", "Targeted Window");
  AnythingCard(Neurotic::UiLiteral("desktop.anythingview.anythingtargetcard_e86dfb4a", "AnythingTargetCard"),heading,cardWidth,dpi,model.light,false);
  const auto origin=ImGui::GetCursorScreenPos();const float available=ImGui::GetContentRegionAvail().x;
  const float spacing=ImGui::GetStyle().ItemSpacing.x;
  const auto clearLabel=Neurotic::Translate(Neurotic::UiLiteral("desktop.anythingview.clear_target_1d772e62", "Clear target"))+"###ClearAnythingTarget";
  const float clearWidth=std::max(100*dpi,ImGui::CalcTextSize(clearLabel.c_str(),nullptr,true).x+ImGui::GetStyle().FramePadding.x*2);
  const float leftWidth=std::max(1.f,available-clearWidth-spacing);
  const float headerHeight=std::max(ImGui::GetFrameHeight(),26*dpi);
  ImGui::PushFont(nullptr,26);
  const auto title=Neurotic::Translate(heading);ImGui::Dummy({available,headerHeight});
  ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{origin.x,origin.y+(headerHeight-ImGui::GetTextLineHeight())*.5f},{origin.x+available,origin.y+headerHeight},origin.x+available,title.c_str(),nullptr,nullptr);
  if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",title.c_str());
  if(ImGui::GetCurrentContext()->LogEnabled)ImGui::LogRenderedText(&origin,title.c_str());ImGui::PopFont();
  const auto targetIdentity=page.clearTargetPending?Json():state.active?(state.status.value("window",Json()).is_object()?state.status["window"]:page.activeTarget):page.selected;
  const float infoHeight=std::max(ImGui::GetTextLineHeight(),24*dpi);
  ImGui::SetCursorScreenPos({origin.x,origin.y+headerHeight+ImGui::GetStyle().ItemSpacing.y});
  ImGui::BeginGroup();ArtworkView icon;
  if(targetIdentity.is_object()&&model.artwork){Game target;target.id="anything";target.target.path=targetIdentity.value("executable",std::string{});icon=model.artwork->Get(target,"icon",false);}
  if(icon){ImGui::Image((ImTextureID)icon.texture,{24*dpi,24*dpi});ImGui::SameLine();}
  const auto targetTitle=targetIdentity.is_object()?Neurotic::Translate(targetIdentity.value("title",std::string(Neurotic::UiLiteral("desktop.anythingview.selected_window_a4149a3d", "Selected window"))).c_str()):Neurotic::Translate(Neurotic::UiLiteral("desktop.anythingview.no_window_selected_e8612462", "No window selected."));
  const auto titleMin=ImGui::GetCursorScreenPos();const float targetWidth=std::max(1.f,leftWidth-(icon?24*dpi+spacing:0.f));
  ImGui::Dummy({targetWidth,infoHeight});
  ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{titleMin.x,titleMin.y+(infoHeight-ImGui::GetTextLineHeight())*.5f},{titleMin.x+targetWidth,titleMin.y+infoHeight},titleMin.x+targetWidth,targetTitle.c_str(),nullptr,nullptr);
  if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",targetTitle.c_str());
  if(ImGui::GetCurrentContext()->LogEnabled)ImGui::LogRenderedText(&titleMin,targetTitle.c_str());
  ImGui::EndGroup();
#ifdef IMGUI_ENABLE_TEST_ENGINE
  {auto& g=*ImGui::GetCurrentContext();IMGUI_TEST_ENGINE_ITEM_INFO(0,"##TargetInformation",0);}
#endif
  const float clearHeight=std::max(infoHeight,ImGui::GetFrameHeight());
  if(targetIdentity.is_object()){
   ImGui::SetCursorScreenPos({origin.x+leftWidth+spacing,origin.y+headerHeight+ImGui::GetStyle().ItemSpacing.y});
   ImGui::BeginDisabled(state.stopping||page.clearTargetPending);
   if(ui::Button(clearLabel.c_str(),{clearWidth,clearHeight})){
    if(model.anything)StopAnythingTarget(page,*model.anything,state,true);
    else if(ClearIdleAnythingTarget(page,false,false,false))page.notice.clear();
   }
   ImGui::EndDisabled();
   if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.anythingview.stop_and_clear_target", "Stop and clear target"));
  }
  ImGui::SetCursorScreenPos({origin.x,origin.y+headerHeight+ImGui::GetStyle().ItemSpacing.y+clearHeight+ImGui::GetStyle().ItemSpacing.y});ImGui::Dummy({available,0});
  EndAnythingCard();
 };
 auto compareCard=[&]{
  AnythingCard(Neurotic::UiLiteral("desktop.anythingview.anythingcomparecard_808636f7", "AnythingCompareCard"),Neurotic::UiLiteral("desktop.anythingview.compare_and_capture_e6edc2ac", "Compare and capture"),cardWidth,dpi,model.light);
  ImGui::BeginDisabled(state.stopping);const char* modes[]={Neurotic::UiLiteral("desktop.settings.v-sync/forcevsync/value.1300117561", "On"),Neurotic::UiLiteral("desktop.settings.dlssg/nativemfgexperimental/value.ca7981b46e", "Off"),Neurotic::UiLiteral("desktop.settings.dlssnr/comparesplit.32afaa7843", "Split"),Neurotic::UiLiteral("desktop.option.a0102468a229", "Stripes")};
  const float segment=(ImGui::GetContentRegionAvail().x-3*ImGui::GetStyle().ItemSpacing.x)/4;
  for(int i=0;i<4;++i){if(i)ImGui::SameLine();ImGui::BeginDisabled(i==3&&state.connected&&!capability(Neurotic::UiLiteral("desktop.anythingview.comparisonstripes_8c9c62e7", "comparisonStripes")));if(AnythingSegment(modes[i],page.comparison==i,segment,dpi)){page.comparison=i;if(i==2&&page.split<=0)page.split=.5f;if(model.anything)model.anything->SetComparison(i==1?1.f:i==2?page.split:0.f,i==3?page.stripes:0,page.comparisonDirection);}ImGui::EndDisabled();}
  ImGui::Dummy({0,9*dpi});ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.s_output_selected_d96fea78", "%s output selected"),Neurotic::Translate(modes[std::clamp(page.comparison,0,3)]).c_str());
  ImGui::BeginDisabled(page.comparison!=2||(state.connected&&!capability(Neurotic::UiLiteral("desktop.anythingview.comparisondirections_d1a35c7e", "comparisonDirections"))));ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.off_side_82db0d24", "Off side"));ImGui::SetNextItemWidth(-1);
  if(ImGui::Combo("##ComparisonDirection",&page.comparisonDirection,Neurotic::UiOptions("desktop.anythingview.left_41ae8217|desktop.anythingview.right_485d28af|desktop.anythingview.top_61c958d9|desktop.anythingview.bottom_7bc531ab|desktop.anythingview.top_left_5e030ac9|desktop.anythingview.top_right_e0fd8bbc|desktop.anythingview.bottom_left_0d0bc074|desktop.anythingview.bottom_right_9274ed97|", "Left\0Right\0Top\0Bottom\0Top-left\0Top-right\0Bottom-left\0Bottom-right\0"))&&model.anything)model.anything->SetComparison(page.split,0,page.comparisonDirection);ImGui::EndDisabled();
  ImGui::BeginDisabled(page.comparison!=2);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.comparison_position_fe332592", "Comparison position"));ImGui::SameLine(std::max(ImGui::GetCursorPosX(),(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x)-ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset")).x-ImGui::GetStyle().FramePadding.x*2));if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset##SplitReset"))){page.split=.5f;if(model.anything)model.anything->SetComparison(page.split,0,page.comparisonDirection);}ImGui::SetNextItemWidth(-1);
  if(ImGui::SliderFloat("##SplitPosition",&page.split,0,1,"%.2f")&&model.anything)model.anything->SetComparison(page.split,0,page.comparisonDirection);ImGui::EndDisabled();
  ImGui::BeginDisabled(page.comparison!=3||(state.connected&&!capability(Neurotic::UiLiteral("desktop.anythingview.comparisonstripes_8c9c62e7", "comparisonStripes"))));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.stripe_count_7f505925", "Stripe count"));ImGui::SameLine(std::max(ImGui::GetCursorPosX(),(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x)-ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset")).x-ImGui::GetStyle().FramePadding.x*2));if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset##StripesReset"))){page.stripes=6;if(model.anything)model.anything->SetComparison(0,page.stripes,page.comparisonDirection);}ImGui::SetNextItemWidth(-1);
  if(ImGui::SliderInt("##StripeCount",&page.stripes,2,32)&&model.anything)model.anything->SetComparison(0,page.stripes,page.comparisonDirection);ImGui::EndDisabled();ImGui::EndDisabled();
  AnythingRule(dpi);ImGui::PushFont(nullptr,26);ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.capture_452b69c6", "Capture"));ImGui::PopFont();
  ImGui::BeginDisabled();ImGui::SetNextItemWidth(-1);int currentOutput=0;ImGui::Combo("##CaptureOutput",&currentOutput,Neurotic::UiOptions("desktop.anythingview.current_comparison_3810247a|", "Current comparison\0"));ImGui::EndDisabled();
  ImGui::Dummy({0,5*dpi});
  ImGui::BeginDisabled(!CanCaptureAnythingScreenshot(state));
  if(ui::ActionButton(Neurotic::UiLiteral("desktop.anythingview.capture_452b69c6", "Capture"),ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive),{ImGui::GetContentRegionAvail().x,35*dpi}))CaptureAnythingScreenshot(model);
  ImGui::EndDisabled();AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.full_output_size_lossless_png_current_completed__d9ccdcdc", "Full output size - lossless PNG - current completed comparison"));
  auto shortcut=Neurotic::UiLiteral("desktop.anythingview.screenshot_shortcut_77140974", "Screenshot shortcut: ")+AnythingHotkeyText(page.screenshotHotkeyModifiers,page.screenshotHotkeyKey);
  if(!page.screenshotHotkeyEnabled)shortcut+=" (disabled)";else if(!page.screenshotHotkeyError.empty())shortcut+=Neurotic::UiLiteral("desktop.anythingview.unavailable_see_key_bindings_723acbc8", " (unavailable - see Key bindings)");else if(!page.screenshotHotkeyAvailable)shortcut+=" (registering)";
  AnythingMuted(shortcut.c_str());
  const auto snapshot=state.status.value("snapshot",Json::object());auto snapshotState=snapshot.value("state",std::string{});
  if(snapshotState==Neurotic::UiLiteral("desktop.anythingview.pending_399dd91d", "pending"))AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.waiting_for_a_completed_output_frame_b135f908", "Waiting for a completed output frame..."));
  else if(snapshotState==Neurotic::UiLiteral("desktop.anythingview.saved_b552945a", "saved"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.saved_s_b41a0196", "Saved: %s"),Neurotic::Translate(snapshot.value(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),std::string{}).c_str()).c_str());
  else if(snapshotState==Neurotic::UiLiteral("desktop.anythingview.failed_2fc133f5", "failed")||snapshotState==Neurotic::UiLiteral("desktop.anythingview.canceled_75aabc0c", "canceled"))ImGui::TextWrapped("%s",Neurotic::Translate(snapshot.value(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),std::string{}).c_str()).c_str());
  AnythingRule(dpi);ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.snapshot_folder_187e0c3b", "Snapshot folder"));AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.localappdata_neurotic_snapshots_78ead4fb", "%LOCALAPPDATA%\\NeuRotic\\Snapshots"));
  if(ui::IconButton(Neurotic::UiLiteral("desktop.anythingview.open_snapshot_folder_867bcf1e", "Open snapshot folder"),0,0,dpi)){try{nrw::SnapshotFolderLease folder;auto result=ShellExecuteW(nullptr,L"open",folder.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(INT_PTR(result)<=32)page.notice=Neurotic::UiMessage("desktop.anythingview.snapshots_folder_could_not_be_opened_3c9f8a29", "Snapshots folder could not be opened.");}catch(const std::exception& e){page.notice=e.what();}}
  AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.on_and_off_use_the_same_captured_frame_changes_a_3497e59c", "On and Off use the same captured frame. Changes appear on the next completed frame."));
  EndAnythingCard();
 };
 auto unavailableCard=[&](const char* id,const char* title){AnythingCard(id,title,cardWidth,dpi,model.light);AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.currently_unavailable_for_nr_anything_2c7fdcd4", "Currently unavailable for NR Anything."));EndAnythingCard();};
 if(wide){ImGui::BeginGroup();processingCard();ImGui::Dummy({0,8*dpi});compareCard();ImGui::EndGroup();ImGui::SameLine(0,gap);ImGui::BeginGroup();targetCard();ImGui::Dummy({0,8*dpi});lookCard();ImGui::Dummy({0,8*dpi});unavailableCard("AnythingMultipassCard","Multipass");ImGui::EndGroup();}
 else{targetCard();ImGui::Dummy({0,8*dpi});processingCard();ImGui::Dummy({0,8*dpi});lookCard();ImGui::Dummy({0,8*dpi});compareCard();ImGui::Dummy({0,8*dpi});unavailableCard("AnythingMultipassCard","Multipass");}
 AnythingRule(dpi);if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.key_bindings_57a0996e", "Key bindings...")))openDialog=Neurotic::UiLiteral("desktop.anythingview.nr_anything_key_bindings_8ec80ae6", "NR Anything key bindings");ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.diagnostics_945a99c5", "Diagnostics...")))openDialog=Neurotic::UiLiteral("desktop.anythingview.nr_anything_diagnostics_18e0216a", "NR Anything diagnostics");
 if(page.hotkeyEnabled&&!page.hotkeyError.empty())ImGui::TextWrapped("%s",Neurotic::Translate(page.hotkeyError.c_str()).c_str());
 AnythingRule(dpi);AnythingMuted(state.message.c_str());if(!page.notice.empty())ImGui::TextWrapped("%s",Neurotic::Translate(page.notice.c_str()).c_str());
 if(openDialog)ImGui::OpenPopup(openDialog);
 if(AnythingDialog(Neurotic::UiLiteral("desktop.anythingview.nr_anything_profiles_fdecfc2f", "NR Anything profiles"),dpi)){DialogBody();ImGui::BeginDisabled(processingBlocked);
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.profile_name_0b06e1d2", "Profile name"));ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##ProfileName",Neurotic::UiLiteral("desktop.anythingview.name_your_current_settings_0efe6009", "Name your current settings"),page.profileName.data(),page.profileName.size());
  const auto name=std::string(page.profileName.data());const bool updating=page.profiles.contains(name)&&!ProtectedAnythingProfile(name);
  ImGui::BeginDisabled(!CanSaveAnythingProfile(page,name));
  if(ui::Button(updating?Neurotic::UiLiteral("desktop.anythingview.update_profile_a2332add", "Update profile"):Neurotic::UiLiteral("desktop.anythingview.save_profile_1d4c8487", "Save profile"))){if(SaveAnythingUserProfile(page,name)){page.selectedUserProfile=name;save();}}ImGui::EndDisabled();
  ImGui::SameLine();ImGui::BeginDisabled(page.selectedUserProfile.empty()||ProtectedAnythingProfile(page.selectedUserProfile)||!page.profiles.contains(page.selectedUserProfile));
  if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.delete_profile_c9f957ce", "Delete profile"))){if(DeleteAnythingUserProfile(page,page.selectedUserProfile)){page.selectedUserProfile.clear();page.profileName.fill(0);save();}}ImGui::EndDisabled();
  ImGui::Separator();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.anythingview.your_profiles_14e06822", "Your profiles"));
  if(ImGui::BeginListBox("##UserProfiles",{-1,140*dpi})){
   for(const auto& item:page.profiles.items())if(!ProtectedAnythingProfile(item.key())&&ImGui::Selectable(item.key().c_str(),page.selectedUserProfile==item.key())){
    page.selectedUserProfile=item.key();snprintf(page.profileName.data(),page.profileName.size(),"%s",item.key().c_str());
   }
   if(page.profiles.empty())ImGui::TextDisabled(Neurotic::UiLiteral("desktop.anythingview.no_saved_profiles_yet_28c400b6", "No saved profiles yet."));ImGui::EndListBox();
  }
  ImGui::EndDisabled();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.select_a_saved_profile_to_update_it_with_your_cu_86a3d322", "Select a saved profile to update it with your current settings or delete it. Built-in profiles are protected. Load profiles from the page's Profile dropdown."));EndAnythingDialog();
 }
 if(AnythingDialog(Neurotic::UiLiteral("desktop.anythingview.nr_anything_key_bindings_8ec80ae6", "NR Anything key bindings"),dpi)){DialogBody();
  auto binding=[&](const char* title,bool& enabled,unsigned& modifiersValue,unsigned& keyValue,unsigned& revision,bool& available,const std::string& error,char defaultKey){
   ImGui::PushID(title);ImGui::TextUnformatted(title);bool changed=ui::Toggle(Neurotic::UiLiteral("desktop.anythingview.enable_global_shortcut_248d8df1", "Enable global shortcut"),&enabled);
   const unsigned modifiers[]={MOD_CONTROL|MOD_ALT,MOD_CONTROL|MOD_SHIFT,MOD_ALT|MOD_SHIFT,MOD_CONTROL|MOD_ALT|MOD_SHIFT};int modifier=0;for(int i=0;i<4;++i)if(modifiersValue==modifiers[i])modifier=i;
   ImGui::SetNextItemWidth(230*dpi);if(ImGui::Combo(Neurotic::UiLiteral("desktop.anythingview.modifiers_976c21b9", "Modifiers"),&modifier,Neurotic::UiOptions("desktop.anythingview.ctrl_alt_0c6439b2|desktop.anythingview.ctrl_shift_c667546e|desktop.anythingview.alt_shift_25912955|desktop.anythingview.ctrl_alt_shift_b3e6fe08|", "Ctrl + Alt\0Ctrl + Shift\0Alt + Shift\0Ctrl + Alt + Shift\0"))){modifiersValue=modifiers[modifier];if(!ValidAnythingHotkey(modifiersValue,keyValue))keyValue=defaultKey;changed=true;}
   auto keyName=[](unsigned key){return key>=VK_F1?"F"+std::to_string(key-VK_F1+1):std::string(1,char(key));};ImGui::SetNextItemWidth(230*dpi);
   if(ImGui::BeginCombo(Neurotic::UiLiteral("desktop.anythingview.key_df9dc9c4", "Key"),keyName(keyValue).c_str())){for(unsigned key='A';key<='Z';++key)if(ImGui::Selectable(keyName(key).c_str(),keyValue==key)){keyValue=key;changed=true;}for(unsigned key=VK_F1;key<=VK_F11;++key){ImGui::BeginDisabled(!ValidAnythingHotkey(modifiersValue,key));if(ImGui::Selectable(keyName(key).c_str(),keyValue==key)){keyValue=key;changed=true;}ImGui::EndDisabled();}ImGui::EndCombo();}
   if(changed){available=false;++revision;save();}
   ImGui::TextWrapped("%s",Neurotic::Translate(!enabled?Neurotic::UiLiteral("desktop.anythingview.shortcut_disabled_207c8974", "Shortcut disabled."):!error.empty()?error.c_str():available?Neurotic::UiLiteral("desktop.anythingview.shortcut_registered_e3154776", "Shortcut registered."):Neurotic::UiLiteral("desktop.anythingview.waiting_for_shortcut_registration_f57dfabc", "Waiting for shortcut registration...")).c_str());
   if(enabled&&!error.empty()&&ui::Button(Neurotic::UiLiteral("desktop.anythingview.retry_registration_d317a9ad", "Retry registration")))++revision;ImGui::PopID();
  };
  binding(Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering"),page.hotkeyEnabled,page.hotkeyModifiers,page.hotkeyKey,page.hotkeyRevision,page.hotkeyAvailable,page.hotkeyError,'N');
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.starts_only_the_explicitly_selected_window_or_st_ec724db4", "Starts only the explicitly selected window, or stops the active session. It never selects the foreground window."));ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
  binding("Screenshot",page.screenshotHotkeyEnabled,page.screenshotHotkeyModifiers,page.screenshotHotkeyKey,page.screenshotHotkeyRevision,page.screenshotHotkeyAvailable,page.screenshotHotkeyError,'S');
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.captures_the_running_session_s_current_compariso_31c0c751", "Captures the running session's current comparison output without switching apps. Wait for an active session and any pending screenshot to finish. Ctrl+Alt+F10 remains the emergency Stop shortcut."));EndAnythingDialog();
 }
 if(AnythingDialog(Neurotic::UiLiteral("desktop.anythingview.advanced_nr_anything_5d1d237f", "Advanced NR Anything"),dpi)){DialogBody();ImGui::BeginDisabled(editing);
 ImGui::BeginDisabled(page.overlay||page.fullscreen);ui::Toggle(Neurotic::UiLiteral("desktop.anythingview.include_cursor_in_preview_e4b9b9b7", "Include cursor in preview"),&page.cursor);ImGui::EndDisabled();ImGui::EndDisabled();EndAnythingDialog();}
 if(AnythingDialog(Neurotic::UiLiteral("desktop.anythingview.nr_anything_diagnostics_18e0216a", "NR Anything diagnostics"),dpi)){
  DialogBody();ImGui::SeparatorText(Neurotic::UiLiteral("desktop.anything.probe_title","One-frame measurement"));
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anything.probe_description","Check up to 256 samples from one frame. Results contain numbers only; no image is saved."));
  ImGui::TextWrapped("%s",Neurotic::Translate(Neurotic::UiLiteral("desktop.anything.probe_requirements","Requires NR, 100% resolution, SDR and Compare set to On.")).c_str());
  ImGui::BeginDisabled(model.showDataMaintenance||!AnythingCanMeasureFrame(state));
  if(ui::Button(Neurotic::UiLiteral("desktop.anything.probe_action","Measure one frame"))&&model.anything)model.anything->MeasureOneFrame();ImGui::EndDisabled();
  const auto measurement=state.status.value("measurement",Json::object());const auto measurementPhase=measurement.value("state",std::string{});
  if(!state.connected)AnythingMuted(Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800","Unavailable"));
  else if(!capability("oneFrameMeasurement"))AnythingMuted(Neurotic::UiLiteral("desktop.anything.stage_unavailable_package","Unavailable in this package."));
  else {
   const char* resultText=measurementPhase=="pending"?Neurotic::UiLiteral("desktop.anything.probe_pending","Measuring one frame..."):measurementPhase=="complete"?Neurotic::UiLiteral("desktop.anything.probe_complete","Measurement complete. Copy session status to share the four results."):measurementPhase=="unsupported"?Neurotic::UiLiteral("desktop.anything.probe_unsupported","Measurement unavailable."):measurementPhase=="invalidated"?Neurotic::UiLiteral("desktop.anything.probe_invalidated","Measurement canceled by a session change."):measurementPhase=="failed/unknown"?Neurotic::UiLiteral("desktop.anything.probe_unknown","Measurement failed. Restart the worker before trying again."):measurementPhase=="failed"?Neurotic::UiLiteral("desktop.anything.probe_failed","Measurement failed."):AnythingCanMeasureFrame(state)?Neurotic::UiLiteral("desktop.anything.probe_ready","Ready to measure."):Neurotic::UiLiteral("desktop.anything.probe_wait","Wait for rendering to be ready.");
   ImGui::TextWrapped("%s",Neurotic::Translate(resultText).c_str());
   const auto reason=measurement.value("reason",std::string{});
   if(!reason.empty()){
    const char* caption=reason.c_str();
    if(measurementPhase=="pending"&&measurement.value("appAcknowledgementPending",false)&&reason=="Waiting for the worker to acknowledge this measurement.")caption=Neurotic::UiLiteral("desktop.anything.probe_acknowledgement_pending","Waiting for the worker to acknowledge this measurement.");
    else if(measurementPhase=="invalidated"&&reason=="Measurement canceled by a session change.")caption=Neurotic::UiLiteral("desktop.anything.probe_invalidated","Measurement canceled by a session change.");
    ImGui::TextWrapped("%s",Neurotic::Translate(caption).c_str());
   }
  }
  ImGui::SeparatorText(Neurotic::UiLiteral("desktop.anythingview.selected_capture_and_output_7108a1a7", "Selected capture and output"));
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.input_s_4bb93ae2", "Input: %s"),Neurotic::Translate(identity.is_object()?identity.value(Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),std::string(Neurotic::UiLiteral("desktop.anythingview.selected_window_a4149a3d", "Selected window"))).c_str():Neurotic::UiLiteral("desktop.anythingview.no_window_selected_6670ad5a", "No window selected")).c_str());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.session_s_1de8419d", "Session: %s"),Neurotic::Translate(state.phase.c_str()).c_str());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.incoming_counts_windows_capture_events_output_co_4f586cc6", "Incoming counts Windows capture events; Output counts accepted presentations. Neither is unique source pictures or display scanout FPS. A warning appears when output stays below 90%% of incoming (at least 5/s) for 5 seconds."));
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.nr_anything_processes_captured_images_native_gam_0caf905f", "NR Anything processes captured images. Native game inputs and external Feeder observations remain unavailable on this route."));
  auto copiedStatus=state.status;if(measurementPhase=="complete"&&measurement.contains("summaries")&&measurement["summaries"].is_array()&&measurement["summaries"].size()==4)copiedStatus["measurement"]["boundaryOrder"]={"model-proxy","resolve-original","publication-resolve","comparison-publication"};
  ImGui::SeparatorText(Neurotic::UiLiteral("desktop.anythingview.session_evidence_cdc1d01d", "Session evidence"));auto details=Json{{Neurotic::UiLiteral("desktop.anythingview.appphase_db20fdde", "appPhase"),state.phase},{Neurotic::UiLiteral("desktop.anythingview.lasterror_811e3f6c", "lastError"),state.lastError},{Neurotic::UiLiteral("desktop.anythingview.forcedtermination_0ee1d4b0", "forcedTermination"),state.forcedTermination},{Neurotic::UiLiteral("desktop.anythingview.worker_02e82f3b", "worker"),std::move(copiedStatus)}}.dump(2);
  if(ui::Button(Neurotic::UiLiteral("desktop.anythingview.copy_session_status_c159b1a7", "Copy session status")))ImGui::SetClipboardText(details.c_str());ImGui::TextWrapped("%s",Neurotic::Translate(details.c_str()).c_str());EndAnythingDialog();
 }
}catch(const std::exception& error){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.anythingview.anything_could_not_be_opened_s_400a5852", "Anything could not be opened: %s"),Neurotic::Translate(error.what()).c_str());}
}
