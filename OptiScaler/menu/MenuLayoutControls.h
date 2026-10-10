#pragma once
#include "MenuLayout.h"
#include "Localization.h"
#include <imgui/imgui_internal.h>
#include <array>
namespace Neurotic::MenuLayout {
inline void WrappedCaption(const std::string& text, ImVec2 min, ImVec2 max, float padding) {
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
        {min.x+padding,min.y+ImGui::GetStyle().FramePadding.y},ImGui::GetColorU32(ImGuiCol_Text),
        text.c_str(),nullptr,(std::max)(1.f,max.x-min.x-2*padding));
}
inline bool ResetLayoutButton(const char* label) {
    const auto text=Translate(label);
    const auto padding=ImGui::GetStyle().FramePadding;
    const float available=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
    if(ImGui::CalcTextSize(text.c_str()).x+2*padding.x<=available)return ImGui::Button(label);
    const float height=ImGui::CalcTextSize(text.c_str(),nullptr,false,(std::max)(1.f,available-2*padding.x)).y+2*padding.y;
    const bool pressed=ImGui::Button("##MenuLayoutWrappedReset",{available,height});
    WrappedCaption(text,ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),padding.x);
    return pressed;
}
inline bool CornerPicker(const char* id,const char* const* names,unsigned& corner,float maxWidth=0) {
    corner=Corner(corner);bool changed=false;
    const auto padding=ImGui::GetStyle().FramePadding;
    const float width=(std::max)(1.f,(std::min)(ImGui::GetContentRegionAvail().x,maxWidth>0?maxWidth:ImGui::GetFontSize()*22.f));
    const float arrow=ImGui::GetFrameHeight(),previewWidth=(std::max)(1.f,width-arrow-2*padding.x);
    std::array<std::string,4> translated;
    float height=ImGui::GetFontSize();
    for(unsigned i=0;i<4;++i) {translated[i]=Translate(names[i]);height=(std::max)(height,ImGui::CalcTextSize(translated[i].c_str(),nullptr,false,previewWidth).y);}
    const bool wrap=height>ImGui::GetFontSize()+.1f;
    ImGui::SetNextItemWidth(width);
    if(wrap) {
        ImGui::SetNextWindowSizeConstraints({width,0},{width,FLT_MAX});
        // Reserve for the longest choice so selecting a value never moves
        // neighboring controls. A fixed arrow width leaves room for the text.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2{padding.x,padding.y+(height-ImGui::GetFontSize())*.5f});
    }
    const bool open=ImGui::BeginCombo(id,wrap?nullptr:translated[corner].c_str(),
        wrap?ImGuiComboFlags_CustomPreview|ImGuiComboFlags_NoArrowButton:ImGuiComboFlags_None);
    if(wrap)ImGui::PopStyleVar();
    if(open) {
        for(unsigned i=0;i<4;++i) {
            if(!wrap) {if(ImGui::Selectable(names[i],i==corner)){corner=i;changed=true;}continue;}
            const float textWidth=(std::max)(1.f,ImGui::GetContentRegionAvail().x-2*padding.x);
            const float textHeight=ImGui::CalcTextSize(translated[i].c_str(),nullptr,false,textWidth).y;
            ImGui::PushID(int(i));
            if(ImGui::Selectable("##MenuCornerChoice",i==corner,0,{0,textHeight+2*padding.y})){corner=i;changed=true;}
            WrappedCaption(translated[i],ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),padding.x);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if(wrap && ImGui::BeginComboPreview()) {
        const auto bounds=GImGui->ComboPreviewData.PreviewRect;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+previewWidth);
        ImGui::TextUnformatted(translated[corner].c_str());ImGui::PopTextWrapPos();
        ImGui::RenderArrow(ImGui::GetWindowDrawList(),{bounds.Max.x-arrow*.75f,bounds.GetCenter().y-ImGui::GetFontSize()*.5f},ImGui::GetColorU32(ImGuiCol_Text),ImGuiDir_Down);
        ImGui::EndComboPreview();
    }
    return changed;
}
inline bool WrappedHeader(const char* label,ImGuiTreeNodeFlags flags) {
    const auto text=Translate(label);const auto padding=ImGui::GetStyle().FramePadding;
    const float font=ImGui::GetFontSize(),inset=padding.x+font+ImGui::GetStyle().ItemInnerSpacing.x;
    const float available=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
    const float height=ImGui::CalcTextSize(text.c_str(),nullptr,false,(std::max)(1.f,available-2*inset)).y;
    if(height<=font+.1f)return ImGui::CollapsingHeader(label,flags);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2{padding.x,padding.y+(height-font)*.5f});
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{0,0,0,0});
    const bool open=ImGui::CollapsingHeader(label,flags);
    ImGui::PopStyleColor();ImGui::PopStyleVar();
    const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
    WrappedCaption(text,low,high,inset);
    ImGui::RenderArrow(ImGui::GetWindowDrawList(),{low.x+padding.x,(low.y+high.y-font)*.5f},ImGui::GetColorU32(ImGuiCol_Text),open?ImGuiDir_Down:ImGuiDir_Right);
    return open;
}
// Immediate in-game changes keep the existing global Save action. Opening this
// section is read-only; invalid persisted intent is sanitized by the loader.
template<class Config> bool Controls(Config& config) {
    const char* heading=UiLiteral("ingame.menu-layout.heading", "Menu layout");
    if(ImGui::CalcTextSize(Translate(heading).c_str()).x>ImGui::GetContentRegionAvail().x) {
        ImGui::Separator();ImGui::TextWrapped("%s",Translate(heading).c_str());
    }else ImGui::SeparatorText(heading);
    const char* names[]={UiLiteral("ingame.menu-layout.upper-left", "Upper left"),
        UiLiteral("ingame.menu-layout.upper-right", "Upper right"),
        UiLiteral("ingame.menu-layout.lower-left", "Lower left"),
        UiLiteral("ingame.menu-layout.lower-right", "Lower right")};
    unsigned corner=Corner(config.MenuCorner.value_or_default());
    ImGui::TextWrapped("%s",Translate(UiLiteral("ingame.menu-layout.corner", "Menu corner")).c_str());
    if(CornerPicker("##MenuCorner",names,corner))config.MenuCorner=corner;
    ImGui::TextWrapped("%s",Translate(UiLiteral("ingame.menu-layout.scale", "Menu scale")).c_str());
    const auto manual=ManualScale(config.MenuScale.has_value()?std::optional<float>{config.MenuScale.value()}:std::nullopt);
    const std::string preview=manual?std::to_string(int(std::round(*manual*100.f)))+"%":Translate(UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"));
    ImGui::SetNextItemWidth((std::min)(ImGui::GetContentRegionAvail().x,ImGui::GetFontSize()*22.f));
    if(ImGui::BeginCombo("##MenuLayoutScale",preview.c_str())) {
        if(ImGui::Selectable(UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"),!manual))config.MenuScale.reset();
        for(int percent=50;percent<=200;percent+=10) {
            const auto label=std::to_string(percent)+"%";
            if(ImGui::Selectable(label.c_str(),manual&&std::abs(*manual-percent*.01f)<.001f))config.MenuScale=percent*.01f;
        }
        ImGui::EndCombo();
    }
    const bool reset=ResetLayoutButton(UiLiteral("ingame.menu-layout.reset", "Reset layout"));
    if(reset) {config.MenuCorner=0u;config.MenuScale.reset();}
    return reset;
}
}
