#include <menu/Localization.h>
#pragma once
#include "imgui.h"
#include <windows.h>
#include <filesystem>
#include <cstring>
namespace nh::ui {
inline void AddActionFont(ImFontAtlas* atlas,float size){
 wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return;
 auto path=std::filesystem::path(windows)/L"Fonts/segoeuib.ttf";
 ImFontConfig config;strcpy_s(config.Name,"NeuRotic Bold");
 if(std::filesystem::is_regular_file(path))atlas->AddFontFromFileTTF(path.string().c_str(),size,&config);
}
inline ImFont* ActionFont(){
 for(auto* font:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(font->GetDebugName(),"NeuRotic Bold")==0)return font;
 return ImGui::GetFont();
}
}
