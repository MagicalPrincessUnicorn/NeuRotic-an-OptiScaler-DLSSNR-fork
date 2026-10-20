// Compiles the real hook bodies extracted by Run-StreamlineObserver.ps1.
// Only application services and the external provider boundary are fakes.
#include "../OptiScaler/mfg/MfgOptionsSnapshot.h"
#include "../OptiScaler/mfg/MfgControl.h"
#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include "../OptiScaler/mfg/MfgRequestTrace.h"
#include "../OptiScaler/mfg/ExperimentalMfgRuntime.h"
#include <atomic>
#include <vector>
#include <d3d12.h>
#include <compare>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <sl_core_api.h>
#define LOG_TRACE(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#include <format>
#include <string>
static std::vector<std::string> receiptLines;
template<class... T> static void CaptureReceiptInfo(const char* format, const T&... values) {
    if(std::strncmp(format,"Native Ada MFG receipt:",sizeof("Native Ada MFG receipt:")-1)==0)
        receiptLines.push_back(std::vformat(format,std::make_format_args(values...)));
}
#define LOG_INFO(...) CaptureReceiptInfo(__VA_ARGS__)
#define LOG_FUNC(...) ((void)0)
#define NrSlCall(a,b,c,call,...) call()
#define NV_GPU_ARCHITECTURE_AD100 1
#define NV_GPU_ARCHITECTURE_GB200 2
enum class API { Vulkan, DX12 };
enum class FGInput { None, NoFG = None, DLSSG };
enum class FGOutput { None, NoFG = None, DLSSG };
enum class FGNvngxReplacement { None, Other };
struct feature_version { int a,b,c; auto operator<=>(const feature_version&) const = default; };
template<class T> struct Setting {
    std::optional<T> stored;
    bool has_value() const { return stored.has_value(); }
    T value() const { return stored.value(); }
    T value_or_default() const { return stored.value_or(T{}); }
    T value_or(T fallback) const { return stored.value_or(fallback); }
    void set_volatile_value(T v) { stored = v; }
};
struct Config {
    Setting<bool> FGDLSSGNativeMfgExperimental, FGDLSSGOverrideForceDMFG;
    Setting<float> FGDLSSGFramerateTargetDMFG;
    Setting<int> FGDLSSGOverrideInterpolationCount;
    static Config* Instance() { static Config c; return &c; }
};
struct FakeFg { bool IsActive(){return false;} bool IsPaused(){return false;} unsigned GetInterpolatedFrameCount(){return 0;} };
struct State {
    API swapchainApi = API::Vulkan;
    FGInput activeFgInput = FGInput::None;
    FGOutput activeFgOutput = FGOutput::None;
    FGNvngxReplacement activeFgNvngx = FGNvngxReplacement::None;
    sl::DLSSGMode dlssgLastSetMode = sl::DLSSGMode::eOff;
    bool dlssgGameDMFGSupported = false;
    int delayMenuRenderBy = 0;
    std::optional<int> dlssgMfgMax;
    feature_version streamlineVersion{2,11,1};
    FakeFg* currentFG = nullptr;
    static State& Instance() { static State s; return s; }
};
struct MenuOverlayBase { static inline bool visible=false; static bool IsVisible(){return visible;} };
struct ReflexHooks { static void setDlssgFrameCount(int){} };
namespace VendorId {enum Value {Invalid,Nvidia,AMD};}
struct IdentifyGpu {
    static inline int architecture = 1;
    static inline bool actualSecondary = false;
    static inline VendorId::Value actualVendor = VendorId::Nvidia;
    struct Gpu { VendorId::Value vendorId=VendorId::Nvidia; struct { int architecture_id = 1; } nvidiaArchInfo; LUID luid{}; };
    static Gpu getPrimaryGpu(){Gpu gpu;gpu.nvidiaArchInfo.architecture_id=actualSecondary?0:architecture;return gpu;}
    static std::vector<Gpu> getAllGpus(){
        auto primary=getPrimaryGpu();
        if(!actualSecondary)return {primary};
        Gpu actual;actual.vendorId=actualVendor;actual.luid.LowPart=2;actual.nvidiaArchInfo.architecture_id=architecture;
        return {primary,actual};
    }
};
namespace Neurotic::Mfg {
bool ObservedAdaD3D12Adapter(LUID& luid) noexcept {
    luid={};if(IdentifyGpu::actualSecondary)luid.LowPart=2;return true;
}
bool TryPublishAdaMfg(HMODULE,uint64_t,uint32_t,LUID,HMODULE,const LUID*) noexcept {return true;}
MfgRuntimeSnapshot AdaMfgSnapshot() noexcept { MfgRuntimeSnapshot s; s.status=MfgRuntimeStatus::Published; s.generation=1; s.maxGenerated=3; return s; }
bool HasOwnedAdaMfg(HMODULE,uint64_t) noexcept{return true;}
void InvalidateAdaMfg(uint64_t) noexcept{}
}
namespace Neurotic::Mfg::Experimental {
static Snapshot snapshot;
Snapshot ReadSnapshot() noexcept {return snapshot;}
bool AllowFeatureCall(bool off) noexcept {
    return MayEnterVendor(snapshot.family,snapshot.stage,snapshot.backendLoaded,
        snapshot.prepared,GameFgScope::Current(),off);
}
void RefuseRequest(Reason reason) noexcept {snapshot.reason=reason;}
void ObserveOptions(uint32_t,uint32_t,uint32_t,int) noexcept {}
void ObserveCapacity(uint32_t) noexcept {}
}
namespace Neurotic::Semantic::Character {
struct Activity {void Requested(bool){} void Reset(){}};
inline Activity& NativeFgWork(){static Activity activity;return activity;}
}
namespace DlssNr {
struct Observer {
    unsigned calls=0, stateCalls=0; bool success=false;
    uint64_t generation=1;
    uint64_t Generation() const{return generation;}
    sl::DLSSGOptions options;
    void Options(uint32_t,const sl::DLSSGOptions& o,bool ok){++calls;options=o;success=ok;}
    void State(uint32_t,const sl::DLSSGState&,bool){++stateCalls;}
};
Observer& VulkanNrStreamlineAdapter(){static Observer o;return o;}
namespace FgLifecycle {
inline uint64_t BeginOptions(){return 1;}
inline bool Enabled(){return false;}
template<class... T> void Options(T...){}
template<class... T> void Completion(T...){}
}
namespace PreFg {
inline void PublishProvider(bool,bool,uint32_t){}
struct ProviderState{uint64_t generation=1;}; inline ProviderState Provider(){return {};}
}
}
struct KernelBaseProxy {static auto GetModuleHandleA_(){return &GetModuleHandleA;}};
struct Util {static HMODULE GetCallerModule(void*){return nullptr;}};
static std::atomic<uint64_t> nrMfgHookGeneration{1};
static std::atomic<HMODULE> mfgSelectedWrapper{reinterpret_cast<HMODULE>(1)};
static Neurotic::Mfg::MfgRequestJournal nrMfgRequests;
struct StreamlineHooks {
private:
    static inline sl::RenderAPI renderApi = sl::RenderAPI::eVulkan;
public:
    static void SetRenderApiForTest(sl::RenderAPI api){renderApi=api;}
    static inline bool nrFgOverrides=false;
    static inline std::mutex lastDlssgOptionsMutex;
    static inline bool lastDlssgOptionsReplayable=false;
    static inline sl::DLSSGOptions lastDlssgOptions;
    static inline sl::ViewportHandle lastDlssgViewport;
    static inline uint64_t lastDlssgOptionsHookGeneration=0;
    static inline PFun_slDLSSGSetOptions* o_slDLSSGSetOptions=nullptr;
    static inline PFun_slDLSSGGetState* o_slDLSSGGetState=nullptr;
    static inline decltype(&slGetFeatureFunction) o_slGetFeatureFunction=nullptr;
    static std::optional<bool> nativeMfgAdapter(LUID&,ID3D12GraphicsCommandList* = nullptr) noexcept;
    static bool prepareNativeMfgCapabilities(){return true;}
    static bool prepareExperimentalMfgCapabilities(){return false;}
    static bool experimentalFgLane(bool fg){return fg&&renderApi==sl::RenderAPI::eD3D12&&
        State::Instance().activeFgInput==FGInput::NoFG&&State::Instance().activeFgOutput==FGOutput::NoFG&&
        State::Instance().activeFgNvngx==FGNvngxReplacement::None;}
    static void bindNativeMfgWrapper(const void*){}
    static void updateDlssgOptions();
    static sl::Result hkslDLSSGSetOptions(const sl::ViewportHandle&,const sl::DLSSGOptions&);
    static sl::Result hkslDLSSGGetState(const sl::ViewportHandle&,sl::DLSSGState&,const sl::DLSSGOptions*);
    static sl::Result hkslGetFeatureFunction(sl::Feature,const char*,void*&);
    static void wrapNativeDlssgFunction(const char*,void*&);
};
static sl::Result dummy_slDLSSGSetOptions(const sl::ViewportHandle&,const sl::DLSSGOptions&){return sl::Result::eOk;}
static sl::Result dummy_slDLSSGGetState(const sl::ViewportHandle&,sl::DLSSGState&,const sl::DLSSGOptions*){return sl::Result::eOk;}
#include "nr_streamline_observer_hooks.inc"

