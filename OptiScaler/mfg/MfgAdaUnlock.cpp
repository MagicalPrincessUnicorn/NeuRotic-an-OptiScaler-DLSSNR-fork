#include <pch.h>

#include "MfgAdaUnlock.h"
#include "MfgAdaPlan.h"
#include "MfgMappedPe.h"
#include "MfgModulePath.h"
#include "MfgPublication.h"
#include "MfgTemporalDescriptor.h"
#include "MfgResolutionDiagnostics.h"
#ifndef NR_MFG_RUNTIME_TEST
#include "MfgCreateEntry.h"
#include <hooks/Streamline_Hooks.h>
#endif

#include <psapi.h>
#include <winver.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <utility>

namespace Neurotic::Mfg
{
struct MfgWrapperResolutionAccess
{
    using Input = MfgWrapperResolution::PreparedInput;
    static Input& Prepared(MfgWrapperResolution& r) noexcept { return r.prepared; }
    static MfgWrapperRoleLease*& Retired(MfgWrapperResolution& r, size_t i) noexcept { return r.retired[i]; }
};
struct MfgWrapperRoleLease
{
    MfgWrapperResolutionAccess::Input input;
    // Like existing provider/wrapper/temporal receipts, published or
    // indeterminate ownership survives input retirement and CRT teardown.
    bool processRetained = false;
};

namespace
{
struct Runtime
{
    struct TextReceipt { uintptr_t address = 0; size_t size = 0; uint64_t fingerprint = 0; };
    std::mutex mutex;
    MfgRuntimeSnapshot snapshot;
    // Completed invalidation is authoritative even before the first receipt.
    // Protected by mutex; never move the accepted generation backwards.
    uint64_t invalidationGeneration = 0;
    MfgPublicationLatch latch;
    MfgPublicationReceipt receipt;
    HMODULE heldWrapper = nullptr;
    HMODULE heldProvider = nullptr;
    void* temporalAllocation = nullptr;
    LUID adapterLuid {};
    bool adapterObserved = false;
    // Immutable while the retained provider/patch receipt exists. Later device
    // observations must never stand in for the adapter that owns publication.
    LUID ownedAdapterLuid {};
    std::atomic<uint64_t> unloadEpoch {1};
    TextReceipt providerText, wrapperText, temporalData;
    std::array<HMODULE, 512> failedDiscoveryModules {};
    DWORD failedDiscoveryBytes = 0;
    MfgRuntimeReason failedDiscoveryReason = MfgRuntimeReason::None;
    uint64_t failedDiscoveryEpoch = 0;
    HMODULE reportedWrapper = nullptr;
    bool providerConflict = false;
    uint64_t wrapperSelectionEpoch = 0;
    const std::atomic<uint64_t>* wrapperHookClock = nullptr;
    uint8_t wrapperTrackingFailed = 0;
    std::array<void*, 2> untrackedWrapperCallbacks {};
    std::array<MfgWrapperRoleLease*, 2> wrapperInputs {};
    // Raw retained receipts intentionally have no CRT-time destructor.
    std::array<MfgWrapperRoleLease*, 2> ownedWrapperInputs {};
    std::array<std::atomic<HMODULE>, 6> wrapperReleaseHints {};
    std::atomic<HMODULE> observedWrapper {nullptr};
};
Runtime runtime;
std::mutex resolutionMutex;
MfgResolutionDiagnostics resolutionDiagnostics;

using WrapperInput = MfgWrapperResolutionAccess::Input;
void ReleaseWrapperInput(WrapperInput& input) noexcept
{
    if (input.callbackOwner) FreeLibrary(input.callbackOwner);
    if (input.resolverOwner) FreeLibrary(input.resolverOwner);
    if (input.sourcePlugin) FreeLibrary(input.sourcePlugin);
    input = {};
}
void ReleaseWrapperLease(MfgWrapperRoleLease* lease) noexcept
{
    if (!lease || lease->processRetained) return;
    ReleaseWrapperInput(lease->input);
    delete lease;
}
int WrapperCallbackKind(const char* name) noexcept
{
    if (reinterpret_cast<uintptr_t>(name) <= 0xffff) return -1;
    if (std::strcmp(name, "slDLSSGSetOptions") == 0) return 0;
    if (std::strcmp(name, "slDLSSGGetState") == 0) return 1;
    return -1;
}
bool HoldWrapperAddress(const void* address, HMODULE& owner) noexcept
{
    return address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(address), &owner) != 0;
}
bool ExecutableWrapperAddress(const void* address, HMODULE owner) noexcept
{
    MEMORY_BASIC_INFORMATION memory {};
    if (!address || !owner || !VirtualQuery(address, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.AllocationBase != owner) return false;
    const DWORD protection = memory.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
HMODULE CommonWrapperTargetLocked(bool& ambiguous) noexcept
{
    HMODULE target = nullptr;
    ambiguous = false;
    for (const auto* lease : runtime.wrapperInputs)
    {
        if (!lease || !lease->input.ready) continue;
        if (target && target != lease->input.callbackOwner) ambiguous = true;
        target = lease->input.callbackOwner;
    }
    return ambiguous || runtime.wrapperTrackingFailed ? nullptr : target;
}
bool CurrentWrapperRoleLocked(HMODULE module) noexcept
{
    bool ambiguous = false;
    if (!module || !runtime.wrapperHookClock ||
        runtime.wrapperHookClock->load(std::memory_order_acquire) != runtime.wrapperSelectionEpoch ||
        CommonWrapperTargetLocked(ambiguous) != module) return false;
    for (const auto* lease : runtime.wrapperInputs)
        if (lease && !ExecutableWrapperAddress(lease->input.callback, lease->input.callbackOwner)) return false;
    return true;
}
MfgRuntimeReason WrapperRoleReasonLocked() noexcept
{
    if (!runtime.wrapperHookClock || runtime.wrapperHookClock->load(std::memory_order_acquire) != runtime.wrapperSelectionEpoch)
        return MfgRuntimeReason::WrapperRoleUnobserved;
    bool ambiguous = false;
    (void)CommonWrapperTargetLocked(ambiguous);
    if (runtime.wrapperTrackingFailed) return MfgRuntimeReason::WrapperTrackingUnavailable;
    for (const auto* lease : runtime.wrapperInputs)
        if (lease && !ExecutableWrapperAddress(lease->input.callback, lease->input.callbackOwner))
            return MfgRuntimeReason::WrapperTrackingUnavailable;
    return ambiguous ? MfgRuntimeReason::WrapperTargetAmbiguous : MfgRuntimeReason::WrapperRoleUnobserved;
}
bool ObservedWrapperFamily(HMODULE module) noexcept
{
    if (!module) return false;
    for (const auto& hint : runtime.wrapperReleaseHints)
        if (hint.load(std::memory_order_acquire) == module) return true;
    return false;
}
void RefreshWrapperRoleLocked() noexcept
{
    auto& snapshot = runtime.snapshot.wrapperRole;
    snapshot.selectionEpoch = runtime.wrapperSelectionEpoch;
    HMODULE target = nullptr;
    bool ambiguous = false;
    for (size_t i = 0; i < runtime.wrapperInputs.size(); ++i)
    {
        const auto* lease = runtime.wrapperInputs[i];
        auto& observation = snapshot.callbacks[i];
        observation = {};
        if (lease)
        {
            const auto& input = lease->input;
            observation = {true, input.source, reinterpret_cast<uintptr_t>(input.sourcePlugin),
                reinterpret_cast<uintptr_t>(input.resolverOwner), reinterpret_cast<uintptr_t>(input.callbackOwner),
                reinterpret_cast<uintptr_t>(input.callback)};
            if (target && target != input.callbackOwner) ambiguous = true;
            target = input.callbackOwner;
        }
        runtime.wrapperReleaseHints[i * 3].store(lease ? lease->input.sourcePlugin : nullptr, std::memory_order_release);
        runtime.wrapperReleaseHints[i * 3 + 1].store(lease ? lease->input.resolverOwner : nullptr, std::memory_order_release);
        runtime.wrapperReleaseHints[i * 3 + 2].store(lease ? lease->input.callbackOwner : nullptr, std::memory_order_release);
    }
    snapshot.trackingFailedMask = runtime.wrapperTrackingFailed;
    for (size_t i = 0; i < 2; ++i)
        snapshot.untrackedCallbacks[i] = reinterpret_cast<uintptr_t>(runtime.untrackedWrapperCallbacks[i]);
    snapshot.status = runtime.wrapperTrackingFailed ? MfgWrapperRoleStatus::TrackingUnavailable :
        ambiguous ? MfgWrapperRoleStatus::Ambiguous : target ?
        MfgWrapperRoleStatus::Observed : MfgWrapperRoleStatus::Unobserved;
    snapshot.selectedWrapper = ambiguous || runtime.wrapperTrackingFailed ? 0 : reinterpret_cast<uintptr_t>(target);
    runtime.observedWrapper.store(ambiguous || runtime.wrapperTrackingFailed ? nullptr : target, std::memory_order_release);
}
void TraceWrapperRoleLocked() noexcept
{
    try
    {
        const auto& s = runtime.snapshot.wrapperRole;
        const auto& a = s.callbacks[0];
        const auto& b = s.callbacks[1];
#ifdef NR_MFG_RUNTIME_TEST
        std::printf("Native Ada MFG wrapper role: epoch=%llu status=%u target=%llu trackingFailed=%u untrackedOptions=%llu untrackedState=%llu "
            "optionsKnown=%u optionsPlugin=%llu optionsResolver=%llu optionsOwner=%llu optionsCallback=%llu "
            "stateKnown=%u statePlugin=%llu stateResolver=%llu stateOwner=%llu stateCallback=%llu\n",
            static_cast<unsigned long long>(s.selectionEpoch), unsigned(s.status),
            static_cast<unsigned long long>(s.selectedWrapper), unsigned(s.trackingFailedMask),
            static_cast<unsigned long long>(s.untrackedCallbacks[0]), static_cast<unsigned long long>(s.untrackedCallbacks[1]), unsigned(a.observed),
            static_cast<unsigned long long>(a.sourcePlugin), static_cast<unsigned long long>(a.resolverOwner),
            static_cast<unsigned long long>(a.callbackOwner), static_cast<unsigned long long>(a.callback), unsigned(b.observed),
            static_cast<unsigned long long>(b.sourcePlugin), static_cast<unsigned long long>(b.resolverOwner),
            static_cast<unsigned long long>(b.callbackOwner), static_cast<unsigned long long>(b.callback));
#else
        LOG_INFO("Native Ada MFG wrapper role: epoch={} status={} target={} trackingFailed={} untrackedOptions={} untrackedState={} "
            "optionsKnown={} optionsPlugin={} optionsResolver={} optionsOwner={} optionsCallback={} "
            "stateKnown={} statePlugin={} stateResolver={} stateOwner={} stateCallback={}",
            s.selectionEpoch, unsigned(s.status), s.selectedWrapper, s.trackingFailedMask,
            s.untrackedCallbacks[0], s.untrackedCallbacks[1],
            a.observed, a.sourcePlugin, a.resolverOwner, a.callbackOwner, a.callback,
            b.observed, b.sourcePlugin, b.resolverOwner, b.callbackOwner, b.callback);
#endif
    }
    catch (...) {} // Metadata formatting never grants or revokes eligibility.
}


struct PreparationContext
{
    MfgPreparationObservation observation;
    // Captured while the wrapper is retained. Exception reporting must not
    // dereference a module after PendingPublication has released its reference.
    std::wstring selectedWrapperPath;
    MfgProviderVersion selectedWrapperVersion;
};
void TracePreparation(const PreparationContext& context, uint64_t generation) noexcept;

bool Report(MfgRuntimeReason reason, uint64_t generation,
    MfgRuntimeStatus status = MfgRuntimeStatus::Unavailable,
    const PreparationContext* preparation = nullptr) noexcept
{
    if (runtime.snapshot.reason != reason || runtime.snapshot.generation != generation ||
        runtime.snapshot.status != status)
    {
        try
        {
        if (preparation && status != MfgRuntimeStatus::Published)
            TracePreparation(*preparation, generation);
#ifdef NR_MFG_RUNTIME_TEST
        std::printf("Native Ada MFG: generation=%llu reason=%s\n",
            static_cast<unsigned long long>(generation), MfgRuntimeReasonName(reason));
#else
        LOG_INFO("Native Ada MFG: generation={} status={} reason={}", generation,
            static_cast<unsigned>(status), MfgRuntimeReasonName(reason));
#endif
        }
        catch (...) {} // Reporting cannot interrupt upstream resolution or publication.
    }
    runtime.snapshot.preparation = preparation ? preparation->observation : MfgPreparationObservation {};
    runtime.snapshot.reason = reason;
    runtime.snapshot.generation = generation;
    runtime.snapshot.status = status;
    return status == MfgRuntimeStatus::Published;
}

bool ObserveCreateOwnerLocked(HMODULE enteredProvider, const LUID* enteredAdapter,
    uint64_t generation) noexcept
{
    if (!enteredProvider || runtime.latch.indeterminate || runtime.providerConflict) return false;
    // A retired unowned call cannot replace the newer startup's device input.
    // With retained resources, genuine ownership conflicts still revoke
    // eligibility regardless of the caller's generation.
    if (!runtime.heldProvider && generation < runtime.invalidationGeneration) return false;
    if (!enteredAdapter)
    {
        if (!runtime.heldProvider)
            return runtime.snapshot.status == MfgRuntimeStatus::Unavailable ?
                Report(MfgRuntimeReason::WaitingAdapter, generation) : false;
        runtime.providerConflict = true;
        return Report(MfgRuntimeReason::AdapterChanged, generation, MfgRuntimeStatus::Stale);
    }
    const bool adapterChanged = runtime.heldProvider &&
        (runtime.ownedAdapterLuid.HighPart != enteredAdapter->HighPart ||
         runtime.ownedAdapterLuid.LowPart != enteredAdapter->LowPart);
    const bool providerChanged = runtime.heldProvider && runtime.heldProvider != enteredProvider;
    if (adapterChanged || providerChanged)
    {
        // Compare retained ownership BEFORE updating the observed device.
        // Keep all provider/temporal resources alive after revoking eligibility.
        runtime.providerConflict = true;
        runtime.adapterLuid = *enteredAdapter;
        runtime.adapterObserved = true;
        return Report(adapterChanged ? MfgRuntimeReason::AdapterChanged : MfgRuntimeReason::ProviderChanged,
            generation, MfgRuntimeStatus::Stale);
    }
    runtime.adapterLuid = *enteredAdapter;
    runtime.adapterObserved = true;
    return true;
}

// These functions run inside loader/export hooks, sometimes recursively on a
// driver's small worker stack. Even early-return paths reserve fixed local
// arrays in the function prologue, before a C++ re-entry guard can run.
bool ReadModulePath(HMODULE module, std::wstring& path) noexcept
{
    try
    {
        path.resize(32768);
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) { path.clear(); return false; }
        path.resize(length);
        return true;
    }
    catch (...) { path.clear(); return false; }
}

void TraceModule(const char* role, HMODULE module, const MfgProviderVersion& version,
    uint32_t profile = 0, size_t descriptors = 0) noexcept
{
    try
    {
    std::wstring path;
    if (!ReadModulePath(module, path)) return;
#ifdef NR_MFG_RUNTIME_TEST
    std::printf("Native Ada MFG %s: path=%ls version=%u.%u.%u.%u profile=%u descriptors=%zu\n",
        role, path.data(), version.major, version.minor, version.patch, version.build, profile, descriptors);
#else
    LOG_INFO("Native Ada MFG {}: path={} version={}.{}.{}.{} profile={} descriptors={}",
        role, wstring_to_string(path.data()), version.major, version.minor, version.patch,
        version.build, profile, descriptors);
#endif
    }
    catch (...) {} // A diagnostic allocation must not interrupt capability startup.
}

// Best-effort metadata only; diagnostic allocation/format failures cannot
// alter admission, currentness, ownership, or the existing refusal latch.
void TracePreparation(const PreparationContext& context, uint64_t generation) noexcept
{
    try
    {
        const auto& o = context.observation;
        const auto& v = context.selectedWrapperVersion;
#ifdef NR_MFG_RUNTIME_TEST
        std::printf("Native Ada MFG preparation: generation=%llu stage=%s wrapper=%llu provider=%llu abi=%u exception=%u "
            "pathKnown=%u versionKnown=%u temporalKnown=%u profile=%u descriptors=%u "
            "qualificationKnown=%u qualificationStatus=%u gateCountsKnown=%u architectureSites=%u wrapperSites=%u\n",
            static_cast<unsigned long long>(generation), MfgPreparationStageName(o.stage),
            static_cast<unsigned long long>(o.selectedWrapperIdentity),
            static_cast<unsigned long long>(o.selectedProviderIdentity), o.callerAbi, unsigned(o.readOnlyException),
            unsigned(o.wrapperPathKnown), unsigned(o.wrapperVersionKnown), unsigned(o.temporalKnown), o.temporalProfile,
            o.temporalDescriptors, unsigned(o.qualificationKnown), o.qualificationStatus, unsigned(o.gateCountsKnown),
            o.architectureSites, o.wrapperSites);
        std::printf("Native Ada MFG selected wrapper metadata: path=%ls version=%u.%u.%u.%u\n",
            o.wrapperPathKnown ? context.selectedWrapperPath.c_str() : L"<unobserved>", v.major, v.minor, v.patch, v.build);
#else
        LOG_INFO("Native Ada MFG preparation: generation={} stage={} wrapper={} provider={} abi={} exception={} "
            "pathKnown={} versionKnown={} temporalKnown={} profile={} descriptors={} "
            "qualificationKnown={} qualificationStatus={} gateCountsKnown={} architectureSites={} wrapperSites={}",
            generation, MfgPreparationStageName(o.stage), o.selectedWrapperIdentity, o.selectedProviderIdentity,
            o.callerAbi, o.readOnlyException, o.wrapperPathKnown, o.wrapperVersionKnown, o.temporalKnown,
            o.temporalProfile, o.temporalDescriptors, o.qualificationKnown, o.qualificationStatus,
            o.gateCountsKnown, o.architectureSites, o.wrapperSites);
        LOG_INFO("Native Ada MFG selected wrapper metadata: path={} version={}.{}.{}.{}",
            o.wrapperPathKnown ? wstring_to_string(context.selectedWrapperPath) : "<unobserved>",
            v.major, v.minor, v.patch, v.build);
#endif
    }
    catch (...) {}
}

class Win32MutationOps final : public MfgMutationOps
{
public:
    bool Read(uintptr_t address, uint8_t* out, size_t size) noexcept override
    {
        return ReadMfgMemory(reinterpret_cast<const void*>(address), out, size);
    }
    bool MakeWritable(uintptr_t address, size_t size, uintptr_t& prior) noexcept override
    {
        DWORD old = 0;
        const DWORD requested = size == 1 ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, requested, &old)) return false;
        prior = old;
        return true;
    }
    bool Write(uintptr_t address, const uint8_t* bytes, size_t size) noexcept override
    {
        SIZE_T written = 0;
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), bytes,
            size, &written) && written == size;
    }
    bool Flush(uintptr_t address, size_t size) noexcept override
    {
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), size) != 0;
    }
    bool RestoreProtection(uintptr_t address, size_t size, uintptr_t prior) noexcept override
    {
        DWORD ignored = 0;
        return VirtualProtect(reinterpret_cast<LPVOID>(address), size,
            static_cast<DWORD>(prior), &ignored) != 0;
    }
};

