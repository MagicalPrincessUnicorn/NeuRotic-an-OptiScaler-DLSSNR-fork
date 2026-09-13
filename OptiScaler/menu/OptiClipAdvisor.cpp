#include "pch.h"
#include "OptiClipAdvisor.h"
#include "Localization.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace OptiClip
{
namespace
{
Controller& Instance()
{
    static Controller controller;
    return controller;
}

void DrawMascot(ImDrawList* draw, ImVec2 topLeft, float scale, double now)
{
    // One continuous, nested double-loop wire: recognizable even without the face.
    // The solid tile gives the same contrast over a light UI or a bright game scene.
    draw->AddRectFilled(topLeft, {topLeft.x + 80 * scale, topLeft.y + 112 * scale},
                        IM_COL32(0, 0, 0, 255), 10 * scale);
    draw->AddRect(topLeft, {topLeft.x + 80 * scale, topLeft.y + 112 * scale},
                  IM_COL32(104, 112, 122, 255), 10 * scale, 0, scale);
    const float bob = std::sin(static_cast<float>(now * 2.1)) * 1.2f;
    const auto point = [&](float x, float y) {
        // A slight lean, with all animation contained inside the fixed tile.
        return ImVec2(topLeft.x + (x + (y - 55) * 0.07f) * scale,
                      topLeft.y + (y + bob) * scale);
    };
    const auto wire = [&](ImU32 colour, float thickness) {
        draw->PathLineTo(point(48, 39));
        draw->PathLineTo(point(48, 71));
        draw->PathBezierCubicCurveTo(point(48, 85), point(28, 85), point(28, 71));
        draw->PathLineTo(point(28, 26));
        draw->PathBezierCubicCurveTo(point(28, 7), point(58, 7), point(58, 26));
        draw->PathLineTo(point(58, 75));
        draw->PathBezierCubicCurveTo(point(58, 105), point(16, 105), point(16, 75));
        draw->PathLineTo(point(16, 36));
        draw->PathStroke(colour, 0, thickness * scale);
        draw->AddCircleFilled(point(48, 39), thickness * scale * 0.5f, colour);
        draw->AddCircleFilled(point(16, 36), thickness * scale * 0.5f, colour);
    };
    wire(IM_COL32(69, 79, 94, 255), 7.0f);
    wire(IM_COL32(193, 204, 218, 255), 4.8f);
    wire(IM_COL32(244, 248, 255, 255), 1.3f);
    const bool blink = std::fmod(now, 4.2) < 0.16;
    for (float x : {34.0f, 49.0f})
    {
        if (blink) draw->AddLine(point(x - 5, 29), point(x + 5, 29), IM_COL32(255, 255, 255, 255), 2 * scale);
        else
        {
            draw->AddCircleFilled(point(x, 29), 8 * scale, IM_COL32(32, 40, 52, 255), 24);
            draw->AddCircleFilled(point(x, 28.5f), 6.5f * scale, IM_COL32(255, 255, 255, 255), 24);
            draw->AddCircleFilled(point(x - 1.5f, 30), 3 * scale, IM_COL32(16, 25, 39, 255), 16);
            draw->AddCircleFilled(point(x - 2.3f, 28.7f), scale, IM_COL32(255, 255, 255, 255), 12);
        }
    }
    draw->AddLine(point(28, 18), point(37, 16), IM_COL32(220, 230, 240, 255), 2 * scale);
    draw->AddLine(point(46, 16), point(54, 18), IM_COL32(220, 230, 240, 255), 2 * scale);
    draw->AddBezierCubic(point(36, 44), point(39, 48), point(43, 48), point(46, 43),
                         IM_COL32(235, 240, 250, 255), 1.6f * scale);
}
} // namespace

void SetEnabled(bool enabled, double nowSeconds) { Instance().SetEnabled(enabled, nowSeconds); }
void Interaction(double nowSeconds) { Instance().Interaction(nowSeconds); }

void ReportRapidToggle(ToggleOrigin origin, const char* message, double nowSeconds)
{
    (void) origin;
    Instance().Report(Event::RapidToggle, nowSeconds, message);
}

void ObserveThreshold(const char* identity, Event event, double value, double boundary,
                      bool committed, double nowSeconds)
{
    Instance().ObserveThreshold(identity, event, value, boundary, committed, nowSeconds);
}

void Render(const Bounds& menu, float menuScale, double nowSeconds, bool enabled)
{
    auto& controller = Instance();
    controller.SetEnabled(enabled, nowSeconds);
    controller.Tick(nowSeconds, true);
    if (!enabled || menu.width <= 1.0f || menu.height <= 1.0f) return;

    const Message* message = controller.Active(nowSeconds);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 8.0f * menuScale;
    const float supportReserve = 34.0f * menuScale;
    const float maxRight = (std::min)(menu.x + menu.width - margin,
        viewport->WorkPos.x + viewport->WorkSize.x - margin);
    const float maxBottom = (std::min)(menu.y + menu.height - supportReserve,
        viewport->WorkPos.y + viewport->WorkSize.y - margin);
    const float availableWidth = maxRight - (std::max)(menu.x + margin, viewport->WorkPos.x + margin);
    const float availableHeight = maxBottom - (std::max)(menu.y + margin, viewport->WorkPos.y + margin);
    if (availableWidth <= 1 || availableHeight <= 1) return;
    const float avatarScale = (std::min)({menuScale, availableWidth / 80.0f, availableHeight / 112.0f});
    const float avatarWidth = 80 * avatarScale, avatarHeight = 112 * avatarScale;
    const float padding = 12 * menuScale;
    const float width = message ? (std::min)(480 * menuScale, availableWidth) : avatarWidth;
    const float textWidth = width - padding * 2;
    const float buttonHeight = ImGui::GetFrameHeight();
    const float dismissWidth = ImGui::CalcTextSize("Dismiss").x + ImGui::GetStyle().FramePadding.x * 2;
    // Tiny/clipped hosts retain the mascot; the current message can reappear when enlarged.
    const float bubbleAvailable = availableHeight - avatarHeight - padding;
    const bool showMessage = message && textWidth >= (std::max)(dismissWidth, ImGui::GetFontSize() * 5) &&
        bubbleAvailable >= buttonHeight + padding * 3 + ImGui::GetFontSize();
    const auto text = showMessage ? Neurotic::Translate(message->text) : std::string();
    const auto textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, (std::max)(1.0f, textWidth));
    const float bubbleHeight = showMessage ? (std::min)(bubbleAvailable,
        textSize.y + buttonHeight + padding * 3) : 0.0f;
    const float height = showMessage ? bubbleHeight + padding + avatarHeight : avatarHeight;

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos({ maxRight, maxBottom }, ImGuiCond_Always, { 1.0f, 1.0f });
    ImGui::SetNextWindowSize({ showMessage ? width : avatarWidth, height }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;
    if (!showMessage) flags |= ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##OptiClipAdvisor", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    // Focusing the host raises it above independent windows. Restore display order
    // without taking navigation focus, and keep active popups above the mascot.
    auto* window = ImGui::GetCurrentWindow();
    ImGui::BringWindowToDisplayFront(window);
    for (const auto& popup : ImGui::GetCurrentContext()->OpenPopupStack)
        if (popup.Window && popup.Window->Active)
        {
            ImGui::BringWindowToDisplayBehind(window, popup.Window);
            break;
        }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    if (showMessage)
    {
        draw->AddRectFilled(origin, {origin.x + width, origin.y + bubbleHeight},
                            ImGui::GetColorU32(ImGuiCol_WindowBg, 0.97f), 8.0f * menuScale);
        draw->AddRect(origin, {origin.x + width, origin.y + bubbleHeight},
                      ImGui::GetColorU32(ImGuiCol_Border), 8.0f * menuScale);
    }
    DrawMascot(draw, { maxRight - avatarWidth, maxBottom - avatarHeight }, avatarScale, nowSeconds);
    if (showMessage)
    {
        const float bodyHeight = (std::max)(1.0f, bubbleHeight - buttonHeight - padding * 3);
        ImGui::SetCursorScreenPos({origin.x + padding, origin.y + padding});
        // The full-width speech bubble remains above the bottom-right mascot, so neither
        // translated prose nor its dismiss control can cover settings beside OptiClip.
        if (ImGui::BeginChild("##OptiClipText", {textWidth, bodyHeight}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav))
        {
            ImGui::SetCursorPosY((std::max)(0.0f, (bodyHeight - textSize.y) * 0.5f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos({origin.x + padding, origin.y + bubbleHeight - buttonHeight - padding});
        if (ImGui::SmallButton("Dismiss##OptiClip")) controller.Dismiss(nowSeconds);
    }
    ImGui::End();
}
} // namespace OptiClip
