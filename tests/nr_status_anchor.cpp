#include "dlssnr/DlssNr_StageControls.h"
#include "dlssnr/NrStatusPanel.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>

struct Geometry { float checkboxY, followingY; };
struct TooltipFrame { bool visible, focused; ImVec2 row; };
static TooltipFrame DrawTooltipFrame()
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({100, 100});
    ImGui::SetNextWindowSize({500, 400});
    ImGui::Begin("Tooltip interaction");
    DlssNr::StatusPanel::Line("##Details", "Full status detail\nSecond line", ImVec4(1,1,1,1));
    TooltipFrame result {false, ImGui::IsItemFocused(), ImGui::GetItemRectMin()};
    ImGui::Button("Other control");
    ImGui::End();
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        result.visible |= window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) != 0;
    ImGui::Render();
    return result;
}

static bool CheckTooltipInput()
{
    const auto check = [](bool condition, const char* description) {
        if (!condition) std::printf("FAIL: %s\n", description);
        return condition;
    };
    auto& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    const auto row = DrawTooltipFrame().row;
    io.AddMousePosEvent(row.x + 4, row.y + 4);
    DrawTooltipFrame();
    if (!check(DrawTooltipFrame().visible, "mouse hover shows status detail")) return false;
    io.AddMouseButtonEvent(0, true);
    DrawTooltipFrame();
    io.AddMouseButtonEvent(0, false);
    if (!check(DrawTooltipFrame().focused, "mouse click focuses status row")) return false;
    io.AddMousePosEvent(300, 350); // Still inside the overlay, clear of the status row.
    if (DrawTooltipFrame().visible)
    {
        std::puts("FAIL: mouse-clicked status tooltip remains visible after leaving its row");
        return false;
    }
    io.AddMousePosEvent(900, 600); // Outside the overlay must also dismiss it without another click.
    if (!check(!DrawTooltipFrame().visible, "outside mouse movement dismisses detail")) return false;
    io.AddMousePosEvent(row.x + 4, row.y + 4);
    if (!check(DrawTooltipFrame().visible, "returning to row shows detail again")) return false;
    io.AddMousePosEvent(900, 600);
    DrawTooltipFrame();
    const auto key = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true); DrawTooltipFrame();
        io.AddKeyEvent(key, false); return DrawTooltipFrame();
    };
    // First Tab takes over the mouse-focused row; a second Tab moves to the following control.
    if (!check(key(ImGuiKey_Tab).visible, "Tab opens detail for keyboard navigation")) return false;
    if (!check(!key(ImGuiKey_Tab).visible, "Tab leaves status row")) return false;
    const auto keyboard = key(ImGuiKey_UpArrow);
    if (!check(keyboard.focused && keyboard.visible, "keyboard navigation shows full detail")) return false;
    if (!check(!key(ImGuiKey_GamepadDpadDown).visible, "gamepad navigation leaves status row")) return false;
    const auto gamepad = key(ImGuiKey_GamepadDpadUp);
    if (!check(gamepad.focused && gamepad.visible, "gamepad navigation shows full detail")) return false;
    io.AddMousePosEvent(300, 350);
    if (!check(!DrawTooltipFrame().visible, "mouse resumes ownership after gamepad")) return false;
    std::puts("PASS: status tooltip dismisses after mouse click and departure; hover, keyboard and gamepad details remain available");
    return true;
}

static Geometry Draw(bool detailed, float width, float scale)
{
    ImGui::NewFrame();
    ImGui::PushFont(nullptr, 16.0f * scale);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, 700));
    ImGui::Begin("Status geometry", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushTextWrapPos(0.0f);
    bool enabled = true;
    DlssNr::MenuStatus::RuntimeStatus status;
    status.state = detailed ? DlssNr::MenuStatus::State::Recovering : DlssNr::MenuStatus::State::Active;
    if (detailed)
    {
        std::strcpy(status.reason.data(), "Waiting for the selected route. A long diagnostic must not move controls.\nOutput may have changed. This is a second diagnostic line.");
        std::strcpy(status.provider.data(), "Selected provider");
        status.realFrame = 900;
    }
    Geometry geometry {};
    DlssNr::StatusPanel::Header(230.0f * scale, [&]() {
        ImGui::Checkbox("Enable Neural Rendering", &enabled);
        geometry.checkboxY = ImGui::GetItemRectMin().y;
        ImGui::Checkbox("Apply the model", &enabled);
        if (detailed) ImGui::SmallButton("Retry");
    }, [&]() {
        // Exercise even an overflowing legacy report: it cannot resize the header.
        DlssNr::StageUi::RenderRuntimeStatus(status);
    });
    ImGui::Button("Next control");
    geometry.followingY = ImGui::GetItemRectMin().y;
    int row = 0;
    for (const auto& text : {std::string(), std::string("Active"), std::string(1000, 'W') + "\nnew line\r\n% text"})
    {
        ImGui::PushID(row++);
        DlssNr::StatusPanel::Line("##Readout", text, ImVec4(1, 1, 1, 1));
        if (std::abs(ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight()) > 0.5f)
            std::abort();
        ImGui::PopID();
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopFont();
    ImGui::Render();
    return geometry;
}
int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 1000);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    Neurotic::SetLanguage("de");
    if (DlssNr::StatusPanel::LocalizedLines(DlssNr::StatusPanel::Format("%s is active.", "Present")) !=
        "Pr\xc3\xa4sentation ist aktiv.") return 2;
    const auto metadata = DlssNr::StatusPanel::LocalizedLines(
        DlssNr::StatusPanel::BoundedDetail("Provider: %.*s", "example") + "\n" +
        DlssNr::StatusPanel::BoundedDetail("Route: %.*s", "Present") + "\n" +
        DlssNr::StatusPanel::BoundedDetail("Reason: %.*s", "Waiting") + "\nReal frame: 900");
    if (metadata.find("Provider:") != std::string::npos || metadata.find("Reason:") != std::string::npos ||
        metadata.find('\n') == std::string::npos) return 3;
    for (const auto* language : {"en", "de"})
    for (float scale : {0.75f, 1.0f, 1.5f, 2.0f})
    for (float width : {360.0f, 1000.0f})
    {
        Neurotic::SetLanguage(language);
        Draw(false, width, scale);
        const auto a = Draw(false, width, scale);
        for (int change = 0; change < 30; ++change)
        {
            const auto b = Draw(change % 2 != 0, width, scale);
            if (std::abs(a.followingY-b.followingY) > 0.5f || std::abs(a.checkboxY-b.checkboxY) > 0.5f)
            {
                std::printf("FAIL: changing status moved controls at width %.1f scale %.1f\n", width, scale);
                return 1;
            }
        }
    }
    if (!CheckTooltipInput()) return 5;
    ImGui::DestroyContext();
    std::puts("PASS: 480 status changes across narrow/wide layouts, four font sizes and English/German keep checkbox and following controls anchored; multiline/long/empty rows stay one line");
    std::puts("PASS: complete-line and metadata localization");
}
