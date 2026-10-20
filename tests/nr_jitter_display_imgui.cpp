#include "dlssnr/NrJitterDisplay.h"
#include "dlssnr/NrPreflightCell.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>

struct FrameResult { std::string hover; ImVec2 center; float height; };
static FrameResult Frame(DlssNr::JitterDisplay::Snapshot& display, float x, float y,
                         unsigned long long sequence, double now, ImVec2 mouse)
{
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(mouse.x, mouse.y);
    ImGui::GetCurrentContext()->Time = now - io.DeltaTime;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({180,160});
    ImGui::Begin("Jitter display");
    NrPreflightSignals::NativeInputs input;
    input.Record(true,x,true,y,false,0,false,0,false,0,false);
    input.observations = sequence;
    const auto& text = display.Update({1,2,0,3,4,5,6,7,8}, input, true, ImGui::GetTime());
    // The real clipped diagnostic cell supplies its full tooltip through TextUnformatted.
    ImGui::LogToBuffer();
    NrPreflightCell::Draw(text);
    const auto min = ImGui::GetItemRectMin(); const auto size = ImGui::GetItemRectSize();
    FrameResult result {ImGui::GetCurrentContext()->LogBuffer.c_str(),
                        {min.x+size.x*.5f, min.y+size.y*.5f}, size.y};
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    return result;
}
int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = {640,480}; io.DeltaTime = 1.0f/60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    DlssNr::JitterDisplay::Snapshot display;
    auto first = Frame(display,-.25f,.125f,1,10,{-100,-100});
    Frame(display,.5f,-.375f,2,10.01,first.center);
    const auto held = Frame(display,.5f,-.375f,3,11.99,first.center);
    const auto latest = Frame(display,.5f,-.375f,4,12,first.center);
    const auto again = Frame(display,-.25f,.125f,5,12.01,first.center);
    bool passed = held.hover.find("X -0.25, Y 0.125 (last Native DLSS input)") != std::string::npos &&
        latest.hover.find("X 0.5, Y -0.375 (last Native DLSS input)") != std::string::npos &&
        again.hover == latest.hover && std::abs(first.height-held.height)<.1f &&
        std::abs(first.height-latest.height)<.1f;
    if (!passed) std::cerr << "FAIL: actual jitter hover or geometry\nHeld: " << held.hover
        << "\nLatest: " << latest.hover << "\nAgain: " << again.hover << '\n';
    ImGui::DestroyContext();
    if (!passed) return 1;
    std::cout << "PASS: actual clipped ImGui Jitter cell and full hover retain one snapshot until two seconds; row geometry is stable\n";
}
