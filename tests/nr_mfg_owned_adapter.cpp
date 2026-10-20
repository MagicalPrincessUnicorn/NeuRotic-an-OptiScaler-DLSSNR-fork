// Link the actual MfgAdaUnlock.cpp with NR_MFG_RUNTIME_TEST. This maps data
// with DONT_RESOLVE_DLL_REFERENCES; it NEVER enters proprietary DLL code.
// Adapter A is a constructed ownership identity, B comes from public WARP.
// GPU capability enumeration is a declared test double, not physical Ada.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <sl_core_api.h>
#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#define NV_GPU_ARCHITECTURE_AD100 1
#define NV_GPU_ARCHITECTURE_GB200 2
namespace VendorId {enum Value {Invalid,Nvidia,AMD};}
enum class FGInput {NoFG,Other};enum class FGOutput {NoFG};enum class FGNvngxReplacement {None};
struct State {
    FGInput activeFgInput=FGInput::NoFG;FGOutput activeFgOutput=FGOutput::NoFG;
    FGNvngxReplacement activeFgNvngx=FGNvngxReplacement::None;
    static State& Instance(){static State s;return s;}
};
struct Config {
    struct Setting {bool stored=true;bool value_or_default() const{return stored;}} FGDLSSGNativeMfgExperimental;
    static Config* Instance(){static Config c;return &c;}
};
struct IdentifyGpu {
    struct Gpu {LUID luid{};VendorId::Value vendorId=VendorId::Nvidia;
        struct {unsigned architecture_id=NV_GPU_ARCHITECTURE_AD100;} nvidiaArchInfo;};
    static inline std::vector<Gpu> gpus;
    static auto getAllGpus(){return gpus;}
};
static std::atomic<uint64_t> nrMfgHookGeneration{12};
struct StreamlineHooks {
    static inline sl::RenderAPI renderApi=sl::RenderAPI::eD3D12;
    static inline std::atomic<HMODULE> mfgSelectedWrapper{nullptr};
    static std::optional<bool> nativeMfgAdapter(LUID&,ID3D12GraphicsCommandList* = nullptr) noexcept;
    static bool prepareNativeMfgCapabilities(HMODULE = nullptr,ID3D12GraphicsCommandList* = nullptr);
};
#include "nr_mfg_owned_adapter_hooks.inc"
static bool Check(bool v,const char* text){std::printf("%s: %s\n",v?"PASS":"FAIL",text);return v;}
int wmain(int argc,wchar_t** argv)
{
    if(argc!=3)return 2;
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> command;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||
       FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))||
       FAILED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))||
       FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&command))))return 2;
    const LUID adapterB=device->GetAdapterLuid();
    LUID adapterA=adapterB;adapterA.LowPart^=1u;
    const std::wstring folder=argv[1],mode=argv[2];
    const auto wrapper=LoadLibraryExW((folder+L"/sl.dlss_g.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    const auto provider=LoadLibraryExW((folder+L"/nvngx_dlssg.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!wrapper||!provider)return 2;
    StreamlineHooks::mfgSelectedWrapper=wrapper;
    Neurotic::Mfg::ObserveAdaD3D12Adapter(adapterA);
    if(!Neurotic::Mfg::TryPublishAdaMfg(wrapper,12,5,adapterA,provider,&adapterA)||
       !Neurotic::Mfg::HasOwnedAdaMfg(wrapper,12)){
        std::puts("SETUP FAIL: actual inert runtime did not own adapter A");return 2;
    }
    if(!Check(Neurotic::Mfg::ObserveAdaCreateOwner(provider,&adapterA,12)&&
        Neurotic::Mfg::TryPublishAdaMfg(wrapper,12,2,adapterA,provider,&adapterA)&&
        Neurotic::Mfg::HasOwnedAdaMfg(wrapper,12),
        "same actual owner and adapter retain valid publication"))return 1;
    if(mode==L"optout")Config::Instance()->FGDLSSGNativeMfgExperimental.stored=false;
    if(mode==L"route")State::Instance().activeFgInput=FGInput::Other;
    IdentifyGpu::Gpu actual;actual.luid=adapterB;
    if(mode==L"unsupported")actual.vendorId=VendorId::AMD;
    if(mode==L"unknown")actual.nvidiaArchInfo.architecture_id=0;
    IdentifyGpu::gpus={actual};
    bool prepared=false;
    if(mode==L"direct")prepared=Neurotic::Mfg::TryPublishAdaMfg(wrapper,12,5,adapterB,provider,&adapterB);
    else if(mode==L"ada"||mode==L"unsupported"||mode==L"unknown"||mode==L"optout"||mode==L"route")
        prepared=StreamlineHooks::prepareNativeMfgCapabilities(provider,command.Get());
    else if(mode==L"unverified")prepared=StreamlineHooks::prepareNativeMfgCapabilities(provider,nullptr);
    else return 2;
    const auto snapshot=Neurotic::Mfg::AdaMfgSnapshot();
    const bool revoked=!prepared&&snapshot.status==Neurotic::Mfg::MfgRuntimeStatus::Stale&&
        !Neurotic::Mfg::HasOwnedAdaMfg(wrapper,12);
    const char* text=mode==L"direct"?"direct actual runtime cannot overwrite owned adapter A with B":
        mode==L"ada"?"authentic Create on other Ada identity revokes owned adapter A":
        mode==L"unsupported"?"authentic unsupported-device Create revokes before admission returns":
        mode==L"unknown"?"authentic unknown-capability Create revokes before admission returns":
        mode==L"optout"?"authentic changed Create revokes ownership even after opt-out":
        mode==L"route"?"authentic changed Create revokes ownership before route admission":
        "unverified Create revokes existing owned adapter eligibility";
    return Check(revoked,text)?0:1;
}
