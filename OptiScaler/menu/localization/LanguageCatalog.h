#pragma once
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <json.hpp>
namespace Neurotic::Localization {
using Json=nlohmann::json;
struct Placeholder{std::string name,type;};
struct EnglishEntry{std::string id,surface,section,control,english,context;std::vector<Placeholder> placeholders;std::vector<std::string> preserve;bool lineBreaks=false;unsigned revision=1;};
using EnglishCatalog=std::map<std::string,EnglishEntry,std::less<>>;
struct Translation{std::string text,note;unsigned revision=0;};
struct LanguagePack{Json document;std::string id,locale,name;std::map<std::string,Translation,std::less<>> entries;std::vector<Json> obsolete;};
enum class Layer{English,Bundled,Community,Local};
struct CatalogLayers{const EnglishCatalog* english=nullptr;const LanguagePack* local=nullptr;const LanguagePack* community=nullptr;const LanguagePack* bundled=nullptr;};
struct ResolvedText{std::string text;Layer layer=Layer::English;bool needsReview=false;const EnglishEntry* entry=nullptr;};
using Argument=std::variant<std::string,int64_t,double>;
using NamedArguments=std::map<std::string,Argument,std::less<>>;
ResolvedText Resolve(const CatalogLayers&,std::string_view id);
std::string Format(const ResolvedText&,const NamedArguments&);
bool UsableText(std::string_view text);
const EnglishCatalog& CanonicalEnglish();
Json EnglishTemplate();
}
