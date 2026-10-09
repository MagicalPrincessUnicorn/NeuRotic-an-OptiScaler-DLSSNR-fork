#pragma once
#include "DlssNr_StageControls.h"
#include "NrAnythingResolutionUi.h"

namespace DlssNr::AnythingModeUi {
// Only controls actually implemented by the captured-image worker. Stored
// native/multipass preferences remain intact while another mode is selected.
template<class C> void Render(C& config) {
    bool enabled=config.DlssNrEnabled.value_or_default();
    if(ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.enable_neural_rendering_38e3f189", "Enable Neural Rendering"),&enabled))config.SetDlssNrEnabled(enabled);
    bool apply=config.DlssNrApplyModel.value_or_default();
    if(ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.apply_the_model_8fad708a", "Apply the model"),&apply))config.DlssNrApplyModel=apply;
    StageUi::RenderControls(config,false,false,false,true);
    if(config.DlssNrRoute.value_or_default()!=3)return;
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.anything_mode.description", "NR Anything processes the captured game window, including its HUD. It uses one image pass."));
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.provider.7e744141b40e", "Style"));
    const char* styles[]={Neurotic::UiLiteral("ingame.option.ef6691545d2c", "Standard"),Neurotic::UiLiteral("ingame.option.d6acb6d51cfc", "Natural"),Neurotic::UiLiteral("ingame.option.912d0988b065", "Cinematic")};
    int style=int(std::clamp(config.DlssNrStyle.value_or_default(),0u,2u));
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##NrAnythingStyle",&style,styles,3))config.DlssNrStyle=uint32_t(style);
    const auto reset=Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset");
    const auto width=[&]{return (std::max)(1.f,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(reset).x-2*ImGui::GetStyle().FramePadding.x-ImGui::GetStyle().ItemSpacing.x);};
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-stagecontrols.nr_resolution_399e2689", "NR Resolution"));
    auto resolution=config.GetDlssNrConfigSnapshot();
    int selection=AnythingResolutionUi::Selection(resolution);
    const char* choices[]={"100%","75%","67%","50%",StageUi::ResolutionChoices[StageUi::ManualChoice]};
    ImGui::SetNextItemWidth(width());
    if(ImGui::Combo("##NrAnythingResolution",&selection,choices,5)){AnythingResolutionUi::Select(config,selection);resolution=config.GetDlssNrConfigSnapshot();}
    ImGui::SameLine();ImGui::PushID("AnythingScale");
    if(ImGui::SmallButton(reset)){AnythingResolutionUi::Select(config,0);selection=0;}
    ImGui::PopID();
    if(selection==AnythingResolutionUi::Manual){
        int scale=int(AnythingResolutionUi::Percent(resolution));ImGui::SetNextItemWidth(-1);
        if(ImGui::SliderInt("##NrAnythingScale",&scale,25,100,"%d%%",ImGuiSliderFlags_AlwaysClamp))AnythingResolutionUi::SetManual(config,scale);
    }
    const auto strength=[&](const char* label,const char* id,auto& option){
        ImGui::TextUnformatted(label);ImGui::PushID(id);ImGui::SetNextItemWidth(width());
        float value=std::clamp(option.value_or_default(),0.f,2.f);
        if(ImGui::SliderFloat("##Strength",&value,0.f,2.f,"%.2f",ImGuiSliderFlags_AlwaysClamp))option=value;
        ImGui::SameLine();if(ImGui::SmallButton(reset))option=1.f;ImGui::PopID();
    };
    strength(Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength"),"AnythingDetail",config.DlssNrTransferStrength);
    strength(Neurotic::UiLiteral("ingame.dlssnr-menu.colour_strength_0db6aec0", "Colour Strength"),"AnythingColour",config.DlssNrColourStrength);
}
}
