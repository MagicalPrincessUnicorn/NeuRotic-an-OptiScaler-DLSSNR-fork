#include "../../OptiScaler/include/KeyChord.h"
#include <iostream>
#include <optional>
#include <mutex>
constexpr int WM_SETFOCUS=7,WM_KILLFOCUS=8;
using HWND=void*;using UINT=unsigned;using DWORD=unsigned;using ULONG64=unsigned long long;
constexpr int VK_CONTROL=17,VK_LCONTROL=162,VK_RCONTROL=163,VK_SHIFT=16,VK_LSHIFT=160,VK_RSHIFT=161,VK_MENU=18,VK_LMENU=164,VK_RMENU=165,VK_ESCAPE=27;
struct Setting {int value=-1;int value_or_default() const{return value;}};
struct Config {
 Setting ShortcutKey,FpsShortcutKey,FGShortcutKey,FpsCycleShortcutKey,DlssNrToggleKey,ScreenshotKey,EscapeClosesMenu;
 static Config* Instance(){static Config config;return &config;}
};
namespace OptiInput {
 struct ButtonState {bool Down=false,Pressed=false,Released=false,BlockedDown=false;DWORD LastMessageTime=0;};
 struct InputState {bool Focused=true;std::uint64_t FocusGeneration=0;std::mutex Mutex;std::array<ButtonState,256> Keys{};int LastPressedKey=0;};InputState _state;
 struct Edges {bool ButtonState::*member;bool& operator[](int key){return _state.Keys[key].*member;}void fill(bool value){for(auto& key:_state.Keys)key.*member=value;}};
 bool& focus=_state.Focused;Edges down{&ButtonState::Down},press{&ButtonState::Pressed},release{&ButtonState::Released};
 void ResetButtonBlockedStateLocked(){}void ResetRawInputBlockStateLocked(){}void ResetRawInputSanitizeCacheLocked(){}
 void BeginFrame(HWND){}bool IsFocused(){return focus;}bool IsKeyDown(int key){return down[key];}
 bool IsKeyPressed(int key){return press[key];}bool IsKeyReleased(int key){return release[key];}int GetLastPressedKey(){return 0;}
 template<class... Args> bool HandleEscapeClose(Args...){return false;}
}
ULONG64 GetTickCount64(){return 100000;}
#define LOG_DEBUG(...)
bool inputMenu=false,inputFps=false,inputFG=false,inputFpsCycle=false,inputDlssNr=false,inputScreenshot=false,inputCloseMenu=false,capturingKey=false;
bool escapeClose=false,_isVisible=false;int lastKey=0;ULONG64 lastInputTick=0,debounceThreshold=50;
struct MenuCommon {static void UpdateManualInput(HWND);};
namespace ABI::Windows::UI::Core {
 struct ICoreWindow{};
 enum class CoreWindowActivationState {CoreWindowActivationState_Deactivated,CoreWindowActivationState_CodeActivated};
 struct IWindowActivatedEventArgs {CoreWindowActivationState state;void get_WindowActivationState(CoreWindowActivationState* value){*value=state;}};
}
struct ImGuiIO {bool queuedFocus=true;void AddFocusEvent(bool focused){queuedFocus=focused;}};
namespace ImGui {void* GetCurrentContext(){return reinterpret_cast<void*>(1);}ImGuiIO& GetIO(){static ImGuiIO io;return io;}}
using PFN_FocusChanged=void(*)(bool);PFN_FocusChanged _focusChangedMethod=nullptr;
#include "../../builds/hub/keybind-runtime-readers.inc"
int main(){
#ifdef NH_UWP_FOCUS_CALLBACK_AVAILABLE
 _focusChangedMethod=UwpShortcutFocusChanged;
#endif
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto clear=[](){inputMenu=inputFps=inputFG=inputFpsCycle=inputDlssNr=inputScreenshot=false;OptiInput::down.fill(false);OptiInput::press.fill(false);OptiInput::release.fill(false);};
 auto config=Config::Instance();
 Setting* settings[]={&config->ShortcutKey,&config->FpsShortcutKey,&config->FGShortcutKey,&config->FpsCycleShortcutKey,&config->DlssNrToggleKey,&config->ScreenshotKey};
 bool* flags[]={&inputMenu,&inputFps,&inputFG,&inputFpsCycle,&inputDlssNr,&inputScreenshot};
 for(int i=0;i<6;i++){
  for(auto setting:settings)setting->value=-1;settings[i]->value=375;clear();
  OptiInput::down[17]=OptiInput::down[119]=OptiInput::press[119]=true;MenuCommon::UpdateManualInput(nullptr);
  check(!*flags[i],"manual path arms on press without triggering action");
  OptiInput::press.fill(false);OptiInput::down[17]=false;OptiInput::release[17]=true;MenuCommon::UpdateManualInput(nullptr);
  OptiInput::release.fill(false);OptiInput::down[119]=false;OptiInput::release[119]=true;MenuCommon::UpdateManualInput(nullptr);
  check(*flags[i],"manual path all six shortcuts retain Ctrl when released before F8");
  clear();KeyDown(119,true,false,false);KeyUp(17);KeyDown(119,false,false,false);KeyUp(119);
  check(*flags[i],"UWP path all six shortcuts retain press chord across modifier release and repeat");
  clear();KeyDown(119,false,false,false);KeyUp(119);check(!*flags[i],"UWP path rejects unmatched plain key for chord binding");
 }
 struct Choice {int packed;bool ctrl,shift,alt;};
 for(int action=0;action<6;++action)for(const Choice choice : {Choice{119,false,false,false},Choice{375,true,false,false},Choice{1143,false,false,true},Choice{887,true,true,false},Choice{1399,true,false,true}}){
  for(auto setting:settings)setting->value=-1;settings[action]->value=choice.packed;clear();
  OptiInput::down[17]=choice.ctrl;OptiInput::down[16]=choice.shift;OptiInput::down[18]=choice.alt;
  OptiInput::down[119]=OptiInput::press[119]=true;MenuCommon::UpdateManualInput(nullptr);
  OptiInput::press.fill(false);OptiInput::down.fill(false);OptiInput::release[119]=true;MenuCommon::UpdateManualInput(nullptr);
  check(*flags[action],"actual manual consumer accepts each supported modifier choice for each of six actions");
  clear();KeyDown(119,choice.ctrl,choice.shift,choice.alt);KeyUp(119);
  check(*flags[action],"actual UWP consumer accepts each supported modifier choice for each of six actions");
  if(choice.packed!=119){clear();KeyDown(119,false,false,false);KeyUp(119);check(!*flags[action],"modified action refuses an unmodified UWP key");}
 }
 clear();config->ScreenshotKey.value=119;KeyDown(119,true,false,false);KeyUp(119);check(inputScreenshot,"legacy UWP plain key retains modifier-tolerant matching");
 clear();config->ScreenshotKey.value=375;KeyDown(119,true,false,false);OptiInput::focus=false;MenuCommon::UpdateManualInput(nullptr);OptiInput::focus=true;KeyUp(119);check(!inputScreenshot,"focus loss invalidates armed UWP chord");
 clear();OptiInput::focus=false;KeyDown(119,true,false,false);KeyUp(119);check(!inputScreenshot,"unfocused UWP callbacks cannot activate shortcut");OptiInput::focus=true;
 clear();capturingKey=true;KeyDown(119,true,false,false);KeyUp(119);check(!inputScreenshot,"in-game listening suppresses UWP shortcut action");capturingKey=false;
 using namespace ABI::Windows::UI::Core;
 IWindowActivatedEventArgs deactivated{CoreWindowActivationState::CoreWindowActivationState_Deactivated};
 IWindowActivatedEventArgs activated{CoreWindowActivationState::CoreWindowActivationState_CodeActivated};
 clear();KeyDown(119,true,false,false);WindowActivated(nullptr,&deactivated);WindowActivated(nullptr,&activated);KeyUp(119);
 check(!inputScreenshot,"UWP deactivate/reactivate callbacks invalidate press without a render tick");
 clear();WindowActivated(nullptr,&deactivated);KeyDown(119,true,false,false);KeyUp(119);
 check(!inputScreenshot,"deactivated UWP callbacks reject input before manual focus refresh");
 WindowActivated(nullptr,&activated);KeyDown(119,true,false,false);KeyUp(119);
 check(inputScreenshot,"new chord after UWP activation works without a render tick");
 clear();OptiInput::down[17]=OptiInput::down[119]=OptiInput::press[119]=true;MenuCommon::UpdateManualInput(nullptr);
 OptiInput::press.fill(false);OptiInput::FocusMessage(WM_KILLFOCUS);OptiInput::down.fill(false);OptiInput::FocusMessage(WM_SETFOCUS);
 OptiInput::release[119]=true;MenuCommon::UpdateManualInput(nullptr);
 check(!inputScreenshot,"actual Win32 blur/refocus messages invalidate armed chord without a render tick");
 clear();OptiInput::down[17]=OptiInput::down[119]=OptiInput::press[119]=true;MenuCommon::UpdateManualInput(nullptr);
 OptiInput::press.fill(false);OptiInput::down.fill(false);OptiInput::release[119]=true;MenuCommon::UpdateManualInput(nullptr);
 check(inputScreenshot,"new manual chord after focus generation change still fires");
 clear();MenuCommon::UpdateManualInput(nullptr);
 OptiInput::SetKeyDown(162,1,false);OptiInput::SetKeyDown(119,2,false);
 OptiInput::FocusMessage(WM_KILLFOCUS);
 check(OptiInput::down[162]&&OptiInput::down[119]&&!OptiInput::press[119],"focus edge reset preserves held keys while removing pending press");
 OptiInput::FocusMessage(WM_SETFOCUS);
 OptiInput::SetKeyUp(162,3);OptiInput::SetKeyUp(119,4);MenuCommon::UpdateManualInput(nullptr);
 check(!inputScreenshot,"actual key producers cannot rearm pending press across no-tick blur/refocus");
 clear();OptiInput::SetKeyDown(162,5,false);OptiInput::SetKeyDown(119,6,false);MenuCommon::UpdateManualInput(nullptr);
 OptiInput::press.fill(false);OptiInput::SetKeyUp(162,7);OptiInput::SetKeyUp(119,8);MenuCommon::UpdateManualInput(nullptr);
 check(inputScreenshot,"actual new keyboard press after focus boundary still activates chord");
 return failed;
}
