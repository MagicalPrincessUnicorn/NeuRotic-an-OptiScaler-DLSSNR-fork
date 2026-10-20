// Included inside namespace DlssNr, beside the existing pass option owners.
// Presentation only: Basic derivation and rendering resource lifetimes stay unchanged.
static void MultipassTitle(const char* title)
{
    // Keep the established header treatment, but bound long translations to
    // this card. The complete label is available only when it is truncated.
    const auto full=Neurotic::Translate(title);
    std::string shown=full;
    const auto* window=ImGui::GetCurrentWindow();
    const float scale=ImGui::GetIO().FontGlobalScale*window->FontWindowScale*window->FontDpiScale;
    ImGui::PushFont(ImGui::GetFont(),ImGui::GetFontSize()*1.25f/scale);
    const float width=(std::max)(0.f,ImGui::GetContentRegionAvail().x-1.f);
    const bool clipped=ImGui::CalcTextSize(shown.c_str()).x>width;
    if(clipped){
        const std::string dots=ImGui::CalcTextSize("...").x<=width?"...":"";
        while(!shown.empty()&&ImGui::CalcTextSize((shown+dots).c_str()).x>width){
            size_t last=shown.size()-1;
            while(last&&((static_cast<unsigned char>(shown[last])&0xc0)==0x80))--last;
            shown.resize(last);
        }
        shown+=dots;
    }
    ImGui::PopFont();
    Neurotic::Sleek::WindowSectionHeader(shown.c_str());
    if(clipped&&Neurotic::ItemHelpHovered(ImGui::GetID(title))){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*25.f);
        ImGui::TextUnformatted(full.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}

static void BeginMultipassCard(const char* id,const char* title)
{
    BeginNrCard(id,nullptr);
    if(title)MultipassTitle(title);
}

static void MultipassHeading(const char* title)
{
    ScopedNestedTextWrap wrap(0.f);
    MultipassTitle(title);
    ImGui::Separator();
}

static bool MultipassCombo(const char* title, const char* id, int& value,
                          const char* const* choices, int count, int defaultValue)
{
    ImGui::PushID(id);
    ImGui::TextWrapped("%s", title);
    const auto* reset=Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset");
    const float reserve=ImGui::CalcTextSize(reset).x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(1.f,ImGui::GetContentRegionAvail().x-reserve));
    bool changed=ImGui::Combo("##Value",&value,choices,count);
    ImGui::SameLine();
    if(ImGui::Button(reset)){value=defaultValue;changed=true;}
    ImGui::PopID();
    return changed;
}

static void MultipassSlider(const char* title, const char* id, NrOptional<float>* value,
                            float minimum,float maximum,float defaultValue=1.f,
                            bool percent=true,const char* format="%.2f")
{
    ScopedNestedTextWrap wrap(0.f);
    const std::string label=std::string(title)+"###"+id;
    DeferredNrSlider(label.c_str(),{value},minimum,maximum,defaultValue,format,percent,nullptr,nullptr,title);
}

