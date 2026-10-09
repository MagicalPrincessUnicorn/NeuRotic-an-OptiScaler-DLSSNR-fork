#include <menu/Localization.h>
#include "TranslationEditorModel.h"
#include "../../../OptiScaler/menu/localization/LanguageRuntime.h"
#include <algorithm>
#include <cctype>
namespace nh {
using namespace Neurotic::Localization;
namespace {std::string Lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}}
void TranslationEditorModel::Load(Json document){draft=std::move(document);saved=draft;close=CloseState::Open;selected.clear();cacheDirty=true;}
void TranslationEditorModel::RefreshCache()const{if(!cacheDirty)return;validated=ValidatePack(draft.dump(),CanonicalEnglish());textCache.clear();for(auto part:{"desktop","in_game"})if(draft.contains(part))for(const auto& row:draft[part])textCache[row.value("id",std::string{})]=row.value("translation",std::string{});cacheDirty=false;}
std::string TranslationEditorModel::Translation(std::string_view id)const{RefreshCache();auto found=textCache.find(id);return found==textCache.end()?std::string{}:found->second;}
std::string TranslationEditorModel::Validation(std::string_view id)const{auto known=CanonicalEnglish().find(id);if(known==CanonicalEnglish().end())return Neurotic::UiMessage("desktop.translationeditormodel.obsolete_entry_kept_for_sharing_unavailable_in_t_abcfa08a", "Obsolete entry; kept for sharing, unavailable in the current interface.");RefreshCache();for(const auto& error:validated.errors)if(error.id==id)return error.reasonCode.empty()?error.reason:Format(SharedText(error.reasonCode),error.parameters);return {};}
void TranslationEditorModel::SetTranslation(std::string_view text){cacheDirty=true;auto known=CanonicalEnglish().find(selected);std::string target=known==CanonicalEnglish().end()?(selected.starts_with("ingame.")?"in_game":"desktop"):known->second.surface;for(auto& row:draft[target])if(row.value("id",std::string{})==selected){row["translation"]=text;row["revision"]=known==CanonicalEnglish().end()?row.value("revision",0u):known->second.revision;return;}if(known!=CanonicalEnglish().end())draft[target].push_back({{"id",selected},{"translation",text},{"revision",known->second.revision},{"note",""}});}
std::vector<std::string> TranslationEditorModel::Rows()const{std::vector<std::string> result;RefreshCache();const auto& parsed=validated;const auto query=Lower(search);for(const auto& [id,entry]:CanonicalEnglish()){
 if(surface==1&&entry.surface!="desktop"||surface==2&&entry.surface!="in_game")continue;
 auto text=Translation(id);bool invalid=!Validation(id).empty(),missing=text.empty();auto translated=parsed.pack.entries.find(id);bool stale=translated!=parsed.pack.entries.end()&&translated->second.revision!=entry.revision;
 if(filter==EditorFilter::Obsolete||filter==EditorFilter::Missing&&!missing||filter==EditorFilter::Invalid&&!invalid||filter==EditorFilter::NeedsReview&&!stale||filter==EditorFilter::Complete&&(missing||invalid||stale))continue;
 if(!query.empty()&&Lower(id+" "+entry.english+" "+text+" "+entry.context).find(query)==std::string::npos)continue;result.push_back(id);
 }if(filter==EditorFilter::All||filter==EditorFilter::Obsolete)for(const auto& old:parsed.pack.obsolete){auto id=old.value("id",std::string{});if(surface==1&&id.starts_with("ingame.")||surface==2&&!id.starts_with("ingame."))continue;if(query.empty()||Lower(old.dump()).find(query)!=std::string::npos)result.push_back(id);}return result;}
}
