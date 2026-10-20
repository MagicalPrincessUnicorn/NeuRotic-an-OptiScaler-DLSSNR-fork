#include "dlssnr/NrPreflightCell.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

static ImVec2 centers[3];
static bool Frame(ImVec2 mouse = {-100, -100}, int hover = -1)
{
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(mouse.x, mouse.y);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImVec2(310, 300));
    ImGui::Begin("Preflight cell test");
    bool stable = true;
    if (ImGui::BeginTable("Preflight", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::PushTextWrapPos(0.0f); // Parent wrapping must not affect diagnostic cells.
        const char* values[] = {"Ready", "No enabled native DLSSG observation; other providers are not observed.",
                                "X -0.3333, Y 0.125\n(last Native DLSS input)"};
        for (int i = 0; i < 3; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            const float width = ImGui::GetContentRegionAvail().x;
            NrPreflightCell::Draw(values[i]);
            const auto size = ImGui::GetItemRectSize();
            const auto begin = ImGui::GetItemRectMin();
            centers[i] = {begin.x + size.x * .5f, begin.y + size.y * .5f};
            stable &= std::abs(size.y - ImGui::GetTextLineHeight()) < .1f && size.x <= width + .1f;
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTable();
    }
    ImGui::End();
    bool tooltip = false;
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        tooltip |= window->Active && (window->Flags & ImGuiWindowFlags_Tooltip);
    if (hover >= 0) stable &= tooltip == (hover > 0);
    ImGui::Render();
    return stable;
}

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = {640, 480};
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    bool passed = Frame() && Frame();
    for (int i : {1, 0, 2})
    {
        Frame(centers[i]); // Establish hovered window after mouse movement.
        passed &= Frame(centers[i], i);
    }
    ImGui::DestroyContext();
    if (!passed)
    {
        std::cerr << "FAIL: diagnostic values changed row geometry or hover did not match clipping\n";
        return 1;
    }
    std::cout << "PASS: diagnostic cells remain one row at allotted width; only clipped values open hover text\n";
}
