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

namespace Neurotic::Mfg
{
namespace
{
struct Runtime
{
    struct TextReceipt { uintptr_t address = 0; size_t size = 0; uint64_t fingerprint = 0; };
    std::mutex mutex;
    MfgRuntimeSnapshot snapshot;
    MfgPublicationLatch latch;
    MfgPublicationReceipt receipt;
    HMODULE heldWrapper = nullptr;
    HMODULE heldProvider = nullptr;
    void* temporalAllocation = nullptr;
    LUID adapterLuid {};
    bool adapterObserved = false;
    std::atomic<uint64_t> unloadEpoch {1};
    TextReceipt providerText, wrapperText, temporalData;
    std::array<HMODULE, 512> failedDiscoveryModules {};
    DWORD failedDiscoveryBytes = 0;
    MfgRuntimeReason failedDiscoveryReason = MfgRuntimeReason::None;
    uint64_t failedDiscoveryEpoch = 0;
    HMODULE reportedWrapper = nullptr;
    bool providerConflict = false;
};
Runtime runtime;
std::mutex resolutionMutex;
MfgResolutionDiagnostics resolutionDiagnostics;

bool Report(MfgRuntimeReason reason, uint64_t generation,
    MfgRuntimeStatus status = MfgRuntimeStatus::Unavailable) noexcept
{
    if (runtime.snapshot.reason != reason || runtime.snapshot.generation != generation ||
        runtime.snapshot.status != status)
    {
#ifdef NR_MFG_RUNTIME_TEST
        std::printf("Native Ada MFG: generation=%llu reason=%s\n",
            static_cast<unsigned long long>(generation), MfgRuntimeReasonName(reason));
#else
        LOG_INFO("Native Ada MFG: generation={} status={} reason={}", generation,
            static_cast<unsigned>(status), MfgRuntimeReasonName(reason));
#endif
    }
    runtime.snapshot.reason = reason;
    runtime.snapshot.generation = generation;
    runtime.snapshot.status = status;
    return status == MfgRuntimeStatus::Published;
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
        !GetProcAddress(held, "NVSDK_NGX_GetGPUArchitecture") ||
        !ReadModuleVersion(held, version) || !MfgProviderTemporalProfile(version))
    { FreeLibrary(held); held = nullptr; return false; }
    bool matched = false;
    try { matched = FindTemporalDescriptors(reinterpret_cast<const uint8_t*>(held), temporal) &&
        temporal.profile == MfgProviderTemporalProfile(version); }
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

bool PrepareAndPublish(HMODULE selectedWrapper, uint64_t generation, uint32_t callerAbi, HMODULE enteredProvider)
{
    PendingPublication pending;
    if (!HoldModule(selectedWrapper, pending.wrapper)) return Report(MfgRuntimeReason::WaitingWrapper, generation);
    MfgProviderVersion wrapperVersion;
    if (!IsWrapperModule(pending.wrapper) ||
        !ReadModuleVersion(pending.wrapper, wrapperVersion) || !IsMfgWrapperVersionSupported(wrapperVersion))
        return Report(MfgRuntimeReason::WrapperVersion, generation, MfgRuntimeStatus::Refused);
    if (runtime.reportedWrapper != pending.wrapper)
    {
        TraceModule("active wrapper", pending.wrapper, wrapperVersion);
        runtime.reportedWrapper = pending.wrapper;
    }
    MfgProviderVersion providerVersion;
    MfgTemporalDescriptors temporal;
    MfgRuntimeReason reason = MfgRuntimeReason::ProviderMissing;
    if (!(enteredProvider ? QualifyLoadedProvider(enteredProvider, pending.provider, providerVersion, temporal, reason) :
        FindSoleProvider(pending.provider, providerVersion, temporal, reason)))
        return Report(reason, generation);
    if (enteredProvider) TraceModule("FG creation selected provider", pending.provider, providerVersion, temporal.profile, temporal.count);
    const auto providerPe = ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(pending.provider));
    const auto wrapperPe = ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(pending.wrapper));
    const auto providerText = providerPe.ExecutableText();
    const auto wrapperText = wrapperPe.ExecutableText();
    MfgSectionSnapshot providerCopy, wrapperCopy;
    if (!providerCopy.Capture(providerText) || !wrapperCopy.Capture(wrapperText))
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
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
    const auto qualification = QualifyMfgProvider(images, {true, true}, generation);
    MfgPatchPlan plan;
    if (!BuildAdaGatePlan(images, qualification, plan) ||
        plan.count + temporal.count > plan.sites.size())
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
    for (size_t i = 0; i < plan.count; ++i)
    {
        auto& address = plan.sites[i].address;
        const auto providerAddress = providerCopy.OriginalAddress(address);
        const auto wrapperAddress = wrapperCopy.OriginalAddress(address);
        if ((!providerAddress && !wrapperAddress) || (providerAddress && wrapperAddress))
            return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
        address = providerAddress ? providerAddress : wrapperAddress;
    }
    pending.allocation = VirtualAlloc(nullptr, temporal.rebuilt.size(), MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!pending.allocation) return Report(MfgRuntimeReason::Allocation, generation, MfgRuntimeStatus::Refused);
    std::memcpy(pending.allocation, temporal.rebuilt.data(), temporal.rebuilt.size());
    DWORD ignored = 0;
    if (!VirtualProtect(pending.allocation, temporal.rebuilt.size(), PAGE_READONLY, &ignored))
        return Report(MfgRuntimeReason::Allocation, generation, MfgRuntimeStatus::Refused);
    for (size_t i = 0; i < temporal.count; ++i)
    {
        MfgPatchSite& site = plan.sites[plan.count++];
        site.address = temporal.slots[i];
        site.size = 8;
        uint8_t current[8] {};
        Win32MutationOps ops;
        if ((site.address & 7) || !ops.Read(site.address, current, 8))
            return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
        std::memcpy(site.expected.data(), current, 8);
        const uint64_t replacement = reinterpret_cast<uintptr_t>(pending.allocation);
        std::memcpy(site.replacement.data(), &replacement, 8);
    }
    Win32MutationOps ops;
    // Qualification used owned copies. Refuse if live code changed before the
    // transaction; publication independently checks every site's expected bytes.
    if (FingerprintReadable(providerCopy.address, providerCopy.bytes.size()) != qualification.providerFingerprint ||
        FingerprintReadable(wrapperCopy.address, wrapperCopy.bytes.size()) != qualification.wrapperFingerprint)
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
    const auto publication = PublishMfgPatches(ops, plan, generation, runtime.latch);
    runtime.snapshot.generation = generation;
    runtime.snapshot.status = publication.status == MfgPublicationStatus::OwnedPublication ?
        MfgRuntimeStatus::Published :
        publication.status == MfgPublicationStatus::Indeterminate ?
        MfgRuntimeStatus::Indeterminate : MfgRuntimeStatus::Refused;
    if (publication.status == MfgPublicationStatus::OwnedPublication ||
        publication.status == MfgPublicationStatus::Indeterminate)
    {
        runtime.heldWrapper = pending.wrapper;
        runtime.heldProvider = pending.provider;
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
                return Report(MfgRuntimeReason::OwnedBytesChanged, generation, MfgRuntimeStatus::Indeterminate);
            }
        }
    }
    return Report(publication.status == MfgPublicationStatus::OwnedPublication ?
        MfgRuntimeReason::Published : MfgRuntimeReason::Mutation, generation, runtime.snapshot.status);
}
}

