#include "LanguageRuntime.h"
#include "LanguageFiles.h"
#include "LanguageBundles.h"
#include "../Localization.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cctype>
#include <set>
#include <algorithm>
#include "../../../apps/NeuRoticHub/storage/UserFile.h"
#pragma comment(lib,"ole32.lib")
namespace Neurotic::Localization {
namespace {
struct RuntimeState{std::mutex mutex;bool enabled=false,loaded=false;LanguagePack selected;std::optional<LanguagePack> queued;std::unordered_map<const char*,std::string> bindings;};
RuntimeState& State(){static RuntimeState state;return state;}
}
void SetSharedCatalog(LanguagePack pack){auto& state=State();std::lock_guard lock(state.mutex);state.selected=std::move(pack);if(state.selected.locale.empty())state.selected.locale="en";state.enabled=state.loaded=true;}
void QueueSharedCatalog(LanguagePack pack){auto& state=State();std::lock_guard lock(state.mutex);state.queued=std::move(pack);}
void PublishQueuedCatalog(){auto& state=State();std::lock_guard lock(state.mutex);if(!state.queued)return;state.selected=std::move(*state.queued);state.queued.reset();if(state.selected.locale.empty())state.selected.locale="en";state.enabled=state.loaded=true;}
void LoadSharedCatalogOnce(){auto& state=State();{std::lock_guard lock(state.mutex);if(state.loaded)return;state.loaded=true;}LanguagePack pack;try{auto parsed=ValidatePack(ReadLanguageFile(SharedLanguageRoot()/L"Active.json"),CanonicalEnglish());if(parsed.accepted)pack=std::move(parsed.pack);}catch(...){}SetSharedCatalog(std::move(pack));}
std::string BoundLiteralId(const char* literal){auto& state=State();std::lock_guard lock(state.mutex);auto found=state.bindings.find(literal);return found==state.bindings.end()?std::string{}:found->second;}
bool SharedCatalogEnabled(){auto& state=State();std::lock_guard lock(state.mutex);return state.enabled;}
std::string SelectedLocale(){auto& state=State();std::lock_guard lock(state.mutex);return state.selected.locale.empty()?"en":state.selected.locale;}
std::string SelectedLanguageName(){auto& state=State();std::lock_guard lock(state.mutex);return state.selected.document.is_object()?state.selected.document.value("language",std::string("English")):"English";}
std::string SelectedPackId(){auto& state=State();std::lock_guard lock(state.mutex);return state.selected.id.empty()||state.selected.id=="english"?"en":state.selected.id;}
std::string LocaleDisplayCode(std::string_view locale){std::string code(locale);std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){return char(std::toupper(c));});return code;}
std::vector<LanguageChoice> AvailableLanguages(const std::filesystem::path& requestedRoot){
 std::vector<LanguageChoice> choices{{"en","en","English"}};
 for(const auto& pack:IncludedPacks())choices.push_back({pack.id,pack.locale,pack.document.value("language",pack.name)});
 try{
  const auto root=requestedRoot.empty()?SharedLanguageRoot():requestedRoot;const auto folder=root/L"Packs";
  if(!std::filesystem::exists(folder))return choices;LanguageDirectoryGuard pinned(folder);size_t count=0;
  for(const auto& file:std::filesystem::directory_iterator(folder)){
   if(++count>4096)break;if(file.path().extension()!=L".nrlang")continue;
   try{auto parsed=ValidatePack(ReadLanguageFile(file.path()),CanonicalEnglish());
    if(parsed.accepted&&!parsed.pack.id.starts_with("included-")&&parsed.pack.id!="en"&&file.path().stem().string()==parsed.pack.id)
     choices.push_back({parsed.pack.id,parsed.pack.locale,parsed.pack.document.value("language",parsed.pack.name)});
   }catch(...){}
  }
 }catch(...){}
 return choices;
}
Json FlattenLanguagePack(const LanguagePack& base,const LanguagePack* local){auto doc=base.document;doc["desktop"]=Json::array();doc["in_game"]=Json::array();Json coverage={{"desktop",0},{"in_game",0},{"reviewed",0},{"stale",0},{"invalid",0},{"total",CanonicalEnglish().size()}};std::set<std::string> included;for(const auto& [id,entry]:CanonicalEnglish()){auto text=Resolve({&CanonicalEnglish(),local,&base,ApplicableBundled(base.locale)},id);if(text.layer==Layer::English)continue;const auto& source=text.layer==Layer::Local?local->entries.at(id):text.layer==Layer::Bundled?ApplicableBundled(base.locale)->entries.at(id):base.entries.at(id);doc[entry.surface].push_back({{"id",id},{"translation",text.text},{"revision",source.revision},{"note",source.note},{"english",entry.english},{"context",entry.context}});coverage[entry.surface]=coverage[entry.surface].get<size_t>()+1;auto status=text.needsReview?"stale":"reviewed";coverage[status]=coverage[status].get<size_t>()+1;included.insert(id);}for(const auto* pack:{local,&base})if(pack)for(const auto& old:pack->obsolete){auto id=old.value("id",std::string{});if(included.insert(id).second)doc[id.starts_with("ingame.")?"in_game":"desktop"].push_back(old);}doc["coverage"]=coverage;return doc;}
bool SelectSharedLanguage(std::string_view id,const std::filesystem::path& requestedRoot,std::string& error){
 try{
  const auto root=requestedRoot.empty()?SharedLanguageRoot():requestedRoot;
  LanguagePack base;
  if(id=="en"){
   Json english={{"formatVersion",1},{"packId","english"},{"locale","en"},{"language","English"},{"name","English"},{"author","NeuRotic"},{"version","1"},{"desktop",Json::array()},{"in_game",Json::array()}};
   base=ValidatePack(english.dump(),CanonicalEnglish()).pack;
  }else if(const auto* included=IncludedPack(id))base=*included;
  else{
   // IDs, locales and JSON fields remain canonical machine data.
   if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=id.npos||id=="."||id=="..")throw std::runtime_error("identifier");
   auto parsed=ValidatePack(ReadLanguageFile(root/L"Packs"/(std::string(id)+".nrlang")),CanonicalEnglish());
   if(!parsed.accepted||parsed.pack.id!=id)throw std::runtime_error("unavailable");base=std::move(parsed.pack);
  }
  auto active=base.document;
  if(id!="en"){
   const auto overrides=root/L"Overrides"/(std::string(id)+".nrlang");
   if(std::filesystem::exists(overrides)){
    auto local=ValidatePack(ReadLanguageFile(overrides),CanonicalEnglish());
    if(local.accepted&&local.pack.id==base.id&&local.pack.locale==base.locale)active=FlattenLanguagePack(base,&local.pack);
   }
   const auto metadata=root/L"Metadata"/(std::string(id)+".json");
   if(std::filesystem::exists(metadata)){
    auto edits=Json::parse(ReadLanguageFile(metadata));for(const auto* field:{"name","author","version"})if(edits.contains(field)&&edits[field].is_string())active[field]=edits[field];
   }
  }
  auto selected=ValidatePack(active.dump(),CanonicalEnglish());if(!selected.accepted)throw std::runtime_error("invalid");
  nh::SaveUserFile(root/L"Active.json",active.dump(2));QueueSharedCatalog(std::move(selected.pack));error.clear();return true;
 }catch(...){error=Neurotic::UiMessage("ingame.language.selection_failed","Language could not be saved. Check the App's language settings and try again.");return false;}
}
ResolvedText SharedText(std::string_view id){auto& state=State();std::lock_guard lock(state.mutex);return Resolve({&CanonicalEnglish(),nullptr,&state.selected,ApplicableBundled(state.selected.locale)},id);}
std::optional<std::string> TranslateBoundRange(const char* begin,const char* end){if(!begin)return std::nullopt;auto& state=State();std::lock_guard lock(state.mutex);if(!state.enabled)return std::nullopt;auto binding=state.bindings.find(begin);if(binding==state.bindings.end())return std::nullopt;auto known=CanonicalEnglish().find(binding->second);if(known==CanonicalEnglish().end())return std::nullopt;auto text=Resolve({&CanonicalEnglish(),nullptr,&state.selected,ApplicableBundled(state.selected.locale)},binding->second).text;std::string_view original=end?std::string_view(begin,size_t(end-begin)):std::string_view(begin);size_t first=original.find_first_not_of(" \r\n\t"),last=original.find_last_not_of(" \r\n\t");if(first!=original.npos)return std::string(original.substr(0,first))+text+std::string(original.substr(last+1));return text;}
void RegisterLiteral(std::string_view id,const char* literal){if(!literal)return;auto& state=State();std::lock_guard lock(state.mutex);if(state.bindings.size()>32768&&!state.bindings.contains(literal))return;state.bindings.insert_or_assign(literal,std::string(id));auto visible=literal;while(*visible&&std::isspace(static_cast<unsigned char>(*visible)))++visible;if(visible!=literal)state.bindings.insert_or_assign(visible,std::string(id));}
}
namespace Neurotic {
const std::string& FontNoticeText(){static const auto text=[](){HMODULE module=nullptr;std::string value;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&FontNoticeText),&module))return value;for(int id:{5104,5105}){auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));if(!resource)continue;auto* bytes=static_cast<const char*>(LockResource(LoadResource(module,resource)));auto size=SizeofResource(module,resource);if(bytes&&size){value.append(bytes,size);value+="\n\n";}}return value;}();return text;}
const char* UiLiteral(std::string_view id,const char* original){Localization::RegisterLiteral(id,original);return original;}
const char* UiOptions(std::string_view ids,const char* original){auto pointer=original;size_t start=0;while(start<=ids.size()){auto end=ids.find('|',start);if(end==ids.npos)end=ids.size();auto id=ids.substr(start,end-start);if(!id.empty())Localization::RegisterLiteral(id,pointer);if(end==ids.size())break;pointer+=std::char_traits<char>::length(pointer)+1;start=end+1;}return original;}
std::string UiText(std::string_view id){return Localization::SharedText(id).text;}
}

namespace Neurotic {std::string UiMessage(std::string_view id,const char* original){auto text=Localization::SharedText(id).text;if(!original)return text;std::string_view source(original);auto first=source.find_first_not_of(" \r\n\t"),last=source.find_last_not_of(" \r\n\t");return first==source.npos?text:std::string(source.substr(0,first))+text+std::string(source.substr(last+1));}}
namespace Neurotic {
ScopedUiLiteral::ScopedUiLiteral(std::string_view id,const char* original){if(id.empty()||!original)return;pointer=original;auto& state=Localization::State();std::lock_guard lock(state.mutex);if(auto found=state.bindings.find(pointer);found!=state.bindings.end())previous=found->second;state.bindings.insert_or_assign(pointer,std::string(id));}
ScopedUiLiteral::~ScopedUiLiteral(){if(!pointer)return;auto& state=Localization::State();std::lock_guard lock(state.mutex);if(previous)state.bindings.insert_or_assign(pointer,*previous);else state.bindings.erase(pointer);}
}
