#pragma once
#include <imgui/imgui.h>
#include "SleekUi.h"
#include "WindowSectionHeader.h"

namespace Neurotic::Sleek
{
inline void BeginContentCard(const char* id, const char* heading = nullptr, bool sectionHeading = false)
{
    ImGui::BeginChild(id, {0, 0}, ImGuiChildFlags_Borders |
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize |
        ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollWithMouse);
    if (heading && *heading && sectionHeading) WindowSectionHeader(heading);
    else if (heading && *heading)
    {
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.06f);
        ImGui::TextUnformatted(heading);
        ImGui::PopFont();
        ImGui::Spacing();
    }
}
inline void EndContentCard() { ImGui::EndChild(); }

// A real two-column layout at normal widths, with full-width cards on small menus.
class CardColumns
{
    bool table;
public:
    explicit CardColumns(const char* id)
        : table(ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 42.0f &&
            ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
    { if (table) ImGui::TableNextColumn(); }
    void Next() { if (table) ImGui::TableNextColumn(); else ImGui::Spacing(); }
    ~CardColumns() { if (table) ImGui::EndTable(); }
};
inline void ControlLabel(const char* label)
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
}
inline void ControlDivider() { ImGui::Separator(); }

inline bool CardSliderScalar(const char* id, ImGuiDataType type, void* value,
                             const void* minimum, const void* maximum,
                             const char* format, ImGuiSliderFlags flags = 0)
{
    const float width = ImGui::CalcItemWidth();
    const float valueWidth = ImGui::GetFontSize() * 3.2f;
    ImGui::SetNextItemWidth((std::max)(1.0f, width - valueWidth - ImGui::GetStyle().ItemSpacing.x));
    const bool oldRail = railSlider;
    railSlider = true;
    const bool changed = ImGui::SliderScalar(id, type, value, minimum, maximum, format, flags);
    railSlider = oldRail;
    // Drawing the value must not replace the slider's last-item state: deferred
    // edits still commit on release, including keyboard and direct numeric input.
    char text[64];
    ImGui::DataTypeFormatString(text, IM_ARRAYSIZE(text), type, value, format);
    const ImVec2 origin = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(
        {origin.x + width - ImGui::CalcTextSize(text).x, origin.y + ImGui::GetStyle().FramePadding.y},
        ImGui::GetColorU32(ImGuiCol_Text), text);
    ImGui::GetCurrentWindow()->DC.CursorPosPrevLine.x = origin.x + width;
    return changed;
}
inline bool CardSliderFloat(const char* id, float* value, float minimum, float maximum,
                            const char* format = "%.2f", ImGuiSliderFlags flags = 0)
{
    return CardSliderScalar(id, ImGuiDataType_Float, value, &minimum, &maximum, format, flags);
}

// Content height is measured by ImGui. Only the enclosing page scrolls; card
// headings are optional because navigation already identifies the page.
class ContentCard
{
public:
    explicit ContentCard(const char* id, const char* heading = nullptr, bool sectionHeading = false)
    {
        BeginContentCard(id, heading, sectionHeading);
    }
    ~ContentCard() { EndContentCard(); }
    ContentCard(const ContentCard&) = delete;
    ContentCard& operator=(const ContentCard&) = delete;
};
}