bool TryPublishAdaMfg(HMODULE selectedWrapper, uint64_t generation,
    uint32_t callerAbi, LUID expectedAdapter, HMODULE enteredProvider, const LUID* enteredAdapter) noexcept
{
    std::lock_guard lock(runtime.mutex);
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
        if (runtime.heldProvider && enteredProvider != runtime.heldProvider)
        {
            runtime.providerConflict = true;
            return Report(MfgRuntimeReason::ProviderChanged, generation, MfgRuntimeStatus::Stale);
        }
        runtime.adapterLuid = *enteredAdapter;
        runtime.adapterObserved = true;
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
        return true;
    }
    if (runtime.heldWrapper || runtime.heldProvider || runtime.temporalAllocation) return false;
    if (runtime.snapshot.status == MfgRuntimeStatus::Refused &&
        runtime.snapshot.generation == generation) return false;
    try { return PrepareAndPublish(selectedWrapper, generation, callerAbi, enteredProvider); }
    catch (...)
    {
        // Publication and its rollback are noexcept. Exceptions here happened
        // during read-only qualification; PendingPublication releases handles.
        return Report(MfgRuntimeReason::Structure, generation, MfgRuntimeStatus::Refused);
    }
}

bool IsAdaMfgModule(HMODULE module) noexcept
{
    return module && (IsWrapperModule(module) ||
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
    if (runtime.heldProvider && runtime.adapterObserved &&
        (runtime.adapterLuid.HighPart != luid.HighPart || runtime.adapterLuid.LowPart != luid.LowPart))
    {
        runtime.providerConflict = true;
        Report(MfgRuntimeReason::AdapterChanged, runtime.snapshot.generation, MfgRuntimeStatus::Stale);
    }
    runtime.adapterLuid = luid;
    runtime.adapterObserved = true;
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
        MfgProviderVersion version;
        if (!GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") ||
            !GetProcAddress(module, "NVSDK_NGX_GetGPUArchitecture") ||
            !ReadModuleVersion(module, version) || !MfgProviderTemporalProfile(version)) return false;
        MfgTemporalDescriptors temporal;
        if (!FindTemporalDescriptors(reinterpret_cast<const uint8_t*>(module), temporal) ||
            temporal.profile != MfgProviderTemporalProfile(version)) return false;
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
    if (runtime.providerConflict || runtime.snapshot.status != MfgRuntimeStatus::Published ||
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

void InvalidateAdaMfg(uint64_t newGeneration) noexcept
{
    std::lock_guard lock(runtime.mutex);
    if (runtime.snapshot.status == MfgRuntimeStatus::Published &&
        runtime.snapshot.generation != newGeneration)
        runtime.snapshot.status = MfgRuntimeStatus::Stale;
}
}
