#include "GameInstallationStatus.h"
#include "GameTargetIdentity.h"
#include "../../../OptiScaler/resource.h"
#include "InstallCompletionDialog.h"
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
#include "NrDllManagerView.h"
#include <menu/ItemHelp.h>
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
static void ProxyPicker(HubModel& model,float dpi,bool queuedDecision=false){
 const auto selected=queuedDecision?model.QueuedProxyName():model.SelectedProxyName();
 const Game* game=queuedDecision?model.OperationGame():model.selected>=0&&model.selected<(int)model.games.size()?&model.games[model.selected]:nullptr;
 const auto recommendation=game&&!selected.empty()?ProxyRecommendationLabel(*game,selected):std::string{};
 const auto emptyChoice=Neurotic::Translate(Neurotic::UiLiteral("desktop.hubshell.choose_proxy_filename", "Choose proxy filename"));const auto preview=selected.empty()?emptyChoice:selected+(recommendation.empty()?"":" · "+recommendation);
 const auto label=Neurotic::UiLiteral("desktop.hubshell.file_proxy_name_4ce337e7", "File Proxy Name");
 const auto originalId=ImGui::GetID(label);const float left=ImGui::GetCursorPosX(),available=ImGui::GetContentRegionAvail().x;
 const float labelWidth=(std::min)(ImGui::GetFontSize()*14.f,available*.30f);
 const bool inlineLabel=available>=labelWidth+ImGui::GetStyle().ItemInnerSpacing.x+MeasureSavedChoiceWidth(available,ImGui::CalcTextSize(emptyChoice.c_str()).x);
 if(selected.empty())ImGui::PushStyleColor(ImGuiCol_Text,ui::StatusColor(ui::ReadyState::Error,model.light));
 ImGui::AlignTextToFramePadding();if(inlineLabel)ImGui::PushTextWrapPos(left+labelWidth);ImGui::TextWrapped("%s",Neurotic::Translate(label).c_str());if(inlineLabel){ImGui::PopTextWrapPos();ImGui::SameLine(left+labelWidth+ImGui::GetStyle().ItemInnerSpacing.x);}
 if(selected.empty())ImGui::PopStyleColor();
 ImGui::SetNextItemWidth((std::max)(1.f,ImGui::GetContentRegionAvail().x));
 if(selected.empty()){ImGui::PushStyleColor(ImGuiCol_Border,ui::StatusColor(ui::ReadyState::Error,model.light));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,std::max(ImGui::GetStyle().FrameBorderSize,dpi));}
 ImGui::BeginDisabled(model.installer.busy&&!model.InstallQueueWorkerActive()&&!model.BackgroundInspectionActive());ImGui::PushOverrideID(originalId);
 if(ImGui::BeginCombo("",preview.c_str())){
  for(int i=0;i<9;++i){auto hint=game?ProxyRecommendationLabel(*game,Proxies[i]):std::string{};auto label=std::string(Proxies[i])+(hint.empty()?"":" · "+hint);const bool active=selected==Proxies[i];if(ImGui::Selectable(label.c_str(),active)){if(queuedDecision)model.ChooseQueuedProxy(i);else model.ChooseProxy(i);}if(active)ImGui::SetItemDefaultFocus();}
  ImGui::EndCombo();
 }
 ImGui::PopID();ImGui::EndDisabled();
 if(selected.empty()){ImGui::PopStyleVar();ImGui::PopStyleColor();}
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.the_dll_name_used_to_load_neurotic_recommendatio_082acd23", "The DLL name used to load NeuRotic. Recommendations use this game's known graphics APIs; choose the one matching your game settings."));
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
struct SummaryActionMetrics {std::string caption;float textWidth,height;};
static SummaryActionMetrics MeasureSummaryAction(const char* label,float width,float dpi,int icon=-1){
 auto caption=Neurotic::Translate(label);caption.erase(0,caption.find_first_not_of(' '));
 const auto padding=ImGui::GetStyle().FramePadding;const float iconSpace=icon>=0?28*dpi:0.f;
 const float textWidth=std::max(1.f,width-2*padding.x-iconSpace);
 const float height=std::max(ImGui::GetFrameHeight(),ImGui::CalcTextSize(caption.c_str(),nullptr,false,textWidth).y+2*padding.y);
 return {std::move(caption),textWidth,height};
}
struct SummarySecondaryLayout {bool stacked;float refreshWidth,visibilityWidth,height;};
static SummarySecondaryLayout MeasureSummarySecondary(float width,float dpi){
 const auto& style=ImGui::GetStyle();
 const auto* refresh=Neurotic::UiLiteral("desktop.hubshell.refresh_d284aefe","Refresh");
 const auto* show=Neurotic::UiLiteral("desktop.library.show_game","Show");const auto* hide=Neurotic::UiLiteral("desktop.library.hide_game","Hide");
 const auto refreshText=Neurotic::Translate(refresh),showText=Neurotic::Translate(show),hideText=Neurotic::Translate(hide);
 const float refreshNatural=ImGui::CalcTextSize(refreshText.c_str()).x+2*style.FramePadding.x+29*dpi;
 const float visibilityNatural=std::max(ImGui::CalcTextSize(showText.c_str()).x,ImGui::CalcTextSize(hideText.c_str()).x)+2*style.FramePadding.x+dpi;
 const bool stacked=refreshNatural+visibilityNatural+style.ItemSpacing.x>width;
 const float refreshWidth=stacked?width:std::max((width-style.ItemSpacing.x)*.5f,refreshNatural);
 const float visibilityWidth=stacked?width:std::max(1.f,width-style.ItemSpacing.x-refreshWidth);
 const float refreshHeight=MeasureSummaryAction(refresh,refreshWidth,dpi,8).height;
 const float visibilityHeight=std::max(MeasureSummaryAction(show,visibilityWidth,dpi).height,MeasureSummaryAction(hide,visibilityWidth,dpi).height);
 return {stacked,refreshWidth,visibilityWidth,stacked?refreshHeight+visibilityHeight+style.ItemSpacing.y:std::max(refreshHeight,visibilityHeight)};
}
static float SummaryFooterHeight(float width,float dpi){
 ImGui::PushFont(nullptr,20);const float titleHeight=ImGui::GetTextLineHeight()*2;ImGui::PopFont();
 ImGui::PushFont(nullptr,14);const float sourceHeight=ImGui::GetTextLineHeight();ImGui::PopFont();
 const float actions=42*dpi+
  MeasureSummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510","Open game folder"),width,dpi,7).height+
  MeasureSummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_screenshots_6b9d871c","Open Screenshots"),width,dpi).height+
  MeasureSummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_ini_file_b892f0d6","Open INI File"),width,dpi).height+
  MeasureSummarySecondary(width,dpi).height;
 // Include the actual item/spacing advances and both separators. This common
 // budget depends on pane, font/style and locale, never the selected game's
 // notices or title length. Short panes retain a discoverable native scrollbar.
 return titleHeight+sourceHeight+actions+ImGui::GetStyle().ItemSpacing.y*13+2*dpi;
}
static bool SummaryAction(const char* label,float width,float dpi,const char* id=nullptr,int icon=-1){
 const auto measured=MeasureSummaryAction(label,width,dpi,icon);const auto& caption=measured.caption;
 const float textWidth=measured.textWidth,height=measured.height;const auto padding=ImGui::GetStyle().FramePadding;const float iconSpace=icon>=0?28*dpi:0.f;
 ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});const bool pressed=ui::Button(id?id:label,{width,height});ImGui::PopStyleColor();
 const auto at=ImGui::GetItemRectMin();const auto color=ImGui::GetColorU32(ImGuiCol_Text);
 if(icon>=0)ui::Glyph(ImGui::GetWindowDrawList(),icon,{at.x+padding.x,at.y+(height-22*dpi)*.5f},22*dpi,color);
 ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{at.x+padding.x+iconSpace,at.y+padding.y},color,caption.c_str(),nullptr,textWidth);
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",caption.c_str());return pressed;
}
static void SelectedGameTitle(const std::string& title,float width){
 std::string display=title;std::replace(display.begin(),display.end(),'\n',' ');std::replace(display.begin(),display.end(),'\r',' ');
 const auto at=ImGui::GetCursorScreenPos();const float line=ImGui::GetTextLineHeight();width=std::max(1.f,width);
 ImGui::Dummy({width,line*2});
 const char* begin=display.c_str();const char* end=begin+display.size();
 const char* split=ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(),begin,end,width);
 auto* draw=ImGui::GetWindowDrawList();
 if(split>=end)draw->AddText(at,ImGui::GetColorU32(ImGuiCol_Text),begin,end);
 else{
  draw->AddText(at,ImGui::GetColorU32(ImGuiCol_Text),begin,split);
  while(split<end&&(*split==' '||*split=='\t'))++split;
  ImGui::RenderTextEllipsis(draw,{at.x,at.y+line},{at.x+width,at.y+2*line},at.x+width,split,end,nullptr);
 }
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",title.c_str());
}
static void PortraitCover(HubModel& model,const Game& game,const ArtworkView& cover,float dpi,ImVec2 pos,ImVec2 size){
 auto draw=ImGui::GetWindowDrawList();
 draw->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},ImGui::GetColorU32(ImGuiCol_FrameBg),8*dpi);
 if(cover){const float scale=std::min(size.x/cover.width,size.y/cover.height);const ImVec2 imageAt{pos.x+(size.x-cover.width*scale)*.5f,pos.y+(size.y-cover.height*scale)*.5f};draw->AddImage((ImTextureID)(intptr_t)cover.texture,imageAt,{imageAt.x+cover.width*scale,imageAt.y+cover.height*scale});}
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
 if(steam&&Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.steam_chooses_the_executable_the_selected_file_i_1c59b94c", "Steam chooses the executable. The selected file is the installation target."));
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.steam_games_open_through_steam_other_games_use_t_1e2330c3", "Steam games open through Steam. Other games use the selected executable and its folder."));
}
static void ActivityOverlay(HubModel& model,float dpi){
 const bool scanning=model.discovery.busy,checking=model.installer.busy&&(model.page!=1||model.BackgroundInspectionActive()),diagnostics=model.gameDiagnosticsBusy||model.DiagnosticExportRunning();
 if(!scanning&&!checking&&!diagnostics)return;
 auto viewport=ImGui::GetMainViewport();float width=std::min(430*dpi,viewport->Size.x-32*dpi);
 ImGui::SetNextWindowPos({viewport->Pos.x+viewport->Size.x-width-16*dpi,viewport->Pos.y+viewport->Size.y-146*dpi});
 ImGui::SetNextWindowSize({width,68*dpi});
 // Passive status must never capture input from the controls beneath it.
 // A real library scan retains its interactive Cancel scan button.
 ImGui::Begin("ActivityOverlay",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|FixedPane|(scanning?ImGuiWindowFlags_None:ImGuiWindowFlags_NoInputs));
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
 const float countdownWidth=std::min(AnythingCountdownWidth(dpi),std::max(1.f,width));
 const float countdownHeight=AnythingCountdownHeight(dpi,countdownWidth);
 const float firstRowHeight=std::max(42*dpi,countdownHeight+4*dpi);
 const float firstRowWidth=std::max(0.f,width-countdownWidth-12*dpi);
 const bool countdownOwnRow=firstRowWidth<ImGui::CalcTextSize(pages[0]).x+40*dpi;
 float x=0,y=countdownOwnRow?firstRowHeight:0.f;int rows=countdownOwnRow?2:1;
 for(int i=0;i<6;++i){
  widths[i]=ImGui::CalcTextSize(pages[i]).x+40*dpi;
  const float gap=x>0?8*dpi:0;
  if(x>0&&x+gap+widths[i]>(rows==1?firstRowWidth:width)){x=0;y+=rows==1?firstRowHeight:42*dpi;++rows;}
  else x+=gap;
  offsets[i]={x,y};x+=widths[i];
 }
 ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});ImGui::BeginChild("Navigation",{width,firstRowHeight+(rows-1)*42*dpi},ImGuiChildFlags_None,FixedPane);ImGui::PopStyleColor();
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
 ImGui::SetCursorScreenPos({origin.x+width-countdownWidth,origin.y+(firstRowHeight-countdownHeight)*.5f});
 RenderAnythingCountdown(model,dpi);
 ImGui::EndChild();ImGui::PopFont();
}
static void ScanActions(HubModel& model,float dpi,const std::array<ImVec2,6>& positions,const std::array<float,6>& widths){
 ImGui::SetCursorScreenPos(positions[2]);
 ImGui::BeginDisabled(model.discovery.busy);
 if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.add_game_9dca998e", "+ Add game"))){auto path=PickExecutable();if(!path.empty())model.Add(path);}
 ImGui::SetCursorScreenPos(positions[3]);
 if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "    Add directory"),7,ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "Add directory"),dpi),dpi)){auto path=PickDirectory();if(!path.empty())model.AddDirectory(path);}
 ImGui::SetCursorScreenPos(positions[4]);
 ImGui::PushStyleColor(ImGuiCol_Text,{1,1,1,1});ImGui::PushStyleColor(ImGuiCol_Button,{.06f,.32f,.68f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.10f,.39f,.77f,1});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.04f,.24f,.54f,1});
 const float scanWidth=widths[4];
 if(ui::IconButton(model.discovery.busy?Neurotic::UiLiteral("desktop.hubshell.scanning_35a6c735", "    Scanning..."):Neurotic::UiLiteral("desktop.hubshell.scan_c58dc649", "    Scan"),6,scanWidth,dpi,model.discovery.busy))model.Scan();
 if(model.discovery.busy){auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();float width=hi.x-lo.x,inset=3*dpi;float start=model.discovery.progress>=0?0.f:float(std::fmod(ImGui::GetTime()*.5,1.0))*.7f,amount=model.discovery.progress>=0?model.discovery.progress:.3f;ImGui::GetWindowDrawList()->AddRectFilled({lo.x+inset+start*(width-2*inset),hi.y-4*dpi},{lo.x+inset+(start+amount)*(width-2*inset),hi.y-2*dpi},IM_COL32(160,220,255,255),dpi);if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.s_llu_seconds_0d568964", "%s · %llu seconds"),Neurotic::Translate(model.discovery.progressStage.empty()?Neurotic::UiLiteral("desktop.hubshell.discovering_games_4303f3d9", "Discovering games"):model.discovery.progressStage.c_str()).c_str(),(GetTickCount64()-model.discovery.started)/1000);}
 ImGui::PopStyleColor(4);ImGui::EndDisabled();
 ImGui::SetCursorScreenPos(positions[5]);
 if(ui::Button(Neurotic::UiLiteral("desktop.installqueue.title","Install queue"),{widths[5],0}))model.showInstallQueue=true;
 if(model.restoreInstallQueueFocus&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
  // Restore the opening control without inventing keyboard input after a
  // mouse close, or stealing focus from a queued review/completion dialog.
  ImGui::FocusWindow(ImGui::GetCurrentWindow());ImGui::SetFocusID(ImGui::GetItemID(),ImGui::GetCurrentWindow());model.restoreInstallQueueFocus=false;
 }
 if(model.InstallQueueBusy()){
  const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
  const ImVec2 center{lo.x+11*dpi,(lo.y+hi.y)*.5f};const float phase=model.reducedMotion?-1.5707963f:static_cast<float>(ImGui::GetTime()*6);
  auto* draw=ImGui::GetWindowDrawList();draw->PathArcTo(center,5*dpi,phase,phase+4.712389f,18);draw->PathStroke(ImGui::GetColorU32(ImGuiCol_Text),0,2*dpi);
 }
}
static bool FavoriteStar(bool favorite,float dpi){
 auto pos=ImGui::GetCursorScreenPos();bool pressed=ImGui::InvisibleButton("##Favorite",{28*dpi,52*dpi});ImVec2 points[10];for(int i=0;i<10;i++){float angle=-1.5707963f+i*.6283185f,r=(i%2?4.5f:10.f)*dpi;points[i]={pos.x+14*dpi+std::cos(angle)*r,pos.y+26*dpi+std::sin(angle)*r};}auto draw=ImGui::GetWindowDrawList();auto color=favorite?IM_COL32(168,103,0,255):ImGui::GetColorU32(ImGuiCol_TextDisabled);if(favorite)draw->AddConcavePolyFilled(points,10,color);else draw->AddPolyline(points,10,color,ImDrawFlags_Closed,1.5f*dpi);if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(favorite?Neurotic::UiLiteral("desktop.hubshell.remove_from_favorites_4bdb1498", "Remove from favorites"):Neurotic::UiLiteral("desktop.hubshell.add_to_favorites_58600dc7", "Add to favorites"));if(ImGui::IsItemFocused())draw->AddRect(pos,{pos.x+28*dpi,pos.y+52*dpi},ImGui::GetColorU32(ImGuiCol_NavCursor),4*dpi,0,1.5f*dpi);return pressed;
}static void LibraryHeader(HubModel& model,float dpi,bool collapsed){
 const bool shortViewport=ImGui::GetMainViewport()->Size.y<520*dpi;
 const auto& style=ImGui::GetStyle();const float gap=style.ItemSpacing.x,rowHeight=ImGui::GetFrameHeight(),rowPitch=rowHeight+style.ItemSpacing.y;
 const float available=std::max(1.f,ImGui::GetContentRegionAvail().x-24*dpi-2*style.ChildBorderSize);
 const char* orders[]={Neurotic::UiLiteral("desktop.hubshell.all_d76f15bf", "All"),Neurotic::UiLiteral("desktop.hubshell.favorites_first_de2d3c03", "Favorites first"),Neurotic::UiLiteral("desktop.hubshell.hidden_82b2ce85", "Hidden")};
 float filterWidth=160*dpi;for(auto label:orders)filterWidth=std::max(filterWidth,ImGui::CalcTextSize(label).x+rowHeight+2*style.FramePadding.x);
 std::array<float,6> widths{filterWidth,220*dpi,
  ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.add_game_9dca998e", "+ Add game")).x+2*style.FramePadding.x,
  ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.add_directory_476e2098", "Add directory"),dpi),
  std::max(ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.scan_c58dc649", "Scan"),dpi),ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.scanning_35a6c735", "Scanning..."),dpi)),
  std::max(150*dpi,ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.installqueue.title","Install queue")).x+2*style.FramePadding.x+22*dpi)};
 std::array<ImVec2,6> positions{};int rows=1;
 const float actions=widths[2]+widths[3]+widths[4]+widths[5]+3*gap;
 if(widths[0]+gap+widths[1]+gap+actions<=available){
  widths[1]=available-widths[0]-actions-2*gap;
  float x=0;for(int i=0;i<6;++i){positions[i]={x,0};x+=widths[i]+gap;}
 }else{
  // Keep filter/search together where possible, then wrap the action group.
  widths[0]=std::min(widths[0],available);
  if(widths[0]+gap+widths[1]<=available){positions[1]={widths[0]+gap,0};widths[1]=available-positions[1].x;}
  else{positions[1]={0,rowPitch};widths[1]=available;++rows;}
  float x=0,y=rows*rowPitch;++rows;
  for(int i=2;i<6;++i){
   // Keep Scan and its adjacent queue action together whenever the viewport
   // can fit the pair; only an intrinsically smaller viewport stacks them.
   if(i==4&&x>0&&widths[4]+gap+widths[5]<=available&&x+widths[4]+gap+widths[5]>available){x=0;y+=rowPitch;++rows;}
   if(x>0&&x+widths[i]>available){x=0;y+=rowPitch;++rows;}
   positions[i]={x,y};x+=widths[i]+gap;
  }
 }
 const float height=16*dpi+rows*rowHeight+(rows-1)*style.ItemSpacing.y+2*style.ChildBorderSize;
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,8*dpi});ImGui::PushStyleColor(ImGuiCol_Border,model.light?ImVec4(.60f,.68f,.78f,.75f):ImVec4(.48f,.59f,.72f,.48f));
 ImGui::BeginChild("LibraryHeader",{0,height},ImGuiChildFlags_Borders,FixedPane);ImGui::PopStyleColor();ImGui::PopStyleVar();
 const auto origin=ImGui::GetCursorScreenPos();for(auto& p:positions){p.x+=origin.x;p.y+=origin.y;}
 ImGui::SetCursorScreenPos(positions[0]);ImGui::SetNextItemWidth(widths[0]);int order=int(model.libraryFilter);
 if(ImGui::Combo("##LibraryOrder",&order,orders,3))model.SetLibraryFilter(LibraryFilter(order));
 // Reserve the clear action even when empty so typing never moves controls.
 ImGui::SetCursorScreenPos(positions[1]);ImGui::SetNextItemWidth(std::max(1.f,widths[1]-rowHeight-gap));
 const bool enter=ImGui::InputTextWithHint("##Search",Neurotic::UiLiteral("desktop.hubshell.search_games_9536b444", "Search games..."),model.search.data(),model.search.size(),ImGuiInputTextFlags_EnterReturnsTrue);
 const auto searchMin=ImGui::GetItemRectMin(),searchMax=ImGui::GetItemRectMax();
 const bool selectionLocked=model.BulkUninstallBusy()||(model.installer.busy&&!model.installer.readOnlyInstaller&&!model.InstallQueueWorkerActive());
 auto matches=model.search[0]?model.OrderedGames(model.search.data()):std::vector<int>{};
 auto selectMatch=[&](int index){
  // The model reports accepted immediate or deferred selection without
  // exposing its child-owning state. Rejection preserves the query.
  if(model.TrySelectLibraryGame(index)){model.search[0]=0;ImGui::ClearActiveID();}
 };
 if(enter&&!matches.empty())selectMatch(matches.front());
 ImGui::SetCursorScreenPos({positions[1].x+widths[1]-rowHeight,positions[1].y});
 ImGui::BeginDisabled(!model.search[0]);
 const auto clearPos=ImGui::GetCursorScreenPos();
 if(ImGui::InvisibleButton("##ClearLibrarySearch",{rowHeight,rowHeight},ImGuiButtonFlags_EnableNav)){model.search[0]=0;ImGui::ClearActiveID();}
 const ImVec2 clearCenter{clearPos.x+rowHeight*.5f,clearPos.y+rowHeight*.5f};const float clearRadius=rowHeight*.30f;
 auto clearDraw=ImGui::GetWindowDrawList();const auto clearColor=ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_Text:ImGuiCol_TextDisabled);
 clearDraw->AddCircle(clearCenter,clearRadius,clearColor,0,1.5f*dpi);
 const float cross=clearRadius*.42f;clearDraw->AddLine({clearCenter.x-cross,clearCenter.y-cross},{clearCenter.x+cross,clearCenter.y+cross},clearColor,1.5f*dpi);clearDraw->AddLine({clearCenter.x-cross,clearCenter.y+cross},{clearCenter.x+cross,clearCenter.y-cross},clearColor,1.5f*dpi);
 if(ImGui::IsItemFocused())clearDraw->AddRect(clearPos,{clearPos.x+rowHeight,clearPos.y+rowHeight},ImGui::GetColorU32(ImGuiCol_NavCursor),4*dpi);
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::UiLiteral("desktop.hubshell.clear_search", "Clear search"));
 ImGui::EndDisabled();
 ScanActions(model,dpi,positions,widths);ImGui::EndChild();
 if(model.search[0]&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)){
  const auto* viewport=ImGui::GetMainViewport();const float room=std::max(1.f,viewport->WorkPos.y+viewport->WorkSize.y-searchMax.y);
  ImGui::SetNextWindowPos({searchMin.x,searchMax.y});ImGui::SetNextWindowSize({searchMax.x-searchMin.x,std::min(room,std::min(260*dpi,(std::min(size_t(7),std::max(size_t(1),matches.size()))*32+16)*dpi))});
  ImGui::Begin("##LibrarySearchResults",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNavFocus);
  // Focusing the parent input raises its root window. Keep this separate
  // suggestion window above that root without taking keyboard focus away.
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
  ImGui::BeginDisabled(selectionLocked);
  if(matches.empty())ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.no_matching_games_a3e79e08", "No matching games"));
  for(int index:matches){ImGui::PushID(index);if(ImGui::Selectable(model.games[index].title.c_str(),model.selected==index,0,{0,28*dpi}))selectMatch(index);ImGui::PopID();}
  ImGui::EndDisabled();ImGui::End();
 }if(!shortViewport)ImGui::Spacing();
}
// Keep the existing card dimensions; the full backend reason is available on
// both the status and folder action without adding controls or moving them.
static void ModelReasonTooltip(const HubModel& model,float dpi){
 if(!Neurotic::ItemHelpHovered()||!model.preflight.is_object()||!model.preflight.contains("components"))return;
 const auto& components=model.preflight["components"];if(!components.is_object()||!components.contains("neuralModel"))return;
 const auto& nr=components["neuralModel"];if(!nr.is_object()||!nr.contains("reason")||!nr["reason"].is_string())return;
 auto reason=nr["reason"].get<std::string>();if(reason.empty())return;
 ImGui::BeginTooltip();ImGui::PushTextWrapPos(480*dpi);ImGui::TextUnformatted(Neurotic::Translate(reason.c_str()).c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
static const char* SharedComponentStatus(const HubModel& model,const char* key,bool compact=false){
 const auto state=ui::ComponentState(model,key);
 if(state==ui::ReadyState::Checking)return compact?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf", "Checking..."):Neurotic::UiLiteral("desktop.shared-files.checking", "Checking shared files...");
 if(state==ui::ReadyState::Ready)return compact?Neurotic::UiLiteral("desktop.shared-files.available-short", "Available"):Neurotic::UiLiteral("desktop.shared-files.available", "Shared files available");
 if(state==ui::ReadyState::Error)return ui::FileStatus(state);
 if(model.preflight.is_object()&&model.preflight.contains("components")&&model.preflight["components"].is_object()){
  const auto& components=model.preflight["components"];
  if(components.contains(key)&&components[key].is_object()&&components[key].contains("status")&&components[key]["status"].is_string()&&components[key]["status"].get_ref<const std::string&>()=="Missing")return compact?Neurotic::UiLiteral("desktop.shared-files.not-supplied", "Not supplied"):Neurotic::UiLiteral("desktop.shared-files.not-supplied-copy", "Not supplied for automatic copy");
 }
 return compact?Neurotic::UiLiteral("desktop.hubshell.unknown_c67449ba", "Unknown"):Neurotic::UiLiteral("desktop.shared-files.unknown", "Shared-file status unavailable");
}
static const char* ComponentConvenienceHelp(int component){
 if(component==1)return Neurotic::UiLiteral("desktop.shared-files.streamline_optional","Optional. Add Streamline files here for automatic copying into each game's Streamline folder.");
 if(component==2)return Neurotic::UiLiteral("desktop.shared-files.nrdll_optional","Optional. Save NR DLL profiles here for automatic copying to games.");
 return "";
}
static void ComponentCard(HubModel& model,int component,float width,float dpi,bool compact){
 const char* titles[]={Neurotic::UiLiteral("desktop.hubshell.microsoft_visual_c_9cd58c37","Microsoft Visual C++"),Neurotic::UiLiteral("desktop.hubshell.streamline_8743a2fe","Streamline"),Neurotic::UiLiteral("desktop.nrdll.manager_title","NR DLL Manager")};
 auto state=component==0?ui::RuntimeState(model):component==2?NrDllManagerAvailability(model):ui::ComponentState(model,"streamline");
 ImGui::PushFont(nullptr,compact?13:16);
 const auto padding=compact?ImVec2{8*dpi,8*dpi}:ImGui::GetStyle().WindowPadding;
 const float wrap=std::max(1.f,width-2*padding.x),spacing=ImGui::GetStyle().ItemSpacing.y;
 float helpHeight=0;for(const int kind:{1,2}){const auto text=Neurotic::Translate(ComponentConvenienceHelp(kind));helpHeight=std::max(helpHeight,ImGui::CalcTextSize(text.c_str(),nullptr,false,wrap).y);}
 const float actionY=padding.y+2*ImGui::GetTextLineHeightWithSpacing()+helpHeight+spacing;
 const float height=actionY+ImGui::GetFrameHeight()+padding.y;
 ImGui::PushID(component);if(compact)ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,padding);
 ImGui::BeginChild("ComponentCard",{width,height},ImGuiChildFlags_Borders,FixedPane);if(compact)ImGui::PopStyleVar();
 ui::StatusDot(state,model.light,dpi);ImGui::TextUnformatted(titles[component]);
 const char* status=component==0?(state==ui::ReadyState::Ready?"Ready":state==ui::ReadyState::Checking?Neurotic::UiLiteral("desktop.hubshell.checking_84a79aaf","Checking..."):Neurotic::UiLiteral("desktop.hubshell.install_or_repair_needed_c7ac96f0","Install or repair needed")):component==2?NrDllManagerAvailabilityLabel(model):SharedComponentStatus(model,"streamline",true);
 ImGui::TextColored(ui::StatusColor(state,model.light),"%s",Neurotic::Translate(status).c_str());
 if(component==1||component==2)ImGui::TextWrapped("%s",Neurotic::Translate(ComponentConvenienceHelp(component)).c_str());
 ImGui::SetCursorPosY(actionY);
 ImGui::BeginDisabled(component==0&&!model.CanDownloadRuntime());
 if(ui::IconButton(component==0?Neurotic::UiLiteral("desktop.hubshell.download_visual_c_a56ac7e0", "    Download Visual C++"):component==2?Neurotic::UiLiteral("desktop.nrdll.manage_short", "Manage"):Neurotic::UiLiteral("desktop.hubshell.open_folder_c24fe9c0", "    Open Folder"),7,component==0?0.f:component==2?std::max(1.f,ImGui::GetContentRegionAvail().x):ui::IconButtonWidth(Neurotic::UiLiteral("desktop.hubshell.open_folder_c24fe9c0", "Open Folder"),dpi),dpi)){
  if(component==0)model.OpenRuntimeDownload();else if(component==2)OpenNrDllManager(model);else model.OpenComponentFolder(true);
 }
 ImGui::EndDisabled();
 if(component==2&&Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::UiMessage("desktop.nrdll.presence_help","Saved profiles describe file availability. Runtime compatibility is checked when the selected DLL is used.").c_str());
 ImGui::EndChild();ImGui::PopID();ImGui::PopFont();
}
static void Dashboard(HubModel& model,float dpi){
 const bool compact=ImGui::GetContentRegionAvail().y<350*dpi;
 if(compact){ImGui::PushFont(nullptr,13);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,4*dpi});}
 const char* introduction=Neurotic::UiLiteral("desktop.hubshell.to_install_neurotic_to_games_go_to_the_installat_2b9d93e3", "To install NeuRotic to games, go to the Installation Library tab. To apply neural rendering to any window, use the NR Anything button in the upper-right corner.");
 const float refreshSide=32*dpi,introWidth=ImGui::GetContentRegionAvail().x,textWidth=std::max(1.f,introWidth-refreshSide-16*dpi);
 const float introHeight=std::max(refreshSide,ImGui::CalcTextSize(introduction,nullptr,false,textWidth).y);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("HomeIntroduction",{0,introHeight},ImGuiChildFlags_None,FixedPane|ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();
 auto introStart=ImGui::GetCursorScreenPos();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+textWidth);ImGui::TextUnformatted(introduction);ImGui::PopTextWrapPos();
 ImGui::SetCursorScreenPos({introStart.x+introWidth-refreshSide,introStart.y});if(RefreshButton("##RefreshHomeComponents",refreshSide,dpi,model.readiness.busy)){model.RefreshPreflight();if(model.nrDllManager&&!model.nrDllManager->Busy())model.nrDllManager->Refresh();}
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.nrdll.refresh_home", "Refresh Microsoft Visual C++, Streamline and saved NR DLL profiles"));ImGui::EndChild();
 float width=ImGui::GetContentRegionAvail().x,card=(width-ImGui::GetStyle().ItemSpacing.x*2)/3;
 for(int i=0;i<3;++i){if(i)ImGui::SameLine();ComponentCard(model,i,card,dpi,compact);}
 auto directories=model.SearchDirectories();
 ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.search_directories_3b5b04cd", "SEARCH DIRECTORIES"));
 if(compact){ImGui::SameLine();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.zu_19537331", "(%zu)"),directories.size());if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.folders_where_neurotic_looks_for_games_10c5b8a4", "Folders where NeuRotic looks for games."));ImGui::SameLine();}
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
  ImGuiListClipper clipper;clipper.Begin((int)directories.size());while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;i++){ImGui::PushID(directories[i].path.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);if(ImGui::Selectable(directories[i].path.c_str(),model.selectedDirectory==i,ImGuiSelectableFlags_SpanAllColumns))model.selectedDirectory=i;if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::Translate(directories[i].path.c_str()).c_str());ImGui::TableSetColumnIndex(1);ImGui::TextDisabled("%s",Neurotic::Translate(directories[i].source.c_str()).c_str());ImGui::PopID();}ImGui::EndTable();
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
 auto streamline=ui::ComponentState(model,Neurotic::UiLiteral("desktop.hubshell.streamline_3a9087b1", "streamline"));ComponentHeading(Neurotic::UiLiteral("desktop.hubshell.streamline_8743a2fe", "Streamline"),streamline,model.light,dpi);ImGui::TextColored(ui::StatusColor(streamline,model.light),"%s",Neurotic::Translate(SharedComponentStatus(model,"streamline")).c_str());
 ImGui::TextWrapped("%s",Neurotic::Translate(ComponentConvenienceHelp(1)).c_str());if(ui::IconButton(Neurotic::UiLiteral("desktop.hubshell.open_streamline_folder_7efeb999", "    Open Streamline folder"),7,0,dpi))model.OpenComponentFolder(true);ImGui::EndChild();ImGui::Spacing();
 ImGui::BeginChild("ModelChecklist",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollWithMouse);
 auto neural=ui::ComponentState(model,Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel"));ComponentHeading(Neurotic::UiLiteral("desktop.hubshell.nr_model_3814a794", "NR Model"),neural,model.light,dpi);
 ImGui::TextWrapped("%s",Neurotic::Translate(ComponentConvenienceHelp(2)).c_str());
 ImGui::TextColored(ui::StatusColor(neural,model.light),"%s",Neurotic::Translate(SharedComponentStatus(model,"neuralModel")).c_str());if(neural==ui::ReadyState::Error)ModelReasonTooltip(model,dpi);if(ui::IconButton(Neurotic::UiLiteral("desktop.nrdll.manage", "Manage NR DLLs"),0,0,dpi))OpenNrDllManager(model);if(neural==ui::ReadyState::Error)ModelReasonTooltip(model,dpi);ImGui::EndChild();
}
static void LogPanel(const std::string& report){
 float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.select_text_or_press_ctrl_a_ctrl_c_3d1f8a71", "Select text, or press Ctrl+A / Ctrl+C"));ImGui::SameLine(std::max(0.f,right));if(ui::CopyButton())ImGui::SetClipboardText(report.c_str());
 // Read-only editing supplies native selection and keyboard copying. Bound display size.
 std::vector<char> buffer(report.begin(),report.begin()+std::min(report.size(),size_t(2*1024*1024)));buffer.push_back(0);
 ImGui::InputTextMultiline("##LogText",buffer.data(),buffer.size(),{ImGui::GetContentRegionAvail().x,std::max(90.f,ImGui::GetContentRegionAvail().y)},ImGuiInputTextFlags_ReadOnly);
}
static void SettingsEditor(HubModel& model,bool naturalFields,float reservedReviewHeight){RenderSavedGameSettings(model,true,naturalFields,reservedReviewHeight,true);}
static void DataMaintenanceModal(HubModel& model,float dpi){
 if(model.showDataMaintenance)ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.app_data_maintenance_41387e8d", "App data maintenance"));
 auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowSize({std::min(580*dpi,viewport->Size.x-32*dpi),0},ImGuiCond_Appearing);
 ImGui::SetNextWindowSizeConstraints({0,0},{viewport->Size.x-32*dpi,viewport->Size.y-32*dpi});
 if(!ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.app_data_maintenance_41387e8d", "App data maintenance"),nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize))return;
 if(!model.showDataMaintenance){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
 const int action=model.dataMaintenanceAction;
 const char* titles[]={Neurotic::UiLiteral("desktop.hubshell.clear_app_data_394adcc5", "Clear App data?"),Neurotic::UiLiteral("desktop.hubshell.clear_game_profiles_84c81f6e", "Clear Game Profiles?"),Neurotic::UiLiteral("desktop.hubshell.open_artwork_cache_ca1265c8", "Open Artwork Cache?")};
 ImGui::TextUnformatted(titles[std::clamp(action,0,2)]);ImGui::Spacing();
 if(action==0)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.clears_the_local_library_including_hidden_games__516f3998", "Clears the local library (including hidden games), preferences, game profiles, artwork and operation receipts. Saved NR DLL profiles and other imported Runtime files are preserved. NeuRotic will restart. Installed game files are untouched."));
 else if(action==1)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.clears_saved_game_inspection_and_profile_data_th_b9afdcd0", "Clears saved game inspection and profile data. The library, hidden games and settings inside each game's folder stay unchanged. Fresh details will be read the next time you select a game. No restart is needed."));
 else ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.opens_the_local_artwork_cache_in_file_explorer_t_028caefc", "Opens the local artwork cache in File Explorer. This button does not delete images or require a restart."));
 if(action<2){ImGui::Spacing();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.location_s_dbf519b1", "Location: %s"),Neurotic::Translate(Utf8(model.dataMaintenancePlan.root.wstring()).c_str()).c_str());ImGui::Text(Neurotic::UiLiteral("desktop.hubshell.files_zu_955176be", "Files: %zu"),model.dataMaintenancePlan.fileCount);for(const auto& issue:model.dataMaintenancePlan.issues)ImGui::TextWrapped("%s",Neurotic::Translate(issue.c_str()).c_str());}
 ImGui::Spacing();ImGui::BeginDisabled(action<2&&!model.dataMaintenancePlan.Ready());
 if(ui::ActionButton(action==0?Neurotic::UiLiteral("desktop.hubshell.clear_and_restart_38557994", "Clear and restart"):action==1?Neurotic::UiLiteral("desktop.hubshell.clear_game_profiles_137fdc81", "Clear Game Profiles"):Neurotic::UiLiteral("desktop.hubshell.open_artwork_cache_6b22c5fa", "Open Artwork Cache"),{.72f,.12f,.17f,1})){model.ConfirmDataMaintenance();ImGui::CloseCurrentPopup();}
 ImGui::EndDisabled();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape)){model.CancelDataMaintenance();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
}
static bool GameIssueMatches(const Json& issue,const Game& game){
 if(!issue.is_object()||!issue.contains("target")||game.target.path.empty())return false;
 try{const auto& target=issue.at("target");const auto path=target.is_string()?target.get<std::string>():target.at("executable").get<std::string>();
  return GameTargetMatches(path,game.target.path);
 }catch(const std::exception&){return false;}
}
static void GameIssueModal(HubModel& model,float dpi){
 static bool acceptReplacement=false,rememberChoice=false;static std::string issueKey;
 const auto head=model.CurrentQueuedInstall();const QueuedInstall* queued=nullptr;
 if(model.reviewedInstallId){queued=model.FindQueuedInstall(model.reviewedInstallId);if(!queued||queued->phase!=QueuedInstall::Phase::NeedsAttention){model.reviewedInstallId=0;model.showIssue=false;return;}}
 else if(head&&GameIssueMatches(model.operationIssue,head->game))queued=head;
 const uint64_t itemId=queued?queued->id:0;
 const bool queueDecision=queued&&head&&queued->id==head->id&&GameIssueMatches(model.operationIssue,queued->game);
 const bool ownsDecision=queueDecision||!model.InstallQueueBusy();
 Json issue=queued&&!queueDecision?Json{{"decisionKind","QueuedInstall"},{"target",queued->game.target.path},{"reason",queued->summary}}:model.operationIssue;
 auto closeIssue=[&]{model.showIssue=false;model.reviewedInstallId=0;ImGui::CloseCurrentPopup();};
 if(model.showIssue&&!issue.is_null()){auto key=issue.dump();if(issueKey!=key||!ImGui::IsPopupOpen(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"))){issueKey=key;acceptReplacement=rememberChoice=false;}ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"));}
 bool antiCheat=issue.is_object()&&issue.value("decisionKind",std::string{})==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk");
 auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowSize({std::min(650*dpi,viewport->Size.x-40*dpi),std::min((antiCheat?360.f:430.f)*dpi,viewport->Size.y-40*dpi)},ImGuiCond_Appearing);
 if(!ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273", "Game needs attention"),&model.showIssue,ImGuiWindowFlags_NoResize)){if(!model.showIssue&&!model.showReview){if(ownsDecision){model.installer.ClearAntiCheatApproval();if(model.fileReview.Active())model.CancelFileReview();}model.reviewedInstallId=0;}return;}
 if(queued)ImGui::TextWrapped("%s",queued->game.title.c_str());
 auto kind=issue.value("decisionKind","");
 if(kind=="FileConflict"){
  if(!model.fileReview.Active()){model.CancelFileReview();ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.s_appears_to_not_have_come_from_neurotic_how_sho_8c90c02a", "%s appears to not have come from NeuRotic. How should we proceed?"),Neurotic::Translate(model.fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).get<std::string>().c_str()).c_str());
  ImGui::Spacing();ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.file_zu_of_zu_dea4e31b", "File %zu of %zu"),model.fileReview.Index()+1,model.fileReview.Count());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.replace_writes_the_neurotic_file_skip_leaves_thi_e430ea8d", "Replace writes the NeuRotic file. Skip leaves this file alone and continues the other files; required skips leave a partial installation. Cancel stops before copying."));
  ImGui::Spacing();ImGui::BeginDisabled(model.installer.busy);
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace"));ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.skip_9e5fb0c9", "Skip")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.skip_9e5fb0c9", "Skip"));ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape))model.CancelFileReview();
  if(model.fileReview.Active()&&model.fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))==Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll")&&issue.value(Neurotic::UiLiteral("desktop.hubshell.cankeepreshade_5ecf0788", "canKeepReShade"),false)){ImGui::Spacing();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.keep_reshade_aa1a1138", "Keep ReShade")))model.DecideFile(Neurotic::UiLiteral("desktop.hubshell.keepreshade_b63abfd0", "KeepReShade"));if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.rename_your_dxgi_dll_to_reshade64_dll_and_enable_6bd057fa", "Rename your dxgi.dll to ReShade64.dll and enable coexistence. This rename is recorded for Uninstall."));}
  ImGui::EndDisabled();if(!model.showIssue)ImGui::CloseCurrentPopup();ImGui::EndPopup();return;
 }
 const float halfWidth=std::max(1.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f);
 const auto retryAction=MeasureSavedReviewAction(Neurotic::UiLiteral("desktop.installqueue.retry","Retry this item"),halfWidth);
 const auto editAction=MeasureSavedReviewAction(Neurotic::UiLiteral("desktop.installqueue.cancel_and_edit","Cancel and edit"),halfWidth);
 const float footerHeight=(antiCheat?ImGui::GetFrameHeightWithSpacing():3.f*(std::max({ImGui::GetFrameHeight(),retryAction.height,editAction.height})+ImGui::GetStyle().ItemSpacing.y))+2*ImGui::GetStyle().ItemSpacing.y+1;
 ImGui::BeginChild("IssueExplanation",{0,std::max(1.f,ImGui::GetContentRegionAvail().y-footerHeight)});
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){
  auto report=issue.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object());auto headline=report.value("headline",std::string{});bool known=headline=="detected"||headline=="documented";
  ImGui::PushFont(nullptr,24);ImGui::PushStyleColor(ImGuiCol_Text,model.light?ImVec4(.87f,.02f,.06f,1):ImVec4(1.f,.18f,.23f,1));
  ImGui::TextWrapped("%s",Neurotic::Translate(known?Neurotic::UiLiteral("desktop.hubshell.warning_this_game_uses_anti_cheat_installing_neu_57e1d706", "Warning: This game uses anti-cheat. Installing NeuRotic could get you banned."):Neurotic::UiLiteral("desktop.hubshell.warning_this_game_may_use_anti_cheat_installing__d568c3bf", "Warning: This game may use anti-cheat. Installing NeuRotic could get you banned.")).c_str());ImGui::PopStyleColor();ImGui::PopFont();ImGui::Spacing();
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.neurotic_compatibility_and_account_safety_are_no_2956d680", "NeuRotic compatibility and account safety are not confirmed."));
  ImGui::Checkbox(Neurotic::UiLiteral("desktop.hubshell.i_understand_the_risk_and_want_to_continue_0784e561", "I understand the risk and want to continue"),&acceptReplacement);ImGui::Spacing();
  ImGui::Checkbox(Neurotic::UiLiteral("desktop.installation.remember_risk","Remember my choice for this game"),&rememberChoice);ImGui::Spacing();
  if(report.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")==Neurotic::UiLiteral("desktop.hubshell.cancelled_b89ba77e", "cancelled"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.the_check_was_canceled_refresh_the_game_before_p_cedfa03a", "The check was canceled. Refresh the game before proceeding."));
  if(ImGui::CollapsingHeader(Neurotic::UiLiteral("desktop.hubshell.detection_details_7109c797", "Detection details"))){ImGui::TextWrapped("%s",Neurotic::Translate(ResultMessage(report,Neurotic::UiLiteral("desktop.hubshell.anti_cheat_status_could_not_be_established_f4c62026", "Anti-cheat status could not be established.")).c_str()).c_str());ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.local_scan_s_ca2932b2", "Local scan: %s"),Neurotic::Translate(report.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),Neurotic::UiLiteral("desktop.hubshell.incomplete_ae0c5c0a", "incomplete")).c_str()).c_str());
   if(report.contains("findings")&&report["findings"].is_array())for(auto& finding:report["findings"])ImGui::BulletText("%s (%s)",Neurotic::Translate(finding.value(Neurotic::UiLiteral("desktop.translationeditorwindow.name_ef8e2b72", "name"),finding.value("provider",Neurotic::UiLiteral("desktop.hubshell.anti_cheat_component_331c188b", "Anti-cheat component"))).c_str()).c_str(),Neurotic::Translate(finding.value("state","possible").c_str()).c_str());
   if(report.contains(Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings"))&&report[Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")].is_array()&&!report[Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")].empty())ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.earlier_findings_remain_relevant_because_this_ch_1342c291", "Earlier findings remain relevant because this check did not finish."));
  }
 }else{
  ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.hubshell.action_needed_2262b939", "ACTION NEEDED"));ImGui::Spacing();ImGui::TextWrapped("%s",Neurotic::Translate(ResultMessage(issue,Neurotic::UiLiteral("desktop.hubshell.the_operation_could_not_continue_nothing_will_be_3d3f7dc1", "The operation could not continue. Nothing will be retried automatically.")).c_str()).c_str());ImGui::Spacing();
  if(kind==Neurotic::UiLiteral("desktop.hubshell.proxyrequired_b6e34ce5", "ProxyRequired")){
   ProxyPicker(model,dpi,queueDecision);ImGui::Spacing();
   ImGui::BeginDisabled((queueDecision?model.QueuedProxyName():model.SelectedProxyName()).empty()||model.installer.busy||!ownsDecision);
   if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"))){closeIssue();if(queueDecision)model.RetryQueuedInstall(itemId);else model.Plan("Install");}
   ImGui::EndDisabled();
  }
  else if(kind=="QueuedInstall")ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installqueue.resolve_here","Retry this item, or change its install options."));
   else ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.close_the_game_and_retry_the_selected_action_aft_5ba7c335", "Close the game and retry the selected action after resolving the listed paths. Missing or edited recorded files do not require Repair."));
  for(const auto* key:{Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors"),Neurotic::UiLiteral("desktop.hubshell.notes_fd9f1432", "notes")})if(issue.contains(key)&&issue[key].is_array())for(const auto& item:issue[key])if(item.is_string()||item.is_object()){
   const auto detail=ResultDetail(item);ImGui::TextWrapped("%s",Neurotic::Translate(detail.c_str()).c_str());
  }
  if(!model.bundleStatus.empty()){ImGui::Spacing();ImGui::TextWrapped("%s",Neurotic::Translate(model.bundleStatus.c_str()).c_str());}
 }
 ImGui::EndChild();ImGui::Separator();
 if(queued&&!antiCheat){
  if(kind!="ProxyRequired"){
   ImGui::BeginDisabled(!queueDecision||model.installer.busy);
   if(SavedReviewButton(Neurotic::UiLiteral("desktop.installqueue.retry","Retry this item"),retryAction)){closeIssue();model.RetryQueuedInstall(itemId);}
   ImGui::EndDisabled();ImGui::SameLine();
  }
  if(SavedReviewButton(Neurotic::UiLiteral("desktop.installqueue.cancel_and_edit","Cancel and edit"),editAction)){closeIssue();model.EditQueuedInstall(itemId);}
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510","Open game folder")))model.OpenQueuedInstallFolder(itemId);
 }
 if(!model.InstallQueueBusy()&&kind!=Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510", "Open game folder")))model.OpenSelectedGameFolder();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.check_again_c396af3d", "Check again"))){model.showIssue=false;ImGui::CloseCurrentPopup();model.RefreshSelected();}}
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")){
  bool canProceed=acceptReplacement&&ownsDecision&&!model.installer.busy&&issue.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object()).value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")!=Neurotic::UiLiteral("desktop.hubshell.cancelled_b89ba77e", "cancelled");
  ImGui::BeginDisabled(!canProceed);if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.proceed_3eb1e2a6", "Proceed"))&&canProceed){model.AcknowledgeRisk(rememberChoice);if(!model.showIssue)ImGui::CloseCurrentPopup();}ImGui::EndDisabled();
 }else if(!model.InstallQueueBusy()){
  ImGui::BeginDisabled(model.DiagnosticExportRunning());
  if(ui::Button(model.DiagnosticExportRunning()?Neurotic::UiLiteral("desktop.hubshell.exporting_diagnostic_bundle_da4d29f0", "Exporting diagnostic bundle..."):Neurotic::UiLiteral("desktop.hubshell.export_diagnostic_bundle_d6689dd1", "Export diagnostic bundle"))){auto destination=PickDirectory(nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_choose_diagnostic_zip_destination_d606f5de", "NeuRotic — choose diagnostic ZIP destination")));if(!destination.empty())model.ExportSelectedDiagnostics(destination);}
  ImGui::EndDisabled();
  if(!model.bundlePath.empty()){ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_bundle_folder_78c51e18", "Open bundle folder")))model.OpenBundleFolder();}
 }
 if(kind=="QueuedInstall"&&queueDecision&&queued->request.value("operation","")!="UpdateNrDll"&&queued->nrDllSnapshot.mode==NrDllCopyMode::Profile){
  ImGui::BeginDisabled(model.installer.busy||model.NrDllQueueSourcePending());
  if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.queue_manual","Install this item without copying an NR DLL"))){model.UseManualNrDllForQueue();ImGui::CloseCurrentPopup();}
  ImGui::EndDisabled();
 }
 if(kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk"))ImGui::SameLine();
 if(ImGui::IsKeyPressed(ImGuiKey_Escape)||ui::Button((kind==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")||kind==Neurotic::UiLiteral("desktop.hubshell.runtimemissing_8fa29c64", "RuntimeMissing"))?Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"):Neurotic::UiLiteral("desktop.hubshell.close_4bae39a9", "Close"))){if(ownsDecision)model.installer.ClearAntiCheatApproval();closeIssue();if(queueDecision&&(kind=="AntiCheatRisk"||kind=="RuntimeMissing"))model.CancelQueuedInstall(itemId);}ImGui::SetItemDefaultFocus();ImGui::EndPopup();
}
static void InstallQueuePanel(HubModel& model,float dpi){
 const auto title=Neurotic::UiMessage("desktop.installqueue.title","Install queue")+"###InstallQueueDetails";
 if(model.showInstallQueue)ImGui::OpenPopup(title.c_str());
 if(!ImGui::IsPopupOpen(title.c_str()))return;
 const auto* viewport=ImGui::GetMainViewport();const auto room=viewport->WorkSize;
 const ImVec2 maximum{std::max(1.f,room.x-32*dpi),std::max(1.f,room.y-32*dpi)};
 ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Appearing,{.5f,.5f});
 ImGui::SetNextWindowSize({std::min(900*dpi,maximum.x),std::min(600*dpi,maximum.y)},ImGuiCond_FirstUseEver);
 ImGui::SetNextWindowSizeConstraints({std::min(520*dpi,maximum.x),std::min(320*dpi,maximum.y)},maximum);
 const bool wasOpen=model.showInstallQueue;
 if(!ImGui::BeginPopupModal(title.c_str(),&model.showInstallQueue,ImGuiWindowFlags_NoSavedSettings)){
  if(wasOpen&&!model.showInstallQueue)model.restoreInstallQueueFocus=true;return;
 }
 auto close=[&]{model.showInstallQueue=false;model.restoreInstallQueueFocus=true;ImGui::CloseCurrentPopup();};
 ImGui::TextWrapped("%s",Neurotic::UiLiteral("desktop.installqueue.help","Installs run in order. You can browse and queue another game. A game needing attention pauses the queue. Closing the App cancels items that have not started installing."));
 const auto terminal=[](QueuedInstall::Phase phase){return phase==QueuedInstall::Phase::Succeeded||phase==QueuedInstall::Phase::Failed||phase==QueuedInstall::Phase::Cancelled;};
 const auto& items=model.InstallQueueItems();
 const bool hasHistory=std::any_of(items.begin(),items.end(),[&](const auto& item){return terminal(item.phase);});
 ImGui::BeginDisabled(!hasHistory);
 if(ui::Button(Neurotic::UiLiteral("desktop.installqueue.clear_history","Clear history")))model.ClearInstallQueueHistory();
 ImGui::EndDisabled();ImGui::Spacing();
 const float footer=ImGui::GetFrameHeight()+2*ImGui::GetStyle().ItemSpacing.y;
 const float height=std::max(1.f,ImGui::GetContentRegionAvail().y-footer);
 ImGui::BeginChild("InstallQueueRows",{0,height},ImGuiChildFlags_Borders);
 uint64_t remove=0,cancel=0,review=0;
 if(model.InstallQueueItems().empty())ImGui::TextWrapped("%s",Neurotic::UiLiteral("desktop.installqueue.empty","The queue is empty."));
 const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
 const float actions=std::min(available*.36f,std::max(120*dpi,ImGui::GetFontSize()*10.f));
 if(ImGui::BeginTable("InstallQueueTable",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH)){
  ImGui::TableSetupColumn("Icon",ImGuiTableColumnFlags_WidthFixed,40*dpi);
  ImGui::TableSetupColumn("Game",ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthFixed,actions);
  for(const auto& item:model.InstallQueueItems()){
   ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(item.id)));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
   const auto iconAt=ImGui::GetCursorScreenPos();GameIcon(model,item.game,iconAt,36*dpi);ImGui::Dummy({36*dpi,36*dpi});ImGui::TableSetColumnIndex(1);
   ImGui::TextWrapped("%s",item.game.title.c_str());
   if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",item.game.target.path.c_str());
   const bool active=model.CurrentQueuedInstall()&&model.CurrentQueuedInstall()->id==item.id;
   const char* status=item.phase==QueuedInstall::Phase::Queued?Neurotic::UiLiteral("desktop.installqueue.in_queue","In queue"):
    item.phase==QueuedInstall::Phase::Succeeded?Neurotic::UiLiteral("desktop.installation.installed","Installed"):
    item.phase==QueuedInstall::Phase::NeedsAttention?Neurotic::UiLiteral("desktop.installqueue.needs_attention","Needs attention"):
    item.phase==QueuedInstall::Phase::Preparing?Neurotic::UiLiteral("desktop.installqueue.preparing","Preparing install"):
    item.phase==QueuedInstall::Phase::Installing?Neurotic::UiLiteral("desktop.installqueue.installing","Installing"):
    item.phase==QueuedInstall::Phase::Cancelled?Neurotic::UiLiteral("desktop.installqueue.cancelled","Cancelled"):Neurotic::UiLiteral("desktop.installqueue.failed","Failed");
   ImGui::TextWrapped("%s",Neurotic::Translate(status).c_str());
   if(item.phase!=QueuedInstall::Phase::Queued&&item.summary!=status&&!item.summary.empty()&&item.summary!="Succeeded")ImGui::TextWrapped("%s",Neurotic::Translate(item.summary.c_str()).c_str());
   if(active&&model.InstallQueueWorkerActive()){
    const auto& job=model.installer;if(!job.progressStage.empty())ImGui::TextWrapped("%s",Neurotic::Translate(job.progressStage.c_str()).c_str());
    ImGui::ProgressBar(job.progress<0?-static_cast<float>(ImGui::GetTime()):job.progress,{std::max(1.f,ImGui::GetContentRegionAvail().x),8*dpi},"");
   }
   ImGui::TableSetColumnIndex(2);
   auto action=[&](const char* label){return SavedReviewButton(label,MeasureSavedReviewAction(label,std::max(1.f,ImGui::GetContentRegionAvail().x)));};
   if(item.phase==QueuedInstall::Phase::NeedsAttention){if(action(Neurotic::UiLiteral("desktop.installqueue.review","Review")))review=item.id;}
   if(item.phase==QueuedInstall::Phase::Queued||item.phase==QueuedInstall::Phase::Preparing||item.phase==QueuedInstall::Phase::NeedsAttention){
    if(action(Neurotic::UiLiteral("desktop.installqueue.cancel_item","Cancel queued item")))cancel=item.id;
   }else if(terminal(item.phase)){
    if(action(Neurotic::UiLiteral("desktop.installqueue.remove_item","Remove item")))remove=item.id;
   }
   ImGui::PopID();
  }ImGui::EndTable();
 }
 ImGui::EndChild();
 // All queue actions can advance state; run them after every row reference dies.
 if(review&&model.ReviewQueuedInstall(review))close();
 if(cancel)model.CancelQueuedInstall(cancel);
 if(remove&&!model.RemoveQueuedInstall(remove))model.message=Neurotic::UiMessage("desktop.installqueue.finishing_item","This item is still finishing. Try again shortly.");
 if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.close_4bae39a9","Close"))||ImGui::IsKeyPressed(ImGuiKey_Escape,false))close();
 ImGui::EndPopup();
}
static void UninstallModal(HubModel& model,float dpi){
 const auto* viewport=ImGui::GetMainViewport();
 if(model.showUninstallConfirm)ImGui::OpenPopup(Neurotic::UiLiteral("desktop.hubshell.uninstall_neurotic_70533052", "Uninstall NeuRotic?"));
 ImGui::SetNextWindowSize({std::min(610*dpi,viewport->Size.x-32*dpi),0},ImGuiCond_Appearing);
 ImGui::SetNextWindowSizeConstraints({0,0},{viewport->Size.x-32*dpi,viewport->Size.y-32*dpi});
 if(ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.hubshell.uninstall_neurotic_70533052", "Uninstall NeuRotic?"),nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){
  if(model.selected>=0&&model.selected<(int)model.games.size())ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.remove_neurotic_from_s_78027486", "Remove NeuRotic from %s?"),Neurotic::Translate(model.games[model.selected].title.c_str()).c_str());
  ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.remove_recorded_neurotic_files_even_if_edited_mi_a3f6e006", "Remove recorded NeuRotic files, even if edited. Missing files are already removed. The game and unrelated files stay in place. Any unresolved file is listed for retry."));
  ImGui::Spacing();if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"),{.65f,.16f,.21f,1})){model.ConfirmUninstall();ImGui::CloseCurrentPopup();}ImGui::SameLine();
  if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape)){model.CancelUninstall();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
 }
}
// A modal request survives deferral. Only the actual root popup owner may
// replace it; unrelated combos and nested editors finish normally.
static void DrawHubModals(HubModel& model,float dpi){
 const auto issue=ImGui::GetID(Neurotic::UiLiteral("desktop.hubshell.game_needs_attention_4a754273","Game needs attention"));
 const auto maintenance=ImGui::GetID(Neurotic::UiLiteral("desktop.hubshell.app_data_maintenance_41387e8d","App data maintenance"));
 const auto uninstall=ImGui::GetID(Neurotic::UiLiteral("desktop.hubshell.uninstall_neurotic_70533052","Uninstall NeuRotic?"));
 const auto manager=ImGui::GetID("###NrDllManager"),queue=ImGui::GetID("###InstallQueueDetails"),completion=ImGui::GetID("##InstallationComplete");
 const bool needsIssue=model.showIssue&&(model.reviewedInstallId||!model.operationIssue.is_null());
 const auto pending=[&](ImGuiID id){
  if(id==issue)return needsIssue;if(id==maintenance)return model.showDataMaintenance;
  if(id==uninstall)return model.showUninstallConfirm;if(id==manager)return model.showNrDllManager;
  if(id==queue)return model.showInstallQueue;
  if(id==completion)return model.installCompletion.Pending()!=nullptr&&!model.InstallQueueBusy()&&!model.restartForMaintenance;
  return false;
 };
 ImGuiID active=GImGui->OpenPopupStack.empty()?0:GImGui->OpenPopupStack[0].PopupId;
 const bool owned=active&&(active==issue||active==maintenance||active==uninstall||active==manager||active==queue||active==completion);
 if(active&&!owned)return;
 // Queue details has no uncommitted edits. Suspend its presentation for the
 // actual queued decision, preserving the request to return to the queue.
 if(owned&&(!pending(active)||(active==queue&&needsIssue))){ImGui::ClosePopupToLevel(0,true);active=0;}
 if(!active){for(const auto id:{maintenance,uninstall,manager,issue,queue,completion})if(pending(id)){active=id;break;}}
 if(active==maintenance)DataMaintenanceModal(model,dpi);
 else if(active==uninstall)UninstallModal(model,dpi);
 else if(active==manager)DrawNrDllManager(model,dpi);
 else if(active==issue)GameIssueModal(model,dpi);
 else if(active==queue)InstallQueuePanel(model,dpi);
 else if(active==completion)ui::InstallationCompletionDialog(model.installCompletion,model.light,dpi);
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
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.open_the_neurotic_github_homepage_44b61456", "Open the NeuRotic GitHub homepage"));
 auto headerDraw=ImGui::GetWindowDrawList();if(brand){float scale=226*dpi/1805.f,h=318*scale,split=277*scale;auto id=(ImTextureID)(intptr_t)brand;
  headerDraw->AddImage(id,logoPos,{logoPos.x+split,logoPos.y+h},{176.f/2155,198.f/730},{453.f/2155,516.f/730});headerDraw->AddImage(id,{logoPos.x+split,logoPos.y},{logoPos.x+226*dpi,logoPos.y+h},{453.f/2155,198.f/730},{1981.f/2155,516.f/730},ImGui::GetColorU32(ImGuiCol_Text));
 }else headerDraw->AddText(logoPos,ImGui::GetColorU32(ImGuiCol_Text),"NeuRotic");
 // Keep the customer banner at the product version; PE strings retain build identity.
 ImGui::PushFont(nullptr,13);const char* buildLabel="0.9.9";
 const ImVec2 buildLo{logoPos.x,logoPos.y+42*dpi},buildHi{logoPos.x+226*dpi,buildLo.y+ImGui::GetTextLineHeight()};
 const ImVec2 buildText{std::max(buildLo.x,buildHi.x-ImGui::CalcTextSize(buildLabel).x),buildLo.y};
 ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
 ImGui::RenderTextEllipsis(headerDraw,buildText,buildHi,buildHi.x,buildLabel,nullptr,nullptr);ImGui::PopStyleColor();
 ImGui::PopFont();
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
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s%s%s",Neurotic::Translate(update.detail.c_str()).c_str(),Neurotic::Translate(update.tag.empty()?"":" · ").c_str(),Neurotic::Translate(update.tag.c_str()).c_str());
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
 // A monitor work area can be smaller than the ordinary scaled minimum.
 // Keep Home and Library content reachable through the native page scroller.
 const bool scrollPage=model.page==0||model.page==1;
 const bool pageChanged=model.lastNavigationPage!=model.page;
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{(shortViewport?8.f:12.f)*dpi,(shortViewport?8.f:12.f)*dpi});ImGui::BeginChild("PageContent",{0,bodyHeight},ImGuiChildFlags_Borders,scrollPage?ImGuiWindowFlags_None:FixedPane);ImGui::PopStyleVar();if(!scrollPage||pageChanged)ImGui::SetScrollY(0);
 auto pagePos=ImGui::GetWindowPos(),pageSize=ImGui::GetWindowSize();
 if(model.lastNavigationPage!=model.page){if(model.lastNavigationPage==1)model.LeaveLibrarySelection();model.lastNavigationPage=model.page;model.pageShownAt=ImGui::GetTime();}
 float reveal=model.reducedMotion?1.f:std::clamp((float)(ImGui::GetTime()-model.pageShownAt)/.18f,0.f,1.f);ImGui::Dummy({0,(1-reveal)*8*dpi});ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*(.85f+.15f*reveal));
 ImVec2 selectedPos{},selectedSize{};bool selectedVisible=false;bool busy=model.installer.busy||model.InstallQueueBusy();ImGui::BeginDisabled(false);
 if(model.page==0)Dashboard(model,dpi);
 else if(model.page==1){
  auto& layoutStorage=ImGui::GetCurrentWindow()->StateStorage;auto collapsedId=ImGui::GetID("##LibraryListCollapsed");bool collapsed=layoutStorage.GetBool(collapsedId,false);
  LibraryHeader(model,dpi,collapsed);
  float left=std::clamp(ImGui::GetContentRegionAvail().x*.30f,235*dpi,335*dpi);bool columns=true;
  // Do not collapse game controls to one pixel when the toolbar fills a
  // constrained work area. PageContent scrolls to this readable shared panel.
  float panelHeight=std::max(260*dpi,ImGui::GetContentRegionAvail().y),listHeight=panelHeight;
  const auto panelsOrigin=ImGui::GetCursorScreenPos();float fraction=1-ui::Animate(ImGui::GetID("##LibrarySlide"),collapsed?1.f:0.f);if(fraction<.0001f)fraction=0;float listWidth=left*fraction;
  if(listWidth>1){ImGui::BeginChild("LibraryColumn",{listWidth,listHeight},ImGuiChildFlags_None,FixedPane);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,12*dpi});ImGui::BeginChild("LibraryList",ImVec2(0,std::max(1.f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders);ImGui::PopStyleVar();
  ImGui::BeginDisabled(model.installer.busy&&!model.installer.readOnlyInstaller&&!model.InstallQueueWorkerActive());
  auto visible=model.OrderedGames(model.search.data());
  ImGuiListClipper clipper;clipper.Begin((int)visible.size(),62*dpi);
  while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;row++){
   int i=visible[row];auto& game=model.games[i];ImGui::PushID(game.id.c_str());float rowY=ImGui::GetCursorPosY();auto pos=ImGui::GetCursorScreenPos();float rowWidth=ImGui::GetContentRegionAvail().x;
   auto draw=ImGui::GetWindowDrawList();ImVec2 rowEnd{pos.x+rowWidth,pos.y+52*dpi};
   bool hover=ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(pos,rowEnd);
   if(model.selected==i||hover)draw->AddRectFilled(pos,rowEnd,ImGui::GetColorU32(hover?ImGuiCol_HeaderHovered:ImGuiCol_Header),5*dpi);
   ImGui::SetCursorScreenPos({pos.x+2*dpi,pos.y});if(FavoriteStar(game.favorite,dpi))model.ToggleFavorite(i);
   ImGui::SetCursorScreenPos({pos.x+34*dpi,pos.y});ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
   if(ImGui::Selectable("##Game",model.selected==i,ImGuiSelectableFlags_NoPadWithHalfSpacing,{std::max(1.f,rowWidth-34*dpi),52*dpi}))model.TrySelectLibraryGame(i);ImGui::PopStyleColor(3);
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
  if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(collapsed?Neurotic::UiLiteral("desktop.hubshell.show_game_list_6de1f3bd", "Show game list"):Neurotic::UiLiteral("desktop.hubshell.hide_game_list_05220953", "Hide game list"));ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("GameDetails",ImVec2(0,columns?panelHeight:std::max(1.f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders,FixedPane);ImGui::PopStyleVar();
  auto selectedBounds=ImGui::GetCurrentWindow()->OuterRectClipped;selectedPos=selectedBounds.Min;selectedSize=selectedBounds.GetSize();selectedVisible=selectedSize.x>0&&selectedSize.y>0;
  if(model.selected<0||model.selected>=(int)model.games.size()){
   ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.select_a_game_to_install_neurotic_or_remove_its__fba2d7de", "Select a game to install NeuRotic or remove its recorded files."));ImGui::Spacing();
   ImGui::Separator();ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.1_select_your_game_91582b72", "1  Select your game"),Neurotic::UiLiteral("desktop.hubshell.choose_its_executable_unsupported_in_game_archit_b867da9b", "Choose its executable. Unsupported in-game architectures can use NR Anything."));ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.2_install_neurotic_5b335339", "2  Install NeuRotic"),Neurotic::UiLiteral("desktop.hubshell.choose_install_and_resolve_unfamiliar_files_or_a_72b3921e", "Choose Install and resolve unfamiliar files or anti-cheat warnings."));ImGui::Spacing();Description(Neurotic::UiLiteral("desktop.hubshell.3_start_your_game_2fdf1e62", "3  Start your game"),Neurotic::UiLiteral("desktop.hubshell.launch_normally_or_use_start_game_db4cbc13", "Launch normally or use Start game."));
  }else{
   auto& game=model.games[model.selected];ImGui::PushID(game.id.c_str());ImGui::BeginDisabled(busy&&!model.BackgroundInspectionActive()&&!model.InstallQueueBusy());
   const bool preparedX86=game.target.bitness==32;
   bool installed=!model.inspection.is_null()&&model.inspection["state"].is_object()&&(model.inspection["state"].value("status","")=="Installed"||model.inspection["state"].value("status","")=="Partial");
   bool antiCheat=model.inspection.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))&&model.inspection[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].value("acknowledgementRequired",false);
   const auto origin=ImGui::GetCursorScreenPos();const auto room=ImGui::GetContentRegionAvail();
   const bool stacked=room.x<470*dpi;
   const float summaryWidth=stacked?room.x:235*dpi;
   const float summaryHeight=stacked?std::min(room.y*.4f,260*dpi):room.y;
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,12*dpi});
   ImGui::BeginChild("SelectedGameHeader",{summaryWidth,summaryHeight},ImGuiChildFlags_AlwaysUseWindowPadding);
   ImGui::PopStyleVar();
   const auto cover=model.artwork?model.artwork->Get(game,"cover",model.onlineArtwork):ArtworkView{};
   // Fit only to the pane and UI scale. Game-specific notices live on the
   // right, so they cannot alter the common art area or action positions.
   const float artRoom=std::max(105*dpi,ImGui::GetContentRegionAvail().y-SummaryFooterHeight(ImGui::GetContentRegionAvail().x,dpi));
   const float artFit=std::min({1.f,ImGui::GetContentRegionAvail().x/(210*dpi),artRoom/(315*dpi)});
   const ImVec2 coverSize{210*dpi*artFit,315*dpi*artFit};
   auto coverPos=ImGui::GetCursorScreenPos();coverPos.x+=(ImGui::GetContentRegionAvail().x-coverSize.x)*.5f;
   PortraitCover(model,game,cover,dpi,coverPos,coverSize);ImGui::Dummy({0,coverSize.y});ImGui::Spacing();
   const bool fallbackInstalled=!model.dumbfireInstalledTarget.empty()&&model.dumbfireInstalledTarget==game.target.path;
   const auto reminder=model.nrModelReminder.Evaluate(model.preflight,model.readiness.busy,std::filesystem::path(Wide(game.target.path)),installed||fallbackInstalled,GetTickCount64(),model.inspection);
   ImGui::PushFont(nullptr,20);const auto titlePos=ImGui::GetCursorScreenPos();
   const float titleWidth=std::max(1.f,ImGui::GetContentRegionAvail().x-(reminder.missingGame?20*dpi:0));
   SelectedGameTitle(game.title,titleWidth);ImGui::PopFont();
   if(reminder.missingGame){
    const ImVec2 center{titlePos.x+titleWidth+10*dpi,titlePos.y+10*dpi};
    const float pulse=model.reducedMotion?1.f:.28f+.72f*(.5f+.5f*std::sin(float(ImGui::GetTime())*5.02655f));
    ImGui::GetWindowDrawList()->AddCircleFilled(center,4.5f*dpi,ImGui::GetColorU32(ImVec4(1,.04f,.07f,pulse)));
    if(Neurotic::RegionHelpHovered(ImGui::GetID("##MissingNrReminderHelp"),{center.x-9*dpi,center.y-10*dpi},{center.x+9*dpi,center.y+10*dpi})){ImGui::BeginTooltip();ImGui::PushTextWrapPos(410*dpi);ImGui::TextUnformatted(reminder.reason.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
   }
   ImGui::PushFont(nullptr,14);
   const auto sourceAt=ImGui::GetCursorScreenPos();const float sourceWidth=std::min(ImGui::CalcTextSize(game.store.c_str()).x,ImGui::GetContentRegionAvail().x*.35f);
   ImGui::Dummy({sourceWidth,ImGui::GetTextLineHeight()});
   ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
   ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),sourceAt,{sourceAt.x+sourceWidth,sourceAt.y+ImGui::GetTextLineHeight()},sourceAt.x+sourceWidth,game.store.c_str(),nullptr,nullptr);ImGui::PopStyleColor();
   if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",game.store.c_str());ImGui::SameLine();
   const bool hasState=model.inspection.is_object()&&model.inspection.contains("state");
   const bool knownState=hasState&&(model.inspection["state"].is_null()||(model.inspection["state"].is_object()&&model.inspection["state"].contains("status")&&model.inspection["state"]["status"].is_string()&&!model.inspection["state"]["status"].get_ref<const std::string&>().empty()));
   ui::GameInstallationStatus(knownState&&model.inspection["state"].is_object()?model.inspection["state"].value("status",std::string{}):std::string{},model.inspectionCached,model.light,knownState,true,model.SelectedInspectionFailure());
   ImGui::PopFont();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
   const float summaryActionWidth=ImGui::GetContentRegionAvail().x;
   StartGameButton(model,summaryActionWidth,dpi);
   if(SummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_game_folder_9d093510","Open game folder"),summaryActionWidth,dpi,nullptr,7))model.OpenSelectedGameFolder();
   if(SummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_screenshots_6b9d871c","Open Screenshots"),summaryActionWidth,dpi))model.OpenSelectedGameScreenshots();
   ImGui::BeginDisabled(!model.CanOpenSelectedIni());if(SummaryAction(Neurotic::UiLiteral("desktop.hubshell.open_ini_file_b892f0d6","Open INI File"),summaryActionWidth,dpi))model.OpenSelectedIni();ImGui::EndDisabled();
   ImGui::Separator();
   const auto secondary=MeasureSummarySecondary(summaryActionWidth,dpi);
   ImGui::BeginDisabled(busy);if(SummaryAction(Neurotic::UiLiteral("desktop.hubshell.refresh_d284aefe","Refresh"),secondary.refreshWidth,dpi,"##RefreshSelectedGame",8))model.RefreshSelected();ImGui::EndDisabled();
   if(!secondary.stacked)ImGui::SameLine();const auto* visibility=game.hidden?Neurotic::UiLiteral("desktop.library.show_game","Show"):Neurotic::UiLiteral("desktop.library.hide_game","Hide");
   if(SummaryAction(visibility,secondary.visibilityWidth,dpi,"##GameVisibility"))model.ToggleHidden(model.selected);
   if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(game.hidden?Neurotic::UiLiteral("desktop.hubshell.show_game_moves_out_of_hidden_when_you_leave_thi_1154cf96","Show game — moves out of Hidden when you leave this selection"):Neurotic::UiLiteral("desktop.hubshell.hide_game_stays_selected_until_you_leave_then_ap_3d46c9f9","Hide game — stays selected until you leave, then appears only in Hidden"));
   ImGui::EndChild();
   if(stacked)ImGui::SetCursorScreenPos({origin.x,origin.y+summaryHeight+ImGui::GetStyle().ItemSpacing.y});
   else{
    ImGui::GetWindowDrawList()->AddLine({origin.x+summaryWidth,origin.y},{origin.x+summaryWidth,origin.y+room.y},ImGui::GetColorU32(ImGuiCol_Border),dpi);
    ImGui::SetCursorScreenPos({origin.x+summaryWidth,origin.y});
   }
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*dpi,12*dpi});
   ImGui::BeginChild("SelectedGameDetails",{0,stacked?std::max(1.f,room.y-summaryHeight-ImGui::GetStyle().ItemSpacing.y):room.y},ImGuiChildFlags_AlwaysUseWindowPadding,FixedPane);ImGui::PopStyleVar();
   const bool compactWorkspace=ImGui::GetContentRegionAvail().y<SavedSettingsWorkspaceHeight()+ImGui::GetFrameHeightWithSpacing()*3;
   // One native owner at every size. Normal fields reserve the review's space;
   // compact natural fields and review share this discoverable scroll area.
   const float scrollHeight=std::max(1.f,ImGui::GetContentRegionAvail().y);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("SelectedGameWorkspace",{0,scrollHeight},ImGuiChildFlags_AlwaysUseWindowPadding);ImGui::PopStyleVar();bool savedReview=false;
   // An inherited scrollbar narrows this actual rendering owner until its
   // content settles. Measure here so reservation and rendering agree.
   const float reviewReserve=compactWorkspace?0.f:SavedSettingsReviewHeight()+ImGui::GetStyle().ItemSpacing.y;
   if(ImGui::BeginTabBar("SelectedGameTabs",ImGuiTabBarFlags_FittingPolicyScroll)){
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.installation_5a51813b", "Installation"))){
   
     const auto choiceLabel=Neurotic::UiLiteral("desktop.hubshell.executable_installation_923395f4", "Executable / installation");
     const auto installationScope=ImGui::GetID("");
     ImGui::PushOverrideID(installationScope);
     const auto changeLabel=game.target.suitable?Neurotic::UiLiteral("desktop.hubshell.change_executable_8d6c615c", "Change executable"):Neurotic::UiLiteral("desktop.hubshell.browse_for_executable_34c75d65", "Browse for executable");
     const float rowWidth=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemInnerSpacing.x;
     const auto emptyChoice=Neurotic::Translate(Neurotic::UiLiteral("desktop.hubshell.choose_executable_dcbba520", "Choose executable"));
     const auto changeAction=MeasureSavedReviewAction(changeLabel,rowWidth);
     const float choiceMinimum=MeasureSavedChoiceWidth(rowWidth,ImGui::CalcTextSize(emptyChoice.c_str()).x);
     const bool inlineAction=rowWidth>=choiceMinimum+gap+changeAction.width;
     const float left=ImGui::GetCursorPosX(),labelWidth=(std::min)(ImGui::GetFontSize()*14.f,rowWidth*.25f);
     const bool inlineLabel=inlineAction&&rowWidth>=labelWidth+2*gap+changeAction.width+choiceMinimum;
     ImGui::AlignTextToFramePadding();if(inlineLabel)ImGui::PushTextWrapPos(left+labelWidth);
     ImGui::TextWrapped("%s",Neurotic::Translate(choiceLabel).c_str());
     if(inlineLabel){ImGui::PopTextWrapPos();ImGui::SameLine(left+labelWidth+gap);}
     const float choiceWidth=(std::max)(1.f,ImGui::GetContentRegionAvail().x-(inlineAction?changeAction.width+gap:0.f));
     const auto preview=game.target.path.empty()?emptyChoice:Utf8(std::filesystem::path(Wide(game.target.path)).filename().wstring())+" · "+std::to_string(game.target.bitness)+"-bit";
     std::string choicePath;
     ImGui::BeginDisabled((model.installer.busy&&!model.InstallQueueWorkerActive())||model.BulkUninstallBusy());
     ImGui::BeginDisabled(game.executableChoices.empty());ImGui::PushOverrideID(ImGui::GetID(choiceLabel));ImGui::SetNextItemWidth(choiceWidth);
     if(ImGui::BeginCombo("",preview.c_str())){for(auto& choice:game.executableChoices){const bool active=GamePathKey(choice.path)==GamePathKey(game.target.path);auto label=choice.store+" · "+std::to_string(choice.bitness)+Neurotic::UiLiteral("desktop.hubshell.bit_786bcde1", "-bit · ")+choice.path;if(ImGui::Selectable(label.c_str(),active))choicePath=choice.path;if(active)ImGui::SetItemDefaultFocus();if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.hubshell.s_installation_s_6d194f69", "%s\nInstallation: %s"),Neurotic::Translate(choice.path.c_str()).c_str(),Neurotic::Translate(choice.root.c_str()).c_str());}ImGui::EndCombo();}
     ImGui::PopID();ImGui::EndDisabled();RequiredOutline(!game.target.suitable,dpi,choiceWidth);
     if(!game.target.path.empty()&&Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s\n%s",game.target.path.c_str(),Neurotic::Translate(game.target.reason.c_str()).c_str());
     if(inlineAction)ImGui::SameLine(0,gap);if(SavedReviewButton(changeLabel,changeAction)){auto path=PickExecutable(game.root);if(!path.empty())model.SetExecutable(model.selected,path);}RequiredOutline(!game.target.suitable,dpi);
     ImGui::EndDisabled();if(!choicePath.empty())model.SetExecutable(model.selected,Wide(choicePath));
     ImGui::BeginDisabled(!game.target.suitable);ProxyPicker(model,dpi);ImGui::EndDisabled();
   if(!game.target.suitable)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.required_choose_the_game_executable_to_unlock_it_4281569b", "Required: choose the game executable to unlock its controls."));
   if(game.target.path.empty()&&!game.target.reason.empty())ImGui::TextWrapped("%s",Neurotic::Translate(game.target.reason.c_str()).c_str());
   if(model.inspectionCached)ImGui::TextWrapped("%s",Neurotic::Translate(model.inspectionRefreshing?Neurotic::UiLiteral("desktop.hubshell.checking_current_files_previous_status_shown_e6aa2abb", "Checking current files; previous status shown."):Neurotic::UiLiteral("desktop.hubshell.previous_status_shown_refresh_to_check_current_f_411cfcd2", "Previous status shown. Refresh to check current files.")).c_str());
   const auto selectedQueue=model.SelectedQueuedInstall();
   if((selectedQueue&&selectedQueue->phase==QueuedInstall::Phase::NeedsAttention)||(GameIssueMatches(model.operationIssue,game)&&!model.AntiCheatRiskAcceptedFor(game))){
    ImGui::TextColored(ui::StatusColor(ui::ReadyState::Attention,model.light),Neurotic::UiLiteral("desktop.hubshell.needs_attention_b2215ec3", "Needs attention"));ImGui::SameLine();
    if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.resolve_1161843d", "Resolve"))){if(selectedQueue)model.ReviewQueuedInstall(selectedQueue->id);else{model.reviewedInstallId=0;model.showIssue=true;}}
   }

     if(preparedX86)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installation.architecture_warning","32-bit executable. This 64-bit package cannot provide in-game integration. Use NR Anything until a compatible integration is available."));
     else if(game.target.suitable&&game.target.machine!=IMAGE_FILE_MACHINE_AMD64)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.installation.other_architecture_warning","Executable architecture differs from this package. If compatible in-game integration is unavailable, use NR Anything."));
     if(antiCheat)ImGui::TextWrapped("%s",Neurotic::Translate(model.RememberedRiskFor(game)?Neurotic::UiLiteral("desktop.installation.risk_remembered","Anti-cheat: choice remembered"):model.AntiCheatRiskAcceptedFor(game)?Neurotic::UiLiteral("desktop.installation.risk_accepted","Anti-cheat: choice accepted"):Neurotic::UiLiteral("desktop.installation.anti_cheat_acknowledgment","Anti-cheat: acknowledgment needed")).c_str());
     if(ShowReFrameworkNote(game))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.some_capcom_games_may_require_reframework_to_wor_0c1bbc45","Some Capcom games may require REFramework to work correctly."));
     ImGui::PopID();ImGui::Spacing();
     ImGui::PushOverrideID(installationScope);
   // Installation guidance reserves wrapped status space within its package
   // group. Saved settings retain their full viewport on other tabs.
   const auto gameModelNotice=Neurotic::UiLiteral("desktop.shared-files.game-missing", "Game folder: nvngx_dlssnr.dll missing");
   const auto appModelNotice=Neurotic::UiLiteral("desktop.shared-files.anything-missing", "NR Anything: model setup required");
   const float guideWidth=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
   const auto guideHeight=[&](const char* text){const auto translated=Neurotic::Translate(text);return ImGui::CalcTextSize(translated.c_str(),nullptr,false,guideWidth).y;};
   const float noticeHeight=(std::max)(ImGui::GetTextLineHeight(),guideHeight(gameModelNotice)+guideHeight(appModelNotice)+ImGui::GetStyle().ItemSpacing.y);
   ImGui::BeginChild("SelectedGameFileGuidance",{0,noticeHeight},ImGuiChildFlags_None,FixedPane|ImGuiWindowFlags_NoBackground);
   if(reminder.missingGame||reminder.missingApp){
    if(reminder.missingGame){ImGui::PushStyleColor(ImGuiCol_Text,ui::StatusColor(ui::ReadyState::Error,model.light));ImGui::TextWrapped("%s",Neurotic::Translate(gameModelNotice).c_str());ImGui::PopStyleColor();}
    if(reminder.missingApp){ImGui::PushStyleColor(ImGuiCol_Text,ui::StatusColor(ui::ReadyState::Attention,model.light));ImGui::TextWrapped("%s",Neurotic::Translate(appModelNotice).c_str());ImGui::PopStyleColor();}
   }
   ImGui::EndChild();ImGui::Spacing();
     ImGui::BeginDisabled(!game.target.suitable);
     NrDllProfilePicker(model,game);ImGui::Spacing();
     ImGui::BeginDisabled(model.installer.busy&&!model.InstallQueueWorkerActive()&&!model.BackgroundInspectionActive());
     bool installInspector=model.InspectorInstallFor(game);
    if(ui::Toggle(Neurotic::UiLiteral("desktop.installation.install_inspector","Install Inspector"),&installInspector))model.SetInspectorInstallFor(game,installInspector);
    if(Neurotic::ItemHelpHovered()){
     ImGui::BeginTooltip();
     const float wrapWidth=std::max(1.f,std::min(360*dpi,ImGui::GetMainViewport()->WorkSize.x-2*ImGui::GetStyle().WindowPadding.x-16*dpi));
     ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrapWidth);
     const auto help=Neurotic::Translate(Neurotic::UiLiteral("desktop.installation.inspector_help","The inspector is completely optional and incomplete. It is a work in progress feature."));
     ImGui::TextUnformatted(help.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    ui::Toggle(Neurotic::UiLiteral("desktop.hubshell.fresh_install_5e42e4f8", "Fresh Install"),&model.freshInstall);
     if(Neurotic::ItemHelpHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.installation.fresh_help","Use package defaults. An ordinary install preserves your saved settings."));
     const float actionWidth=std::max(1.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f);
     ImGui::BeginDisabled(model.SelectedProxyName().empty());
     if(OperationAction(model,Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"),Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"),{.02f,.38f,.46f,1},actionWidth,dpi))model.Plan(Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install"));ImGui::EndDisabled();ImGui::EndDisabled();
     ImGui::SameLine();ImGui::BeginDisabled(busy);if(ui::ActionButton(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"),{.65f,.16f,.21f,1},{actionWidth,42*dpi}))model.Plan(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"));ImGui::EndDisabled();
     ImGui::Spacing();
     ImGui::EndDisabled();ImGui::PopID();ImGui::EndTabItem();
    }
    ImGui::BeginDisabled(!game.target.suitable||model.inspection.is_null());
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.ini_settings_f791a0c5", "INI Settings"))){savedReview=true;RenderSavedGameCompatibility(model,true,compactWorkspace,reviewReserve,true);ImGui::EndTabItem();}
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.game_settings_76603cce", "Game Settings"))){savedReview=true;SettingsEditor(model,compactWorkspace,reviewReserve);ImGui::EndTabItem();}
    ImGui::EndDisabled();
    if(ImGui::BeginTabItem(Neurotic::UiLiteral("desktop.hubshell.diagnostics_1be508a4", "Diagnostics"))){ImGui::Spacing();ImGui::BeginDisabled(busy);if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.refresh_details_0ab28849", "Refresh details")))model.RefreshSelected();ImGui::EndDisabled();ImGui::Spacing();ImGui::BeginDisabled(!model.CanCollectGameDiagnostics()||model.gameDiagnosticsBusy);if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.quick_log_preview_0640cf60", "Quick log preview")))model.CollectGameDiagnostics();ImGui::EndDisabled();ImGui::Spacing();ImGui::BeginDisabled(!game.target.suitable||game.target.path.empty()||model.DiagnosticExportRunning());if(ui::Button(model.DiagnosticExportRunning()?Neurotic::UiLiteral("desktop.hubshell.exporting_diagnostic_bundle_da4d29f0", "Exporting diagnostic bundle..."):Neurotic::UiLiteral("desktop.hubshell.export_complete_diagnostic_bundle_7eed7a46", "Export complete diagnostic bundle"))){auto destination=PickDirectory(nh::Wide(Neurotic::UiMessage("desktop.hubshell.neurotic_choose_diagnostic_zip_destination_d606f5de", "NeuRotic — choose diagnostic ZIP destination")));if(!destination.empty())model.ExportSelectedDiagnostics(destination);}ImGui::EndDisabled();ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.collect_logs_configuration_module_inventory_and__d05e23f4", "Collect logs, configuration, module inventory and capability evidence into a ZIP for review."));if(!model.bundleStatus.empty())ImGui::TextWrapped("%s",Neurotic::Translate(model.bundleStatus.c_str()).c_str());if(!model.bundlePath.empty()){if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.open_bundle_folder_78c51e18", "Open bundle folder")))model.OpenBundleFolder();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.copy_zip_path_1ad418dc", "Copy ZIP path")))ImGui::SetClipboardText(model.bundlePath.c_str());}if(!installed)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.hubshell.the_quick_log_preview_requires_recorded_game_log_19f75b32", "The quick log preview requires recorded game logs."));if(!model.gameDiagnosticsStatus.empty())ImGui::TextWrapped("%s",Neurotic::Translate(model.gameDiagnosticsStatus.c_str()).c_str());if(!model.gameDiagnosticsText.empty())LogPanel(model.gameDiagnosticsText);ImGui::EndTabItem();}
    ImGui::EndTabBar();
   }
   if(savedReview&&!model.settings.empty())RenderSavedSettingsReview(model,ImGui::GetCurrentWindow()->ParentWindow->ID);
   FinishSavedSettingsFieldsLayout();
   ImGui::EndChild();
   ImGui::EndChild();
   ImGui::EndDisabled();ImGui::PopID();
  }
  ImGui::EndChild();
  if(busy&&!model.BackgroundInspectionActive()&&!model.InstallQueueBusy()&&selectedVisible){
   auto resume=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(selectedPos);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("SelectedGameLoadingShade",selectedSize,ImGuiChildFlags_None,FixedPane|ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();
   auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(selectedPos,{selectedPos.x+selectedSize.x,selectedPos.y+selectedSize.y},IM_COL32(105,110,120,165),8*dpi);
   ImVec2 box{std::min(300*dpi,selectedSize.x-16*dpi),std::min(100*dpi,selectedSize.y-8*dpi)},at{selectedPos.x+(selectedSize.x-box.x)*.5f,selectedPos.y+(selectedSize.y-box.y)*.5f};
   draw->AddRectFilled(at,{at.x+box.x,at.y+box.y},ImGui::GetColorU32(ImGuiCol_PopupBg),8*dpi);auto text=ImGui::CalcTextSize("Loading");float groupWidth=text.x+34*dpi,groupX=at.x+(box.x-groupWidth)*.5f;
   ui::Glyph(draw,6,{groupX,at.y+(box.y-22*dpi)*.5f},22*dpi,ImGui::GetColorU32(ImGuiCol_Text),true);draw->AddText({groupX+34*dpi,at.y+(box.y-text.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),"Loading");
   if(!model.installer.readOnlyInstaller&&!model.InstallQueueWorkerActive())OperationProgress(model.installer,selectedPos,selectedSize,dpi);
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
    auto path=UserRoot();std::error_code error;std::filesystem::create_directories(path,error);if(!error)ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);else model.message=Neurotic::UiMessage("desktop.hubshell.local_logs_could_not_be_opened_22e6deae", "Local logs could not be opened.");
   }ImGui::Spacing();if(!model.lastResult.is_null())LogPanel(model.lastResult.dump(2));else ImGui::TextDisabled(Neurotic::UiLiteral("desktop.hubshell.no_operation_has_been_attempted_in_this_session_7c439d17", "No operation has been attempted in this session."));}
  if(pageBody)ImGui::EndChild();
 }
 ImGui::EndDisabled();ImGui::PopStyleVar();ImGui::EndChild();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
 float supportWidth=ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.hubshell.enjoying_neurotic_send_ko_fi_c8a05712", "Enjoying NeuRotic? Send Ko-Fi")).x+ImGui::GetStyle().FramePadding.x*2;float footerWidth=std::max(100.f,ImGui::GetContentRegionAvail().x-supportWidth-20*dpi);ImGui::BeginChild(Neurotic::UiLiteral("desktop.hubshell.footerstatus_363908dc", "FooterStatus"),{footerWidth,32*dpi},0,FixedPane);auto statusPos=ImGui::GetCursorScreenPos();float statusRight=statusPos.x+ImGui::GetContentRegionAvail().x;ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),statusPos,{statusRight,statusPos.y+ImGui::GetTextLineHeight()},statusRight,model.message.c_str(),nullptr,nullptr);if(!model.message.empty()&&Neurotic::RegionHelpHovered(ImGui::GetID("##FooterStatusHelp"),ImGui::GetCurrentWindow()->InnerRect.Min,ImGui::GetCurrentWindow()->InnerRect.Max)){ImGui::BeginTooltip();ImGui::PushTextWrapPos(480*dpi);ImGui::TextUnformatted(model.message.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}ImGui::EndChild();ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.enjoying_neurotic_send_ko_fi_c8a05712", "Enjoying NeuRotic? Send Ko-Fi")))ShellExecuteW(nullptr,L"open",L"https://ko-fi.com/espiownage",nullptr,nullptr,SW_SHOWNORMAL);
 ActivityOverlay(model,dpi);DrawHubModals(model,dpi);
 if(!model.showIssue&&!model.installer.busy)model.installer.ClearAntiCheatApproval();
 ImGui::End();ImGui::PopFont();
 // Every box finishes drawing with this frame's palette. The next frame
 // starts the existing water compositor from a uniformly old-theme image.
 if(changeTheme){model.light=nextLight;model.Save();}
}
}
