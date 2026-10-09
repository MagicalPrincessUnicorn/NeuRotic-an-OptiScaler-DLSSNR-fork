#include <menu/Localization.h>
#pragma once
#include "TranslationEditorWindow.h"
#include <array>
namespace nh {
class LanguageManagerView {
 LanguageStore store;TranslationEditorWindow editor;HWND owner;std::vector<InstalledLanguage> packs;std::string selected="en",status;bool packsLoaded=false,manage=false,adding=false;std::array<char,64> locale{};std::array<char,256> name{};Neurotic::Localization::PackValidation importPreview;bool previewPending=false;
 void EnsurePacks(){if(!packsLoaded){packs=store.List();packsLoaded=true;}}
 public:
 LanguageManagerView(HWND,ID3D11Device*,ID3D11DeviceContext*,std::filesystem::path userRoot);
 void Draw();void RenderEditor(bool light,float scale=0){editor.Render(light,scale);}bool RequestClose(){return editor.RequestClose();}
 bool EditorOpen()const{return editor.IsOpen();}TranslationEditorWindow& Editor(){return editor;}LanguageStore& Store(){return store;}
};
void SetLanguageManager(LanguageManagerView*);void RenderLanguageManager();
}
