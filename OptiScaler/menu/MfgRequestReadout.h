#pragma once
#include <menu/Localization.h>
#include <mfg/MfgControl.h>
#include <imgui/imgui_internal.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <limits>

namespace Neurotic::Sleek
{
// Setters can replace the receipt between observations. Reserve translated
// slots independently of that cadence, without retaining old facts.
inline void DrawMfgRequestReadout(const Neurotic::Mfg::MfgRequestReceipt& request)
{
    const auto message=[](const char* id,const char* english){return Neurotic::Translate(Neurotic::UiLiteral(id,english));};
    const auto format=[](const char* original,auto... arguments){
        const char* pattern=original; Neurotic::LocalizedFormat localized(pattern);
        const int count=std::snprintf(nullptr,0,pattern,arguments...);
        if(count<0)return std::string{};
        std::string value(size_t(count)+1,'\0');
        std::snprintf(value.data(),value.size(),pattern,arguments...);value.resize(size_t(count));return value;
    };
    const char* game=Neurotic::UiLiteral("ingame.menu-common.game_s_u_generated_selected_s_c67c8b33", "Game: %s, %u generated; selected: %s");
    const char* forwarded=Neurotic::UiLiteral("ingame.menu-common.forwarded_s_u_generated_setoptions_s_08aa5c1c", "Forwarded: %s, %u generated; SetOptions: %s");
    const char* maximum=Neurotic::UiLiteral("ingame.menu-common.reported_maximum_u_distinct_frames_unverified_f389287e", "Reported maximum: %u; distinct frames unverified.");
    const char* presentations=Neurotic::UiLiteral("ingame.menu-common.reported_presentations_u_distinct_frames_unverif_245aed56", "Reported presentations: %u; distinct frames unverified.");
    const std::array<std::string,2> enabled={message("ingame.objectruleeditor.off_dc516be5","Off"),message("ingame.menu-common.on_d2f9df8a","On")};
    const std::array<std::string,8> selected={message("ingame.provider.5c412a262086","Game"),enabled[0],"2X","3X","4X","5X","6X",message("ingame.menu-common.unknown_d80d0833","Unknown")};
    const std::array<std::string,3> result={message("ingame.provider.62a2fed3d6e0","pending"),message("ingame.provider.070c160a6299","accepted"),message("ingame.provider.20cd938a2ea6","rejected")};
    const std::array<std::string,2> fallback={
        message("ingame.menu-common.selected_ratio_rejected_game_request_restored_b8e39460","Selected ratio rejected; game request restored."),
        message("ingame.menu-common.selected_ratio_rejected_restoring_the_game_reque_9161f84f","Selected ratio rejected; restoring the game request failed.")};
    std::array<std::string,5> text={
        message("ingame.mfg-readout.game_unavailable","Game settings: unavailable."),
        message("ingame.mfg-readout.forwarded_unavailable","Forwarded settings: unavailable."),{},
        message("ingame.mfg-readout.maximum_unavailable","Reported maximum: unavailable."),
        message("ingame.mfg-readout.presentations_unavailable","Reported presentations: unavailable.")};
    std::array<std::vector<std::string>,5> variants;
    for(size_t row=0;row<text.size();++row)variants[row].push_back(text[row]);
    constexpr auto widestCount=std::numeric_limits<unsigned>::max();
    for(const auto& on:enabled)
    {
        for(const auto& name:selected)variants[0].push_back(format(game,on.c_str(),widestCount,name.c_str()));
        for(const auto& outcome:result)variants[1].push_back(format(forwarded,on.c_str(),widestCount,outcome.c_str()));
    }
    variants[2].insert(variants[2].end(),fallback.begin(),fallback.end());
    variants[3].push_back(format(maximum,widestCount));variants[4].push_back(format(presentations,widestCount));
    if(request.attempt&&!request.stale)
    {
        const auto index=static_cast<size_t>(request.selected);
        text[0]=format(game,enabled[request.gameEnabled].c_str(),request.gameGenerated,selected[std::min(index,selected.size()-1)].c_str());
        text[1]=format(forwarded,enabled[request.forwardedEnabled].c_str(),request.forwardedGenerated,result[!request.setterResult?0:request.accepted?1:2].c_str());
        if(request.fellBackToGame)text[2]=fallback[request.accepted?0:1];
        if(request.observedMax)text[3]=format(maximum,*request.observedMax);
        if(request.observedPresented)text[4]=format(presentations,*request.observedPresented);
    }
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
    for(size_t row=0;row<text.size();++row)
    {
        float height=ImGui::GetTextLineHeight();
        for(const auto& possible:variants[row])height=std::max(height,ImGui::CalcTextSize(possible.c_str(),nullptr,false,width).y);
        height=std::max(height,ImGui::CalcTextSize(text[row].c_str(),nullptr,false,width).y);
        const auto at=ImGui::GetCursorScreenPos();ImGui::Dummy({width,height});
        if(!text[row].empty())
        {
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),at,
                ImGui::GetColorU32(ImGuiCol_TextDisabled),text[row].c_str(),nullptr,width);
            if(ImGui::GetCurrentContext()->LogEnabled)ImGui::LogRenderedText(&at,text[row].c_str());
        }
    }
}
}
