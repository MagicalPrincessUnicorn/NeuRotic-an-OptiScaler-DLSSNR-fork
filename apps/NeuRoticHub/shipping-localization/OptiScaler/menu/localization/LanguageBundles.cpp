#include "LanguageBundles.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iterator>
#include <mutex>
#include <stdexcept>
namespace Neurotic::Localization {
namespace {
struct EmbeddedPack { const char* id; const char* locale; const char* const* chunks; size_t chunkCount; };
#include "BundledPacks.inc"
struct CachedPack { std::once_flag once; PackValidation validation; };
const PackValidation& LoadPack(size_t index) {
 // The index and cache grow with generated data, without a language-count cap.
 static std::array<CachedPack,std::size(EmbeddedPacks)> cached;
 auto& slot=cached[index];
 std::call_once(slot.once,[&] {
  const auto& source=EmbeddedPacks[index];
  size_t length=0;
  for(size_t i=0;i<source.chunkCount;++i)length+=std::strlen(source.chunks[i]);
  std::string bytes;bytes.reserve(length);
  for(size_t i=0;i<source.chunkCount;++i)bytes+=source.chunks[i];
  auto parsed=ValidatePack(bytes,CanonicalEnglish());
  if(!parsed.accepted||parsed.pack.id!=source.id||parsed.pack.locale!=source.locale)
   throw std::runtime_error("Included language pack is invalid.");
  slot.validation=std::move(parsed);
 });
 return slot.validation;
}
std::string LowerTag(std::string_view value) {
 std::string tag(value);
 std::transform(tag.begin(),tag.end(),tag.begin(),[](unsigned char c){return char(std::tolower(c));});
 return tag;
}
}
const std::vector<LanguagePack>& IncludedPacks() {
 static const auto packs=[] {
  std::vector<LanguagePack> list;list.reserve(std::size(EmbeddedPacks));
  for(size_t i=0;i<std::size(EmbeddedPacks);++i)list.push_back(LoadPack(i).pack);
  return list;
 }();
 return packs;
}
const PackValidation* IncludedPackValidation(std::string_view id) {
 for(size_t i=0;i<std::size(EmbeddedPacks);++i)if(id==EmbeddedPacks[i].id)return &LoadPack(i);
 return nullptr;
}
const LanguagePack* IncludedPack(std::string_view id) {
 const auto* parsed=IncludedPackValidation(id);
 return parsed?&parsed->pack:nullptr;
}
const LanguagePack* ApplicableBundled(std::string_view locale) {
 const auto tag=LowerTag(locale);
 for(size_t i=0;i<std::size(EmbeddedPacks);++i)
  if(LowerTag(EmbeddedPacks[i].locale)==tag)return &LoadPack(i).pack;
 const auto primary=tag.substr(0,tag.find('-'));
 if(primary=="zh"||primary=="pt")return nullptr;
 for(size_t i=0;i<std::size(EmbeddedPacks);++i)
  if(LowerTag(EmbeddedPacks[i].locale)==primary)return &LoadPack(i).pack;
 return nullptr;
}
}
