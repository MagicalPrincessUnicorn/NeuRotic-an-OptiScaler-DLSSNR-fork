#include "menu/Localization.h"
#include "menu/localization/LanguageRuntime.h"
#include "menu/localization/LanguageBundles.h"
#include <iostream>
int main(){using namespace Neurotic::Localization;int failed=0;auto check=[&](bool ok,const char* note){std::cout<<(ok?"PASS ":"FAIL ")<<note<<'\n';failed+=!ok;};
 const auto& english=CanonicalEnglish();auto item=std::find_if(english.begin(),english.end(),[](const auto& pair){return pair.second.surface=="desktop"&&pair.second.placeholders.empty()&&pair.second.preserve.empty();});
 Json document={{"formatVersion",1},{"packId","runtime-fixture"},{"locale","de"},{"language","Deutsch"},{"name","Fixture"},{"author","Test"},{"version","1"},{"desktop",Json::array({{{"id",item->first},{"translation","Meine Übersetzung"},{"revision",0}}})},{"in_game",Json::array()}};auto parsed=ValidatePack(document.dump(),english);check(parsed.accepted,"runtime fixture validated");SetSharedCatalog(parsed.pack);
 const auto& original=item->second.english;const auto* label=Neurotic::UiLiteral(item->first,original.c_str());check(label==original.c_str()&&std::string(label)==original,"stable literal binding preserves original control identity");
 const char* begin=label;const char* end=nullptr;{Neurotic::LocalizedRange range(begin,end);check(std::string(begin,end)=="Meine Übersetzung","validated shared catalog translates real presentation range");}
 begin=label;end=nullptr;{Neurotic::EnglishPreview preview;Neurotic::LocalizedRange range(begin,end);check(begin==label,"English preview does not mutate selected language");}
 std::string userText=original;begin=userText.c_str();end=nullptr;{Neurotic::LocalizedRange range(begin,end);check(begin==userText.c_str(),"matching user-authored text remains unbound and unchanged");}
 SetSharedCatalog({});begin=label;end=nullptr;{Neurotic::LocalizedRange range(begin,end);check(std::string(begin,end)==original,"missing selected translation falls through to canonical English");}
 check(SelectedLocale()=="en","empty shared catalog selects safe English fallback");
 auto templ=std::find_if(english.begin(),english.end(),[](const auto& pair){return pair.second.placeholders.size()==1&&pair.second.placeholders[0].type=="number"&&pair.second.english.find('%')!=std::string::npos;});
 if(templ!=english.end()){auto copy=document;copy["desktop"]=Json::array();copy["in_game"]=Json::array();copy[templ->second.surface].push_back({{"id",templ->first},{"translation","Übersetzt: "+templ->second.english},{"revision",1}});auto valid=ValidatePack(copy.dump(),english);check(valid.accepted&&valid.errors.empty(),"compiled numeric format retains exact validated ABI tokens");SetSharedCatalog(valid.pack);const char* format=Neurotic::UiLiteral(templ->first,templ->second.english.c_str());{Neurotic::LocalizedFormat scoped(format);check(std::string(format).starts_with("Übersetzt: "),"formatted presentation resolves before value insertion");}}
 std::string dynamic=item->second.english;{Neurotic::ScopedUiLiteral binding(item->first,dynamic.c_str());check(TranslateBoundRange(dynamic.c_str(),nullptr).has_value(),"scoped dynamic label keeps original identity and translates during draw");}check(!TranslateBoundRange(dynamic.c_str(),nullptr).has_value(),"dynamic binding is retired before its buffer can become user data");
 SetSharedCatalog({});QueueSharedCatalog(parsed.pack);check(SelectedLocale()=="en","Apply keeps the current draw catalog until the next frame boundary");PublishQueuedCatalog();check(SelectedLocale()=="de"&&SharedText(item->first).text=="Meine Übersetzung","frame boundary publishes the immutable selected catalog");
 check(ApplicableBundled("PT-br")&&ApplicableBundled("PT-br")->locale=="pt-BR","BCP47 casing retains the Brazilian bundled identity");check(ApplicableBundled("pt-PT")==nullptr,"regional Portuguese does not silently use Brazilian fallback");
 check(Neurotic::UiMessage("desktop.anythingview.nr_anything_56792861","NR Anything · ").ends_with(" "),"copied messages preserve original presentation padding");
 return failed?1:0;
}
