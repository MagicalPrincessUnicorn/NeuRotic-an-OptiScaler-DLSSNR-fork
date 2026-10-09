#include "ui/HubViewModel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
#include <algorithm>
#include <iostream>

int RunLibraryPolishUiTests() {
    wchar_t fixtureRoot[32768]{};
    if(!GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixtureRoot,32768)) {
        std::cerr<<"FAIL LIBRARY-UI: explicit fixture root is required\n";return 1;
    }
    if(std::filesystem::exists(nh::UserRoot()/L"library.json")) {
        std::cerr<<"FAIL LIBRARY-UI: use a fresh fixture without library.json\n";return 1;
    }
    int failed=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<"LIBRARY-UI: "<<label<<'\n';if(!ok)++failed;};
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
    io.DisplaySize={1240,820};io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);
    nh::ApplySharedTheme(false);
    nh::HubModel model;model.page=1;model.selected=0;model.reducedMotion=true;model.onlineArtwork=false;
    // Unsuitable, nonexistent targets cannot inspect or mutate a real game.
    model.games={{"ui-a","A fixture game","Custom",nh::Utf8((nh::UserRoot()/L"A").wstring()),{}},
                 {"ui-b","B fixture game","Custom",nh::Utf8((nh::UserRoot()/L"B").wstring()),{}}};
    auto window=[](const char* fragment){for(auto w:GImGui->Windows)if(w->Active&&strstr(w->Name,fragment))return w;return static_cast<ImGuiWindow*>(nullptr);};
    std::string text;
    auto render=[&](ImGuiID activate=0){
        ImGui::NewFrame();if(activate)GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=activate;
        ImGui::LogToBuffer();nh::RenderHub(model,nullptr,1);text=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::Render();
    };
    for(int i=0;i<3;++i)render();
    auto header=window("SelectedGameHeader");
    check(header!=nullptr,"selected game has a title header");
    if(header)render(header->GetID("##GameVisibility"));
    render();
    check(model.games[0].hidden&&model.selected==0&&model.OrderedGames().size()==2,"eye hides while keeping current game and row selected");
    auto list=window("LibraryList");
    if(list){const auto seed=ImHashStr("ui-b",0,list->IDStack.back());render(ImHashStr("##Game",0,seed));}
    render();
    check(model.selected==1&&model.OrderedGames()==std::vector<int>{1},"clicking another row dismisses the hidden game");
    model.SetLibraryFilter(nh::LibraryFilter::Hidden);model.Select(0);for(int i=0;i<3;++i)render();
    check(text.find("Hidden")!=std::string::npos&&model.OrderedGames()==std::vector<int>{0},"Hidden dropdown mode displays its hidden game");
    header=window("SelectedGameHeader");if(header)render(header->GetID("##GameVisibility"));render();
    check(!model.games[0].hidden&&model.selected==0&&model.OrderedGames()==std::vector<int>{0},"eye shows a game without immediately removing Hidden selection");
    model.page=4;render();model.page=1;render();
    check(model.selected==-1&&model.OrderedGames().empty(),"leaving and returning removes the shown game and its stale details");
    model.SetLibraryFilter(nh::LibraryFilter::All);model.Select(0);for(int i=0;i<3;++i)render();
    header=window("SelectedGameHeader");if(header)render(header->GetID("##GameVisibility"));
    model.page=4;render();model.page=1;render();
    check(model.selected==-1&&model.games[0].hidden&&model.OrderedGames()==std::vector<int>{1},"page navigation dismisses a newly hidden game's detail pane");

    model.page=4;for(int i=0;i<3;++i)render();
    check(text.find("Clear App data folder")!=std::string::npos&&text.find("Clear Game Profiles folder")!=std::string::npos&&text.find("Open Artwork Cache")!=std::string::npos,"Settings presents the three explicit data-maintenance actions");
    check(text.find("Uninstall NeuRotic from all games")==std::string::npos&&text.find("Uninstall all games")==std::string::npos,"Settings has no bulk game-uninstall action");

    model.page=1;model.SetLibraryFilter(nh::LibraryFilter::All);model.Select(1);
    model.showUninstallConfirm=true;for(int i=0;i<3;++i)render();
    check(!model.uninstallPermanent&&!model.uninstallRemoveSettings&&
          !model.uninstallCategories[3]&&!model.uninstallCategories[4]&&!model.uninstallCategories[5],
          "uninstall opens with permanent removal and optional deletions unchecked");
    check(text.find("Remove NeuRotic settings")!=std::string::npos&&text.find("Remove verified backup records")!=std::string::npos&&text.find("Remove private NR models")!=std::string::npos&&text.find("Remove screenshots")!=std::string::npos,"normal uninstall exposes separate settings, backups, models and screenshot options");
    auto modal=ImGui::GetTopMostPopupModal();if(modal)render(modal->GetID("Permanent sanitize-style removal (skip restoration)"));render();
    check(model.uninstallPermanent&&text.find("Installed files, settings and logs")!=std::string::npos&&text.find("Review permanent cleanup")!=std::string::npos,"permanent option reveals category choices and a review action");
    modal=ImGui::GetTopMostPopupModal();if(modal)render(modal->GetID("Cancel"));render();
    check(!model.showUninstallConfirm&&!model.installer.busy,"cancel closes uninstall without starting a worker");
    model.operationIssue={{"decisionKind","UninstallCleanup"},{"status","FailedWithChanges"},{"originalsRestored",true},
        {"reason","Optional cleanup incomplete"},{"deleted",nh::Json::array({"OptiScaler.log"})},{"remaining",nh::Json::array({"Screenshots/kept.png"})}};
    model.showIssue=true;for(int i=0;i<3;++i)render();
    check(text.find("original game files were restored")!=std::string::npos&&text.find("Review recovery")==std::string::npos&&text.find("Screenshots/kept.png")!=std::string::npos,
          "optional cleanup failure reports restored originals and remaining files without offering transaction recovery");
    check(!model.anything&&!model.discovery.busy&&!model.installer.busy,"fixture never starts capture, discovery or a game operation");
    ImGui::DestroyContext();std::filesystem::remove(nh::UserRoot()/L"library.json");
    return failed;
}
