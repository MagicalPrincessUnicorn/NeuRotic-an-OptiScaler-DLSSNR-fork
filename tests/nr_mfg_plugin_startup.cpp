// Exact production hook bodies are supplied in the generated include.
// Actual WARP COM device; GPU enumeration/provider ownership/capacity cache
// are explicit public simulations. No vendor image or entry is loaded.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <sl_core_api.h>
#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include "../OptiScaler/mfg/MfgAdaPlan.h"
#include <atomic>
#include <array>
#include <cstring>
#include <cstdio>
#include <optional>
#include <vector>
#include <string>
#include <functional>
#include <json.hpp>
#include "../OptiScaler/include/sl.param/parameters.h"
#include "../OptiScaler/mfg/MfgControl.h"
#include "../OptiScaler/hooks/StreamlineOnLoadInvocation.h"
#include "../OptiScaler/hooks/StreamlineManagedCaps.h"
#include "../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <stdexcept>
using namespace nlohmann::literals;
#define LOG_FUNC(...) ((void)0)
#define NrSlCall(a,b,c,call,...) call()
#define NV_GPU_ARCHITECTURE_AD100 1
#define NV_GPU_ARCHITECTURE_GB200 2
enum class FGInput { NoFG, Other, DLSSG, NvngxFG };
enum class FGOutput { NoFG, Other, DLSSG };
enum class FGNvngxReplacement { None, Other };
struct State {
    struct {unsigned major=2;} streamlineVersion; // Declared modern fixture; spoofing stays disabled.
    FGInput activeFgInput = FGInput::NoFG;
    FGOutput activeFgOutput = FGOutput::NoFG;
    FGNvngxReplacement activeFgNvngx = FGNvngxReplacement::None;
    static State& Instance() { static State s; return s; }
};
struct Config {
    struct Setting { bool stored = true; bool value_or_default() const {return stored;} };
    Setting FGDLSSGNativeMfgExperimental;
    Setting StreamlineSpoofing{false};
    static Config* Instance() {static Config c;return &c;}
};
static bool EqualLuid(LUID a, LUID b) {return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;}
namespace VendorId {enum Value {Invalid,Nvidia,AMD};}
#include "nr_mfg_plugin_startup_caps.inc"
// This startup fixture does not exercise capability spoofing. A future accidental
// entry must fail explicitly rather than invent a successful capability override.
template<class Reader>
static void NrBeginManagedCaps(std::optional<Neurotic::Hooks::StreamlineScopedCaps<SystemCaps>>&,
    sl::param::IParameters*,sl::Feature,Neurotic::Hooks::StreamlineCapsTarget,Reader&&)
{throw std::logic_error("capability override is outside the MFG startup fixture");}
static bool NrOwnedOnLoadJson(bool result,const char** output,std::string_view rendered,const char*)
{return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,output,rendered);}
struct IdentifyGpu {
    struct Gpu {LUID luid{}; VendorId::Value vendorId=VendorId::Nvidia; bool dlssCapable=true; struct {int architecture_id=0;} nvidiaArchInfo;};
    static inline std::vector<Gpu> gpus;
    static auto getAllGpus() {return gpus;}
    static auto getPrimaryGpu() {return gpus.front();}
};
namespace Fixture {
std::array<uint8_t,32> provider;
std::array<uint8_t,16> wrapper;
LUID observed{};
bool hasObserved=false, providerAvailable=true, providerAmbiguous=false;
unsigned originalCalls=0, prepareCalls=0, pluginCalls=0, pluginHookCalls=0, writes=0;
bool allOriginalsPrepared=true, reenter=false, inside=false, reentryOk=false, pluginResult=true;
HMODULE publicWrapper=nullptr, ownedWrapper=nullptr;
void* bindingAddress=nullptr;
Neurotic::Mfg::MfgPublicationReceipt ownedReceipt;
uint64_t ownedProviderFingerprint=0,ownedWrapperFingerprint=0;
uint32_t cachedGenerated=0;
sl::Result originalResult=sl::Result::eOk;
Neurotic::Mfg::MfgRuntimeSnapshot publication;
struct Ops : Neurotic::Mfg::MfgMutationOps {
    static bool Contains(uintptr_t p,size_t n) {
        const auto in=[&](auto& a){auto b=reinterpret_cast<uintptr_t>(a.data());
            return p>=b&&p-b<=a.size()&&n<=a.size()-(p-b);};
        return in(provider)||in(wrapper);
    }
    bool Read(uintptr_t p,uint8_t* b,size_t n) noexcept override {
        if(!Contains(p,n))return false;std::memcpy(b,reinterpret_cast<void*>(p),n);return true;}
    bool MakeWritable(uintptr_t p,size_t n,uintptr_t& prior) noexcept override {prior=0;return Contains(p,n);}
    bool Write(uintptr_t p,const uint8_t* b,size_t n) noexcept override {
        if(!Contains(p,n))return false;++writes;std::memcpy(reinterpret_cast<void*>(p),b,n);return true;}
    bool Flush(uintptr_t p,size_t n) noexcept override {return Contains(p,n);}
    bool RestoreProtection(uintptr_t p,size_t n,uintptr_t) noexcept override {return Contains(p,n);}
};
void Reset(LUID actual) {
    provider={0x3d,0xb0,1,0,0,0,0,0,0x81,0xfd,0xb0,1,0,0};
    wrapper={0xba,3,0,0,0,0x3b,0xca,0x0f,0x42,0xd1};
    observed={};hasObserved=false;providerAvailable=true;providerAmbiguous=false;
    originalCalls=prepareCalls=cachedGenerated=pluginCalls=pluginHookCalls=writes=0;
    publication={};originalResult=sl::Result::eOk;ownedWrapper=nullptr;ownedReceipt={};
    ownedProviderFingerprint=ownedWrapperFingerprint=0;allOriginalsPrepared=true;
    reenter=inside=reentryOk=false;pluginResult=true;
    Config::Instance()->FGDLSSGNativeMfgExperimental.stored=true;
    State::Instance()={};
    IdentifyGpu::Gpu primary,secondary;primary.luid={actual.LowPart^1u,actual.HighPart};
    secondary.luid=actual;secondary.nvidiaArchInfo.architecture_id=NV_GPU_ARCHITECTURE_AD100;
    IdentifyGpu::gpus={primary,secondary};
}
bool Check(bool value,const char* message) {std::printf("%s: %s\n",value?"PASS":"FAIL",message);return value;}
}
namespace Neurotic::Mfg {
bool ObserveAdaCreateOwner(HMODULE,const LUID*,uint64_t) noexcept {return false;} // No entered Create in these startup cases.
bool IsAdaMfgModule(HMODULE module) noexcept {return module==Fixture::publicWrapper;}
void InvalidateAdaMfg(uint64_t generation) noexcept {
    if(Fixture::publication.status==MfgRuntimeStatus::Published&&Fixture::publication.generation!=generation)
        Fixture::publication.status=MfgRuntimeStatus::Stale;
}
bool ObservedAdaD3D12Adapter(LUID& luid) noexcept {if(!Fixture::hasObserved)return false;luid=Fixture::observed;return true;}
void ObserveAdaD3D12Adapter(LUID luid) noexcept {Fixture::observed=luid;Fixture::hasObserved=true;}
MfgRuntimeSnapshot AdaMfgSnapshot() noexcept {return Fixture::publication;}
bool TryPublishAdaMfg(HMODULE selected,uint64_t generation,uint32_t abi,LUID expected,HMODULE,const LUID*) noexcept {
    ++Fixture::prepareCalls;
    if(!selected||selected!=Fixture::publicWrapper||!Fixture::hasObserved||
       !EqualLuid(expected,Fixture::observed)||!Fixture::providerAvailable)return false;
    if(Fixture::ownedReceipt.owned){
        if(Fixture::ownedWrapper!=selected)return false;
        Fixture::Ops ops;
        for(size_t i=0;i<Fixture::ownedReceipt.plan.count;++i){
            const auto& site=Fixture::ownedReceipt.plan.sites[i];uint8_t current[8]{};
            if(!ops.Read(site.address,current,site.size)||!MfgBytesEqual(current,site.replacement.data(),site.size))return false;
        }
        if(FingerprintMfgImage(Fixture::provider.data(),Fixture::provider.size())!=Fixture::ownedProviderFingerprint||
           FingerprintMfgImage(Fixture::wrapper.data(),Fixture::wrapper.size())!=Fixture::ownedWrapperFingerprint)return false;
        Fixture::publication.status=MfgRuntimeStatus::Published;Fixture::publication.generation=generation;
        return true;
    }
    MfgProviderImages images {
        {Fixture::provider.data(),Fixture::provider.size(),101,generation,true,
            Fixture::providerAmbiguous?2u:1u,{310,8,0,0},1,abi,
            FingerprintMfgImage(Fixture::provider.data(),Fixture::provider.size())},
        {Fixture::wrapper.data(),Fixture::wrapper.size(),reinterpret_cast<uintptr_t>(selected),generation,true,1,{2,13,0,0},0,abi,
            FingerprintMfgImage(Fixture::wrapper.data(),Fixture::wrapper.size())},101,reinterpret_cast<uintptr_t>(selected)};
    const auto q=QualifyMfgProvider(images,{true,true},generation);
    MfgPatchPlan plan;if(!BuildAdaGatePlan(images,q,plan))return false;
    Fixture::Ops ops;MfgPublicationLatch latch;
    const auto p=PublishMfgPatches(ops,plan,generation,latch);
    if(p.status!=MfgPublicationStatus::OwnedPublication)return false;
    Fixture::ownedWrapper=selected;Fixture::ownedReceipt=p.receipt;
    Fixture::ownedProviderFingerprint=FingerprintMfgImage(Fixture::provider.data(),Fixture::provider.size());
    Fixture::ownedWrapperFingerprint=FingerprintMfgImage(Fixture::wrapper.data(),Fixture::wrapper.size());
    Fixture::publication.status=MfgRuntimeStatus::Published;
    Fixture::publication.generation=generation;Fixture::publication.maxGenerated=q.maxGenerated;
    return true;
}
}
namespace Neurotic::Mfg::Experimental {
void ObserveDevice(ID3D12Device*) noexcept {} void ObserveWrapper(HMODULE) noexcept {}
}
namespace Neurotic::Semantic::Character {
struct Activity {void Reset() noexcept {}};
inline Activity& NativeFgWork(){static Activity value;return value;}
}
static Neurotic::Mfg::MfgRequestJournal nrMfgRequests;
static void PatchSL1PluginJson(nlohmann::json&) {} // Unrelated SL1 policy, not exercised in native NoFG lane.
static std::atomic<uint64_t> nrMfgHookGeneration{12};
struct StreamlineHooks {
    static inline sl::RenderAPI renderApi=sl::RenderAPI::eD3D12;
    static inline std::atomic<HMODULE> mfgSelectedWrapper{reinterpret_cast<HMODULE>(202)};
    static inline sl::Result (*o_slSetD3DDevice)(void*)=nullptr;
    using OnLoadInvocation=Neurotic::Hooks::StreamlineOnLoadInvocation<sl::param::IParameters>;
    static inline OnLoadInvocation::Load selectedFixturePlugin=nullptr;
    // Direct transform signature fixture, not a fabricated registry/image lease.
    static OnLoadInvocation InvocationForFixture(){return {selectedFixturePlugin,nullptr,nullptr,nullptr};}
    static void hookSystemCaps(sl::param::IParameters*) {} static uint32_t getSystemCapsArch(const SystemCaps* = nullptr){return 0;}
    static void spoofArch(uint32_t,sl::Feature) {} static void setArch(uint32_t) {}
    static void bindNativeMfgWrapper(const void*);
    static bool hkdlssg_slOnPluginLoad(const OnLoadInvocation&,const char*,const char**);
    static bool prepareExperimentalMfgCapabilities() noexcept {return false;}
    static std::optional<bool> nativeMfgAdapter(LUID&,ID3D12GraphicsCommandList* = nullptr) noexcept;
    static bool prepareNativeMfgCapabilities(HMODULE=nullptr,ID3D12GraphicsCommandList* = nullptr);
    static sl::Result hkslSetD3DDevice(void*);
};
#include "nr_mfg_plugin_startup_hooks.inc"

