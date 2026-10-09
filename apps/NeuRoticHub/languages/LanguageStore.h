#pragma once
#include "../../../OptiScaler/menu/localization/LanguagePack.h"
#include "storage/UserFile.h"
namespace nh {
using Json=Neurotic::Localization::Json;
struct LanguageResult{bool success=false;std::string message,packId;std::vector<Neurotic::Localization::EntryError> errors;};
struct InstalledLanguage{std::string id,locale,name,author,version;bool included=false;size_t desktop=0,inGame=0,stale=0,invalid=0;};
class LanguageStore {
 std::filesystem::path root;
 std::filesystem::path Path(std::string_view id,const wchar_t* folder)const;
 LanguageResult WritePack(const Json&,const wchar_t* folder);
 Neurotic::Localization::LanguagePack DraftOverrides(std::string_view id,const Json& draft,const Neurotic::Localization::LanguagePack& base)const;
 Json DraftMetadata(std::string_view id,const Json& draft,const Neurotic::Localization::LanguagePack& base)const;
 Json ReadMetadata(std::string_view id)const;
 public:
 explicit LanguageStore(std::filesystem::path userRoot):root(std::move(userRoot)/L"Languages"){}
 LanguageResult AddLanguage(std::string_view locale,std::string_view name);
 LanguageResult ImportPack(const std::filesystem::path&);
 LanguageResult ImportText(std::string_view);
 LanguageResult SaveDraft(std::string_view id,const Json&);
 LanguageResult Apply(std::string_view id,const Json& draft=Json());
 LanguageResult ExportPack(std::string_view id,const std::filesystem::path&);
 LanguageResult ExportEnglishTemplate(const std::filesystem::path&);
 LanguageResult RemovePack(std::string_view id);
 Json ReadPack(std::string_view id)const;
 Json ReadDraft(std::string_view id)const;
 bool Conflicted(std::string_view pack,std::string_view entry)const;
 std::vector<InstalledLanguage> List()const;
 std::string ActiveLocale()const;
 Neurotic::Localization::LanguagePack ActivePack()const;
 const std::filesystem::path& Root()const{return root;}
};
}