bool ReadModuleVersion(HMODULE module, MfgProviderVersion& version) noexcept
{
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    if (!resource) return false;
    const DWORD size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
    if (!bytes || size < sizeof(VS_FIXEDFILEINFO) || size > (1u << 20)) return false;
    for (size_t offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4)
    {
        VS_FIXEDFILEINFO info {};
        if (!ReadMfgMemory(bytes + offset, &info, sizeof info)) return false;
        if (info.dwSignature != 0xFEEF04BD || info.dwStrucVersion != 0x00010000) continue;
        version.major = HIWORD(info.dwFileVersionMS);
        version.minor = LOWORD(info.dwFileVersionMS);
        version.patch = HIWORD(info.dwFileVersionLS);
        version.build = LOWORD(info.dwFileVersionLS);
        return true;
    }
    return false;
}

bool HoldModule(HMODULE module, HMODULE& held) noexcept
{
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(module), &held) != 0;
}

bool ModuleNameIs(HMODULE module, const wchar_t* expected) noexcept
{
    std::wstring path;
    if (!ReadModulePath(module, path)) return false;
    const wchar_t* leaf = path.data();
    for (const wchar_t* cursor = path.data(); *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/') leaf = cursor + 1;
    return _wcsicmp(leaf, expected) == 0;
}

bool IsWrapperModule(HMODULE module) noexcept
{
    std::wstring path;
    return ReadModulePath(module, path) && IsMfgWrapperPath(path.data());
}

bool AdapterMatches(LUID expected) noexcept
{
    return runtime.adapterObserved &&
        runtime.adapterLuid.HighPart == expected.HighPart &&
        runtime.adapterLuid.LowPart == expected.LowPart;
}

uint64_t FingerprintReadable(uintptr_t address, size_t size) noexcept
{
    if (!address || !size || size > (128u << 20) || address > UINTPTR_MAX - size) return 0;
    Win32MutationOps ops;
    constexpr size_t blockSize = 65536;
    std::unique_ptr<uint8_t[]> block(new (std::nothrow) uint8_t[blockSize]);
    if (!block) return 0;
    uint64_t hash = 14695981039346656037ull;
    for (size_t offset = 0; offset < size;)
    {
        const size_t count = (std::min)(blockSize, size - offset);
        if (!ops.Read(address + offset, block.get(), count)) return 0;
        for (size_t i = 0; i < count; ++i)
        {
            hash ^= block[i];
            hash *= 1099511628211ull;
        }
        offset += count;
    }
    return hash;
}

bool OwnedBytesStillPresent() noexcept
{
    if (!runtime.receipt.owned || !runtime.providerText.fingerprint ||
        !runtime.wrapperText.fingerprint || !runtime.temporalData.fingerprint) return false;
    Win32MutationOps ops;
    const auto& plan = runtime.receipt.plan;
    for (size_t i = 0; i < plan.count; ++i)
    {
        uint8_t current[8] {};
        const auto& site = plan.sites[i];
        if (!ops.Read(site.address, current, site.size) ||
            !MfgBytesEqual(current, site.replacement.data(), site.size)) return false;
    }
    return FingerprintReadable(runtime.providerText.address, runtime.providerText.size) ==
            runtime.providerText.fingerprint &&
        FingerprintReadable(runtime.wrapperText.address, runtime.wrapperText.size) ==
            runtime.wrapperText.fingerprint &&
        FingerprintReadable(runtime.temporalData.address, runtime.temporalData.size) ==
            runtime.temporalData.fingerprint;
}

void ReleaseUnpublished(HMODULE& wrapper, HMODULE& provider, void*& allocation) noexcept
{
    if (allocation) { VirtualFree(allocation, 0, MEM_RELEASE); allocation = nullptr; }
    if (provider) { FreeLibrary(provider); provider = nullptr; }
    if (wrapper) { FreeLibrary(wrapper); wrapper = nullptr; }
}

struct PendingPublication
{
    HMODULE wrapper = nullptr;
    HMODULE provider = nullptr;
    void* allocation = nullptr;
    ~PendingPublication() { ReleaseUnpublished(wrapper, provider, allocation); }
};

bool QualifyLoadedProvider(HMODULE module, HMODULE& held, MfgProviderVersion& version,
    MfgTemporalDescriptors& temporal, MfgRuntimeReason& reason)
{
    if (!module || !HoldModule(module, held)) return false;
    if (!GetProcAddress(held, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") ||
        !GetProcAddress(held, "NVSDK_NGX_GetGPUArchitecture"))
    { FreeLibrary(held); held = nullptr; return false; }
    ReadModuleVersion(held, version); // Best-effort diagnostics, never admission.
    bool matched = false;
    try { matched = FindTemporalDescriptors(reinterpret_cast<const uint8_t*>(held), temporal); }
    catch (...) { FreeLibrary(held); held = nullptr; throw; }
    TraceModule(matched ? "provider" : "provider profile mismatch", held,
        version, temporal.profile, temporal.count);
    if (!matched)
    { reason = MfgRuntimeReason::ProviderProfile; FreeLibrary(held); held = nullptr; return false; }
    if (!ObserveAdaLoadedModule(held))
    {
        reason = MfgRuntimeReason::CreationObserverUnavailable;
        FreeLibrary(held); held = nullptr; return false;
    }
    return true;
}

bool FindSoleProvider(HMODULE& chosen, MfgProviderVersion& version,
    MfgTemporalDescriptors& temporal, MfgRuntimeReason& reason)
{
    std::array<HMODULE, 512> modules {};
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
        static_cast<DWORD>(sizeof modules), &needed) || needed > sizeof modules)
    { reason = MfgRuntimeReason::ProviderMissing; return false; }
    if (runtime.failedDiscoveryBytes == needed &&
        runtime.failedDiscoveryReason != MfgRuntimeReason::None &&
        runtime.failedDiscoveryEpoch == runtime.unloadEpoch.load() &&
        std::memcmp(modules.data(), runtime.failedDiscoveryModules.data(), needed) == 0)
    { reason = runtime.failedDiscoveryReason; return false; }
    ++runtime.snapshot.discoveryScans;
    unsigned candidates = 0;
    reason = MfgRuntimeReason::ProviderMissing;
    for (size_t i = 0; i < needed / sizeof(HMODULE); ++i)
    {
        HMODULE module = modules[i];
        // NGX may map the selected snippet under an opaque OTA .bin name.
        // Provider export plus the independently validated temporal program
        // identifies it; a matching file name alone does not qualify it.
        if (!module || !GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl")) continue;
        HMODULE held = nullptr;
        MfgProviderVersion observed;
        MfgTemporalDescriptors found;
        if (!QualifyLoadedProvider(module, held, observed, found, reason)) continue;
        if (++candidates > 1)
        {
            FreeLibrary(held);
            FreeLibrary(chosen);
            chosen = nullptr;
            reason = MfgRuntimeReason::ProviderAmbiguous;
            runtime.failedDiscoveryModules = modules;
            runtime.failedDiscoveryBytes = needed;
            runtime.failedDiscoveryReason = reason;
            runtime.failedDiscoveryEpoch = runtime.unloadEpoch.load();
            return false;
        }
        chosen = held;
        version = observed;
        temporal = std::move(found);
    }
    if (candidates == 1) return true;
    runtime.failedDiscoveryModules = modules;
    runtime.failedDiscoveryBytes = needed;
    runtime.failedDiscoveryReason = reason;
    runtime.failedDiscoveryEpoch = runtime.unloadEpoch.load();
    return false;
}

bool PrepareAndPublish(HMODULE selectedWrapper, uint64_t generation, uint32_t callerAbi, HMODULE enteredProvider,
    PreparationContext& context)
{
    auto& observed = context.observation;
    PendingPublication pending;
    observed.stage = MfgPreparationStage::HoldWrapper;
    if (!HoldModule(selectedWrapper, pending.wrapper)) return Report(MfgRuntimeReason::WaitingWrapper, generation, MfgRuntimeStatus::Unavailable, &context);
    observed.wrapperPathKnown = ReadModulePath(pending.wrapper, context.selectedWrapperPath);
    observed.wrapperVersionKnown = ReadModuleVersion(pending.wrapper, context.selectedWrapperVersion);
    observed.stage = MfgPreparationStage::WrapperRole;
    const auto wrapperVersion = context.selectedWrapperVersion;
    if (!CurrentWrapperRoleLocked(pending.wrapper))
        return Report(WrapperRoleReasonLocked(), generation, MfgRuntimeStatus::Unavailable, &context);
    if (runtime.reportedWrapper != pending.wrapper)
    {
        TraceModule("active wrapper", pending.wrapper, wrapperVersion);
        runtime.reportedWrapper = pending.wrapper;
    }
    MfgProviderVersion providerVersion;
    MfgTemporalDescriptors temporal;
    MfgRuntimeReason reason = MfgRuntimeReason::ProviderMissing;
    observed.stage = MfgPreparationStage::ProviderDiscovery;
    if (!(enteredProvider ? QualifyLoadedProvider(enteredProvider, pending.provider, providerVersion, temporal, reason) :
        FindSoleProvider(pending.provider, providerVersion, temporal, reason)))
        return Report(reason, generation, MfgRuntimeStatus::Unavailable, &context);
    observed.selectedProviderIdentity = reinterpret_cast<uintptr_t>(pending.provider);
    observed.temporalKnown = true;
    observed.temporalProfile = temporal.profile;
    observed.temporalDescriptors = static_cast<uint32_t>(temporal.count);
    if (enteredProvider) TraceModule("FG creation selected provider", pending.provider, providerVersion, temporal.profile, temporal.count);
    const auto providerPe = ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(pending.provider));
    const auto wrapperPe = ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(pending.wrapper));
    const auto providerText = providerPe.ExecutableText();
    const auto wrapperText = wrapperPe.ExecutableText();
    MfgSectionSnapshot providerCopy, wrapperCopy;
    observed.stage = MfgPreparationStage::ProviderTextSnapshot;
    if (!providerCopy.Capture(providerText))
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    observed.stage = MfgPreparationStage::WrapperTextSnapshot;
    if (!wrapperCopy.Capture(wrapperText))
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    MfgProviderImages images;
    images.provider = {providerCopy.bytes.data(), providerCopy.bytes.size(),
        reinterpret_cast<uintptr_t>(pending.provider), generation, true, 1,
        providerVersion, temporal.profile, callerAbi,
        FingerprintMfgImage(providerCopy.bytes.data(), providerCopy.bytes.size())};
    images.wrapper = {wrapperCopy.bytes.data(), wrapperCopy.bytes.size(),
        reinterpret_cast<uintptr_t>(pending.wrapper), generation, true, 1,
        wrapperVersion, 0, callerAbi,
        FingerprintMfgImage(wrapperCopy.bytes.data(), wrapperCopy.bytes.size())};
    images.boundProviderIdentity = reinterpret_cast<uintptr_t>(pending.provider);
    images.boundWrapperIdentity = reinterpret_cast<uintptr_t>(pending.wrapper);
    observed.stage = MfgPreparationStage::ProviderQualification;
    const auto qualification = QualifyMfgProvider(images, {true, true}, generation);
    observed.qualificationKnown = true;
    observed.qualificationStatus = static_cast<uint32_t>(qualification.status);
    // The reducer counts only after both computed fingerprints match the
    // captured inputs. Earlier refusal leaves counts unvisited, not zero.
    observed.gateCountsKnown = qualification.providerFingerprint && qualification.wrapperFingerprint &&
        qualification.providerFingerprint == images.provider.snapshotFingerprint &&
        qualification.wrapperFingerprint == images.wrapper.snapshotFingerprint;
    observed.architectureSites = static_cast<uint32_t>(qualification.architectureSites);
    observed.wrapperSites = static_cast<uint32_t>(qualification.wrapperSites);
    MfgPatchPlan plan;
    if (qualification.status == MfgQualificationStatus::ReadyForExperiment)
        observed.stage = MfgPreparationStage::GatePlan;
    if (!BuildAdaGatePlan(images, qualification, plan))
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    observed.stage = MfgPreparationStage::PatchCapacity;
    if (plan.count + temporal.count > plan.sites.size())
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    observed.stage = MfgPreparationStage::GateAddressMapping;
    for (size_t i = 0; i < plan.count; ++i)
    {
        auto& address = plan.sites[i].address;
        const auto providerAddress = providerCopy.OriginalAddress(address);
        const auto wrapperAddress = wrapperCopy.OriginalAddress(address);
        if ((!providerAddress && !wrapperAddress) || (providerAddress && wrapperAddress))
            return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
        address = providerAddress ? providerAddress : wrapperAddress;
    }
    observed.stage = MfgPreparationStage::TemporalAllocation;
    pending.allocation = VirtualAlloc(nullptr, temporal.rebuilt.size(), MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!pending.allocation) return Report(MfgRuntimeReason::Allocation, generation, MfgRuntimeStatus::Refused, &context);
    std::memcpy(pending.allocation, temporal.rebuilt.data(), temporal.rebuilt.size());
    DWORD ignored = 0;
    observed.stage = MfgPreparationStage::TemporalProtection;
    if (!VirtualProtect(pending.allocation, temporal.rebuilt.size(), PAGE_READONLY, &ignored))
        return Report(MfgRuntimeReason::Allocation, generation, MfgRuntimeStatus::Refused, &context);
    observed.stage = MfgPreparationStage::TemporalSlotRead;
    for (size_t i = 0; i < temporal.count; ++i)
    {
        MfgPatchSite& site = plan.sites[plan.count++];
        site.address = temporal.slots[i];
        site.size = 8;
        uint8_t current[8] {};
        Win32MutationOps ops;
        if ((site.address & 7) || !ops.Read(site.address, current, 8))
            return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
        std::memcpy(site.expected.data(), current, 8);
        const uint64_t replacement = reinterpret_cast<uintptr_t>(pending.allocation);
        std::memcpy(site.replacement.data(), &replacement, 8);
    }
    Win32MutationOps ops;
    // Qualification used owned copies. Refuse if live code changed before the
    // transaction; publication independently checks every site's expected bytes.
    observed.stage = MfgPreparationStage::ProviderCodeCurrent;
    if (FingerprintReadable(providerCopy.address, providerCopy.bytes.size()) != qualification.providerFingerprint)
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    observed.stage = MfgPreparationStage::WrapperCodeCurrent;
    if (FingerprintReadable(wrapperCopy.address, wrapperCopy.bytes.size()) != qualification.wrapperFingerprint)
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    if (!CurrentWrapperRoleLocked(pending.wrapper) || runtime.wrapperSelectionEpoch != generation)
        return Report(WrapperRoleReasonLocked(), generation, MfgRuntimeStatus::Unavailable, &context);
    observed.stage = MfgPreparationStage::Publication;
    const auto publication = PublishMfgPatches(ops, plan, generation, runtime.latch);
    runtime.snapshot.generation = generation;
    runtime.snapshot.status = publication.status == MfgPublicationStatus::OwnedPublication ?
        MfgRuntimeStatus::Published :
        publication.status == MfgPublicationStatus::Indeterminate ?
        MfgRuntimeStatus::Indeterminate : MfgRuntimeStatus::Refused;
    if (publication.status == MfgPublicationStatus::OwnedPublication ||
        publication.status == MfgPublicationStatus::Indeterminate)
    {
        for (size_t i = 0; i < runtime.wrapperInputs.size(); ++i)
        {
            runtime.ownedWrapperInputs[i] = runtime.wrapperInputs[i];
            if (runtime.ownedWrapperInputs[i]) runtime.ownedWrapperInputs[i]->processRetained = true;
        }
        runtime.heldWrapper = pending.wrapper;
        runtime.heldProvider = pending.provider;
        runtime.ownedAdapterLuid = runtime.adapterLuid;
        runtime.temporalAllocation = pending.allocation;
        pending.wrapper = nullptr;
        pending.provider = nullptr;
        pending.allocation = nullptr;
        runtime.receipt = publication.receipt;
        runtime.snapshot.generation = generation;
        runtime.snapshot.temporalProfile = temporal.profile;
        runtime.snapshot.patchSites = static_cast<uint32_t>(plan.count);
        runtime.snapshot.maxGenerated = qualification.maxGenerated;
        runtime.snapshot.providerFingerprint = qualification.providerFingerprint;
        runtime.snapshot.wrapperFingerprint = qualification.wrapperFingerprint;
        if (publication.status == MfgPublicationStatus::OwnedPublication)
        {
            runtime.providerText = {reinterpret_cast<uintptr_t>(providerText.bytes), providerText.size,
                FingerprintReadable(reinterpret_cast<uintptr_t>(providerText.bytes), providerText.size)};
            runtime.wrapperText = {reinterpret_cast<uintptr_t>(wrapperText.bytes), wrapperText.size,
                FingerprintReadable(reinterpret_cast<uintptr_t>(wrapperText.bytes), wrapperText.size)};
            runtime.temporalData = {reinterpret_cast<uintptr_t>(runtime.temporalAllocation),
                temporal.rebuilt.size(), FingerprintReadable(
                    reinterpret_cast<uintptr_t>(runtime.temporalAllocation), temporal.rebuilt.size())};
            if (!runtime.providerText.fingerprint || !runtime.wrapperText.fingerprint ||
                !runtime.temporalData.fingerprint)
            {
                runtime.snapshot.status = MfgRuntimeStatus::Indeterminate;
                runtime.latch.indeterminate = true;
                return Report(MfgRuntimeReason::OwnedBytesChanged, generation, MfgRuntimeStatus::Indeterminate, &context);
            }
        }
    }
    return Report(publication.status == MfgPublicationStatus::OwnedPublication ?
        MfgRuntimeReason::Published : MfgRuntimeReason::Mutation, generation, runtime.snapshot.status, &context);
}
}

