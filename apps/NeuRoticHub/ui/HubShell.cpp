#include "GameInstallationStatus.h"
#include "languages/LanguageMessages.h"
#include <menu/Localization.h>
#include "languages/LanguageManagerView.h"
#include "HubViewModel.h"
#include "ObjectRuleHost.h"
#include "imgui.h"
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <shlobj.h>
#include "SleekWidgets.h"
#include "ActionFont.h"
#include "NumericSettings.h"
#include "KeybindCapture.h"
#include "anything/AnythingView.h"
#include "artwork/ArtworkService.h"
#include "library/GameCatalog.h"
#include "UpdateStatus.h"
#include "GameSettingsView.h"
#include "InstallDefaultsView.h"
#include <d3d11.h>
#include <wrl/client.h>
namespace nh {
static constexpr ImGuiWindowFlags FixedPane = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
static bool RefreshButton(const char* id,float side,float dpi,bool busy=false){
 ImGui::BeginDisabled(busy);bool pressed=ui::Button(id,{side,side});auto pos=ImGui::GetItemRectMin();ui::Glyph(ImGui::GetWindowDrawList(),8,{pos.x+(side-22*dpi)*.5f,pos.y+(side-22*dpi)*.5f},22*dpi,ImGui::GetColorU32(ImGuiCol_Text),busy);ImGui::EndDisabled();return pressed;
}

struct HeroSlides {
 struct Image {Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;float width=0,height=0;};
 std::string key;Image current,outgoing;double started=0;
 void Update(const std::string& next,ArtworkView image,double now,bool reduced){
  if(next!=key){outgoing=current;current={};key=next;started=now;}
  if(image&&current.texture.Get()!=image.texture){if(current.texture)outgoing=current;current.texture=(ID3D11ShaderResourceView*)image.texture;current.width=image.width;current.height=image.height;started=now;}
  if(reduced||now-started>=.28)outgoing={};
 }
 float Progress(double now,bool reduced)const{float p=reduced?1.f:std::clamp(float((now-started)/.28),0.f,1.f);return 1-(1-p)*(1-p)*(1-p);}
};
static void HeroBackground(HubModel& model,ImVec2 window,ImVec2 size,float dpi){
 if(!model.heroSlides)model.heroSlides=std::make_shared<HeroSlides>();std::string key;ArtworkView hero;
 if(model.selected>=0&&model.selected<(int)model.games.size()){auto& game=model.games[model.selected];key=game.id;if(model.artwork)hero=model.artwork->Get(game,"hero",model.onlineArtwork);}
 auto& slides=*model.heroSlides;double now=ImGui::GetTime();slides.Update(key,hero,now,model.reducedMotion);float progress=slides.Progress(now,model.reducedMotion);auto draw=ImGui::GetWindowDrawList();
 draw->PushClipRect(window,{window.x+size.x,window.y+size.y},true);
 auto image=[&](const HeroSlides::Image& image,float offset){if(!image.texture||image.width<=0||image.height<=0)return;float target=size.x/size.y,ratio=image.width/image.height;ImVec2 uv0{0,0},uv1{1,1};if(ratio>target){float f=target/ratio;uv0.x=(1-f)*.5f;uv1.x=1-uv0.x;}else{float f=ratio/target;uv0.y=(1-f)*.5f;uv1.y=1-uv0.y;}float x0=std::max(window.x,window.x+offset),x1=std::min(window.x+size.x,window.x+offset+size.x);if(x1<=x0)return;float u0=uv0.x,du=uv1.x-uv0.x;uv0.x=u0+du*(x0-window.x-offset)/size.x;uv1.x=u0+du*(x1-window.x-offset)/size.x;ImDrawFlags corners=ImDrawFlags_RoundCornersNone;if(x0<=window.x)corners|=ImDrawFlags_RoundCornersLeft;if(x1>=window.x+size.x)corners|=ImDrawFlags_RoundCornersRight;if(corners!=ImDrawFlags_RoundCornersNone)corners&=~ImDrawFlags_RoundCornersNone;draw->AddImageRounded((ImTextureID)(intptr_t)image.texture.Get(),{x0,window.y},{x1,window.y+size.y},uv0,uv1,IM_COL32(255,255,255,105),12*dpi,corners);};
 image(slides.outgoing,-size.x*progress);image(slides.current,size.x*(1-progress));auto tint=ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);tint.w=model.light?.90f:.84f;draw->AddRectFilled(window,{window.x+size.x,window.y+size.y},ImGui::GetColorU32(tint),12*dpi);draw->PopClipRect();
}
static const char* Proxies[]={Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll"),Neurotic::UiLiteral("desktop.option.d508058f7eba", "winmm.dll"),Neurotic::UiLiteral("desktop.option.e4c456927fa4", "version.dll"),Neurotic::UiLiteral("desktop.option.dcc94e2ae3ad", "dbghelp.dll"),Neurotic::UiLiteral("desktop.option.cbf80229b83a", "d3d12.dll"),Neurotic::UiLiteral("desktop.option.e3dbb87412e9", "wininet.dll"),Neurotic::UiLiteral("desktop.option.bf5d99b5c9ae", "winhttp.dll"),Neurotic::UiLiteral("desktop.option.fd4503a9892c", "OptiScaler.asi"),Neurotic::UiLiteral("desktop.option.5990097c3bc5", "OptiScaler.dll")};
static void ProxyPicker(HubModel& model,float dpi){
 const auto selected=model.SelectedProxyName();
 const Game* game=model.selected>=0&&model.selected<(int)model.games.size()?&model.games[model.selected]:nullptr;
 const auto recommendation=game&&!selected.empty()?ProxyRecommendationLabel(*game,selected):std::string{};
 const auto preview=selected.empty()?Neurotic::Translate(Neurotic::UiLiteral("desktop.hubshell.choose_proxy_filename", "Choose proxy filename")):selected+(recommendation.empty()?"":" · "+recommendation);
 ImGui::SetNextItemWidth(std::max(1.f,std::min(410*dpi,ImGui::GetContentRegionAvail().x*.74f)));
 ImGui::BeginDisabled(model.installer.busy);
 if(ImGui::BeginCombo(Neurotic::UiLiteral("desktop.hubshell.file_proxy_name_4ce337e7", "File Proxy Name"),preview.c_str())){
  for(int i=0;i<9;++i){auto hint=game?ProxyRecommendationLabel(*game,Proxies[i]):std::string{};auto label=std::string(Proxies[i])+(hint.empty()?"":" · "+hint);const bool active=selected==Proxies[i];if(ImGui::Selectable(label.c_str(),active))model.ChooseProxy(i);if(active)ImGui::SetItemDefaultFocus();}
  ImGui::EndCombo();
 }
 ImGui::EndDisabled();
 if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.the_dll_name_used_to_load_neurotic_recommendatio_082acd23", "The DLL name used to load NeuRotic. Recommendations use this game's known graphics APIs; choose the one matching your game settings."));
}
static std::filesystem::path PickExecutable(const std::string& folder={}){
 wchar_t path[32768]{};auto initial=Wide(folder);OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=GetActiveWindow();auto filter=nh::Wide(Neurotic::UiMessage("desktop.hubshell.game_executable_exe_1dd28368", "Game executable (*.exe)"));filter.push_back(0);filter+=L"*.exe";filter.push_back(0);filter.push_back(0);picker.lpstrFilter=filter.c_str();picker.lpstrFile=path;picker.nMaxFile=32768;auto title=nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_select_the_actual_game_executable_6edd9505", "NeuRotic — select the actual game executable"));picker.lpstrTitle=title.c_str();picker.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
 return GetOpenFileNameW(&picker)?std::filesystem::path(path):std::filesystem::path();
}
static std::filesystem::path PickDirectory(std::wstring title=nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_search_for_games_in_this_directory_2b2b012a", "NeuRotic — search for games in this directory"))){
 IFileOpenDialog* picker=nullptr;std::filesystem::path path;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))){DWORD options=0;picker->GetOptions(&options);picker->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT);picker->SetTitle(title.c_str());if(SUCCEEDED(picker->Show(GetActiveWindow()))){IShellItem* item=nullptr;if(SUCCEEDED(picker->GetResult(&item))){PWSTR raw=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&raw))){path=raw;CoTaskMemFree(raw);}item->Release();}}picker->Release();}return path;
}
static void GameIcon(HubModel& model,const Game& game,ImVec2 pos,float size){
 auto image=model.artwork?model.artwork->Get(game,"icon",model.onlineArtwork):ArtworkView{};auto draw=ImGui::GetWindowDrawList();if(image)draw->AddImageRounded((ImTextureID)(intptr_t)image.texture,pos,{pos.x+size,pos.y+size},{0,0},{1,1},IM_COL32_WHITE,7);else{draw->AddRectFilled(pos,{pos.x+size,pos.y+size},ImGui::GetColorU32(ImGuiCol_FrameBg),7);ui::Glyph(draw,1,{pos.x+size*.2f,pos.y+size*.2f},size*.6f,ImGui::GetColorU32(ImGuiCol_TextDisabled));}
}
static void PortraitCover(HubModel& model,const Game& game,const ArtworkView& cover,float dpi,ImVec2 pos,ImVec2 size){
 auto draw=ImGui::GetWindowDrawList();
 draw->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},ImGui::GetColorU32(ImGuiCol_FrameBg),8*dpi);
 if(cover){const float scale=std::min(size.x/cover.width,size.y/cover.height);draw->AddImage((ImTextureID)(intptr_t)cover.texture,pos,{pos.x+cover.width*scale,pos.y+cover.height*scale});}
 else{GameIcon(model,game,{pos.x+(size.x-48*dpi)*.5f,pos.y+size.y*.35f},48*dpi);auto label=Neurotic::UiLiteral("desktop.hubshell.no_cover_d9b9ad62", "No cover");auto text=ImGui::CalcTextSize(label);draw->AddText({pos.x+(size.x-text.x)*.5f,pos.y+size.y*.7f},ImGui::GetColorU32(ImGuiCol_TextDisabled),label);}
}
static bool AccentAction(const char* label,ImVec4 color,float width,float dpi){
 return ui::ActionButton(label,color,{width,42*dpi});
}
static bool OperationAction(HubModel& model,const char* operation,const char* label,ImVec4 color,float width,float dpi){
 bool pressed=AccentAction(label,color,width,dpi);
 if(model.installer.busy&&model.installer.action==operation){auto p=ImGui::GetItemRectMin();ui::Glyph(ImGui::GetWindowDrawList(),6,{p.x+10*dpi,p.y+11*dpi},20*dpi,ImGui::GetColorU32(ImGuiCol_Text),true);}
 return pressed;
}
static void OperationProgress(const ProcessJob& job,ImVec2 pos,ImVec2 size,float dpi){
 auto draw=ImGui::GetWindowDrawList();float width=std::max(1.f,size.x-24*dpi);ImVec2 at{pos.x+12*dpi,pos.y+12*dpi};
 draw->AddRectFilled(at,{at.x+width,at.y+74*dpi},ImGui::GetColorU32(ImGuiCol_PopupBg),6*dpi);
 auto label=job.progressStage.empty()?Neurotic::UiMessage("desktop.native.b93900bded31", "Working..."):job.progressStage;
 ImGui::RenderTextEllipsis(draw,{at.x+10*dpi,at.y+8*dpi},{at.x+width-10*dpi,at.y+30*dpi},at.x+width-10*dpi,label.c_str(),nullptr,nullptr);
 ImVec2 bar{at.x+10*dpi,at.y+36*dpi};float barWidth=std::max(1.f,width-20*dpi);
 draw->AddRectFilled(bar,{bar.x+barWidth,bar.y+8*dpi},ImGui::GetColorU32(ImGuiCol_FrameBg),4*dpi);
 float begin=job.progress<0?float(std::fmod(ImGui::GetTime()*.45,1.0))*.7f:0,amount=job.progress<0?.3f:std::clamp(job.progress,0.f,1.f);
 draw->AddRectFilled({bar.x+begin*barWidth,bar.y},{bar.x+(begin+amount)*barWidth,bar.y+8*dpi},ImGui::GetColorU32(ImGuiCol_PlotHistogram),4*dpi);
 auto detail=Neurotic::Localization::FormatSharedText("desktop.native.19701ba31b62", {{"progress",job.progress<0?Neurotic::UiMessage("desktop.native.a92f0449a9f7", "Working"):std::to_string(int(job.progress*100))+"%"},{"seconds",std::to_string((GetTickCount64()-job.started)/1000)}});
 draw->AddText({at.x+10*dpi,at.y+50*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),detail.c_str());
}
static bool ShowReFrameworkNote(const Game& game){
 auto name=CatalogKey(Utf8(std::filesystem::path(Wide(game.target.path)).filename().wstring()));
 const char* executables[]={Neurotic::UiLiteral("desktop.option.1c30ec89bf8d", "kunitsugami.exe"),Neurotic::UiLiteral("desktop.option.27f54bcb7e01", "kunitsugamidemo.exe"),Neurotic::UiLiteral("desktop.option.ca10e9187800", "monsterhunterwilds.exe"),Neurotic::UiLiteral("desktop.option.3800fb767853", "monsterhunterrise.exe"),Neurotic::UiLiteral("desktop.option.db30d5bb5ccb", "drdr.exe"),Neurotic::UiLiteral("desktop.option.03e58339d385", "dd2ccs.exe"),Neurotic::UiLiteral("desktop.option.74d1e3f72c4a", "dd2.exe"),Neurotic::UiLiteral("desktop.option.1438c72b0739", "pragmata_sketchbook.exe"),Neurotic::UiLiteral("desktop.option.39b8bbb8716f", "re9.exe"),Neurotic::UiLiteral("desktop.option.d5f29df82807", "re9demo.exe"),Neurotic::UiLiteral("desktop.option.6826ee64cb43", "monster_hunter_stories_3_twisted_reflection.exe"),Neurotic::UiLiteral("desktop.option.33b9476fa956", "pragmata.exe"),Neurotic::UiLiteral("desktop.option.1dd88ff68fb3", "onimushawots_demo.exe"),Neurotic::UiLiteral("desktop.option.71ddafc33c83", "re2.exe"),Neurotic::UiLiteral("desktop.option.5756918e1857", "re3.exe"),Neurotic::UiLiteral("desktop.option.07e8cb1b2264", "re4.exe"),Neurotic::UiLiteral("desktop.option.16f6f3004935", "re7.exe"),Neurotic::UiLiteral("desktop.option.f4f870e18081", "re8.exe"),Neurotic::UiLiteral("desktop.option.820e155efb18", "devilmaycry5.exe"),Neurotic::UiLiteral("desktop.option.8beff081f9b9", "streetfighter6.exe")};
 if(std::any_of(std::begin(executables),std::end(executables),[&](auto value){return name==value;}))return true;
 auto title=CatalogKey(game.title);return title.starts_with(Neurotic::UiLiteral("desktop.hubshell.resident_evil_85a968d9", "resident evil"))||title.starts_with(Neurotic::UiLiteral("desktop.hubshell.monster_hunter_70a51e55", "monster hunter"))||title.starts_with("dragon's dogma")||title.starts_with(Neurotic::UiLiteral("desktop.hubshell.devil_may_cry_e2c16921", "devil may cry"))||title.starts_with(Neurotic::UiLiteral("desktop.hubshell.street_fighter_e2862d5b", "street fighter"))||title.starts_with("kunitsu-gami")||title.starts_with("pragmata")||title.starts_with(Neurotic::UiLiteral("desktop.hubshell.dead_rising_14490e6e", "dead rising"));
}
static void StartGameButton(HubModel& model,float width,float dpi){
 const bool steam=model.selected>=0&&model.selected<(int)model.games.size()&&model.games[model.selected].store=="Steam";
 ImGui::BeginDisabled(!model.CanStartGame());if(AccentAction(steam?Neurotic::UiLiteral("desktop.hubshell.play_in_steam_424384f1", "Play in Steam"):Neurotic::UiLiteral("desktop.hubshell.start_game_db59c44a", "Start game"),{.04f,.39f,.32f,1},width,dpi))model.StartGame();ImGui::EndDisabled();
 if(steam&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.steam_chooses_the_executable_the_selected_file_i_1c59b94c", "Steam chooses the executable. The selected file is the installation target."));
 if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.steam_games_open_through_steam_other_games_use_t_1e2330c3", "Steam games open through Steam. Other games use the selected executable and its folder."));
}
static void ActivityOverlay(HubModel& model,float dpi){
 const bool scanning=model.discovery.busy,checking=model.installer.busy&&model.page!=1,diagnostics=model.gameDiagnosticsBusy||model.DiagnosticExportRunning();
 if(!scanning&&!checking&&!diagnostics)return;
 auto viewport=ImGui::GetMainViewport();float width=std::min(430*dpi,viewport->Size.x-32*dpi);
 ImGui::SetNextWindowPos({viewport->Pos.x+viewport->Size.x-width-16*dpi,viewport->Pos.y+viewport->Size.y-146*dpi});
 ImGui::SetNextWindowSize({width,68*dpi});
 ImGui::Begin("ActivityOverlay",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|FixedPane);
 // Page focus returns after decisions close. Preserve access to Cancel scan
 // without stealing focus or moving activity above an open decision/popup.
 if(!model.showIssue&&!model.showReview&&!model.showUninstallConfirm&&!model.showDataMaintenance&&
    !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
 auto pos=ImGui::GetCursorScreenPos();ui::Glyph(ImGui::GetWindowDrawList(),6,{pos.x,pos.y+8*dpi},20*dpi,ImGui::GetColorU32(ImGuiCol_Text),true);
 const char* label=scanning?Neurotic::UiLiteral("desktop.hubshell.scanning_libraries_ec3ae7b8", "Scanning libraries"):checking?(model.installer.readOnlyInstaller?Neurotic::UiLiteral("desktop.hubshell.checking_game_98751133", "Checking game"):Neurotic::UiLiteral("desktop.hubshell.applying_changes_c4add805", "Applying changes")):Neurotic::UiLiteral("desktop.hubshell.collecting_logs_330cd850", "Collecting logs");
 float cancel=scanning?ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.cancel_scan_3bb39c6b", "Cancel scan")).x+ImGui::GetStyle().FramePadding.x*2:0;
 float right=pos.x+ImGui::GetContentRegionAvail().x-(scanning?cancel+8*dpi:0);
 ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{pos.x+28*dpi,pos.y+8*dpi},{right,pos.y+36*dpi},right,label,nullptr,nullptr);
 if(scanning){ImGui::SetCursorScreenPos({right+8*dpi,pos.y+4*dpi});if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.cancel_scan_3bb39c6b", "Cancel scan"),{.06f,.32f,.68f,1}))model.CancelScan();}
 ImGui::End();
}
static void RequiredOutline(bool required,float dpi,float frameWidth=0){if(!required||!ImGui::IsItemVisible())return;float pulse=ui::reduced?1.f:.75f+.25f*std::sin(float(ImGui::GetTime())*4.f);auto color=ImVec4(1.f,.10f,.18f,pulse);auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();if(frameWidth>0)max.x=min.x+frameWidth;auto draw=ImGui::GetWindowDrawList();auto clip=draw->GetClipRectMin(),end=draw->GetClipRectMax();float inset=2*dpi;ImVec2 a{std::max(min.x+inset,clip.x+inset),std::max(min.y+inset,clip.y+inset)},b{std::min(max.x-inset,end.x-inset),std::min(max.y-inset,end.y-inset)};if(b.x>a.x&&b.y>a.y)draw->AddRect(a,b,ImGui::GetColorU32(color),4*dpi,0,2.5f*dpi);}
static void ProgressBar(float fraction,float dpi){ImGui::ProgressBar(fraction<0?-float(ImGui::GetTime()):fraction,{ImGui::GetContentRegionAvail().x,8*dpi},"");}
static void Navigation(HubModel& model,float dpi,float width){
 const char* pages[]={Neurotic::UiLiteral("desktop.option.3a78695388b3", "Home"),Neurotic::UiLiteral("desktop.hubshell.installation_library_da34cc6a", "Installation Library"),Neurotic::UiLiteral("desktop.anythingview.nr_anything_331a981c", "NR Anything"),Neurotic::UiLiteral("desktop.option.a150ce221602", "Components"),Neurotic::UiLiteral("desktop.option.74a883a037bc", "Settings"),Neurotic::UiLiteral("desktop.hubshell.diagnostics_1be508a4", "Diagnostics")};
 // Wrap at the measured caption width rather than reducing the navigation font.
 ImGui::PushFont(nullptr,18);
 std::array<float,6> widths{};std::array<ImVec2,6> offsets{};
 const float countdownWidth=AnythingCountdownWidth(dpi);
 const float firstRowWidth=std::max(1.f,width-countdownWidth-12*dpi);
 float x=0,y=0;int rows=1;
 for(int i=0;i<6;++i){
  widths[i]=ImGui::CalcTextSize(pages[i]).x+40*dpi;
  const float gap=x>0?8*dpi:0;
  if(x>0&&x+gap+widths[i]>(rows==1?firstRowWidth:width)){x=0;y+=42*dpi;++rows;}
  else x+=gap;
  offsets[i]={x,y};x+=widths[i];
 }
 ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});ImGui::BeginChild("Navigation",{width,rows*42*dpi},ImGuiChildFlags_None,FixedPane);ImGui::PopStyleColor();
 const auto origin=ImGui::GetCursorScreenPos();
 for(int i=0;i<6;++i){
  ImGui::SetCursorScreenPos({origin.x+offsets[i].x,origin.y+offsets[i].y});
  ImGui::PushID(i);const bool selected=model.page==i;const float tabWidth=widths[i];auto p=ImGui::GetCursorScreenPos();
  if(ImGui::InvisibleButton("##MainTab",{tabWidth,38*dpi}))model.page=i;
  auto draw=ImGui::GetWindowDrawList();const bool hovered=ImGui::IsItemHovered();
  if(offsets[i].x>0)draw->AddLine({p.x-4*dpi,p.y+10*dpi},{p.x-4*dpi,p.y+28*dpi},ImGui::GetColorU32(ImGuiCol_Border),dpi);
  if(hovered)draw->AddRectFilled(p,{p.x+tabWidth,p.y+38*dpi},ImGui::GetColorU32(ImGuiCol_FrameBg),5*dpi);
  auto color=ImGui::GetColorU32(selected||hovered?ImGuiCol_Text:ImGuiCol_TextDisabled);ui::NavigationGlyph(draw,i,{p.x+8*dpi,p.y+10*dpi},18*dpi,color);
  draw->AddText({p.x+32*dpi,p.y+(38*dpi-ImGui::GetFontSize())*.5f},color,pages[i]);
  if(selected)draw->AddLine({p.x+8*dpi,p.y+37*dpi},{p.x+tabWidth-8*dpi,p.y+37*dpi},ImGui::GetColorU32(model.light?ImVec4(.10f,.35f,.64f,1):ImVec4(.39f,.71f,1,1)),2*dpi);
  if(ImGui::IsItemFocused())draw->AddRect(p,{p.x+tabWidth,p.y+38*dpi},ImGui::GetColorU32(ImGuiCol_NavCursor),4*dpi);
  ImGui::PopID();
 }
 ImGui::SetCursorScreenPos({origin.x+width-countdownWidth,origin.y+(38*dpi-ImGui::GetFrameHeight())*.5f});
 RenderAnythingCountdown(model,dpi);
 ImGui::EndChild();ImGui::PopFont();
}
static void ScanActions(HubModel& model,float dpi,const std::array<ImVec2,5>& positions,const std::array<float,5>& widths){
 ImGui::SetCursorScreenPos(positions[2]);
 ImGui::BeginDisabled(model.discovery.busy);
 if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.add_game_9dca998e", "+ Add game"))){auto path=PickExecutable();if(!path.empty())model.Add(path);}
 ImGui::SetCursorScreenPos(positions[3]);
 if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "    Add directory"),7,ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "Add directory"),dpi),dpi)){auto path=PickDirectory();if(!path.empty())model.AddDirectory(path);}
 ImGui::SetCursorScreenPos(positions[4]);
 ImGui::PushStyleColor(ImGuiCol_Text,{1,1,1,1});ImGui::PushStyleColor(ImGuiCol_Button,{.06f,.32f,.68f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.10f,.39f,.77f,1});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.04f,.24f,.54f,1});
 const float scanWidth=widths[4];
 if(ui::IconButton(model.discovery.busy?Neurotic::UiLiteral("desktop.hubshell.scanning_35a6c735", "    Scanning..."):Neurotic::UiLiteral("desktop.hubshell.scan_c58dc649", "    Scan"),6,scanWidth,dpi,model.discovery.busy))model.Scan();
 if(model.discovery.busy){auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();float width=hi.x-lo.x,inset=3*dpi;float start=model.discovery.progress>=0?0.f:float(std::fmod(ImGui::GetTime()*.5,1.0))*.7f,amount=model.discovery.progress>=0?model.discovery.progress:.3f;ImGui::GetWindowDrawList()->AddRectFilled({lo.x+inset+start*(width-2*inset),hi.y-4*dpi},{lo.x+inset+(start+amount)*(width-2*inset),hi.y-2*dpi},IM_COL32(160,220,255,255),dpi);if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.s_llu_seconds_0d568964", "%s · %llu seconds"),Neurotic::Translate(model.discovery.progressStage.empty()?Neurotic::UiLiteral("desktop.hubshell.discovering_games_4303f3d9", "Discovering games"):model.discovery.progressStage.c_str()).c_str(),(GetTickCount64()-model.discovery.started)/1000);}
 ImGui::PopStyleColor(4);ImGui::EndDisabled();
}
static bool FavoriteStar(bool favorite,float dpi){
 auto pos=ImGui::GetCursorScreenPos();bool pressed=ImGui::InvisibleButton("##Favorite",{28*dpi,52*dpi});ImVec2 points[10];for(int i=0;i<10;i++){float angle=-1.5707963f+i*.6283185f,r=(i%2?4.5f:10.f)*dpi;points[i]={pos.x+14*dpi+std::cos(angle)*r,pos.y+26*dpi+std::sin(angle)*r};}auto draw=ImGui::GetWindowDrawList();auto color=favorite?IM_COL32(168,103,0,255):ImGui::GetColorU32(ImGuiCol_TextDisabled);if(favorite)draw->AddConcavePolyFilled(points,10,color);else draw->AddPolyline(points,10,color,ImDrawFlags_Closed,1.5f*dpi);if(ImGui::IsItemHovered())ImGui::SetTooltip(favorite?Neurotic::UiLiteral("desktop.hubshell.remove_from_favorites_4bdb1498", "Remove from favorites"):Neurotic::UiLiteral("desktop.hubshell.add_to_favorites_58600dc7", "Add to favorites"));if(ImGui::IsItemFocused())draw->AddRect(pos,{pos.x+28*dpi,pos.y+52*dpi},ImGui::GetColorU32(ImGuiCol_NavCursor),4*dpi,0,1.5f*dpi);return pressed;
}static void LibraryHeader(HubModel& model,float dpi,bool collapsed){
 const bool shortViewport=ImGui::GetMainViewport()->Size.y<520*dpi;
 const auto& style=ImGui::GetStyle();const float gap=style.ItemSpacing.x,rowHeight=ImGui::GetFrameHeight(),rowPitch=rowHeight+style.ItemSpacing.y;
 const float available=std::max(1.f,ImGui::GetContentRegionAvail().x-24*dpi-2*style.ChildBorderSize);
 const char* orders[]={Neurotic::UiLiteral("desktop.hubshell.all_d76f15bf", "All"),Neurotic::UiLiteral("desktop.hubshell.favorites_first_de2d3c03", "Favorites first"),Neurotic::UiLiteral("desktop.hubshell.hidden_82b2ce85", "Hidden")};
 float filterWidth=160*dpi;for(auto label:orders)filterWidth=std::max(filterWidth,ImGui::CalcTextSize(label).x+rowHeight+2*style.FramePadding.x);
 std::array<float,5> widths{filterWidth,220*dpi,
  ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.add_game_9dca998e", "+ Add game")).x+2*style.FramePadding.x,
  ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "Add directory"),dpi),
  std::max(ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.scan_c58dc649", "Scan"),dpi),ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.scanning_35a6c735", "Scanning..."),dpi))};
 std::array<ImVec2,5> positions{};int rows=1;
 const float actions=widths[2]+widths[3]+widths[4]+2*gap;
 if(widths[0]+gap+widths[1]+gap+actions<=available){
  widths[1]=available-widths[0]-actions-2*gap;
  float x=0;for(int i=0;i<5;++i){positions[i]={x,0};x+=widths[i]+gap;}
 }else{
  // Keep filter/search together where possible, then wrap the action group.
  widths[0]=std::min(widths[0],available);
  if(widths[0]+gap+widths[1]<=available){positions[1]={widths[0]+gap,0};widths[1]=available-positions[1].x;}
  else{positions[1]={0,rowPitch};widths[1]=available;++rows;}
  float x=0,y=rows*rowPitch;++rows;
  for(int i=2;i<5;++i){if(x>0&&x+widths[i]>available){x=0;y+=rowPitch;++rows;}positions[i]={x,y};x+=widths[i]+gap;}
 }
 const float height=16*dpi+rows*rowHeight+(rows-1)*style.ItemSpacing.y+2*style.ChildBorderSize;
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,8*dpi});ImGui::PushStyleColor(ImGuiCol_Border,model.light?ImVec4(.60f,.68f,.78f,.75f):ImVec4(.48f,.59f,.72f,.48f));
 ImGui::BeginChild("LibraryHeader",{0,height},ImGuiChildFlags_Borders,FixedPane);ImGui::PopStyleColor();ImGui::PopStyleVar();
 const auto origin=ImGui::GetCursorScreenPos();for(auto& p:positions){p.x+=origin.x;p.y+=origin.y;}
 ImGui::SetCursorScreenPos(positions[0]);ImGui::SetNextItemWidth(widths[0]);int order=int(model.libraryFilter);
 if(ImGui::Combo("##LibraryOrder",&order,orders,3))model.SetLibraryFilter(LibraryFilter(order));
 ImGui::SetCursorScreenPos(positions[1]);ImGui::SetNextItemWidth(widths[1]);ImGui::InputTextWithHint("##Search",Neurotic::UiLiteral("desktop.hubshell.search_games_9536b444", "Search games..."),model.search.data(),model.search.size());
 const auto searchMin=ImGui::GetItemRectMin(),searchMax=ImGui::GetItemRectMax();
 ScanActions(model,dpi,positions,widths);ImGui::EndChild();
 if(collapsed&&model.search[0]){
  auto matches=model.OrderedGames(model.search.data());ImGui::SetNextWindowPos({searchMin.x,searchMax.y});ImGui::SetNextWindowSize({searchMax.x-searchMin.x,std::min(260*dpi,(std::min(size_t(7),std::max(size_t(1),matches.size()))*32+16)*dpi)});
  ImGui::Begin("##LibrarySearchResults",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::BeginDisabled(model.installer.busy&&!model.installer.readOnlyInstaller);
  if(matches.empty())ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.no_matching_games_a3e79e08", "No matching games"));
  for(int index:matches){ImGui::PushID(index);if(ImGui::Selectable(model.games[index].title.c_str(),model.selected==index,0,{0,28*dpi})){model.Select(index);model.search[0]=0;}ImGui::PopID();}
  ImGui::EndDisabled();ImGui::End();
 }if(!shortViewport)ImGui::Spacing();
}
static void ComponentCard(HubModel& model,int component,float width,float dpi,bool compact){
 const char* titles[]={Neurotic::UiLiteral("desktop.hubshell.microsoft_visual_c_9cd58c37", "Microsoft Visual C++"),Neurotic::UiLiteral("desktop.hubshell.streamline_8743a2fe", "Streamline"),Neurotic::UiLiteral("desktop.hubshell.nr_model_3814a794", "NR Model")};
 auto state=component==0?ui::RuntimeState(model):ui::ComponentState(model,component==1?Neurotic::UiLiteral("desktop.hubshell.streamline_3a9087b1", "streamline"):Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel"));
 ImGui::PushID(component);if(compact)ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{8*dpi,8*dpi});ImGui::BeginChild("ComponentCard",{width,(compact?74.f:114.f)*dpi},ImGuiChildFlags_Borders,FixedPane);if(compact)ImGui::PopStyleVar();
 ImGui::PushFont(nullptr,compact?13:16);ui::StatusDot(state,model.light,dpi);ImGui::TextUnformatted(titles[component]);
 const char* status=component==0?(state==ui::ReadyState::Ready?"Ready":state==ui::ReadyState::Checking?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "Checking..."):Neurotic::UiLiteral("desktop.hubshell.install_or_repair_needed_c7ac96f0", "Install or repair needed")):ui::FileStatus(state);
 ImGui::TextColored(ui::StatusColor(state,model.light),"%s",Neurotic::Translate(status).c_str());ImGui::PopFont();
 ImGui::SetCursorPosY((compact?40.f:76.f)*dpi);ImGui::PushFont(nullptr,compact?13:16);
 ImGui::BeginDisabled(component==0&&!model.CanDownloadRuntime());
 if(ui::IconButton(component==0?Neurotic::UiLiteral("desktop.hubshell.download_visual_c_a56ac7e0", "    Download Visual C++"):Neurotic::UiLiteral("desktop.hubshell.open_folder_c24fe9c0", "    Open Folder"),7,component==0?0.f:ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.open_folder_c24fe9c0", "Open Folder"),dpi),dpi)){
  if(component==0)model.OpenRuntimeDownload();else model.OpenComponentFolder(component==1);
 }
 ImGui::EndDisabled();
 if(component>0&&state!=ui::ReadyState::Ready&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",Neurotic::Translate(component==1?Neurotic::UiLiteral("desktop.hubshell.add_streamline_files_here_for_neurotic_to_instal_416d8777", "Add Streamline files here for NeuRotic to install them to your games automatically."):Neurotic::UiLiteral("desktop.hubshell.add_nvngx_dlssnr_dll_file_here_for_neurotic_to_i_6811393c", "Add nvngx_dlssnr.dll file here for NeuRotic to install it to your games automatically.")).c_str());
 ImGui::PopFont();
 ImGui::EndChild();ImGui::PopID();
}
static void Dashboard(HubModel& model,float dpi){
 const bool compact=ImGui::GetContentRegionAvail().y<350*dpi;
 if(compact){ImGui::PushFont(nullptr,13);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,4*dpi});}
 const char* introduction=Neurotic::UiLiteral("desktop.hubshell.to_install_neurotic_to_games_go_to_the_installat_2b9d93e3", "To install NeuRotic to games, go to the Installation Library tab. To apply neural rendering to any window, use the NR Anything button in the upper-right corner.");
 const float refreshSide=32*dpi,introWidth=ImGui::GetContentRegionAvail().x,textWidth=std::max(1.f,introWidth-refreshSide-16*dpi);
 const float introHeight=std::max(refreshSide,ImGui::CalcTextSize(introduction,nullptr,false,textWidth).y);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("HomeIntroduction",{0,introHeight},ImGuiChildFlags_None,FixedPane|ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();
 auto introStart=ImGui::GetCursorScreenPos();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+textWidth);ImGui::TextUnformatted(introduction);ImGui::PopTextWrapPos();
 ImGui::SetCursorScreenPos({introStart.x+introWidth-refreshSide,introStart.y});if(RefreshButton("##RefreshHomeComponents",refreshSide,dpi,model.readiness.busy))model.RefreshPreflight();
 if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.refresh_microsoft_visual_c_streamline_and_nr_mod_f05a2d94", "Refresh Microsoft Visual C++, Streamline and NR model availability"));ImGui::EndChild();
 float width=ImGui::GetContentRegionAvail().x,card=(width-ImGui::GetStyle().ItemSpacing.x*2)/3;
 for(int i=0;i<3;++i){if(i)ImGui::SameLine();ComponentCard(model,i,card,dpi,compact);}
 auto directories=model.SearchDirectories();
 ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.search_directories_3b5b04cd", "SEARCH DIRECTORIES"));
 if(compact){ImGui::SameLine();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.zu_19537331", "(%zu)"),directories.size());if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.folders_where_neurotic_looks_for_games_10c5b8a4", "Folders where NeuRotic looks for games."));ImGui::SameLine();}
 else{ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.folders_where_neurotic_looks_for_games_10c5b8a4", "Folders where NeuRotic looks for games."));ImGui::Spacing();}
 ImGui::BeginDisabled(model.discovery.busy);
 if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "    Add directory"),7,156*dpi,dpi)){auto path=PickDirectory();if(!path.empty())model.AddDirectory(path);}
 ImGui::SameLine();ImGui::BeginDisabled(model.selectedDirectory<0||model.selectedDirectory>=(int)directories.size());
 if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.remove_selected_11d38fde", "Remove selected"),{.65f,.16f,.21f,1}))model.RemoveDirectory(model.selectedDirectory);
 ImGui::EndDisabled();ImGui::EndDisabled();ImGui::Spacing();
 if(!compact)ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.zu_directories_zu_games_in_installation_library_3dd02a0e", "%zu directories · %zu games in Installation Library"),directories.size(),model.games.size());
 ImGui::BeginChild("DirectoryManager",{0,std::max(1.f,ImGui::GetContentRegionAvail().y)},ImGuiChildFlags_Borders);
 if(directories.empty())ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.add_a_directory_to_search_it_or_run_scan_in_inst_df50da4f", "Add a directory to search it, or run Scan in Installation Library to discover installed games and their launcher folders."));
 else if(ImGui::BeginTable("Directories",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerH,{0,0})){
  ImGui::TableSetupColumn(Neurotic::UiLiteral("desktop.provider.74ccd4330384", "Folder"),ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn(Neurotic::UiLiteral("desktop.provider.0e570ca6fabe", "Source"),ImGuiTableColumnFlags_WidthFixed,120*dpi);ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
  ImGuiListClipper clipper;clipper.Begin((int)directories.size());while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;i++){ImGui::PushID(directories[i].path.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);if(ImGui::Selectable(directories[i].path.c_str(),model.selectedDirectory==i,ImGuiSelectableFlags_SpanAllColumns))model.selectedDirectory=i;if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Neurotic::Translate(directories[i].path.c_str()).c_str());ImGui::TableSetColumnIndex(1);ImGui::TextDisabled("%s",Neurotic::Translate(directories[i].source.c_str()).c_str());ImGui::PopID();}ImGui::EndTable();
 }ImGui::EndChild();if(compact){ImGui::PopStyleVar();ImGui::PopFont();}
}
static void Description(const char* title,const char* body){ImGui::TextUnformatted(title);ImGui::Spacing();ImGui::TextDisabled("%s",Neurotic::Translate(body).c_str());}
static void ComponentHeading(const char* title,ui::ReadyState state,bool light,float dpi){
 auto pos=ImGui::GetCursorScreenPos();float side=22*dpi;ImGui::Dummy({side,side});auto draw=ImGui::GetWindowDrawList();auto color=ImGui::GetColorU32(ui::StatusColor(state,light));
 draw->AddRect(pos,{pos.x+side,pos.y+side},color,4*dpi,0,1.5f*dpi);
 if(state==ui::ReadyState::Ready){draw->AddLine({pos.x+5*dpi,pos.y+11*dpi},{pos.x+9*dpi,pos.y+15*dpi},color,2*dpi);draw->AddLine({pos.x+9*dpi,pos.y+15*dpi},{pos.x+17*dpi,pos.y+6*dpi},color,2*dpi);}
 else if(state==ui::ReadyState::Error){draw->AddLine({pos.x+11*dpi,pos.y+5*dpi},{pos.x+11*dpi,pos.y+12*dpi},color,2*dpi);draw->AddCircleFilled({pos.x+11*dpi,pos.y+16*dpi},dpi,color);}
 ImGui::SameLine();Neurotic::Sleek::WindowSectionHeader(title);
}
static void PreflightPanel(HubModel& model){
 const float dpi=ImGui::GetIO().FontGlobalScale;
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.component_files_are_checked_here_processing_stat_2d23187e", "Component files are checked here. Processing status is shown while NeuRotic is running."));ImGui::Spacing();
 ImGui::BeginDisabled(model.readiness.busy);if(ui::IconButton(model.readiness.busy?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "    Checking..."):Neurotic::UiLiteral("desktop.hubshell.refresh_components_47b5ec92", "    Refresh components"),8,0,dpi,model.readiness.busy))model.RefreshPreflight();ImGui::EndDisabled();
 if(!model.preflightError.empty()){ImGui::PushStyleColor(ImGuiCol_Text,ui::StatusColor(ui::ReadyState::Attention,model.light));ImGui::TextWrapped("%s",Neurotic::Translate(model.preflightError.c_str()).c_str());ImGui::PopStyleColor();}ImGui::Spacing();
 ImGui::BeginChild("RuntimeChecklist",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollWithMouse);
 auto runtimeState=ui::RuntimeState(model);if(runtimeState==ui::ReadyState::Attention)runtimeState=ui::ReadyState::Error;
 ComponentHeading(Neurotic::UiLiteral("desktop.hubshell.microsoft_visual_c_9cd58c37", "Microsoft Visual C++"),runtimeState,model.light,dpi);
 ImGui::TextColored(ui::StatusColor(runtimeState,model.light),"%s",Neurotic::Translate(runtimeState==ui::ReadyState::Ready?Neurotic::UiLiteral("desktop.hubshell.ready_required_for_installed_games_0d5f7ef7", "Ready · required for installed games"):runtimeState==ui::ReadyState::Checking?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "Checking..."):Neurotic::UiLiteral("desktop.hubshell.missing_or_outdated_required_for_installed_games_48278ab3", "Missing or outdated · required for installed games")).c_str());
 if(model.preflight.contains(Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime"))){auto& runtime=model.preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")];ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.x64_s_or_newer_377f0647", "x64 %s or newer"),Neurotic::Translate(runtime.value(Neurotic::UiLiteral("desktop.hubshell.minimumversion_0e9002ae", "minimumVersion"),Neurotic::UiLiteral("desktop.hubshell.unknown_c67449ba", "Unknown")).c_str()).c_str());if(runtimeState!=ui::ReadyState::Ready){if(runtime.contains(Neurotic::UiLiteral("desktop.hubshell.problems_803a9177", "problems")))for(auto& problem:runtime[Neurotic::UiLiteral("desktop.hubshell.problems_803a9177", "problems")])if(problem.is_string())ImGui::TextWrapped("%s",Neurotic::Translate(problem.get_ref<const std::string&>().c_str()).c_str());if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.install_microsoft_runtime_b837b786", "Install Microsoft runtime")))model.OpenRuntimeDownload();}}
 if(ui::Toggle(Neurotic::UiLiteral("desktop.hubshell.ignore_missing_visual_c_runtime_29b40fc3", "Ignore missing Visual C++ runtime"),&model.continueWithoutRuntime))model.Save();
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.applies_to_all_games_when_off_installation_asks__83651c3f", "Applies to all games. When off, installation asks before continuing without the runtime. A game may fail to load NeuRotic until the runtime is installed."));ImGui::EndChild();ImGui::Spacing();
 ImGui::BeginChild("StreamlineChecklist",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollWithMouse);
 auto streamline=ui::ComponentState(model,Neurotic::UiLiteral("desktop.hubshell.streamline_3a9087b1", "streamline"));ComponentHeading(Neurotic::UiLiteral("desktop.hubshell.streamline_8743a2fe", "Streamline"),streamline,model.light,dpi);ImGui::TextColored(ui::StatusColor(streamline,model.light),"%s",Neurotic::Translate(streamline==ui::ReadyState::Ready?Neurotic::UiLiteral("desktop.hubshell.supplied_matching_files_preserved_across_updates_fe6591d4", "Supplied · matching files preserved across updates"):streamline==ui::ReadyState::Checking?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "Checking..."):Neurotic::UiLiteral("desktop.hubshell.optional_matching_provider_files_not_supplied_6a924065", "Optional · matching provider files not supplied")).c_str());
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.used_by_compatible_frame_generation_paths_keep_m_4f08beb8", "Used by compatible frame-generation paths. Keep matching files together in the persistent component folder."));if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.open_streamline_folder_7efeb999", "    Open Streamline folder"),7,0,dpi))model.OpenComponentFolder(true);ImGui::EndChild();ImGui::Spacing();
 ImGui::BeginChild("ModelChecklist",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollWithMouse);
 auto neural=ui::ComponentState(model,Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel"));if(neural==ui::ReadyState::Attention)neural=ui::ReadyState::Error;ComponentHeading(Neurotic::UiLiteral("desktop.hubshell.nr_model_3814a794", "NR Model"),neural,model.light,dpi);
 ImGui::TextColored(ui::StatusColor(neural,model.light),"%s",Neurotic::Translate(neural==ui::ReadyState::Ready?Neurotic::UiLiteral("desktop.hubshell.supplied_model_validation_passed_65f6caf4", "Supplied · model validation passed"):neural==ui::ReadyState::Checking?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "Checking..."):Neurotic::UiLiteral("desktop.hubshell.required_for_neural_rendering_model_not_supplied_1b0a91a5", "Required for neural rendering · model not supplied")).c_str());if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.open_nr_model_folder_472c391f", "Open NR model folder"),0,0,dpi))model.OpenComponentFolder(false);ImGui::EndChild();
}
static void LogPanel(const std::string& report){
 float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.select_text_or_press_ctrl_a_ctrl_c_3d1f8a71", "Select text, or press Ctrl+A / Ctrl+C"));ImGui::SameLine(std::max(0.f,right));if(ui::CopyButton())ImGui::SetClipboardText(report.c_str());
 // Read-only editing supplies native selection and keyboard copying. Bound display size.
 std::vector<char> buffer(report.begin(),report.begin()+std::min(report.size(),size_t(2*1024*1024)));buffer.push_back(0);
 ImGui::InputTextMultiline("##LogText",buffer.data(),buffer.size(),{ImGui::GetContentRegionAvail().x,std::max(90.f,ImGui::GetContentRegionAvail().y)},ImGuiInputTextFlags_ReadOnly);
}
static void SettingsEditor(HubModel& model){RenderSavedGameSettings(model);}
static void DataMaintenanceModal(HubModel& model,float dpi){
 if(model.showDataMaintenance)ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.app_data_maintenance_41387e8d", "App data maintenance"));
 auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowSize({std::min(580*dpi,viewport->Size.x-32*dpi),0},ImGuiCond_Appearing);
 ImGui::SetNextWindowSizeConstraints({0,0},{viewport->Size.x-32*dpi,viewport->Size.y-32*dpi});
 if(!ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.app_data_maintenance_41387e8d", "App data maintenance"),nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize))return;
 if(!model.showDataMaintenance){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
 const int action=model.dataMaintenanceAction;
 const char* titles[]={Neurotic::UiLiteral("desktop.hubshell.clear_app_data_394adcc5", "Clear App data?"),Neurotic::UiLiteral("desktop.hubshell.clear_game_profiles_84c81f6e", "Clear Game Profiles?"),Neurotic::UiLiteral("desktop.hubshell.open_artwork_cache_ca1265c8", "Open Artwork Cache?")};
 ImGui::TextUnformatted(titles[std::clamp(action,0,2)]);ImGui::Spacing();
 if(action==0)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.clears_the_local_library_including_hidden_games__516f3998", "Clears the local library (including hidden games), preferences, game profiles, artwork, operation receipts and imported Runtime files such as NR models and Streamline. NeuRotic will restart. Installed game files are untouched."));
 else if(action==1)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.clears_saved_game_inspection_and_profile_data_th_b9afdcd0", "Clears saved game inspection and profile data. The library, hidden games and settings inside each game's folder stay unchanged. Fresh details will be read the next time you select a game. No restart is needed."));
 else ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.opens_the_local_artwork_cache_in_file_explorer_t_028caefc", "Opens the local artwork cache in File Explorer. This button does not delete images or require a restart."));
 if(action<2){ImGui::Spacing();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.location_s_dbf519b1", "Location: %s"),Neurotic::Translate(Utf8(model.dataMaintenancePlan.root.wstring()).c_str()).c_str());ImGui::Text(Neurotic::UiLiteral("desktop.hubshell.files_zu_955176be", "Files: %zu"),model.dataMaintenancePlan.fileCount);for(const auto& issue:model.dataMaintenancePlan.issues)ImGui::TextWrapped("%s",Neurotic::Translate(issue.c_str()).c_str());}
 ImGui::Spacing();ImGui::BeginDisabled(action<2&&!model.dataMaintenancePlan.Ready());
 if(ui::ActionButton(action==0?Neurotic::UiLiteral("desktop.hubshell.clear_and_restart_38557994", "Clear and restart"):action==1?Neurotic::UiLiteral("desktop.hubshell.clear_game_profiles_137fdc81", "Clear Game Profiles"):Neurotic::UiLiteral("desktop.hubshell.open_artwork_cache_6b22c5fa", "Open Artwork Cache"),{.72f,.12f,.17f,1})){model.ConfirmDataMaintenance();ImGui::CloseCurrentPopup();}
 ImGui::EndDisabled();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape)){model.CancelDataMaintenance();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
}
static void GameIssueModal(HubModel& model,float dpi){
 static bool acceptReplacement=false;static std::string issueKey;
 if(model.showIssue&&!model.operationIssue.is_null()){auto key=model.operationIssue.dump();if(issueKey!=key||!ImGui::IsPopupOpen(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"))){issueKey=key;acceptReplacement=false;}ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"));}
 bool antiCheat=model.operationIssue.is_object()&&model.operationIssue.value("decisionKind",std::string{})==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk");
 auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowSize({std::min(650*dpi,viewport->Size.x-40*dpi),std::min((antiCheat?330.f:430.f)*dpi,viewport->Size.y-40*dpi)},ImGuiCond_Appearing);
 if(!ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"),&model.showIssue,ImGuiWindowFlags_NoResize)){if(!model.showIssue&&!model.showReview){model.installer.ClearAntiCheatApproval();if(model.fileReview.Active())model.CancelFileReview();}return;}
 auto issue=model.operationIssue;auto kind=issue.value("decisionKind","");
 if(kind=="FileConflict"){
  if(!model.fileReview.Active()){model.CancelFileReview();ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.s_appears_to_not_have_come_from_neurotic_how_sho_8c90c02a", "%s appears to not have come from NeuRotic. How should we proceed?"),Neurotic::Translate(model.fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).get<std::string>().c_str()).c_str());
  ImGui::Spacing();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.file_zu_of_zu_dea4e31b", "File %zu of %zu"),model.fileReview.Index()+1,model.fileReview.Count());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.replace_writes_the_neurotic_file_skip_leaves_thi_e430ea8d", "Replace writes the NeuRotic file. Skip leaves this file alone and continues the other files; required skips leave a partial installation. Cancel stops before copying."));
  ImGui::Spacing();ImGui::BeginDisabled(model.installer.busy);
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace"));ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.skip_9e5fb0c9", "Skip")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.skip_9e5fb0c9", "Skip"));ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape))model.CancelFileReview();
  if(model.fileReview.Active()&&model.fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))==Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll")&&issue.value(Neurotic::UiLiteral("desktop.hubshell.cankeepreshade_5ecf0788", "canKeepReShade"),false)){ImGui::Spacing();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.keep_reshade_aa1a1138", "Keep ReShade")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.keepreshade_b63abfd0", "KeepReShade"));if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.rename_your_dxgi_dll_to_reshade64_dll_and_enable_6bd057fa", "Rename your dxgi.dll to ReShade64.dll and enable coexistence. This rename is recorded for Uninstall."));}
  ImGui::EndDisabled();if(!model.showIssue)ImGui::CloseCurrentPopup();ImGui::EndPopup();return;
 }
 const float footerHeight=(antiCheat?1.f:3.f)*ImGui::GetFrameHeightWithSpacing()+2*ImGui::GetStyle().ItemSpacing.y+1;
 ImGui::BeginChild("IssueExplanation",{0,std::max(1.f,ImGui::GetContentRegionAvail().y-footerHeight)});
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){
  auto report=issue.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object());auto headline=report.value("headline",std::string{});bool known=headline=="detected"||headline=="documented";
  ImGui::PushFont(nullptr,24);ImGui::PushStyleColor(ImGuiCol_Text,model.light?ImVec4(.87f,.02f,.06f,1):ImVec4(1.f,.18f,.23f,1));
  ImGui::TextWrapped("%s",Neurotic::Translate(known?Neurotic::UiLiteral("desktop.hubshell.warning_this_game_uses_anti_cheat_installing_neu_57e1d706", "Warning: This game uses anti-cheat. Installing NeuRotic could get you banned."):Neurotic::UiLiteral("desktop.hubshell.warning_this_game_may_use_anti_cheat_installing__d568c3bf", "Warning: This game may use anti-cheat. Installing NeuRotic could get you banned.")).c_str());ImGui::PopStyleColor();ImGui::PopFont();ImGui::Spacing();
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.neurotic_compatibility_and_account_safety_are_no_2956d680", "NeuRotic compatibility and account safety are not confirmed."));
  ImGui::Checkbox(Neurotic::UiLiteral("desktop.hubshell.i_understand_the_risk_and_want_to_continue_0784e561", "I understand the risk and want to continue"),&acceptReplacement);ImGui::Spacing();
  if(report.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")==Neurotic::UiLiteral("desktop.hubshell.cancelled_b89ba77e", "cancelled"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.the_check_was_canceled_refresh_the_game_before_p_cedfa03a", "The check was canceled. Refresh the game before proceeding."));
  if(ImGui::CollapsingHeader(Neurotic::UiLiteral("desktop.hubshell.detection_details_7109c797", "Detection details"))){ImGui::TextWrapped("%s",Neurotic::Translate(ResultMessage(report,Neurotic::UiLiteral("desktop.hubshell.anti_cheat_status_could_not_be_established_f4c62026", "Anti-cheat status could not be established.")).c_str()).c_str());ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.local_scan_s_ca2932b2", "Local scan: %s"),Neurotic::Translate(report.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),Neurotic::UiLiteral("desktop.hubshell.incomplete_ae0c5c0a", "incomplete")).c_str()).c_str());
   if(report.contains("findings")&&report["findings"].is_array())for(auto& finding:report["findings"])ImGui::BulletText("%s (%s)",Neurotic::Translate(finding.value(Neurotic::UiLiteral("desktop.translationeditorwindow.name_ef8e2b72", "name"),finding.value("provider",Neurotic::UiLiteral("desktop.hubshell.anti_cheat_component_331c188b", "Anti-cheat component"))).c_str()).c_str(),Neurotic::Translate(finding.value("state","possible").c_str()).c_str());
   if(report.contains(Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings"))&&report[Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")].is_array()&&!report[Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")].empty())ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.earlier_findings_remain_relevant_because_this_ch_1342c291", "Earlier findings remain relevant because this check did not finish."));
  }
 }else{
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.action_needed_2262b939", "ACTION NEEDED"));ImGui::Spacing();ImGui::TextWrapped("%s",Neurotic::Translate(ResultMessage(issue,Neurotic::UiLiteral("desktop.hubshell.the_operation_could_not_continue_nothing_will_be_3d3f7dc1", "The operation could not continue. Nothing will be retried automatically.")).c_str()).c_str());ImGui::Spacing();
  if(kind==Neurotic::UiLiteral("desktop.hubshell.proxyrequired_b6e34ce5", "ProxyRequired")){
   ProxyPicker(model,dpi);ImGui::Spacing();
   ImGui::BeginDisabled(model.SelectedProxyName().empty()||model.installer.busy);
   if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"))){model.showIssue=false;ImGui::CloseCurrentPopup();model.Plan("Install");}
   ImGui::EndDisabled();
  }
  else ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.close_the_game_and_retry_the_selected_action_aft_5ba7c335", "Close the game and retry the selected action after resolving the listed paths. Missing or edited recorded files do not require Repair."));
  for(const auto* key:{Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors"),Neurotic::UiLiteral("desktop.hubshell.notes_fd9f1432", "notes")})if(issue.contains(key)&&issue[key].is_array())for(const auto& item:issue[key])if(item.is_string()||item.is_object()){
   const auto detail=ResultDetail(item);ImGui::TextWrapped("%s",Neurotic::Translate(detail.c_str()).c_str());
  }
  if(!model.bundleStatus.empty()){ImGui::Spacing();ImGui::TextWrapped("%s",Neurotic::Translate(model.bundleStatus.c_str()).c_str());}
 }
 ImGui::EndChild();ImGui::Separator();
 if(kind!=Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510", "Open game folder")))model.OpenSelectedGameFolder();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.check_again_c396af3d", "Check again"))){model.showIssue=false;ImGui::CloseCurrentPopup();model.RefreshSelected();}}
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){
  bool canProceed=acceptReplacement&&!model.installer.busy&&issue.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object()).value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")!=Neurotic::UiLiteral("desktop.hubshell.cancelled_b89ba77e", "cancelled");
  ImGui::BeginDisabled(!canProceed);if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.proceed_3eb1e2a6", "Proceed"))&&canProceed){model.AcknowledgeRisk();if(!model.showIssue)ImGui::CloseCurrentPopup();}ImGui::EndDisabled();
 }else{
  ImGui::BeginDisabled(model.DiagnosticExportRunning());
  if(ui::Button(model.DiagnosticExportRunning()?Neurotic::UiLiteral("desktop.hubshell.exporting_diagnostic_bundle_da4d29f0", "Exporting diagnostic bundle..."):Neurotic::UiLiteral("desktop.hubshell.export_diagnostic_bundle_d6689dd1", "Export diagnostic bundle"))){auto destination=PickDirectory(nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_choose_diagnostic_zip_destination_d606f5de", "NeuRotic — choose diagnostic ZIP destination")));if(!destination.empty())model.ExportSelectedDiagnostics(destination);}
  ImGui::EndDisabled();
  if(!model.bundlePath.empty()){ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_bundle_folder_78c51e18", "Open bundle folder")))model.OpenBundleFolder();}
 }
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk"))ImGui::SameLine();
 if(ImGui::IsKeyPressed(ImGuiKey_Escape)||ui::Button((kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")||kind==Neurotic::UiLiteral("desktop.hubshell.runtimemissing_8fa29c64", "RuntimeMissing"))?Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"):Neurotic::UiLiteral("desktop.hubshell.close_4bae39a9", "Close"))){model.installer.ClearAntiCheatApproval();model.showIssue=false;ImGui::CloseCurrentPopup();}ImGui::SetItemDefaultFocus();ImGui::EndPopup();
}
void RenderHub(HubModel& model,void* brand,float dpi){
 bool nextLight=model.light;bool changeTheme=false;
 ui::reduced=model.reducedMotion;
 // Logical font sizes are scaled once by the Windows DPI factor in FontGlobalScale.
 ImGui::PushFont(nullptr,18);
 auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
 const bool shortViewport=viewport->Size.y<520*dpi;
 if(shortViewport)ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{8*dpi,8*dpi});
 ImGui::Begin("NeuRotic",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|FixedPane);if(shortViewport)ImGui::PopStyleVar();
 ImGui::SetScrollY(0);
 const auto headerStart=ImGui::GetCursorPos();const float headerWidth=ImGui::GetContentRegionAvail().x,controlHeight=42*dpi;
 ImGui::PushFont(ui::ActionFont(),18);const float anythingWidth=std::max(212*dpi,ImGui::CalcTextSize(Neurotic::Translate("NR Anything").c_str()).x+78*dpi);ImGui::PopFont();
 const float controlGap=12*dpi,controlsWidth=controlHeight+controlGap*2+dpi+anythingWidth;
 auto logoPos=ImGui::GetCursorScreenPos();if(ImGui::InvisibleButton("##ProjectHomepage",{226*dpi,58*dpi}))ShellExecuteW(nullptr,L"open",ProjectPage,nullptr,nullptr,SW_SHOWNORMAL);
 if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.open_the_neurotic_github_homepage_44b61456", "Open the NeuRotic GitHub homepage"));
 auto headerDraw=ImGui::GetWindowDrawList();if(brand){float scale=226*dpi/1805.f,h=318*scale,split=277*scale;auto id=(ImTextureID)(intptr_t)brand;
  headerDraw->AddImage(id,logoPos,{logoPos.x+split,logoPos.y+h},{176.f/2155,198.f/730},{453.f/2155,516.f/730});headerDraw->AddImage(id,{logoPos.x+split,logoPos.y},{logoPos.x+226*dpi,logoPos.y+h},{453.f/2155,198.f/730},{1981.f/2155,516.f/730},ImGui::GetColorU32(ImGuiCol_Text));
 }else headerDraw->AddText(logoPos,ImGui::GetColorU32(ImGuiCol_Text),"NeuRotic");
 ImGui::PushFont(nullptr,13);headerDraw->AddText({logoPos.x+226*dpi-ImGui::CalcTextSize("Alpha 0.9.7").x,logoPos.y+42*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),"Alpha 0.9.7");ImGui::PopFont();
 ImGui::SameLine(0,12*dpi);auto separator=ImGui::GetCursorScreenPos();ImGui::Dummy({1*dpi,58*dpi});headerDraw->AddLine({separator.x,separator.y+5*dpi},{separator.x,separator.y+53*dpi},ImGui::GetColorU32(ImGuiCol_Border),dpi);ImGui::SameLine(0,12*dpi);
 auto update=GetUpdateStatus();auto color=update.state==UpdateState::Current?ui::StatusColor(ui::ReadyState::Ready,model.light):update.state==UpdateState::Available?ui::StatusColor(ui::ReadyState::Attention,model.light):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
 ImGui::PushFont(nullptr,14);
 const char* updateLabel=Neurotic::UiLiteral("desktop.hubshell.update_1d1ce149", "Update");
 const char* notesLabel=Neurotic::UiLiteral("desktop.hubshell.patch_notes_b3fdcb1c", "Patch notes");
 const float updateWidth=ImGui::CalcTextSize(updateLabel).x+ImGui::GetStyle().FramePadding.x*2;
 const float notesWidth=ImGui::CalcTextSize(notesLabel).x+ImGui::GetStyle().FramePadding.x*2;
 const float actionsWidth=updateWidth+ImGui::GetStyle().ItemSpacing.x+notesWidth;
 const float desiredReleaseWidth=std::max(230*dpi,actionsWidth);
 const bool releaseBelow=headerWidth-controlsWidth-275*dpi<desiredReleaseWidth;
 const float releaseWidth=releaseBelow?headerWidth:desiredReleaseWidth;
 const float statusHeight=ImGui::GetTextLineHeight()*2;
 const float releaseHeight=releaseBelow?std::max(statusHeight,ImGui::GetFrameHeight()):std::max(58*dpi,statusHeight+ImGui::GetStyle().ItemSpacing.y+ImGui::GetFrameHeight());
 const float topHeight=releaseBelow?58*dpi:releaseHeight;
 ImGui::PopFont();
 if(releaseBelow)ImGui::SetCursorPos({headerStart.x,headerStart.y+topHeight+ImGui::GetStyle().ItemSpacing.y});
 ImGui::BeginChild(Neurotic::UiLiteral("desktop.hubshell.releaseinfo_83e3bae1", "ReleaseInfo"),{releaseWidth,releaseHeight},ImGuiChildFlags_None,FixedPane);ImGui::PushFont(nullptr,14);
 const auto releaseStart=ImGui::GetCursorPos();
 const float statusWidth=releaseBelow?std::max(1.f,ImGui::GetContentRegionAvail().x-actionsWidth-12*dpi):ImGui::GetContentRegionAvail().x;
 ImGui::PushTextWrapPos(releaseStart.x+statusWidth);ImGui::TextColored(color,"%s",Neurotic::Translate(update.detail.c_str()).c_str());ImGui::PopTextWrapPos();
 if(ImGui::IsItemHovered())ImGui::SetTooltip("%s%s%s",Neurotic::Translate(update.detail.c_str()).c_str(),Neurotic::Translate(update.tag.empty()?"":" · ").c_str(),Neurotic::Translate(update.tag.c_str()).c_str());
 ImGui::SetCursorPos(releaseBelow?ImVec2{releaseStart.x+releaseWidth-actionsWidth,releaseStart.y}:ImVec2{releaseStart.x,releaseStart.y+statusHeight+ImGui::GetStyle().ItemSpacing.y});
 if(update.state==UpdateState::Available){if(ui::Button(updateLabel,{updateWidth,0}))ShellExecuteW(nullptr,L"open",UpdatePage,nullptr,nullptr,SW_SHOWNORMAL);}else ImGui::Dummy({updateWidth,ImGui::GetFrameHeight()});
 ImGui::SameLine();if(ui::Button(notesLabel,{notesWidth,0}))ShellExecuteW(nullptr,L"open",PatchNotesPage,nullptr,nullptr,SW_SHOWNORMAL);ImGui::PopFont();ImGui::EndChild();
 const float controlsX=headerStart.x+std::max(0.f,headerWidth-controlsWidth);
 const float controlsY=headerStart.y+(58*dpi-controlHeight)*.5f;
 ImGui::SetCursorPos({controlsX,controlsY});
 if(ui::ThemeButton(nextLight,controlHeight))changeTheme=true;
 const auto themeMax=ImGui::GetItemRectMax();const float dividerX=themeMax.x+controlGap;
 headerDraw->AddLine({dividerX,themeMax.y-controlHeight+4*dpi},{dividerX,themeMax.y-4*dpi},ImGui::GetColorU32(ImGuiCol_Border),dpi);
 ImGui::SetCursorPos({controlsX+controlHeight+controlGap*2+dpi,controlsY});RenderAnythingAction(model,dpi,anythingWidth,controlHeight);
 ImGui::SetCursorPos({headerStart.x,headerStart.y+topHeight+(releaseBelow?ImGui::GetStyle().ItemSpacing.y+releaseHeight:0)});ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
 Navigation(model,dpi,ImGui::GetContentRegionAvail().x);if(!shortViewport)ImGui::Spacing();
 float bodyHeight=std::max(100*dpi,ImGui::GetContentRegionAvail().y-72*dpi);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{(shortViewport?8.f:12.f)*dpi,(shortViewport?8.f:12.f)*dpi});ImGui::BeginChild("PageContent",{0,bodyHeight},ImGuiChildFlags_Borders,FixedPane);ImGui::PopStyleVar();ImGui::SetScrollY(0);
 auto pagePos=ImGui::GetWindowPos(),pageSize=ImGui::GetWindowSize();
 if(model.lastNavigationPage!=model.page){if(model.lastNavigationPage==1)model.LeaveLibrarySelection();model.lastNavigationPage=model.page;model.pageShownAt=ImGui::GetTime();}
 float reveal=model.reducedMotion?1.f:std::clamp((float)(ImGui::GetTime()-model.pageShownAt)/.18f,0.f,1.f);ImGui::Dummy({0,(1-reveal)*8*dpi});ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*(.85f+.15f*reveal));
 ImVec2 selectedPos{},selectedSize{};bool selectedVisible=false;bool busy=model.installer.busy;ImGui::BeginDisabled(false);
 if(model.page==0)Dashboard(model,dpi);
 else if(model.page==1){
  auto& layoutStorage=ImGui::GetCurrentWindow()->StateStorage;auto collapsedId=ImGui::GetID("##LibraryListCollapsed");bool collapsed=layoutStorage.GetBool(collapsedId,false);
  LibraryHeader(model,dpi,collapsed);
  float left=std::clamp(ImGui::GetContentRegionAvail().x*.30f,235*dpi,335*dpi);bool columns=true;
  float panelHeight=std::max(1.f,ImGui::GetContentRegionAvail().y),listHeight=panelHeight;
  const auto panelsOrigin=ImGui::GetCursorScreenPos();float fraction=1-ui::Animate(ImGui::GetID("##LibrarySlide"),collapsed?1.f:0.f);if(fraction<.0001f)fraction=0;float listWidth=left*fraction;
  if(listWidth>1){ImGui::BeginChild("LibraryColumn",{listWidth,listHeight},ImGuiChildFlags_None,FixedPane);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,12*dpi});ImGui::BeginChild("LibraryList",ImVec2(0,std::max(1.f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders);ImGui::PopStyleVar();
  ImGui::BeginDisabled(busy&&!model.installer.readOnlyInstaller);
  auto visible=model.OrderedGames(model.search.data());
  ImGuiListClipper clipper;clipper.Begin((int)visible.size(),62*dpi);
  while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;row++){
   int i=visible[row];auto& game=model.games[i];ImGui::PushID(game.id.c_str());float rowY=ImGui::GetCursorPosY();auto pos=ImGui::GetCursorScreenPos();float rowWidth=ImGui::GetContentRegionAvail().x;
   auto draw=ImGui::GetWindowDrawList();ImVec2 rowEnd{pos.x+rowWidth,pos.y+52*dpi};
   bool hover=ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(pos,rowEnd);
   if(model.selected==i||hover)draw->AddRectFilled(pos,rowEnd,ImGui::GetColorU32(hover?ImGuiCol_HeaderHovered:ImGuiCol_Header),5*dpi);
   ImGui::SetCursorScreenPos({pos.x+2*dpi,pos.y});if(FavoriteStar(game.favorite,dpi))model.ToggleFavorite(i);
   ImGui::SetCursorScreenPos({pos.x+34*dpi,pos.y});ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
   if(ImGui::Selectable("##Game",model.selected==i,ImGuiSelectableFlags_NoPadWithHalfSpacing,{std::max(1.f,rowWidth-34*dpi),52*dpi}))model.Select(i);ImGui::PopStyleColor(3);
   GameIcon(model,game,{pos.x+38*dpi,pos.y+8*dpi},36*dpi);
   auto start=ImVec2(pos.x+84*dpi,pos.y+7*dpi),end=ImVec2(pos.x+rowWidth-5*dpi,pos.y+26*dpi);ImGui::RenderTextEllipsis(draw,start,end,end.x,game.title.c_str(),nullptr,nullptr);
   auto label=game.store+" · "+(game.target.suitable?"Ready":Neurotic::UiLiteral("desktop.hubshell.choose_executable_dcbba520", "Choose executable"));draw->AddText({start.x,pos.y+29*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),label.c_str());
   ImGui::SetCursorPosY(rowY+62*dpi-ImGui::GetStyle().ItemSpacing.y);ImGui::Dummy({0,0});ImGui::PopID();
  }
  if(visible.empty())ImGui::TextWrapped(model.games.empty()?Neurotic::UiLiteral("desktop.hubshell.scan_to_find_games_or_add_a_game_or_directory_70a0c411", "Scan to find games, or add a game or directory."):model.libraryFilter==LibraryFilter::Hidden&&!model.search[0]?Neurotic::UiLiteral("desktop.hubshell.no_hidden_games_c0ea29b0", "No hidden games."):Neurotic::UiLiteral("desktop.hubshell.no_games_match_this_search_6decd1b3", "No games match this search."));
  ImGui::EndDisabled();ImGui::EndChild();ImGui::EndChild();}
  ImGui::SetCursorScreenPos({panelsOrigin.x+(left+ImGui::GetStyle().ItemSpacing.x)*fraction,panelsOrigin.y});
  auto barPos=ImGui::GetCursorScreenPos();if(ui::Button("##LibraryDrawer",{16*dpi,panelHeight})){layoutStorage.SetBool(collapsedId,!collapsed);}
  auto barDraw=ImGui::GetWindowDrawList();float cy=barPos.y+panelHeight*.5f,cx=barPos.x+8*dpi,direction=collapsed?1.f:-1.f;
  barDraw->AddLine({cx-direction*2*dpi,cy-5*dpi},{cx+direction*2*dpi,cy},ImGui::GetColorU32(ImGuiCol_TextDisabled),1.5f*dpi);barDraw->AddLine({cx+direction*2*dpi,cy},{cx-direction*2*dpi,cy+5*dpi},ImGui::GetColorU32(ImGuiCol_TextDisabled),1.5f*dpi);
  if(ImGui::IsItemHovered())ImGui::SetTooltip(collapsed?Neurotic::UiLiteral("desktop.hubshell.show_game_list_6de1f3bd", "Show game list"):Neurotic::UiLiteral("desktop.hubshell.hide_game_list_05220953", "Hide game list"));ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,12*dpi});ImGui::BeginChild("GameDetails",ImVec2(0,columns?panelHeight:std::max(1.f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders);ImGui::PopStyleVar();
  auto selectedBounds=ImGui::GetCurrentWindow()->OuterRectClipped;selectedPos=selectedBounds.Min;selectedSize=selectedBounds.GetSize();selectedVisible=selectedSize.x>0&&selectedSize.y>0;
  if(model.selected<0||model.selected>=(int)model.games.size()){
   ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.select_a_game_to_install_neurotic_or_remove_its__fba2d7de", "Select a game to install NeuRotic or remove its recorded files."));ImGui::Spacing();
   ImGui::Separator();ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.1_select_your_game_91582b72", "1  Select your game"),Neurotic::UiLiteral("desktop.hubshell.choose_its_executable_unsupported_in_game_archit_b867da9b", "Choose its executable. Unsupported in-game architectures can use NR Anything."));ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.2_install_neurotic_5b335339", "2  Install NeuRotic"),Neurotic::UiLiteral("desktop.hubshell.choose_install_and_resolve_unfamiliar_files_or_a_72b3921e", "Choose Install and resolve unfamiliar files or anti-cheat warnings."));ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.3_start_your_game_2fdf1e62", "3  Start your game"),Neurotic::UiLiteral("desktop.hubshell.launch_normally_or_use_start_game_db4cbc13", "Launch normally or use Start game."));
  }else{
   auto& game=model.games[model.selected];ImGui::PushID(game.id.c_str());ImGui::BeginDisabled(busy);
   const bool preparedX86=game.target.bitness==32;
   bool installed=!model.inspection.is_null()&&model.inspection["state"].is_object()&&(model.inspection["state"].value("status","")=="Installed"||model.inspection["state"].value("status","")=="Partial");
   bool antiCheat=model.inspection.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))&&model.inspection[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].value("acknowledgementRequired",false);
   const auto coverPos=ImGui::GetCursorScreenPos();
   const auto cover=model.artwork?model.artwork->Get(game,"cover",model.onlineArtwork):ArtworkView{};
   auto* details=ImGui::GetCurrentWindow();
   const float coverInset=details->WindowPadding.y;
   const auto choiceLabel=Neurotic::UiLiteral("desktop.hubshell.executable_installation_923395f4", "Executable / installation");
   // Reserve scrollbar space consistently: only pane size and UI scale affect
   // the artwork, never the selected game's controls or scrolling content.
   const float paneWidth=std::max(1.f,details->Size.x-2*details->WindowPadding.x-2*details->WindowBorderSize-ImGui::GetStyle().ScrollbarSize);
   // Leave room for the padded divider and tab strip in short app windows.
   const float tabReserve=coverInset+ImGui::GetFrameHeight()+2*ImGui::GetStyle().ItemSpacing.y;
   const float paneHeight=std::max(1.f,details->Size.y-2*coverInset-2*details->WindowBorderSize-tabReserve);
   const float controlsWidth=std::max(270*dpi,80*dpi+ImGui::CalcTextSize(choiceLabel).x+ImGui::GetStyle().ItemInnerSpacing.x);
   const float artFit=std::min({1.f,paneHeight/(420*dpi),std::max(1.f,paneWidth-controlsWidth-ImGui::GetStyle().ItemSpacing.x)/(280*dpi)});
   const ImVec2 coverSize{280*dpi*artFit,420*dpi*artFit};
   ImGui::SetCursorScreenPos({coverPos.x+coverSize.x+ImGui::GetStyle().ItemSpacing.x,coverPos.y});
   ImGui::BeginChild("SelectedGameHeader",{0,coverSize.y},ImGuiChildFlags_None);
   const bool fallbackInstalled=!model.dumbfireInstalledTarget.empty()&&model.dumbfireInstalledTarget==game.target.path;
   auto reminder=model.nrModelReminder.Evaluate(model.preflight,model.readiness.busy,std::filesystem::path(Wide(game.target.path)),installed||fallbackInstalled,GetTickCount64(),model.inspection);
   ImGui::PushFont(nullptr,22);auto titlePos=ImGui::GetCursorScreenPos();float titleRight=titlePos.x+ImGui::GetContentRegionAvail().x;float titleWidth=std::max(1.f,titleRight-titlePos.x-40*dpi-(reminder.Visible()?24*dpi:0));ImGui::Dummy({titleWidth,ImGui::GetTextLineHeight()});ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),titlePos,{titlePos.x+titleWidth,titlePos.y+ImGui::GetTextLineHeight()},titlePos.x+titleWidth,game.title.c_str(),nullptr,nullptr);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Neurotic::Translate(game.title.c_str()).c_str());
   if(reminder.Visible()){
    auto center=ImVec2(titlePos.x+std::min(titleWidth,ImGui::CalcTextSize(game.title.c_str()).x)+12*dpi,titlePos.y+ImGui::GetTextLineHeight()*.5f);
    float pulse=model.reducedMotion?1.f:.28f+.72f*(.5f+.5f*std::sin(float(ImGui::GetTime())*5.02655f));
    ImGui::GetWindowDrawList()->AddCircleFilled(center,7*dpi,ImGui::GetColorU32(ImVec4(1,.04f,.07f,.20f*pulse)));
    ImGui::GetWindowDrawList()->AddCircleFilled(center,4.5f*dpi,ImGui::GetColorU32(ImVec4(1,.04f,.07f,pulse)));
    if(ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect({center.x-10*dpi,center.y-12*dpi},{center.x+10*dpi,center.y+12*dpi})){ImGui::BeginTooltip();ImGui::PushTextWrapPos(410*dpi);ImGui::TextUnformatted(reminder.reason.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
   }ImGui::PopFont();
   ImGui::SameLine();ImGui::SetCursorScreenPos({titleRight-32*dpi,titlePos.y});
   if(ui::Button("##GameVisibility",{32*dpi,28*dpi}))model.ToggleHidden(model.selected);
   auto eye=ImGui::GetItemRectMin();auto eyeDraw=ImGui::GetWindowDrawList();auto eyeColor=ImGui::GetColorU32(ImGuiCol_Text);
   ImVec2 eyeCenter{eye.x+16*dpi,eye.y+14*dpi};
   eyeDraw->AddBezierCubic({eyeCenter.x-10*dpi,eyeCenter.y},{eyeCenter.x-4*dpi,eyeCenter.y-9*dpi},{eyeCenter.x+4*dpi,eyeCenter.y-9*dpi},{eyeCenter.x+10*dpi,eyeCenter.y},eyeColor,1.5f*dpi);
   eyeDraw->AddBezierCubic({eyeCenter.x-10*dpi,eyeCenter.y},{eyeCenter.x-4*dpi,eyeCenter.y+9*dpi},{eyeCenter.x+4*dpi,eyeCenter.y+9*dpi},{eyeCenter.x+10*dpi,eyeCenter.y},eyeColor,1.5f*dpi);
   eyeDraw->AddCircle(eyeCenter,3*dpi,eyeColor,0,1.5f*dpi);if(game.hidden)eyeDraw->AddLine({eyeCenter.x-10*dpi,eyeCenter.y+9*dpi},{eyeCenter.x+10*dpi,eyeCenter.y-9*dpi},eyeColor,2*dpi);
   if(ImGui::IsItemHovered())ImGui::SetTooltip(game.hidden?Neurotic::UiLiteral("desktop.hubshell.show_game_moves_out_of_hidden_when_you_leave_thi_1154cf96", "Show game — moves out of Hidden when you leave this selection"):Neurotic::UiLiteral("desktop.hubshell.hide_game_stays_selected_until_you_leave_then_ap_3d46c9f9", "Hide game — stays selected until you leave, then appears only in Hidden"));
   ImGui::PushFont(nullptr,14);ImGui::TextDisabled("%s ·",game.store.c_str());ImGui::SameLine();
   ui::GameInstallationStatus(installed?model.inspection["state"].value("status",std::string{}):std::string{},model.inspectionCached,model.light);
   if(antiCheat){
    ImGui::TextColored(ui::StatusColor(ui::ReadyState::Attention,model.light),"%s",Neurotic::Translate(Neurotic::UiLiteral("desktop.installation.anti_cheat_acknowledgment","Anti-cheat: acknowledgment needed")).c_str());
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Neurotic::Translate(model.inspection.at("antiCheat").value("reason",Neurotic::UiLiteral("desktop.hubshell.compatibility_is_not_confirmed_7c6d8700", "Compatibility is not confirmed.")).c_str()).c_str());
   }
   ImGui::PopFont();ImGui::Spacing();
   if(preparedX86)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.manuallibrary.a_compatible_in_game_integration_is_unavailable__87dd6414", "A compatible in-game integration is unavailable for this architecture. Use NR Anything."));
   if(ShowReFrameworkNote(game)){ImGui::PushFont(nullptr,14);ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.some_capcom_games_may_require_reframework_to_wor_0c1bbc45", "Some Capcom games may require REFramework to work correctly."));ImGui::PopFont();}
   StartGameButton(model,std::max(1.f,std::min(220*dpi,ImGui::GetContentRegionAvail().x-42*dpi-ImGui::GetStyle().ItemSpacing.x)),dpi);ImGui::SameLine();if(RefreshButton("##RefreshSelectedGame",42*dpi,dpi,model.installer.busy))model.RefreshSelected();if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.check_current_files_and_game_status_44577022", "Check current files and game status"));

   const float shortcutWidth=std::max(ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510", "Open game folder"),dpi),ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.browse_for_executable_34c75d65", "Browse for executable")).x+ImGui::GetStyle().FramePadding.x*2);
   const float shortcutsAvailable=ImGui::GetContentRegionAvail().x;
   const auto screenshotsLabel=Neurotic::UiLiteral("desktop.hubshell.open_screenshots_6b9d871c", "Open Screenshots");
   if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510", "    Open game folder"),7,shortcutWidth,dpi))model.OpenSelectedGameFolder();
   if(shortcutWidth+ImGui::GetStyle().ItemSpacing.x+ImGui::CalcTextSize(screenshotsLabel).x+ImGui::GetStyle().FramePadding.x*2<=shortcutsAvailable)ImGui::SameLine();
   if(ui::Button(screenshotsLabel))model.OpenSelectedGameScreenshots();
   ImGui::BeginDisabled(!model.CanOpenSelectedIni());if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_ini_file_b892f0d6", "Open INI File"),{shortcutWidth,0}))model.OpenSelectedIni();ImGui::EndDisabled();
   if(ui::Button(game.target.suitable?Neurotic::UiLiteral("desktop.hubshell.change_executable_8d6c615c", "Change executable"):Neurotic::UiLiteral("desktop.hubshell.browse_for_executable_34c75d65", "Browse for executable"),{shortcutWidth,0})){auto path=PickExecutable(game.root);if(!path.empty())model.SetExecutable(model.selected,path);}RequiredOutline(!game.target.suitable,dpi);
   if(!game.target.path.empty()){ImGui::SameLine();auto name=Utf8(std::filesystem::path(Wide(game.target.path)).filename().wstring());auto pos=ImGui::GetCursorScreenPos();float width=std::max(1.f,ImGui::GetContentRegionAvail().x);ImGui::Dummy({width,ImGui::GetFrameHeight()});ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{pos.x,pos.y+ImGui::GetStyle().FramePadding.y},{pos.x+width,pos.y+ImGui::GetFrameHeight()},pos.x+width,name.c_str(),nullptr,nullptr);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",Neurotic::Translate(game.target.path.c_str()).c_str(),Neurotic::Translate(game.target.reason.c_str()).c_str());}
   if(!game.executableChoices.empty()){
    const float choicesAvailable=ImGui::GetContentRegionAvail().x;
    const bool stackedChoice=choicesAvailable<80*dpi+ImGui::CalcTextSize(choiceLabel).x+ImGui::GetStyle().ItemInnerSpacing.x;
    const auto choiceId=ImGui::GetID(choiceLabel);
    const float choiceWidth=std::max(80*dpi,stackedChoice?choicesAvailable:std::min(ImGui::CalcItemWidth(),choicesAvailable-ImGui::CalcTextSize(choiceLabel).x-ImGui::GetStyle().ItemInnerSpacing.x));
    const auto preview=game.target.suitable?Utf8(std::filesystem::path(Wide(game.target.path)).filename().wstring())+" · "+std::to_string(game.target.bitness)+"-bit":Neurotic::UiLiteral("desktop.hubshell.choose_executable_dcbba520", "Choose executable");std::string choicePath;
    ImGui::BeginDisabled(model.installer.busy||model.BulkUninstallBusy());
    if(stackedChoice){ImGui::TextWrapped("%s",choiceLabel);ImGui::PushOverrideID(choiceId);}
    ImGui::SetNextItemWidth(choiceWidth);
    if(ImGui::BeginCombo(stackedChoice?"":choiceLabel,preview.c_str())){for(auto& choice:game.executableChoices){const bool active=GamePathKey(choice.path)==GamePathKey(game.target.path);auto label=choice.store+" · "+std::to_string(choice.bitness)+Neurotic::UiLiteral("desktop.hubshell.bit_786bcde1", "-bit · ")+choice.path;if(ImGui::Selectable(label.c_str(),active))choicePath=choice.path;if(active)ImGui::SetItemDefaultFocus();if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.s_installation_s_6d194f69", "%s\nInstallation: %s"),Neurotic::Translate(choice.path.c_str()).c_str(),Neurotic::Translate(choice.root.c_str()).c_str());}ImGui::EndCombo();}
    if(stackedChoice)ImGui::PopID();
    ImGui::EndDisabled();RequiredOutline(!game.target.suitable,dpi,choiceWidth);if(!choicePath.empty())model.SetExecutable(model.selected,Wide(choicePath));
   }
   if(!game.target.suitable)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.required_choose_the_game_executable_to_unlock_it_4281569b", "Required: choose the game executable to unlock its controls."));
   if(game.target.path.empty()&&!game.target.reason.empty())ImGui::TextWrapped("%s",Neurotic::Translate(game.target.reason.c_str()).c_str());
   if(model.inspectionCached)ImGui::TextWrapped("%s",Neurotic::Translate(model.inspectionRefreshing?Neurotic::UiLiteral("desktop.hubshell.checking_current_files_previous_status_shown_e6aa2abb", "Checking current files; previous status shown."):Neurotic::UiLiteral("desktop.hubshell.previous_status_shown_refresh_to_check_current_f_411cfcd2", "Previous status shown. Refresh to check current files.")).c_str());
   if(!model.operationIssue.is_null()){ImGui::TextColored(ui::StatusColor(ui::ReadyState::Attention,model.light),Neurotic::UiLiteral("desktop.hubshell.needs_attention_b2215ec3", "Needs attention"));ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.resolve_1161843d", "Resolve")))model.showIssue=true;}
   ImGui::EndChild();
   PortraitCover(model,game,cover,dpi,coverPos,coverSize);
   ImGui::SetCursorScreenPos({coverPos.x,coverPos.y+coverSize.y+coverInset});
   ImGui::Separator();
   ImGui::BeginDisabled(!game.target.suitable);
   if(ImGui::BeginTabBar("SelectedGameTabs")){
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.installation_5a51813b", "Installation"))){
     Neurotic::Sleek::WindowSectionHeader(Neurotic::UiLiteral("desktop.hubshell.installation_5a51813b", "Installation"));ImGui::Spacing();
     ProxyPicker(model,dpi);ImGui::Spacing();
     ImGui::BeginDisabled(preparedX86||model.installer.busy);
     ui::Toggle(Neurotic::UiLiteral("desktop.hubshell.fresh_install_5e42e4f8", "Fresh Install"),&model.freshInstall);
     if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.use_package_default_settings_install_normally_ke_110beff6", "Use package default settings. Install normally keeps your current INI. Character Inspector versions are reused in either mode."));
     const float actionWidth=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
     if(OperationAction(model,Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"),Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"),{.02f,.38f,.46f,1},actionWidth,dpi))model.Plan(Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"));ImGui::EndDisabled();
     ImGui::SameLine();if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"),{.65f,.16f,.21f,1},{actionWidth,42*dpi}))model.Plan(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"));
     ImGui::Spacing();
     ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.install_copies_the_current_package_unfamiliar_fi_29534045", "Install copies the current package. Unfamiliar files are handled individually. Uninstall removes recorded files, including edited files, and reverses an explicitly recorded ReShade rename when dxgi.dll is available."));
     ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.character_inspector_is_included_private_screensh_36ba5522", "Character Inspector is included. Private screenshots and unrelated files stay in place."));
     ImGui::EndTabItem();
    }
    ImGui::BeginDisabled(model.inspection.is_null());
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.ini_settings_f791a0c5", "INI Settings"))){RenderSavedGameCompatibility(model);ImGui::EndTabItem();}
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.game_settings_76603cce", "Game Settings"))){SettingsEditor(model);ImGui::EndTabItem();}
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.diagnostics_1be508a4", "Diagnostics"))){ImGui::Spacing();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.refresh_details_0ab28849", "Refresh details")))model.RefreshSelected();ImGui::Spacing();ImGui::BeginDisabled(!model.CanCollectGameDiagnostics()||model.gameDiagnosticsBusy);if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.quick_log_preview_0640cf60", "Quick log preview")))model.CollectGameDiagnostics();ImGui::EndDisabled();ImGui::Spacing();ImGui::BeginDisabled(model.DiagnosticExportRunning());if(ui::Button(model.DiagnosticExportRunning()?Neurotic::UiLiteral("desktop.hubshell.exporting_diagnostic_bundle_da4d29f0", "Exporting diagnostic bundle..."):Neurotic::UiLiteral("desktop.hubshell.export_complete_diagnostic_bundle_7eed7a46", "Export complete diagnostic bundle"))){auto destination=PickDirectory(nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_choose_diagnostic_zip_destination_d606f5de", "NeuRotic — choose diagnostic ZIP destination")));if(!destination.empty())model.ExportSelectedDiagnostics(destination);}ImGui::EndDisabled();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.collect_logs_configuration_module_inventory_and__d05e23f4", "Collect logs, configuration, module inventory and capability evidence into a ZIP for review."));if(!model.bundleStatus.empty())ImGui::TextWrapped("%s",Neurotic::Translate(model.bundleStatus.c_str()).c_str());if(!model.bundlePath.empty()){if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_bundle_folder_78c51e18", "Open bundle folder")))model.OpenBundleFolder();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.copy_zip_path_1ad418dc", "Copy ZIP path")))ImGui::SetClipboardText(model.bundlePath.c_str());}if(!installed)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.the_quick_log_preview_requires_recorded_game_log_19f75b32", "The quick log preview requires recorded game logs."));if(!model.gameDiagnosticsStatus.empty())ImGui::TextWrapped("%s",Neurotic::Translate(model.gameDiagnosticsStatus.c_str()).c_str());if(!model.gameDiagnosticsText.empty())LogPanel(model.gameDiagnosticsText);ImGui::EndTabItem();}
    ImGui::EndDisabled();ImGui::EndTabBar();
   }
   ImGui::EndDisabled();
   ImGui::EndDisabled();ImGui::PopID();
  }
  ImGui::EndChild();
  if(busy&&selectedVisible){
   auto resume=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(selectedPos);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("SelectedGameLoadingShade",selectedSize,ImGuiChildFlags_None,FixedPane|ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();
   auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(selectedPos,{selectedPos.x+selectedSize.x,selectedPos.y+selectedSize.y},IM_COL32(105,110,120,165),8*dpi);
   ImVec2 box{std::min(300*dpi,selectedSize.x-16*dpi),std::min(100*dpi,selectedSize.y-8*dpi)},at{selectedPos.x+(selectedSize.x-box.x)*.5f,selectedPos.y+(selectedSize.y-box.y)*.5f};
   draw->AddRectFilled(at,{at.x+box.x,at.y+box.y},ImGui::GetColorU32(ImGuiCol_PopupBg),8*dpi);auto text=ImGui::CalcTextSize("Loading");float groupWidth=text.x+34*dpi,groupX=at.x+(box.x-groupWidth)*.5f;
   ui::Glyph(draw,6,{groupX,at.y+(box.y-22*dpi)*.5f},22*dpi,ImGui::GetColorU32(ImGuiCol_Text),true);draw->AddText({groupX+34*dpi,at.y+(box.y-text.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),"Loading");
   if(!model.installer.readOnlyInstaller)OperationProgress(model.installer,selectedPos,selectedSize,dpi);
   ImGui::InvisibleButton("##BlockSelectedGame",selectedSize);ImGui::EndChild();ImGui::SetCursorScreenPos(resume);ImGui::Dummy({0,0});
  }
 }else{
  bool pageBody=model.page!=2;if(pageBody)ImGui::BeginChild("PageBody",{0,0});
  if(model.page==2)RenderAnything(model,dpi);
  if(model.page==3)PreflightPanel(model);
  if(model.page==4){if(ImGui::BeginTabBar("##AppSettingsTabs")){if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.installdefaults.general","General"))){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.game_recommendations_are_starting_points_not_a_g_219a2194", "Game recommendations are starting points, not a guarantee. Compatibility can vary by game version and graphics mode. Check the game rules before using modifications, especially online."));ImGui::Spacing();RenderLanguageManager();ImGui::Spacing();if(ui::Toggle(Neurotic::UiLiteral("desktop.hubshell.reduced_motion_ee127977", "Reduced motion"),&model.reducedMotion))model.Save();if(ui::Toggle(Neurotic::UiLiteral("desktop.hubshell.download_steam_artwork_2cb82a8c", "Download Steam artwork"),&model.onlineArtwork)){if(model.artwork)model.artwork->SetOnlineEnabled(model.onlineArtwork);model.Save();}ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.cached_images_and_local_game_icons_remain_availa_18535270", "Cached images and local game icons remain available offline."));ImGui::Spacing();ImGui::Separator();ImGui::Spacing();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.search_directories_3b5b04cd", "SEARCH DIRECTORIES"));if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.manage_directories_on_home_3ecc5aff", "Manage directories on Home")))model.page=0;ImGui::Spacing();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.game_settings_are_available_in_installation_libr_84e9d64d", "Game settings are available in Installation Library. Your library and operation receipts are saved locally:"));ImGui::TextWrapped("%s",Neurotic::Translate(Utf8(UserRoot().wstring()).c_str()).c_str());ImGui::Spacing();
   if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.open_appdata_folder_4060bae1", "Open AppData Folder"),0,0,dpi)){
    const auto path=UserRoot();std::error_code error;std::filesystem::create_directories(path,error);
    if(error||reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)model.message=Neurotic::UiMessage("desktop.hubshell.the_appdata_folder_could_not_be_opened_8b3d9e30", "The AppData folder could not be opened.");
   }
   ImGui::Spacing();ImGui::BeginDisabled(model.installer.busy||model.discovery.busy||model.readiness.busy||model.gameDiagnosticsBusy||model.DiagnosticExportRunning()||model.BulkUninstallBusy());
   const char* maintenanceLabels[]={Neurotic::UiLiteral("desktop.hubshell.clear_app_data_folder_02b18dcc", "Clear App data folder"),Neurotic::UiLiteral("desktop.hubshell.clear_game_profiles_folder_7a9f9470", "Clear Game Profiles folder"),Neurotic::UiLiteral("desktop.hubshell.open_artwork_cache_6b22c5fa", "Open Artwork Cache")};
   for(int action=0;action<3;++action)if(ui::ActionButton(maintenanceLabels[action],{.72f,.12f,.17f,1}))model.RequestDataMaintenance(action);
   ImGui::EndDisabled();ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.installdefaults.tab","Install Defaults"))){RenderInstallDefaults(model);ImGui::EndTabItem();}
   ImGui::EndTabBar();}
  }
  if(model.page==5){ImGui::Spacing();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_local_logs_afdf316a", "Open local logs"))){
    auto path=UserRoot()/L"requests";std::error_code error;std::filesystem::create_directories(path,error);if(!error)ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);else model.message=Neurotic::UiMessage("desktop.hubshell.local_logs_could_not_be_opened_22e6deae", "Local logs could not be opened.");
   }ImGui::Spacing();if(!model.lastResult.is_null())LogPanel(model.lastResult.dump(2));else ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.no_operation_has_been_attempted_in_this_session_7c439d17", "No operation has been attempted in this session."));}
  if(pageBody)ImGui::EndChild();
 }
 ImGui::EndDisabled();ImGui::PopStyleVar();ImGui::EndChild();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
 float supportWidth=ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.enjoying_neurotic_send_ko_fi_c8a05712", "Enjoying NeuRotic? Send Ko-Fi")).x+ImGui::GetStyle().FramePadding.x*2;float footerWidth=std::max(100.f,ImGui::GetContentRegionAvail().x-supportWidth-20*dpi);ImGui::BeginChild(Neurotic::UiLiteral("desktop.hubshell.footerstatus_363908dc", "FooterStatus"),{footerWidth,32*dpi},0,FixedPane);auto statusPos=ImGui::GetCursorScreenPos();float statusRight=statusPos.x+ImGui::GetContentRegionAvail().x;ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),statusPos,{statusRight,statusPos.y+ImGui::GetTextLineHeight()},statusRight,model.message.c_str(),nullptr,nullptr);if(ImGui::IsWindowHovered()&&!model.message.empty()){ImGui::BeginTooltip();ImGui::PushTextWrapPos(480*dpi);ImGui::TextUnformatted(model.message.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}ImGui::EndChild();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.enjoying_neurotic_send_ko_fi_c8a05712", "Enjoying NeuRotic? Send Ko-Fi")))ShellExecuteW(nullptr,L"open",L"https://ko-fi.com/espiownage",nullptr,nullptr,SW_SHOWNORMAL);
 ActivityOverlay(model,dpi);DataMaintenanceModal(model,dpi);GameIssueModal(model,dpi);
 if(model.showUninstallConfirm)ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.uninstall_neurotic_70533052", "Uninstall NeuRotic?"));
 ImGui::SetNextWindowSize({std::min(610*dpi,viewport->Size.x-32*dpi),0},ImGuiCond_Appearing);
 ImGui::SetNextWindowSizeConstraints({0,0},{viewport->Size.x-32*dpi,viewport->Size.y-32*dpi});
 if(ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.uninstall_neurotic_70533052", "Uninstall NeuRotic?"),nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){
  if(model.selected>=0&&model.selected<(int)model.games.size())ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.remove_neurotic_from_s_78027486", "Remove NeuRotic from %s?"),Neurotic::Translate(model.games[model.selected].title.c_str()).c_str());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.remove_recorded_neurotic_files_even_if_edited_mi_a3f6e006", "Remove recorded NeuRotic files, even if edited. Missing files are already removed. The game and unrelated files stay in place. Any unresolved file is listed for retry."));
  ImGui::Spacing();if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"),{.65f,.16f,.21f,1})){model.ConfirmUninstall();ImGui::CloseCurrentPopup();}ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape)){model.CancelUninstall();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
 }
 if(!model.showIssue&&!model.installer.busy)model.installer.ClearAntiCheatApproval();
 ImGui::End();ImGui::PopFont();
 // Every box finishes drawing with this frame's palette. The next frame
 // starts the existing water compositor from a uniformly old-theme image.
 if(changeTheme){model.light=nextLight;model.Save();}
}
}