static void RenderMultipassPassEditor(Config* config,unsigned int passCount)
{
    auto* storage=ImGui::GetStateStorage();
    const int index=storage->GetInt(ImGui::GetID("##NrMultipassEditPass"),1);
    const auto* popup=Neurotic::UiLiteral("ingame.multipass.pass_settings", "Pass settings###NrMultipassEditor");
    const bool wasOpen=ImGui::IsPopupOpen(popup);
    Neurotic::SetBoundedPopupSize(47.f,36.f);
    bool open=true;
    if(!ImGui::BeginPopupModal(popup,&open,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)){
        if(wasOpen&&!open)CancelNrEdits();return;
    }
    const bool nestedPopup=GImGui->OpenPopupStack.Size>GImGui->BeginPopupStack.Size;
    // NewFrame may deactivate an edited slider on Escape. Cancel before a
    // deactivation can reach its release-commit path below.
    if(!nestedPopup&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&ImGui::IsKeyPressed(ImGuiKey_Escape)){
        CancelNrEdits();ImGui::CloseCurrentPopup();ImGui::EndPopup();return;
    }
    if(!open||index<1||unsigned(index)>=passCount||!config->DlssNrBasicMultipass.value_or_default().advanced){
        CancelNrEdits();ImGui::CloseCurrentPopup();ImGui::EndPopup();return;
    }
    const auto pass=PassOptions(config,unsigned(index));
    ImGui::PushTextWrapPos(0);
    ImGui::BeginChild("##NrMultipassEditorBody",{0,-ImGui::GetFrameHeightWithSpacing()},ImGuiChildFlags_None);
    ImGui::PushID(index);
    const auto title=StatusPanel::Format(Neurotic::UiLiteral("ingame.multipass.pass_number","Pass %u"),unsigned(index+1));
    MultipassHeading(title.c_str());
    ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.preceding_input","Input: completed image from Pass %u."),index);
    if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.copy_preceding","Copy preceding pass"))){
        CopyPassOptions(PassOptions(config,unsigned(index-1)),pass);CancelNrEdits();
    }
    const auto* resetPass=Neurotic::UiLiteral("ingame.multipass.reset_pass","Reset pass");
    if(MenuControls::LastItemHasInlineRoom(ImGui::CalcTextSize(resetPass).x+ImGui::GetStyle().FramePadding.x*2))ImGui::SameLine();
    if(ImGui::Button(resetPass))ImGui::OpenPopup("##NrResetPassConfirm");
    Neurotic::SetBoundedPopupSize(28.f,12.f);
    if(ImGui::BeginPopupModal("##NrResetPassConfirm",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)){
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.reset_pass_question","Reset every setting for Pass %u? Other passes remain unchanged."),index+1);
        if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.confirm_ea44f268","Confirm"))){ResetPassOptions(pass);CancelNrEdits();ImGui::CloseCurrentPopup();}
        ImGui::SameLine();
        if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d","Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    {
        Neurotic::Sleek::CardColumns columns("##NrPassEditorColumns");
        BeginMultipassCard("##NrPassRendering",Neurotic::UiLiteral("ingame.multipass.rendering","Rendering"));
        bool apply=pass.applyModel->value_or_default();
        if(MenuControls::EmphasizedCheckbox(Neurotic::UiLiteral("ingame.dlssnr-menu.apply_the_model_8fad708a","Apply the model"),&apply))*pass.applyModel=apply;
        ResetButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1","Reset##PassApply"),[&]{*pass.applyModel=true;});
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.apply_help","Off hides this pass's edit. Processing continues."));
        MultipassSlider(Neurotic::UiLiteral("ingame.multipass.model_resolution","Model resolution"),"PassResolution",pass.workingScale,.25f,2.f);
        const float scale=pass.workingScale->value_or_default();
        const char* downscalers[]={"FSR1","Bicubic","Catmull-Rom","Lanczos2","Lanczos3","Kaiser2","Kaiser3","MAGIC"};
        int downscaler=std::clamp(int(pass.scalingDownscaler->value_or_default()),0,7);
        ImGui::BeginDisabled(scale<=1.f);
        if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.downscaler","Downscaler"),"PassDownscaler",downscaler,downscalers,8,4))*pass.scalingDownscaler=Scaler(downscaler);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s",Neurotic::UiLiteral("ingame.multipass.above_native","Above 100% model resolution only."));
        const char* presets[]={Neurotic::UiLiteral("ingame.menu-common.default_92fe477b","Default"),Neurotic::UiLiteral("ingame.provider.d1ced7405198","Preset 1"),Neurotic::UiLiteral("ingame.provider.c475c1784510","Preset 2"),Neurotic::UiLiteral("ingame.provider.e7aebbc38aae","Preset 3")};
        int preset=std::clamp(int(pass.preset->value_or_default()),0,3);
        if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.model_preset","Model preset"),"PassPreset",preset,presets,4,0))*pass.preset=uint32_t(preset);
        const char* styles[]={Neurotic::UiLiteral("ingame.provider.1edcf76142e5","Default (standard)"),Neurotic::UiLiteral("ingame.option.d6acb6d51cfc","Natural"),Neurotic::UiLiteral("ingame.option.912d0988b065","Cinematic")};
        int style=std::clamp(int(pass.style->value_or_default()),0,2);
        if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.style","Style"),"PassStyle",style,styles,3,0))*pass.style=uint32_t(style);
        MultipassSlider(Neurotic::UiLiteral("ingame.multipass.model_strength","Model strength"),"PassModel",pass.intensity,0.f,2.f);
        EndNrCard();columns.Next();
        BeginMultipassCard("##NrPassComposition",Neurotic::UiLiteral("ingame.multipass.composition","Composition"));
        const char* transfers[]={Neurotic::UiLiteral("ingame.option.ae25d6481fc1","Classic"),Neurotic::UiLiteral("ingame.dlssnr-menu.matched_residual_fd28d530","Matched residual")};
        int transfer=pass.transfer->value_or_default()==1?1:0;
        ImGui::BeginDisabled(scale>=1.f);
        if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.enlargement","Enlargement"),"PassEnlargement",transfer,transfers,2,1))*pass.transfer=uint32_t(transfer);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s",Neurotic::UiLiteral("ingame.multipass.below_native","Below 100% model resolution only."));
        MultipassSlider(Neurotic::UiLiteral("ingame.multipass.detail_strength","Detail strength"),"PassDetail",pass.transferStrength,0.f,2.f);
        MultipassSlider(Neurotic::UiLiteral("ingame.multipass.colour_strength","Colour strength"),"PassColour",pass.colourStrength,0.f,4.f);
        MultipassSlider(Neurotic::UiLiteral("ingame.multipass.highlight_guard","Highlight guard"),"PassHighlight",pass.maxRatio,1.f,8.f,2.f,false,"%.1fx");
        const char* proxies[]={Neurotic::UiLiteral("ingame.dlssnr-menu.soft_knee_composition_c250dfcd","Soft-knee composition"),Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_composition_d3aa36e9","Neutwo composition"),Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_pure_inverse_aad577e4","Neutwo pure inverse"),Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_composition_b4f92297","Hybrid composition"),Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_pure_inverse_4bbbf18c","Hybrid pure inverse")};
        int proxy=std::clamp(int(pass.reversibleMode->value_or_default()),0,4);
        if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.proxy_composition","Proxy composition"),"PassProxy",proxy,proxies,5,0))*pass.reversibleMode=uint32_t(proxy);
        ImGui::TextWrapped("%s",proxy==1||proxy==2?Neurotic::UiLiteral("ingame.multipass.experimental_composition","Experimental composition mode."):" ");
        EndNrCard();
    }
    BeginMultipassCard("##NrPassLocal",Neurotic::UiLiteral("ingame.multipass.local_tuning","Local tuning"));
    MultipassSlider(Neurotic::UiLiteral("ingame.multipass.local_structure","Local structure"),"PassStructure",pass.localStructure,0.f,2.f);
    MultipassSlider(Neurotic::UiLiteral("ingame.multipass.local_tone","Local tone"),"PassTone",pass.localTone,0.f,2.f);
    MultipassSlider(Neurotic::UiLiteral("ingame.multipass.skin_structure","Skin structure"),"PassSkin",pass.skinStructure,-1.f,2.f,-1.f,false);
    ImGui::TextDisabled("%s",Neurotic::UiLiteral("ingame.multipass.skin_follow","-1 follows Local structure."));
    bool mask=pass.autoMask->value_or_default();
    if(Neurotic::WrappedPopupCheckbox(Neurotic::UiLiteral("ingame.multipass.auto_skin_mask","Auto skin mask"),&mask))*pass.autoMask=mask;
    ResetButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1","Reset##PassMask"),[&]{*pass.autoMask=true;});
    EndNrCard();ImGui::PopID();ImGui::EndChild();
    // A fixed action row stays reachable when the editor body scrolls.
    if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.close","Close"))){
        CancelNrEdits();ImGui::CloseCurrentPopup();
    }
    ImGui::PopTextWrapPos();ImGui::EndPopup();
}

