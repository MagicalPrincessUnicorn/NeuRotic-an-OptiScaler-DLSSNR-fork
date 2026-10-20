// CPU regressions against the production journal and hook forwarding path.
// No D3D device, model, provider, command submission or GPU is constructed.
#define NATIVE_RECORDING_HOOK_TEST_ACCESS
#include <nr/d3d12/NativeRecordingHooks.h>
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
using namespace Neurotic::D3D12;

struct ReferenceProbe final : IUnknown
{
    ULONG references = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { return --references; }
    template<class T> T* As() { return reinterpret_cast<T*>(this); }
};
static void Require(bool value, const char* what)
{ if (!value) throw std::runtime_error(what); }
static unsigned checks = 0, failures = 0;
template<class F> void Case(const char* name, F body)
{
    ++checks;
    try { body(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
namespace Neurotic::D3D12
{
struct NativeRecordingHooksTestAccess
{
    template<unsigned Slot, class Signature, class F, class... A>
    static void Call(NativeRecordingHooks& hooks, F original, ID3D12GraphicsCommandList* list, A... args)
    {
        using H = NativeRecordingHooks::Hook<Slot, Signature>;
        auto* prior = NativeRecordingHooks::instance_;
        const auto priorOriginal = H::original;
        NativeRecordingHooks::instance_ = &hooks;
        H::original = original;
        try { H::Call(reinterpret_cast<ID3D12GraphicsCommandList10*>(list), args...); }
        catch (...) { H::original = priorOriginal; NativeRecordingHooks::instance_ = prior; throw; }
        H::original = priorOriginal; NativeRecordingHooks::instance_ = prior;
    }
    static void Protected(NativeRecordingHooks& h, ID3D12GraphicsCommandList* l)
    { h.Before<67>(l, static_cast<ID3D12ProtectedResourceSession*>(nullptr)); }
    static void RenderPass(NativeRecordingHooks& h, ID3D12GraphicsCommandList* l)
    { h.Before<68>(l, UINT{0}, static_cast<const D3D12_RENDER_PASS_RENDER_TARGET_DESC*>(nullptr),
          static_cast<const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*>(nullptr), D3D12_RENDER_PASS_FLAG_NONE); }
    template<class P,class F, class Action>
    static void WithSyntheticEnrollment(NativeRecordingHooks& h, unsigned count,P pipeline,F setter, Action action)
    {
        // CPU adapter test only: installation and native interface qualification
        // are declared inputs. Actual Originals/Replay and observer guards run.
        using H=NativeRecordingHooks::Hook<75,decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>;
        using Base=NativeRecordingHooks::Hook<25,decltype(&ID3D12GraphicsCommandList10::SetPipelineState)>;
        const auto priorBase=Base::original;const auto prior=H::original;
        Base::original=pipeline;H::original=setter;h.count_=count;h.installed_=true;
        h.coverage_.store(std::make_shared<NativeRecordingHooks::Coverage>());
        try { action(); }
        catch(...) {h.coverage_.store({});h.installed_=false;H::original=prior;Base::original=priorBase;throw;}
        h.coverage_.store({});h.installed_=false;H::original=prior;Base::original=priorBase;
    }
    template<class F,class Action>
    static void WithSyntheticBiasEnrollment(NativeRecordingHooks& h,unsigned count,F setter,Action action){
        using H=NativeRecordingHooks::Hook<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>;
        const auto prior=H::original;H::original=setter;h.count_=count;h.installed_=true;
        h.coverage_.store(std::make_shared<NativeRecordingHooks::Coverage>());
        try{action();}catch(...){h.coverage_.store({});h.installed_=false;H::original=prior;throw;}
        h.coverage_.store({});h.installed_=false;H::original=prior;
    }
};
}
template<class O, class F> bool StateObjectSetter(O& o, F f)
{
    if constexpr (requires { o.stateObject = f; }) { o.stateObject = f; return true; }
    return false;
}
template<class D> bool ExactBlend(const D& d, const std::array<std::uint32_t,4>& bits)
{
    if constexpr (requires { d.blendKnown; d.blendBits; }) return d.blendKnown && d.blendBits == bits;
    return false;
}
// Preserve assertion failures across the observer's intentional exception boundary.
template<class F> void Current(NativeRecordingObserver& observer, ID3D12GraphicsCommandList* list, F action)
{
    std::exception_ptr failure;
    const bool accepted=observer.WithCurrent(list,[&](auto& state,auto inc) {
        try { return action(state,inc); }
        catch (...) { failure=std::current_exception(); return false; }
    });
    if (failure) std::rethrow_exception(failure);
    Require(accepted,"journal action accepted");
}
struct Fixture
{
    ReferenceProbe listOwner, psoOwner, rtOwner, otherOwner;
    ID3D12GraphicsCommandList* list = listOwner.As<ID3D12GraphicsCommandList>();
    ID3D12PipelineState* pso = psoOwner.As<ID3D12PipelineState>();
    ID3D12StateObject* rt = rtOwner.As<ID3D12StateObject>();
    NativeRecordingState state;
    Fixture() { Require(state.Begin(list, 1, pso), "begin"); }
};
static unsigned writes, pipelineCalls, stateObjectCalls;
static ID3D12GraphicsCommandList4* lastStateList;
static ID3D12StateObject* lastStateObject;
static std::vector<std::string> calls;
static const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* expectedParams;
static const D3D12_WRITEBUFFERIMMEDIATE_MODE* expectedModes;
static void STDMETHODCALLTYPE Write(ID3D12GraphicsCommandList2*, UINT n,
    const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* p, const D3D12_WRITEBUFFERIMMEDIATE_MODE* m)
{ ++writes; calls.emplace_back("write"); Require(n == 1 && p == expectedParams && m == expectedModes, "original write arguments"); }
static void STDMETHODCALLTYPE Pipeline(ID3D12GraphicsCommandList*, ID3D12PipelineState*)
{ ++pipelineCalls; calls.emplace_back("pipeline"); }
static void STDMETHODCALLTYPE StateObject(ID3D12GraphicsCommandList4* list, ID3D12StateObject* object)
{ ++stateObjectCalls;lastStateList=list;lastStateObject=object;calls.emplace_back("state-object"); }

static unsigned depthBiasCalls;
static ID3D12GraphicsCommandList9* lastDepthBiasList;
static std::array<std::uint32_t,3> lastDepthBiasBits;
static std::function<void()> depthBiasReentry;
static void STDMETHODCALLTYPE DepthBiasOriginal(ID3D12GraphicsCommandList9* list,float a,float b,float c){
    ++depthBiasCalls;lastDepthBiasList=list;
    lastDepthBiasBits={std::bit_cast<std::uint32_t>(a),std::bit_cast<std::uint32_t>(b),std::bit_cast<std::uint32_t>(c)};
    if(depthBiasReentry){auto action=std::move(depthBiasReentry);depthBiasReentry={};action();}
}
int main()
{
    Case("direct Originals replay rejects changed reentrant depth-bias bits and forwards the call",[]{
        // Declared CPU enrollment, but the production Originals/Replay/Hook::Call
        // path runs. No observer-direct mutation substitutes for the nested hook.
        for(int changed=-1;changed<3;++changed){
            Fixture f;NativeRecordingObserver observer;auto* hooks=new NativeRecordingHooks(observer);
            observer.SetHookCoverage(true);Require(observer.Reset(f.list,f.pso),"covered CPU Reset");
            const std::array<std::uint32_t,3> bits{0x80000000u,0x7fc12345u,0x3fc00000u};
            const auto a=std::bit_cast<float>(bits[0]),b=std::bit_cast<float>(bits[1]),c=std::bit_cast<float>(bits[2]);
            Current(observer,f.list,[&](auto& state,auto inc){return state.ObserveDepthBias(inc,a,b,c);});
            NativeRecordingHooksTestAccess::WithSyntheticBiasEnrollment(*hooks,84,DepthBiasOriginal,[&]{
                auto originals=hooks->Originals();originals.pipeline=[](auto*,auto*){};
                const auto saved=observer.CaptureRestorable(f.list,RestoreMask::Pipeline,originals);
                Require(bool(saved),"saved actual bias recipe");
                const auto incarnation=observer.Observe(f.list).incarnation;
                Current(observer,f.list,[&](auto& state,auto inc){return state.ObservePipeline(inc,f.pso);});
                auto nested=bits;if(changed>=0)nested[changed]^=1u;
                depthBiasCalls=0;
                depthBiasReentry=[&]{
                    NativeRecordingHooksTestAccess::Call<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>(
                        *hooks,DepthBiasOriginal,f.list,std::bit_cast<float>(nested[0]),
                        std::bit_cast<float>(nested[1]),std::bit_cast<float>(nested[2]));
                };
                const bool restored=observer.Restore(f.list,incarnation,*saved,originals);
                Require(depthBiasCalls==2&&lastDepthBiasBits==nested,"both genuine original calls preserve exact nested arguments");
                Require(restored==(changed<0),"only exact raw-bit reentry may commit saved depth bias");
                Require(observer.Observe(f.list).completeCoverage==(changed<0),"changed reentry invalidates recording authority");
            });
        }
    });
    Case("depth-bias observer forwards exact bits once and restores its authentic recipe",[]{
        Fixture f;NativeRecordingObserver observer;auto* hooks=new NativeRecordingHooks(observer);
        Require(observer.Reset(f.list,f.pso),"reset");depthBiasCalls=0;
        const float a=std::bit_cast<float>(0x80000000u),b=std::bit_cast<float>(0x7fc12345u),c=1.5f;
        NativeRecordingHooksTestAccess::Call<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>(*hooks,DepthBiasOriginal,f.list,a,b,c);
        Require(depthBiasCalls==1&&lastDepthBiasList==reinterpret_cast<ID3D12GraphicsCommandList9*>(f.list)&&
                lastDepthBiasBits==std::array<std::uint32_t,3>{0x80000000,0x7fc12345,0x3fc00000},"exact forwarding bits");
        Current(observer,f.list,[&](auto& state,auto inc){
            std::vector<std::string> replay;NativeRecordingState::OriginalSetters o;
            o.pipeline=[&](auto* l,auto* p){Require(l==f.list&&p==f.pso,"authentic PSO");replay.push_back("P");};
            o.depthBias=[&](auto*,float x,float y,float z){
                Require((std::array<std::uint32_t,3>{std::bit_cast<std::uint32_t>(x),std::bit_cast<std::uint32_t>(y),std::bit_cast<std::uint32_t>(z)}==lastDepthBiasBits),"exact replay bits");replay.push_back("B");};
            auto saved=state.CaptureRestorable(f.list,inc,RestoreMask::Pipeline,o);Require(bool(saved),"82 does not unconditionally taint");
            Require(state.ObservePipeline(inc,f.pso),"temporary same PSO bind");
            Require(state.Restore(*saved,o)&&replay==std::vector<std::string>{"P","B"},"restore PSO then explicit bias");return true;
        });
    });
    Case("depth-bias original is available only at the actual installed slot boundary",[]{
        Fixture f;NativeRecordingObserver observer;auto* hooks=new NativeRecordingHooks(observer);
        NativeRecordingHooksTestAccess::WithSyntheticBiasEnrollment(*hooks,82,DepthBiasOriginal,[&]{
            Require(!hooks->Originals().depthBias,"older interface has no82 original");});
        NativeRecordingHooksTestAccess::WithSyntheticBiasEnrollment(*hooks,84,DepthBiasOriginal,[&]{
            auto o=hooks->Originals();Require(bool(o.depthBias),"declared v9 original available");depthBiasCalls=0;
            o.depthBias(f.list,1.5f,0.75f,-2.25f);
            Require(depthBiasCalls==1&&lastDepthBiasBits==std::array<std::uint32_t,3>{0x3fc00000,0x3f400000,0xc0100000},"actual original callback and bits");});
        using Fn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList9*,float,float,float);
        NativeRecordingHooksTestAccess::WithSyntheticBiasEnrollment(*hooks,84,static_cast<Fn>(nullptr),[&]{
            Require(!hooks->Originals().depthBias,"missing actual82 original unavailable");});
    });
    Case("depth-bias outer witness accepts once and rejects changed duplicate missing or extra effects",[]{
        ReferenceProbe object;auto* list=object.As<ID3D12GraphicsCommandList>();
        bool matched=false;
        NativeReplayEffects::Invoke<82>(list,[&]{matched=NativeReplayEffects::Observe<82>(list,1.5f,0.75f,-2.25f);},1.5f,0.75f,-2.25f);
        Require(matched,"exact actual effect accepted");
        bool rejected=false;matched=true;
        try{NativeReplayEffects::Invoke<82>(list,[&]{matched=NativeReplayEffects::Observe<82>(list,std::bit_cast<float>(0x3fc00001u),0.75f,-2.25f);},1.5f,0.75f,-2.25f);}catch(...){rejected=true;}
        Require(rejected&&!matched,"changed raw value rejected");
        rejected=false;bool first=false,second=true;
        try{NativeReplayEffects::Invoke<82>(list,[&]{first=NativeReplayEffects::Observe<82>(list,1.5f,0.75f,-2.25f);second=NativeReplayEffects::Observe<82>(list,1.5f,0.75f,-2.25f);},1.5f,0.75f,-2.25f);}catch(...){rejected=true;}
        Require(rejected&&first&&!second,"duplicate effect rejected");
        rejected=false;try{NativeReplayEffects::Invoke<82>(list,[]{},1.5f,0.75f,-2.25f);}catch(...){rejected=true;}
        Require(rejected,"swallowed effect rejected");
        rejected=false;matched=true;
        try{NativeReplayEffects::Invoke<82>(list,[&]{matched=NativeReplayEffects::Observe<25>(list,static_cast<ID3D12PipelineState*>(nullptr));},1.5f,0.75f,-2.25f);}catch(...){rejected=true;}
        Require(rejected&&!matched,"extra different setter rejected");
    });

    Case("WriteBufferImmediate preserves binding knowledge and forwards once", [] {
        ReferenceProbe listOwner, psoOwner;
        auto* list = listOwner.As<ID3D12GraphicsCommandList>();
        auto* pso = psoOwner.As<ID3D12PipelineState>();
        NativeRecordingObserver observer;
        auto* hooks = new NativeRecordingHooks(observer); // process-retained callback owner; no hooks installed
        Require(observer.Reset(list,pso), "reset");
        std::optional<NativeRecordingState::Snapshot> saved;
        NativeRecordingState::OriginalSetters originals;
        unsigned replayed = 0;
        originals.pipeline = [&](auto* l, auto* p) { Require(l == list && p == pso, "authentic pipeline"); ++replayed; };
        Current(observer,list,[&](auto& state, auto inc) { saved=state.CaptureRestorable(list,inc,RestoreMask::Pipeline,originals); return true; });
        Require(bool(saved), "saved before write");
        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER p{0x1000, 0x12345678};
        D3D12_WRITEBUFFERIMMEDIATE_MODE m=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
        expectedParams=&p; expectedModes=&m; writes=0; calls.clear();
        NativeRecordingHooksTestAccess::Call<66,decltype(&ID3D12GraphicsCommandList10::WriteBufferImmediate)>(*hooks,Write,list,UINT{1},&p,&m);
        Require(writes == 1 && calls == std::vector<std::string>{"write"}, "write forwarded exactly once");
        Current(observer,list,[&](auto& state, auto) {
            NativeRecordingState::RestoreDiagnostic d;
            Require(state.Restore(*saved,originals,&d), "buffer write must not taint caller bindings");
            Require(d.currentWorkOrdinal == d.savedWorkOrdinal + 1, "write counted as ordered work"); return true;
        });
        Require(replayed==1 && writes==1, "restore bindings once without repeating data write");
    });
    Case("state-object pipeline is captured and restored through matching setter", [] {
        Fixture f; NativeRecordingObserver observer; auto* hooks = new NativeRecordingHooks(observer);
        Require(observer.Reset(f.list,f.pso), "reset"); stateObjectCalls=0;
        NativeRecordingHooksTestAccess::Call<75,decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>(*hooks,StateObject,f.list,f.rt);
        Require(stateObjectCalls==1, "state-object forwarded once");
        Current(observer,f.list,[&](auto& state, auto inc) {
            NativeRecordingState::OriginalSetters o; unsigned rtReplay=0, ordinaryReplay=0;
            o.pipeline=[&](auto*,auto*) { ++ordinaryReplay; };
            StateObjectSetter(o,[&](auto* l,auto* rt) { Require(l==f.list && rt==f.rt,"exact current state object"); ++rtReplay; });
            auto saved=state.CaptureRestorable(f.list,inc,RestoreMask::Pipeline,o);
            Require(bool(saved), "state-object setter must not taint recording");
            Require(state.ObservePipeline(inc,f.pso), "temporary compute PSO");
            Require(state.Restore(*saved,o), "restore state-object pipeline");
            Require(rtReplay==1 && ordinaryReplay==1,"authentic ordinary baseline then matching state-object setter"); return true;
        });
    });
    Case("ordinary setter supersedes earlier state object without reviving fatal taint", [] {
        Fixture f; NativeRecordingObserver observer; auto* hooks = new NativeRecordingHooks(observer);
        Require(observer.Reset(f.list,f.pso), "reset");
        NativeRecordingHooksTestAccess::Call<75,decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>(*hooks,StateObject,f.list,f.rt);
        NativeRecordingHooksTestAccess::Call<25,decltype(&ID3D12GraphicsCommandList10::SetPipelineState)>(*hooks,Pipeline,f.list,f.pso);
        Current(observer,f.list,[&](auto& state,auto inc) {
            NativeRecordingState::OriginalSetters o; unsigned ordinaryReplay=0;
            o.pipeline=[&](auto*,auto* p){Require(p==f.pso,"current ordinary PSO");++ordinaryReplay;};
            auto saved=state.CaptureRestorable(f.list,inc,RestoreMask::Pipeline,o);
            Require(saved && state.Restore(*saved,o) && ordinaryReplay==1,"superseded state object does not remain a fatal fault");
            state.Taint(inc,"IndependentResourceFault",80); Require(state.ObservePipeline(inc,f.pso),"later observed setter");
            Require(!state.Capture(f.list,inc,RestoreMask::Pipeline),"setter cannot heal independent resource fault"); return true;
        });
    });
    Case("protected sessions and render-pass scope remain refused", [] {
        Fixture f; NativeRecordingObserver observer; auto* hooks = new NativeRecordingHooks(observer);
        Require(observer.Reset(f.list,f.pso),"reset"); NativeRecordingHooksTestAccess::Protected(*hooks,f.list);
        Current(observer,f.list,[&](auto& s,auto inc){Require(!s.Capture(f.list,inc,RestoreMask::Pipeline),"protected scope remains guarded");return true;});
        Require(observer.Reset(f.list,f.pso),"fresh reset"); NativeRecordingHooksTestAccess::RenderPass(*hooks,f.list);
        Current(observer,f.list,[&](auto& s,auto inc){Require(!s.Capture(f.list,inc,RestoreMask::Pipeline),"render pass remains guarded");return true;});
    });
    Case("unobserved blend has its own missing-state reason", [] {
        Fixture f; NativeStateCaptureDiagnostic d;
        Require(!f.state.Capture(f.list,1,RestoreMask::BlendFactor,&d),"unknown refuses");
        Require(std::string(d.reason)=="Native.State.BlendFactorUnknown","unknown is distinct from an observed numeric value");
    });
    const std::array<std::uint32_t,4> tuples[]={
        {0x80000000u,0xbf800000u,0x40000000u,0x3f000000u}, // -0, -1, 2, .5
        {0x7fc12345u,0x7f800000u,0xff800000u,0x00000000u} // payload NaN, infinities, +0; CPU bit transport only
    };
    for (const auto& bits:tuples) Case("observed blend preserves exact setter bits without numeric allowlist", [&] {
        Fixture f; float values[4]; std::memcpy(values,bits.data(),sizeof(values));
        Require(f.state.ObserveBlendFactor(1,values),"observe actual argument bytes");
        NativeStateCaptureDiagnostic d; auto saved=f.state.Capture(f.list,1,RestoreMask::BlendFactor,&d);
        Require(bool(saved),"observed public setter state is not rejected by numeric range");
        Require(ExactBlend(d,bits),"diagnostics retain exact known tuple bits");
        const float temporary[4]={.25f,.5f,.75f,1.f}; Require(f.state.ObserveBlendFactor(1,temporary),"temporary NR state");
        NativeRecordingState::OriginalSetters o; unsigned replay=0;
        o.blendFactor=[&](auto* l,const float* p){Require(l==f.list && p && std::memcmp(p,bits.data(),sizeof(values))==0,"replay bit-identical caller tuple");++replay;};
        Require(f.state.Restore(*saved,o) && replay==1,"exact one replay");
    });
    Case("omitted blend mutation still refuses before any replay", [] {
        Fixture f; auto saved=f.state.Capture(f.list,1,RestoreMask::Pipeline); Require(bool(saved),"pipeline saved");
        const float changed[4]={.2f,.3f,.4f,.5f}; Require(f.state.ObserveBlendFactor(1,changed),"changed blend");
        unsigned replay=0; NativeRecordingState::OriginalSetters o; o.pipeline=[&](auto*,auto*){++replay;};
        NativeRecordingState::RestoreDiagnostic d;
        Require(!f.state.Restore(*saved,o,&d) && replay==0 && std::string(d.reason)=="Native.Restore.BlendFactorBoundary","omitted-domain guard remains authentic");
    });
    Case("state-object snapshot retains identity after external and journal release", [] {
        ReferenceProbe listOwner,psoOwner,rtOwner;
        auto* list=listOwner.As<ID3D12GraphicsCommandList>();
        auto state=std::make_unique<NativeRecordingState>();
        Require(state->Begin(list,1,psoOwner.As<ID3D12PipelineState>()),"begin");
        Require(state->ObserveStateObject(1,rtOwner.As<ID3D12StateObject>()),"observed state object");
        auto saved=state->Capture(list,1,RestoreMask::Pipeline); Require(bool(saved),"snapshot");
        rtOwner.Release(); state.reset();
        Require(rtOwner.references==1,"snapshot owns last state-object reference");
        saved.reset(); Require(rtOwner.references==0,"snapshot retires retained state object exactly once");
    });
    Case("missing state-object original setter refuses before mutation", [] {
        Fixture f; Require(f.state.ObserveStateObject(1,f.rt),"observe state object");
        NativeRecordingState::OriginalSetters o; unsigned replay=0;
        o.pipeline=[&](auto*,auto*){++replay;};
        Require(!f.state.CaptureRestorable(f.list,1,RestoreMask::Pipeline,o) && replay==0,
            "ordinary setter does not stand in for missing versioned setter");
    });
    Case("blend diagnostics preserve scope and each event's exact payload", [] {
        Fixture f;
        const std::array<std::uint32_t,4> a{0x80000000u,0xbf800000u,0x40000000u,0x3f000000u};
        const std::array<std::uint32_t,4> b{0x3e800000u,0x3f000000u,0x3f400000u,0x3f800000u};
        float av[4],bv[4]; std::memcpy(av,a.data(),sizeof(av));std::memcpy(bv,b.data(),sizeof(bv));
        {
            NativeRecordingPhaseScope upstream(NativeRecordingPhase::UpstreamEvaluation);
            Require(f.state.ObserveBlendFactor(1,av),"upstream tuple");
            { NativeRecordingPhaseScope nr(NativeRecordingPhase::NeuralRendering);
              Require(f.state.ObserveBlendFactor(1,bv),"NR tuple"); }
            Require(NativeRecordingPhaseScope::Current()==NativeRecordingPhase::UpstreamEvaluation,"nested phase restores");
        }
        Require(NativeRecordingPhaseScope::Current()==NativeRecordingPhase::Caller,"scope ends without changing eligibility");
        NativeStateCaptureDiagnostic d; f.state.Diagnose(&d);
        Require(d.blendPhase==NativeRecordingPhase::NeuralRendering && d.blendMutation==2 && ExactBlend(d,b),"last observation correlation");
        std::vector<NativeStateCaptureDiagnostic::TraceEntry> blend;
        for(UINT i=0;i<d.traceCount;++i)if(std::strcmp(d.trace[i].operation,"blend-factor")==0)blend.push_back(d.trace[i]);
        Require(blend.size()==2 && blend[0].blendBits==a && blend[1].blendBits==b &&
            blend[0].sequence<blend[1].sequence && d.blendObservedSequence==blend[1].sequence,"exact payload survives later setter");
    });
    Case("later unsupported effects are diagnosed without clearing first fault", [] {
        Fixture f; f.state.Taint(1,"UnsupportedEffect",67);f.state.Taint(1,"UnsupportedEffect",68);
        Require(f.state.ObservePipeline(1,f.pso),"later authentic setter");
        NativeStateCaptureDiagnostic d;f.state.Diagnose(&d);
        Require(d.firstInvalidation.slot==67 && (d.unsupportedEffectBits[1] & 0x18u)==0x18u,
            "both scoped effects retained with original first cause");
        Require(!f.state.Capture(f.list,1,RestoreMask::Pipeline),"diagnostic catalogue does not grant recovery");
    });
    Case("partial unobserved root arguments remain a specific capture limit", [] {
        ReferenceProbe rootOwner;Fixture f;auto* root=rootOwner.As<ID3D12RootSignature>();
        Require(f.state.RegisterLayout(root,{{RootKind::Constants,2}}),"two constants");
        Require(f.state.ObserveComputeRootSignature(1,root),"observed root");
        const UINT value=7;Require(f.state.ObserveComputeConstants(1,0,1,&value,0),"one observed constant");
        Require(!f.state.Capture(f.list,1,RestoreMask::Compute),"no invented second constant");
    });
    Case("Originals state-object callback restores through covered observer", [] {
        Fixture f;NativeRecordingObserver observer;auto* hooks=new NativeRecordingHooks(observer);
        observer.SetHookCoverage(true);Require(observer.Reset(f.list,f.pso),"observed Reset after declared coverage");
        Current(observer,f.list,[&](auto& state,auto inc){return state.ObserveStateObject(inc,f.rt);});
        NativeRecordingHooksTestAccess::WithSyntheticEnrollment(*hooks,76,Pipeline,StateObject,[&] {
            const auto originals=hooks->Originals();
            Require(bool(originals.stateObject),"actual versioned callback supplied");
            const auto saved=observer.CaptureRestorable(f.list,RestoreMask::Pipeline,originals);
            Require(bool(saved),"covered observer captures state object through actual Originals");
            const auto incarnation=observer.Observe(f.list).incarnation;
            Current(observer,f.list,[&](auto& state,auto inc){return state.ObservePipeline(inc,f.pso);});
            stateObjectCalls=0;calls.clear();
            Require(observer.Restore(f.list,incarnation,*saved,originals),"observer invokes actual versioned replay callback");
            Require(stateObjectCalls==1 && calls==std::vector<std::string>{"pipeline","state-object"} &&
                lastStateList==reinterpret_cast<ID3D12GraphicsCommandList4*>(f.list) && lastStateObject==f.rt,
                "exactly one matching callback with authentic arguments");
            observer.SetHookCoverage(false);stateObjectCalls=0;
            Require(!observer.Restore(f.list,incarnation,*saved,originals) && stateObjectCalls==0,"coverage loss refuses before callback");
        });
        NativeRecordingHooksTestAccess::WithSyntheticEnrollment(*hooks,75,Pipeline,StateObject,[&] {
            Require(!hooks->Originals().stateObject,"older interface count supplies no versioned callback");
        });
        NativeRecordingHooksTestAccess::WithSyntheticEnrollment(*hooks,76,Pipeline,
            static_cast<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,ID3D12StateObject*)>(nullptr),[&] {
            Require(!hooks->Originals().stateObject,"absent real original supplies no versioned callback");
        });
    });
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