MfgWrapperResolution::MfgWrapperResolution(MfgWrapperResolution&& other) noexcept
    : prepared(std::exchange(other.prepared, PreparedInput {})), function(other.function), result(other.result)
{
    for (size_t i = 0; i < 2; ++i) retired[i] = std::exchange(other.retired[i], nullptr);
    retiredGeneration = std::exchange(other.retiredGeneration, 0);
    retiredTracking = other.retiredTracking; retiredUntracked = other.retiredUntracked;
}
MfgWrapperResolution& MfgWrapperResolution::operator=(MfgWrapperResolution&& other) noexcept
{
    if (this == &other) return *this;
    ReleaseWrapperInput(prepared);
    for (auto* lease : retired) ReleaseWrapperLease(lease);
    prepared = std::exchange(other.prepared, PreparedInput {});
    for (size_t i = 0; i < 2; ++i) retired[i] = std::exchange(other.retired[i], nullptr);
    retiredGeneration = std::exchange(other.retiredGeneration, 0);
    retiredTracking = other.retiredTracking; retiredUntracked = other.retiredUntracked;
    function = other.function; result = other.result;
    return *this;
}
MfgWrapperResolution::~MfgWrapperResolution()
{
    ReleaseWrapperInput(prepared);
    for (auto* lease : retired) ReleaseWrapperLease(lease);
}

