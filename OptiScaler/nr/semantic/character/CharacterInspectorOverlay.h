#pragma once
#include "CharacterInspectorDraw.h"
#include "CharacterInspectorSettings.h"
#include "CharacterBoxHold.h"
#include "CharacterInspectorColor.h"
#include "menu/Localization.h"

namespace Neurotic::Semantic::Character {
// Existing ImGui/menu owner only. Neither preview choice is persisted or read
// by capture/worker threads, and neither can admit a fixture to live inference.
struct InspectorPreviewState { bool canvas=false,fullGame=false; };
inline InspectorPreviewState& InspectorPreviewSession() {
    static InspectorPreviewState state;
    return state;
}
inline void ResetInspectorPreviewSession() noexcept { InspectorPreviewSession()={}; }
inline bool WantsInspectorFixtureOverlay(const InspectorSettings& settings) noexcept {
    return settings.enabled && InspectorPreviewSession().fullGame;
}
inline bool NeedsInspectorFixtureOverlay(const InspectorSettings& settings,const ImGuiViewport& viewport) {
    return WantsInspectorFixtureOverlay(settings) &&
        std::isfinite(viewport.Pos.x) && std::isfinite(viewport.Pos.y) &&
        std::isfinite(viewport.Size.x) && std::isfinite(viewport.Size.y) &&
        viewport.Size.x>0 && viewport.Size.y>0 && viewport.Size.x<=32768 && viewport.Size.y<=32768 &&
        std::abs(viewport.Pos.x)<=1e6 && std::abs(viewport.Pos.y)<=1e6;
}
inline DrawStyle InspectorDrawStyle(const InspectorSettings& settings,const ImGuiViewport& viewport) {
    const auto fb=ImGui::GetIO().DisplayFramebufferScale;
    const float fx=std::isfinite(fb.x)&&fb.x>0?fb.x:1.f;
    const float fy=std::isfinite(fb.y)&&fb.y>0?fb.y:1.f;
    const float outputScale=settings.autoLabelScale ? std::clamp(
        std::min(viewport.Size.x*fx/1920.f,viewport.Size.y*fy/1080.f),.75f,3.f) : 1.f;
    DrawStyle style{};style.color=ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    // Canonical SDR white; the live overlay has its own output conversion and
    // must not inherit the menu's HDR tone mapping or brightness adjustment.
    style.estimatedColor=IM_COL32(255,255,255,255);
    style.thickness=std::clamp(static_cast<float>(settings.boxThickness)/fy,.25f,16.f);
    style.labelHeight=std::min(128.f,std::clamp(16.f*outputScale*settings.labelScalePercent/100.f,8.f,96.f)/fy);
    style.bodyBoxes=settings.bodyBoxes;style.torsoBand=settings.torsoEstimate;
    style.maxLabels=settings.maximumLabels;return style;
}
inline Admission RenderInspectorLiveOverlay(const Snapshot& snapshot,const DisplayContext& display,
    const InspectorSettings& settings,ImGuiViewport& viewport,bool editRules=false) {
    if(!settings.enabled)return Admission::Disabled;
    auto style=InspectorDrawStyle(settings,viewport);style.editRules=editRules;
    if(Rules::Activity().load()==1)style.rules=Rules::GameStore().Read();
    const auto label=Neurotic::Translate("Sampled Person");style.label=label;
    auto& list=*ImGui::GetBackgroundDrawList(&viewport);const int first=list.VtxBuffer.Size;
    const auto result=DrawCharacterOverlay(list,snapshot,display,
        {viewport.Pos.x,viewport.Pos.y,viewport.Size.x,viewport.Size.y},style);
    if(result==Admission::Ready)RecordInspectorColors(list,first,display.colorPolicy);
    return result;
}
inline Admission RenderInspectorLiveOverlay(const HeldSnapshot& snapshot,const DisplayContext& display,
    const InspectorSettings& settings,ImGuiViewport& viewport,bool editRules=false) {
    if(!settings.enabled)return Admission::Disabled;
    const auto admission=AdmitHeld(snapshot,display,settings.boxHoldMs,settings.smartBoxHandoff);
    if(admission!=Admission::Ready)return admission;
    auto style=InspectorDrawStyle(settings,viewport);style.editRules=editRules;
    if(Rules::Activity().load()==1)style.rules=Rules::GameStore().Read();
    const auto label=Neurotic::Translate("Sampled Person");style.label=label;
    auto& list=*ImGui::GetBackgroundDrawList(&viewport);const int first=list.VtxBuffer.Size;
    const auto result=EmitCharacterGeometry(list,PredictTrackedGeometry(snapshot.geometry,display.nowNs,!display.fgActive),
        {viewport.Pos.x,viewport.Pos.y,viewport.Size.x,viewport.Size.y},style);
    if(result==Admission::Ready)RecordInspectorColors(list,first,display.colorPolicy);
    return result;
}
// Call only during the single existing NewFrame/EndFrame interval. Background
// geometry covers the game viewport while ordinary menus remain above it.
inline Admission RenderInspectorFixtureOverlay(const InspectorSettings& settings,ImGuiViewport& viewport) {
    if(!NeedsInspectorFixtureOverlay(settings,viewport)) return Admission::Disabled;
    auto fixture=MakeCharacterFixture();
    fixture.count=std::min(fixture.count,static_cast<std::size_t>(std::clamp(settings.maximumPersons,1u,16u)));
    auto style=InspectorDrawStyle(settings,viewport);
    const auto label=Neurotic::Translate("Person");style.label=label;
    auto& draw=*ImGui::GetBackgroundDrawList(&viewport);
    const ViewportRect extent{viewport.Pos.x,viewport.Pos.y,viewport.Size.x,viewport.Size.y};
    const auto result=DrawFixtureObservation(draw,fixture,extent,style,true);
    if(result!=Admission::Ready) return result;
    const auto note=Neurotic::Translate("Synthetic overlay preview");
    const auto at=ImVec2(viewport.Pos.x+12,viewport.Pos.y+12);
    const auto size=ImGui::CalcTextSize(note.c_str());
    draw.PushClipRect(viewport.Pos,{viewport.Pos.x+viewport.Size.x,viewport.Pos.y+viewport.Size.y},true);
    draw.AddRectFilled({at.x-4,at.y-4},{at.x+size.x+4,at.y+size.y+4},IM_COL32(0,0,0,190),3);
    draw.AddText(at,IM_COL32(255,255,255,255),note.c_str());
    draw.PopClipRect();
    return result;
}
}
