// Included after the existing ReferenceProbe/state-model test helpers.
// Symbolic PSO/state-object owners exercise the real journal, not GPU defaults.
#include <bit>
static void DepthBiasStateRegressions()
{
    ReferenceProbe objects[6];
    auto* list=objects[0].As<ID3D12GraphicsCommandList>();
    auto* graphics=objects[1].As<ID3D12PipelineState>();
    auto* compute=objects[2].As<ID3D12PipelineState>();
    auto* inserted=objects[3].As<ID3D12PipelineState>();
    auto* ray=objects[4].As<ID3D12StateObject>();
    std::vector<std::string> replayed;
    std::array<std::uint32_t,3> bits{};
    const auto originals=[&] {
        NativeRecordingState::OriginalSetters o;
        o.pipeline=[&](auto*,auto* value){replayed.push_back(value==graphics?"G":value==compute?"C":"I");};
        o.stateObject=[&](auto*,auto* value){assert(value==ray);replayed.push_back("S");};
        o.depthBias=[&](auto*,float a,float b,float c){
            bits={std::bit_cast<std::uint32_t>(a),std::bit_cast<std::uint32_t>(b),std::bit_cast<std::uint32_t>(c)};
            replayed.push_back("B");};
        o.heaps=[](auto*,UINT,ID3D12DescriptorHeap* const*){};
        return o;
    };
    struct Case {std::vector<char> tail;std::vector<std::string> expected;};
    const Case cases[]={
        {{},{"G","B"}},
        {{'G'},{"G"}},
        {{'C'},{"C"}},
        {{'S'},{"G","B","S"}},
        {{'S','G'},{"G"}},
        {{'C','S','G','C'},{"C"}}
    };
    for(const auto& example:cases){
        NativeRecordingState state;assert(state.Begin(list,1,graphics));
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        for(char binding:example.tail){
            if(binding=='S')assert(state.ObserveStateObject(1,ray));
            else assert(state.ObservePipeline(1,binding=='G'?graphics:compute));
        }
        auto o=originals();
        const bool hasBias=std::find(example.expected.begin(),example.expected.end(),"B")!=example.expected.end();
        if(!hasBias)o.depthBias={};
        if(std::find(example.expected.begin(),example.expected.end(),"S")==example.expected.end())o.stateObject={};
        auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        assert(state.ObservePipeline(1,inserted));replayed.clear();bits={0xdeadbeef,0xdeadbeef,0xdeadbeef};
        assert(state.Restore(*saved,o));assert(replayed==example.expected);
        if(hasBias)assert((bits==std::array<std::uint32_t,3>{0x3fc00000,0x3f400000,0xc0100000}));
        else assert((bits==std::array<std::uint32_t,3>{0xdeadbeef,0xdeadbeef,0xdeadbeef}));
        // Committed knowledge remains authentic for a second independent restore.
        auto again=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(again);
        assert(state.ObservePipeline(1,inserted));replayed.clear();
        assert(state.Restore(*again,o));assert(replayed==example.expected);
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDepthBias(1,1.0f,2.0f,3.0f));assert(state.ObservePipeline(1,compute));
        assert(state.ObservePipeline(1,graphics));
        // A genuine ordinary rebind supersedes old bias/tail; the later setter adds exact new values.
        assert(state.ObserveDepthBias(1,-1.5f,0.0f,2.25f));
        auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*saved,o));
        assert((replayed==std::vector<std::string>{"G","B"}));
        assert((bits==std::array<std::uint32_t,3>{0xbfc00000,0,0x40100000}));
        // CPU bits only: these values are never passed to a native GPU setter.
        assert(state.ObserveDepthBias(1,std::bit_cast<float>(0x80000000u),std::bit_cast<float>(0x7fc12345u),1.5f));
        saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*saved,o));
        assert((bits==std::array<std::uint32_t,3>{0x80000000,0x7fc12345,0x3fc00000}));
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));assert(state.ObserveStateObject(1,ray));
        auto noBias=o;noBias.depthBias={};
        assert(!state.CaptureRestorable(list,1,RestoreMask::Pipeline,noBias));
        auto noAnchor=o;noAnchor.pipeline={};
        assert(!state.CaptureRestorable(list,1,RestoreMask::Pipeline,noAnchor));
        assert(state.ObservePipeline(1,graphics));
        auto noTail=o;noTail.stateObject={};
        noTail.depthBias={};
        auto superseded=state.CaptureRestorable(list,1,RestoreMask::Pipeline,noTail);assert(superseded);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*superseded,noTail));
        assert((replayed==std::vector<std::string>{"G"}));
        // Old recordings need no version9 callback when no setter was observed.
        assert(state.Begin(list,2,graphics));
        auto old=state.CaptureRestorable(list,2,RestoreMask::Pipeline,noBias);assert(old);
        replayed.clear();assert(state.Restore(*old,noBias));assert((replayed==std::vector<std::string>{"G"}));
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,nullptr));auto o=originals();
        assert(state.ObserveStateObject(1,ray));
        replayed.clear();assert(!state.CaptureRestorable(list,1,RestoreMask::Pipeline,o));assert(replayed.empty());
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        assert(!state.CaptureRestorable(list,1,RestoreMask::Pipeline,o));
        assert(state.ObservePipeline(1,graphics));o.depthBias={};
        auto recovered=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(recovered);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*recovered,o));
        assert((replayed==std::vector<std::string>{"G"}));
        assert(state.Begin(list,2,graphics));
        auto fresh=state.CaptureRestorable(list,2,RestoreMask::Pipeline,o);assert(fresh);
        assert(state.ObservePipeline(2,inserted));replayed.clear();assert(state.Restore(*fresh,o));
        assert((replayed==std::vector<std::string>{"G"}));
    }
    {
        // Declared genuine DIRECT Reset provenance in this CPU state-model case.
        // Numeric GPU/dummy defaults are never assumed or passed to a real setter.
        NativeRecordingState state;assert(state.Begin(list,1,nullptr,true));
        assert(state.ObserveStateObject(1,ray));auto o=originals();o.pipeline={};o.depthBias={};
        for(unsigned repeat=0;repeat<3;++repeat){
            auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
            assert(state.ObservePipeline(1,inserted));assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
            replayed.clear();bits={0xdeadbeef,0xdeadbeef,0xdeadbeef};
            assert(state.Restore(*saved,o));assert((replayed==std::vector<std::string>{"S"}));
            assert((bits==std::array<std::uint32_t,3>{0xdeadbeef,0xdeadbeef,0xdeadbeef}));
        }
        auto missing=o;missing.stateObject={};
        assert(!state.CaptureRestorable(list,1,RestoreMask::Pipeline,missing));
        assert(state.ObservePipeline(1,graphics));o=originals();o.depthBias={};
        auto graphicsState=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(graphicsState);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*graphicsState,o));
        assert((replayed==std::vector<std::string>{"G"}));
        assert(state.Begin(list,2,nullptr,true));assert(state.ObserveStateObject(2,ray));
        state.Taint(2,"UnsupportedEffect",27);
        assert(!state.CaptureRestorable(list,2,RestoreMask::Pipeline,o));
        assert(state.Begin(list,3,nullptr,true));assert(state.ObserveStateObject(3,ray));
        assert(state.CaptureRestorable(list,3,RestoreMask::Pipeline,o));
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        for(unsigned i=0;i<300;++i)assert(state.ObserveStateObject(1,ray));
        auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*saved,o));
        assert(replayed.size()==302&&replayed[0]=="G"&&replayed[1]=="B");
        assert(std::all_of(replayed.begin()+2,replayed.end(),[](const auto& item){return item=="S";}));
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDescriptorHeaps(1,0,nullptr));assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        auto partial=state.CaptureRestorable(list,1,RestoreMask::Heaps,o);assert(partial);
        // A same-pointer ordinary bind outside the heap mask supersedes the old bias.
        assert(state.ObservePipeline(1,graphics));replayed.clear();
        assert(state.Restore(*partial,o)&&replayed.empty());o.depthBias={};
        auto current=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(current);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*current,o));
        assert((replayed==std::vector<std::string>{"G"}));
    }
    for(bool setBack:{false,true}){
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDescriptorHeaps(1,0,nullptr));assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        auto partial=state.CaptureRestorable(list,1,RestoreMask::Heaps,o);assert(partial);
        assert(state.ObserveDepthBias(1,4.0f,5.0f,6.0f));
        if(setBack)assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        replayed.clear();assert(state.Restore(*partial,o));assert(replayed.empty());
        auto current=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(current);
        assert(state.ObservePipeline(1,inserted));replayed.clear();assert(state.Restore(*current,o));
        assert(!replayed.empty()&&replayed.front()=="G"&&replayed.back()=="B");
        if(setBack)assert((bits==std::array<std::uint32_t,3>{0x3fc00000,0x3f400000,0xc0100000}));
        else assert((bits==std::array<std::uint32_t,3>{0x40800000,0x40a00000,0x40c00000}));
    }
    for(bool setBack:{false,true}){
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDescriptorHeaps(1,0,nullptr));assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        auto saved=state.CaptureRestorable(list,1,RestoreMask::Heaps,o);assert(saved);
        o.heaps=[&](auto*,UINT,ID3D12DescriptorHeap* const*){
            assert(state.ObserveDepthBias(1,4.0f,5.0f,6.0f));
            if(setBack)assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));
        };
        replayed.clear();assert(!state.Restore(*saved,o));assert(replayed.empty());
    }
    for(bool resetAtBias:{false,true}){
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));assert(state.ObserveStateObject(1,ray));
        auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        if(resetAtBias)o.depthBias=[&](auto*,float,float,float){replayed.push_back("B");assert(state.Begin(list,2,inserted));};
        else o.pipeline=[&](auto*,auto*){replayed.push_back("G");assert(state.Begin(list,2,inserted));};
        replayed.clear();assert(!state.Restore(*saved,o));
        assert(replayed==(resetAtBias?std::vector<std::string>{"G","B"}:std::vector<std::string>{"G"}));
    }
    {
        NativeRecordingState state;assert(state.Begin(list,1,graphics));auto o=originals();
        assert(state.ObserveDepthBias(1,1.5f,0.75f,-2.25f));assert(state.ObservePipeline(1,compute));
        assert(state.ObserveStateObject(1,ray));auto saved=state.CaptureRestorable(list,1,RestoreMask::Pipeline,o);assert(saved);
        assert(state.Begin(list,2,inserted));
        assert(objects[1].references>1&&objects[2].references>1&&objects[4].references>1);
        replayed.clear();assert(!state.Restore(*saved,o));assert(replayed.empty());
        saved.reset();assert(objects[1].references==1&&objects[2].references==1&&objects[4].references==1);
    }
    std::cout<<"PASS depth-bias ordered anchor/tail, raw bits, pipeline-only closure, partial-mask preservation and in-replay mutation refusal, missing routes, reset leases and ownership\n";
}
