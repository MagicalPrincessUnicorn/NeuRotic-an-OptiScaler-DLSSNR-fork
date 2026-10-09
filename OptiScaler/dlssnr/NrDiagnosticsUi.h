#pragma once
#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <menu/BoundedPopup.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NrDiagnosticsUi {
enum class Page { Runtime, Inputs, Timing, Connections };
#ifdef NEUROTIC_NR_DIAGNOSTICS_TEST
inline int RequestedPage=-1;
#endif
enum class Kind { Scalar, Detail };
struct Row {
    const char* label;
    std::string value;
    Kind kind;
    // Resolve a bound literal before copying it: IDs are associated with the
    // original pointer, whereas owner strings must retain their exact bytes.
    Row(const char* label,const char* value,Kind kind=Kind::Scalar)
        :label(label),value(Neurotic::Translate(value)),kind(kind) {}
    Row(const char* label,std::string value,Kind kind=Kind::Scalar)
        :label(label),value(std::move(value)),kind(kind) {}
};
struct Group { const char* id; const char* title; std::vector<Row> rows; };
inline int Lines(Kind kind,float width,float font) {
    // Geometry depends on the field and available space, never on a changing value.
    return kind==Kind::Detail && width<font*28.f ? 2 : 1;
}
inline void Cell(const char* id,std::string_view value,int lines) {
    lines=std::clamp(lines,1,2);
    const auto full=Neurotic::Translate(value);
    auto visible=full;
    for(auto& c:visible)if(c=='\n'||c=='\r'||c=='\t')c=' ';
    const auto pos=ImGui::GetCursorScreenPos();
    const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
    const float height=ImGui::GetTextLineHeight()*lines;
    const bool clipped=full!=visible || ImGui::CalcTextSize(visible.c_str(),nullptr,false,lines==2?width:-1.f).y>height+.1f ||
        (lines==1&&ImGui::CalcTextSize(visible.c_str()).x>width);
    ImGui::InvisibleButton(id,{width,height},ImGuiButtonFlags_EnableNav);
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(pos,{pos.x+width,pos.y+height},true);
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),pos,ImGui::GetColorU32(ImGuiCol_Text),
        visible.c_str(),nullptr,lines==2?width:0.f);
    draw->PopClipRect();
    if(clipped&&(ImGui::IsItemHovered()||ImGui::IsItemFocused())){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*40.f);
        ImGui::TextUnformatted(full.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
inline void Table(const char* id,const std::vector<Row>& rows) {
    if(!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerH))return;
    ImGui::TableSetupColumn("##Label",ImGuiTableColumnFlags_WidthStretch,.44f);
    ImGui::TableSetupColumn("##Value",ImGuiTableColumnFlags_WidthStretch,.56f);
    for(size_t i=0;i<rows.size();++i){const auto& row=rows[i];ImGui::PushID(static_cast<int>(i));ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const int labelLines=ImGui::GetContentRegionAvail().x<ImGui::GetFontSize()*14.f?2:1;
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));Cell("##Label",row.label,labelLines);ImGui::PopStyleColor();
        ImGui::TableSetColumnIndex(1);Cell("##Value",row.value,Lines(row.kind,ImGui::GetContentRegionAvail().x,ImGui::GetFontSize()));ImGui::PopID();
    }
    ImGui::EndTable();
}
inline void ObservationRow(const char* label,std::string_view value,const ImVec4& color) {
    ImGui::TableNextRow();ImGui::PushID(ImGui::TableGetRowIndex());ImGui::TableSetColumnIndex(0);
    const int labelLines=ImGui::GetContentRegionAvail().x<ImGui::GetFontSize()*14.f?2:1;
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));Cell("##Label",label,labelLines);ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);ImGui::PushStyleColor(ImGuiCol_Text,color);
    Cell("##Value",value,Lines(Kind::Detail,ImGui::GetContentRegionAvail().x,ImGui::GetFontSize()));ImGui::PopStyleColor();ImGui::PopID();
}
inline void Groups(const std::vector<Group>& groups) {
    const bool wide=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*46.f;
    const bool columns=wide&&ImGui::BeginTable("##NrDiagnosticGroups",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX);
    for(const auto& g:groups){if(columns)ImGui::TableNextColumn();ImGui::PushID(g.id);
        ImGui::TextUnformatted(g.title);ImGui::Separator();Table("##Readings",g.rows);ImGui::Spacing();ImGui::PopID();
    }
    if(columns)ImGui::EndTable();
}
inline Page Tabs(int& selected) {
    const char* labels[]={Neurotic::UiLiteral("ingame.nr-diagnostics.runtime","Runtime"),Neurotic::UiLiteral("ingame.nr-diagnostics.inputs","Inputs"),Neurotic::UiLiteral("ingame.nr-diagnostics.timing","Timing"),Neurotic::UiLiteral("ingame.nr-diagnostics.connections","Connections")};
    const char* ids[]={"###NrDiagRuntime","###NrDiagInputs","###NrDiagTiming","###NrDiagConnections"};
    if(ImGui::BeginTabBar("##NrDiagnosticDetails",ImGuiTabBarFlags_FittingPolicyScroll)){
        for(int i=0;i<4;++i){std::string name=Neurotic::Translate(labels[i])+ids[i];
            ImGuiTabItemFlags flags=ImGuiTabItemFlags_None;
#ifdef NEUROTIC_NR_DIAGNOSTICS_TEST
            if(RequestedPage==i)flags|=ImGuiTabItemFlags_SetSelected;
#endif
            if(ImGui::BeginTabItem(name.c_str(),nullptr,flags)){selected=i;ImGui::EndTabItem();}}
        ImGui::EndTabBar();
    }
    return static_cast<Page>(selected);
}
inline void InfoDialog(const std::string& details) {
    if(ImGui::Button(Neurotic::UiLiteral("ingame.nr-diagnostics.about","About these readings")))ImGui::OpenPopup("##NrDiagnosticInfo");
    if(ImGui::IsPopupOpen("##NrDiagnosticInfo"))Neurotic::SetBoundedPopupSize(38,23);
    bool open=true;
    if(ImGui::BeginPopupModal("##NrDiagnosticInfo",&open,ImGuiWindowFlags_NoSavedSettings)){
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.nr-diagnostics.about","About these readings"));ImGui::Separator();
        const float actions=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y;
        if(ImGui::BeginChild("##NrDiagnosticInfoBody",{0,(std::max)(1.f,ImGui::GetContentRegionAvail().y-actions)})){
            ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(details.c_str());ImGui::PopTextWrapPos();
        }ImGui::EndChild();
        if(ImGui::Button(Neurotic::UiLiteral("ingame.common.close","Close"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
}
