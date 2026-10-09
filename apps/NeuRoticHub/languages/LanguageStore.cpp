#include "../../../OptiScaler/menu/localization/LanguageRuntime.h"
#include <menu/Localization.h>
#include "LanguageStore.h"
#include "../../../OptiScaler/menu/localization/LanguageBundles.h"
#include "../../../OptiScaler/menu/localization/LanguageFiles.h"
#include <regex>
#include <set>
namespace nh {
using namespace Neurotic::Localization;
namespace {
Json Read(const std::filesystem::path& path){return Json::parse(ReadLanguageFile(path));}
LanguageResult Failure(const std::exception& error){return {false,error.what()};}
Json Empty(std::string id,std::string locale,std::string name){return {{"formatVersion",1},{"packId",id},{"locale",locale},{"language",name},{"name",name},{"author",""},{"version","1"},{"desktop",Json::array()},{"in_game",Json::array()}};}
Json WithMetadata(Json document,const Json& edits){
 if(!edits.is_object())throw std::runtime_error(Format(SharedText("desktop.languageerror.87a532b4348e"),{}));
 for(auto it=edits.begin();it!=edits.end();++it){
  if(it.key()!="name"&&it.key()!="author"&&it.key()!="version")throw std::runtime_error(Format(SharedText("desktop.languageerror.3e6684b9fe11"),{{"field",it.key()}}));
  document[it.key()]=it.value();
 }
 auto checked=ValidatePack(document.dump(),CanonicalEnglish());
 if(!checked.accepted)throw std::runtime_error(checked.errorCode.empty()?checked.error:Format(SharedText(checked.errorCode),checked.errorParameters));
 return document;
}
Json Flatten(const LanguagePack& base,const LanguagePack* local){return FlattenLanguagePack(base,local);}
PackValidation ReadBase(const LanguageStore& store,std::string_view id){
 // Only compiled product data is immutable; authored files stay freshly validated.
 if(const auto* bundled=IncludedPackValidation(id))return *bundled;
 return ValidatePack(store.ReadPack(id).dump(),CanonicalEnglish());
}
}
std::filesystem::path LanguageStore::Path(std::string_view id,const wchar_t* folder)const{if(id.empty()||id.size()>96||!std::regex_match(id.begin(),id.end(),std::regex("[A-Za-z0-9][A-Za-z0-9_.-]*")))throw std::runtime_error(Neurotic::UiMessage("desktop.languagestore.invalid_language_pack_identity_b7d727a2", "Invalid language pack identity."));return root/folder/(std::filesystem::path(std::string(id)).wstring()+L".nrlang");}
LanguageResult LanguageStore::WritePack(const Json& data,const wchar_t* folder){auto parsed=ValidatePack(data.dump(),CanonicalEnglish());if(!parsed.accepted)return {false,parsed.errorCode.empty()?parsed.error:Format(SharedText(parsed.errorCode),parsed.errorParameters)};SaveUserFile(Path(parsed.pack.id,folder),data.dump(2));return {true,Neurotic::UiMessage("desktop.languagestore.language_pack_saved_bcbedc5b", "Language pack saved."),parsed.pack.id,parsed.errors};}
LanguageResult LanguageStore::AddLanguage(std::string_view locale,std::string_view name)try{if(!ValidLocale(locale)||!UsableText(name))throw std::runtime_error(Neurotic::UiMessage("desktop.languagestore.enter_a_valid_locale_and_language_name_e9e4b0ae", "Enter a valid locale and language name."));GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error(Neurotic::UiMessage("desktop.languagestore.new_language_identifier_unavailable_811c2948", "New language identifier unavailable."));wchar_t value[40]{};StringFromGUID2(guid,value,40);std::string id="community-";for(wchar_t c:std::wstring_view(value))if(c!=L'{'&&c!=L'}')id+=char(c);return WritePack(Empty(id,std::string(locale),std::string(name)),L"Packs");}catch(const std::exception& e){return Failure(e);}
LanguageResult LanguageStore::ImportPack(const std::filesystem::path& path)try{return ImportText(ReadLanguageFile(path));}catch(const std::exception& e){return Failure(e);}
LanguageResult LanguageStore::ImportText(std::string_view bytes)try{auto parsed=ValidatePack(bytes,CanonicalEnglish());if(!parsed.accepted)return {false,parsed.errorCode.empty()?parsed.error:Format(SharedText(parsed.errorCode),parsed.errorParameters)};if(parsed.pack.id.starts_with("included-"))return {false,Neurotic::UiMessage("desktop.languagestore.included_pack_identities_are_reserved_export_an__68438c15", "Included pack identities are reserved. Export an edited pack before sharing it.")};auto destination=Path(parsed.pack.id,L"Packs");Json conflicts=Json::array();
 if(std::filesystem::exists(destination)){
  auto previous=ValidatePack(ReadLanguageFile(destination),CanonicalEnglish());
  if(previous.accepted){
   LanguagePack local;bool hasLocal=false;
   if(std::filesystem::exists(Path(parsed.pack.id,L"Drafts"))){local=DraftOverrides(parsed.pack.id,ReadDraft(parsed.pack.id),previous.pack);hasLocal=true;}
   else if(std::filesystem::exists(Path(parsed.pack.id,L"Overrides"))){auto saved=ValidatePack(ReadLanguageFile(Path(parsed.pack.id,L"Overrides")),CanonicalEnglish());if(saved.accepted){local=std::move(saved.pack);hasLocal=true;}}
   if(hasLocal)for(const auto& [id,edit]:local.entries){auto old=previous.pack.entries.find(id),next=parsed.pack.entries.find(id);if(old!=previous.pack.entries.end()&&next!=parsed.pack.entries.end()&&old->second.text!=next->second.text&&edit.text!=next->second.text)conflicts.push_back(id);}
  }
 }
 SaveUserFile(destination,parsed.pack.document.dump(2));SaveUserFile(root/L"Conflicts"/(std::filesystem::path(parsed.pack.id).wstring()+L".json"),conflicts.dump(2));return {true,conflicts.empty()?Neurotic::UiMessage("desktop.languagestore.community_pack_imported_6caffa6d", "Community pack imported."):Neurotic::UiMessage("desktop.languagestore.updated_community_pack_imported_local_edits_rema_47a3ee9b", "Updated community pack imported. Local edits remain selected; conflicting entries need review."),parsed.pack.id,parsed.errors};}catch(const std::exception& e){return Failure(e);}
Json LanguageStore::ReadPack(std::string_view id)const{if(auto* included=IncludedPack(id))return included->document;return Read(Path(id,L"Packs"));}
bool LanguageStore::Conflicted(std::string_view id,std::string_view entry)const{try{auto data=Read(root/L"Conflicts"/(Path(id,L"Packs").filename().stem().wstring()+L".json"));return std::find(data.begin(),data.end(),Json(entry))!=data.end();}catch(...){return false;}}
Json LanguageStore::ReadDraft(std::string_view id)const{
 auto path=Path(id,L"Drafts");if(std::filesystem::exists(path))return Read(path);
 auto base=ReadBase(*this,id);if(!base.accepted)throw std::runtime_error(base.errorCode.empty()?base.error:Format(SharedText(base.errorCode),base.errorParameters));
 if(std::filesystem::exists(Path(id,L"Overrides"))){auto local=ValidatePack(ReadLanguageFile(Path(id,L"Overrides")),CanonicalEnglish());if(local.accepted)return WithMetadata(Flatten(base.pack,&local.pack),ReadMetadata(id));}
 return base.pack.document;
}
LanguagePack LanguageStore::DraftOverrides(std::string_view id,const Json& draft,const LanguagePack& base)const{
 if(draft.value("packId",std::string{})!=id)throw std::runtime_error(Neurotic::UiMessage("desktop.languagestore.draft_belongs_to_another_pack_26a46ab3", "Draft belongs to another pack."));auto edited=ValidatePack(draft.dump(),CanonicalEnglish());if(!edited.accepted)throw std::runtime_error(edited.errorCode.empty()?edited.error:Format(SharedText(edited.errorCode),edited.errorParameters));
 auto reference=base;auto baseline=Path(id,L"DraftBases");if(std::filesystem::exists(baseline)){auto parsed=ValidatePack(ReadLanguageFile(baseline),CanonicalEnglish());if(parsed.accepted)reference=std::move(parsed.pack);}
 auto overrides=Empty(base.id,base.locale,base.name);
 for(const auto& [entry,text]:edited.pack.entries){auto previous=reference.entries.find(entry);if(previous!=reference.entries.end()&&previous->second.text==text.text&&previous->second.revision==text.revision&&previous->second.note==text.note)continue;overrides[CanonicalEnglish().at(entry).surface].push_back({{"id",entry},{"translation",text.text},{"revision",text.revision},{"note",text.note}});}
 for(const auto& old:edited.pack.obsolete)overrides[old.value("id",std::string{}).starts_with("ingame.")?"in_game":"desktop"].push_back(old);
 return ValidatePack(overrides.dump(),CanonicalEnglish()).pack;
}
Json LanguageStore::ReadMetadata(std::string_view id)const{
 auto path=Path(id,L"Metadata");path.replace_extension(L".json");
 return std::filesystem::exists(path)?Read(path):Json::object();
}
Json LanguageStore::DraftMetadata(std::string_view id,const Json& draft,const LanguagePack& base)const{
 auto reference=base.document;auto baseline=Path(id,L"DraftBases");
 if(std::filesystem::exists(baseline)){auto parsed=ValidatePack(ReadLanguageFile(baseline),CanonicalEnglish());if(parsed.accepted)reference=std::move(parsed.pack.document);}
 Json edits=Json::object();
 // Store only explicit edits; an untouched old draft must not freeze metadata
 // supplied by a newer community base. Identity and locale remain base-owned.
 for(const auto* field:{"name","author","version"})if(draft.value(field,std::string{})!=reference.value(field,std::string{}))edits[field]=draft.value(field,std::string{});
 return edits;
}
LanguageResult LanguageStore::SaveDraft(std::string_view id,const Json& draft)try{
 if(draft.value("packId",std::string{})!=id)throw std::runtime_error(Neurotic::UiMessage("desktop.languagestore.draft_belongs_to_another_pack_26a46ab3", "Draft belongs to another pack."));auto parsed=ValidatePack(draft.dump(),CanonicalEnglish());if(!parsed.accepted)return {false,parsed.errorCode.empty()?parsed.error:Format(SharedText(parsed.errorCode),parsed.errorParameters)};
 // Remember the source against which edits were made, so a later community
 // update cannot turn unchanged draft values into overriding local edits.
 auto baseline=Path(id,L"DraftBases");if(!std::filesystem::exists(baseline))SaveUserFile(baseline,ReadPack(id).dump(2));
 return WritePack(draft,L"Drafts");
}catch(const std::exception& e){return Failure(e);}
LanguageResult LanguageStore::Apply(std::string_view id,const Json& draft)try{
 if(id=="en"){auto empty=Empty("english","en","English");SaveUserFile(root/L"Active.json",empty.dump(2));return {true,Neurotic::UiMessage("desktop.languagestore.english_applied_e6956113", "English applied."),"en"};}
 auto base=ReadBase(*this,id);if(!base.accepted)throw std::runtime_error(base.errorCode.empty()?base.error:Format(SharedText(base.errorCode),base.errorParameters));LanguagePack local;const LanguagePack* localLayer=nullptr;std::vector<EntryError> errors;Json metadata=ReadMetadata(id);
 if(!draft.is_null()){auto edited=ValidatePack(draft.dump(),CanonicalEnglish());if(!edited.accepted)throw std::runtime_error(edited.errorCode.empty()?edited.error:Format(SharedText(edited.errorCode),edited.errorParameters));local=DraftOverrides(id,draft,base.pack);metadata=DraftMetadata(id,draft,base.pack);auto written=WritePack(local.document,L"Overrides");if(!written.success)throw std::runtime_error(written.message);auto metadataPath=Path(id,L"Metadata");metadataPath.replace_extension(L".json");SaveUserFile(metadataPath,metadata.dump(2));errors=edited.errors;localLayer=&local;
 }else if(std::filesystem::exists(Path(id,L"Overrides"))){auto parsed=ValidatePack(ReadLanguageFile(Path(id,L"Overrides")),CanonicalEnglish());if(parsed.accepted){local=std::move(parsed.pack);localLayer=&local;errors=parsed.errors;}}
 auto active=Flatten(base.pack,localLayer);active["desktop"]=Json::array();active["in_game"]=Json::array();auto merged=Flatten(base.pack,localLayer);for(auto surface:{"desktop","in_game"})for(const auto& entry:merged[surface])if(CanonicalEnglish().contains(entry.at("id").get<std::string>()))active[surface].push_back(entry);
 active=WithMetadata(std::move(active),metadata);SaveUserFile(root/L"Active.json",active.dump(2));return {true,errors.empty()?Neurotic::UiMessage("desktop.languagestore.language_applied_in_game_menus_use_it_on_the_nex_f1eab4b6", "Language applied. In-game menus use it on the next launch."):Neurotic::UiMessage("desktop.languagestore.usable_translations_applied_invalid_entries_use__7ec6e24c", "Usable translations applied. Invalid entries use the next available translation."),base.pack.id,errors};
 }catch(const std::exception& e){return Failure(e);}
LanguageResult LanguageStore::ExportPack(std::string_view id,const std::filesystem::path& path)try{
 auto base=ReadBase(*this,id);if(!base.accepted)throw std::runtime_error(base.errorCode.empty()?base.error:Format(SharedText(base.errorCode),base.errorParameters));LanguagePack local;const LanguagePack* layer=nullptr;size_t invalid=base.errors.size();Json metadata=ReadMetadata(id);
 if(std::filesystem::exists(Path(id,L"Drafts"))){auto draft=ReadDraft(id);invalid+=ValidatePack(draft.dump(),CanonicalEnglish()).errors.size();local=DraftOverrides(id,draft,base.pack);metadata=DraftMetadata(id,draft,base.pack);layer=&local;}
 else if(std::filesystem::exists(Path(id,L"Overrides"))){auto parsed=ValidatePack(ReadLanguageFile(Path(id,L"Overrides")),CanonicalEnglish());if(parsed.accepted){invalid+=parsed.errors.size();local=std::move(parsed.pack);layer=&local;}}
 auto flattened=WithMetadata(Flatten(base.pack,layer),metadata);flattened["sourcePack"]=base.pack.id;flattened["coverage"]["invalid"]=invalid;if(base.pack.id.starts_with("included-"))flattened["packId"]="community-"+base.pack.id;SaveUserFile(path,flattened.dump(2));return {true,invalid?Format(SharedText("desktop.languages.export_partial"),{{"arg1",int64_t(invalid)}}):Neurotic::UiMessage("desktop.languagestore.edited_language_pack_exported_364c312b", "Edited language pack exported."),base.pack.id};
}catch(const std::exception& e){return Failure(e);}
LanguageResult LanguageStore::ExportEnglishTemplate(const std::filesystem::path& path)try{SaveUserFile(path,EnglishTemplate().dump(2));return {true,Neurotic::UiMessage("desktop.languagestore.english_template_exported_6980e0cb", "English template exported.")};}catch(const std::exception& e){return Failure(e);}
std::vector<InstalledLanguage> LanguageStore::List()const{std::vector<InstalledLanguage> result;for(const auto& pack:IncludedPacks()){InstalledLanguage row{pack.id,pack.locale,pack.name,pack.document.value("author",std::string{}),pack.document.value("version",std::string{}),true};for(const auto& [id,text]:pack.entries){if(CanonicalEnglish().at(id).surface=="desktop")++row.desktop;else ++row.inGame;row.stale+=text.revision!=CanonicalEnglish().at(id).revision;}row.invalid=IncludedPackValidation(pack.id)->errors.size();result.push_back(std::move(row));}auto folder=root/L"Packs";if(!std::filesystem::exists(folder))return result;LanguageDirectoryGuard pinned(folder);size_t count=0;for(const auto& file:std::filesystem::directory_iterator(folder)){if(++count>4096)break;if(file.path().extension()!=L".nrlang")continue;try{auto parsed=ValidatePack(ReadLanguageFile(file.path()),CanonicalEnglish());if(!parsed.accepted)continue;InstalledLanguage row{parsed.pack.id,parsed.pack.locale,parsed.pack.name,parsed.pack.document.value("author",std::string{}),parsed.pack.document.value("version",std::string{})};row.invalid=parsed.errors.size();for(const auto& [id,text]:parsed.pack.entries){if(CanonicalEnglish().at(id).surface=="desktop")++row.desktop;else ++row.inGame;row.stale+=text.revision!=CanonicalEnglish().at(id).revision;}result.push_back(std::move(row));}catch(...){}}return result;}
LanguagePack LanguageStore::ActivePack()const{try{auto parsed=ValidatePack(ReadLanguageFile(root/L"Active.json"),CanonicalEnglish());if(parsed.accepted)return parsed.pack;}catch(...){}return ValidatePack(Empty("english","en","English").dump(),CanonicalEnglish()).pack;}
std::string LanguageStore::ActiveLocale()const{return ActivePack().locale;}
LanguageResult LanguageStore::RemovePack(std::string_view id)try{if(IncludedPack(id))return {false,Neurotic::UiMessage("desktop.languagestore.included_packs_remain_available_select_english_o_a5f19fd6", "Included packs remain available. Select English or another language to stop using this pack.")};auto path=Path(id,L"Packs");DeleteLanguageFile(path);if(ActivePack().id==id)return Apply("en");return {true,Neurotic::UiMessage("desktop.languagestore.community_pack_removed_drafts_and_local_edits_ar_430114fe", "Community pack removed. Drafts and local edits are preserved."),std::string(id)};}catch(const std::exception& e){return Failure(e);}
}