MfgWrapperResolution MfgWrapperResolution::FromPluginReturn(const char* name, void* function,
    uint64_t generation, const void* ownOptionsHook, const void* ownStateHook)
{
    MfgWrapperResolution observation;
    observation.function = function;
    auto& input = observation.prepared;
    input.epoch = generation;
    input.source = MfgWrapperResolutionSource::Plugin;
    const int kind = WrapperCallbackKind(name);
    if (kind < 0 || !function || function == ownOptionsHook || function == ownStateHook) return observation;
    input.kind = static_cast<uint8_t>(kind); input.callback = function; input.attempted = true;
    // The private producer calls this AFTER its original lookup returned. No
    // upstream resolver, copied trampoline or callback body is invoked here.
    try { input.ready = HoldWrapperAddress(function, input.callbackOwner) &&
        ExecutableWrapperAddress(function, input.callbackOwner); }
    catch (...) { input.ready = false; }
    return observation;
}
MfgWrapperResolution MfgWrapperResolution::FromFeatureReturn(sl::Result result, sl::Feature feature,
    const char* name, void* function, uint64_t generation, const void* ownOptionsHook, const void* ownStateHook)
{
    if (result != sl::Result::eOk || feature != sl::kFeatureDLSS_G) return {};
    auto observation = FromPluginReturn(name, function, generation, ownOptionsHook, ownStateHook);
    observation.prepared.source = MfgWrapperResolutionSource::Feature;
    observation.result = result;
    return observation;
}

