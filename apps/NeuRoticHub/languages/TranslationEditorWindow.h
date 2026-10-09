#pragma once
#include "LanguageStore.h"
#include "TranslationEditorModel.h"
#include <memory>
#include <windows.h>
struct ID3D11Device;struct ID3D11DeviceContext;struct IDXGISwapChain;
namespace nh {
std::filesystem::path ChooseLanguageFile(HWND owner,bool save,const wchar_t* suggested=L"Language.nrlang");
class TranslationEditorWindow {
 struct Impl;std::unique_ptr<Impl> impl;
 public:
 TranslationEditorWindow(HWND owner,ID3D11Device*,ID3D11DeviceContext*,LanguageStore&);~TranslationEditorWindow();
 bool Open(std::string_view packId);void Render(bool light,float scale=0);bool IsOpen()const;bool RequestClose();
 HWND Handle()const;TranslationEditorModel* Model();
 IDXGISwapChain* SwapChain()const;
};
}
