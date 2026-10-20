// MP006: drive the actual editor, shared catalog, header and tooltip. The
// fixture only observes strings passed to real drawing calls and item bounds.
template<class Capture>
static void MultipassHeadingLocalization(Config* config,Capture capture) {
    auto& io=ImGui::GetIO();io.DisplaySize={1200,1300};io.DeltaTime=.1f;
    DlssNr::BasicMultipass::Update(*config,[](auto& p){p.advanced=true;});
    config->DlssNrPasses=3u;config->DlssNrMultipassEnabled=true;
    for(unsigned p=0;p<10;++p)DlssNr::ResetPassOptions(DlssNr::PassOptions(config,p));
    const auto& english=Neurotic::Localization::CanonicalEnglish().at("ingame.multipass.pass_number");
    auto frame=[&]{
        MultipassFixture::headings.clear();MultipassFixture::headingTooltip.clear();
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Heading fixture",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar);
        DlssNr::RenderMultipassMenu(config,1.f);ImGui::End();ImGui::Render();
    };
    const std::string unit="Pr\xc3\xbc" "fung ";
    std::string longPrefix;for(int i=0;i<38;++i)longPrefix+=unit;
    for(const bool longText:{false,true}) {
        Neurotic::Localization::LanguagePack pack;pack.id="fixture-pass-heading";pack.locale="de";
        const auto prefix=longText?longPrefix:std::string("Durchgang ");
        pack.entries["ingame.multipass.pass_number"]={prefix+"%u","Fixture",english.revision};
        Neurotic::Localization::SetSharedCatalog(std::move(pack));
        io.AddMousePosEvent(-100,-100);frame();frame();
        // Use the actual final additional-pass Edit button, including ImGui's
        // ordinary focus transfer, rather than opening the popup in test code.
        const auto opener=MultipassFixture::items.at("Edit");
        io.AddMousePosEvent(opener.rect.GetCenter().x,opener.rect.GetCenter().y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();frame();
        const std::string expected=prefix+"3";
        auto found=std::find_if(MultipassFixture::headings.begin(),MultipassFixture::headings.end(),
            [&](const auto& heading){return heading.full==expected;});
        if(found==MultipassFixture::headings.end())throw std::runtime_error("MP006 editor heading did not resolve canonical translated template");
        const auto heading=*found;
        if(heading.rect.Max.x>heading.right+1.f)throw std::runtime_error("MP006 editor heading escaped its bounds");
        if(longText) {
            if(!heading.clipped||!heading.shown.ends_with("..."))throw std::runtime_error("MP006 long heading was not bounded");
            const auto prefixLength=heading.shown.size()-3;
            if(expected.compare(0,prefixLength,heading.shown,0,prefixLength)!=0||
               (prefixLength<expected.size()&&(static_cast<unsigned char>(expected[prefixLength])&0xc0)==0x80))
                throw std::runtime_error("MP006 truncation split a UTF8 character");
            io.AddMousePosEvent(heading.rect.GetCenter().x,heading.rect.GetCenter().y);
            for(int i=0;i<12;++i)frame();
            if(MultipassFixture::headingTooltip!=expected)throw std::runtime_error("MP006 full multibyte heading missing from actual tooltip");
            capture("heading-long-hover");
        } else {
            if(heading.clipped||heading.shown!=expected)throw std::runtime_error("MP006 short translated heading changed");
            capture("heading-german");
        }
        io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();frame();
        if(!GImGui->OpenPopupStack.empty())throw std::runtime_error("MP006 Escape left editor open");
        if(GImGui->NavWindow!=opener.window||GImGui->NavId!=opener.id)
            throw std::runtime_error("MP006 focus did not return to actual opening Edit button");
        std::cout<<"PASS MP006 "<<(longText?"long UTF8 bounded heading and full hover":"canonical German heading")<<"; Escape restores opening Edit focus\n";
    }
    Neurotic::Localization::SetSharedCatalog({});io.DeltaTime=1.f/60.f;io.AddMousePosEvent(-100,-100);
}