MfgWrapperBinding MfgWrapperResolution::Commit(std::atomic<uint64_t>& clock, uint64_t ownAdvance,
    MfgWrapperResolution* invalidated) noexcept
{
    auto& input = prepared;
    if (!input.attempted || input.kind >= 2 || !input.epoch) return {};
    const auto captured = input.epoch;
    // ownAdvance is the actual fetch_add result in the EXISTING cache-change
    // branch, not a source/quiescence promise. Any other drift rejects metadata.
    const auto expected = ownAdvance ? ownAdvance : captured;
    if (ownAdvance && ownAdvance != captured + 1) return {MfgWrapperBindingStatus::Retired, nullptr, 0};
    std::lock_guard lock(runtime.mutex);
    if (clock.load(std::memory_order_acquire) != expected || expected < runtime.invalidationGeneration)
        return {MfgWrapperBindingStatus::Retired, nullptr, 0};
    const bool restoreOwn = ownAdvance && invalidated && invalidated->retiredGeneration == captured &&
        runtime.wrapperSelectionEpoch == expected && !runtime.wrapperInputs[0] && !runtime.wrapperInputs[1];
    const bool sameContext = runtime.wrapperSelectionEpoch == captured || restoreOwn;
    auto currentInput = [&](size_t i) -> MfgWrapperRoleLease* {
        return !sameContext ? nullptr : restoreOwn ? invalidated->retired[i] : runtime.wrapperInputs[i]; };
    const auto oldFailed = restoreOwn ? invalidated->retiredTracking : sameContext ? runtime.wrapperTrackingFailed : uint8_t{0};
    HMODULE oldTarget = nullptr; bool oldAmbiguous = false;
    for (size_t i = 0; i < 2; ++i) if (auto* previous = currentInput(i))
    {
        if (oldTarget && oldTarget != previous->input.callbackOwner) oldAmbiguous = true;
        oldTarget = previous->input.callbackOwner;
    }
    if (oldAmbiguous || oldFailed) oldTarget = nullptr;
    const auto bit = static_cast<uint8_t>(1u << input.kind);
    const auto* prior = currentInput(input.kind);
    const bool repeat = prior && prior->input.callback == input.callback &&
        ExecutableWrapperAddress(prior->input.callback, prior->input.callbackOwner);
    // Allocate before advancing a metadata generation; failure is tracked as an
    // actual unavailable input, never a claim that the old different callback is ready.
    auto* lease = !repeat && input.ready && input.callbackOwner ? new (std::nothrow) MfgWrapperRoleLease : nullptr;
    const bool tracked = repeat || lease;
    const auto nextFailed = tracked ? static_cast<uint8_t>(oldFailed & ~bit) : static_cast<uint8_t>(oldFailed | bit);
    HMODULE proposed = nullptr; bool ambiguous = false;
    for (size_t i = 0; i < 2; ++i)
    {
        const auto* current = currentInput(i);
        HMODULE owner = current ? current->input.callbackOwner : nullptr;
        if (i == input.kind && tracked) owner = repeat ? prior->input.callbackOwner : input.callbackOwner;
        if (!owner) continue;
        if (proposed && proposed != owner) ambiguous = true;
        proposed = owner;
    }
    const auto target = ambiguous || nextFailed ? nullptr : proposed;
    const bool changed = !sameContext || target != oldTarget || ambiguous != oldAmbiguous || nextFailed != oldFailed ||
        (!tracked && runtime.untrackedWrapperCallbacks[input.kind] != input.callback);
    uint64_t generation = expected;
    if (changed && !ownAdvance)
    {
        auto current = expected;
        if (!clock.compare_exchange_strong(current, expected + 1, std::memory_order_acq_rel))
        { delete lease; return {MfgWrapperBindingStatus::Retired, nullptr, 0}; }
        generation = expected + 1;
    }
    if (!sameContext)
    {
        for (size_t i = 0; i < 2; ++i) retired[i] = std::exchange(runtime.wrapperInputs[i], nullptr);
        runtime.wrapperTrackingFailed = 0; runtime.untrackedWrapperCallbacks = {};
    }
    else if (restoreOwn)
    {
        for (size_t i = 0; i < 2; ++i) runtime.wrapperInputs[i] = std::exchange(invalidated->retired[i], nullptr);
        runtime.untrackedWrapperCallbacks = invalidated->retiredUntracked;
        invalidated->retiredGeneration = 0;
    }
    runtime.wrapperSelectionEpoch = generation;
    runtime.wrapperHookClock = &clock; // Process-static metadata clock, never a callable lease.
    runtime.wrapperTrackingFailed = nextFailed;
    runtime.untrackedWrapperCallbacks[input.kind] = tracked ? nullptr : input.callback;
    if (lease)
    {
        if (sameContext) retired[0] = runtime.wrapperInputs[input.kind];
        lease->input = std::exchange(input, WrapperInput {});
        runtime.wrapperInputs[lease->input.kind] = lease;
        runtime.unloadEpoch.fetch_add(1);
    }
    else input.attempted = false; // A token is consumed once, including repeated/failed tracking.
    RefreshWrapperRoleLocked();
    if (changed) TraceWrapperRoleLocked();
    if (nextFailed)
    {
        if (!runtime.latch.indeterminate && !runtime.providerConflict)
            Report(MfgRuntimeReason::WrapperTrackingUnavailable, generation,
                runtime.heldWrapper ? MfgRuntimeStatus::Stale : MfgRuntimeStatus::Unavailable);
        return {changed ? MfgWrapperBindingStatus::TrackingUnavailable : MfgWrapperBindingStatus::TrackingUnavailableUnchanged, nullptr, generation};
    }
    if (ambiguous)
    {
        if (!runtime.latch.indeterminate && !runtime.providerConflict)
            Report(MfgRuntimeReason::WrapperTargetAmbiguous, generation,
                runtime.heldWrapper ? MfgRuntimeStatus::Stale : MfgRuntimeStatus::Unavailable);
        return {changed ? MfgWrapperBindingStatus::Ambiguous : MfgWrapperBindingStatus::AmbiguousUnchanged, nullptr, generation};
    }
    if (runtime.heldWrapper && runtime.heldWrapper != target && !runtime.latch.indeterminate && !runtime.providerConflict)
        Report(MfgRuntimeReason::WrapperChanged, generation, MfgRuntimeStatus::Stale);
    return {changed || ownAdvance ? MfgWrapperBindingStatus::Changed : MfgWrapperBindingStatus::Unchanged, target, generation};
}
bool TryPublishAdaMfg(HMODULE selectedWrapper, uint64_t generation,
    uint32_t callerAbi, LUID expectedAdapter, HMODULE enteredProvider, const LUID* enteredAdapter) noexcept
{
    std::lock_guard lock(runtime.mutex);
    // This check must be inside the same lock as initial preparation/mutation.
    // Ignore retired requests without poisoning a newer owned publication.
    if (generation < runtime.invalidationGeneration) return false;
    if (runtime.latch.indeterminate) return false;
    if (runtime.providerConflict) return false;
    // Actual invocation evidence is immutable call-local input, consumed under
    // the same lock as ownership. Do not mix adapter observations from separate
    // concurrent command lists, or leave a published receipt after a conflict.
    if (enteredProvider)
    {
        if (!enteredAdapter || enteredAdapter->HighPart != expectedAdapter.HighPart ||
            enteredAdapter->LowPart != expectedAdapter.LowPart)
        {
            runtime.providerConflict = true;
            return Report(MfgRuntimeReason::AdapterChanged, generation, MfgRuntimeStatus::Stale);
        }
        if (!ObserveCreateOwnerLocked(enteredProvider, enteredAdapter, generation)) return false;
    }
    // An invalid request cannot erase the receipt for already owned patches.
    if (!selectedWrapper || !generation) return runtime.heldWrapper ? false :
        Report(MfgRuntimeReason::WaitingWrapper, generation);
    if (callerAbi < 1 || callerAbi > 5) return runtime.heldWrapper ? false :
        Report(MfgRuntimeReason::InvalidAbi, generation);
    if (!AdapterMatches(expectedAdapter)) return runtime.heldWrapper ? false :
        Report(MfgRuntimeReason::WaitingAdapter, generation);
    if (runtime.heldWrapper && runtime.heldWrapper != selectedWrapper)
    {
        return Report(MfgRuntimeReason::WrapperChanged, generation, MfgRuntimeStatus::Stale);
    }
    if ((runtime.heldWrapper || runtime.heldProvider) && !CurrentWrapperRoleLocked(selectedWrapper))
        return Report(WrapperRoleReasonLocked(), generation, MfgRuntimeStatus::Stale);
    if (runtime.snapshot.status == MfgRuntimeStatus::Published)
    {
        if (runtime.snapshot.generation != generation || runtime.heldWrapper != selectedWrapper)
        { runtime.snapshot.status = MfgRuntimeStatus::Stale; return false; }
        if (!OwnedBytesStillPresent())
        {
            runtime.latch.indeterminate = true;
            return Report(MfgRuntimeReason::OwnedBytesChanged, generation, MfgRuntimeStatus::Indeterminate);
        }
        return true;
    }
    if (runtime.snapshot.status == MfgRuntimeStatus::Stale &&
        runtime.heldWrapper == selectedWrapper && runtime.heldProvider)
    {
        if (!OwnedBytesStillPresent())
        {
            runtime.latch.indeterminate = true;
            return Report(MfgRuntimeReason::OwnedBytesChanged, generation, MfgRuntimeStatus::Indeterminate);
        }
        runtime.receipt.plan.generation = generation;
        runtime.snapshot.generation = generation;
        runtime.snapshot.status = MfgRuntimeStatus::Published;
        return Report(MfgRuntimeReason::Published, generation, MfgRuntimeStatus::Published);
    }
    if (runtime.heldWrapper || runtime.heldProvider || runtime.temporalAllocation) return false;
    if (runtime.snapshot.status == MfgRuntimeStatus::Refused &&
        runtime.snapshot.generation == generation) return false;
    PreparationContext context;
    context.observation.selectedWrapperIdentity = reinterpret_cast<uintptr_t>(selectedWrapper);
    context.observation.selectedProviderIdentity = reinterpret_cast<uintptr_t>(enteredProvider);
    context.observation.callerAbi = callerAbi;
    try { return PrepareAndPublish(selectedWrapper, generation, callerAbi, enteredProvider, context); }
    catch (...)
    {
        // Publication and its rollback are noexcept. Exceptions here happened
        // during read-only qualification; PendingPublication releases handles.
        context.observation.readOnlyException = true;
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused, &context);
    }
}

