// Public WARP installed hooks plus a synthetic owning COM wrapper ABI.
// No private wrapper/provider/game or command submission. RT setter observations
// below are deliberately observer-only; WARP ray-tracing support is not assumed.
static NativeRecordingHooksTestAccess::BiasFn biasPriorOriginal;
static std::function<void()> biasInstalledReentry;
static unsigned biasInstalledCalls;
static std::array<std::uint32_t,3> biasInstalledLast;
static unsigned biasPublicReentries;
static std::vector<std::pair<std::uintptr_t,std::array<std::uint32_t,3>>> biasInstalledTrace;
__declspec(noinline) static void STDMETHODCALLTYPE BiasInstalledOriginal(ID3D12GraphicsCommandList9* list,float a,float b,float c)
{
    ++biasInstalledCalls;biasInstalledLast={std::bit_cast<std::uint32_t>(a),std::bit_cast<std::uint32_t>(b),std::bit_cast<std::uint32_t>(c)};
    biasInstalledTrace.emplace_back(reinterpret_cast<std::uintptr_t>(list),biasInstalledLast);
    biasPriorOriginal(list,a,b,c);
    if(biasInstalledReentry){auto action=std::move(biasInstalledReentry);biasInstalledReentry={};action();}
}
struct BiasOriginalScope
{
    BiasOriginalScope(){biasPriorOriginal=NativeRecordingHooksTestAccess::ExchangeBiasOriginal(BiasInstalledOriginal);}
    ~BiasOriginalScope(){NativeRecordingHooksTestAccess::ExchangeBiasOriginal(biasPriorOriginal);biasInstalledReentry={};}
};
struct BiasOuterProbe
{
    // Deliberate COM ABI fixture: only declared methods are invoked; remaining
    // entries are route-pin sentinels, never substitute native implementations.
    void** methods=nullptr;std::array<void*,86> table{};ULONG references=1;
    ComPtr<ID3D12GraphicsCommandList> terminal;ComPtr<ID3D12GraphicsCommandList9> native9;
    bool allow9=true;BiasOuterProbe* foreign9=nullptr;
    enum class Effect{Exact,Changed,Missing,Duplicate,Extra};Effect effect=Effect::Exact;
    unsigned biasCalls=0,alternateCalls=0;
    static HRESULT STDMETHODCALLTYPE Query(BiasOuterProbe* self,REFIID iid,void** out)
    {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==DlssNr::NativeIdentity::streamlineBase)return self->terminal->QueryInterface(__uuidof(IUnknown),out);
        if(iid==__uuidof(ID3D12GraphicsCommandList9)){
            if(!self->allow9)return E_NOINTERFACE;
            auto* chosen=self->foreign9?self->foreign9:self;*out=chosen;Add(chosen);return S_OK;
        }
        if(iid==__uuidof(IUnknown)||iid==__uuidof(ID3D12Object)||iid==__uuidof(ID3D12DeviceChild)||
            iid==__uuidof(ID3D12CommandList)||iid==__uuidof(ID3D12GraphicsCommandList)){
            *out=self;Add(self);return S_OK;
        }
        return E_NOINTERFACE;
    }
    static ULONG STDMETHODCALLTYPE Add(BiasOuterProbe* self){return ++self->references;}
    static ULONG STDMETHODCALLTYPE Release(BiasOuterProbe* self){auto n=--self->references;if(!n)delete self;return n;}
    __declspec(noinline) static void STDMETHODCALLTYPE Sentinel(BiasOuterProbe*){throw std::runtime_error("unused wrapper route invoked");}
    __declspec(noinline) static void STDMETHODCALLTYPE Heaps(BiasOuterProbe* self,UINT count,ID3D12DescriptorHeap* const* heaps)
    {self->terminal->SetDescriptorHeaps(count,heaps);}
    __declspec(noinline) static void STDMETHODCALLTYPE Pipeline(BiasOuterProbe* self,ID3D12PipelineState* value)
    {self->terminal->SetPipelineState(value);}
    __declspec(noinline) static void STDMETHODCALLTYPE Bias(BiasOuterProbe* self,float a,float b,float c)
    {
        ++self->biasCalls;if(self->effect==Effect::Missing)return;
        self->native9->RSSetDepthBias(self->effect==Effect::Changed?a+1.0f:a,b,c);
        if(self->effect==Effect::Duplicate)self->native9->RSSetDepthBias(a,b,c);
        if(self->effect==Effect::Extra){
            ID3D12DescriptorHeap* emptyHeaps[]={nullptr};
            self->terminal->SetDescriptorHeaps(0,emptyHeaps);
        }
    }
    __declspec(noinline) static void STDMETHODCALLTYPE AlternateBias(BiasOuterProbe* self,float a,float b,float c)
    {++self->alternateCalls;Bias(self,a,b,c);}
    explicit BiasOuterProbe(ID3D12GraphicsCommandList* value):terminal(value)
    {
        Ok(terminal.As(&native9));table.fill(reinterpret_cast<void*>(&Sentinel));methods=table.data();
        table[0]=reinterpret_cast<void*>(&Query);table[1]=reinterpret_cast<void*>(&Add);table[2]=reinterpret_cast<void*>(&Release);
        table[25]=reinterpret_cast<void*>(&Pipeline);table[28]=reinterpret_cast<void*>(&Heaps);table[82]=reinterpret_cast<void*>(&Bias);
    }
};
struct ObservedRtObject final:IUnknown
{
    ULONG references=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override
    {if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--references;if(!n)delete this;return n;}
};
static std::vector<ComPtr<ID3D12GraphicsCommandList>> DepthBiasInstalledRegressions(ID3D12Device* device,NativeRecordingHooks& hooks,NativeRecordingObserver& observer)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 support{};
    if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16,&support,sizeof(support)))||!support.DynamicDepthBiasSupported){
        std::cout<<"UNTESTED installed depth-bias boundaries: public WARP dynamic bias unavailable\n";return {};
    }
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12GraphicsCommandList9> version9;
    if(FAILED(list.As(&version9))){std::cout<<"UNTESTED installed depth-bias boundaries: public WARP v9 unavailable\n";Ok(list->Close());return {};}
    Ok(list->Close());
    D3D12_ROOT_SIGNATURE_DESC rd{};ComPtr<ID3DBlob> blob,error;
    Ok(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
    ComPtr<ID3D12RootSignature> root;Ok(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto vs=Compile("float4 main(uint id:SV_VertexID):SV_Position{return float4(float(id),0,0,1);}","vs_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};gd.pRootSignature=root.Get();gd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    gd.SampleMask=UINT_MAX;gd.SampleDesc.Count=1;gd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    gd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    gd.DepthStencilState.FrontFace=gd.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    gd.Flags=D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS;
    ComPtr<ID3D12PipelineState> pipeline;Ok(device->CreateGraphicsPipelineState(&gd,IID_PPV_ARGS(&pipeline)));
    const auto begin=[&]{Ok(list->Reset(allocator.Get(),pipeline.Get()));version9->RSSetDepthBias(64.0f,.25f,.5f);};
    for(int changed=-1;changed<3;++changed){
        begin();auto originals=hooks.Originals();
        auto saved=observer.CaptureRestorable(list.Get(),RestoreMask::Pipeline,originals);Check(bool(saved),"installed82 snapshot");
        const auto incarnation=observer.Observe(list.Get()).incarnation;
        list->SetPipelineState(pipeline.Get());version9->RSSetDepthBias(8.0f,0,0);
        const std::array<float,3> expected{64.0f,.25f,.5f};auto nested=expected;if(changed>=0)nested[changed]+=.125f;
        biasInstalledCalls=0;biasPublicReentries=0;biasInstalledTrace.clear();
        {
            BiasOriginalScope original;
            biasInstalledReentry=[&]{++biasPublicReentries;version9->RSSetDepthBias(nested[0],nested[1],nested[2]);};
            const bool restored=observer.Restore(list.Get(),incarnation,*saved,originals);
            Check(restored==(changed<0),"actual installed82 reentry exact-bits commit boundary");
        }
        const std::array<std::uint32_t,3> expectedBits{std::bit_cast<std::uint32_t>(expected[0]),
            std::bit_cast<std::uint32_t>(expected[1]),std::bit_cast<std::uint32_t>(expected[2])};
        const std::array<std::uint32_t,3> nestedBits{std::bit_cast<std::uint32_t>(nested[0]),
            std::bit_cast<std::uint32_t>(nested[1]),std::bit_cast<std::uint32_t>(nested[2])};
        std::vector<std::pair<std::uintptr_t,std::vector<std::array<std::uint32_t,3>>>> identities;
        bool routesKnown=true,rootSeen=false;
        std::cout<<"INSTALLED82 reentryAxis="<<changed<<" originalCallbacks="<<biasInstalledCalls<<" publicReentries="<<biasPublicReentries<<'\n';
        for(const auto& event:biasInstalledTrace){
            std::cout<<" callbackList="<<std::hex<<event.first<<" rawBits="<<event.second[0]<<','<<event.second[1]<<','<<event.second[2]<<std::dec<<'\n';
            auto* observed=reinterpret_cast<ID3D12GraphicsCommandList*>(event.first);
            rootSeen|=observed==list.Get();
            routesKnown&=observed==list.Get()||NativeRecordingHooksTestAccess::KnownBiasForwarding(hooks,list.Get(),observed);
            auto group=std::find_if(identities.begin(),identities.end(),[&](const auto& entry){return entry.first==event.first;});
            if(group==identities.end()){identities.push_back({event.first,{}});group=std::prev(identities.end());}
            group->second.push_back(event.second);
        }
        // Require the exact two requests in order at EACH authenticated list.
        // Extra terminal calls, reordered inputs or unknown aliases fail. Only
        // the number of genuinely observed forwarding identities is variable.
        const std::vector<std::array<std::uint32_t,3>> sequence{expectedBits,nestedBits};
        Check(biasPublicReentries==1&&rootSeen&&routesKnown&&!identities.empty()&&
            std::all_of(identities.begin(),identities.end(),[&](const auto& entry){return entry.second==sequence;}),
            "each covered installed82 identity forwards exactly restored then nested request");
        Check(observer.Observe(list.Get()).completeCoverage==(changed<0),"actual changed82 reentry retires authority");
        Ok(list->Close());
    }
    ComPtr<ID3D12GraphicsCommandList> outerOwner,foreignOwner;
    auto* outer=new BiasOuterProbe(list.Get());outerOwner.Attach(reinterpret_cast<ID3D12GraphicsCommandList*>(outer));
    auto* foreign=new BiasOuterProbe(list.Get());foreignOwner.Attach(reinterpret_cast<ID3D12GraphicsCommandList*>(foreign));
    Check(hooks.InstallWrapperHeapHook(outerOwner.Get()),"actual synthetic owning-wrapper heap hook installed");
    for(auto effect:{BiasOuterProbe::Effect::Exact,BiasOuterProbe::Effect::Changed,BiasOuterProbe::Effect::Missing,
                    BiasOuterProbe::Effect::Duplicate,BiasOuterProbe::Effect::Extra}){
        begin();outer->effect=effect;outer->biasCalls=0;
        auto context=NativeRecordingReplayContext::Prepare(hooks,outerOwner.Get());
        Check(context->Valid()&&context->version9.Get()==reinterpret_cast<ID3D12GraphicsCommandList9*>(outer),"production context acquires same-IUnknown wrapper v9");
        const auto incarnation=observer.Observe(list.Get()).incarnation;
        auto plan=NativeRecordingReplayPlan::Make(hooks,context,incarnation,RestoreMask::Pipeline,0,nullptr);
        Check(!plan.refusal&&bool(plan.setters.depthBias),"production wrapper plan has pinned82 callback");
        auto saved=observer.CaptureRestorable(list.Get(),RestoreMask::Pipeline,plan.setters);Check(bool(saved),"production wrapper plan captures authentic bias");
        list->SetPipelineState(pipeline.Get());version9->RSSetDepthBias(8.0f,0,0);
        Check(observer.Restore(list.Get(),incarnation,*saved,plan.setters)==(effect==BiasOuterProbe::Effect::Exact),
            "production context/plan accepts exact effect only, rejects changed missing duplicate extra");
        Check(outer->biasCalls==1,"production plan dispatches exactly one outer82 call");Ok(list->Close());
    }
    for(bool mismatched:{false,true}){
        begin();outer->allow9=mismatched;outer->foreign9=mismatched?foreign:nullptr;
        auto context=NativeRecordingReplayContext::Prepare(hooks,outerOwner.Get());
        Check(context->Valid()&&!context->version9,"absent or mismatched wrapper-v9 identity supplies no82 authority");
        auto plan=NativeRecordingReplayPlan::Make(hooks,context,observer.Observe(list.Get()).incarnation,RestoreMask::Pipeline,0,nullptr);
        Check(!plan.setters.depthBias&&!observer.CaptureRestorable(list.Get(),RestoreMask::Pipeline,plan.setters),"missing authentic wrapper82 refuses affected recipe before mutation");
        Ok(list->Close());
    }
    outer->allow9=true;outer->foreign9=nullptr;outer->effect=BiasOuterProbe::Effect::Exact;
    begin();auto context=NativeRecordingReplayContext::Prepare(hooks,outerOwner.Get());
    auto plan=NativeRecordingReplayPlan::Make(hooks,context,observer.Observe(list.Get()).incarnation,RestoreMask::Pipeline,0,nullptr);
    const auto before=outer->biasCalls;outer->table[82]=reinterpret_cast<void*>(&BiasOuterProbe::AlternateBias);
    Check(!context->Valid()&&!plan.setters.validate(),"production context detects changed pinned wrapper82 target");
    bool refused=false;try{plan.setters.depthBias(list.Get(),64.0f,.25f,.5f);}catch(const std::exception&){refused=true;}
    Check(refused&&outer->biasCalls==before,"changed wrapper route refused before dispatch");
    Check(NativeRecordingReplayPlan::Make(hooks,context,observer.Observe(list.Get()).incarnation,RestoreMask::Pipeline,0,nullptr).refusal!=nullptr,"changed context cannot prepare new replay plan");
    outer->table[82]=reinterpret_cast<void*>(&BiasOuterProbe::Bias);Ok(list->Close());
    // Actual successful covered null DIRECT Reset supplies provenance. RT object
    // and setter remain observer inputs: no native ray-tracing API is executed.
    ComPtr<IUnknown> rtOwner;rtOwner.Attach(new ObservedRtObject);
    auto* rt=reinterpret_cast<ID3D12StateObject*>(rtOwner.Get());
    Ok(list->Reset(allocator.Get(),nullptr));
    NativeRecordingHooksTestAccess::ObserveStateObjectOnly(hooks,list.Get(),rt);
    auto rtOriginals=hooks.Originals();rtOriginals.pipeline={};rtOriginals.depthBias={};unsigned rtCalls=0;
    rtOriginals.stateObject=[&](auto* target,auto* object){++rtCalls;NativeRecordingHooksTestAccess::ReplayObservedStateObject(hooks,target,object);};
    for(unsigned i=0;i<3;++i){
        auto saved=observer.CaptureRestorable(list.Get(),RestoreMask::Pipeline,rtOriginals);Check(bool(saved),"covered genuine null DIRECT Reset permits RT-only observer recipe");
        NativeRecordingHooksTestAccess::ObservePipelineOnly(hooks,list.Get(),pipeline.Get());
        NativeRecordingHooksTestAccess::ObserveDepthBiasOnly(hooks,list.Get());
        Check(observer.Restore(list.Get(),observer.Observe(list.Get()).incarnation,*saved,rtOriginals),"repeated RT-only provenance restoration has no fabricated numeric bias");
    }
    Check(rtCalls==3,"RT-only observer callbacks repeated exactly");Ok(list->Close());
    ComPtr<ID3D12GraphicsCommandList> missed;Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&missed)));
    NativeRecordingHooksTestAccess::ObserveStateObjectOnly(hooks,missed.Get(),rt);
    Check(!observer.CaptureRestorable(missed.Get(),RestoreMask::Pipeline,rtOriginals),"missed real Reset cannot borrow null-DIRECT RT provenance");Ok(missed->Close());
    auto** originalTable=*reinterpret_cast<void***>(list.Get());std::array<void*,86> discoveryTable{};
    std::copy_n(originalTable,NativeRecordingHooksTestAccess::SlotCount(hooks),discoveryTable.data());
    routeOriginalGetType=reinterpret_cast<RouteTypeFn>(originalTable[8]);discoveryTable[8]=reinterpret_cast<void*>(&RouteType);
    bool mutated=false;routeTypeCallback=[&]{if(!observer.CaptureRouteHistory(list.Get()).enrollment)return false;
        mutated=true;NativeRecordingHooksTestAccess::ObserveDepthBiasOnly(hooks,list.Get());return true;};
    *reinterpret_cast<void***>(list.Get())=discoveryTable.data();Ok(list->Reset(allocator.Get(),nullptr));
    *reinterpret_cast<void***>(list.Get())=originalTable;routeTypeCallback={};
    NativeRecordingHooksTestAccess::ObserveStateObjectOnly(hooks,list.Get(),rt);
    Check(mutated&&!observer.CaptureRestorable(list.Get(),RestoreMask::Pipeline,rtOriginals),"discovery-time setter mutation cannot certify null-DIRECT RT provenance");
    Ok(list->Close());Ok(list->Reset(allocator.Get(),nullptr));Ok(list->Close());
    std::cout<<"PASS installed public WARP82 original/reentry and production wrapper-v9 context/plan identity-route-effect boundaries; null-DIRECT Reset RT observer provenance repeated/missed/discovery mutation; actual nativeRTAPI=0 GPUsubmission=0\n";
    std::cout<<"KEEPALIVE fixtureList="<<std::hex<<reinterpret_cast<std::uintptr_t>(list.Get())
             <<" missedList="<<reinterpret_cast<std::uintptr_t>(missed.Get())<<std::dec<<'\n';
    return {list,missed};
}
