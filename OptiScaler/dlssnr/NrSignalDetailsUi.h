#pragma once
#include "NrSignalDetails.h"
#include "NrStatusPanel.h"

namespace DlssNr::SignalDetails {
inline void Rows(const char* id,const std::vector<Row>& rows) {
    if(rows.empty()||!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX))return;
    for(const auto& row:rows){
        ImGui::TableNextRow();ImGui::TableNextColumn();
        StatusPanel::Line(row.label,row.label,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TableNextColumn();
        StatusPanel::Line(row.label,row.value,ImGui::GetStyleColorVec4(ImGuiCol_Text));
    }
    ImGui::EndTable();
}
inline void Draw(const Panel& panel) {
    Rows("##SignalValues",panel.values);
    if(!panel.technical.empty()){
        ImGui::Spacing();
        if(ImGui::TreeNode("Technical details")){
            ImGui::TextDisabled("Last diagnostic observation");
            Rows("##SignalTechnical",panel.technical);
            ImGui::TreePop();
        }
    }
}
}