bool IsAdaMfgWrapperModule(HMODULE module) noexcept
{
    return module && runtime.observedWrapper.load(std::memory_order_acquire) == module;
}

bool IsAdaMfgModule(HMODULE module) noexcept
{
    return module && (ObservedWrapperFamily(module) || IsWrapperModule(module) ||
        ModuleNameIs(module, L"nvngx_dlssg.dll") ||
        GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") != nullptr);
}

void NoteAdaModuleRelease(HMODULE module) noexcept
{
    if (!module) return;
    std::array<HMODULE, 512> modules {};
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
        static_cast<DWORD>(sizeof modules), &needed) || needed > sizeof modules)
    {
        runtime.unloadEpoch.fetch_add(1);
        return;
    }
    for (size_t i = 0; i < needed / sizeof(HMODULE); ++i)
        if (modules[i] == module) return;
    runtime.unloadEpoch.fetch_add(1);
}

void ObserveAdaD3D12Adapter(LUID luid) noexcept
{
    std::lock_guard lock(runtime.mutex);
    if (runtime.heldProvider &&
        (runtime.ownedAdapterLuid.HighPart != luid.HighPart || runtime.ownedAdapterLuid.LowPart != luid.LowPart))
    {
        runtime.providerConflict = true;
        Report(MfgRuntimeReason::AdapterChanged, runtime.snapshot.generation, MfgRuntimeStatus::Stale);
    }
    runtime.adapterLuid = luid;
    runtime.adapterObserved = true;
}

