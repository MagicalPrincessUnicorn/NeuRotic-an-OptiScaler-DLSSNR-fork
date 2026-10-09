#include <menu/Localization.h>
#pragma once
#include "../../../OptiScaler/include/KeyChord.h"
#include "../../../OptiScaler/menu/input/HotkeyChord.h"
#include <cstring>
#include <map>
#include <vector>
namespace nh {
struct KeybindCaptureState {
 bool listening=false;std::string scope,field;std::array<bool,256> held{};
 std::map<std::string,int> pendingModifiers;
 void ResetListening(){listening=false;field.clear();held.fill(false);}
 void Scope(const std::string& selected){if(scope!=selected){ResetListening();pendingModifiers.clear();scope=selected;}}
 void Begin(const std::string& selected,const std::string& id,const std::array<bool,256>& down){Scope(selected);field=id;held=down;listening=true;}
 void Release(int key){if(key>=0&&key<256)held[key]=false;}
 std::optional<int> Press(int key,int modifiers){
  if(!listening||key<0||key>=256||held[key])return {};
  if(key==27){ResetListening();return {};}
  if(key==8){ResetListening();return -1;}
  if(modifiers&~Neurotic::KeyChord::ModifierMask)return {};
  if(!Neurotic::KeyChord::IsOrdinary(key)||Neurotic::KeyChord::IsModifier(key))return {};
  ResetListening();return key;
 }
};
}
#ifndef NH_KEYBIND_CORE_ONLY
#include "imgui.h"
namespace nh {
inline int KeybindVk(ImGuiKey key){
 if(key>=ImGuiKey_A&&key<=ImGuiKey_Z)return 65+key-ImGuiKey_A;
 if(key>=ImGuiKey_0&&key<=ImGuiKey_9)return 48+key-ImGuiKey_0;
 if(key>=ImGuiKey_F1&&key<=ImGuiKey_F24)return 112+key-ImGuiKey_F1;
 if(key>=ImGuiKey_Keypad0&&key<=ImGuiKey_Keypad9)return 96+key-ImGuiKey_Keypad0;
 switch(key){
 case ImGuiKey_Tab:return 9;case ImGuiKey_LeftArrow:return 37;case ImGuiKey_RightArrow:return 39;case ImGuiKey_UpArrow:return 38;case ImGuiKey_DownArrow:return 40;
 case ImGuiKey_PageUp:return 33;case ImGuiKey_PageDown:return 34;case ImGuiKey_Home:return 36;case ImGuiKey_End:return 35;case ImGuiKey_Insert:return 45;
 case ImGuiKey_Delete:return 46;case ImGuiKey_Backspace:return 8;case ImGuiKey_Space:return 32;case ImGuiKey_Enter:case ImGuiKey_KeypadEnter:return 13;
 case ImGuiKey_Escape:return 27;case ImGuiKey_LeftCtrl:return 162;case ImGuiKey_RightCtrl:return 163;case ImGuiKey_LeftShift:return 160;case ImGuiKey_RightShift:return 161;
 case ImGuiKey_LeftAlt:return 164;case ImGuiKey_RightAlt:return 165;case ImGuiKey_Menu:return 93;case ImGuiKey_Apostrophe:return 222;case ImGuiKey_Comma:return 188;
 case ImGuiKey_Minus:return 189;case ImGuiKey_Period:return 190;case ImGuiKey_Slash:return 191;case ImGuiKey_Semicolon:return 186;case ImGuiKey_Equal:return 187;
 case ImGuiKey_LeftBracket:return 219;case ImGuiKey_Backslash:return 220;case ImGuiKey_RightBracket:return 221;case ImGuiKey_GraveAccent:return 192;
 case ImGuiKey_CapsLock:return 20;case ImGuiKey_ScrollLock:return 145;case ImGuiKey_NumLock:return 144;case ImGuiKey_PrintScreen:return 44;case ImGuiKey_Pause:return 19;
 case ImGuiKey_KeypadDecimal:return 110;case ImGuiKey_KeypadDivide:return 111;case ImGuiKey_KeypadMultiply:return 106;case ImGuiKey_KeypadSubtract:return 109;case ImGuiKey_KeypadAdd:return 107;
 default:return 0;
 }
}
inline int KeybindDefault(const std::string& section,const std::string& key){
 if(section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")){if(key=="ShortcutKey")return 45;if(key=="FpsShortcutKey")return 33;if(key=="FpsCycleShortcutKey")return 34;if(key=="FGShortcutKey")return 35;}
 if(section==Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")&&key=="ToggleKey")return -1;
 if(section=="Screenshots"&&key==Neurotic::UiLiteral("desktop.anythingview.key_df9dc9c4", "Key"))return -1;return -1;
}
template<class Field> int EffectiveKeybind(const Field& field){
 auto parsed=Neurotic::KeyChord::Parse(field.draft.data());if(!parsed)return -1;return *parsed==Neurotic::KeyChord::Auto?KeybindDefault(field.section,field.key):*parsed;
}
template<class Field> bool RenderKeybindCapture(Field& field,const std::vector<Field>& all,KeybindCaptureState& capture,const std::string& gameScope){
 using namespace Neurotic::KeyChord;capture.Scope(gameScope);
 const auto id=field.section+"/"+field.key;ImGui::PushID(id.c_str());
 if(capture.listening&&(ImGui::GetIO().AppFocusLost||!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))capture.ResetListening();
 bool changed=false;auto set=[&](const std::string& value){std::memcpy(field.draft.data(),value.c_str(),value.size()+1);capture.ResetListening();changed=true;};
 const bool owner=capture.listening&&capture.field==id;
 auto parsed=Parse(field.draft.data());
 const int effective=EffectiveKeybind(field);
 int modifierIndex=Neurotic::HotkeyChord::ModifierIndex(effective);
 if(effective<=0)if(auto pending=capture.pendingModifiers.find(id);pending!=capture.pendingModifiers.end())modifierIndex=pending->second;
 const float modifierWidth=ImGui::GetFontSize()*11;
 const float separatorWidth=ImGui::CalcTextSize(" + ").x;
 const float actionsWidth=ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset")).x+ImGui::GetStyle().FramePadding.x*2;
 const float bindingWidth=std::max(ImGui::GetFontSize()*6,std::min(ImGui::GetFontSize()*13,ImGui::GetContentRegionAvail().x-modifierWidth-separatorWidth-actionsWidth-ImGui::GetStyle().ItemSpacing.x*3));
 const float headerX=ImGui::GetCursorPosX();
 ImGui::TextDisabled(Neurotic::UiLiteral("desktop.keybindcapture.modifier_04519907", "Modifier"));ImGui::SameLine(headerX+modifierWidth+separatorWidth+ImGui::GetStyle().ItemSpacing.x*2);ImGui::TextDisabled(Neurotic::UiLiteral("desktop.keybindcapture.keybind_e08bbdb3", "Keybind"));
 ImGui::SetNextItemWidth(modifierWidth);
 const auto modifierLabel=modifierIndex>=0?Neurotic::HotkeyChord::ModifierLabels[modifierIndex]:Neurotic::UiLiteral("desktop.keybindcapture.saved_modifier_db4769a7", "Saved modifier");
 if(ImGui::BeginCombo("##modifier",modifierLabel)){
  for(size_t i=0;i<Neurotic::HotkeyChord::ModifierValues.size();++i){
   if(ImGui::Selectable(Neurotic::HotkeyChord::ModifierLabels[i],modifierIndex==static_cast<int>(i))){
    const int next=Neurotic::HotkeyChord::WithModifier(effective,Neurotic::HotkeyChord::ModifierValues[i]);
    if(effective>0){set(std::to_string(next));capture.pendingModifiers.erase(id);}
    else{capture.pendingModifiers[id]=static_cast<int>(i);capture.ResetListening();}
    modifierIndex=static_cast<int>(i);
   }
  }
  ImGui::EndCombo();
 }
 ImGui::SameLine();ImGui::TextDisabled(" + ");ImGui::SameLine();
 auto label=owner?std::string(Neurotic::UiLiteral("desktop.keybindcapture.waiting_for_input_e27fd308", "Waiting for input...")):parsed?(effective>0?BaseLabel(effective&255):Label(effective)):std::string(Neurotic::UiLiteral("desktop.keybindcapture.unsupported_saved_shortcut_f93f2dbb", "Unsupported saved shortcut"));
 if(parsed&&*parsed==Auto)label="Default ("+(effective>0?BaseLabel(effective&255):Label(effective))+")";
 if(ImGui::Button((label+"##binding").c_str(),ImVec2(bindingWidth,0))){
  std::array<bool,256> held{};for(int k=ImGuiKey_NamedKey_BEGIN;k<ImGuiKey_NamedKey_END;k++){int vk=KeybindVk((ImGuiKey)k);if(vk)held[vk]=held[vk]||ImGui::IsKeyDown((ImGuiKey)k);}
  capture.Begin(gameScope,id,held);
 }
 if(owner){
  if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGui::IsItemHovered())capture.ResetListening();
  const auto& io=ImGui::GetIO();const int modifiers=(io.KeyCtrl?Ctrl:0)|(io.KeyShift?Shift:0)|(io.KeyAlt?Alt:0)|(io.KeySuper?0x800:0);
  for(int k=ImGuiKey_NamedKey_BEGIN;k<ImGuiKey_NamedKey_END&&capture.listening;k++){
   int vk=KeybindVk((ImGuiKey)k);if(!vk)continue;
   if(!ImGui::IsKeyDown((ImGuiKey)k))capture.Release(vk);
   if(ImGui::IsKeyPressed((ImGuiKey)k,false))if(auto value=capture.Press(vk,modifiers)){
    int next=*value;
    if(next>0){next=Neurotic::HotkeyChord::WithKey(effective,next);if(modifierIndex>=0)next=Neurotic::HotkeyChord::WithModifier(next,Neurotic::HotkeyChord::ModifierValues[modifierIndex]);}
    set(std::to_string(next));capture.pendingModifiers.erase(id);
   }
  }
 }
 ImGui::SameLine();if(ImGui::Button(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset"))){set(Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"));capture.pendingModifiers.erase(id);}
 if(capture.listening&&capture.field==id){ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel")))capture.ResetListening();}
 for(const auto& other:all){if(other.type!="keycode"||(other.section==field.section&&other.key==field.key))continue;
  if(Conflicts(EffectiveKeybind(field),EffectiveKeybind(other)))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.keybindcapture.shortcut_conflict_with_s_choose_another_binding__48462998", "Shortcut conflict with %s. Choose another binding before saving."),Neurotic::Translate(other.label.c_str()).c_str());
 }
 ImGui::PopID();return changed;
}
}
#endif
