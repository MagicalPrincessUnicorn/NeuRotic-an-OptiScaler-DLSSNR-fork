#include <menu/Localization.h>
#pragma once

#include <imgui/imgui.h>
#include <algorithm>
#include <initializer_list>
#include "menu/SleekUi.h"

namespace DlssNr::MenuControls
{
inline ImVec4 EmphasizedToggleStateColor(bool enabled)
{
    if (Neurotic::Sleek::Enabled())
        return enabled ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    return enabled ? ImVec4(0.25f, 0.90f, 0.38f, 1.0f) : ImVec4(0.95f, 0.25f, 0.22f, 1.0f);
}

inline ImVec4 EmphasizedToggleFocusColor()
{
    return Neurotic::Sleek::Enabled() ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) : ImVec4(1.0f, 0.72f, 0.18f, 1.0f);
}

inline void HighlightItem(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
                          bool focused = false)
{
    const auto& style = ImGui::GetStyle();
    if (Neurotic::Sleek::Enabled())
    {
        // Include the antialias fringe inside the selector rather than outside its hit area.
        const float inset=focused?1.5f:1.0f;
        draw->AddRect(min+ImVec2(inset,inset),max-ImVec2(inset,inset),
            ImGui::GetColorU32(EmphasizedToggleFocusColor()),style.FrameRounding,
            ImDrawFlags_None,focused?2.0f:1.0f);
        return;
    }
    const ImVec2 padding((std::max)(2.0f, style.FramePadding.x * 0.5f),
                         (std::max)(1.0f, style.FramePadding.y * 0.25f));
    draw->AddRect(min - padding, max + padding,
                  ImGui::GetColorU32(EmphasizedToggleFocusColor()), style.FrameRounding,
                  ImDrawFlags_None, focused ? 2.0f : 1.0f);
}

inline void HighlightLastItem(bool focused = false)
{
    HighlightItem(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), focused);
}

// Reserve the whole inline row before the native slider draws its label.
inline float InlineSliderWidth(const char* label)
{
    const auto& style=ImGui::GetStyle();
    const float tail=ImGui::CalcTextSize(label,nullptr,true).x+style.ItemInnerSpacing.x+
        ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x+style.FramePadding.x*2+
        ImGui::CalcTextSize("(?)").x+style.ItemSpacing.x*2;
    return (std::min)(ImGui::CalcItemWidth(),
        (std::max)(1.0f,ImGui::GetContentRegionAvail().x-tail));
}

// One track width for a group, including translated labels, Reset and help.
inline float HalfWidthSliderGroup(std::initializer_list<const char*> labels)
{
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    float width=ImGui::CalcItemWidth();
    for(const char* label : labels) width=(std::min)(width,InlineSliderWidth(label));
    ImGui::PopItemWidth();
    return width;
}

