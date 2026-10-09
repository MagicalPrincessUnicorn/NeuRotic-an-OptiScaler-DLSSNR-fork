#pragma once
// Actual App display fixture: no discovery, private model or game operations.
#include "ui/HubViewModel.h"
#include "imgui_internal.h"
namespace nh {
inline void ApplyLibraryLayoutLongLabels(bool japanese){
 using namespace Neurotic::Localization;LanguagePack pack;pack.id="layout-label-stress";pack.locale=japanese?"ja":"en";pack.name="Layout label stress";
 const std::pair<const char*,const char*> labels[]={
  {"desktop.hubshell.all_d76f15bf",japanese?"すべてのインストール済みゲーム":"All games in the installation library"},
  {"desktop.hubshell.favorites_first_de2d3c03",japanese?"お気に入りのゲームを先頭に表示":"Favorite games shown first in the library"},
  {"desktop.hubshell.hidden_82b2ce85",japanese?"非表示のゲームライブラリー":"Hidden games in the installation library"},
  {"desktop.hubshell.search_games_9536b444",japanese?"インストール済みのゲームを検索...":"Search your installed game library..."},
  {"desktop.hubshell.add_game_9dca998e",japanese?"ゲーム実行ファイルを追加":"+ Add selected game executable"},
  {"desktop.hubshell.add_directory_476e2098",japanese?"ゲームを含むディレクトリを追加":"Add directory containing installed games"},
  {"desktop.hubshell.scan_c58dc649",japanese?"インストール済みライブラリーを検索":"Search available installation libraries"},
  {"desktop.hubshell.scanning_35a6c735",japanese?"インストール済みライブラリーを検索中...":"Searching available installation libraries..."}};
 for(auto [id,text]:labels)pack.entries[id]={text,"",CanonicalEnglish().at(id).revision};SetSharedCatalog(std::move(pack));
 for(auto [id,text]:labels)if(SharedText(id).text!=text)throw std::runtime_error("Library long-label fixture catalog was not applied");
}
inline void PrepareLibraryLayoutFixture(HubModel& model,int frames,const std::filesystem::path& output){
 model.loaded=false;model.page=1;model.onlineArtwork=false;
 if(model.games.empty()&&frames==0){
  for(int i=0;i<24;++i){Game game;game.id="layout-"+std::to_string(i);game.title="Fixture game "+std::to_string(i);game.store="Steam";game.root=Utf8((output/L"inert-games").wstring());game.steamRoot=Utf8((output/L"steam").wstring());game.storeId=std::to_string(730+i);game.target={game.root+"/Fixture.exe","Fixture",true,"",64};game.favorite=i==1;model.games.push_back(game);}
  model.selected=0;model.inspection={{"state",nullptr},{"settings",Json::array()}};model.preflight={{"runtime",{{"ready",true}}},{"components",Json::object()}};
 }
 if(frames==20)model.selected=1;
 if(frames==40)model.selected=2;
 if(frames==60)model.selected=3;
 if(frames==80){model.selected=0;model.games[0].title="Monster Hunter Wilds";model.games[0].target.suitable=false;model.inspectionCached=true;}
 model.discovery.busy=frames>=100&&frames<120;model.discovery.progress=.4f;model.discovery.started=GetTickCount64()-12000;
 if(frames==120){model.inspectionCached=false;model.message="Fixture scan failed. Previous library retained.";}
 if(frames==140){model.selected=-1;model.search.fill(0);strcpy_s(model.search.data(),model.search.size(),"No matching fixture");}
 if(frames==160){model.selected=0;strcpy_s(model.search.data(),model.search.size(),"Fixture game 1");for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"PageContent"))w->StateStorage.SetBool(w->GetID("##LibraryListCollapsed"),true);}
 if(frames==180){model.search.fill(0);model.selected=0;model.games[0].target.suitable=true;for(auto* w:GImGui->Windows)if(w->Active&&std::strstr(w->Name,"PageContent"))w->StateStorage.SetBool(w->GetID("##LibraryListCollapsed"),false);}
 if(frames==200){model.games.clear();model.selected=-1;}
}
inline const char* LibraryLayoutState(int frames){
 const char* names[]={"idle-portrait","square-art","landscape-art","missing-art","conditional-notice","scanning","scan-error","no-match","collapsed-search","restored","empty"};return names[std::clamp(frames/20,0,10)];
}
inline Json MeasureLibraryLayout(HWND handle,float scale){
 RECT outer{},client{};GetWindowRect(handle,&outer);GetClientRect(handle,&client);Json windows=Json::array();
 for(auto* w:GImGui->Windows)if(w->Active)windows.push_back({{"name",w->Name},{"position",{w->Pos.x,w->Pos.y}},{"size",{w->Size.x,w->Size.y}},{"inner",{w->InnerRect.Min.x,w->InnerRect.Min.y,w->InnerRect.Max.x,w->InnerRect.Max.y}},{"clip",{w->InnerClipRect.Min.x,w->InnerClipRect.Min.y,w->InnerClipRect.Max.x,w->InnerClipRect.Max.y}},{"scrollMax",{w->ScrollMax.x,w->ScrollMax.y}}});
 return {{"uiScale",scale},{"nativeWindowDpi",GetDpiForWindow(handle)},{"outerSize",{outer.right-outer.left,outer.bottom-outer.top}},{"clientSize",{client.right,client.bottom}},{"windows",windows},{"displayOnly",true},{"gameOperations",false},{"effectiveToolbarLabels",{{"filter",Neurotic::UiText("desktop.hubshell.all_d76f15bf")},{"search",Neurotic::UiText("desktop.hubshell.search_games_9536b444")},{"add",Neurotic::UiText("desktop.hubshell.add_game_9dca998e")},{"directory",Neurotic::UiText("desktop.hubshell.add_directory_476e2098")},{"scan",Neurotic::UiText("desktop.hubshell.scan_c58dc649")},{"scanning",Neurotic::UiText("desktop.hubshell.scanning_35a6c735")}}}};
}
}