static void OriginalPluginProbe()
{
    ++Fixture::pluginCalls;
    const bool prepared=Fixture::publication.status==Neurotic::Mfg::MfgRuntimeStatus::Published&&
        Fixture::wrapper[9]==0xd2;
    Fixture::allOriginalsPrepared=Fixture::allOriginalsPrepared&&prepared;
    Fixture::cachedGenerated=Fixture::wrapper[9]==0xd2?Fixture::wrapper[1]:1;
    if(Fixture::reenter&&!Fixture::inside){
        Fixture::inside=true;
        const auto generation=nrMfgHookGeneration.load();const auto writes=Fixture::writes;
        StreamlineHooks::bindNativeMfgWrapper(Fixture::bindingAddress);
        const char* json=nullptr;++Fixture::pluginHookCalls;
        const bool nested=StreamlineHooks::hkdlssg_slOnPluginLoad(StreamlineHooks::InvocationForFixture(),"{}",&json);
        Fixture::reentryOk=nested&&json&&nrMfgHookGeneration.load()==generation&&Fixture::writes==writes;
        Fixture::inside=false;
    }
}
static sl::Result OriginalDeviceStartup(void*)
{
    ++Fixture::originalCalls;
    StreamlineHooks::bindNativeMfgWrapper(Fixture::bindingAddress);
    const char* json=nullptr;++Fixture::pluginHookCalls;
    Fixture::pluginResult=StreamlineHooks::hkdlssg_slOnPluginLoad(StreamlineHooks::InvocationForFixture(),"{}",&json);
    return Fixture::pluginResult?sl::Result::eOk:sl::Result::eErrorFeatureNotSupported;
}
int wmain(int argc,wchar_t** argv)
{
    if(argc!=2)return 2;
    using Microsoft::WRL::ComPtr;
    const auto module=LoadLibraryW(argv[1]); // ONLY self-authored public test DLL.
    if(!module)return 2;
    auto plugin=reinterpret_cast<decltype(StreamlineHooks::selectedFixturePlugin)>(GetProcAddress(module,"FixturePluginLoad"));
    auto setProbe=reinterpret_cast<void(*)(void(*)())>(GetProcAddress(module,"FixtureSetProbe"));
    auto setResult=reinterpret_cast<void(*)(bool)>(GetProcAddress(module,"FixtureSetResult"));
    if(!plugin||!setProbe||!setResult)return 2;
    Fixture::publicWrapper=module;Fixture::bindingAddress=reinterpret_cast<void*>(plugin);
    StreamlineHooks::selectedFixturePlugin=plugin;StreamlineHooks::o_slSetD3DDevice=OriginalDeviceStartup;
    setProbe(OriginalPluginProbe);
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
       FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))||
       FAILED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 2;
    unsigned failures=0;
    for(unsigned mode=0;mode<5;++mode){
        Fixture::Reset(device->GetAdapterLuid());StreamlineHooks::mfgSelectedWrapper=nullptr;
        Config::Instance()->FGDLSSGNativeMfgExperimental.stored=mode!=1;
        Fixture::providerAvailable=mode!=2;Fixture::reenter=mode==3;setResult(mode!=4);
        const auto generation=nrMfgHookGeneration.load();
        const auto result=StreamlineHooks::hkslSetD3DDevice(device.Get());
        const bool prepared=mode!=1&&mode!=2;
        const unsigned invocations=mode==3?2u:1u;
        if(!Fixture::Check(result==(mode==4?sl::Result::eErrorFeatureNotSupported:sl::Result::eOk)&&
            Fixture::originalCalls==1&&Fixture::pluginCalls==invocations&&Fixture::pluginHookCalls==invocations,
            "nested startup forwards each original exactly once and preserves result"))++failures;
        if(!Fixture::Check(Fixture::cachedGenerated==(prepared?3u:1u)&&
            (prepared?Fixture::allOriginalsPrepared:Fixture::publication.status!=Neurotic::Mfg::MfgRuntimeStatus::Published),
            "full preparation precedes original plugin cache, or native capacity stays untouched"))++failures;
        if(!Fixture::Check(StreamlineHooks::mfgSelectedWrapper.load()==module&&
            nrMfgHookGeneration.load()==generation+1,
            "real GetModuleHandleEx binding selects the entered public wrapper once"))++failures;
        if(!Fixture::Check(Fixture::writes==(prepared?3u:0u),
            "nested preparation owns one complete gate plan without duplicate or partial writes"))++failures;
        if(mode==3&&!Fixture::Check(Fixture::reentryOk,"reentrant plugin startup retains current generation and owned bytes"))++failures;
    }
    std::printf("Nested public plugin startup: %u failure(s)\n",failures);
    FreeLibrary(module);
    return failures?1:0;
}
