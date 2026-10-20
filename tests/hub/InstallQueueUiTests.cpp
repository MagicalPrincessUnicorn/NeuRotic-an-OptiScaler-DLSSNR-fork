#include "InstallQueueTests.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/GameSettingsView.h"
#include "BackgroundScanUiProof.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
namespace nh {void ApplySharedTheme(bool);}
int RunInstallQueueUiTests(){
 int checks=0,failed=0;auto check=[&](bool value,const char* label){++checks;failed+=!value;std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;};
 const auto fixture=nh::UserRoot()/L"install-queue-ui";std::filesystem::create_directories(fixture);
 nh::HubModel model;model.games={Game(fixture,"ui-queue-a"),Game(fixture,"ui-queue-b"),Game(fixture,"ui-queue-c")};for(auto& game:model.games)game.target=nh::InspectExecutable(nh::Wide(game.target.path));
 model.games[0].title="First queued game with a long installation title";model.games[1].title="Second queued game with a different installation title";
 model.page=1;model.selected=0;model.reducedMotion=true;model.onlineArtwork=false;model.ChooseProxy(0);model.Plan("Install");const auto first=Read(model.installer.request);const auto firstChild=model.installer.request;model.Select(1);model.ChooseProxy(1);
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1240,820};io.DeltaTime=1.f/60.f;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16.f);nh::ApplySharedTheme(false);
 auto window=[](const char* fragment){for(auto w:GImGui->Windows)if(w->Active&&strstr(w->Name,fragment))return w;return static_cast<ImGuiWindow*>(nullptr);};
  const auto usable=[&](ImGuiWindow* pane){auto page=window("PageContent");if(!pane||!page)return false;auto rect=pane->InnerClipRect;rect.ClipWith(page->InnerClipRect);return rect.GetWidth()>=120&&rect.GetHeight()>=120;};
 // Current shipping queue is a toolbar-opened modal with a stable ### ID.
 auto queuePopup=[&](){return window("###InstallQueueDetails");};
 std::string rendered;auto render=[&](ImGuiID activate=0){ImGui::NewFrame();if(activate)GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=activate;ImGui::LogToBuffer();nh::RenderHub(model,nullptr,1);rendered=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::Render();};
 auto openQueue=[&](){if(auto header=window("LibraryHeader"))render(header->GetID("Install queue"));for(int i=0;i<3;++i)render();};
 auto queueAction=[&](uint64_t id,const char* label){if(auto rows=window("InstallQueueRows")){const auto table=ImHashStr("InstallQueueTable",0,rows->IDStack.back());const auto opaque=reinterpret_cast<void*>(static_cast<uintptr_t>(id));const auto seed=ImHashData(&opaque,sizeof(opaque),table);render(ImHashStr(label,0,seed));for(int i=0;i<3;++i)render();}};
 for(int i=0;i<3;++i)render();
 ImGuiID installationTab=0;for(auto& bar:GImGui->TabBars.Buf)for(auto& tab:bar.Tabs)if(std::strcmp(ImGui::TabBarGetTabName(&bar,&tab),"Installation")==0)installationTab=tab.ID;
 if(auto details=window("SelectedGameDetails")){ImGui::SetScrollY(details,details->ScrollMax.y);for(int i=0;i<3;++i)render();}
 if(installationTab)render(ImHashStr("Install",0,installationTab));
 check(model.InstallQueueItems().size()==2&&model.installer.request==firstChild,"real Installation button accepts B while A Plan retains sole child");
 if(model.InstallQueueItems().size()!=2){model.Plan("Install");} // Preserve later layout coverage after an interaction assertion failure.
 Feed(model,QueuePlan(first,"ui-plan-a",QueueDefaults()));
 const auto writer=model.installer.request;
 for(bool light:{false,true})for(const auto display:{ImVec2{1240,820},ImVec2{880,600}}){
  model.light=light;nh::ApplySharedTheme(light);io.DisplaySize=display;for(int i=0;i<3;++i)render();
  auto list=window("LibraryList"),detailsPane=window("GameDetails");
  check(usable(list)&&usable(detailsPane),"actual visible library and game panes retain usable clipped area at normal and narrow sizes");
  check(!queuePopup()&&!window("SelectedGameLoadingShade")&&model.installer.request==writer,"queue-owned writer leaves browsed library and game pane unobscured");
  uint64_t cancelId=0;
  if(display.x==880){
   if(list){ImGui::SetScrollY(list,list->ScrollMax.y);for(int i=0;i<3;++i)render();const auto seed=ImHashStr(model.games[2].id.c_str(),0,list->IDStack.back());render(ImHashStr("##Game",0,seed));for(int i=0;i<3;++i)render();}
   check(model.selected==2&&model.installer.request==writer,"real narrow library-row activation browses another game while A writer remains fixed");
   ImGuiID narrowInstallTab=0;for(auto& bar:GImGui->TabBars.Buf)if(bar.CurrFrameVisible==GImGui->FrameCount)for(auto& tab:bar.Tabs)if(std::strcmp(ImGui::TabBarGetTabName(&bar,&tab),"Installation")==0)narrowInstallTab=tab.ID;
   model.ChooseProxy(0);if(auto details=window("SelectedGameDetails")){ImGui::SetScrollY(details,details->ScrollMax.y);for(int i=0;i<3;++i)render();}
   if(narrowInstallTab)render(ImHashStr("Install",0,narrowInstallTab));
   const bool accepted=model.InstallQueueItems().size()>=3&&model.InstallQueueItems().back().game.id==model.games[2].id&&model.InstallQueueItems().back().phase==nh::QueuedInstall::Phase::Queued&&model.installer.request==writer;
   check(accepted,"real narrow Installation button queues the browsed game without replacing A writer");
   cancelId=accepted?model.InstallQueueItems().back().id:0;
  }
  openQueue();auto popup=queuePopup();const auto viewport=ImGui::GetMainViewport();
  check(popup&&popup->Pos.x>=viewport->Pos.x&&popup->Pos.y>=viewport->Pos.y&&popup->Pos.x+popup->Size.x<=viewport->Pos.x+viewport->Size.x+1&&popup->Pos.y+popup->Size.y<=viewport->Pos.y+viewport->Size.y+1,"real toolbar queue button opens bounded queue details at normal and narrow sizes in both themes");
  auto rows=window("InstallQueueRows");
  check(rows&&popup&&rows->OuterRectClipped.Min.y>=popup->InnerRect.Min.y&&rows->OuterRectClipped.Max.y<=popup->InnerRect.Max.y+1,"actual queue rows stay inside their modal bounds in both themes");
  check(rendered.find("Install queue")!=std::string::npos&&rendered.find("Installing")!=std::string::npos&&rendered.find("In queue")!=std::string::npos,"actual queue shows current installation and following queued game in both themes");
  if(display.x==880){
   if(rows){ImGui::SetScrollY(rows,rows->ScrollMax.y);for(int i=0;i<3;++i)render();}
   if(cancelId)queueAction(cancelId,"Cancel queued item");
   const bool cancelled=cancelId&&model.InstallQueueItems().back().id==cancelId&&model.InstallQueueItems().back().phase==nh::QueuedInstall::Phase::Cancelled;
   check(cancelled&&model.CurrentQueuedInstall()&&model.CurrentQueuedInstall()->game.id==model.games[0].id&&model.installer.request==writer,"real compact queue Cancel affects only pending C and preserves A writer");
   if(light)CaptureBackgroundScanUiProof("queue-details-narrow-light");
  }
  if(!light&&display.x==1240)CaptureBackgroundScanUiProof("queue-details-normal-dark");
  if(auto current=queuePopup())render(current->GetID("Close"));for(int i=0;i<3;++i)render();
  check(!queuePopup()&&usable(window("LibraryList"))&&usable(window("GameDetails")),"real queue Close returns to usable visible browsing panes at normal and narrow sizes");
  if(display.x==880){model.Select(1);for(int i=0;i<3;++i)render();if(auto current=window("LibraryList"))ImGui::SetScrollY(current,0);for(int i=0;i<3;++i)render();}
  if(light&&display.x==880)CaptureBackgroundScanUiProof("queue-write-narrow-light");
  if(!light&&display.x==1240)CaptureBackgroundScanUiProof("queue-write-normal-dark");
 }
 io.DisplaySize={1240,820};model.light=false;nh::ApplySharedTheme(false);for(int i=0;i<3;++i)render();
 if(auto list=window("LibraryList")){const auto seed=ImHashStr(model.games[0].id.c_str(),0,list->IDStack.back());render(ImHashStr("##Game",0,seed));}
 check(model.selected==0&&model.installer.request==writer&&!model.installer.readOnlyInstaller,"real library-row activation changes browsing while A Execute identity stays fixed");
 Feed(model,QueueDone(model.games[0],"ui-plan-a"));
 const auto noticeSerial=model.installCompletion.Pending()?model.installCompletion.Pending()->serial:0;
 check(noticeSerial!=0,"completion-eligible A receipt genuinely queues its owned completion notice");
 for(int i=0;i<3;++i)render();auto earlyCompletion=ImGui::GetTopMostPopupModal();
 check(!earlyCompletion&&model.InstallQueueBusy()&&model.installCompletion.Pending(),"A completion modal stays deferred while B is preparing in the queue");
 // A failing predecessor may show the premature modal. Dismiss it through
 // its real Done control so later warning/layout checks still execute.
 if(earlyCompletion){render(earlyCompletion->GetID("Done"));for(int i=0;i<3;++i)render();}
 const auto b=Read(model.installer.request);Feed(model,QueuePlan(b,"ui-plan-b",QueueDefaults(),true));for(int i=0;i<3;++i)render();
 auto modal=ImGui::GetTopMostPopupModal();
 check(!modal&&!model.showIssue&&model.selected==0&&model.OperationGame()->id==model.games[1].id&&model.operationIssue.value("target","")==model.games[1].target.path&&usable(window("LibraryList"))&&usable(window("GameDetails")),"delayed B warning stays on its queue item while A browsing remains usable without a modal");
 // Open the existing toolbar queue, then Review B through its table row.
 io.DisplaySize={880,600};for(int i=0;i<3;++i)render();
 const bool attentionClosed=!model.showIssue&&!ImGui::GetTopMostPopupModal();
 openQueue();const bool queueDetailsOpened=queuePopup()!=nullptr;
 if(auto rowsNow=window("InstallQueueRows")){ImGui::SetScrollY(rowsNow,0);for(int i=0;i<3;++i)render();}
 if(auto head=model.CurrentQueuedInstall())queueAction(head->id,"Review");
 for(int i=0;i<3;++i)render();modal=ImGui::GetTopMostPopupModal();
 std::cout<<"QUEUE_REVIEW_TRACE initial_closed="<<attentionClosed<<" queue_opened="<<queueDetailsOpened<<" final_modal="<<(modal!=nullptr)<<" show_issue="<<model.showIssue<<std::endl;
 check(attentionClosed&&queueDetailsOpened&&modal&&model.showIssue&&model.OperationGame()->id==model.games[1].id&&model.operationIssue.value("target","")==model.games[1].target.path,"real compact queue Review reopens the existing B-owned attention decision");
 io.DisplaySize={1240,820};for(int i=0;i<3;++i)render();
 const auto reviewedPlan=model.plan,reviewedIssue=model.operationIssue;model.showIssue=false;render();
 ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,200});ImGui::Begin("QueueSavedSettingsGuard");
 const auto restoreId=ImGui::GetID("Restore Defaults");GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=restoreId;
 nh::RenderSavedSettingsReview(model,0);const auto restoreFlags=GImGui->LastItemData.ItemFlags;ImGui::End();ImGui::Render();
 check((restoreFlags&ImGuiItemFlags_Disabled)&&model.plan==reviewedPlan&&model.operationIssue==reviewedIssue&&!model.installer.busy,"actual Restore Defaults footer is disabled for a waiting queue and cannot discard its bound warning");
 model.showIssue=true;for(int i=0;i<3;++i)render();modal=ImGui::GetTopMostPopupModal();
 CaptureBackgroundScanUiProof("queue-attention-normal-dark");
 if(modal)render(modal->GetID("Cancel"));
 check(!model.InstallQueueBusy()&&model.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Succeeded&&model.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Cancelled,"actual warning Cancel cancels only B and preserves completed A result");
 for(int i=0;i<3;++i)render();auto completedModal=ImGui::GetTopMostPopupModal();
 const bool delivered=model.installCompletion.Pending()&&model.installCompletion.Pending()->serial==noticeSerial&&completedModal;
 check(delivered,"stored A completion notice is delivered with original identity only after the queue ends");
 if(delivered){render(completedModal->GetID("Done"));for(int i=0;i<3;++i)render();}
 check(!model.installCompletion.Pending(),"real Done acknowledgement consumes the delivered completion identity without restarting work");
 Stop(model.installer);ImGui::DestroyContext();
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_IMGUI_DRAW_DATA=true NO_GRAPHICS_DEVICE=true NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_UI_STANDALONE
int main()try{return RunInstallQueueUiTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
#endif
