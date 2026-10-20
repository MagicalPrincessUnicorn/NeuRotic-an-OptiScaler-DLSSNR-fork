#include "ui/KeybindCapture.h"
#include "imgui_internal.h"
#include <iostream>
#include <cstdlib>
#include <windows.h>
#include <werapi.h>
#pragma comment(lib,"wer.lib")
#ifdef IMGUI_ENABLE_TEST_ENGINE
#include <map>
static std::map<std::string,ImRect> keybindRects;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID,const ImRect&,const ImGuiLastItemData*){}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(ctx->LastItemData.ID==id)keybindRects[label]=ctx->LastItemData.Rect;}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}
#endif
namespace {
struct Field {
 std::string section="Menu",key="ShortcutKey",type="keycode",label="Menu shortcut",original="0x2D";
 std::array<char,128> draft{};
};
}
int RunKeybindUiTests(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 WerSetFlags(WER_FAULT_REPORTING_NO_UI);
 std::cout<<std::unitbuf;_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto previous=ImGui::GetCurrentContext();auto context=ImGui::CreateContext();auto& io=ImGui::GetIO();
 io.IniFilename=nullptr;io.DisplaySize={640,480};io.DeltaTime=1.f/60.f;io.Fonts->AddFontDefault();
 io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 std::vector<Field> fields(2);std::strcpy(fields[0].draft.data(),"0x2D");fields[1].key="FpsShortcutKey";fields[1].label="FPS shortcut";std::strcpy(fields[1].draft.data(),"119");
 nh::KeybindCaptureState capture;ImVec2 button{},modifierButton{},resetButton{},lastMin{},lastMax{};float windowWidth=500;std::string text;
 auto frame=[&](const std::string& scope="game-a"){
#ifdef IMGUI_ENABLE_TEST_ENGINE
  keybindRects.clear();GImGui->TestEngineHookItems=true;
#endif
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({windowWidth,400});ImGui::Begin("Keybind test");
  auto origin=ImGui::GetCursorScreenPos();origin.y+=ImGui::GetTextLineHeightWithSpacing();
  modifierButton={origin.x+30,origin.y+10};button={origin.x+ImGui::GetFontSize()*11+ImGui::CalcTextSize(" + ").x+ImGui::GetStyle().ItemSpacing.x*2,origin.y};
  resetButton={button.x+ImGui::GetFontSize()*13+ImGui::GetStyle().ItemSpacing.x+15,button.y+10};
  ImGui::LogToBuffer();nh::RenderKeybindCapture(fields[0],fields,capture,scope);text=GImGui->LogBuffer.c_str();ImGui::LogFinish();
  lastMin=ImGui::GetItemRectMin();lastMax=ImGui::GetItemRectMax();ImGui::End();ImGui::Render();
 };
 frame();frame();frame();check(std::string(fields[0].draft.data())=="0x2D","rendering legacy shortcut preserves exact original token");
 check(text.find("Modifier")!=text.npos&&text.find("Keybind")!=text.npos&&text.find("Reset")!=text.npos&&text.find("Ctrl / Shift / Alt")==text.npos,"editor exposes Modifier, Keybind and Reset without obsolete help");
 check(text.find(" + ")!=text.npos,"desktop shortcut row visibly separates modifier and key with plus");
 io.DisplaySize={1800,1100};windowWidth=1600;frame();frame();check(lastMax.x<button.x+500,"maximized keybind stays bounded with Modifier and Reset on the same row");windowWidth=500;frame();
 auto click=[&](ImVec2 where){io.AddMousePosEvent(where.x,where.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
 click({button.x+30,button.y+10});check(capture.listening&&std::string(fields[0].draft.data())=="0x2D","click actual field begins listening without editing draft");
 io.AddKeyEvent(ImGuiKey_F8,true);frame();check(!capture.listening&&std::string(fields[0].draft.data())=="119","actual focused input captures F8 into draft");
 check(fields[0].original=="0x2D"&&std::string(fields[1].draft.data())=="119","capture preserves original setting and duplicate field");
 io.AddKeyEvent(ImGuiKey_F8,false);io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);frame();
 click({button.x+30,button.y+10});io.AddKeyEvent(ImGuiKey_F8,true);frame();
 check(std::string(fields[0].draft.data())=="119","key capture preserves separately selected None despite held Ctrl");
 io.AddKeyEvent(ImGuiKey_F8,false);io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();frame();
 click(modifierButton);frame();frame();
 check(text.find("Ctrl + Shift")!=text.npos&&text.find("Ctrl + Alt")!=text.npos&&text.find("Windows")==text.npos,"modifier dropdown exposes allowed combinations");
 auto popup=ImGui::FindWindowByName("##Combo_00");
#ifdef IMGUI_ENABLE_TEST_ENGINE
 if(keybindRects.contains("Ctrl"))click(keybindRects["Ctrl"].GetCenter());
#else
 // Use the open combo's actual navigation rather than guessing its font-dependent row height.
 io.AddKeyEvent(ImGuiKey_Home,true);frame();io.AddKeyEvent(ImGuiKey_Home,false);frame();
 io.AddKeyEvent(ImGuiKey_DownArrow,true);frame();io.AddKeyEvent(ImGuiKey_DownArrow,false);frame();
 io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();frame();
#endif
 check(std::string(fields[0].draft.data())=="375","actual Control selector updates packed shortcut while retaining F8");
 io.AddKeyEvent(ImGuiKey_F8,false);frame();click({button.x+30,button.y+10});io.AddKeyEvent(ImGuiKey_F9,true);frame();
 check(std::string(fields[0].draft.data())=="376","key capture retains Control chosen in independent selector");
 io.AddKeyEvent(ImGuiKey_F9,false);frame();click({button.x+30,button.y+10});io.AddKeyEvent(ImGuiKey_Backspace,true);frame();
 check(std::string(fields[0].draft.data())=="-1","Backspace clears keybind");io.AddKeyEvent(ImGuiKey_Backspace,false);frame();
 click(resetButton);check(std::string(fields[0].draft.data())=="auto","actual Reset restores automatic default key and None modifier");
 io.AddKeyEvent(ImGuiKey_F8,false);frame();click({button.x+30,button.y+10});io.AddKeyEvent(ImGuiKey_Escape,true);frame();
 check(!capture.listening&&std::string(fields[0].draft.data())=="auto","actual Escape input cancels without editing draft");
 io.AddKeyEvent(ImGuiKey_Escape,false);frame();click({button.x+30,button.y+10});frame("game-b");check(!capture.listening,"selected game change cancels actual rendered capture");
 click({button.x+30,button.y+10});io.AddFocusEvent(false);frame("game-a");check(!capture.listening,"application blur cancels actual rendered capture");
 ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);return failed;
}
#ifdef NH_KEYBIND_UI_STANDALONE
int main(){return RunKeybindUiTests();}
#endif
