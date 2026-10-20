#include "NrDllQueueTests.cpp"
#include <ui/NrDllManagerView.h>
#include <menu/localization/LanguageRuntime.h>
#include "imgui_internal.h"
#include <menu/font/Hack_Compressed.h>
int RunNrDllManagerUiTests(){
 int checks=0,failed=0;auto check=[&](bool v,const char* why){++checks;failed+=!v;std::cout<<(v?"PASS ":"FAIL ")<<why<<'\n';};
 const auto fixture=nh::UserRoot()/L"nr-manager-ui";std::filesystem::create_directories(fixture);const auto source=fixture/L"nvngx_dlssnr.dll";Write(source,"inert source bytes, never loaded");
 nh::NrDllProfiles store(fixture/L"vault",true);const auto first=store.Import(source,"User label ## stays visible %s {value}");const auto second=store.Import(source,std::string(240,'W'));
 nh::HubModel model;model.nrDllManager=std::make_shared<nh::NrDllManager>(fixture/L"vault",true);model.nrDllManager->Refresh();DrainNr(model);model.nrDllSelectedId=first.id;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1240,820};io.DeltaTime=1.f/60;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);
 bool picker=false;model.games={Game(fixture,"selection")};
 bool closedPreviewReached=false;std::string rendered;auto frame=[&](ImGuiID activate=0){model.nrDllManager->Poll();ImGui::NewFrame();if(activate)GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=activate;ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);ImGui::SetNextWindowSize({std::max(1.f,std::min(680.f,io.DisplaySize.x-40)),std::max(1.f,std::min(460.f,io.DisplaySize.y-40))},ImGuiCond_Always);ImGui::Begin("Host");ImGui::LogToBuffer();if(picker){nh::NrDllProfilePicker(model,model.games[0]);const auto rect=GImGui->ComboPreviewData.PreviewRect;closedPreviewReached=rect.Min.x==0&&rect.Min.y==0&&rect.Max.x==0&&rect.Max.y==0;}else nh::DrawNrDllManager(model,1);rendered=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();};
 model.showNrDllManager=true;for(int i=0;i<3;++i)frame();auto modal=ImGui::GetTopMostPopupModal();
 check(modal&&modal->Size.x<=io.DisplaySize.x&&modal->Size.y<=io.DisplaySize.y,"actual manager modal fits narrow viewport");
 check(rendered.find(first.name)!=std::string::npos,"user profile label syntax is drawn verbatim without translation or hidden-ID parsing");
 io.DisplaySize={880,600};for(int i=0;i<3;++i)frame();modal=ImGui::GetTopMostPopupModal();
 check(modal&&modal->Size.x<=io.DisplaySize.x&&modal->Size.y<=io.DisplaySize.y,"open manager adapts when the actual viewport is resized from wide to narrow");
 const auto id=modal?modal->ID:0;
 Neurotic::Localization::LanguagePack labels;labels.locale="en";
 labels.entries["desktop.nrdll.manager_title"]={"Saved rendering files for all of your game installations",{},Neurotic::Localization::CanonicalEnglish().at("desktop.nrdll.manager_title").revision};
 labels.entries["desktop.nrdll.manager_help"]={std::string(350,'L'),{},Neurotic::Localization::CanonicalEnglish().at("desktop.nrdll.manager_help").revision};
 Neurotic::Localization::SetSharedCatalog(std::move(labels));for(int i=0;i<3;++i)frame();modal=ImGui::GetTopMostPopupModal();
 check(modal&&modal->ID==id&&rendered.find("Saved rendering files for all of your game installations")!=std::string::npos,"live authored language updates modal text without changing its identity");
 check(modal&&modal->Size.x<=io.DisplaySize.x&&modal->Size.y<=io.DisplaySize.y,"long authored labels remain in a scrollable bounded modal");
 Neurotic::Localization::SetSharedCatalog({});for(int i=0;i<2;++i)frame();modal=ImGui::GetTopMostPopupModal();
 if(modal)frame(ImHashStr("Set default",0,ImHashStr("nr-actions",0,modal->IDStack.back())));DrainNr(model);frame();
 check(store.List().defaultId==first.id,"actual Set default button changes only the selected saved profile");
 model.nrDllEdit="remove";frame();
 check(rendered.find("Installed game DLLs and the original download stay unchanged")!=std::string::npos,"removal asks for explicit confirmation and explains installed-file preservation");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
 check(model.showNrDllManager&&model.nrDllEdit.empty(),"first Escape abandons removal confirmation without deleting a profile");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
 check(!model.showNrDllManager&&store.List().profiles.size()==2,"second Escape closes manager and preserves saved profiles");
 picker=true;const auto library=nh::UserRoot()/L"library.json";const bool hadLibrary=std::filesystem::exists(library);const auto before=hadLibrary?Read(library):Json();frame();frame();
 check(rendered.find(first.name)!=std::string::npos,"closed profile preview retains the complete literal ## suffix");
 const auto* visibleHost=ImGui::FindWindowByName("Host");check(visibleHost&&visibleHost->InnerClipRect.GetWidth()>100&&visibleHost->InnerClipRect.GetHeight()>100&&closedPreviewReached,"bounded visible host reaches actual custom closed-preview drawing");
 check(std::filesystem::exists(library)==hadLibrary&&(!hadLibrary||Read(library)==before),"drawing per-game selector does not write preferences or select a fallback");
 Write(fixture/L"vault/default.json",Json{{"schemaVersion",1},{"profileId",std::string(32,'f')}}.dump());model.nrDllManager->Refresh();DrainNr(model);frame();
 const auto& missingCatalog=model.nrDllManager->Catalog();check(missingCatalog.defaultId==std::string(32,'f')&&!nh::NrDllDefaultIssue(missingCatalog)&&std::any_of(missingCatalog.issues.begin(),missingCatalog.issues.end(),[](const auto& issue){return issue.profileId==std::string(32,'f')&&issue.code==nh::NrDllErrorCode::MissingProfile;}),"valid default schema retains missing ID and reports profile-specific absence, not unreadable metadata");
 check(rendered.find("Default unavailable")!=std::string::npos,"nonempty saved default pointing to a missing profile is unavailable, never no-default");
 model.SetNrDllChoice(model.games[0],{nh::NrDllChoiceMode::Profile,first.id});frame();
 check(rendered.find(first.name)!=std::string::npos&&rendered.find("Default unavailable")==std::string::npos,"explicit valid profile stays visible independently of a missing default");model.SetNrDllChoice(model.games[0],{});
 store.SetDefault(first.id);const auto metadata=first.dllPath.parent_path()/L"profile.json";const auto savedMetadata=Read(metadata);Write(metadata,"{ unreadable configured profile");model.nrDllManager->Refresh();DrainNr(model);frame();
 check(rendered.find("Default unavailable")!=std::string::npos,"unreadable configured default profile is unavailable, never no-default");
 Write(metadata,savedMetadata.dump());store.ClearDefault();model.nrDllManager->Refresh();DrainNr(model);frame();
 check(rendered.find("No default selected")!=std::string::npos&&rendered.find("Default unavailable")==std::string::npos,"legitimate empty default remains distinct from a missing configured profile");
 store.SetDefault(first.id);model.nrDllManager->Refresh();DrainNr(model);frame();
 auto host=ImGui::FindWindowByName("Host");if(host)frame(host->GetID("##nr-dll-choice"));
 ImGuiWindow* popup=nullptr;for(const auto& candidate:GImGui->OpenPopupStack)if(candidate.Window&&candidate.Window->Active)popup=candidate.Window;
 if(popup)frame(popup->GetID("Manual — do not copy an NR DLL"));frame();
 check(model.NrDllChoiceFor(model.games[0]).mode==nh::NrDllChoiceMode::Manual,"actual per-game dropdown permits explicit manual installation");
 Write(fixture/L"vault/default.json","{ invalid default");model.nrDllManager->Refresh();DrainNr(model);model.SetNrDllChoice(model.games[0],{});frame();
 check(rendered.find("Default unavailable")!=std::string::npos,"unreadable default is distinct from legitimate no-default in the actual closed picker");
 picker=false;model.showNrDllManager=true;for(int i=0;i<3;++i)frame();modal=ImGui::GetTopMostPopupModal();
 check(modal&&rendered.find("Clear default")!=std::string::npos&&rendered.find("Your other profiles remain available")!=std::string::npos,"manager exposes explicit unreadable-default recovery without hiding valid profiles");
 if(modal)frame(modal->GetID("Clear default"));DrainNr(model);frame();
 check(store.List().defaultId.empty()&&!nh::NrDllDefaultIssue(store.List()),"actual Clear default action repairs unreadable metadata explicitly");
 model.showNrDllManager=false;frame();store.SetDefault(first.id);model.nrDllManager->Refresh();DrainNr(model);
 model.nrDllManager->Import(source,"");DrainNr(model);
 check(!model.nrDllManager->Catalog().profiles.empty()&&nh::NrDllManagerAvailability(model)==nh::ui::ReadyState::Error,"failed manager operation cannot present its stale catalog as ready");
 model.nrDllManager->Refresh();DrainNr(model);
 check(nh::NrDllManagerAvailability(model)==nh::ui::ReadyState::Ready,"successful refresh restores truthful available-file status");
 std::filesystem::remove(first.dllPath);std::filesystem::remove(second.dllPath);model.nrDllManager->Refresh();DrainNr(model);
 check(!model.nrDllManager->Catalog().profiles.empty()&&nh::NrDllManagerAvailability(model)!=nh::ui::ReadyState::Ready,"nonempty profiles with missing real files are not reported ready");
 ImGui::DestroyContext();std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_IMGUI_DRAW_DATA=true NO_GRAPHICS_DEVICE=true NO_PROVIDER_LOAD=true\n";return failed;
}
#ifdef NEUROTIC_NR_DLL_MANAGER_UI_STANDALONE
int main()try{return RunNrDllManagerUiTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
#endif
