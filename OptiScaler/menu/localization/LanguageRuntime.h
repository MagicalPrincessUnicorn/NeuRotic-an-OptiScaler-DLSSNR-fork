#pragma once
#include "LanguagePack.h"
#include <optional>
#include <filesystem>
#define NEUROTIC_LANGUAGE_SELECTION 1
namespace Neurotic::Localization {
void SetSharedCatalog(LanguagePack pack);
void QueueSharedCatalog(LanguagePack pack);
void PublishQueuedCatalog();
void LoadSharedCatalogOnce();
bool SharedCatalogEnabled();
std::string BoundLiteralId(const char*);
std::string SelectedLocale();
std::string SelectedLanguageName();
std::string SelectedPackId();
std::string LocaleDisplayCode(std::string_view locale);
struct LanguageChoice { std::string id,locale,name; };
Json FlattenLanguagePack(const LanguagePack& base,const LanguagePack* local);
std::vector<LanguageChoice> AvailableLanguages(const std::filesystem::path& root={});
bool SelectSharedLanguage(std::string_view id,const std::filesystem::path& root,std::string& error);
ResolvedText SharedText(std::string_view id);
inline std::string FormatSharedText(std::string_view id,const NamedArguments& arguments){return Format(SharedText(id),arguments);}
std::optional<std::string> TranslateBoundRange(const char* begin,const char* end);
}
