#include <pch.h>
#include "ExperimentalMfgRuntime.h"
#include "MfgAdaUnlock.h"
#include "MfgCreateEntry.h"
#include "MfgModuleVersion.h"
#include <misc/IdentifyGpu.h>
#include <Config.h>
#include <proxies/KernelBase_Proxy.h>
#include <psapi.h>
#include <mutex>
#include <array>
#include <memory>
#include "../../external/mfg_ampere/src/addons/mfgunlock/blackwell_temporal.hpp"
#include "../../external/mfg_ampere/src/addons/mfgunlock/capability_policy.hpp"

namespace Neurotic::Mfg::Experimental {
namespace {
struct Runtime {
    std::mutex mutex;
    SessionPreferences preferences;
    Snapshot state;
    bool killed=false,created=false,attempted=false;
    HMODULE provider=nullptr;
    HMODULE wrapper=nullptr;
    uintptr_t wrapperGate=0;
    std::array<uint8_t,10> wrapperPreimage{};
    void* createEntry=nullptr;
    uintptr_t physicalGpu=0;
    RetainedCreateEntries createObserver;
    EarlyCoverage coverage;
    bool coverageStarted=false;
    uint64_t validatedEpoch=0;
    std::vector<mfgunlock::blackwelltemporal::Redirect> redirects;
    std::vector<mfgunlock::provider::internal::BytePatch> gates;
};
// Process lifetime retention: no detach unpatch, destructor, provider unload or
// GPU wait. Even a poisoned transaction keeps every possible live allocation.
Runtime& R() { static auto* runtime=new Runtime; return *runtime; }
std::atomic_bool requested{false};
std::atomic<uint64_t> moduleEpoch{1};
std::atomic<HMODULE> retainedProvider{nullptr};
uint64_t Id(LUID v) { return (uint64_t(uint32_t(v.HighPart))<<32)|v.LowPart; }
bool WrapperCurrent(const Runtime& r) {
    std::array<uint8_t,10> observed{};
    return r.wrapperGate && ReadMfgMemory(reinterpret_cast<const void*>(r.wrapperGate),observed.data(),observed.size()) &&
        observed==r.wrapperPreimage;
}
bool Report(Runtime& r,Stage stage,Reason reason) {
    if(!CanReport(r.state.stage,stage))return false;
    if(stage==Stage::Unavailable || stage==Stage::Poisoned || stage==Stage::Off)r.state.prepared=false;
    if(r.state.stage!=stage || r.state.reason!=reason)
        LOG_INFO("Experimental MFG: family={} stage={} reason={} hardwareQualification=NOT_RUN",
            unsigned(r.state.family),StageText(stage),ReasonText(reason));
    r.state.stage=stage;r.state.reason=reason;
    return stage==Stage::Ready || stage==Stage::ActivityObserved;
}
bool Rollback(Runtime& r) {
    bool ok=mfgunlock::provider::RestoreMfgComparisons(r.gates);
    ok=mfgunlock::blackwelltemporal::Restore(r.redirects) && ok;
    // Only unsubmitted preparation may reach here. Never called by UI/reload.
    AcquireSRWLockExclusive(&mfgunlock::provider::internal::g_lock);
    for(auto& p:mfgunlock::provider::internal::g_providers)
        ok=mfgunlock::provider::internal::Undo(p) && ok;
    ReleaseSRWLockExclusive(&mfgunlock::provider::internal::g_lock);
    r.state.prepared=false;
    return ok;
}
void BeginCoverage(Runtime& r) noexcept {
    if(r.coverageStarted)return;
    try {
    // Observe loads from opt-in session startup, before renderer identity is
    // available. Already-loaded providers still have no proven pre-use history.
    auto modules=std::make_unique<std::array<HMODULE,1024>>();DWORD bytes=0;
    if(!EnumProcessModules(GetCurrentProcess(),modules->data(),sizeof(*modules),&bytes) || bytes>sizeof(*modules))return;
    auto ids=std::make_unique<std::array<uintptr_t,1024>>();
    for(size_t i=0;i<bytes/sizeof(HMODULE);++i)(*ids)[i]=reinterpret_cast<uintptr_t>((*modules)[i]);
    r.coverage.Begin({ids->data(),bytes/sizeof(HMODULE)});r.coverageStarted=true;
    } catch(...) { } // Failed inventory never becomes proof of a fresh provider.
}
}
void BeginSession(Preferences p) noexcept {
    auto& r=R();std::lock_guard lock(r.mutex);
    if(r.preferences.begun)return;
    r.preferences.Begin(p);r.state.session=r.state.saved=p;
    wchar_t env[4]{};
    r.killed=GetEnvironmentVariableW(L"NEUROTIC_DISABLE_EXPERIMENTAL_MFG",env,4)==1 && env[0]==L'1';
    r.state.requested=p.rtx30||p.rtx20;
    requested.store(r.state.requested && !r.killed);
    if(r.state.requested && !r.killed)BeginCoverage(r);
    Report(r,r.state.requested?Stage::Waiting:Stage::Off,
        r.killed?Reason::DisabledByEnvironment:r.state.requested?Reason::IdentityUnknown:Reason::NotRequested);
}
void SettingsSaved(Preferences p,bool success) noexcept { auto& r=R();std::lock_guard lock(r.mutex);r.preferences.Saved(p,success);r.state.saved=r.preferences.saved;r.state.saveFailed=!success; }
Snapshot ReadSnapshot() noexcept { auto& r=R();std::lock_guard lock(r.mutex);auto s=r.state;
    s.prepared=IsAdmitted(s.stage,s.prepared) && r.validatedEpoch==moduleEpoch.load() &&
        r.createObserver.IsCurrent(r.provider,r.createEntry) && WrapperCurrent(r);return s; }
bool Requested() noexcept { return requested.load(std::memory_order_acquire); }
bool AllowFeatureCall(bool off) noexcept {
    if(!Requested())return true;
    const auto s=ReadSnapshot();
    return MayEnterVendor(s.family,s.stage,s.backendLoaded,s.prepared,GameFgScope::Current(),off);
}
void ObserveDevice(ID3D12Device* device) noexcept {
    if(!device)return;
    try {
        const auto luid=device->GetAdapterLuid();
        Renderer evidence{};
        std::array<char,128> name{};
        uintptr_t physical=0;
        for(const auto& gpu:IdentifyGpu::getAllGpus()) if(IsEqualLUID(luid,gpu.luid)) {
            evidence={true,uint32_t(gpu.vendorId),uint32_t(gpu.nvidiaArchInfo.architecture_id),
                uint32_t(gpu.nvidiaArchInfo.implementation_id),gpu.deviceId,Id(luid),
                (gpu.nvidiaArchInfo.architecture_id==0x160 && gpu.name.find("GeForce RTX 20")!=std::string::npos) ||
                (gpu.nvidiaArchInfo.architecture_id==0x170 && gpu.name.find("GeForce RTX 30")!=std::string::npos)};
            std::copy_n(gpu.name.data(),std::min(gpu.name.size(),name.size()-1),name.data());
            physical=reinterpret_cast<uintptr_t>(gpu.unspoofedPhysicalGpu);
            break;
        }
        auto& r=R();std::lock_guard lock(r.mutex);
        if(r.state.stage==Stage::Unavailable || r.state.stage==Stage::Poisoned)return;
        if(r.state.renderer.bound && r.state.renderer.luid!=evidence.luid) {
            Report(r,Stage::Unavailable,Reason::DeviceChanged);return;
        }
        r.state.renderer=evidence;
        r.state.rendererName=name;
        r.physicalGpu=physical;
        r.state.family=Select(r.preferences.session,evidence,r.killed,false);
        if(r.state.family!=Family::None && !r.coverageStarted) {
            BeginCoverage(r);
        }
        if(!r.state.requested) Report(r,Stage::Off,Reason::NotRequested);
        else if(r.killed) Report(r,Stage::Off,Reason::DisabledByEnvironment);
        else if(!evidence.bound) Report(r,Stage::Waiting,Reason::IdentityUnknown);
        else if(r.state.family==Family::None) Report(r,Stage::Off,Detect(evidence)==Family::None?Reason::WrongGpu:Reason::NotRequested);
        else if(!r.attempted) Report(r,Stage::Waiting,Reason::ProviderMissing);
    } catch(...) { }
}
bool MatchesAdapter(LUID adapter) noexcept {auto s=ReadSnapshot();return s.renderer.bound && s.renderer.luid==Id(adapter);}
bool PrepareAtBoundary(bool gameOwnedD3D12) noexcept {
    if(!Requested())return false;
    auto& r=R();std::unique_lock lock(r.mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    if(r.state.family==Family::None)return false;
    if(!gameOwnedD3D12)return Report(r,Stage::Unavailable,Reason::UnsupportedRoute);
    if(r.state.stage==Stage::Poisoned || r.state.stage==Stage::Unavailable)return false;
    if(!r.wrapper)return Report(r,Stage::Waiting,Reason::WrapperMissing);
    if(!r.state.backendLoaded && r.created)return Report(r,Stage::Unavailable,Reason::TooLate);
    if(!r.state.backendLoaded && r.attempted)return false;
    try {
        uint32_t structuralCeiling=r.state.ceiling;
        if(!r.state.backendLoaded) {
            MfgProviderVersion wrapperVersion;
            const auto wrapperPe=ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(r.wrapper));
            MfgSectionSnapshot wrapperCopy;
            if(!ReadMfgModuleVersion(r.wrapper,wrapperVersion) ||
               !wrapperCopy.Capture(wrapperPe.ExecutableText()))
                return Report(r,Stage::Unavailable,Reason::WrapperChanged);
            size_t gateOffset=0;
            structuralCeiling=QualifiedMfgWrapperCeiling(wrapperCopy.bytes.data(),wrapperCopy.bytes.size(),wrapperVersion,&gateOffset);
            if(!structuralCeiling)return Report(r,Stage::Unavailable,Reason::StructuralLimit);
            r.wrapperGate=wrapperCopy.address+gateOffset;
            std::copy_n(wrapperCopy.bytes.data()+gateOffset,r.wrapperPreimage.size(),r.wrapperPreimage.begin());
        }
        if(!WrapperCurrent(r))return Report(r,r.state.backendLoaded?Stage::Poisoned:Stage::Unavailable,Reason::WrapperChanged);
        if(Config::Instance()->FGDLSSGOverrideForceDMFG.value_or_default())
            return Report(r,Stage::Unavailable,Reason::DynamicNotQualified);
        if(Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() ||
           AdaMfgSnapshot().status==MfgRuntimeStatus::Published)
            return Report(r,Stage::Unavailable,Reason::OwnerConflict);
        const auto observedEpoch=moduleEpoch.load();
        std::array<HMODULE,1024> modules{};DWORD bytes=0;
        if(!EnumProcessModules(GetCurrentProcess(),modules.data(),sizeof(modules),&bytes) || bytes>sizeof(modules))
            return Report(r,Stage::Unavailable,Reason::ProviderAmbiguous);
        HMODULE selected=nullptr;
        for(size_t i=0;i<bytes/sizeof(HMODULE);++i) {
            // The retained selected image already passed the full marker scan.
            // Every other executable candidate is rechecked, including native
            // Ldr loads which may bypass the Win32 load-return notification.
            if((r.state.backendLoaded && modules[i]==r.provider) || mfgunlock::provider::IsDlssgProvider(modules[i])) {
                if(selected && selected!=modules[i])return Report(r,Stage::Unavailable,Reason::ProviderAmbiguous);
                selected=modules[i];
            }
        }
        if(!selected)return Report(r,Stage::Waiting,Reason::ProviderMissing);
        if(r.state.backendLoaded) {
            bool current=selected==r.provider && mfgunlock::provider::PreparedProviderCount()==1 &&
                r.createObserver.IsCurrent(r.provider,r.createEntry);
            for(const auto& gate:r.gates)current=current && *gate.address==gate.after;
            for(const auto& redirect:r.redirects)for(const auto& descriptor:redirect.descriptors)
                current=current && *descriptor.slot==reinterpret_cast<uint64_t>(redirect.allocation);
            if(!current)return Report(r,Stage::Poisoned,Reason::OwnerConflict);
            r.validatedEpoch=observedEpoch;return r.validatedEpoch==moduleEpoch.load();
        }
        if(!r.coverage.CanPrepare(reinterpret_cast<uintptr_t>(selected)) ||
           !r.createObserver.IsCurrent(selected,r.createEntry))
            return Report(r,Stage::Unavailable,Reason::TooLate);
        // Retain the selected executable mapping for qualification below.
        // The discovery marker scan above still reads unretained candidates;
        // its unload/protection race remains a separate hardening follow-up.
        // Unknown or externally mutated code/payloads fail the fingerprints.
        HMODULE held=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(selected),&held) || held!=selected)
            return Report(r,Stage::Unavailable,Reason::ProviderUnknown);
        r.provider=held;retainedProvider.store(held);r.attempted=true;
        Report(r,Stage::Preparing,Reason::None);
        mfgunlock::architecture::Configure(r.state.family==Family::Rtx30?mfgunlock::Architecture::kAmpere:mfgunlock::Architecture::kTuring);
        mfgunlock::g_enabled.store(true);
        mfgunlock::blackwelltemporal::Plan plan;
        mfgunlock::blackwelltemporal::Result result;
        std::string version;
        std::vector<unsigned char*> sites;
        if(!mfgunlock::provider::FindQualifiedMfgGates(held,sites) ||
           !mfgunlock::blackwelltemporal::Prepare(held,false,mfgunlock::blackwelltemporal::BoundaryArtifactMode::kOff,
               plan,result,version,r.state.family==Family::Rtx30?86:75))
            return Report(r,Stage::Unavailable,Reason::ProviderUnknown);
        if(!mfgunlock::provider::PrepareProvider(held) ||
           !mfgunlock::blackwelltemporal::Commit(plan,r.redirects,result) ||
           !mfgunlock::provider::ApplyMfgComparisons(sites,r.gates)) {
            const bool restored=Rollback(r);
            return Report(r,restored?Stage::Unavailable:Stage::Poisoned,
                restored?Reason::PreparationFailed:Reason::RollbackFailed);
        }
        if(!WrapperCurrent(r)) {
            const bool restored=Rollback(r);
            return Report(r,restored?Stage::Unavailable:Stage::Poisoned,
                restored?Reason::WrapperChanged:Reason::RollbackFailed);
        }
        r.state.backendLoaded=true;
        r.state.ceiling=structuralCeiling;
        if(!r.createObserver.IsCurrent(held,r.createEntry))
            return Report(r,Stage::Poisoned,Reason::OwnerConflict);
        r.state.prepared=true;
        r.validatedEpoch=observedEpoch;
        LOG_INFO("Experimental MFG prepared: provider={} version={} temporal=Blackwell-sm{} optionalQuality=off presentation=UNKNOWN",
            std::filesystem::path(mfgunlock::provider::internal::ModulePath(held)).filename().string(),version,r.state.family==Family::Rtx30?86:75);
        return Report(r,Stage::Ready,Reason::None);
    } catch(...) {
        // An exceptional partial mutation cannot be assumed rolled back.
        r.state.prepared=false;return Report(r,Stage::Poisoned,Reason::RollbackFailed);
    }
}
bool RelaxStreamline(int original,LUID adapter) noexcept {
    auto s=ReadSnapshot();
    return mfgunlock::policy::CanRelaxStreamlineSupport({Requested(),s.renderer.bound,MatchesAdapter(adapter),
        s.prepared?1u:0u,static_cast<uint32_t>(original)},
        s.family==Family::Rtx30?&mfgunlock::architecture::kAmpere:s.family==Family::Rtx20?&mfgunlock::architecture::kTuring:nullptr);
}
bool RelaxRequirements(uint32_t result,uint32_t feature,LUID adapter,uint32_t& flags,uint32_t& minArchitecture) noexcept {
    if(!GameFgScope::Current())return false;
    auto s=ReadSnapshot();
    const auto* profile=s.family==Family::Rtx30?&mfgunlock::architecture::kAmpere:
        s.family==Family::Rtx20?&mfgunlock::architecture::kTuring:nullptr;
    if(!mfgunlock::policy::CanRelaxRequirements({Requested(),MatchesAdapter(adapter),s.prepared?1u:0u,
        result,feature,flags,minArchitecture},profile))return false;
    flags=0;minArchitecture=profile->native_arch;return true;
}
void ObserveCapacity(uint32_t cap) noexcept {if(!Requested())return;auto& r=R();std::lock_guard lock(r.mutex);r.state.rawCapacity=cap>=1&&cap<=5?cap:0;}
void ObserveOptions(uint32_t abi,uint32_t original,uint32_t generated,int result) noexcept {
    if(!Requested())return;auto& r=R();std::lock_guard lock(r.mutex);
    if(r.state.family==Family::None)return;
    if(!r.state.optionsObserved || r.state.abi!=abi || r.state.original!=original ||
       r.state.forwarded!=generated || r.state.result!=result)
        LOG_INFO("Experimental MFG options: ABI={} originalGenerated={} forwardedGenerated={} result={} wrapperCeiling={} delivery=UNKNOWN",
            abi,original,generated,result,r.state.ceiling);
    r.state.optionsObserved=true;r.state.abi=abi;r.state.original=original;r.state.forwarded=generated;r.state.result=result;
}
void ObserveEvaluation(bool success) noexcept {if(!Requested()||!success)return;auto& r=R();std::lock_guard lock(r.mutex);if(r.state.prepared){++r.state.evaluations;Report(r,Stage::ActivityObserved,Reason::None);}}
void BeforeCreate() noexcept {if(!Requested())return;auto& r=R();std::lock_guard lock(r.mutex);r.created=true;r.coverage.Create();if(!r.state.prepared&&r.state.family!=Family::None)Report(r,Stage::Unavailable,Reason::TooLate);}
void DeviceRemoved() noexcept {if(!Requested())return;auto& r=R();std::lock_guard lock(r.mutex);if(!MayPoisonDevice(r.state.family,r.state.backendLoaded))return;r.state.prepared=false;Report(r,Stage::Poisoned,Reason::DeviceRemoved);}
void RefuseRequest(Reason reason) noexcept {if(!Requested())return;auto& r=R();std::lock_guard lock(r.mutex);if(r.state.family!=Family::None)Report(r,Stage::Unavailable,reason);}
void ObserveWrapper(HMODULE wrapper) noexcept {
    if(!Requested() || !wrapper)return;
    auto& r=R();std::lock_guard lock(r.mutex);
    if(r.wrapper && r.wrapper!=wrapper) {Report(r,Stage::Unavailable,Reason::WrapperChanged);return;}
    if(!r.wrapper) {
        HMODULE held=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(wrapper),&held) || held!=wrapper) {
            Report(r,Stage::Unavailable,Reason::WrapperChanged);return;
        }
    }
    r.wrapper=wrapper;
}
bool AllowCreate(HMODULE owner,ID3D12GraphicsCommandList* command) noexcept {
    const auto snapshot=ReadSnapshot();
    if(snapshot.stage==Stage::Poisoned)return false;
    if(!snapshot.backendLoaded)return true; // Pristine, unprepared provider keeps its native behavior.
    if(!AllowFeatureCall() || !snapshot.prepared || !command)return false;
    ID3D12Device* device=nullptr;
    if(FAILED(command->GetDevice(IID_PPV_ARGS(&device))) || !device)return false;
    const auto luid=device->GetAdapterLuid();
    const auto removed=device->GetDeviceRemovedReason();
    device->Release();
    if(FAILED(removed)) {DeviceRemoved();return false;}
    auto& r=R();std::lock_guard lock(r.mutex);
    if(r.state.renderer.luid!=Id(luid))return Report(r,Stage::Unavailable,Reason::DeviceChanged);
    return owner==r.provider && IsAdmitted(r.state.stage,r.state.prepared) &&
        r.validatedEpoch==moduleEpoch.load();
}
void ObserveModuleLoad(HMODULE module,DWORD flags) noexcept {
    if(!Requested() || Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() ||
       !module || (reinterpret_cast<uintptr_t>(module)&3) ||
       (flags & (DONT_RESOLVE_DLL_REFERENCES|LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE)))return;
    // Existing Win32 load-return hook: no provider scans, file I/O or waits.
    // Install only the retained, exact-entry Create observer; prepare elsewhere.
    const auto get=KernelBaseProxy::GetProcAddress_();
    if(!get || !get(module,"NVSDK_NGX_GetGPUArchitecture") ||
       !get(module,"NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl"))return;
    // Only a different executable DLSS-G candidate invalidates readiness.
    // Ordinary DLL loads and repeat notifications cannot interrupt active FG.
    if(!NewProviderNeedsValidation(reinterpret_cast<uintptr_t>(module),reinterpret_cast<uintptr_t>(retainedProvider.load()),true))return;
    moduleEpoch.fetch_add(1);
    auto& r=R();std::unique_lock lock(r.mutex,std::try_to_lock);
    if(!lock.owns_lock() || r.state.backendLoaded || !r.coverage.Fresh(reinterpret_cast<uintptr_t>(module)))return;
    const auto entry=reinterpret_cast<void*>(get(module,"NVSDK_NGX_D3D12_CreateFeature"));
    const bool installed=r.createObserver.Install(module,entry,
        [](HMODULE,ID3D12GraphicsCommandList*) noexcept {BeforeCreate();},AllowCreate);
    r.coverage.Covered(reinterpret_cast<uintptr_t>(module),installed);
    if(installed && r.coverage.CanPrepare(reinterpret_cast<uintptr_t>(module)))r.createEntry=entry;
}
uintptr_t AllowedPhysical(bool fg,LUID adapter) noexcept {
    if(!fg || !Requested())return 0;
    auto& r=R();std::lock_guard lock(r.mutex);
    return IsAdmitted(r.state.stage,r.state.prepared) && r.validatedEpoch==moduleEpoch.load() &&
        r.state.renderer.luid==Id(adapter)?r.physicalGpu:0;
}
ArchitectureScope::ArchitectureScope(bool fg,LUID adapter) noexcept : scope(AllowedPhysical(fg,adapter)) {}
bool ExposeArchitecture(uintptr_t gpu,int result,uint32_t& architecture) noexcept {
    if(!ThreadScope::Current() || ThreadScope::Current()!=gpu || result!=0)return false;
    const auto s=ReadSnapshot();
    if(!s.prepared || architecture!=s.renderer.architecture)return false;
    architecture=0x190;return true;
}
}
