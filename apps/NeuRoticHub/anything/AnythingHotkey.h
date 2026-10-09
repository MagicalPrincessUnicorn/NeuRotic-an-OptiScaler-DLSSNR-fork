#include <menu/Localization.h>
#pragma once
#include <windows.h>
#include "AnythingUiState.h"
namespace nh {
inline bool ValidAnythingHotkey(unsigned modifiers,unsigned key){
 const bool modifierChoice=modifiers==(MOD_CONTROL|MOD_ALT)||modifiers==(MOD_CONTROL|MOD_SHIFT)||modifiers==(MOD_ALT|MOD_SHIFT)||modifiers==(MOD_CONTROL|MOD_ALT|MOD_SHIFT);
 return modifierChoice&&((key>='A'&&key<='Z')||(key>=VK_F1&&key<=VK_F11))&&!(modifiers==(MOD_CONTROL|MOD_ALT)&&key==VK_F10);
}
inline std::string AnythingHotkeyText(unsigned modifiers,unsigned key){
 std::string label;if(modifiers&MOD_CONTROL)label+="Ctrl + ";if(modifiers&MOD_ALT)label+="Alt + ";if(modifiers&MOD_SHIFT)label+="Shift + ";
 return label+(key>=VK_F1&&key<=VK_F11?"F"+std::to_string(key-VK_F1+1):std::string(1,char(key)));
}
// Window-thread owner; MOD_NOREPEAT prevents a held shortcut from starting and stopping repeatedly.
class AnythingHotkey {
 struct Binding {bool enabled=false;unsigned modifiers=0,key=0,revision=0;bool operator==(const Binding&)const=default;};
 HWND window_=nullptr;bool initialized_=false;
 std::array<Binding,2> bindings_{};std::array<bool,2> registered_{};
 bool AcceptBinding(size_t index,WPARAM id,LPARAM event,const Binding& current,bool available)const{
  const auto& bound=bindings_[index];
  return registered_[index]&&available&&current.enabled&&current==bound&&id==(index?ScreenshotId:Id)&&LOWORD(event)==bound.modifiers&&HIWORD(event)==bound.key;
 }
public:
 static constexpr int Id=0x4e52;
 static constexpr int ScreenshotId=0x4e53;
 AnythingHotkey()=default;AnythingHotkey(const AnythingHotkey&)=delete;AnythingHotkey& operator=(const AnythingHotkey&)=delete;
 ~AnythingHotkey(){if(registered_[0])UnregisterHotKey(window_,Id);if(registered_[1])UnregisterHotKey(window_,ScreenshotId);}
 void Update(HWND window,AnythingUiState& page){
  if(!page.preferencesLoaded)return;
  const std::array<Binding,2> requested={Binding{page.hotkeyEnabled,page.hotkeyModifiers,page.hotkeyKey,page.hotkeyRevision},Binding{page.screenshotHotkeyEnabled,page.screenshotHotkeyModifiers,page.screenshotHotkeyKey,page.screenshotHotkeyRevision}};
  if(initialized_&&window_==window&&requested==bindings_)return;
  // Release both before registering either, so swapping bindings never collides
  // with our own old registration. Rendering wins an explicitly duplicate pair.
  if(registered_[0])UnregisterHotKey(window_,Id);if(registered_[1])UnregisterHotKey(window_,ScreenshotId);
  registered_={};initialized_=true;window_=window;bindings_=requested;
  auto install=[&](size_t index,bool& available,std::string& error){
   available=false;error.clear();const auto& binding=bindings_[index];if(!binding.enabled)return;
   if(!ValidAnythingHotkey(binding.modifiers,binding.key)){error=Neurotic::UiLiteral("desktop.anythinghotkey.choose_a_supported_shortcut_ctrl_alt_f10_is_rese_a12f6e78", "Choose a supported shortcut. Ctrl+Alt+F10 is reserved for emergency Stop.");return;}
   if(index&&bindings_[0].enabled&&bindings_[0].modifiers==binding.modifiers&&bindings_[0].key==binding.key){error=Neurotic::UiLiteral("desktop.anythinghotkey.choose_a_different_shortcut_from_rendering_fc9a5f5c", "Choose a different shortcut from Rendering.");return;}
   if(!RegisterHotKey(window_,index?ScreenshotId:Id,binding.modifiers|MOD_NOREPEAT,binding.key)){error=Neurotic::UiLiteral("desktop.anythinghotkey.shortcut_unavailable_windows_error_5e91d3f5", "Shortcut unavailable (Windows error ")+std::to_string(GetLastError())+Neurotic::UiLiteral("desktop.anythinghotkey.choose_another_binding_or_retry_1f9eb48c", "). Choose another binding or retry.");return;}
   registered_[index]=available=true;
  };
  install(0,page.hotkeyAvailable,page.hotkeyError);install(1,page.screenshotHotkeyAvailable,page.screenshotHotkeyError);
 }
 bool Accept(WPARAM id,LPARAM binding,const AnythingUiState& page)const{
  return page.preferencesLoaded&&AcceptBinding(0,id,binding,{page.hotkeyEnabled,page.hotkeyModifiers,page.hotkeyKey,page.hotkeyRevision},page.hotkeyAvailable);
 }
 bool AcceptScreenshot(WPARAM id,LPARAM binding,const AnythingUiState& page)const{
  if(page.hotkeyEnabled&&page.hotkeyModifiers==page.screenshotHotkeyModifiers&&page.hotkeyKey==page.screenshotHotkeyKey)return false;
  return page.preferencesLoaded&&AcceptBinding(1,id,binding,{page.screenshotHotkeyEnabled,page.screenshotHotkeyModifiers,page.screenshotHotkeyKey,page.screenshotHotkeyRevision},page.screenshotHotkeyAvailable);
 }
};
}
