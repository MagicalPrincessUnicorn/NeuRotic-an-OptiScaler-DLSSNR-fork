#pragma once
#include "LanguageStore.h"
namespace nh {
enum class EditorFilter{All,Missing,NeedsReview,Invalid,Complete,Obsolete};
enum class CloseState{Open,Ask,Ready};
class TranslationEditorModel {
 Json draft,saved;std::string selected,search;EditorFilter filter=EditorFilter::All;int surface=0;CloseState close=CloseState::Open;
 mutable bool cacheDirty=true;mutable Neurotic::Localization::PackValidation validated;mutable std::map<std::string,std::string,std::less<>> textCache;
 void RefreshCache()const;
 public:
 void Load(Json document);void Select(std::string id){selected=std::move(id);}
 const std::string& Selection()const{return selected;}
 const Json& Document()const{return draft;}Json& Metadata(){cacheDirty=true;return draft;}
 bool Dirty()const{return draft!=saved;}void MarkSaved(){saved=draft;close=CloseState::Open;}
 void SetTranslation(std::string_view text);std::string Translation(std::string_view id)const;std::string Validation(std::string_view id)const;
 void SetSearch(std::string value){search=std::move(value);}void SetFilter(EditorFilter value){filter=value;}void SetSurface(int value){surface=value;}
 std::vector<std::string> Rows()const;CloseState RequestClose(){return close=Dirty()?CloseState::Ask:CloseState::Ready;}
 CloseState CloseStatus()const{return close;}void CancelClose(){close=CloseState::Open;}void DiscardClose(){close=CloseState::Ready;}
};
}