bool ObserveAdaCreateOwner(HMODULE enteredProvider, const LUID* enteredAdapter,
    uint64_t generation) noexcept
{
    std::lock_guard lock(runtime.mutex);
    return ObserveCreateOwnerLocked(enteredProvider, enteredAdapter, generation);
}

bool ObservedAdaD3D12Adapter(LUID& luid) noexcept
{
    std::lock_guard lock(runtime.mutex);
    if (!runtime.adapterObserved) return false;
    luid = runtime.adapterLuid;
    return true;
}

MfgRuntimeSnapshot AdaMfgSnapshot() noexcept
{
    std::lock_guard lock(runtime.mutex);
    return runtime.snapshot;
}

bool ObserveAdaLoadedModule(HMODULE module, DWORD loadFlags) noexcept
{
#ifndef NR_MFG_RUNTIME_TEST
    const DWORD nonExecutable = DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE |
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (!module || (reinterpret_cast<uintptr_t>(module) & 3) || (loadFlags & nonExecutable) ||
        !Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default()) return false;
    static thread_local bool observingLoad = false;
    if (observingLoad) return false;
    struct Guard { bool& flag; Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(observingLoad);
    try
    {
        PendingPublication observation;
        if (!HoldModule(module, observation.provider)) return false;
        if (!GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") ||
            !GetProcAddress(module, "NVSDK_NGX_GetGPUArchitecture")) return false;
        MfgTemporalDescriptors temporal;
        if (!FindTemporalDescriptors(reinterpret_cast<const uint8_t*>(module), temporal)) return false;
        auto* entry = reinterpret_cast<void*>(GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature"));
        static RetainedCreateEntries hooks;
        return hooks.Install(module, entry, [](HMODULE owner, ID3D12GraphicsCommandList* command) noexcept {
            try { StreamlineHooks::prepareNativeMfgCapabilities(owner, command); }
            catch (...) {} // Always forward the game's original Create call.
        });
    }
    catch (...) {} // Observers cannot interrupt native loading or export resolution.
    return false;
#else
    (void)module; (void)loadFlags;
    // The inert mapped-image fixture deliberately never enters vendor code;
    // actual cached-pointer interception is tested with the synthetic DLL.
    return true;
#endif
}

void ObserveAdaExportResolution(HMODULE queried, const char* name,
    const void* resolved, const void* caller) noexcept
{
    struct LastErrorGuard
    {
        DWORD value = GetLastError();
        ~LastErrorGuard() { SetLastError(value); }
    } lastError;
    const auto kind = MfgResolutionExport(name);
    if (!kind || !queried || !resolved || !caller) return;
    static thread_local bool observing = false;
    if (observing) return;
    struct Guard { bool& value; Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } } guard(observing);
    HMODULE callerOwner = nullptr, entryOwner = nullptr;
    constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(caller), &callerOwner) ||
        !GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(resolved), &entryOwner)) return;
    // Exclude our own structural scans; record only driver NGX resolution.
    if (!ModuleNameIs(callerOwner, L"_nvngx.dll") && !ModuleNameIs(callerOwner, L"nvngx.dll")) return;
    // Entry interception covers the raw pointer returned to NGX, including
    // subsequent cached-pointer calls. A forwarded export is not a provider.
    if (kind == 3 && queried == entryOwner) ObserveAdaLoadedModule(entryOwner);
    try
    {
        std::lock_guard lock(resolutionMutex);
        if (!resolutionDiagnostics.Record({reinterpret_cast<uintptr_t>(queried),
            reinterpret_cast<uintptr_t>(resolved), reinterpret_cast<uintptr_t>(caller), kind})) return;
        std::wstring queryPath, entryPath, callerPath;
        if (!ReadModulePath(queried, queryPath) || !ReadModulePath(entryOwner, entryPath) ||
            !ReadModulePath(callerOwner, callerPath)) return;
#ifdef NR_MFG_RUNTIME_TEST
        std::printf("Native Ada MFG resolution: export=%s queried=%ls entry=%ls caller=%ls; invocation unverified\n",
            name, queryPath.data(), entryPath.data(), callerPath.data());
#else
        LOG_INFO("Native Ada MFG resolution: export={} queried={} entry={} caller={} caller_address={} entry_address={}; invocation unverified",
            name, wstring_to_string(queryPath.data()), wstring_to_string(entryPath.data()),
            wstring_to_string(callerPath.data()), reinterpret_cast<uintptr_t>(caller),
            reinterpret_cast<uintptr_t>(resolved));
#endif
    }
    catch (...) {} // Diagnostics cannot interrupt export resolution.
}

