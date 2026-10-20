// Only externally published snapshots are supplied here. The production route
// selection, freshness policy and text formatting run unchanged in the fixture.
static void MultipassStatusCases(Config* config) {
    auto& io=ImGui::GetIO();io.DisplaySize={1200,1700};
    auto observe=[&](bool enabled,unsigned requested,bool expected,const char* name) {
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::Begin("Status fixture");
        DlssNr::RenderMultipassObservation(config,true,enabled,true,requested);
        const auto text=MultipassFixture::text;
        const bool active=text.find("completed")!=std::string::npos;
        ImGui::End();ImGui::Render();
        if(active!=expected)throw std::runtime_error(std::string(name)+": "+text);
        std::cout<<"PASS status "<<name<<": "<<text<<"\n";
    };
    auto& n=MultipassFixture::native;auto& p=MultipassFixture::present;auto& vk=MultipassFixture::vk;
    config->SetDlssNrEnabled(true);config->DlssNrRoute=0u;State::Instance().api=API::DX12;
    n={};n.enabled=n.running=true;n.lifecycleGeneration=5;n.layerCount=3;n.gpuCompletedOutputEvaluations=100;
    p={};p.requested=p.active=true;p.api=DlssNr::PresentApi::D3D12;p.requestedPlacement="Present";
    p.resourceGeneration=10;p.lastCompletedFence=20;
    observe(true,3,false,"native baseline");++n.gpuCompletedOutputEvaluations;observe(true,3,true,"native progress");
    config->DlssNrRoute=1u;observe(true,3,false,"route switch baselines existing present work");
    ++n.gpuCompletedOutputEvaluations;observe(true,3,false,"native progress cannot qualify present");
    ++p.lastCompletedFence;observe(true,3,true,"present progress");
    ++p.resourceGeneration;observe(true,3,false,"new present owner needs progress");
    ++p.lastCompletedFence;observe(true,3,true,"new present owner progresses");
    p.failed=true;observe(true,3,false,"failed present");p.failed=false;
    observe(false,3,false,"multipass off");observe(true,0,false,"zero requested");
    io.DeltaTime=1.1f;observe(true,3,false,"stale present");io.DeltaTime=1.f/60.f;
    p.api=DlssNr::PresentApi::Vulkan;vk={};vk.requested=vk.active=true;vk.generation=7;vk.completed=500;
    vk.completedPasses=3;observe(true,3,false,"Vulkan baseline");
    ++vk.completed;observe(true,3,true,"Vulkan present progresses");
    ++vk.generation;observe(true,3,false,"Vulkan owner replaced");
    ++vk.completed;vk.needsRecreate=true;observe(true,3,false,"Vulkan recovery pending");
    vk.needsRecreate=false;++vk.completed;observe(true,3,true,"Vulkan recovered");
    config->SetDlssNrEnabled(false);observe(true,3,false,"parent NR off");
}

static void MultipassLocalization(Config* config) {
    Neurotic::Localization::LanguagePack pack;pack.id="fixture-bindings";pack.locale="en";
    for(const auto& [id,entry]:Neurotic::Localization::CanonicalEnglish()){
        if(id=="ingame.menu-common.reset_14de5ef1")pack.entries[id]={"Zuruck", "Fixture",entry.revision};
        if(id=="ingame.provider.d1ced7405198")pack.entries[id]={"Modell Eins","Fixture",entry.revision};
        if(id=="ingame.provider.c475c1784510")pack.entries[id]={"Modell Zwei","Fixture",entry.revision};
        if(id=="ingame.provider.e7aebbc38aae")pack.entries[id]={"Modell Drei","Fixture",entry.revision};
    }
    Neurotic::Localization::SetSharedCatalog(std::move(pack));
    DlssNr::BasicMultipass::Update(*config,[](auto& p){p.advanced=true;});config->DlssNrPasses=3u;
    ImGui::GetIO().DisplaySize={1200,1700};
    for(int i=0;i<4;++i){
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowSize({1200,1700});ImGui::Begin("Localization fixture");
        if(i==1){ImGui::GetStateStorage()->SetInt(ImGui::GetID("##NrMultipassEditPass"),1);ImGui::OpenPopup("Pass settings###NrMultipassEditor");}
        DlssNr::RenderMultipassMenu(config,1.f);ImGui::End();ImGui::Render();
    }
    for(const char* id:{"Reset##PassApply","Reset##PassMask"}){
        // Localization resolves visible text only. ImGui hashes the original
        // widget label separately, retaining its hidden identity suffix.
        if(MultipassFixture::smallCaptions.at(id)!="Zuruck")throw std::runtime_error(std::string("Untranslated moved control: ")+id+" resolved="+MultipassFixture::smallCaptions.at(id));
        const auto item=MultipassFixture::items.at(id);
        if(item.id!=item.window->GetID(id))throw std::runtime_error(std::string("Translated widget identity changed: ")+id);
    }
    if(MultipassFixture::presetCaptions!=std::vector<std::string>{"Modell Eins","Modell Zwei","Modell Drei"})throw std::runtime_error("Untranslated model preset options");
    std::cout<<"PASS existing reset/preset translations and stable widget suffixes\n";
    ImGui::ClosePopupToLevel(0,true);Neurotic::Localization::SetSharedCatalog({});
}
