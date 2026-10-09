#include "ui/HubViewModel.h"
#include "anything/AnythingView.h"
#include "anything/AnythingController.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
namespace nh {void ApplySharedTheme(bool);}
template<class State> constexpr bool HasDeferredGuideControls=requires(State state){state.guides;}||requires(State state){state.depth;};
int RunFocusedUxTests(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 int failed=0;auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failed+=!ok;};
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1240,820};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);
 nh::HubModel model;model.reducedMotion=true;model.page=1;model.games={{"a","Required executable","Custom","C:/fixture",{}}};model.selected=0;model.games[0].candidates={"C:/fixture/choose.exe"};model.games[0].executableChoices={{"C:/fixture/choose.exe","C:/fixture","Custom","","","",64}};
 auto window=[](const char* part){for(auto w:GImGui->Windows)if(strstr(w->Name,part)&&w->Active)return w;return (ImGuiWindow*)nullptr;};
 ImU32 red=0;auto render=[&](ImGuiID activate=0){ImGui::NewFrame();red=ImGui::GetColorU32(ImVec4(1.f,.1f,.18f,1));if(activate){GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=activate;}nh::RenderHub(model,nullptr,1);ImGui::Render();};
 auto redBorder=[&]{int count=0;auto w=window("SelectedGameHeader");if(w)for(auto& vertex:w->DrawList->VtxBuffer)count+=vertex.col==red;return count;};
 for(bool light:{false,true}){model.light=light;nh::ApplySharedTheme(light);model.games[0].target.suitable=false;for(int i=0;i<3;i++)render();check(redBorder()>0,"NH-UX: required executable has a bright red outline in both themes");model.games[0].target.suitable=true;for(int i=0;i<3;i++)render();check(redBorder()==0,"NH-UX: executable outline returns to normal when requirement is met");}
 model.games[0].target.suitable=false;for(int i=0;i<3;i++)render();auto header=window("SelectedGameHeader");bool visible=header!=nullptr;if(header)for(auto& command:header->DrawList->CmdBuffer){ImRect clip{command.ClipRect.x,command.ClipRect.y,command.ClipRect.z,command.ClipRect.w};for(unsigned i=0;i<command.ElemCount;i++){auto& vertex=header->DrawList->VtxBuffer[command.VtxOffset+header->DrawList->IdxBuffer[command.IdxOffset+i]];if((vertex.col&0x00ffffff)==(red&0x00ffffff)&&(vertex.col>>24)&&!clip.Contains(vertex.pos))visible=false;}}check(visible,"NH-UX: complete required-executable outline stays inside its actual draw-command clip");

 for(float width:{1240.f,880.f}){
  io.DisplaySize.x=width;model.page=0;for(int i=0;i<3;i++)render();auto navigation=window("Navigation"),content=window("PageContent");
  std::vector<ImGuiWindow*> cards;for(auto candidate:GImGui->Windows)if(candidate->Active&&strstr(candidate->Name,"ComponentCard"))cards.push_back(candidate);
  std::sort(cards.begin(),cards.end(),[](auto left,auto right){return left->Pos.x<right->Pos.x;});
  bool cardsBelow=cards.size()==3&&navigation&&content&&!window("HeaderReadiness");
  bool cardsFit=cards.size()==3;
  for(auto card:cards){cardsBelow&=content&&card->Pos.y>=content->Pos.y;cardsFit&=card->ContentSize.x<=card->InnerRect.GetWidth()+1&&card->ContentSize.y<=card->InnerRect.GetHeight()+1;}
  check(cardsBelow&&navigation->Pos.y<content->Pos.y,"NH-UX: three component cards belong on Home below fixed navigation without duplicate readiness boxes");
  check(cardsFit,"NH-UX: Home component card content fits without clipping");
  auto intro=window("HomeIntroduction");bool refreshReachable=false;
  if(intro&&cards.size()==3){
   auto id=intro->GetID("##RefreshHomeComponents");GImGui->NavWindow=intro;GImGui->NavId=id;render();
   auto rect=ImGui::WindowRectRelToAbs(intro,intro->NavRectRel[ImGuiNavLayer_Main]);
   const bool submitted=GImGui->NavIdIsAlive&&GImGui->NavId==id&&rect.Max.y<=cards[0]->Pos.y&&std::abs(rect.Max.x-intro->InnerRect.Max.x)<1;
   io.AddMousePosEvent(rect.GetCenter().x,rect.GetCenter().y);render();refreshReachable=submitted&&GImGui->HoveredId==id;
  }
  check(refreshReachable,"NH-UX: one shared Home Refresh is reachable at the right end of the introduction");
  model.page=1;io.AddMousePosEvent(-1000,-1000);for(int i=0;i<3;i++)render();check(!window("HeaderReadiness"),"NH-UX: component readouts no longer occupy the application header on other pages");
 }
 io.DisplaySize.y=600;for(int i=0;i<3;i++)render();auto details=window("GameDetails");
 // The outer pane's bottom can put the entire header above the viewport.
 // Sweep both real scroll ranges: the fixed header keeps overflow controls
 // reachable without changing artwork geometry.
 bool compactClip=details!=nullptr,sawVisibleRed=false,headerScrollReachable=false,noOuterRed=true,culledAtBottom=false;int visibleSamples=0;
 const float maximumScroll=details?details->ScrollMax.y:0;
 for(int sample=0;details&&sample<=16;++sample){
  details->Scroll.y=maximumScroll*float(sample)/16;for(int i=0;i<3;i++)render();auto compactHeader=window("SelectedGameHeader");int sampleRed=0;
  if(!compactHeader){compactClip=false;continue;}
  headerScrollReachable|=compactHeader->ScrollMax.y>0&&!(compactHeader->Flags&ImGuiWindowFlags_NoScrollWithMouse);
  const float innerMaximum=compactHeader->ScrollMax.y;
  for(int innerSample=0;innerSample<=8;++innerSample){
  compactHeader->Scroll.y=innerMaximum*float(innerSample)/8;for(int i=0;i<3;i++)render();sampleRed=0;
  for(auto& command:compactHeader->DrawList->CmdBuffer){ImRect clip{command.ClipRect.x,command.ClipRect.y,command.ClipRect.z,command.ClipRect.w};for(unsigned i=0;i<command.ElemCount;i++){auto& vertex=compactHeader->DrawList->VtxBuffer[command.VtxOffset+compactHeader->DrawList->IdxBuffer[command.IdxOffset+i]];if((vertex.col&0x00ffffff)==(red&0x00ffffff)&&(vertex.col>>24)){++sampleRed;if(!clip.Contains(vertex.pos))compactClip=false;}}}
  if(sampleRed){sawVisibleRed=true;++visibleSamples;}
  for(auto& vertex:details->DrawList->VtxBuffer)if((vertex.col&0x00ffffff)==(red&0x00ffffff)&&(vertex.col>>24))noOuterRed=false;
  if(sample==16)culledAtBottom=compactHeader->Pos.y+compactHeader->Size.y<=details->InnerRect.Min.y&&sampleRed==0;
  }
 }
 check(compactClip&&sawVisibleRed&&noOuterRed&&headerScrollReachable,"NH-UX: required outlines remain reachable inside header draw clips across both scroll ranges");
 check(culledAtBottom&&noOuterRed,"NH-UX: fully offscreen executable controls are culled without stray outer-pane outlines");
 if(!compactClip||!sawVisibleRed||!noOuterRed||!headerScrollReachable||!culledAtBottom)std::cout<<"OUTLINE-SWEEP visibleSamples="<<visibleSamples<<" clip="<<compactClip<<" outerClean="<<noOuterRed<<" headerScrollReachable="<<headerScrollReachable<<" culledAtBottom="<<culledAtBottom<<" max="<<maximumScroll<<std::endl;
 if(details)details->Scroll.y=0;io.DisplaySize.y=820;
 // Exercise the production drawer and search path with inert, unsuitable targets:
 // choosing a result cannot launch an installer or inspect real game files.
 {
  io.DisplaySize={1240,820};model.page=1;model.reducedMotion=true;
  model.games.push_back({"b","Matching second game","Custom","C:/fixture-second",{}});
  model.games[1].target.suitable=false;model.selected=0;model.search[0]=0;
  for(int i=0;i<3;++i)render();auto pane=window("PageContent");auto list=window("LibraryList");auto gamePane=window("GameDetails");
  check(list&&gamePane&&list->Pos.x<gamePane->Pos.x,"NH-UX: fresh library shows its game list beside selected-game details");
  model.reducedMotion=false;for(int i=0;i<3;++i)render();if(pane)render(pane->GetID("##LibraryDrawer"));
  float previous=gamePane?gamePane->Pos.x:0,largestTailStep=0;bool monotonic=true;int tailSamples=0;float lastVisibleX=previous,firstHiddenX=previous;bool sawVisible=false,sawHidden=false;
  for(int i=0;i<80;++i){render();gamePane=window("GameDetails");if(!gamePane){monotonic=false;continue;}float x=gamePane->Pos.x;monotonic&=x<=previous+.1f;if(i>18){largestTailStep=std::max(largestTailStep,std::abs(x-previous));++tailSamples;}previous=x;if(window("LibraryColumn")){sawVisible=true;lastVisibleX=x;}else if(!sawHidden){sawHidden=true;firstHiddenX=x;}}
  check(monotonic&&tailSamples>20&&largestTailStep<2.f&&sawVisible&&sawHidden&&std::abs(firstHiddenX-lastVisibleX)<2.f,"NH-UX: drawer and surrounding gap finish collapsing smoothly without a final position jump");
  if(!monotonic||largestTailStep>=2.f||!sawHidden||std::abs(firstHiddenX-lastVisibleX)>=2.f)std::cout<<"DRAWER tail="<<largestTailStep<<" disappearance="<<std::abs(firstHiddenX-lastVisibleX)<<" hidden="<<sawHidden<<std::endl;
  model.reducedMotion=true;strcpy_s(model.search.data(),model.search.size(),"Matching second");
  for(int i=0;i<3;++i)render();auto searchHeader=window("LibraryHeader");
  if(searchHeader){GImGui->NavWindow=searchHeader;GImGui->NavId=searchHeader->GetID("##Search");render();}
  auto results=window("##LibrarySearchResults");ImRect searchRect;if(searchHeader)searchRect=ImGui::WindowRectRelToAbs(searchHeader,searchHeader->NavRectRel[ImGuiNavLayer_Main]);
  check(results&&searchHeader&&searchRect.GetWidth()>100&&std::abs(results->Pos.x-searchRect.Min.x)<1.f&&std::abs(results->Pos.y-searchRect.Max.y)<1.f&&std::abs(results->Size.x-searchRect.GetWidth())<1.f&&!window("LibraryColumn"),"NH-UX: hidden-list matches open directly beneath the search field at its width");
  if(results)std::cout<<"SEARCH-GEOMETRY field="<<searchRect.Min.x<<','<<searchRect.Min.y<<','<<searchRect.Max.x<<','<<searchRect.Max.y<<" popup="<<results->Pos.x<<','<<results->Pos.y<<" width="<<results->Size.x<<std::endl;
  if(results){const int index=1;auto seed=ImHashData(&index,sizeof(index),results->IDStack.back());render(ImHashStr(model.games[index].title.c_str(),0,seed));}
  check(model.selected==1&&model.search[0]==0,"NH-UX: selecting a matching search result switches games and clears the search");
  for(int i=0;i<3;++i)render();check(!window("##LibrarySearchResults"),"NH-UX: choosing a result dismisses the dropdown");
  if(pane)render(pane->GetID("##LibraryDrawer"));for(int i=0;i<3;++i)render();model.selected=0;
  for(int i=0;i<3;++i)render();auto choiceHeader=window("SelectedGameHeader");
  if(choiceHeader){GImGui->NavWindow=choiceHeader;GImGui->NavId=choiceHeader->GetID("Executable / installation");render();}
  ImRect choiceRect;if(choiceHeader)choiceRect=ImGui::WindowRectRelToAbs(choiceHeader,choiceHeader->NavRectRel[ImGuiNavLayer_Main]);
  bool frameOnly=choiceHeader&&choiceRect.GetWidth()>100;int choiceVertices=0;
  if(choiceHeader)for(auto& vertex:choiceHeader->DrawList->VtxBuffer)if(vertex.col==red&&vertex.pos.y>=choiceRect.Min.y&&vertex.pos.y<=choiceRect.Max.y){++choiceVertices;if(vertex.pos.x>choiceRect.Max.x+.1f||vertex.pos.x<choiceRect.Min.x-.1f)frameOnly=false;}
  check(frameOnly&&choiceVertices>0,"NH-UX: required combo outline ends at the input frame before the visible label");
  // A new ImGui context models application restart: drawer state must not leak
  // from a previously collapsed window or become a persisted model preference.
  if(pane)render(pane->GetID("##LibraryDrawer"));for(int i=0;i<3;++i)render();
  auto priorContext=ImGui::GetCurrentContext();auto freshContext=ImGui::CreateContext();ImGui::SetCurrentContext(freshContext);auto& freshIo=ImGui::GetIO();freshIo.IniFilename=freshIo.LogFilename=nullptr;freshIo.DisplaySize={1240,820};freshIo.DeltaTime=1.f/60;freshIo.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;freshIo.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);nh::ApplySharedTheme(false);
  nh::HubModel restarted;restarted.page=1;restarted.reducedMotion=true;restarted.games={{"restart","Fresh launch game","Custom","C:/fixture",{}}};restarted.selected=0;
  for(int i=0;i<3;++i){ImGui::NewFrame();nh::RenderHub(restarted,nullptr,1);ImGui::Render();}
  check(window("LibraryList")&&window("GameDetails")&&window("LibraryList")->Pos.x<window("GameDetails")->Pos.x,"NH-UX: fresh model and context restore a visible game list after the previous session hid it");
  ImGui::DestroyContext(freshContext);ImGui::SetCurrentContext(priorContext);nh::ApplySharedTheme(model.light);
  if(pane)render(pane->GetID("##LibraryDrawer"));model.games.pop_back();model.selected=0;GImGui->NavId=0;io.AddMousePosEvent(-1000,-1000);
 }
 // Folder-opening behavior is covered by AnythingUiTests with an inert folder
 // opener. This integrated fixture checks reachability without opening Explorer.
 model.page=0;for(int i=0;i<3;i++)render();auto root=window("NeuRotic");ImGuiID anythingAction=root?root->GetID("##NRAnythingControl"):0;if(root){GImGui->NavWindow=root;GImGui->NavId=anythingAction;render();}check(anythingAction&&GImGui->NavIdIsAlive&&GImGui->NavId==anythingAction,"NH-UX: global NR Anything action is reachable from Home");io.DisplaySize.x=1240;
 nh::AnythingUiState options;auto request=nh::AnythingStartRequest(options);check(request.value("mode","")=="countdown"&&request.value("seconds",0u)==3&&!request.contains("window"),"NH-NR: None defaults to a three-second foreground countdown");
 check(!HasDeferredGuideControls<nh::AnythingUiState>,"NH-NR: deferred guide and bundle controls are absent from App state");
 check(request.value("guides","")=="off"&&!request.contains("depthBundle"),"NH-NR: countdown requests preserve the frozen guides-off interface");
 check(request.value("nrScalePercent",0)==67,"NH-NR: global countdown starts at the accepted Balanced 67 percent resolution");
 for(int index=0;index<4;++index){options.resolution=index;request=nh::AnythingStartRequest(options);const int expected[]={100,75,67,50};check(request.value("nrScalePercent",0)==expected[index],"NH-NR: global start preserves each accepted resolution preset");}options.resolution=2;
 options.selected={{"hwnd",101ull},{"pid",999999u},{"processCreation",123456ull},{"title","Media"},{"executable","C:/Media/player.exe"},{"windowClass","Fixture"}};request=nh::AnythingStartRequest(options);check(request.value("mode","")=="selected"&&request["window"]==options.selected&&!request.contains("seconds"),"NH-NR: explicit selection targets the complete identity directly");options.selected=nh::Json();
 check(request.value("nrScalePercent",0)==67,"NH-NR: explicit window request retains Balanced resolution independently of countdown selection");
 check(request.value("guides","")=="off"&&!request.contains("depthBundle"),"NH-NR: selected-window requests preserve the frozen guides-off interface");
 nh::AnythingSnapshot snapshot;snapshot.active=true;snapshot.phase="Countdown";bool countdown=true;for(unsigned seconds=5;seconds;--seconds){snapshot.status["countdownRemainingMs"]=uint64_t(seconds)*1000-200;countdown&=nh::AnythingActionLabel(options,snapshot)=="NR Anything · "+std::to_string(seconds);}check(countdown,"NH-NR: header action follows every worker countdown value from 5 through 1");snapshot.phase="Ready";snapshot.status=nh::Json::object();options.awaitingCountdown=true;check(nh::AnythingActionLabel(options,snapshot)=="NR Anything · 3","NH-NR: an accepted countdown start shows three before the first worker event");snapshot.phase="Running";snapshot.status={{"nrCompleted",1u}};check(nh::AnythingActionLabel(options,snapshot)=="Stop","NH-NR: running state replaces the countdown with Stop");snapshot.stopping=true;check(nh::AnythingActionLabel(options,snapshot)=="Stopping...","NH-NR: stopping state never offers another start");
 { // Configurable countdown request and label regression checks.
 nh::AnythingUiState page;auto request=nh::AnythingStartRequest(page);
 check(request.value("seconds",0u)==3,"Fresh App request uses a three-second foreground countdown");
 for(int seconds:{1,3,10,30}){page.countdownSeconds=seconds;check(nh::AnythingStartRequest(page).value("seconds",0u)==seconds,"Configured countdown request preserves supported duration");}
 page.countdownSeconds=0;check(nh::AnythingStartRequest(page).value("seconds",0u)==1,"Countdown draft is bounded at one second");
 page.countdownSeconds=31;check(nh::AnythingStartRequest(page).value("seconds",0u)==30,"Countdown draft is bounded at thirty seconds");
 nh::AnythingSnapshot snapshot;snapshot.active=true;snapshot.phase="Countdown";snapshot.status["countdownRemainingMs"]=uint64_t(29750);
 check(nh::AnythingActionLabel(page,snapshot)=="NR Anything · 30","Worker countdown labels support the existing thirty-second range");
 snapshot.phase="Ready";snapshot.status=nh::Json::object();page.awaitingCountdown=true;page.activeCountdownSeconds=7;page.countdownSeconds=2;
 check(nh::AnythingActionLabel(page,snapshot)=="NR Anything · 7","Accepted countdown label retains its request when the next-start draft changes");
 snapshot.phase="Countdown";snapshot.status["countdownRemainingMs"]=uint64_t(5900);
 check(nh::AnythingActionLabel(page,snapshot)=="NR Anything · 6","Worker remaining time replaces the provisional label");
 snapshot.stopping=true;check(nh::AnythingActionLabel(page,snapshot)=="Stopping...","Stop state overrides countdown label");
 page.selected={{"hwnd",101ull},{"pid",999999u},{"processCreation",123456ull},{"title","Media"},{"executable","C:/Media/player.exe"},{"windowClass","Fixture"}};
 request=nh::AnythingStartRequest(page);check(request.value("mode","")=="selected"&&request["window"]==page.selected&&!request.contains("seconds"),"Selected windows retain immediate start and full identity");
 check(request.value("guides","")=="off","Countdown change preserves guides off");
 }
 // Optional actual pipe fixture: no capture/provider entry, no real application target.
 wchar_t fixture[32768]{};if(GetEnvironmentVariableW(L"NEUROTIC_HUB_PROTOCOL_FIXTURE",fixture,32768)){
  auto data=nh::AppRoot().parent_path()/L"ux-protocol";std::filesystem::create_directories(data);
  auto guard=data/L"unrelated.txt",legacy=data/L"anything.pending.json";const std::string sentinel="Unrelated model-preference data";
  std::ofstream(guard,std::ios::binary)<<sentinel;if(std::filesystem::exists(legacy))throw std::runtime_error("Inspect prior model preference temporary file");
  if(!CreateHardLinkW(legacy.c_str(),guard.c_str(),nullptr))throw std::runtime_error("Cannot construct model preference hardlink fixture");
  model.anything=std::make_shared<nh::AnythingController>(fixture,data);model.anything->Connect();auto wait=[&](auto predicate){auto end=GetTickCount64()+5000;while(!predicate()&&GetTickCount64()<end){render();Sleep(5);}return predicate();};
  check(wait([&]{auto s=model.anything->Snapshot();return s.connected&&!s.busy;}),"NH-NR: global action fixture connects");model.anything->SelectModel(data/L"fixture-model.dll",false);check(wait([&]{auto s=model.anything->Snapshot();return s.ready&&!s.busy&&!s.windows.empty();}),"NH-NR: header services the window catalog without visiting settings");
  {std::ifstream in(guard,std::ios::binary);std::string observed(std::istreambuf_iterator<char>(in),{});check(observed==sentinel&&model.anything->Snapshot().modelRevision>0,"NH-NR: actual model acknowledgment saves preference without overwriting a linked temporary referent");}std::filesystem::remove(legacy);
  model.page=0;root=window("NeuRotic");render(root->GetID("##NRAnythingControl"));check(wait([&]{return model.anything->Snapshot().phase=="Countdown";}),"NH-NR: global Home action starts the actual countdown request");check(model.anything->Snapshot().status.value("receivedStart",nh::Json::object()).value("seconds",0u)==3,"NH-NR: actual worker receives the default three seconds");model.page=4;render(root->GetID("##NRAnythingControl"));check(wait([&]{auto s=model.anything->Snapshot();return !s.active&&!s.stopping&&!s.busy;}),"NH-NR: same global action cancels the countdown from another page");
  model.anythingUi.selected=model.anything->Snapshot().windows.at(0);render(root->GetID("##NRAnythingControl"));check(wait([&]{auto s=model.anything->Snapshot();return s.phase=="Running"&&!s.busy;}),"NH-NR: global action targets an explicitly selected window directly");
  render(root->GetID("##NRAnythingControl"));check(wait([&]{auto s=model.anything->Snapshot();return !s.active&&!s.stopping&&!s.busy;}),"NH-NR: global Stop ends rendering without automatically starting another target");
  model.anythingUi.selected=nh::Json();model.anythingUi.countdownSeconds=9;render(root->GetID("##NRAnythingControl"));model.anythingUi.countdownSeconds=2;check(wait([&]{return model.anything->Snapshot().phase=="Countdown";}),"NH-NR: a separate Select starts the next countdown off the settings page");check(model.anything->Snapshot().status.value("receivedStart",nh::Json::object()).value("seconds",0u)==9&&model.anythingUi.activeCountdownSeconds==9,"NH-NR: accepted start retains its requested countdown despite later draft edits");render(root->GetID("##NRAnythingControl"));wait([&]{return !model.anything->Snapshot().stopping;});model.anything.reset();
 }
 model.page=4;model.operationIssue={{"decisionKind","AntiCheatRisk"},{"target","C:/fixture/choose.exe"},{"antiCheat",{{"headline","detected"},{"scan_status","complete"},{"fingerprint","fixture"}}}};model.showIssue=true;
 for(int i=0;i<3;i++)render();auto popup=window("Game needs attention"),body=window("IssueExplanation");
 check(popup&&body,"NH-UX: anti-cheat acknowledgment appears as a focused dialog");
 if(popup&&body){auto proceed=popup->GetID("Proceed"),checkbox=body->GetID("I understand the risk and want to continue");auto before=model.message;render(proceed);check(model.message==before&&model.showIssue,"NH-UX: Proceed cannot act before the checkbox is selected");
  render(checkbox);render(proceed);check(model.message=="Review this game's warning again before continuing"&&model.showIssue,"NH-UX: checked Proceed still requires the current game/action approval owner");
  render(popup->GetID("Cancel"));check(!model.showIssue&&!model.showReview,"NH-UX: Cancel closes the dialog without installing or planning");
 }
 for(int i=0;i<3;i++)render();model.operationIssue["antiCheat"]["scan_status"]="cancelled";model.showIssue=true;model.message="Canceled fixture";for(int i=0;i<3;i++)render();popup=window("Game needs attention");body=window("IssueExplanation");
 if(popup&&body){render(body->GetID("I understand the risk and want to continue"));render(popup->GetID("Proceed"));check(model.showIssue&&model.message=="Canceled fixture","NH-UX: a canceled anti-cheat check cannot be approved even with acknowledgment");render(popup->GetID("Cancel"));}
 for(float width:{880.f,1240.f}){
  io.DisplaySize={width,600};for(int i=0;i<3;i++)render();model.operationIssue={{"status","FailedWithoutMutation"},{"reason","Restore record needs inspection."}};model.bundleStatus="Diagnostic bundle ready.";model.bundlePath="C:/fixture/diagnostics.zip";model.showIssue=true;
  for(int i=0;i<3;i++)render();popup=window("Game needs attention");
  bool closeVisible=false;if(popup){auto close=popup->GetID("Close");GImGui->NavWindow=popup;GImGui->NavId=close;render();auto rect=ImGui::WindowRectRelToAbs(popup,popup->NavRectRel[ImGuiNavLayer_Main]);closeVisible=GImGui->NavIdIsAlive&&popup->InnerClipRect.Contains(rect)&&popup->ScrollMax.y==0;render(close);}
  check(closeVisible&&!model.showIssue,"NH-UX: generic issue keeps its export actions and Close visible without whole-dialog scrolling");
 }
 ImGui::DestroyContext();return failed;
}