static void RenderMultipassObservation(Config* config,bool supported,bool enabled,bool basicMode,unsigned int requested)
{
    // Use existing completion evidence and expire a readout after progress stops.
    // A saved setting or a configured row never makes this observation green.
    const auto settings=config->GetDlssNrConfigSnapshot();
    static std::optional<NrConfigSnapshot<Config>> previous;
    static uint64_t selection=0;
    if(!previous||!previous->SameConfiguration(settings)){previous=settings;++selection;}
    const bool presentRoute=settings.DlssNrRoute.value_or_default()!=0;
    uint64_t counter=0,owner=0,route=0;
    unsigned completedPasses=0;
    bool operational=false;
    if(presentRoute){
        const auto present=PresentTelemetry();
        route=1+uint64_t(present.api);
        if(present.api==PresentApi::Vulkan){
            const auto vk=GetVulkanPresentStatus().Snapshot();
            counter=vk.completed;owner=vk.generation;
            operational=vk.requested&&vk.active&&!vk.failed&&!vk.needsRecreate;
            // Completed output is owned here; recorded pass counts are not a
            // per-fence pass-count measurement and are not presented as one.
        }else{
            counter=present.lastCompletedFence;owner=present.resourceGeneration;
            operational=present.requested&&present.active&&!present.failed&&!present.policyBlocked&&
                present.requestedPlacement=="Present";
        }
    }else{
        const auto native=SelectedNativeTelemetry();
        route=IsVulkanInput()?5:6;
        counter=native.gpuCompletedOutputEvaluations;owner=native.lifecycleGeneration;
        completedPasses=native.layerCount;
        operational=native.enabled&&native.running&&!native.failed&&!native.outputQuarantined&&
            !native.transitionPending&&!native.resetPending;
    }
    static MenuStatus::ActivityObservation observation;
    const auto identity=selection^(uint64_t(State::Instance().api)<<56)^(route<<48);
    const bool fresh=observation.Fresh(identity,counter,ImGui::GetTime(),owner);
    const bool active=settings.DlssNrEnabled.value_or_default()&&supported&&enabled&&requested>0&&operational&&fresh;
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const auto green=background.x+background.y+background.z>1.5f?ImVec4(.08f,.48f,.25f,1):ImVec4(.45f,.86f,.61f,1);
    ImGui::PushStyleColor(ImGuiCol_Text,active?green:ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if(!supported)ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.unsupported","Multipass requires a supported Native or Present adapter."));
    else if(!enabled)ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.off","Multipass off. Saved settings retained."));
    else if(basicMode&&requested==0)ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.zero","0 passes requested. Effect bypassed; loaded resources retained."));
    else if(active&&completedPasses)ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.completed","%u requested | %u completed on the last frame"),requested,completedPasses);
    else if(active)ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.output_completed","%u requested | output completed"),requested);
    else ImGui::TextWrapped(Neurotic::UiLiteral("ingame.multipass.unobserved","%u requested | completion not observed"),requested);
    ImGui::PopStyleColor();
}

static void RenderMultipassMenu(Config* config,float menuResScale)
{
    ScopedNestedTextWrap wrap;
    // Cancel before drawing any slider: NewFrame can already have deactivated
    // a keyboard edit, making this frame look like a normal release to it.
    if(ImGui::IsKeyPressed(ImGuiKey_Escape))CancelNrEdits();
    const bool supported=IsVulkanInput()||State::Instance().api==API::DX12||config->DlssNrRoute.value_or_default()!=0;
    auto basic=BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
    bool enabled=config->DlssNrMultipassEnabled.value_or_default();
    ImGui::BeginDisabled(!supported);
    if(MenuControls::EmphasizedCheckbox(Neurotic::UiLiteral("ingame.multipass.enable","Enable Multipass"),&enabled)){
        {NrConfigSynchronization::Transaction transaction;config->DlssNrMultipassEnabled=enabled;
        config->DlssNrSecondLayer=enabled&&config->DlssNrPasses.value_or_default()>1;}CancelNrEdits();
    }
    ImGui::EndDisabled();
    const char* modes[]={Neurotic::UiLiteral("ingame.option.0e35f6e9742e","Basic"),Neurotic::UiLiteral("ingame.objectruleeditor.advanced_cfbc9ae1","Advanced")};
    for(int i=0;i<2;++i){
        if(MenuControls::LastItemHasInlineRoom(ImGui::CalcTextSize(modes[i]).x+ImGui::GetStyle().FramePadding.x*2))ImGui::SameLine();
        const bool selected=basic.advanced==(i==1);
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if(ImGui::Button(modes[i])&&!selected){BasicMultipass::Update(*config,[&](auto& p){p.advanced=i==1;});CancelNrEdits();}
        if(selected)ImGui::PopStyleColor();
    }
    basic=BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
    unsigned int passCount=basic.advanced?std::clamp(config->DlssNrPasses.value_or_default(),1u,10u):BasicMultipass::Count(basic);
    RenderMultipassObservation(config,supported,enabled,!basic.advanced,passCount);
    ImGui::Spacing();
    if(!basic.advanced){
        // An external profile reload can change modes while a modal is open.
        // Submit and close that modal so it cannot leave an invisible input blocker.
        RenderMultipassPassEditor(config,0);
        BeginNrCard("##NrBasicSetup",nullptr);
        const int columns=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*42.f?3:1;
        if(ImGui::BeginTable("##NrBasicSetupColumns",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)){
            ImGui::TableNextColumn();
            const char* maxima[]={"1","2","3","4","5","6","7","8","9","10"};int maximum=int(basic.maximum)-1;
            if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.maximum","Maximum passes"),"BasicMaximum",maximum,maxima,10,0)){
                BasicMultipass::Update(*config,[&](auto& p){p.maximum=uint32_t(maximum+1);});CancelNrEdits();
            }
            ImGui::TableNextColumn();
            const auto slider=[&](const char* title,const char* id,CustomOptional<float>& preview,float BasicMultipass::Profile::* member,float minimum,float maximum,unsigned int cumulative=0){
                ScopedNestedTextWrap fieldWrap(0.f);
                BasicMultipass::Profile original;uint64_t generation;
                {NrConfigSynchronization::Transaction transaction;original=config->DlssNrBasicMultipass.value_or_default();generation=NrConfigSynchronization::ProfileGeneration();}
                preview=original.*member;const std::string label=std::string(title)+"###"+id;ImVec2 lo,hi;
                const bool changed=DeferredNrSlider<CustomOptional<float>>(label.c_str(),{&preview},minimum,maximum,1.f,"%d%%",true,&lo,&hi,title);
                if(cumulative)DrawCumulativePassSegments(lo,hi,cumulative,menuResScale);
                if(changed&&!BasicMultipass::CommitEdit(*config,original,generation,member,preview.value_or_default()))CancelNrEdits();
            };
            static CustomOptional<float> resolution{1.f},model{1.f},detail{1.f};
            slider(Neurotic::UiLiteral("ingame.multipass.model_resolution","Model resolution"),"NrBasicResolution",resolution,&BasicMultipass::Profile::resolution,.25f,2.f);
            basic=BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
            ImGui::TableNextColumn();const char* downscalers[]={"FSR1","Bicubic","Catmull-Rom","Lanczos2","Lanczos3","Kaiser2","Kaiser3","MAGIC"};int downscaler=int(basic.downscaler);
            ImGui::BeginDisabled(basic.resolution<=1.f);
            if(MultipassCombo(Neurotic::UiLiteral("ingame.multipass.downscaler","Downscaler"),"BasicDownscaler",downscaler,downscalers,8,4))BasicMultipass::Update(*config,[&](auto& p){p.downscaler=uint32_t(downscaler);});
            ImGui::EndDisabled();ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.above_native","Above 100% model resolution only."));
            ImGui::EndTable();
            MultipassHeading(Neurotic::UiLiteral("ingame.multipass.strength_totals","Strength totals"));
            slider(Neurotic::UiLiteral("ingame.multipass.model_strength","Model strength"),"NrBasicModel",model,&BasicMultipass::Profile::model,0.f,float(basic.maximum),basic.maximum);
            slider(Neurotic::UiLiteral("ingame.multipass.detail_strength","Detail strength"),"NrBasicDetail",detail,&BasicMultipass::Profile::detail,0.f,float(basic.maximum),basic.maximum);
        }
        EndNrCard();
        basic=BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
        MultipassHeading(Neurotic::UiLiteral("ingame.multipass.pass_overview","Pass overview"));
        ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.configured_contributions","Configured contributions. Totals include Pass 1."));
        if(ImGui::BeginTable("##NrBasicOverview",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchSame)){
            for(const char* column:{Neurotic::UiLiteral("ingame.multipass.pass","Pass"),Neurotic::UiLiteral("ingame.multipass.model","Model"),Neurotic::UiLiteral("ingame.multipass.detail","Detail"),Neurotic::UiLiteral("ingame.multipass.configured","Configured")})ImGui::TableSetupColumn(column);
            ImGui::TableHeadersRow();
            for(unsigned int i=0;i<basic.maximum;++i){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%u",i+1);
                ImGui::TableNextColumn();ImGui::Text("%d%%",int(lroundf(BasicMultipass::Strength(basic.model,i)*100)));
                ImGui::TableNextColumn();ImGui::Text("%d%%",int(lroundf(BasicMultipass::Strength(basic.detail,i)*100)));
                ImGui::TableNextColumn();ImGui::TextWrapped("%s",i<BasicMultipass::Count(basic)?Neurotic::UiLiteral("ingame.multipass.requested","Requested"):Neurotic::UiLiteral("ingame.multipass.not_requested","Not requested"));}
            ImGui::EndTable();
        }
        ImGui::Spacing();ImGui::Separator();
        ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.shared_look","Preset, style and other image settings follow Rendering."));
        if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.open_rendering","Rendering settings >")))requestedMenuPage=MenuPage::Overview;
        return;
    }
    // Additional profiles remain stored when the requested count is reduced.
    static unsigned int previousCount=0;
    RenderPassCountSelector(config);
    passCount=std::clamp(config->DlssNrPasses.value_or_default(),1u,10u);
    if(previousCount!=passCount){previousCount=passCount;CancelNrEdits();}
    if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.reset_additional","Reset additional passes")))ImGui::OpenPopup("##NrResetAdditionalConfirm");
    Neurotic::SetBoundedPopupSize(31.f,15.f);
    if(ImGui::BeginPopupModal("##NrResetAdditionalConfirm",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)){
        ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.reset_additional_question","Reset all nine additional pass profiles and turn Multipass off? Pass 1 and the pass count remain unchanged."));
        if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.confirm_ea44f268","Confirm"))){
            {NrConfigSynchronization::Transaction transaction;config->DlssNrMultipassEnabled=false;config->DlssNrSecondLayer=false;
            for(unsigned int i=1;i<10;++i)ResetPassOptions(PassOptions(config,i));}CancelNrEdits();ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d","Cancel"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(passCount>1){
        BeginMultipassCard("##NrAdvancedShared",Neurotic::UiLiteral("ingame.multipass.shared_additional","Shared settings · additional passes"));
        const auto shared=[&](const char* title,const char* id,NrOptional<float>* PassOptionRefs::* member,float minimum,float maximum){
            ScopedNestedTextWrap fieldWrap(0.f);
            std::vector<NrOptional<float>*> targets;for(unsigned int i=1;i<passCount;++i)targets.push_back(PassOptions(config,i).*member);
            const std::string label=std::string(title)+"###"+id;
            DeferredNrSlider(label.c_str(),targets,minimum,maximum,1.f,"%d%%",true,nullptr,nullptr,title);
            if(PendingNrEdits<NrOptional<float>>()[label].Mixed())ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.mixed","Mixed values. Adjust to apply one value to all additional passes."));
        };
        shared(Neurotic::UiLiteral("ingame.multipass.model_resolution","Model resolution"),"AdditionalPassModelResolution",&PassOptionRefs::workingScale,.25f,2.f);
        shared(Neurotic::UiLiteral("ingame.multipass.model_strength","Model strength"),"AdditionalPassModelStrength",&PassOptionRefs::intensity,0.f,2.f);
        shared(Neurotic::UiLiteral("ingame.multipass.detail_strength","Detail strength"),"AdditionalPassDetailStrength",&PassOptionRefs::transferStrength,0.f,2.f);
        ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.shared_scope","Applies to Pass 2 onward. Pass 1 remains unchanged."));EndNrCard();
    }
    MultipassHeading(Neurotic::UiLiteral("ingame.multipass.pass_overview","Pass overview"));
    ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.configured_values","Configured values. Model edit shows the saved Apply setting."));
    int editPass=-1;
    // Horizontal scrolling preserves readable columns on narrow menus.
    const char* columns[]={Neurotic::UiLiteral("ingame.multipass.pass","Pass"),Neurotic::UiLiteral("ingame.multipass.resolution","Resolution"),Neurotic::UiLiteral("ingame.multipass.model","Model"),Neurotic::UiLiteral("ingame.multipass.detail","Detail"),Neurotic::UiLiteral("ingame.multipass.model_edit","Model edit"),Neurotic::UiLiteral("ingame.multipass.settings","Settings")};
    const auto textWidth=[](const char* text){const auto translated=Neurotic::Translate(text);return ImGui::CalcTextSize(translated.c_str()).x;};
    std::array<float,6> widths{};
    for(size_t i=0;i<widths.size();++i)widths[i]=(std::max)(textWidth(columns[i]),ImGui::GetFontSize()*3.f);
    widths[4]=(std::max)({widths[4],textWidth(Neurotic::UiLiteral("ingame.multipass.visible","Visible")),textWidth(Neurotic::UiLiteral("ingame.multipass.hidden","Hidden"))});
    widths[5]=(std::max)({widths[5],textWidth(Neurotic::UiLiteral("ingame.multipass.rendering_link","Rendering >"))+ImGui::GetStyle().FramePadding.x*2,textWidth(Neurotic::UiLiteral("ingame.multipass.edit","Edit"))+ImGui::GetStyle().FramePadding.x*2});
    // WidthFixed requests describe content widths. With no vertical borders,
    // ImGui adds two CellPadding gaps between each pair of columns separately.
    float innerWidth=2.f*ImGui::GetStyle().CellPadding.x*(widths.size()-1);
    for(float& width:widths){width+=ImGui::GetStyle().CellPadding.x*2+2.f;innerWidth+=width;}
    const float extra=(std::max)(0.f,ImGui::GetContentRegionAvail().x-innerWidth)/6.f;
    for(float& width:widths)width+=extra;
    innerWidth=(std::max)(innerWidth,ImGui::GetContentRegionAvail().x);
    // Table rows add CellPadding on both sides, not ordinary ItemSpacing.
    const float rowHeight=ImGui::GetFrameHeight()+ImGui::GetStyle().CellPadding.y*2;
    const float headerHeight=ImGui::GetTextLineHeight()+ImGui::GetStyle().CellPadding.y*2;
    const float tableHeight=passCount*rowHeight+headerHeight+ImGui::GetStyle().ScrollbarSize+2.f;
    if(ImGui::BeginTable("##NrAdvancedOverview",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollX|ImGuiTableFlags_SizingFixedFit,{0,tableHeight},innerWidth)){
        ImGui::PushTextWrapPos(-1.f);
        for(size_t i=0;i<widths.size();++i)ImGui::TableSetupColumn(columns[i],ImGuiTableColumnFlags_WidthFixed,widths[i]);
        ImGui::TableHeadersRow();
        for(unsigned int i=0;i<passCount;++i){const auto pass=PassOptions(config,i);ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%u",i+1);
            ImGui::TableNextColumn();ImGui::Text("%d%%",int(lroundf(pass.workingScale->value_or_default()*100)));
            ImGui::TableNextColumn();ImGui::Text("%d%%",int(lroundf(pass.intensity->value_or_default()*100)));
            ImGui::TableNextColumn();ImGui::Text("%d%%",int(lroundf(pass.transferStrength->value_or_default()*100)));
            ImGui::TableNextColumn();ImGui::TextUnformatted(pass.applyModel->value_or_default()?Neurotic::UiLiteral("ingame.multipass.visible","Visible"):Neurotic::UiLiteral("ingame.multipass.hidden","Hidden"));
            ImGui::TableNextColumn();if(i==0){if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.rendering_link","Rendering >")))requestedMenuPage=MenuPage::Overview;}
            else if(ImGui::Button(Neurotic::UiLiteral("ingame.multipass.edit","Edit"))){
                // Write the selection in the parent after the scrolling table closes.
                editPass=int(i);CancelNrEdits();
            }
            ImGui::PopID();
        }
        ImGui::PopTextWrapPos();ImGui::EndTable();
    }
    if(editPass>0){ImGui::GetStateStorage()->SetInt(ImGui::GetID("##NrMultipassEditPass"),editPass);ImGui::OpenPopup(Neurotic::UiLiteral("ingame.multipass.pass_settings","Pass settings###NrMultipassEditor"));}
    if(passCount>=4)ImGui::TextWrapped("%s",Neurotic::UiLiteral("ingame.multipass.high_count","High pass counts are experimental and may exhaust GPU memory or frame time."));
    RenderMultipassPassEditor(config,passCount);
}
