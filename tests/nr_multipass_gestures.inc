// Exercises production RenderMultipassMenu and native ImGui input over frames.
// Catches dropped drafts, early commits and Escape committing before cancellation.
static void MultipassGestures(Config* config) {
    auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiID activate=0;bool openEditor=false;
    auto frame=[&]{
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1200,1700});
        ImGui::Begin("Gesture fixture",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar);
        if(activate){GImGui->NavActivateId=GImGui->NavActivateDownId=activate;activate=0;}
        if(openEditor){ImGui::GetStateStorage()->SetInt(ImGui::GetID("##NrMultipassEditPass"),1);ImGui::OpenPopup("Pass settings###NrMultipassEditor");openEditor=false;}
        DlssNr::RenderMultipassMenu(config,1.f);ImGui::End();ImGui::Render();
    };
    auto fail=[](const std::string& name,const char* reason){throw std::runtime_error(name+": "+reason);};
    for(int mode:{0,1,2})for(bool keyboard:{false,true})for(bool escape:{false,true}) {
        const bool advanced=mode!=0,modal=mode==2;
        const std::string name=std::string(modal?"modal":advanced?"advanced":"basic")+(keyboard?" keyboard":" mouse")+(escape?" cancel":" release");
        if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
        ImGui::ClearActiveID();DlssNr::pendingNrEdits.clear();
        auto profile=DlssNr::BasicMultipass::Profile{};profile.advanced=advanced;profile.maximum=3;profile.model=1;profile.detail=1;
        config->DlssNrBasicMultipass=profile;config->DlssNrPasses=3u;config->DlssNrMultipassEnabled=true;
        for(unsigned i=0;i<10;++i)DlssNr::ResetPassOptions(DlssNr::PassOptions(config,i));
        auto value=[&]{return advanced?DlssNr::PassOptions(config,1).intensity->value_or_default():config->DlssNrBasicMultipass.value_or_default().model;};
        io.DisplaySize={1200,1700};io.AddMousePosEvent(-100,-100);frame();frame();
        if(modal){openEditor=true;frame();frame();frame();}
        const char* field=modal?"###PassModel":advanced?"###AdditionalPassModelStrength":"###NrBasicModel";
        const auto item=MultipassFixture::items.at(field);
        if(keyboard) {
            ImGui::SetNavID(item.id,ImGuiNavLayer_Main,0,item.rect);GImGui->NavWindow=item.window;
            GImGui->NavInputSource=ImGuiInputSource_Keyboard;activate=item.id;frame();
            io.AddKeyEvent(ImGuiKey_RightArrow,true);frame();io.AddKeyEvent(ImGuiKey_RightArrow,false);frame();
        } else {
            io.AddMousePosEvent(item.rect.Min.x+item.rect.GetWidth()*.65f,item.rect.GetCenter().y);frame();
            io.AddMouseButtonEvent(0,true);frame();
            io.AddMousePosEvent(item.rect.Min.x+item.rect.GetWidth()*.80f,item.rect.GetCenter().y);frame();frame();
        }
        if(value()!=1.f)fail(name,"saved value changed during gesture");
        float preview=1.f;for(auto& [key,edit]:DlssNr::pendingNrEdits)
            if(key.find(field)!=std::string::npos)preview=edit.Value();
        if(preview==1.f)fail(name,"real input produced no retained preview");
        if(escape){io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);}
        else if(keyboard){activate=item.id;}
        else io.AddMouseButtonEvent(0,false);
        frame();io.AddMouseButtonEvent(0,false);frame();
        if(escape?value()!=1.f:std::abs(value()-preview)>.0001f)fail(name,"release/cancel saved the wrong value");
        if(advanced&&(DlssNr::PassOptions(config,0).intensity->value_or_default()!=1.f||
             DlssNr::PassOptions(config,2).intensity->value_or_default()!=(modal?1.f:value())||DlssNr::PassOptions(config,3).intensity->value_or_default()!=1.f))
            fail(name,"shared edit escaped additional requested passes");
        if(modal&&escape&&!GImGui->OpenPopupStack.empty())fail(name,"Escape left editor open");
        std::cout<<"PASS gesture "<<name<<" preview="<<preview<<" saved="<<value()<<"\n";
    }
    for(int transition:{0,1,2}) {
        if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
        ImGui::ClearActiveID();DlssNr::pendingNrEdits.clear();
        auto profile=DlssNr::BasicMultipass::Profile{};profile.advanced=true;
        config->DlssNrBasicMultipass=profile;config->DlssNrPasses=3u;
        for(unsigned i=0;i<10;++i)DlssNr::ResetPassOptions(DlssNr::PassOptions(config,i));
        io.AddMousePosEvent(-100,-100);frame();frame();
        auto item=MultipassFixture::items.at("###AdditionalPassModelStrength");
        io.AddMousePosEvent(item.rect.Min.x+item.rect.GetWidth()*.8f,item.rect.GetCenter().y);frame();
        io.AddMouseButtonEvent(0,true);frame();frame();
        if(transition==0)DlssNr::BasicMultipass::Update(*config,[](auto& p){p.advanced=false;});
        else if(transition==1)config->DlssNrPasses=2u;
        else {NrConfigSynchronization::InvalidateProfileEdits();*DlssNr::PassOptions(config,1).intensity=.7f;}
        frame();io.AddMouseButtonEvent(0,false);frame();frame();
        if(DlssNr::PassOptions(config,1).intensity->value_or_default()!=(transition==2?.7f:1.f)||DlssNr::PassOptions(config,2).intensity->value_or_default()!=1.f)
            fail("transition", "draft overwrote mode/count/profile transition");
        std::cout<<"PASS transition "<<transition<<"\n";
    }
    if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
    auto values=[](const DlssNr::PassOptionRefs& p){return std::array<float,15>{p.workingScale->value_or_default(),float(p.scalingDownscaler->value_or_default()),float(p.transfer->value_or_default()),float(p.preset->value_or_default()),p.intensity->value_or_default(),float(p.style->value_or_default()),p.localStructure->value_or_default(),p.localTone->value_or_default(),p.skinStructure->value_or_default(),float(p.autoMask->value_or_default()),p.transferStrength->value_or_default(),p.colourStrength->value_or_default(),p.maxRatio->value_or_default(),float(p.reversibleMode->value_or_default()),float(p.applyModel->value_or_default())};};
    const std::array<float,15> defaults={1,4,1,0,1,0,1,1,-1,1,1,1,2,0,1};
    const std::array<float,15> custom={.5f,2,0,3,.6f,2,.3f,.4f,.2f,0,.7f,.8f,3,4,0};
    auto first=DlssNr::PassOptions(config,0);
    *first.workingScale=.5f;*first.scalingDownscaler=Scaler(2);*first.transfer=0u;*first.preset=3u;*first.intensity=.6f;*first.style=2u;
    *first.localStructure=.3f;*first.localTone=.4f;*first.skinStructure=.2f;*first.autoMask=false;
    *first.transferStrength=.7f;*first.colourStrength=.8f;*first.maxRatio=3.f;*first.reversibleMode=4u;*first.applyModel=false;
    for(unsigned i=1;i<10;++i)DlssNr::ResetPassOptions(DlssNr::PassOptions(config,i));
    DlssNr::BasicMultipass::Update(*config,[](auto& p){p.advanced=true;});config->DlssNrPasses=3u;
    io.AddMousePosEvent(-100,-100);frame();openEditor=true;frame();frame();
    activate=MultipassFixture::items.at("Copy preceding pass").id;frame();frame();
    if(values(DlssNr::PassOptions(config,1))!=custom||values(first)!=custom)fail("copy", "did not copy all 15 fields");
    activate=MultipassFixture::items.at("Reset pass").id;frame();frame();
    activate=MultipassFixture::items.at("Confirm").id;frame();frame();
    if(values(DlssNr::PassOptions(config,1))!=defaults||values(first)!=custom)fail("reset", "wrong 15-field scope");
    for(unsigned i=2;i<10;++i)if(values(DlssNr::PassOptions(config,i))!=defaults)fail("copy/reset", "changed another profile");
    std::cout<<"PASS UI copy/reset all 15 fields; Pass1 and inactive profiles preserved\n";
    if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
    io.AddMousePosEvent(-100,-100);ImGui::ClearActiveID();DlssNr::pendingNrEdits.clear();
}
