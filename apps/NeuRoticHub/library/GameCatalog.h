#include <menu/Localization.h>
#pragma once
#include "ui/HubViewModel.h"
#include "GameCatalogData.h"
#include <algorithm>
namespace nh {
inline std::string CatalogKey(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return (char)std::tolower(c);});return text;}
inline Json GameRecommendation(const Game& game){
 static const Json catalog=Json::parse(GameCatalogData);
 auto store=CatalogKey(game.store),title=CatalogKey(game.title);
 for(auto& entry:catalog["games"])for(auto& identity:entry["ids"])if(!game.storeId.empty()&&CatalogKey(identity["store"].get<std::string>())==store&&identity["id"]==game.storeId)return entry;
 // Exact title/alias only; executable filenames and fuzzy titles never establish identity.
 for(auto& entry:catalog["games"]){if(CatalogKey(entry[Neurotic::UiLiteral("desktop.translationeditorwindow.name_ef8e2b72", "name")].get<std::string>())==title)return entry;for(auto& alias:entry["aliases"])if(alias.is_string()&&CatalogKey(alias.get<std::string>())==title)return entry;}
 return Json();
}
// Only an unambiguous known API family selects a proxy automatically.
// Multi-API titles expose each recommendation without guessing their active renderer.
inline std::string RecommendedProxy(const Game& game){
 auto row=GameRecommendation(game);if(row.is_null())return {};std::string selected;
 for(const auto& profile:row["profiles"]){const auto proxy=profile.value("api","")=="vulkan"?std::string("winmm.dll"):profile.value("proxy","");if(selected.empty())selected=proxy;else if(selected!=proxy)return {};}
 return selected;
}
inline int RecommendedProxyIndex(const Game& game){auto proxy=RecommendedProxy(game);const char* names[]={Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll"),Neurotic::UiLiteral("desktop.option.d508058f7eba", "winmm.dll"),Neurotic::UiLiteral("desktop.option.e4c456927fa4", "version.dll"),Neurotic::UiLiteral("desktop.option.dcc94e2ae3ad", "dbghelp.dll"),Neurotic::UiLiteral("desktop.option.cbf80229b83a", "d3d12.dll"),Neurotic::UiLiteral("desktop.option.e3dbb87412e9", "wininet.dll"),Neurotic::UiLiteral("desktop.option.bf5d99b5c9ae", "winhttp.dll"),Neurotic::UiLiteral("desktop.option.fd4503a9892c", "OptiScaler.asi"),Neurotic::UiLiteral("desktop.option.5990097c3bc5", "OptiScaler.dll")};for(int i=0;i<9;++i)if(proxy==names[i])return i;return 0;}
inline std::string ProxyRecommendationLabel(const Game& game,const std::string& proxy){
 auto row=GameRecommendation(game);std::string apis;if(row.is_null())return {};
 for(const auto& profile:row["profiles"]){auto api=profile.value("api","");if((api=="vulkan"?"winmm.dll":profile.value("proxy",""))!=proxy)continue;if(!apis.empty())apis+=" / ";apis+=api=="vulkan"?"Vulkan":api=="d3d12"?"DX12":api=="d3d11"?"DX11":api;}
 return apis.empty()?std::string{}:Neurotic::UiLiteral("desktop.gamecatalog.recommended_for_ed693cc7", "Recommended for ")+apis;
}

}