bool HasOwnedAdaMfg(HMODULE selectedWrapper, uint64_t generation) noexcept
{
    std::lock_guard lock(runtime.mutex);
    if (!CurrentWrapperRoleLocked(selectedWrapper) || runtime.providerConflict ||
        runtime.snapshot.status != MfgRuntimeStatus::Published ||
        runtime.snapshot.generation != generation || !runtime.receipt.owned ||
        runtime.heldWrapper != selectedWrapper || !runtime.heldProvider) return false;
    Win32MutationOps ops;
    for (size_t i = 0; i < runtime.receipt.plan.count; ++i)
    {
        const auto& site = runtime.receipt.plan.sites[i];
        uint8_t current[8] {};
        if (!ops.Read(site.address, current, site.size) ||
            !MfgBytesEqual(current, site.replacement.data(), site.size))
        {
            runtime.snapshot.status = MfgRuntimeStatus::Indeterminate;
            runtime.latch.indeterminate = true;
            return false;
        }
    }
    return true;
}

MfgWrapperResolution InvalidateAdaMfg(uint64_t newGeneration) noexcept
{
    MfgWrapperResolution retired;
    {
        std::lock_guard lock(runtime.mutex);
        if (newGeneration > runtime.invalidationGeneration)
            runtime.invalidationGeneration = newGeneration;
        if (newGeneration > runtime.wrapperSelectionEpoch)
        {
            retired.retiredGeneration = runtime.wrapperSelectionEpoch;
            retired.retiredTracking = runtime.wrapperTrackingFailed;
            retired.retiredUntracked = runtime.untrackedWrapperCallbacks;
            for (size_t i = 0; i < 2; ++i)
                MfgWrapperResolutionAccess::Retired(retired, i) = std::exchange(runtime.wrapperInputs[i], nullptr);
            runtime.wrapperSelectionEpoch = newGeneration;
            runtime.wrapperTrackingFailed = 0; runtime.untrackedWrapperCallbacks = {};
            RefreshWrapperRoleLocked();
            runtime.snapshot.wrapperRole.status = MfgWrapperRoleStatus::Retired;
            runtime.unloadEpoch.fetch_add(1);
        }
        if (runtime.snapshot.status == MfgRuntimeStatus::Published &&
            runtime.snapshot.generation < runtime.invalidationGeneration)
            runtime.snapshot.status = MfgRuntimeStatus::Stale;
    }
    // Free only unpublished displaced observation owners, after runtime lock.
    // A caller holding another lock retains this value until outside that scope.
    return retired;
}
}
