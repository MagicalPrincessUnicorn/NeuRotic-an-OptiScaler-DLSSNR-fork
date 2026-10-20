// Included beside MenuCommon's page owners. These controls use the same Config,
// backend selection and reinitialization actions as their existing full pages.
void MenuCommon::RenderAdvancedSrSettings(RenderMenuContext& ctx)
{
    auto& state=ctx.state;auto* config=ctx.config;auto* feature=ctx.currentFeature;
    const bool active=feature&&!feature->IsFrozen(),rr=active&&feature->GetUpscalerType()==Upscaler::DLSSD;
    const bool columns=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*31;
    if(!active){ImGui::TextWrapped("SR controls need an initialized upscaler. Saved choices are retained.");return;}
    GetCurrentBackendInfo(state.api,currentBackend,&currentBackendName);
    ImGui::TextWrapped("%s · %d.%d.%d%s",feature->ShortName().c_str(),feature->Version().major,feature->Version().minor,feature->Version().patch,rr?" · SR + RR":"");
    ImGui::Spacing();
    if(ImGui::BeginTable("##AdvancedSrChoice",columns?2:1,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)){
        ImGui::TableNextColumn();Neurotic::Sleek::ControlLabel("Backend");
        if(rr)ImGui::TextUnformatted("DLSS Ray Reconstruction");
        else if(state.api==API::DX12)AddDx12Backends(currentBackend);
        else if(state.api==API::DX11)AddDx11Backends(currentBackend);else AddVulkanBackends(currentBackend);
        ImGui::TableNextColumn();ImGui::TextUnformatted("Quality mode");
        const char* quality="Unknown";
        switch(feature->PerfQualityValue()){
        case NVSDK_NGX_PerfQuality_Value_MaxQuality:quality="Quality";break;
        case NVSDK_NGX_PerfQuality_Value_Balanced:quality="Balanced";break;
        case NVSDK_NGX_PerfQuality_Value_MaxPerf:quality="Performance";break;
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance:quality="Ultra Performance";break;
        case NVSDK_NGX_PerfQuality_Value_UltraQuality:quality="Ultra Quality";break;
        case NVSDK_NGX_PerfQuality_Value_DLAA:quality="Native AA";break;default:break;}
        ImGui::AlignTextToFramePadding();ImGui::Text("%s · Game",quality);ImGui::EndTable();
    }
    ImGui::BeginDisabled(rr||state.newBackend==Upscaler::Reset||state.newBackend==currentBackend);
    if(ImGui::Button("Apply backend")){
        if(state.newBackend==Upscaler::XeSS){config->DisableReactiveMask.reset();config->DlssReactiveMaskBias.reset();}
        MARK_ALL_BACKENDS_CHANGED();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",rr?"RR shares this stage. Its availability follows the game's DLSSD path.":"Backend changes request reinitialization. Quality follows the game; ratios may be overridden below.");
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    if(ImGui::BeginTable("##AdvancedSrOverrides",columns?2:1,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)){
        ImGui::TableNextColumn();
        bool ratio=config->UpscaleRatioOverrideEnabled.value_or_default();
        if(ImGui::Checkbox("Override upscale ratio",&ratio)){config->UpscaleRatioOverrideEnabled=ratio;if(ratio)config->QualityRatioOverrideEnabled=false;}
        ImGui::BeginDisabled(!ratio);Neurotic::Sleek::ControlLabel("Upscale ratio");
        float value=config->UpscaleRatioOverrideValue.value_or_default();
        if(Neurotic::Sleek::CardSliderFloat("##AdvancedSrRatio",&value,config->ExtendedLimits.value_or_default()?.1f:1.f,config->ExtendedLimits.value_or_default()?6.f:3.f,"%.2fx"))config->UpscaleRatioOverrideValue=value;
        ResetSliderSetting("UpscaleRatioOverrideValue",config->UpscaleRatioOverrideValue);ImGui::EndDisabled();
        ImGui::TableNextColumn();bool sharp=config->OverrideSharpness.value_or_default();
        if(ImGui::Checkbox("Override sharpness",&sharp)){
            config->OverrideSharpness=sharp;
            if(currentBackend==Upscaler::DLSS&&feature->Version().major<3){state.newBackend=currentBackend;MARK_ALL_BACKENDS_CHANGED();}
        }
        ImGui::BeginDisabled(!sharp);Neurotic::Sleek::ControlLabel("Sharpness");
        float amount=config->Sharpness.value_or_default();
        if(Neurotic::Sleek::CardSliderFloat("##AdvancedSrSharpness",&amount,0,1,"%.2f"))config->Sharpness=amount;
        ResetSliderSetting("Sharpness",config->Sharpness);ImGui::EndDisabled();ImGui::EndTable();
    }
    if(ImGui::CollapsingHeader("Per-quality ratios and output scaling"))RenderActiveImageSettings(ctx);
}