// Place a selector's caption at the far edge of its column, reserving room for help.
inline void AlignTrailingLabel(const char* label)
{
    ImGui::SameLine();
    const float current = ImGui::GetCursorPosX();
    const float right = current + ImGui::GetContentRegionAvail().x;
    const float captionWidth = ImGui::CalcTextSize(label).x +
        ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((std::max)(current, right - captionWidth));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

// The checkbox colour carries on/off state; its yellow outline marks a primary control.
inline float PrimaryCheckboxPadding() { return (std::max)(4.f, ImGui::GetStyle().FramePadding.x); }
inline float PrimaryCheckboxWidth(const char* label)
{
    return Neurotic::Sleek::ToggleWidth() + ImGui::GetStyle().ItemInnerSpacing.x +
        ImGui::CalcTextSize(label, nullptr, true).x + PrimaryCheckboxPadding() * 2;
}
inline bool PrimaryCheckbox(const char* label, bool* value)
{
    auto* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const auto& style = ImGui::GetStyle();
    const auto id = window->GetID(label);
    const float pad = PrimaryCheckboxPadding();
    const float toggle = Neurotic::Sleek::ToggleWidth();
    const float available = (std::max)(toggle + pad * 2 + style.ItemInnerSpacing.x + 1,
        ImGui::GetContentRegionAvail().x);
    const float width = (std::min)(PrimaryCheckboxWidth(label), available);
    const float wrap = width - toggle - pad * 2 - style.ItemInnerSpacing.x;
    const auto text = ImGui::CalcTextSize(label, nullptr, true, wrap);
    const ImRect bounds(window->DC.CursorPos, window->DC.CursorPos + ImVec2(width,
        (std::max)(ImGui::GetFrameHeight(), text.y + style.FramePadding.y * 2)));
    ImGui::ItemSize(bounds, style.FramePadding.y);
    if (!ImGui::ItemAdd(bounds, id)) return false;
    bool hovered = false, held = false;
    const bool changed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
    if (changed) { *value = !*value; ImGui::MarkItemEdited(id); }
    const auto accent = EmphasizedToggleFocusColor();
    auto* draw = window->DrawList;
    draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, .18f)), style.FrameRounding);
    const ImRect control(bounds.Min + ImVec2(pad, 0), bounds.Min + ImVec2(pad + toggle, ImGui::GetFrameHeight()));
    if (Neurotic::Sleek::Enabled()) Neurotic::Sleek::DrawSwitch(draw, control, id, *value, false, hovered, held);
    else {
        ImGui::RenderFrame(control.Min, control.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
        if (*value) ImGui::RenderCheckMark(draw, control.Min + ImVec2(4,4), ImGui::GetColorU32(ImGuiCol_CheckMark), ImGui::GetFrameHeight()-8);
    }
    ImGui::RenderTextWrapped({control.Max.x + style.ItemInnerSpacing.x, bounds.Min.y + style.FramePadding.y}, label, nullptr, wrap);
    HighlightItem(draw, bounds.Min, bounds.Max, true);
    ImGui::RenderNavCursor(bounds, id);
    return changed;
}
inline bool EmphasizedCheckbox(const char* label, bool* value, bool showStateText = true, bool primary = false)
{
    if (primary) return PrimaryCheckbox(label, value);
    const ImVec4 state = EmphasizedToggleStateColor(*value);
    const ImVec4 background(state.x, state.y, state.z, 0.16f);
    const auto& style = ImGui::GetStyle();

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, state);
    ImGui::PushStyleColor(ImGuiCol_Text, Neurotic::Sleek::Enabled() ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : state);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Neurotic::Sleek::Enabled() ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) : state);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, background);
    const bool changed = ImGui::Checkbox(label, value);
    const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
    if (showStateText)
    {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextColored(state, "%s",Neurotic::Translate(*value ? Neurotic::UiLiteral("ingame.dlssnr-menucontrols.on_0818b59f", "ON") : Neurotic::UiLiteral("ingame.dlssnr-menucontrols.off_aaedffb0", "OFF")).c_str());
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    if (!Neurotic::Sleek::Enabled() || focused) HighlightLastItem(focused);
    return changed;
}

inline float CheckboxWithHelpWidth(const char* label)
{
    const auto& style = ImGui::GetStyle();
    return Neurotic::Sleek::ToggleWidth() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x +
           style.ItemSpacing.x + ImGui::CalcTextSize("(?)").x;
}

inline bool LastItemHasInlineRoom(float requiredWidth)
{
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    return right - ImGui::GetItemRectMax().x >= ImGui::GetStyle().ItemSpacing.x + requiredWidth;
}

inline float ResponsiveMultipassControlWidth(float available, float menuScale,
                                             float resetWidth, float spacing,
                                             float preferredWidth = 440.0f)
{
    const float reservedWidth =
        (std::max)(1.0f, available * 0.75f - resetWidth - spacing);
    return (std::min)(preferredWidth * menuScale, reservedWidth);
}
}
