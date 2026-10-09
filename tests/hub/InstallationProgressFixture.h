#pragma once
#include "ui/HubViewModel.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace nh {
inline const char* InstallationProgressScene(int frame){
 const char* names[]={"zero","half-wave-a","half-wave-b","settings-measured","home-unknown",
                      "full-stage-busy","new-stage-zero","file-decision","scan-and-write","completed"};
 return names[std::clamp(frame/12,0,9)];
}
inline void PrepareInstallationProgressFixture(HubModel& model,int frame,bool longLabels,bool reduced){
 // Presentation state only. The caller skips Load and Poll in this fixture;
 // no process, game transaction, worker, model or live target is created.
 if(frame==0){Game game;game.id="progress-fixture";game.title="Inert progress fixture";game.root="C:/Inert Progress";game.target={"C:/Inert Progress/Fixture.exe","Fixture",true,"",64};model.games={game};model.selected=0;model.inspection={{"state",nullptr},{"settings",Json::array()}};}
 const int scene=std::min(frame/12,9);
 model.loaded=false;model.onlineArtwork=false;model.reducedMotion=reduced;
 model.page=scene==3?4:scene==4?0:1;
 model.installer.busy=scene!=9;model.installer.readOnlyInstaller=false;
 model.installer.started=GetTickCount64()-12000;model.installer.action="Install";
 model.installer.progress=scene==0||scene==6?0:scene==4?-1:scene==5?1:scene==3?.75f:.5f;
 model.installer.progressStage=scene==4?"Preparing fixture":scene==6?"Writing fixture files":"Installing fixture files";
 if(longLabels)model.installer.progressStage="Preparing and installing the selected application files, checking their destination and preserving the existing configuration before finishing this stage";
 model.discovery.busy=scene==8;model.discovery.started=GetTickCount64()-12000;
 if(scene==7&&frame%12==0){model.operationIssue={{"decisionKind","FileConflict"},{"target",model.games[0].target.path},{"fileConflicts",Json::array({{{"path","dxgi.dll"}}})},{"canKeepReShade",false}};model.fileReview.Begin(model.operationIssue["fileConflicts"]);model.showIssue=true;}
 if(scene==8&&frame%12==0){model.showIssue=false;model.fileReview.Cancel();model.operationIssue=Json();ImGui::ClosePopupToLevel(0,true);}
}
inline Json MeasureInstallationProgressFixture(const HubModel& model,int frame,float dpi){
 ImGuiWindow *shade=nullptr,*selected=nullptr,*activity=nullptr;bool logo=false,decision=false;Json windows=Json::array();
 for(auto* w:GImGui->Windows)if(w->Active){
  if(std::strstr(w->Name,"SelectedGameLoadingShade"))shade=w;
  if(std::strcmp(w->Name,"InstallationProgress")==0)logo=true;
  if(std::strcmp(w->Name,"ActivityOverlay")==0)activity=w;
  if(std::strstr(w->Name,"GameDetails")&&w->ParentWindow&&std::strstr(w->ParentWindow->Name,"PageContent")&&w->ParentWindow->ParentWindow&&std::strcmp(w->ParentWindow->ParentWindow->Name,"NeuRotic")==0){if(selected)throw std::runtime_error("Ambiguous selected pane");selected=w;}
  if(w->Flags&ImGuiWindowFlags_Modal)decision=true;
  windows.push_back({{"name",w->Name},{"position",{w->Pos.x,w->Pos.y}},{"size",{w->Size.x,w->Size.y}},{"flags",w->Flags}});
 }
 const int scene=std::min(frame/12,9);const bool expectedShade=model.page==1&&model.installer.busy;
 if(logo||bool(shade)!=expectedShade||bool(activity)!=(scene==3||scene==4||scene==8)||decision!=(scene==7))throw std::runtime_error("Restored shade/activity/modal differs from actual native scene");
 Json bar=nullptr;
 if(shade){
  if(!selected||std::abs(shade->Pos.x-selected->OuterRectClipped.Min.x)>=1||std::abs(shade->Pos.y-selected->OuterRectClipped.Min.y)>=1||std::abs(shade->Size.x-selected->OuterRectClipped.GetWidth())>=1||std::abs(shade->Size.y-selected->OuterRectClipped.GetHeight())>=1||(shade->Flags&ImGuiWindowFlags_NoInputs))throw std::runtime_error("Restored shade bounds/input blocking differs from selected pane");
  const auto color=ImGui::GetColorU32(ImGuiCol_PlotHistogram);const auto* draw=shade->DrawList;
  ImVec2 low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high{-low.x,-low.y};int vertices=0;
  for(const auto& v:draw->VtxBuffer)if((v.col&0xffffff)==(color&0xffffff)&&v.pos.y>=shade->Pos.y+48*dpi-1.f&&v.pos.y<=shade->Pos.y+56*dpi+1.f){low.x=std::min(low.x,v.pos.x);low.y=std::min(low.y,v.pos.y);high.x=std::max(high.x,v.pos.x);high.y=std::max(high.y,v.pos.y);++vertices;}
  if(model.installer.progress>0&&vertices==0)throw std::runtime_error("Restored measured progress bar missing");
  if(vertices)bar={low.x,low.y,high.x,high.y};
 }
 return {{"scene",InstallationProgressScene(frame)},{"page",model.page},{"scale",dpi},{"light",model.light},{"reducedMotion",model.reducedMotion},{"stage",model.installer.progressStage},{"fraction",model.installer.progress},{"busy",model.installer.busy},{"selectedShadeVisible",bool(shade)},{"activityVisible",bool(activity)},{"decisionVisible",decision},{"logoWaterVisible",logo},{"measuredBarBounds",bar},{"windows",windows},{"displayOnly",true},{"gameOperations",false},{"workerStarted",false}};
}
}
