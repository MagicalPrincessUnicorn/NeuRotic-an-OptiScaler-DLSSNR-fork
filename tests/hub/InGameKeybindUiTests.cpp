#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "SynchronizedOptional.h"
#include "menu/input/HotkeyChord.h"
#include <concepts>
#include <cfloat>
#include <iostream>
#include <map>
#include <string>

constexpr int UnboundKey=-1;
int lastKey=0;bool capturingKey=false;
namespace OptiInput { void SetGameplayPolicy(bool,bool,bool,bool){} }
std::string wstring_to_string(const wchar_t* value){char buffer[256]{};WideCharToMultiByte(CP_UTF8,0,value,-1,buffer,sizeof(buffer),nullptr,nullptr);return buffer;}
namespace Neurotic::Sleek {float ButtonWidth(const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}}
struct Config {NrOptional<int> ScreenshotKey{UnboundKey};};
struct MenuCommon {static void RenderScreenshotKeybind(Config*);};
#include "../../builds/hub/hotkey-chord/in-game-keybind.inc"

static std::map<std::string,ImRect> rects;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx,ImGuiID id,const ImRect& rect,const ImGuiLastItemData*){auto* window=ctx->CurrentWindow;if(window&&window->IDStack.Size>0&&id&&id==ImGui::GetID("##Modifier"))rects["##Modifier"]=rect;}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(ctx->LastItemData.ID==id)rects[label]=ctx->LastItemData.Rect;}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}

int main()
{
    int failures=0;
    auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failures+=!ok;};
    ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1200,800};io.DeltaTime=1.f/60;
    io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    Keybind ordinary("Menu",10);CustomOptional<int> binding{VK_INSERT};Config config;
    bool screenshot=false,inSharedTable=false,balanced=true;std::string text;int rowDelta=0;
    auto frame=[&] {
        rects.clear();GImGui->TestEngineHookItems=true;
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1050,700});ImGui::Begin("Game keybind fixture");
        ImGui::LogToBuffer();
        if(screenshot&&!inSharedTable)MenuCommon::RenderScreenshotKeybind(&config);
        else if(ImGui::BeginTable("fixture",5,ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Action");ImGui::TableSetupColumn("Modifier");ImGui::TableSetupColumn("+");ImGui::TableSetupColumn("Keybind");ImGui::TableSetupColumn("Reset");
            const int before=ImGui::GetCurrentTable()->CurrentRow;
            if(screenshot)MenuCommon::RenderScreenshotKeybind(&config);else ordinary.Render(binding);
            rowDelta=ImGui::GetCurrentTable()->CurrentRow-before;
            ImGui::EndTable();
        }
        text=GImGui->LogBuffer.c_str();ImGui::LogFinish();
        balanced &= GImGui->CurrentTable==nullptr;
        ImGui::End();ImGui::Render();
    };
    auto click=[&](const std::string& label) {
        if(!rects.contains(label)){check(false, ("missing actual UI item "+label).c_str());return;}
        const auto point=rects[label].GetCenter();io.AddMousePosEvent(point.x,point.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
    };
    auto choose=[&](const char* label){click("##Modifier");click(label);};
    frame();frame();
    check(binding.value_or_default()==45&&!binding.has_value(),"viewing default keybind preserves unset default authority");
    for(const auto& c : {std::pair<const char*,int>{"None",45},{"Control",301},{"Alt",1069},{"Control + Shift",813},{"Control + Alt",1325}}) {
        choose(c.first);check(binding.value_or_default()==c.second,"actual modifier choices preserve Insert while recombining packed authority");
    }
    binding=557;frame();
    check(text.find("Custom")!=text.npos&&binding.value_or_default()==557,"legacy unsupported Shift binding displays and survives viewing");
    choose("Control");check(binding.value_or_default()==301,"explicit supported modifier edit replaces legacy Shift binding");
    click(Keybind::KeyNameFromVirtualKeyCode(45));
    lastKey=VK_LWIN;frame();check(capturingKey&&binding.value_or_default()==301,"Windows alone cannot finish in-game capture");
    lastKey=VK_SHIFT;frame();check(capturingKey&&binding.value_or_default()==301,"modifier alone cannot finish in-game capture");
    lastKey=VK_F8;frame();check(!capturingKey&&binding.value_or_default()==375,"ordinary key capture preserves separately chosen Control");
    click("Reset");check(binding.value_or_default()==45&&!binding.has_value(),"Reset restores original Insert and None without rewriting default");
    screenshot=true;frame();frame();
    choose("Control + Alt");check(config.ScreenshotKey.value_or_default()==-1,"selecting modifier before unbound screenshot key preserves unbound saved authority");
    click("Unbound");lastKey=VK_F9;frame();
    check(config.ScreenshotKey.value_or_default()==1400&&!capturingKey,"pending modifier applies when first screenshot key is captured");
    inSharedTable=true;frame();check(rowDelta==1&&config.ScreenshotKey.value_or_default()==1400,"same screenshot editor appends one five-column row to existing General table");
    click(Keybind::KeyNameFromVirtualKeyCode(120));lastKey=VK_BACK;frame();
    check(config.ScreenshotKey.value_or_default()==-1&&!capturingKey,"in-game Backspace clears screenshot binding");
    click("Reset");check(config.ScreenshotKey.value_or_default()==-1&&!config.ScreenshotKey.has_value(),"screenshot Reset restores unbound default");
    inSharedTable=false;frame();choose("Alt");click("Unbound");lastKey=VK_ESCAPE;frame();
    check(config.ScreenshotKey.value_or_default()==-1&&!capturingKey,"Escape cancels pending screenshot capture without changing saved key");
    check(balanced,"all binding frames leave table scopes balanced");
    ImGui::DestroyContext();return failures?1:0;
}
