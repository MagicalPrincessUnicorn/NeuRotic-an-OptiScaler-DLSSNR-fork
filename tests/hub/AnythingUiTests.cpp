#include "ui/HubViewModel.h"
#include "anything/AnythingView.h"
#include "anything/AnythingController.h"
#include "anything/AnythingPreferences.h"
#include "ui/ActionFont.h"
#include "artwork/ArtworkService.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
#include "../../OptiScaler/menu/Localization.h"
#include <iostream>
#include <cstdlib>
#include <map>
struct SeenItem{ImRect rect;ImGuiItemFlags flags=0;ImGuiID id=0;ImGuiWindow* window=nullptr;};
static std::map<std::string,SeenItem> seenItems;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx,ImGuiID id,const ImRect& rect,const ImGuiLastItemData* data){for(const char* label:{"##WindowDropdown","##Countdown","##NRAnythingControl","##AnythingFG"})if(ctx->CurrentWindow&&ctx->CurrentWindow->IDStack.Size>0&&id==ctx->CurrentWindow->GetID(label))seenItems[label]={rect,data?data->ItemFlags:ctx->LastItemData.ItemFlags,id,ctx->CurrentWindow};}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(ctx->LastItemData.ID==id)seenItems[label]={ctx->LastItemData.Rect,ctx->LastItemData.ItemFlags,id,ctx->CurrentWindow};}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}
// Inert desktop services: only the isolated catalog fixture is launched, never capture.
namespace nh {
ProcessJob::~ProcessJob(){}
void HubModel::Save(){}
static int modelFolderOpens=0;
void HubModel::OpenComponentFolder(bool streamline){if(!streamline)++modelFolderOpens;}
std::filesystem::path UserRoot(){return std::filesystem::current_path()/L"builds"/L"hub-anything"/L"ui-fixture";}
std::filesystem::path AppRoot(){return UserRoot();}
std::filesystem::path SharedRuntimeRoot(){return UserRoot()/L"Runtime";}
ArtworkView ArtworkService::Get(const Game&,const char*,bool){return {};}
}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1240,1600};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);nh::ui::AddActionFont(io.Fonts,16);
 nh::HubModel model;model.anythingUi.preferencesLoaded=true;model.loaded=false;ImVec2 fixtureSize{1040,1480};float testDpi=1;const auto baseStyle=ImGui::GetStyle();std::string renderedText;
 auto render=[&](const char* activate=nullptr){
  const auto priorId=activate&&seenItems.contains(activate)?seenItems[activate].id:0;
  const auto priorWindow=activate&&seenItems.contains(activate)?seenItems[activate].window:nullptr;
  GImGui->TestEngineHookItems=true;seenItems.clear();
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize(fixtureSize);ImGui::Begin("AnythingFixture");
  if(activate){ImGuiID id=0;const char* owner=(strstr(activate,"profile")||strstr(activate,"look"))?"AnythingLookCard":strstr(activate,"Comparison")?"AnythingCompareCard":"AnythingPage";
   if(!strcmp(activate,"Natural")||!strcmp(activate,"Cinematic"))owner="AnythingLookCard";
   if(strstr(activate,"Countdown")||strstr(activate,"ClearAnythingTarget"))owner="AnythingTargetCard";
   if(strstr(activate,"Reset##"))owner="AnythingCompareCard";
   for(auto w:GImGui->Windows)if(strstr(w->Name,owner)&&w->WasActive&&(strcmp(owner,"AnythingPage")||w->ParentWindow==ImGui::GetCurrentWindow()))id=w->GetID(activate);
   if(auto modal=ImGui::GetTopMostPopupModal();modal&&strstr(modal->Name,"NR Anything profiles"))for(auto w:GImGui->Windows)if(w->ParentWindow==modal&&w->WasActive)id=w->GetID(activate);
   if(priorId)id=priorId;if(priorWindow){GImGui->NavWindow=priorWindow;GImGui->NavId=id;}GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=id;}
  ImGui::LogToBuffer();nh::RenderAnythingAction(model,testDpi,202*testDpi);ImGui::SameLine();nh::RenderAnythingCountdown(model,testDpi);nh::RenderAnything(model,testDpi);renderedText=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();
 };
 render();render();render();render();auto host=ImGui::FindWindowByName("AnythingFixture");auto baseline=host->ContentSize;
 auto card=[](const char* name){for(auto w:GImGui->Windows)if(strstr(w->Name,name)&&w->Active)return w;return (ImGuiWindow*)nullptr;};
 auto look=card("AnythingLookCard"),processing=card("AnythingProcessingCard"),compare=card("AnythingCompareCard");
 auto headingHeight=[](ImGuiWindow* w){float top=FLT_MAX,bottom=-FLT_MAX;int vertices=0;auto color=ImGui::GetColorU32(ImGuiCol_Text);if(w)for(const auto& v:w->DrawList->VtxBuffer)if(v.col==color){top=std::min(top,v.pos.y);bottom=std::max(bottom,v.pos.y);if(++vertices==16)break;}return vertices==16?bottom-top:0.f;};
 const float logicalHeadingHeight=headingHeight(look);
 model.anythingUi.profile="My look";model.anythingUi.transfer=.4f;model.anythingUi.colour=.7f;
 render("Natural");
 check(model.anythingUi.look==2&&model.anythingUi.profile=="My look"&&model.anythingUi.transfer==.4f&&model.anythingUi.colour==.7f,"Rendering style changes preserve profile identity and strengths");
 check(nh::AnythingStartRequest(model.anythingUi).value("modelStyle",-1)==1,"Natural style maps to real worker enum on next start");
 render("Cinematic");check(nh::AnythingStartRequest(model.anythingUi).value("modelStyle",-1)==2,"Cinematic style maps to real worker enum on next start");
 check(look&&processing&&compare,"Approved page has Look, Processing, and Compare and capture cards");
 if(look&&processing&&compare){
  auto target=card("AnythingTargetCard");check(target&&target->Pos.x>=processing->Pos.x+processing->Size.x&&fabsf(target->Pos.y-processing->Pos.y)<1,"Wide layout aligns Targeted Window beside Processing");
  check(compare->Pos.y>=processing->Pos.y+processing->Size.y&&fabsf(processing->Pos.x-compare->Pos.x)<1,"Processing sits above Compare in the left column");
  check(target&&look->Pos.y>=target->Pos.y+target->Size.y&&fabsf(target->Pos.x-look->Pos.x)<1,"Targeted Window sits above Look in the right column");
 }
 check(!card("AnythingFgCard")&&card("AnythingMultipassCard")&&seenItems.contains("##AnythingFG")&&look&&look->InnerRect.Contains(seenItems.at("##AnythingFG").rect),"Frame generation control stays in Look and Multipass retains its separate unavailable card");
 check(renderedText.find("Use this to apply neural rendering to any window.")==std::string::npos&&!seenItems.contains("Manage")&&renderedText.find("NR Model Ready")==std::string::npos&&renderedText.find("No window selected.")!=std::string::npos&&renderedText.find("Processing: Unavailable")!=std::string::npos&&renderedText.find("Incoming: Unavailable")!=std::string::npos&&renderedText.find("Neural Rendering size: Unavailable")!=std::string::npos,"Requested guidance/model controls removed and empty target/processing states displayed");
 render("##NRAnythingControl");render();check(nh::modelFolderOpens==1&&!model.anything&&ImGui::GetTopMostPopupModal()==nullptr,"Missing-model Start opens folder directly without worker/rendering/setup modal");
 model.anythingUi.selected={{"title","Selected fixture window"},{"executable","fixture.exe"}};render();
 check(renderedText.find("Selected fixture window")!=std::string::npos&&renderedText.find("No window selected.")==std::string::npos,"Targeted Window shows the selected title instead of its empty state");model.anythingUi.selected=nh::Json();render();
 model.anythingUi.comparison=2;model.anythingUi.split=.9f;render();render("Reset##SplitReset");check(model.anythingUi.split==.5f,"Split Reset restores centered comparison");
 model.anythingUi.comparison=3;model.anythingUi.stripes=22;render();render("Reset##StripesReset");check(model.anythingUi.stripes==6,"Stripe Reset restores six stripes");
 for(int mode=0;mode<4;++mode){model.anythingUi.comparison=mode;render();check(renderedText.find("Comparison position")!=std::string::npos&&renderedText.find("Stripe count")!=std::string::npos,"Comparison position and stripe count remain visible in every mode");
  check(seenItems.contains("##SplitPosition")&&bool(seenItems["##SplitPosition"].flags&ImGuiItemFlags_Disabled)==(mode!=2)&&seenItems.contains("##StripeCount")&&bool(seenItems["##StripeCount"].flags&ImGuiItemFlags_Disabled)==(mode!=3),"Actual comparison slider items are disabled precisely outside their modes");
 }
 model.anythingUi.comparison=0;render();const auto splitBefore=model.anythingUi.split;const auto stripeBefore=model.anythingUi.stripes;
 for(const char* slider:{"##SplitPosition","##StripeCount"}){auto rect=seenItems[slider].rect;io.AddMousePosEvent(rect.Max.x-4,rect.GetCenter().y);io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();}
 check(model.anythingUi.split==splitBefore&&model.anythingUi.stripes==stripeBefore,"Clicking irrelevant comparison sliders cannot change settings");
 check(seenItems.contains("##WindowDropdown")&&seenItems.contains("##NRAnythingControl")&&fabsf(seenItems["##WindowDropdown"].rect.Min.x-seenItems["##NRAnythingControl"].rect.Max.x)<1&&fabsf(seenItems["##WindowDropdown"].rect.Min.y-seenItems["##NRAnythingControl"].rect.Min.y)<1,"Window arrow joins the main action as one split button");
 check(renderedText.find("Choose window")==std::string::npos&&seenItems.contains("##Countdown"),"Page removes duplicate window selector and preserves countdown");
 auto targetWindow=[&](){for(auto* w:GImGui->Windows)if(strstr(w->Name,"AnythingTargetCard")&&w->Active)return w;return static_cast<ImGuiWindow*>(nullptr);};
 auto target=targetWindow();check(target&&seenItems.contains("##Countdown")&&!target->InnerRect.Contains(seenItems.at("##Countdown").rect)&&host->InnerRect.Contains(seenItems.at("##Countdown").rect)&&seenItems.at("##Countdown").rect.GetWidth()<250,"Countdown stays outside Targeted Window in the header with a content-sized width");
 check(nh::ui::ActionFont()!=io.Fonts->Fonts[0],"NR Anything uses an actual bold font face");
 model.anythingUi.selected={{"title","Inert selected window"}};model.anythingUi.pendingStart={{"mode","window"}};model.anythingUi.awaitingCountdown=true;render();render("Clear target###ClearAnythingTarget");render();
 check(model.anythingUi.selected.is_null()&&model.anythingUi.pendingStart.is_null()&&!model.anythingUi.awaitingCountdown&&renderedText.find("No window selected.")!=std::string::npos,"Clear target discards idle selection and deferred Start and displays the empty state");
 model.anythingUi.selected={{"title","Preserved active target"}};check(!nh::ClearIdleAnythingTarget(model.anythingUi,true,false,false)&&!model.anythingUi.selected.is_null(),"Clear target cannot detach an active session");model.anythingUi.selected=nh::Json();
 for(auto entry:std::initializer_list<std::pair<const char*,const char*>>{{"Save profile...","NR Anything profiles"},{"Key bindings...","NR Anything key bindings"},{"Advanced look...","Advanced NR Anything"},{"Diagnostics...","NR Anything diagnostics"}}){
  render(entry.first);render();auto modal=ImGui::GetTopMostPopupModal();check(modal&&strstr(modal->Name,entry.second),entry.second);
  if(!strcmp(entry.first,"Advanced look..."))check(renderedText.find("Depth Anything")==std::string::npos&&!seenItems.contains("##DepthProvider")&&!seenItems.contains("Preview depth without NR"),"Advanced look does not expose unready depth controls");
  check(host->ContentSize.x==baseline.x&&host->ContentSize.y==baseline.y,"Modal does not expand main layout");
  io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);render();render();
  check(ImGui::GetTopMostPopupModal()==nullptr,"Escape closes modal");bool focusReturned=false;for(auto w=GImGui->NavWindow;w;w=w->ParentWindow)focusReturned|=w==host;check(focusReturned,"Closing modal restores originating page/card focus");
 }
 render("Save profile...");render();
 auto modal=ImGui::GetTopMostPopupModal();check(modal&&strstr(modal->Name,"NR Anything profiles"),"Profile management opens independently");
 snprintf(model.anythingUi.profileName.data(),model.anythingUi.profileName.size(),"%s","My saved look");render();render("Save profile");render();
 check(model.anythingUi.profiles.contains("My saved look"),"Save dialog stores current settings as a user profile");
 model.anythingUi.transfer=.25f;render("Update profile");render();
 check(model.anythingUi.profiles.value("My saved look",nh::Json::object()).value("detail",-1.f)==.25f,"Save dialog updates an existing user profile");
 render("Delete profile");render();check(!model.anythingUi.profiles.contains("My saved look"),"Save dialog deletes selected user profile");
 io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);render();render();
 nh::AnythingUiState profiles;
 const nh::Json manual={{"look",0},{"detail",1.f},{"colour",1.f},{"quality",4},{"manualPercent",43}};
 check(nh::ApplyAnythingProfile(profiles,manual)&&nh::AnythingStartRequest(profiles)["nrScalePercent"]==43,"Manual profile sends its exact percentage to the existing worker contract");
 const auto manualBefore=nh::AnythingProfile(profiles);
 for(auto invalid:std::initializer_list<nh::Json>{24,101,43.5,"43"}){auto bad=manual;bad["manualPercent"]=invalid;check(!nh::ApplyAnythingProfile(profiles,bad)&&nh::AnythingProfile(profiles)==manualBefore,"Invalid manual values are rejected without changing current settings");}
 nh::AnythingUiState restoredManual;check(nh::ApplyAnythingProfile(restoredManual,nh::AnythingProfile(profiles))&&nh::AnythingStartRequest(restoredManual)["nrScalePercent"]==43,"Manual percentage survives profile serialization");
 auto legacy=manual;legacy["quality"]=2;legacy.erase("manualPercent");check(nh::ApplyAnythingProfile(profiles,legacy)&&nh::AnythingStartRequest(profiles)["nrScalePercent"]==67,"Legacy preset profiles keep their original resolution");
 for(auto name:{"Default"," default ","DEFAULT"}){const auto before=profiles.profiles;check(!nh::SaveAnythingUserProfile(profiles,name)&&!nh::DeleteAnythingUserProfile(profiles,name)&&profiles.profiles==before,"Default profile identity is protected without mutating saved profiles");}
 for(auto name:{"Standard","Neutral","Natural","Cinematic"," standard ","NATURAL"}){check(nh::SaveAnythingUserProfile(profiles,name)&&profiles.profiles.contains(name)&&nh::DeleteAnythingUserProfile(profiles,name)&&!profiles.profiles.contains(name),"Former built-in names remain available for user-profile save and deletion");}
 for(int i=0;i<64;++i)nh::SaveAnythingUserProfile(profiles,"Profile "+std::to_string(i));profiles.transfer=.3f;
 check(nh::SaveAnythingUserProfile(profiles,"Profile 0")&&profiles.profiles["Profile 0"]["detail"]==.3f&&!nh::SaveAnythingUserProfile(profiles,"Overflow"),"Full profile library permits update but refuses a 65th profile");
 fixtureSize={620,600};for(int i=0;i<4;++i)render();look=card("AnythingLookCard");processing=card("AnythingProcessingCard");compare=card("AnythingCompareCard");auto page=card("AnythingPage");
 check(look&&processing&&compare&&look->Pos.y>=processing->Pos.y+processing->Size.y&&compare->Pos.y>=look->Pos.y+look->Size.y,"Compact layout stacks Processing, Look, then comparison");
 check(page&&page->ScrollMax.y>0&&host->ScrollMax.y==0,"Compact content scrolls only within AnythingPage");
 if(look&&processing&&compare)for(auto w:{look,processing,compare})check(w->ContentSize.x<=w->InnerRect.GetWidth()+1&&w->ScrollMax.x==0,"Card controls stay inside compact width without horizontal scrolling");
 for(float dpi:{1.25f,2.f}){
  testDpi=dpi;io.FontGlobalScale=dpi;ImGui::GetStyle()=baseStyle;ImGui::GetStyle().ScaleAllSizes(dpi);io.DisplaySize={1240*dpi,1600*dpi};fixtureSize={1040*dpi,1480*dpi};
  for(int i=0;i<5;++i)render();look=card("AnythingLookCard");processing=card("AnythingProcessingCard");compare=card("AnythingCompareCard");
  auto target=card("AnythingTargetCard");check(target&&seenItems.contains("##Countdown")&&!target->InnerRect.Contains(seenItems.at("##Countdown").rect)&&host->InnerRect.Contains(seenItems.at("##Countdown").rect)&&seenItems["##NRAnythingControl"].rect.GetHeight()==42*dpi,"Countdown stays in the header and the expanded primary action fits at scaled sizes");
  check(processing&&target&&target->Pos.x>=processing->Pos.x+processing->Size.x&&fabsf(target->Pos.y-processing->Pos.y)<2,"Scaled wide layout retains aligned columns at 125/200 percent");
  std::cout<<"DPI heading glyph height: scale="<<dpi<<" baseline="<<logicalHeadingHeight<<" observed="<<headingHeight(look)<<'\n';
  check(look&&logicalHeadingHeight>0&&fabsf(headingHeight(look)/dpi-logicalHeadingHeight)<3,"Actual heading glyph height scales once at 125/200 percent");
  if(look&&processing&&compare)for(auto w:{look,processing,compare})check(w->ContentSize.x<=w->InnerRect.GetWidth()+2&&w->ScrollMax.x==0,"Scaled card content stays within available width");
  fixtureSize={620*dpi,600*dpi};for(int i=0;i<5;++i)render();look=card("AnythingLookCard");compare=card("AnythingCompareCard");page=card("AnythingPage");
  check(look&&compare&&compare->Pos.y>=look->Pos.y+look->Size.y&&page&&page->ScrollMax.y>0&&host->ScrollMax.y==0,"Scaled compact page stacks and keeps scrolling inside content");
 }
 check(!model.anything,"Headless dialog test never starts worker or capture");
 nh::AnythingSnapshot active;active.active=true;active.phase="Running";model.anythingUi.selected={{"hwnd",5u}};
 check(nh::AnythingActionLabel(model.anythingUi,active)=="Stop","Active primary action always stops even when selection differs");
 nh::AnythingSnapshot idle;check(nh::AnythingActionLabel(model.anythingUi,idle)=="NR Anything","Selected target offers normal NR Anything button");model.anythingUi.selected=nh::Json();check(nh::AnythingActionLabel(model.anythingUi,idle)=="NR Anything","No target offers NR Anything");
 nh::AnythingSnapshot capture;capture.connected=capture.active=true;capture.phase="Running";capture.status={{"sourcePaused",false},{"capabilities",{{"outputSnapshots",true}}}};
 check(nh::CanCaptureAnythingScreenshot(capture),"Completed active rendering exposes the shared screenshot action");
 for(int unavailable=0;unavailable<8;++unavailable){auto blocked=capture;switch(unavailable){case 0:blocked.connected=false;break;case 1:blocked.active=false;break;case 2:blocked.busy=true;break;case 3:blocked.stopping=true;break;case 4:blocked.phase="Countdown";break;case 5:blocked.status["sourcePaused"]=true;break;case 6:blocked.status["capabilities"]["outputSnapshots"]=false;break;case 7:blocked.status["snapshot"]={{"state","pending"}};break;}check(!nh::CanCaptureAnythingScreenshot(blocked),"Screenshot refuses disconnected, idle, busy, stopping, countdown, paused, unsupported or pending state");}
 check(!nh::CaptureAnythingScreenshot(model)&&!model.anything,"Screenshot with no session cannot start a worker");
 // The isolated pipe fixture emits catalog data only; no capture/provider entry.
 testDpi=1;io.FontGlobalScale=1;ImGui::GetStyle()=baseStyle;io.DisplaySize={1240,1600};fixtureSize={1040,1480};
 model.anything=std::make_shared<nh::AnythingController>(std::filesystem::current_path()/L"builds/hub-anything/ui/AnythingProtocolFixture.exe",nh::UserRoot()/L"catalog");model.anything->Connect();
 auto deadline=GetTickCount64()+6000;while(!model.anything->Snapshot().connected&&GetTickCount64()<deadline)Sleep(10);
 check(model.anything->Snapshot().connected,"Dropdown uses connected inert catalog fixture");render();for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);render();
 render("##WindowDropdown");check(model.anythingUi.windowListPending&&GImGui->OpenPopupStack.Size==0,"Window dropdown waits for a newly requested catalog before opening");
 deadline=GetTickCount64()+6000;while(model.anything->Snapshot().busy&&GetTickCount64()<deadline)Sleep(10);
 render();render();check(!model.anythingUi.windowListPending&&GImGui->OpenPopupStack.Size>0&&ImGui::GetTopMostPopupModal()==nullptr&&renderedText.find("Media")!=std::string::npos,"Completed fresh catalog opens searchable dropdown without modal");
 io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);render();render();
 render("##Countdown");render();check(GImGui->OpenPopupStack.Size>0&&renderedText.find("Choose foreground after delay")!=std::string::npos&&ImGui::GetTopMostPopupModal()==nullptr,"Countdown has its own dropdown without window list or modal");
 io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);render();model.anything->SelectModel(nh::UserRoot()/L"inert-model.dll",false);for(int i=0;i<200&&(!model.anything->Snapshot().ready||model.anything->Snapshot().busy);++i)Sleep(10);
 render();for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);render();
 nh::ApplyAnythingProfile(model.anythingUi,manual);
 model.anythingUi.selected=model.anything->Snapshot().windows.at(0);model.page=1;render();render("##NRAnythingControl");
 for(int i=0;i<200&&(model.anything->Snapshot().busy||model.anything->Snapshot().phase!="Running");++i)Sleep(10);
 check(model.page==2&&model.anything->Snapshot().active&&model.anything->Snapshot().status["receivedStart"]["window"]==model.anythingUi.selected,"Primary split action opens Anything and starts the selected target");
 check(model.anything->Snapshot().status["receivedStart"]["nrScalePercent"]==43,"Manual scale reaches the selected session over the actual inert pipe");
 render();render("Reset##ManualResolution");for(int i=0;i<200&&(model.anything->Snapshot().busy||!model.anything->Snapshot().status.contains("receivedProcessing"));++i)Sleep(10);
 check(model.anythingUi.manualPercent==100&&model.anything->Snapshot().status.value("receivedProcessing",nh::Json::object()).value("nrScalePercent",0)==100,"Manual reset applies live through the existing processing command");
 model.showDataMaintenance=true;check(!nh::CaptureAnythingScreenshot(model),"Screenshot is refused during App data maintenance");model.showDataMaintenance=false;
 model.anything->SetComparison(.25f,0,2);for(int i=0;i<200&&(model.anything->Snapshot().busy||model.anything->Snapshot().status.value("comparisonSplit",-1.f)!=.25f);++i)Sleep(10);
 model.page=1;auto screenshotTarget=model.anythingUi.selected;check(nh::CaptureAnythingScreenshot(model),"Screenshot shortcut helper sends the existing snapshot command");
 for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);
 check(model.page==1&&model.anythingUi.selected==screenshotTarget&&model.anything->Snapshot().status.value("comparisonSplit",-1.f)==.25f&&model.anything->Snapshot().status.value("snapshot",nh::Json::object()).value("state",std::string{})=="pending","Screenshot preserves App page, target and current comparison and waits for completed output");
 check(!nh::CaptureAnythingScreenshot(model),"A pending screenshot cannot be requested twice");
 render();auto selectedBefore=model.anythingUi.selected;render("##WindowDropdown");
 check((seenItems["##WindowDropdown"].flags&ImGuiItemFlags_Disabled)&&model.anythingUi.selected==selectedBefore&&!model.anythingUi.windowListPending,"Active session refuses target dropdown changes");
 model.page=1;render("##NRAnythingControl");for(int i=0;i<200&&model.anything->Snapshot().stopping;++i)Sleep(10);
 check(model.page==2&&!model.anything->Snapshot().active,"Primary Stop also opens Anything and stops its session");
 render();render("##WindowDropdown");for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);render();render();render("None");render();
 check(model.anythingUi.selected.is_null()&&GImGui->OpenPopupStack.Size==0,"Split dropdown None clears selection and closes its popup");
 render("##WindowDropdown");for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);render();render();render("Media Ω");render();
 check(model.anythingUi.selected==model.anything->Snapshot().windows.at(0)&&GImGui->OpenPopupStack.Size==0,"Split dropdown selects the refreshed exact window identity");
 testDpi=2;io.FontGlobalScale=2;ImGui::GetStyle()=baseStyle;ImGui::GetStyle().ScaleAllSizes(2);io.DisplaySize={880,600};fixtureSize={840,540};render();
 render("##WindowDropdown");for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);render();render();render();
 ImGuiWindow* catalogPopup=GImGui->OpenPopupStack.Size?GImGui->OpenPopupStack.back().Window:nullptr;
 auto results=card("WindowResults");
 check(catalogPopup&&catalogPopup->Pos.y>=0&&catalogPopup->Pos.y+catalogPopup->Size.y<=io.DisplaySize.y+1&&results&&results->InnerRect.GetHeight()>0,"Window popup stays reachable in a 600-pixel viewport at 200 percent scale");
 io.AddKeyEvent(ImGuiKey_Escape,true);render();io.AddKeyEvent(ImGuiKey_Escape,false);render();
 testDpi=1;io.FontGlobalScale=1;ImGui::GetStyle()=baseStyle;io.DisplaySize={1240,1600};fixtureSize={1040,1480};render();
 model.anythingUi.selected=nh::Json();nh::ToggleAnythingSelected(model);check(!model.anything->Snapshot().active,"Shortcut refuses a missing target without entering countdown");
 model.anythingUi.selected=model.anything->Snapshot().windows.at(0);model.page=1;nh::ToggleAnythingSelected(model);for(int i=0;i<200&&(model.anything->Snapshot().busy||model.anything->Snapshot().phase!="Running");++i)Sleep(10);
 check(model.page==2&&model.anything->Snapshot().status["receivedStart"]["window"]==model.anythingUi.selected&&model.anything->Snapshot().status["receivedStart"]["mode"]=="selected","Shortcut opens Anything and starts exact selected identity over actual inert pipe");
 render();render("Capture");for(int i=0;i<200&&model.anything->Snapshot().busy;++i)Sleep(10);
 check(model.anything->Snapshot().status.value("snapshot",nh::Json::object()).value("state",std::string{})=="pending","Capture button uses the same running-session screenshot path");
 model.anythingUi.selected=nh::Json();nh::ToggleAnythingSelected(model);for(int i=0;i<200&&model.anything->Snapshot().stopping;++i)Sleep(10);check(!model.anything->Snapshot().active,"Shortcut stops active session even when selection is absent");
 model.anything->Start({{"mode","countdown"},{"seconds",3}});for(int i=0;i<200&&model.anything->Snapshot().phase!="Countdown";++i)Sleep(10);render();
 check(renderedText.find("Waiting for countdown")!=std::string::npos,"Processing summary never labels countdown as Running");
 check(renderedText.find("Processing:")<renderedText.find("Look"),"Processing summary precedes page cards below target identity");
 check(renderedText.find("lossless PNG")!=std::string::npos&&renderedText.find("BMP")==std::string::npos,"Capture hint describes actual PNG output");
 check(seenItems.contains("Natural")&&!(seenItems["Natural"].flags&ImGuiItemFlags_Disabled),"Advertised live processing keeps active style controls usable");
 model.anything->Stop();for(int i=0;i<200&&model.anything->Snapshot().stopping;++i)Sleep(10);model.anything.reset();
 std::filesystem::remove(nh::UserRoot()/L"Runtime/anything-profiles.json");
 ImGui::DestroyContext();return failures?1:0;
}
