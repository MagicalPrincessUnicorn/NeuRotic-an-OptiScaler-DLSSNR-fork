#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
namespace nh {
struct LaunchPlan {bool allowed=false,platform=false;std::wstring file,directory;};
inline bool ValidSteamAppId(const std::string& id){
 if(id.empty()||id.size()>10)return false;uint64_t value=0;
 for(unsigned char c:id){if(c<'0'||c>'9')return false;value=value*10+(c-'0');}
 return value>0&&value<=UINT32_MAX;
}
template<class Game> LaunchPlan MakeLaunchPlan(const Game& game){
 if(game.store=="Steam")return ValidSteamAppId(game.storeId)?LaunchPlan{true,true,L"steam://rungameid/"+std::wstring(game.storeId.begin(),game.storeId.end()),{}}:LaunchPlan{};
 if(!game.target.suitable||game.target.path.empty())return {};
 auto executable=std::filesystem::path(Wide(game.target.path));
 return {true,false,executable.wstring(),executable.parent_path().wstring()};
}
struct WindowBounds {int x,y,width,height;};
inline WindowBounds FitHubWindow(int left,int top,int width,int height,float dpi,bool compact=false){
 dpi=std::clamp(dpi,0.5f,8.f);int w=std::min(width,int((compact?880:1240)*dpi));int h=std::min(height,int((compact?600:960)*dpi));
 return {left+(width-w)/2,top+(height-h)/2,w,h};
}
}
