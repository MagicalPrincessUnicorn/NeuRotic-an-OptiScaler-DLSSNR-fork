#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
namespace Neurotic::KeyChord {
inline constexpr int Ctrl=0x100,Shift=0x200,Alt=0x400,ModifierMask=0x700,Auto=-2;
inline bool IsModifier(int key){return key==16||key==17||key==18||(key>=160&&key<=165);}
inline bool IsOrdinary(int key){
 return key==8||key==9||key==13||key==19||key==20||key==27||(key>=32&&key<=40)||key==44||key==45||key==46||
 (key>=48&&key<=57)||(key>=65&&key<=90)||key==93||(key>=96&&key<=111)||(key>=112&&key<=135)||
 key==144||key==145||(key>=166&&key<=183)||(key>=186&&key<=192)||(key>=219&&key<=223)||key==226;
}
inline bool Valid(int value){return value==Auto||value==-1||(value>=0&&value<=255)||
 (value>=256&&value<=0x7ff&&(value&ModifierMask)!=0&&IsOrdinary(value&255));}
inline std::optional<int> Parse(std::string_view text){
 if(text=="auto")return Auto;
 int base=10;if(text.size()>2&&text[0]=='0'&&(text[1]=='x'||text[1]=='X')){base=16;text.remove_prefix(2);}
 int value=0;auto result=std::from_chars(text.data(),text.data()+text.size(),value,base);
 if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!Valid(value)||value==Auto)return {};
 return value;
}
inline bool Matches(int binding,int pressed){
 if(!Valid(binding)||binding<=0||pressed<=0)return false;
 return (binding&255)==(pressed&255)&&((binding&ModifierMask)==0||binding==pressed);
}
inline bool Conflicts(int first,int second){return first>0&&second>0&&(Matches(first,second)||Matches(second,first));}
inline std::string BaseLabel(int key){
 if(key>=65&&key<=90)return std::string(1,(char)key);
 if(key>=48&&key<=57)return std::string(1,(char)key);
 if(key>=112&&key<=135)return "F"+std::to_string(key-111);
 if(key>=96&&key<=105)return "Numpad "+std::to_string(key-96);
 switch(key){
 case 0:return "Disabled";case 1:return "Mouse left";case 2:return "Mouse right";case 4:return "Mouse middle";
 case 5:return "Mouse X1";case 6:return "Mouse X2";
 case 8:return "Backspace";case 9:return "Tab";case 13:return "Enter";case 16:return "Shift";case 17:return "Ctrl";case 18:return "Alt";
 case 19:return "Pause";case 20:return "Caps Lock";case 27:return "Escape";case 32:return "Space";case 33:return "Page Up";case 34:return "Page Down";
 case 35:return "End";case 36:return "Home";case 37:return "Left";case 38:return "Up";case 39:return "Right";case 40:return "Down";
 case 44:return "Print Screen";case 45:return "Insert";case 46:return "Delete";case 91:return "Left Windows";case 92:return "Right Windows";case 93:return "Menu";
 case 106:return "Numpad *";case 107:return "Numpad +";case 109:return "Numpad -";case 110:return "Numpad .";case 111:return "Numpad /";
 case 144:return "Num Lock";case 145:return "Scroll Lock";case 160:return "Left Shift";case 161:return "Right Shift";case 162:return "Left Ctrl";case 163:return "Right Ctrl";case 164:return "Left Alt";case 165:return "Right Alt";
 case 186:return ";";case 187:return "=";case 188:return ",";case 189:return "-";case 190:return ".";case 191:return "/";case 192:return "`";
 case 219:return "[";case 220:return "\\";case 221:return "]";case 222:return "'";case 226:return "OEM key";
 default:return "Saved key";
 }
}
inline std::string Label(int value){
 if(value==Auto)return "Default (auto)";if(value==-1)return "Unbound";if(!Valid(value))return "Unsupported saved shortcut";
 std::string label;if(value&Ctrl)label+="Ctrl + ";if(value&Shift)label+="Shift + ";if(value&Alt)label+="Alt + ";return label+BaseLabel(value&255);
}
// Snapshot modifiers at key-down, then match at release: releasing Ctrl before F8
// must not lose a Ctrl+F8 shortcut, or turn it into another chord.
struct ReleaseTracker {
 std::array<int,256> armed{};
 void Reset(){armed.fill(0);}
 std::optional<int> Observe(int key,int modifiers,bool pressed,bool released){
  if(key<=0||key>=256)return {};
  if(pressed&&!armed[key])armed[key]=key|(modifiers&ModifierMask);
  if(!released)return {};
  int chord=armed[key];armed[key]=0;if(!chord)return {};return chord;
 }
};
}
