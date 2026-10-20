#pragma once
#include <menu/ItemHelp.h>
#include "HubViewModel.h"
#include "SleekWidgets.h"
#include <imgui.h>
#include <commdlg.h>
#include <shellapi.h>

namespace nh {
inline std::string NrDllErrorText(NrDllErrorCode error){
 switch(error){
 case NrDllErrorCode::ReadOnly:return Neurotic::UiMessage("desktop.nrdll.read_only","This App session cannot change saved NR DLL profiles.");
 case NrDllErrorCode::InvalidName:return Neurotic::UiMessage("desktop.nrdll.invalid_name","Enter a profile name on one line. If it is too long, shorten it.");
 case NrDllErrorCode::WrongFilename:return Neurotic::UiMessage("desktop.nrdll.wrong_filename","Choose a file named nvngx_dlssnr.dll.");
 case NrDllErrorCode::MissingProfile:case NrDllErrorCode::NoDefault:return Neurotic::UiMessage("desktop.nrdll.missing_profile","This profile is unavailable. Choose another profile or manual installation.");
 case NrDllErrorCode::InUse:return Neurotic::UiMessage("desktop.nrdll.in_use","This profile or its settings are busy. Finish any queued installation or close other programs using the files, then try again.");
 case NrDllErrorCode::ChangedAsset:return Neurotic::UiMessage("desktop.nrdll.changed_asset","The saved NR DLL changed. Import it again to use the new bytes.");
 case NrDllErrorCode::InvalidPath:return Neurotic::UiMessage("desktop.nrdll.invalid_path","Choose a regular file named nvngx_dlssnr.dll that NeuRotic can read.");
 case NrDllErrorCode::UnsafePath:return Neurotic::UiMessage("desktop.nrdll.unsafe_path","NeuRotic's saved profile folder is linked or inaccessible. Restore access to App data and try again.");
 case NrDllErrorCode::CorruptMetadata:return Neurotic::UiMessage("desktop.nrdll.corrupt_metadata","Some saved profile information could not be read. Your files were preserved.");
 case NrDllErrorCode::Cancelled:return Neurotic::UiMessage("desktop.nrdll.cancelled","The operation stopped. The profile list shows the current saved files.");
 default:return Neurotic::UiMessage("desktop.nrdll.io_error","The NR DLL operation could not finish. Check that the file is accessible and try again.");
 }
}
inline std::filesystem::path PickNrDllFile(){
 wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=GetActiveWindow();
 auto filter=Wide(Neurotic::UiMessage("desktop.nrdll.file_filter","NR DLL (nvngx_dlssnr.dll)"));filter.push_back(0);filter+=L"nvngx_dlssnr.dll";filter.push_back(0);filter.push_back(0);
 const auto title=Wide(Neurotic::UiMessage("desktop.nrdll.choose_file","Choose your nvngx_dlssnr.dll"));picker.lpstrFilter=filter.c_str();picker.lpstrTitle=title.c_str();picker.lpstrFile=path;picker.nMaxFile=32768;
 picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
 return GetOpenFileNameW(&picker)?std::filesystem::path(path):std::filesystem::path{};
}
inline ui::ReadyState NrDllManagerAvailability(const HubModel& model){
 const auto manager=model.nrDllManager;if(!manager)return ui::ReadyState::Attention;
 if(manager->Busy())return ui::ReadyState::Checking;
 if(auto error=manager->Error();error&&*error!=NrDllErrorCode::Cancelled)return ui::ReadyState::Error;
 const auto& catalog=manager->Catalog();
 if(catalog.profiles.empty()||!catalog.issues.empty()||std::any_of(catalog.profiles.begin(),catalog.profiles.end(),[](const auto& p){return !p.available;}))return ui::ReadyState::Attention;
 return ui::ReadyState::Ready;
}
inline const char* NrDllManagerAvailabilityLabel(const HubModel& model){
 const auto state=NrDllManagerAvailability(model);
 if(state==ui::ReadyState::Checking)return Neurotic::UiLiteral("desktop.nrdll.loading","Loading profiles…");
 if(state==ui::ReadyState::Error||(model.nrDllManager&&(!model.nrDllManager->Catalog().profiles.empty()||!model.nrDllManager->Catalog().issues.empty())&&state!=ui::ReadyState::Ready))return Neurotic::UiLiteral("desktop.nrdll.needs_attention","Profiles need attention");
 if(state==ui::ReadyState::Ready)return Neurotic::UiLiteral("desktop.nrdll.saved_profiles","Saved profiles");
 return Neurotic::UiLiteral("desktop.nrdll.no_profiles","No saved profiles");
}
inline void OpenNrDllManager(HubModel& model){
 if(model.showIssue||model.showUninstallConfirm||model.showDataMaintenance||model.restartForMaintenance)return;
 model.showNrDllManager=true;model.nrDllEdit.clear();
 if(model.nrDllManager&&!model.nrDllManager->Busy())model.nrDllManager->Refresh();
}
inline void NrDllProfilePicker(HubModel& model,const Game& game){
 const auto choice=model.NrDllChoiceFor(game);const NrDllCatalog empty;
 const auto& catalog=model.nrDllManager?model.nrDllManager->Catalog():empty;
 std::string preview=choice.mode==NrDllChoiceMode::Manual?Neurotic::UiMessage("desktop.nrdll.manual","Manual — do not copy an NR DLL"):choice.mode==NrDllChoiceMode::Default?Neurotic::UiMessage("desktop.nrdll.default","Default"):Neurotic::UiMessage("desktop.nrdll.unavailable","Unavailable profile");
 const auto id=choice.mode==NrDllChoiceMode::Default?catalog.defaultId:choice.profileId;
 const auto found=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& p){return p.id==id;});
 const bool unknownDefault=choice.mode==NrDllChoiceMode::Default&&(NrDllDefaultIssue(catalog).has_value()||(model.nrDllManager&&!model.nrDllManager->CatalogCurrent())||(!catalog.defaultId.empty()&&found==catalog.profiles.end()));
 if(unknownDefault)preview=Neurotic::UiMessage("desktop.nrdll.default_unavailable","Default unavailable");
 else if(found!=catalog.profiles.end())preview=(choice.mode==NrDllChoiceMode::Default?preview+": ":"")+found->name+(found->available?"":" · "+Neurotic::UiMessage("desktop.nrdll.unavailable","Unavailable profile"));
 else if(choice.mode==NrDllChoiceMode::Default)preview+=" · "+Neurotic::UiMessage("desktop.nrdll.no_default","No default selected");
 ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.nrdll.selector","NR DLL"));ImGui::SameLine();ImGui::SetNextItemWidth(-1);
 const bool comboOpen=ImGui::BeginCombo("##nr-dll-choice",nullptr,ImGuiComboFlags_CustomPreview);
 if(comboOpen){
  if(ImGui::Selectable(Neurotic::UiMessage("desktop.nrdll.default","Default").c_str(),choice.mode==NrDllChoiceMode::Default))model.SetNrDllChoice(game,{});
  if(ImGui::Selectable(Neurotic::UiMessage("desktop.nrdll.manual","Manual — do not copy an NR DLL").c_str(),choice.mode==NrDllChoiceMode::Manual))model.SetNrDllChoice(game,{NrDllChoiceMode::Manual,{}});
  for(const auto& profile:catalog.profiles){ImGui::PushID(profile.id.c_str());
   // User labels are drawn as text, so ##/printf syntax is never interpreted.
   const auto start=ImGui::GetCursorScreenPos();const auto width=std::max(1.f,ImGui::GetContentRegionAvail().x);
   const auto label=profile.name+(profile.available?"":" · "+Neurotic::UiMessage("desktop.nrdll.unavailable","Unavailable profile"));
   const auto height=std::max(ImGui::GetTextLineHeight(),ImGui::CalcTextSize(label.c_str(),nullptr,false,width).y);
   if(ImGui::Selectable("##profile",choice.mode==NrDllChoiceMode::Profile&&choice.profileId==profile.id,0,{0,height}))model.SetNrDllChoice(game,{NrDllChoiceMode::Profile,profile.id});
   // Selectable owns the row extent and spacing; literal text does not move its cursor.
   ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),start,ImGui::GetColorU32(ImGuiCol_Text),label.c_str(),nullptr,width);ImGui::PopID();
  }
  ImGui::EndCombo();
 }
 const bool previewHovered=Neurotic::ItemHelpHovered();
 if(ImGui::BeginComboPreview()){ImGui::TextUnformatted(preview.c_str());ImGui::EndComboPreview();}
 if(previewHovered&&!comboOpen){ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);ImGui::TextUnformatted(preview.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
 // This explicit action is separate from ordinary Install/Update/Fresh.
 if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.add_update","Add/Update NR DLL")))model.Plan("UpdateNrDll");
 if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::UiLiteral("desktop.nrdll.add_update_help","Add or replace this game's NR DLL with the selected profile. Other installation files stay unchanged."));
}
inline void SetNrDllProfilePopupBounds(const std::string& title,float dpi){
 const auto* viewport=ImGui::GetMainViewport();const auto room=viewport->WorkSize;
 const auto width=std::max(1.f,std::min(480*dpi,room.x-32*dpi));bool workChanged=false;
 if(auto* popup=ImGui::FindWindowByName(title.c_str())){
  auto& state=popup->StateStorage;
  const auto known=popup->GetID("##nr-work-known"),x=popup->GetID("##nr-work-x"),y=popup->GetID("##nr-work-y"),w=popup->GetID("##nr-work-w"),h=popup->GetID("##nr-work-h"),requested=popup->GetID("##nr-work-width");
  workChanged=state.GetBool(known,false)&&(state.GetFloat(x)!=viewport->WorkPos.x||state.GetFloat(y)!=viewport->WorkPos.y||state.GetFloat(w)!=room.x||state.GetFloat(h)!=room.y||state.GetFloat(requested)!=width);
  state.SetBool(known,true);state.SetFloat(x,viewport->WorkPos.x);state.SetFloat(y,viewport->WorkPos.y);state.SetFloat(w,room.x);state.SetFloat(h,room.y);state.SetFloat(requested,width);
 }
 // Reanchor only on appearance or real work-area/scale changes; retain native dragging otherwise.
 ImGui::SetNextWindowPos(viewport->GetWorkCenter(),workChanged?ImGuiCond_Always:ImGuiCond_Appearing,{.5f,.5f});
 ImGui::SetNextWindowSize({width,0},ImGuiCond_Always);
 ImGui::SetNextWindowSizeConstraints({0,0},{std::max(1.f,room.x-32*dpi),std::max(1.f,room.y-32*dpi)});
}
inline void DrawNrDllProfileEditor(HubModel& model,const std::shared_ptr<NrDllManager>& manager,float dpi){
 const bool editing=model.nrDllEdit=="import"||model.nrDllEdit=="rename";
 const auto title=Neurotic::UiMessage("desktop.nrdll.profile_editor","Save NR DLL profile")+"###NrDllProfileEditor";
 if(editing&&!ImGui::IsPopupOpen(title.c_str()))ImGui::OpenPopup(title.c_str());
 SetNrDllProfilePopupBounds(title,dpi);
 bool open=true;
 auto cancel=[&]{model.nrDllEdit.clear();model.nrDllName.fill(0);model.nrDllImportPath.clear();};
 if(!ImGui::BeginPopupModal(title.c_str(),&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){if(!open&&editing)cancel();return;}
 if(!editing){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
 if(model.nrDllEdit=="import"){
  ImGui::TextUnformatted(Utf8(model.nrDllImportPath.filename().wstring()).c_str());
  if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Utf8(model.nrDllImportPath.wstring()).c_str());
 }
 ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.nrdll.profile_name","Profile name"));
 if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
 ImGui::SetNextItemWidth(-1);ImGui::InputText("##nr-profile-name",model.nrDllName.data(),model.nrDllName.size());
 if(auto error=manager->Error())ImGui::TextWrapped("%s",NrDllErrorText(*error).c_str());
 const std::string_view name(model.nrDllName.data());const bool named=name.find_first_not_of(" \t\r\n")!=std::string_view::npos;
 ImGui::BeginDisabled(!named||manager->Busy()||!manager->Writable());
 if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.save_profile","Save profile"))){
  const bool started=model.nrDllEdit=="import"?manager->Import(model.nrDllImportPath,model.nrDllName.data()):manager->Rename(model.nrDllSelectedId,model.nrDllName.data());
  if(started){model.nrDllEdit.clear();ImGui::CloseCurrentPopup();}
 }
 ImGui::EndDisabled();ImGui::SameLine();
 if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908","Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape,false)){cancel();ImGui::CloseCurrentPopup();}
 ImGui::EndPopup();
}
inline void DrawNrDllRemovalConfirm(HubModel& model,const std::shared_ptr<NrDllManager>& manager,float dpi){
 const bool removing=model.nrDllEdit=="remove";
 const auto title=Neurotic::UiMessage("desktop.nrdll.confirm_remove","Confirm removal")+"###NrDllProfileRemoval";
 if(removing&&!ImGui::IsPopupOpen(title.c_str()))ImGui::OpenPopup(title.c_str());
 SetNrDllProfilePopupBounds(title,dpi);
 bool open=true;
 if(!ImGui::BeginPopupModal(title.c_str(),&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){if(!open&&removing)model.nrDllEdit.clear();return;}
 if(!removing){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
 ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.remove_confirm","Remove this saved profile? Installed game DLLs and the original download stay unchanged. Games with this explicit choice will show it as unavailable.").c_str());
 ImGui::BeginDisabled(manager->Busy()||!manager->Writable());
 if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.confirm_remove","Confirm removal"))&&manager->Remove(model.nrDllSelectedId)){model.nrDllEdit.clear();ImGui::CloseCurrentPopup();}
 ImGui::EndDisabled();
 if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.cancel_edit","Cancel edit"))||ImGui::IsKeyPressed(ImGuiKey_Escape,false)){model.nrDllEdit.clear();ImGui::CloseCurrentPopup();}
 if(ImGui::IsWindowAppearing())ImGui::SetItemDefaultFocus();
 ImGui::EndPopup();
}
inline void DrawNrDllManager(HubModel& model,float dpi){
 const auto title=Neurotic::UiMessage("desktop.nrdll.manager_title","NR DLL Manager")+"###NrDllManager";
 if(model.showNrDllManager)ImGui::OpenPopup(title.c_str());
 const auto work=ImGui::GetMainViewport()->WorkSize;
 const ImVec2 maximum{std::max(1.f,work.x-32*dpi),std::max(1.f,work.y-32*dpi)};
 ImGui::SetNextWindowSizeConstraints({std::min(360*dpi,maximum.x),std::min(320*dpi,maximum.y)},maximum);
 ImGui::SetNextWindowSize({std::max(1.f,std::min(640*dpi,work.x-32*dpi)),std::max(1.f,std::min(620*dpi,work.y-32*dpi))},ImGuiCond_Appearing);
 if(!ImGui::BeginPopupModal(title.c_str(),&model.showNrDllManager))return;
 const bool profileEditorAtStart=model.nrDllEdit=="import"||model.nrDllEdit=="rename"||model.nrDllEdit=="remove";
 auto manager=model.nrDllManager;
 if(!manager){ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.session_unavailable","Open a writable App session to manage NR DLLs.").c_str());}
 else{
  ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.manager_help_short","Add DLLs as named profiles. Choose a default or a profile for each game.").c_str());
  if(auto error=manager->Error())ImGui::TextWrapped("%s",NrDllErrorText(*error).c_str());
  if(model.nrDllFolderOpenFailed)ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.open_folder_failed","The saved profile folder could not be opened. Check that it is still available and try again.").c_str());
  if(NrDllDefaultIssue(manager->Catalog()))ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.default_recovery","The saved default could not be read. Your other profiles remain available. Select a profile and set it as default, explicitly clear the default, or choose manual installation.").c_str());
  if(!manager->Catalog().issues.empty())ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.catalog_issues","Some saved profiles are unavailable or have unreadable information. Your files were preserved; choose another profile or manual installation.").c_str());
  if(manager->Busy())ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.working","Working with NR DLL files…").c_str());
  const auto& catalog=manager->Catalog();
  ImGui::BeginChild("nr-profile-list",{0,std::max(60*dpi,std::min(190*dpi,ImGui::GetContentRegionAvail().y*.4f))},ImGuiChildFlags_Borders);
  if(catalog.profiles.empty())ImGui::TextWrapped("%s",Neurotic::UiMessage("desktop.nrdll.empty","No saved NR DLL profiles. Add a file or use manual installation.").c_str());
  for(const auto& profile:catalog.profiles){ImGui::PushID(profile.id.c_str());
   auto label=profile.name;if(profile.id==catalog.defaultId)label+=" · "+Neurotic::UiMessage("desktop.nrdll.default","Default");if(!profile.available)label+=" · "+Neurotic::UiMessage("desktop.nrdll.unavailable","Unavailable profile");
   const auto start=ImGui::GetCursorScreenPos();const auto width=std::max(1.f,ImGui::GetContentRegionAvail().x);
   const auto height=std::max(ImGui::GetTextLineHeight(),ImGui::CalcTextSize(label.c_str(),nullptr,false,width).y);
   if(ImGui::Selectable("##profile",model.nrDllSelectedId==profile.id,0,{0,height})){model.nrDllSelectedId=profile.id;model.nrDllEdit.clear();}
   ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),start,ImGui::GetColorU32(ImGuiCol_Text),label.c_str(),nullptr,width);ImGui::PopID();
  }ImGui::EndChild();
  const auto selected=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& p){return p.id==model.nrDllSelectedId;});
  ImGui::BeginDisabled(manager->Busy()||!manager->Writable()||model.showDataMaintenance||model.restartForMaintenance||!model.nrDllEdit.empty());
  if(ImGui::BeginTable("nr-actions",2,ImGuiTableFlags_SizingStretchSame)){
   ImGui::TableNextColumn();if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.add","Add DLL"),{-1,0})){auto path=PickNrDllFile();if(!path.empty()){model.nrDllImportPath=std::move(path);model.nrDllName.fill(0);model.nrDllEdit="import";}}
   ImGui::TableNextColumn();ImGui::BeginDisabled(selected==catalog.profiles.end());if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.rename","Rename"),{-1,0})){model.nrDllName.fill(0);std::copy_n(selected->name.data(),std::min(selected->name.size(),model.nrDllName.size()-1),model.nrDllName.data());model.nrDllEdit="rename";}ImGui::EndDisabled();
   ImGui::TableNextColumn();ImGui::BeginDisabled(selected==catalog.profiles.end()||!selected->available);if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.set_default","Set default"),{-1,0}))manager->SetDefault(selected->id);ImGui::EndDisabled();
   ImGui::TableNextColumn();ImGui::BeginDisabled(selected==catalog.profiles.end());if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.remove","Remove profile"),{-1,0}))model.nrDllEdit="remove";ImGui::EndDisabled();ImGui::EndTable();
  }
  ImGui::BeginDisabled(selected==catalog.profiles.end());
  if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.open_folder","Open selected profile folder"))){const auto folder=selected->dllPath.parent_path();model.nrDllFolderOpenFailed=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"explore",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32;}
  ImGui::EndDisabled();
  if((!catalog.defaultId.empty()||NrDllDefaultIssue(catalog).has_value())&&ui::Button(Neurotic::UiLiteral("desktop.nrdll.clear_default","Clear default")))manager->ClearDefault();
  ImGui::EndDisabled();
  if(manager->Busy()&&ui::Button(Neurotic::UiLiteral("desktop.nrdll.cancel_operation","Stop current operation")))manager->CancelCurrent();
  DrawNrDllProfileEditor(model,manager,dpi);
  DrawNrDllRemovalConfirm(model,manager,dpi);
 }
 ImGui::Separator();
 const bool escape=ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&!profileEditorAtStart;
 if(ui::Button(Neurotic::UiLiteral("desktop.nrdll.close","Close"))||escape){
  if(!model.nrDllEdit.empty())model.nrDllEdit.clear();else{model.showNrDllManager=false;ImGui::CloseCurrentPopup();}
 }
 ImGui::EndPopup();
}
}
