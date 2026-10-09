#include <menu/Localization.h>
#include "LanguageManagerView.h"
#include "../../../OptiScaler/menu/localization/FontNoticesView.h"
#include "ui/SleekWidgets.h"
#include "../../../OptiScaler/menu/localization/LanguageRuntime.h"
#include "../../../OptiScaler/menu/localization/LanguageFiles.h"
#include "../../../OptiScaler/menu/WindowSectionHeader.h"
namespace nh {
using namespace Neurotic::Localization;
namespace{LanguageManagerView* activeManager=nullptr;}
void SetLanguageManager(LanguageManagerView* value){activeManager=value;}void RenderLanguageManager(){if(activeManager)activeManager->Draw();}
LanguageManagerView::LanguageManagerView(HWND parent,ID3D11Device* device,ID3D11DeviceContext* context,std::filesystem::path root):store(std::move(root)),editor(parent,device,context,store),owner(parent){
 // Hub startup already loaded this root before preparing localized fonts.
 if(store.Root().lexically_normal()==SharedLanguageRoot().lexically_normal()){
  LoadSharedCatalogOnce();selected=SelectedPackId();
 }else{
  auto active=store.ActivePack();selected=active.id;SetSharedCatalog(std::move(active));
 }
 if(selected=="english")selected="en";
}
void LanguageManagerView::Draw(){
 Neurotic::Sleek::WindowSectionHeader(Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language"));
 const auto active=SelectedPackId();std::string preview=SelectedLanguageName()+" · "+LocaleDisplayCode(SelectedLocale());std::string requested;
 ImGui::SetNextItemWidth(std::min(400.f,ImGui::GetContentRegionAvail().x));
 if(ImGui::BeginCombo("##GlobalLanguage",preview.c_str())){
  EnsurePacks();
  const auto englishChoice=Neurotic::Translate(Neurotic::UiLiteral("desktop.hubshell.english_620735a7","English"))+" · EN";
  if(ImGui::Selectable(englishChoice.c_str(),active=="en"))requested="en";
  for(const auto& row:packs){ImGui::PushID(row.id.c_str());if(ImGui::Selectable((row.name+" · "+LocaleDisplayCode(row.locale)).c_str(),active==row.id))requested=row.id;ImGui::PopID();}
  ImGui::EndCombo();
 }
 if(!requested.empty()){auto result=store.Apply(requested);status=result.message;if(result.success){selected=requested;QueueSharedCatalog(store.ActivePack());}}
 if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.manage_languages_49037c03", "Manage Languages")))manage=!manage;
 if(manage){EnsurePacks();if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.add_language_cf4d2cd4", "Add Language"))){adding=true;ImGui::OpenPopup(Neurotic::UiLiteral("desktop.languagemanagerview.add_language_cf4d2cd4", "Add Language"));}ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.import_833627da", "Import"))){auto path=ChooseLanguageFile(owner,false);if(!path.empty())try{importPreview=ValidatePack(ReadLanguageFile(path),CanonicalEnglish());previewPending=true;ImGui::OpenPopup(Neurotic::UiLiteral("desktop.languagemanagerview.import_language_pack_ae3a74b6", "Import language pack"));}catch(const std::exception& e){status=e.what();}}
  if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.export_english_template_a6966f93", "Export English Template"))){auto path=ChooseLanguageFile(owner,true,L"English-template.nrlang");if(!path.empty())status=store.ExportEnglishTemplate(path).message;}
  ImGui::BeginDisabled(selected=="en");if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.translation_editor_79e1b38f", "Translation Editor"))){if(!editor.Open(selected))status=Neurotic::UiMessage("desktop.languagemanagerview.translation_editor_could_not_open_this_pack_fini_2d6fda48", "Translation Editor could not open this pack. Finish the current draft first.");}ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.export_selected_pack_781b0326", "Export selected pack"))){auto path=ChooseLanguageFile(owner,true);if(!path.empty())status=store.ExportPack(selected,path).message;}ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.remove_91f637d2", "Remove")))ImGui::OpenPopup(Neurotic::UiLiteral("desktop.languagemanagerview.remove_language_pack_b6e097d8", "Remove language pack?"));ImGui::EndDisabled();
  for(const auto& row:packs)if(row.id==selected){ImGui::TextWrapped("%s · %s · %s",Neurotic::Translate(row.author.c_str()).c_str(),Neurotic::Translate(row.version.c_str()).c_str(),Neurotic::Translate(row.included?Neurotic::UiLiteral("desktop.languagemanagerview.included_pack_f83dff6c", "Included pack"):Neurotic::UiLiteral("desktop.languagemanagerview.community_pack_395a5118", "Community pack")).c_str());ImGui::TextWrapped(Neurotic::UiLiteral("desktop.languages.base_coverage", "Base pack coverage — Desktop: %zu · In-game: %zu · Needs review: %zu · Invalid: %zu"),row.desktop,row.inGame,row.stale,row.invalid);}
 }
 if(ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.languagemanagerview.add_language_cf4d2cd4", "Add Language"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::InputText(Neurotic::UiLiteral("desktop.languagemanagerview.locale_05bb405d", "Locale"),locale.data(),locale.size());ImGui::InputText(Neurotic::UiLiteral("desktop.languagemanagerview.name_f51763bb", "Name"),name.data(),name.size());ImGui::TextDisabled(Neurotic::UiLiteral("desktop.languagemanagerview.examples_pl_pt_br_zh_cn_a_new_pack_starts_empty_78539ba0", "Examples: PL, PT-BR, ZH-CN. A new pack starts empty."));if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.create_810774d1", "Create"))){auto result=store.AddLanguage(locale.data(),name.data());status=result.message;if(result.success){packs=store.List();selected=result.packId;editor.Open(selected);adding=false;ImGui::CloseCurrentPopup();}}ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel"))){adding=false;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
 if(ImGui::BeginPopupModal(Neurotic::UiLiteral("desktop.languagemanagerview.import_language_pack_ae3a74b6", "Import language pack"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){if(importPreview.accepted){auto& pack=importPreview.pack;ImGui::TextWrapped("%s · %s · %s",Neurotic::Translate(pack.name.c_str()).c_str(),LocaleDisplayCode(pack.locale).c_str(),Neurotic::Translate(pack.document.value("author",std::string{}).c_str()).c_str());ImGui::TextWrapped(Neurotic::UiLiteral("desktop.languagemanagerview.usable_entries_zu_invalid_zu_obsolete_zu_c9c49072", "Usable entries: %zu · Invalid: %zu · Obsolete: %zu"),pack.entries.size(),importPreview.errors.size(),pack.obsolete.size());ImGui::TextWrapped(Neurotic::UiLiteral("desktop.languagemanagerview.import_preserves_local_edits_and_does_not_change_00b00a49", "Import preserves local edits and does not change the active language."));if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.import_833627da", "Import"))){auto result=store.ImportText(pack.document.dump());status=result.message;if(result.success){packs=store.List();selected=result.packId;ImGui::CloseCurrentPopup();}}}else ImGui::TextWrapped("%s",Neurotic::Translate((importPreview.errorCode.empty()?importPreview.error:Format(SharedText(importPreview.errorCode),importPreview.errorParameters)).c_str()).c_str());ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel")))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 const char* removeDialogTitle=Neurotic::UiLiteral("desktop.languagemanagerview.remove_language_pack_b6e097d8", "Remove language pack?");
 const float removeDialogMaxWidth=std::max(1.f,ImGui::GetMainViewport()->WorkSize.x-ImGui::GetStyle().WindowPadding.x*2);
 const float removeDialogMinWidth=std::min(removeDialogMaxWidth,ImGui::CalcTextSize(Neurotic::Translate(removeDialogTitle).c_str(),nullptr,true).x+ImGui::GetStyle().WindowPadding.x*2+ImGui::GetStyle().FramePadding.x*2);
 ImGui::SetNextWindowSizeConstraints({removeDialogMinWidth,0},{removeDialogMaxWidth,ImGui::GetMainViewport()->WorkSize.y});
 if(ImGui::BeginPopupModal(removeDialogTitle,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.languagemanagerview.remove_this_pack_drafts_and_local_edits_remain_r_ff4c5834", "Remove this pack? Drafts and local edits remain. Removing the active pack selects English."));if(ui::Button(Neurotic::UiLiteral("desktop.languagemanagerview.remove_91f637d2", "Remove"))){auto result=store.RemovePack(selected);status=result.message;if(result.success){packs=store.List();selected="en";QueueSharedCatalog(store.ActivePack());ImGui::CloseCurrentPopup();}}ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel")))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 Neurotic::FontNoticesView();
 if(!status.empty())ImGui::TextWrapped("%s",Neurotic::Translate(status.c_str()).c_str());
}
}
