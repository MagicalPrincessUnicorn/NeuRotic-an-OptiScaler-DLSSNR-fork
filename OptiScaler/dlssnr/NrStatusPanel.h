#pragma once
#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <menu/SleekUi.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace DlssNr::StatusPanel
{
inline std::string LocalizedLines(std::string_view text)
{
    std::string result;
    size_t begin = 0;
    while (begin < text.size())
    {
        const auto end = text.find_first_of("\r\n", begin);
        if (end == std::string_view::npos)
        {
            result += Neurotic::Translate(text.substr(begin));
            break;
        }
        result += Neurotic::Translate(text.substr(begin, end - begin));
        result += text[end];
        begin = end + 1;
    }
    return result;
}

template<class... Args> std::string Format(const char* format, Args... args)
{
    Neurotic::LocalizedFormat localized(format);
    const int count = std::snprintf(nullptr, 0, format, args...);
    if (count < 0) return {};
    std::string text(static_cast<size_t>(count) + 1, '\0');
    std::snprintf(text.data(), text.size(), format, args...);
    text.resize(static_cast<size_t>(count));
    return text;
}

inline std::string BoundedDetail(const char* format, std::string_view value)
{
    const auto translated = Neurotic::Translate(value);
    return Format(Neurotic::Translate(format).c_str(), static_cast<int>(translated.size()), translated.c_str());
}

// Fixed geometry, including embedded newlines and very long owner messages.
// Only overflowing values expose their complete text on hover.
inline void Line(const char* id, std::string_view text, const ImVec4& color)
{
    const std::string full = LocalizedLines(text);
    std::string visible(full);
    for (auto& c : visible) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    const auto pos = ImGui::GetCursorScreenPos();
    const ImVec2 size((std::max)(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetTextLineHeight());
    ImGui::PushID(id);
    ImGui::Dummy(size);
    const bool overflow = ImGui::CalcTextSize(visible.c_str()).x > size.x || full != visible;
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(pos, end, true);
    draw->AddText(pos, ImGui::GetColorU32(color), visible.c_str());
    draw->PopClipRect();
    if (!full.empty() && overflow && hovered)
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(full.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}

template<class... Args> void Linef(const char* id, const ImVec4& color, const char* format, Args... args)
{
    // Format first so the existing recursive translator sees dynamic values too.
    Line(id, Format(format, args...), color);
}

template<class Controls, class Readouts>
void Header(float controlsWidth, Controls controls, Readouts readouts)
{
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float padding = ImGui::GetFontSize() * 0.25f;
    controlsWidth += padding * 2.0f;
    const bool wide = ImGui::GetContentRegionAvail().x >= controlsWidth + gap + ImGui::GetFontSize() * 12.0f;
    const float height = (std::max)(3.0f * ImGui::GetFrameHeightWithSpacing(),
                                   4.0f * ImGui::GetTextLineHeightWithSpacing()) + padding * 2.0f;
    constexpr auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    if (ImGui::BeginChild("##NrToggleAnchor", ImVec2(wide ? controlsWidth : 0.0f, height),
                         ImGuiChildFlags_AlwaysUseWindowPadding, flags)) controls();
    ImGui::EndChild();
    if (wide) ImGui::SameLine(0.0f, gap);
    if (ImGui::BeginChild("##NrStatusAnchor", ImVec2(0.0f, height),
                         ImGuiChildFlags_AlwaysUseWindowPadding, flags)) readouts();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace DlssNr::StatusPanel
