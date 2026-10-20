#pragma once
#include <imgui/imgui_internal.h>
namespace Neurotic {
namespace HelpDetail {
inline ImGuiID Key(ImGuiID id,const char* field){return ImHashStr(field,0,id);}
inline ImGuiID Identity(ImGuiID explicitId) {
    if(explicitId)return explicitId;
    if(const auto id=ImGui::GetItemID())return id;
    // Text and Dummy items have no navigation ID. Their window-scoped rectangle
    // supplies a distinct fallback; callers can provide a stable semantic ID.
    return ImGui::GetCurrentWindow()->GetIDFromRectangle(GImGui->LastItemData.Rect);
}
inline bool Navigation(ImGuiID id) {
    auto& g=*ImGui::GetCurrentContext();
    return g.NavHighlightItemUnderNav&&g.NavCursorVisible&&g.NavId==id;
}
inline bool Dismissed(ImGuiID id,bool inside,bool navigation,bool active) {
    auto* storage=ImGui::GetStateStorage();const auto frame=ImGui::GetFrameCount();
    const auto visited=Key(id,"help-visited"),dismissal=Key(id,"help-dismissed");
    const int previous=storage->GetInt(visited,-2);
    bool dismissed=previous<frame-1?false:storage->GetBool(dismissal,false);
    // A popup blocking the source is not physical pointer departure. In
    // particular, closing a clicked combo must not resurrect its old help.
    if(!inside&&!navigation)dismissed=false;
    if((inside&&ImGui::IsAnyMouseDown())||active)dismissed=true;
    storage->SetBool(dismissal,dismissed);storage->SetInt(visited,frame);
    return dismissed;
}
inline bool RegionDelay(ImGuiID id,bool eligible,bool navigation) {
    auto& g=*ImGui::GetCurrentContext();auto* storage=ImGui::GetStateStorage();
    const auto flags=navigation?g.Style.HoverFlagsForTooltipNav:g.Style.HoverFlagsForTooltipMouse;
    const float delay=flags&ImGuiHoveredFlags_DelayNormal?g.Style.HoverDelayNormal:
                      flags&ImGuiHoveredFlags_DelayShort?g.Style.HoverDelayShort:0.f;
    const auto frameKey=Key(id,"help-region-frame"),timeKey=Key(id,"help-region-time");
    const auto eligibleKey=Key(id,"help-region-eligible"),stationaryKey=Key(id,"help-region-stationary");
    const auto frame=g.FrameCount;const auto previous=storage->GetInt(frameKey,-2);
    float elapsed=storage->GetFloat(timeKey,0.f);
    bool stationary=(previous==frame||previous==frame-1)&&storage->GetBool(stationaryKey,false);
    if(previous!=frame) {
        elapsed=eligible&&previous==frame-1&&storage->GetBool(eligibleKey,false)?
            ImMin(elapsed+g.IO.DeltaTime,delay+1.f):0.f;
        if(!eligible)stationary=false;
        if(eligible&&(navigation||g.MouseStationaryTimer>=g.Style.HoverStationaryDelay))stationary=true;
        storage->SetInt(frameKey,frame);storage->SetFloat(timeKey,elapsed);
        storage->SetBool(eligibleKey,eligible);storage->SetBool(stationaryKey,stationary);
    }
    return eligible&&elapsed>=delay&&(!(flags&ImGuiHoveredFlags_Stationary)||stationary);
}
} // namespace HelpDetail

// Mouse activation dismisses help until real departure; retained mouse focus is
// not keyboard navigation. No widget input, layout or saved preference changes.
inline bool ItemHelpHovered(ImGuiID explicitId=0,ImGuiHoveredFlags timing=ImGuiHoveredFlags_None) {
    const auto id=HelpDetail::Identity(explicitId);
    const auto& rect=GImGui->LastItemData.Rect;
    const bool inside=ImGui::IsMouseHoveringRect(rect.Min,rect.Max);
    const bool navigation=HelpDetail::Navigation(id),active=ImGui::IsItemActive();
    const bool dismissed=HelpDetail::Dismissed(id,inside,navigation,active);
    if(dismissed||ImGui::IsAnyMouseDown()||active)return false;
    timing&=ImGuiHoveredFlags_DelayMask_;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride|ImGuiHoveredFlags_AllowWhenDisabled))
        return ImGui::IsItemHovered(timing|ImGuiHoveredFlags_NoNavOverride|ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled);
    return navigation&&ImGui::IsItemHovered(timing|ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled);
}

// Coordinate-only help uses a real, window-scoped caller ID. It never creates a
// button or changes LastItemData, so read-only status regions stay read-only.
inline bool RegionHelpHovered(ImGuiID id,ImVec2 minimum,ImVec2 maximum) {
    IM_ASSERT(id!=0);
    const bool inside=ImGui::IsMouseHoveringRect(minimum,maximum);
    const bool navigation=HelpDetail::Navigation(id),active=GImGui->ActiveId==id;
    const bool dismissed=HelpDetail::Dismissed(id,inside,navigation,active);
    const bool pointer=inside&&ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    return HelpDetail::RegionDelay(id,!dismissed&&!ImGui::IsAnyMouseDown()&&!active&&(pointer||navigation),navigation);
}
} // namespace Neurotic
