#pragma once

#include <imgui/imgui.h>
#include <algorithm>

namespace DlssNr::MenuControls
{
inline ImVec4 EmphasizedToggleStateColor(bool enabled)
{
    return enabled ? ImVec4(0.25f, 0.90f, 0.38f, 1.0f) : ImVec4(0.95f, 0.25f, 0.22f, 1.0f);
}

inline ImVec4 EmphasizedToggleFocusColor()
{
    return ImVec4(1.0f, 0.72f, 0.18f, 1.0f);
}

// Preserve the ordinary checkbox ID and navigation semantics, then add a strong state label and
// outline around the resulting group. Colour is reinforced by explicit ON/OFF text.
inline bool EmphasizedCheckbox(const char* label, bool* value)
{
    const ImVec4 state = EmphasizedToggleStateColor(*value);
    const ImVec4 background(state.x, state.y, state.z, 0.16f);
    const auto& style = ImGui::GetStyle();

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, state);
    ImGui::PushStyleColor(ImGuiCol_Text, state);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, state);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, background);
    const bool changed = ImGui::Checkbox(label, value);
    const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextColored(state, "%s", *value ? "ON" : "OFF");
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    const bool highlighted = focused || ImGui::IsItemHovered();
    const ImVec4 outline = highlighted ? EmphasizedToggleFocusColor() : state;
    const ImVec2 padding((std::max)(2.0f, style.FramePadding.x * 0.5f),
                         (std::max)(1.0f, style.FramePadding.y * 0.25f));
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - padding,
                                        ImGui::GetItemRectMax() + padding,
                                        ImGui::GetColorU32(outline), style.FrameRounding,
                                        ImDrawFlags_None, highlighted ? 2.0f : 1.0f);
    return changed;
}

inline float CheckboxWithHelpWidth(const char* label)
{
    const auto& style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x +
           style.ItemSpacing.x + ImGui::CalcTextSize("(?)").x;
}

inline bool LastItemHasInlineRoom(float requiredWidth)
{
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    return right - ImGui::GetItemRectMax().x >= ImGui::GetStyle().ItemSpacing.x + requiredWidth;
}

inline float ResponsiveBasicResolutionWidth(float available, float menuScale,
                                            float resetWidth, float spacing,
                                            float preferredWidth = 320.0f)
{
    // Keep the complete slider/reset row in the left three quarters of the
    // panel. OptiClip owns the lower-right corner and must never cover either
    // control, even at narrow widths or large UI scales.
    const float safeWidth = (std::max)(1.0f, available * 0.75f - resetWidth - spacing);
    return (std::min)(preferredWidth * menuScale, safeWidth);
}

inline float CumulativeStrengthWidthFraction(unsigned int maximumPasses)
{
    return (std::min)((std::max)(maximumPasses, 1u), 4u) / 4.0f;
}

inline float ResponsiveCumulativeStrengthWidth(float available, float resetWidth,
                                               float spacing, unsigned int maximumPasses)
{
    const float safeFullWidth = (std::max)(1.0f, available - resetWidth - spacing);
    const float optiClipSafeWidth =
        (std::max)(1.0f, available * 0.75f - resetWidth - spacing);
    return (std::min)(safeFullWidth * CumulativeStrengthWidthFraction(maximumPasses),
                      optiClipSafeWidth);
}

inline float ResponsiveMultipassControlWidth(float available, float menuScale,
                                             float resetWidth, float spacing,
                                             float preferredWidth = 440.0f)
{
    const float optiClipSafeWidth =
        (std::max)(1.0f, available * 0.75f - resetWidth - spacing);
    return (std::min)(preferredWidth * menuScale, optiClipSafeWidth);
}
}
