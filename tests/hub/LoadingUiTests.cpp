#include "ui/HubViewModel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
#include <iostream>
#include <cstdlib>
#include <tuple>
namespace nh {void ApplySharedTheme(bool);}
int RunLoadingUiTests(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 std::cout<<std::unitbuf;
 int failed=0;auto check=[&](bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;if(!value)failed++;};
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1240,820};io.DeltaTime=1.f/60;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);
 nh::HubModel model;model.reducedMotion=true;model.games={{"a","First game","Custom","C:/fixture/a",{}},{"b","Second game","Custom","C:/fixture/b",{}}};model.selected=0;
 auto window=[](const char* part){for(auto w:GImGui->Windows)if(strstr(w->Name,part)&&w->Active)return w;return (ImGuiWindow*)nullptr;};
 float cancelWidth=0,frameHeight=0;int foregroundVertices=0;std::string rendered;auto render=[&]{ImGui::NewFrame();ImGui::LogToBuffer();nh::RenderHub(model,nullptr,1);rendered=GImGui->LogBuffer.c_str();ImGui::LogFinish();cancelWidth=ImGui::CalcTextSize("Cancel scan").x+ImGui::GetStyle().FramePadding.x*2;frameHeight=ImGui::GetFrameHeight();foregroundVertices=ImGui::GetForegroundDrawList()->VtxBuffer.Size;ImGui::Render();};
 model.page=1;nh::ApplySharedTheme(false);for(int i=0;i<3;i++)render();auto idleBody=window("PageContent");auto idlePosition=idleBody?idleBody->Pos:ImVec2{};
 model.discovery.busy=true;model.discovery.started=GetTickCount64();for(int i=0;i<3;i++)render();auto scanningBody=window("PageContent");
 check(scanningBody&&scanningBody->Pos.x==idlePosition.x&&scanningBody->Pos.y==idlePosition.y,"NH-LOADING: scanning does not shift the content layout");
 for(bool light:{false,true}){model.light=light;nh::ApplySharedTheme(light);for(int page:{1,4,0}){model.page=page;for(int i=0;i<3;i++)render();check(window("ActivityOverlay")&&!foregroundVertices,"NH-LOADING: scan overlay stays undimmed across pages in both themes");}}
 model.Select(1);render();check(model.selected==1&&model.discovery.busy&&window("ActivityOverlay"),"NH-LOADING: changing games preserves the global scan and its cancel control");
 model.discovery.busy=false;model.installer.busy=model.installer.readOnlyInstaller=true;model.page=4;for(int i=0;i<3;i++)render();check(window("ActivityOverlay")&&!window("Working")&&!foregroundVertices,"NH-LOADING: selected-game checks use undimmed floating activity");
 model.page=1;model.inspectionRefreshing=true;model.installer.action="Inspect";model.installer.started=GetTickCount64();model.installer.progressStage="Checking fixture files";model.installer.progress=.25f;for(int i=0;i<3;i++)render();auto details=window("GameDetails");
 check(details&&window("SelectedGameLoadingShade")&&!window("Working")&&rendered.find("Checking fixture files")==std::string::npos&&window("SelectedGameLoadingShade")->Pos.x==details->OuterRectClipped.Min.x&&window("SelectedGameLoadingShade")->Pos.y==details->OuterRectClipped.Min.y,"NH-LOADING: first inspection covers only the selected game without an added progress row");
 model.inspection={{"state",{{"status","installed-verified"}}}};model.inspectionCached=true;for(int i=0;i<3;i++)render();check(window("SelectedGameLoadingShade")&&rendered.find("Checking fixture files")==std::string::npos,"NH-LOADING: cached refresh retains a selected-game loading overlay");model.installer.busy=false;for(int i=0;i<3;i++)render();check(rendered.find("Checking fixture files")==std::string::npos,"NH-LOADING: idle cached selection has no stale operation progress");model.installer.busy=true;
 model.page=4;for(int i=0;i<3;i++)render();check(window("ActivityOverlay")&&!window("CheckingSelectedGame")&&!window("Working"),"NH-LOADING: another page retains compact activity without a game overlay");model.page=1;
 io.DisplaySize={880,600};for(int i=0;i<3;i++)render();check(!window("HeaderActivity")&&window("Navigation")&&window("PageContent")&&window("Navigation")->OuterRectClipped.Max.y<=window("PageContent")->Pos.y&&window("Navigation")->ScrollMax.y==0&&window("Navigation")->ContentSize.y<=window("Navigation")->InnerRect.GetHeight()+1,"NH-LOADING: narrow navigation remains contained above content without reserved activity space");auto pageContent=window("PageContent");details=window("GameDetails");check(details&&pageContent&&details->OuterRectClipped.Min.y>=pageContent->InnerRect.Min.y&&details->OuterRectClipped.Max.y<=pageContent->InnerRect.Max.y+1&&window("SelectedGameLoadingShade"),"NH-LOADING: compact overlay remains within the selected-game pane");io.DisplaySize={1240,820};
 model.inspectionRefreshing=false;model.installer.busy=false;for(int i=0;i<3;i++)render();auto idleDetails=window("GameDetails");auto idleContent=idleDetails->ContentSize;auto idleHeader=window("SelectedGameHeader")->Pos;
 for(auto operation:{"Update","Repair","ChangeProxy"}){model.installer.busy=true;model.installer.readOnlyInstaller=false;model.installer.action=operation;for(int i=0;i<3;i++)render();auto activeDetails=window("GameDetails");check(window("SelectedGameLoadingShade")&&!window("Working")&&!foregroundVertices&&rendered.find("Checking fixture files")!=std::string::npos&&activeDetails->ContentSize.y==idleContent.y&&window("SelectedGameHeader")->Pos.y==idleHeader.y,"NH-LOADING: operation overlay leaves the underlying content and header unchanged");model.installer.busy=false;for(int i=0;i<3;i++)render();check(!window("SelectedGameLoadingShade"),"NH-LOADING: operation overlay dismisses after completion");}
 // Explicit human rollback restores the earlier shade/bar/activity presentation.
 for(bool light:{false,true}){
  model.light=light;nh::ApplySharedTheme(light);model.installer.busy=true;model.installer.readOnlyInstaller=false;
  model.page=1;
  for(float fraction:{-1.f,0.f,.5f,1.f}){
   model.installer.progress=fraction;model.installer.progressStage="Installing fixture files";
   for(int i=0;i<3;i++)render();auto shade=window("SelectedGameLoadingShade");auto selected=window("GameDetails");
   check(shade&&selected&&!window("InstallationProgress")&&!window("ActivityOverlay")&&rendered.find("Installing fixture files")!=std::string::npos&&shade->Pos.x==selected->OuterRectClipped.Min.x&&shade->Pos.y==selected->OuterRectClipped.Min.y&&(shade->Flags&ImGuiWindowFlags_NoInputs)==0,
         "NH-ROLLBACK: original selected-game shade and measured/indeterminate bar restore input blocking without logo-water");
  }
  for(int page:{4,0}){model.page=page;for(int i=0;i<3;i++)render();
   check(window("ActivityOverlay")&&!window("InstallationProgress")&&!window("SelectedGameLoadingShade"),"NH-ROLLBACK: navigating away restores compact installer activity without logo-water");}
 }
 model.installer.busy=false;model.page=1;for(int i=0;i<3;i++)render();
 check(!window("SelectedGameLoadingShade")&&!window("InstallationProgress")&&!window("ActivityOverlay"),"NH-ROLLBACK: completion removes loading presentation");
 model.installer.readOnlyInstaller=false;model.installer.progress=.25f;model.installer.progressStage="Checking fixture files";model.page=1;
 model.discovery.busy=model.installer.busy=true;for(int i=0;i<3;i++)render();check(window("ActivityOverlay")&&!window("Working")&&window("SelectedGameLoadingShade")&&!foregroundVertices,"NH-LOADING: simultaneous scan and installation leave Cancel scan undimmed");
 model.installer.readOnlyInstaller=model.gameDiagnosticsBusy=true;for(int i=0;i<3;i++)render();auto activityRows=window("ActivityOverlay");check(activityRows&&activityRows->ContentSize.y<=activityRows->InnerRect.GetHeight(),"NH-LOADING: simultaneous activity rows fit without clipping");model.installer.busy=model.discovery.busy=model.gameDiagnosticsBusy=false;
 model.installer.ReadProgress("[NR-PROGRESS] {\"stage\":\"Installing files\",\"done\":3,\"total\":12}\n");check(model.installer.progress==.25f&&model.installer.progressStage=="Installing files","NH-PROGRESS: actual completed file counts drive the progress bar");model.installer.ReadProgress("[NR-PROGRESS] {\"stage\":\"wrong\",\"done\":20,\"total\":2}\n[NR-PROGRESS] incomplete");check(model.installer.progress==.25f,"NH-PROGRESS: malformed and impossible counts never fabricate progress");model.installer.ReadProgress("[NR-PROGRESS] {\"stage\":\"Preparing\",\"done\":0,\"total\":0}\n");check(model.installer.progress<0,"NH-PROGRESS: work without a measurable total stays indeterminate");
 auto clickEvidence=[&](const char* stage){auto modal=ImGui::GetTopMostPopupModal();nh::Json order=nh::Json::array();for(int i=0;i<GImGui->Windows.Size;i++){auto w=GImGui->Windows[i];if(w->Active&&(std::strcmp(w->Name,"NeuRotic")==0||std::strcmp(w->Name,"ActivityOverlay")==0||(w->Flags&ImGuiWindowFlags_Modal)))order.push_back({{"name",w->Name},{"order",i},{"position",{w->Pos.x,w->Pos.y}},{"size",{w->Size.x,w->Size.y}},{"flags",w->Flags}});}std::cout<<"NH-CLICK-EVIDENCE "<<nh::Json{{"stage",stage},{"popupCount",GImGui->OpenPopupStack.Size},{"modal",modal?modal->Name:""},{"nav",GImGui->NavWindow?GImGui->NavWindow->Name:""},{"hovered",GImGui->HoveredWindow?GImGui->HoveredWindow->Name:""},{"activeId",GImGui->ActiveId},{"hoveredId",GImGui->HoveredId},{"mouse",{io.MousePos.x,io.MousePos.y}},{"down",io.MouseDown[0]},{"windows",order}}.dump()<<'\n';};
 // A real idle child gives Cancel a deterministic process to stop without a game.
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);auto exe=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";auto command=nh::Quote(exe.wstring())+L" -NoProfile -Command Start-Sleep -Seconds 30";STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION child{};
 bool created=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE;
 if(created){CloseHandle(child.hThread);model.discovery.process=child.hProcess;model.discovery.busy=model.discovery.discovery=true;model.discovery.started=GetTickCount64();model.page=5;for(int i=0;i<3;i++)render();auto activity=window("ActivityOverlay");if(activity){ImVec2 point{activity->Pos.x+activity->Size.x-activity->WindowPadding.x-cancelWidth*.5f,activity->Pos.y+activity->WindowPadding.y+4+frameHeight*.5f};clickEvidence("before-hover");io.AddMousePosEvent(point.x,point.y);render();clickEvidence("hover");io.AddMouseButtonEvent(0,true);render();clickEvidence("down");io.AddMouseButtonEvent(0,false);render();clickEvidence("up");}check(!model.discovery.error.empty(),"NH-LOADING: Cancel scan is clickable from another page");if(model.discovery.error.empty())model.CancelScan();auto until=GetTickCount64()+5000;while(model.discovery.busy&&GetTickCount64()<until){model.Poll();Sleep(1);}for(int i=0;i<3;i++)render();check(!model.discovery.busy&&!window("ActivityOverlay")&&model.games.size()==2,"NH-LOADING: cancellation stops progress and preserves indexed games");}else check(false,"NH-LOADING: idle cancellation fixture starts");
 // Header geometry must survive different games, extra notices, and resizing.
 model.page=1;model.showIssue=false;model.inspection=nh::Json();model.operationIssue=nh::Json();model.inspectionCached=false;model.selected=0;
 io.DisplaySize={1240,1100};for(int i=0;i<4;i++)render();
 auto fixedHeader=window("SelectedGameHeader"),fixedDetails=window("GameDetails");
 const float reservedHeight=fixedHeader?fixedHeader->Size.y:0;
 const float artColumnWidth=fixedHeader&&fixedDetails?fixedHeader->Pos.x-fixedDetails->Pos.x:0;
 model.selected=1;model.games[1].target={"C:/fixture/MonsterHunterWilds.exe","MonsterHunterWilds",false,std::string(1500,'W'),32};model.inspectionCached=true;model.operationIssue={{"reason","Fixture attention"}};
 for(int i=0;i<4;i++)render();fixedHeader=window("SelectedGameHeader");fixedDetails=window("GameDetails");
 check(fixedHeader&&fixedDetails&&reservedHeight>0&&fixedHeader->Size.y==reservedHeight&&fixedHeader->Pos.x-fixedDetails->Pos.x==artColumnWidth,"NH-ART-FIXED: switching games and adding controls/notices preserves header and art-column dimensions");
 // An unresolved executable displays its reason inline rather than in a tooltip.
 model.games[1].target.path.clear();for(int i=0;i<4;i++)render();fixedHeader=window("SelectedGameHeader");
 check(fixedHeader&&fixedHeader->ScrollMax.y>0&&!(fixedHeader->Flags&ImGuiWindowFlags_NoScrollWithMouse),"NH-ART-FIXED: excess header information remains scrollable in the controls column");
 io.DisplaySize={880,600};for(int i=0;i<4;i++)render();fixedHeader=window("SelectedGameHeader");fixedDetails=window("GameDetails");
 check(fixedHeader&&fixedDetails&&fixedHeader->Size.y<reservedHeight&&fixedHeader->Pos.x-fixedDetails->Pos.x<artColumnWidth&&fixedHeader->ScrollMax.y>0,"NH-ART-FIXED: a smaller window shrinks artwork while excess controls remain scrollable");
 io.DisplaySize={1240,1100};for(int i=0;i<4;i++)render();fixedHeader=window("SelectedGameHeader");fixedDetails=window("GameDetails");
 check(fixedHeader&&fixedDetails&&fixedHeader->Size.y==reservedHeight&&fixedHeader->Pos.x-fixedDetails->Pos.x==artColumnWidth,"NH-ART-FIXED: enlarging the window restores standard artwork dimensions with the same game selected");
 ImGui::DestroyContext();return failed;
}