static unsigned failures, setterCalls, getterCalls, resolverCalls;
static const sl::DLSSGOptions* enteredOptions;
static sl::DLSSGMode enteredMode;
static size_t enteredStateVersion;
static sl::PFunOnAPIErrorCallback* enteredCallback;
static sl::BaseStructure* enteredNext;
static size_t enteredOptionsVersion;
static uint32_t enteredGenerated;
static sl::Result setterResult=sl::Result::eOk, getterResult=sl::Result::eOk;
static bool rejectHighRatio=false;
static bool changeGenerationInSetter=false;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);++failures;}}
static sl::Result Set(const sl::ViewportHandle&,const sl::DLSSGOptions& o){
    ++setterCalls;enteredOptions=&o;enteredMode=o.mode;
    enteredCallback=o.onErrorCallback;enteredNext=o.next;
    enteredOptionsVersion=o.structVersion;enteredGenerated=o.numFramesToGenerate;
    if(changeGenerationInSetter){++nrMfgHookGeneration;State::Instance().dlssgLastSetMode=sl::DLSSGMode::eOn;}
    if(rejectHighRatio&&o.numFramesToGenerate>1)return sl::Result::eErrorInvalidParameter;
    return setterResult;
}
static sl::Result Get(const sl::ViewportHandle&,sl::DLSSGState& s,const sl::DLSSGOptions*){
    ++getterCalls;enteredStateVersion=s.structVersion;
    s.numFramesActuallyPresented=17;
    if(s.structVersion>=2)s.numFramesToGenerateMax=1;
    return getterResult;
}
static sl::Result Resolve(sl::Feature,const char* name,void*& f){
    ++resolverCalls;
    f=std::strcmp(name,"slDLSSGSetOptions")==0?reinterpret_cast<void*>(&Set):reinterpret_cast<void*>(&Get);
    return sl::Result::eOk;
}
int main(){
    using H=StreamlineHooks;
    H::o_slDLSSGSetOptions=&Set;H::o_slDLSSGGetState=&Get;H::o_slGetFeatureFunction=&Resolve;
    sl::ViewportHandle viewport(0);
    for(size_t v=1;v<=5;++v)for(unsigned borrowed=0;borrowed<4;++borrowed){
        sl::DLSSGOptions options;options.structVersion=v;options.mode=sl::DLSSGMode::eAuto;
        sl::DLSSGOptions extension;
        if(borrowed&1)options.onErrorCallback=[](const sl::APIError&){};
        if(borrowed&2)options.next=&extension;
        auto& observer=DlssNr::VulkanNrStreamlineAdapter();observer.calls=0;
        setterCalls=getterCalls=0;
        H::hkslDLSSGSetOptions(viewport,options);
        Check(setterCalls==1&&enteredOptions==&options,"passive setter forwards the exact original object once");
        Check(enteredMode==sl::DLSSGMode::eAuto,"passive setter preserves Auto");
        Check(observer.calls==1&&observer.success&&observer.options.mode==sl::DLSSGMode::eAuto,"every known ABI is scalar-observed with borrowed pointers");
        Check(!borrowed||!H::lastDlssgOptionsReplayable,"borrowed inputs cannot back UI replay");
        Check(getterCalls==0,"setter performs no extra state query");
    }
    sl::DLSSGOptions off;off.mode=sl::DLSSGMode::eOff;off.onErrorCallback=[](const sl::APIError&){};
    H::hkslDLSSGSetOptions(viewport,off);
    Check(DlssNr::VulkanNrStreamlineAdapter().options.mode==sl::DLSSGMode::eOff,"callback-bearing Off is observed");
    State::Instance().dlssgLastSetMode=sl::DLSSGMode::eOn;
    off.onErrorCallback=nullptr;setterResult=sl::Result::eErrorInvalidParameter;
    H::hkslDLSSGSetOptions(viewport,off);
    Check(State::Instance().dlssgLastSetMode==sl::DLSSGMode::eOn,"failed setter never publishes speculative mode");
    setterResult=sl::Result::eOk;
    changeGenerationInSetter=true;H::hkslDLSSGSetOptions(viewport,off);changeGenerationInSetter=false;
    Check(State::Instance().dlssgLastSetMode==sl::DLSSGMode::eOn&&!H::lastDlssgOptionsReplayable,
        "late setter from retired hook generation cannot publish mode or replay");
    nrMfgHookGeneration=1;
    sl::DLSSGOptions unknown;unknown.structVersion=99;setterCalls=0;
    H::hkslDLSSGSetOptions(viewport,unknown);
    Check(setterCalls==1&&enteredOptions==&unknown&&!DlssNr::VulkanNrStreamlineAdapter().success,
        "unknown ABI forwards unchanged and cannot publish effective observations");
    for(size_t version=1;version<=4;++version){
        sl::DLSSGState state;state.structVersion=version;getterCalls=0;State::Instance().dlssgMfgMax.reset();
        H::hkslDLSSGGetState(viewport,state,nullptr);
        Check(getterCalls==1&&enteredStateVersion==version,"passive getter preserves caller ABI and issues one query");
        Check(state.numFramesActuallyPresented==17,"game receives the provider query counter");
    }
    void* f=nullptr;resolverCalls=0;
    H::hkslGetFeatureFunction(sl::kFeatureDLSS_G,"slDLSSGSetOptions",f);
    Check(f==reinterpret_cast<void*>(&H::hkslDLSSGSetOptions),"public resolver wraps without MFG experiment enabled");
    Check(resolverCalls==1,"setter wrapping does not resolve or query unrelated functions");
    // Opening NeuRotic must leave the game's FG mode/ratio/flags untouched.
    // Its physical-Present overlay draws after generation and owns its waits.
    MenuOverlayBase::visible=true;
    for(auto mode : {sl::DLSSGMode::eOn, sl::DLSSGMode::eAuto, sl::DLSSGMode::eDynamic})
    for(uint32_t count : {1u,2u,3u,5u}){
        sl::DLSSGOptions game;game.mode=mode;game.numFramesToGenerate=count;
        game.onErrorCallback=[](const sl::APIError&){};
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored.reset();
        H::hkslDLSSGSetOptions(viewport,game);
        Check(enteredOptions==&game&&enteredMode==mode&&enteredGenerated==count,
            "visible overlay preserves game FG and MFG settings exactly");
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
        H::hkslDLSSGSetOptions(viewport,game);
        Check(enteredMode==sl::DLSSGMode::eOff&&nrMfgRequests.Current().accepted,
            "explicit Off still applies while overlay is visible");
    }
    MenuOverlayBase::visible=false;
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored.reset();
    // The menu's Game / Off / 2X control must work during a live game call
    // even when its callback/extension pointers cannot be retained for UI replay.
    for(size_t v=1;v<=5;++v)for(unsigned borrowed=1;borrowed<4;++borrowed){
        sl::DLSSGOptions game;game.structVersion=v;game.mode=sl::DLSSGMode::eOn;
        sl::DLSSGOptions extension;
        if(borrowed&1)game.onErrorCallback=[](const sl::APIError&){};
        if(borrowed&2)game.next=&extension;
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
        setterCalls=0;H::hkslDLSSGSetOptions(viewport,game);
        Check(setterCalls==1&&enteredMode==sl::DLSSGMode::eOff,"borrowed live options honor explicit Off");
        Check(enteredCallback==game.onErrorCallback&&enteredNext==game.next&&enteredOptionsVersion==v,
            "synchronous override preserves callback, extension chain and caller ABI");
        Check(game.mode==sl::DLSSGMode::eOn&&!H::lastDlssgOptionsReplayable,
            "override neither mutates game request nor retains borrowed options for UI replay");
        Check(DlssNr::VulkanNrStreamlineAdapter().success&&DlssNr::VulkanNrStreamlineAdapter().options.mode==sl::DLSSGMode::eOff,
            "Present sees accepted Off instead of game's unmodified On");
        Check(Neurotic::Mfg::AcceptedMfgMultiplier(nrMfgRequests.Current())==0u,
            "ordinary Vulkan Off reports provider acceptance");
        setterCalls=0;H::updateDlssgOptions();
        Check(setterCalls==0,"menu never replays borrowed callback or extension pointers");
        setterResult=sl::Result::eErrorInvalidParameter;setterCalls=0;
        H::hkslDLSSGSetOptions(viewport,game);
        Check(setterCalls==1&&enteredMode==sl::DLSSGMode::eOff&&!nrMfgRequests.Current().accepted,
            "rejected Off is reported and never silently falls back to On");
        setterResult=sl::Result::eOk;
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored.reset();
        H::hkslDLSSGSetOptions(viewport,game);
        Check(enteredOptions==&game&&enteredMode==sl::DLSSGMode::eOn,"Game restores exact original borrowed request");
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=1;
        game.numFramesToGenerate=3;H::hkslDLSSGSetOptions(viewport,game);
        Check(enteredMode==sl::DLSSGMode::eOn&&enteredGenerated==1,"2X sets one generated frame with borrowed options");
    }
    // A saved DX12 unlock flag must not swallow the ordinary Vulkan Off control.
    Config::Instance()->FGDLSSGNativeMfgExperimental.stored=true;
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
    off.mode=sl::DLSSGMode::eDynamic;H::hkslDLSSGSetOptions(viewport,off);
    Check(enteredMode==sl::DLSSGMode::eOff,"Vulkan Off takes precedence over a saved DX12 unlock flag");
    Config::Instance()->FGDLSSGNativeMfgExperimental.stored=false;
    // Existing explicit ordinary override and Ada fallback remain functional.
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
    off.mode=sl::DLSSGMode::eOn;H::hkslDLSSGSetOptions(viewport,off);
    Check(enteredMode==sl::DLSSGMode::eOff,"explicit Off override still forwards Off");
    H::SetRenderApiForTest(sl::RenderAPI::eD3D12);State::Instance().swapchainApi=API::DX12;
    Config::Instance()->FGDLSSGNativeMfgExperimental.stored=true;
    // Provider readiness/acceptance remains the existing declared test double;
    // these cases prove the REAL option hook chooses the actual adapter and
    // forwards the own 3x/4x counts, not proprietary delivery/content.
    IdentifyGpu::actualSecondary=true;
    sl::DLSSGOptions own;own.mode=sl::DLSSGMode::eOn;own.numFramesToGenerate=1;
    const auto hasNativeReceipt=[](const char* phase,const char* field){
        for(const auto& line:receiptLines)
            if(line.find(phase)!=std::string::npos&&line.find(field)!=std::string::npos)return true;
        return false;
    };
    for(int generated : {2,3}){
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=generated;
        setterCalls=0;
        const auto result=H::hkslDLSSGSetOptions(viewport,own);
        Check(result==sl::Result::eOk&&setterCalls==1&&enteredGenerated==static_cast<unsigned>(generated)&&
            nrMfgRequests.Current().selected==Neurotic::Mfg::SelectionFromStoredGenerated(generated)&&
            Neurotic::Mfg::AcceptedMfgMultiplier(nrMfgRequests.Current())==static_cast<unsigned>(generated+1),
            generated==2?"own 3X reaches the actual secondary Ada provider boundary":
                         "own 4X reaches the actual secondary Ada provider boundary");
        Check(hasNativeReceipt("phase=setter",generated==2?"forwarded_generated=2":"forwarded_generated=3")&&
            hasNativeReceipt("phase=setter","setter_result=0"),
            "actual accepted setter is projected without a delivery claim");
    }
    sl::DLSSGState passiveCounter;const auto queriesBefore=getterCalls;
    H::hkslDLSSGGetState(viewport,passiveCounter,nullptr);
    Check(getterCalls==queriesBefore+1&&passiveCounter.numFramesActuallyPresented==17&&
        passiveCounter.numFramesToGenerateMax==3&&hasNativeReceipt("phase=state","raw_max=1")&&
        hasNativeReceipt("phase=state","presented_since_query=17"),
        "existing game query projects raw capacity and since-query count before advertisement");
    const auto receiptCount=receiptLines.size();
    const auto settersBefore=setterCalls,statesBefore=getterCalls;
    for(unsigned i=0;i<32;++i){H::hkslDLSSGSetOptions(viewport,own);H::hkslDLSSGGetState(viewport,passiveCounter,nullptr);}
    Check(receiptLines.size()==receiptCount&&setterCalls==settersBefore+32&&getterCalls==statesBefore+32,
        "unchanged per-frame requests neither flood receipts nor add SDK queries");
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
    H::hkslDLSSGSetOptions(viewport,own);
    Check(enteredMode==sl::DLSSGMode::eOff,"own Off remains available on the actual secondary Ada adapter");
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=3;
    own.mode=sl::DLSSGMode::eOff;H::hkslDLSSGSetOptions(viewport,own);
    Check(enteredMode==sl::DLSSGMode::eOff,"game Off survives own high-ratio selection on the actual adapter");
    own.mode=sl::DLSSGMode::eOn;
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
    IdentifyGpu::architecture=0;H::hkslDLSSGSetOptions(viewport,own);
    Check(enteredMode==sl::DLSSGMode::eOff,"own Off remains available when actual architecture is unknown");
    IdentifyGpu::actualVendor=VendorId::AMD;H::hkslDLSSGSetOptions(viewport,own);
    Check(enteredMode==sl::DLSSGMode::eOff,"own Off remains available on a known other actual adapter");
    IdentifyGpu::architecture=1;IdentifyGpu::actualVendor=VendorId::Nvidia;
    // Explicit Off dominates both game Dynamic and our forced Dynamic.
    // Forced Dynamic requires the provider's reported capability as well as
    // the saved control. This is explicit simulated provider state.
    const bool previousDynamicSupport=State::Instance().dlssgGameDMFGSupported;
    State::Instance().dlssgGameDMFGSupported=true;
    for(bool force : {false,true}){
        Config::Instance()->FGDLSSGOverrideForceDMFG.stored=force;
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=0;
        own.mode=force?sl::DLSSGMode::eOn:sl::DLSSGMode::eDynamic;
        setterCalls=0;setterResult=sl::Result::eOk;
        auto result=H::hkslDLSSGSetOptions(viewport,own);
        Check(result==sl::Result::eOk&&setterCalls==1&&enteredMode==sl::DLSSGMode::eOff&&
            nrMfgRequests.Current().selectionStatus==Neurotic::Mfg::MfgDecisionStatus::SelectedOff,
            force?"own Off dominates forced Dynamic":"own Off dominates game Dynamic");
        setterCalls=0;setterResult=sl::Result::eErrorInvalidParameter;
        result=H::hkslDLSSGSetOptions(viewport,own);
        Check(result==sl::Result::eErrorInvalidParameter&&setterCalls==1&&enteredMode==sl::DLSSGMode::eOff&&
            !nrMfgRequests.Current().accepted&&!nrMfgRequests.Current().fellBackToGame,
            force?"rejected own Off under forced Dynamic never falls back On":
                  "rejected own Off under game Dynamic never falls back On");
    }
    State::Instance().dlssgGameDMFGSupported=previousDynamicSupport;
    Config::Instance()->FGDLSSGOverrideForceDMFG.stored=false;setterResult=sl::Result::eOk;
    IdentifyGpu::actualSecondary=false;
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored.reset();
    State::Instance().dlssgMfgMax.reset();sl::DLSSGState rawCapacity;
    H::hkslDLSSGGetState(viewport,rawCapacity,nullptr);
    Check(State::Instance().dlssgMfgMax==1,"raw provider capacity is retained separately from advertised experimental capacity");
    // Recovery must include failed game-owned requests, not just manual overrides.
    // Run negative cases before the process-lifetime latch is first set.
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored.reset();
    sl::DLSSGOptions gameHigh;gameHigh.mode=sl::DLSSGMode::eOn;gameHigh.numFramesToGenerate=5;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"accepted game high ratio does not require restart");
    setterResult=sl::Result::eErrorInvalidParameter;
    gameHigh.numFramesToGenerate=1;H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"ordinary 2X failure does not latch high-ratio recovery");
    gameHigh.numFramesToGenerate=5;gameHigh.mode=sl::DLSSGMode::eOff;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"disabled request with a retained high count does not latch");
    gameHigh.mode=sl::DLSSGMode::eOn;gameHigh.structVersion=99;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"opaque ABI cannot produce a high-ratio refusal observation");
    gameHigh.structVersion=5;changeGenerationInSetter=true;
    H::hkslDLSSGSetOptions(viewport,gameHigh);changeGenerationInSetter=false;nrMfgHookGeneration=1;
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"retired setter cannot latch a new-session restriction");
    H::SetRenderApiForTest(sl::RenderAPI::eVulkan);
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"Vulkan cannot acquire the DX12 recovery latch");
    H::SetRenderApiForTest(sl::RenderAPI::eD3D12);IdentifyGpu::architecture=0;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"non-Ada experimental GPU cannot acquire the RTX40 latch");
    IdentifyGpu::architecture=1;State::Instance().activeFgInput=FGInput::DLSSG;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"replacement FG route cannot acquire native recovery state");
    State::Instance().activeFgInput=FGInput::NoFG;
    namespace EM=Neurotic::Mfg::Experimental;
    EM::snapshot.family=EM::Family::Rtx30;EM::snapshot.backendLoaded=true;EM::snapshot.stage=EM::Stage::Poisoned;
    setterCalls=0;H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(setterCalls==0&&!nrMfgRequests.HighRatioRefusal().blocked,
        "experimental admission refusal never enters provider or fabricates a native setter refusal");
    EM::snapshot={};
    setterCalls=0;
    const auto gameResult=H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(gameResult==sl::Result::eErrorInvalidParameter&&setterCalls==1&&enteredOptions==&gameHigh,
        "failed game high ratio preserves original request and error without invented fallback");
    Check(nrMfgRequests.HighRatioRefusal().blocked&&nrMfgRequests.HighRatioRefusal().requestedGenerated==5,
        "failed game-selected 6X latches restart guidance");
    setterResult=sl::Result::eOk;gameHigh.numFramesToGenerate=1;
    H::hkslDLSSGSetOptions(viewport,gameHigh);
    Check(Neurotic::Mfg::AcceptedMfgMultiplier(nrMfgRequests.Current())==2u&&nrMfgRequests.HighRatioRefusal().blocked,
        "later accepted Game 2X keeps effective setting separate from restart guidance");
    for(int selection : {0,1}){
        Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=selection;
        H::hkslDLSSGSetOptions(viewport,gameHigh);
        Check(nrMfgRequests.Current().accepted&&nrMfgRequests.HighRatioRefusal().blocked,
            "Off and 2X stay usable without clearing the session restriction");
    }
    nrMfgRequests.Invalidate(2);
    Check(nrMfgRequests.HighRatioRefusal().blocked,"hook churn cannot clear restart guidance");
    // A fresh process constructs a new journal; no product reset entry is added.
    nrMfgRequests.~MfgRequestJournal();
    new (&nrMfgRequests) Neurotic::Mfg::MfgRequestJournal;
    Check(!nrMfgRequests.HighRatioRefusal().blocked,"new session clears the previous rejection");
    Config::Instance()->FGDLSSGOverrideInterpolationCount.stored=3;
    rejectHighRatio=true;setterCalls=0;off.numFramesToGenerate=1;
    H::hkslDLSSGSetOptions(viewport,off);
    Check(setterCalls==2&&enteredMode==sl::DLSSGMode::eOn,"rejected explicit high-ratio override falls back to game request");
    Check(nrMfgRequests.HighRatioRefusal().blocked,"manual high-ratio rejection retains its existing session latch");
    std::printf("Streamline hook observer: %u failure(s)\n",failures);
    return failures?1:0;
}
