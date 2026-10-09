#include <menu/Localization.h>
#pragma once
#include "ManualLibrary.h"
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <vector>
namespace nh {
// Library identity and active installer/launch context are separate. A choice
// always carries the context of its own installation, including store launch.
struct GameExecutableChoice {std::string path,root,store,storeId,steamRoot,iconHint;int bitness=0;};
struct Game {std::string id,title,store,root;Target target;std::string storeId,steamRoot,iconHint;std::vector<std::string> candidates;bool manualTarget=false,favorite=false,hidden=false;std::vector<GameExecutableChoice> executableChoices;};
inline std::wstring GamePathKey(const std::string& value){
 if(value.empty())return {};auto path=std::filesystem::path(Wide(value));if(!path.is_absolute()||path.wstring().starts_with(L"\\\\"))throw std::runtime_error(Neurotic::UiMessage("desktop.gamegrouping.a_local_absolute_game_path_is_required_356caedf", "A local absolute game path is required"));auto key=path.lexically_normal().wstring();std::replace(key.begin(),key.end(),L'/',L'\\');while(key.size()>3&&key.back()==L'\\')key.pop_back();std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return (wchar_t)towlower(c);});return key;
}
inline bool GamePathWithin(const std::string& path,const std::string& root){auto key=GamePathKey(path),base=GamePathKey(root);return !key.empty()&&!base.empty()&&(key==base||key.starts_with(base+L"\\"));}
inline bool GameHasStoreIdentity(const std::string& store,const std::string& id){return !id.empty()&&store!="Custom"&&store!="Folder"&&store!=Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual");}
inline int GameStorePriority(const std::string& store){return store=="Custom"||store=="Folder"||store==Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual")?0:1;}
inline GameExecutableChoice GameChoiceContext(const Game& game,const std::string& path,int bitness){return {path,game.root,game.store,game.storeId,game.steamRoot,game.iconHint,bitness};}
inline void AddGameChoice(Game& game,GameExecutableChoice choice){
 auto target=InspectExecutable(Wide(choice.path));if(!target.suitable)return;choice.path=target.path;choice.bitness=target.bitness;
 if(!GamePathWithin(choice.path,choice.root))throw std::runtime_error(Neurotic::UiMessage("desktop.gamegrouping.executable_choice_escapes_installation_f1e298af", "Executable choice escapes installation"));
 auto key=GamePathKey(choice.path);auto found=std::find_if(game.executableChoices.begin(),game.executableChoices.end(),[&](auto& old){return GamePathKey(old.path)==key;});
 if(found==game.executableChoices.end()){if(game.executableChoices.size()>=256)throw std::runtime_error(Neurotic::UiMessage("desktop.gamegrouping.game_executable_choice_limit_reached_df63e170", "Game executable choice limit reached"));game.executableChoices.push_back(std::move(choice));}
 else if(GameStorePriority(choice.store)>GameStorePriority(found->store)||(!GameHasStoreIdentity(found->store,found->storeId)&&GameHasStoreIdentity(choice.store,choice.storeId)))*found=std::move(choice);
}
inline void NormalizeGameChoices(Game& game){
 auto prior=std::move(game.executableChoices);game.executableChoices.clear();for(auto& choice:prior)AddGameChoice(game,std::move(choice));
 for(auto& path:game.candidates)if(GamePathWithin(path,game.root))AddGameChoice(game,GameChoiceContext(game,path,0));
 if(!game.target.path.empty()&&GamePathWithin(game.target.path,game.root))AddGameChoice(game,GameChoiceContext(game,game.target.path,game.target.bitness));
 game.candidates.clear();for(auto& choice:game.executableChoices)game.candidates.push_back(choice.path);
}
inline bool SameGameIdentity(const Game& a,const Game& b){
 // A conflicting edition/store ID vetoes folder and display-name coincidence.
 auto identities=[](const Game& game){std::map<std::string,std::set<std::string>> result;auto add=[&](const std::string& store,const std::string& id){if(GameHasStoreIdentity(store,id))result[store].insert(id);};add(game.store,game.storeId);for(auto& choice:game.executableChoices)add(choice.store,choice.storeId);return result;};
 auto left=identities(a),right=identities(b);bool shared=false;
 for(auto& [store,ids]:left){auto other=right.find(store);if(other==right.end())continue;bool match=std::any_of(ids.begin(),ids.end(),[&](auto& id){return other->second.contains(id);});if(!match)return false;shared=true;}
 if(shared)return true;
 if(!a.id.empty()&&a.id==b.id)return true;
 if(a.target.suitable&&b.target.suitable&&GamePathKey(a.target.path)==GamePathKey(b.target.path))return true;
 for(auto& left:a.executableChoices)for(auto& right:b.executableChoices)if(GamePathKey(left.path)==GamePathKey(right.path))return true;
 // A manually assigned executable needs its exact choice or store identity;
 // a shared directory alone cannot assign an ambiguous edition to that row.
 if((a.store==Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual")&&a.manualTarget)||(b.store==Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual")&&b.manualTarget))return false;
 return !a.root.empty()&&!b.root.empty()&&GamePathKey(a.root)==GamePathKey(b.root);
}
inline bool ApplyGameChoice(Game& game,const std::string& path){
 auto key=GamePathKey(path);auto choice=std::find_if(game.executableChoices.begin(),game.executableChoices.end(),[&](auto& value){return GamePathKey(value.path)==key;});if(choice==game.executableChoices.end())return false;
 game.target=InspectExecutable(Wide(choice->path));game.root=choice->root;game.store=choice->store;game.storeId=choice->storeId;game.steamRoot=choice->steamRoot;game.iconHint=choice->iconHint;return true;
}
inline void MergeGameEntry(Game& game,Game incoming,bool refreshMetadata=true){
 NormalizeGameChoices(game);NormalizeGameChoices(incoming);
 const auto active=game.target.path;const bool adoptManual=!game.manualTarget&&incoming.manualTarget;
 const bool adoptTarget=incoming.target.suitable&&(active.empty()||!game.target.suitable);
 for(auto& choice:incoming.executableChoices)AddGameChoice(game,std::move(choice));
 game.favorite=game.favorite||incoming.favorite;game.hidden=game.hidden||incoming.hidden;
 if(GameStorePriority(incoming.store)>GameStorePriority(game.store)||(refreshMetadata&&GameStorePriority(incoming.store)==GameStorePriority(game.store)))game.title=incoming.title;
 if(adoptManual||adoptTarget){game.target=incoming.target;game.root=incoming.root;game.store=incoming.store;game.storeId=incoming.storeId;game.steamRoot=incoming.steamRoot;game.iconHint=incoming.iconHint;}
 if(game.target.path.empty()&&!incoming.storeId.empty()){
  if(game.storeId.empty()||GameStorePriority(incoming.store)>GameStorePriority(game.store)){game.store=incoming.store;game.storeId=incoming.storeId;}
  if(game.steamRoot.empty())game.steamRoot=incoming.steamRoot;
 }
 game.manualTarget=game.manualTarget||incoming.manualTarget;
 if(!game.target.path.empty())ApplyGameChoice(game,game.target.path);
 if(game.iconHint.empty())game.iconHint=incoming.iconHint;
 game.candidates.clear();for(auto& choice:game.executableChoices)game.candidates.push_back(choice.path);
}
inline std::vector<Game> GroupLibraryGames(std::vector<Game> games,const std::string& preferredId={}){
 for(auto& game:games)NormalizeGameChoices(game);
 // A later row can bridge two earlier rows through distinct executables. Keep
 // coalescing until the resulting identities no longer overlap.
 for(size_t i=0;i<games.size();++i){bool joined;do{joined=false;for(size_t j=i+1;j<games.size();++j)if(SameGameIdentity(games[i],games[j])){if(!preferredId.empty()&&games[j].id==preferredId)std::swap(games[i],games[j]);MergeGameEntry(games[i],std::move(games[j]),false);games.erase(games.begin()+j);joined=true;break;}}while(joined);}
 return games;
}
}
