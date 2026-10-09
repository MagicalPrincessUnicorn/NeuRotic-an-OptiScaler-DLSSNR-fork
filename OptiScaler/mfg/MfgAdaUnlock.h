#pragma once

#include <windows.h>

#include <cstdint>

namespace Neurotic::Mfg
{
enum class MfgRuntimeStatus : uint8_t
{
    Unavailable, Refused, Indeterminate, Published, Stale
};

enum class MfgRuntimeReason : uint8_t
{
    None, WaitingWrapper, WaitingAdapter, InvalidAbi, ProviderMissing,
    ProviderAmbiguous, WrapperVersion, ProviderProfile, Structure,
    Allocation, Mutation, OwnedBytesChanged, WrapperChanged, Published, ProviderChanged, AdapterChanged,
    CreationObserverUnavailable
};

constexpr const char* MfgRuntimeReasonName(MfgRuntimeReason reason) noexcept
{
    switch (reason)
    {
    case MfgRuntimeReason::WaitingWrapper: return "waiting for active wrapper";
    case MfgRuntimeReason::WaitingAdapter: return "waiting for matching D3D12 device";
    case MfgRuntimeReason::InvalidAbi: return "unsupported options layout";
    case MfgRuntimeReason::ProviderMissing: return "loaded DLSS-G provider not found";
    case MfgRuntimeReason::ProviderAmbiguous: return "multiple loaded DLSS-G providers";
    case MfgRuntimeReason::ProviderChanged: return "FG creation changed provider; restart required";
    case MfgRuntimeReason::AdapterChanged: return "FG creation adapter changed or unverified; restart required";
    case MfgRuntimeReason::CreationObserverUnavailable: return "provider FG creation entry unavailable or already modified";
    case MfgRuntimeReason::WrapperVersion: return "wrapper version unsupported";
    case MfgRuntimeReason::ProviderProfile: return "provider temporal profile unmatched";
    case MfgRuntimeReason::Structure: return "provider or wrapper gate structure unmatched";
    case MfgRuntimeReason::Allocation: return "temporal allocation failed";
    case MfgRuntimeReason::Mutation: return "patch transaction failed";
    case MfgRuntimeReason::OwnedBytesChanged: return "owned patch bytes changed";
    case MfgRuntimeReason::WrapperChanged: return "active wrapper changed";
    case MfgRuntimeReason::Published: return "Ada gates, temporal path and wrapper published";
    default: return "not attempted";
    }
}

struct MfgRuntimeSnapshot
{
    MfgRuntimeStatus status = MfgRuntimeStatus::Unavailable;
    uint64_t generation = 0;
    uint32_t temporalProfile = 0;
    uint32_t patchSites = 0;
    uint32_t maxGenerated = 0;
    uint64_t providerFingerprint = 0;
    uint64_t wrapperFingerprint = 0;
    MfgRuntimeReason reason = MfgRuntimeReason::None;
    uint64_t discoveryScans = 0;
};

// Called before native NGX capability queries and from DLSS-G SetOptions,
// never Present. A concrete provider is supplied only by an actual Feature 11
// entry thunk; otherwise discovery must find a sole qualified provider.
// Returns true only for a fully owned publication receipt.
bool TryPublishAdaMfg(HMODULE selectedWrapper, uint64_t generation,
    uint32_t callerAbi, LUID expectedAdapter, HMODULE enteredProvider = nullptr,
    const LUID* enteredAdapter = nullptr) noexcept;
MfgRuntimeSnapshot AdaMfgSnapshot() noexcept;
bool HasOwnedAdaMfg(HMODULE selectedWrapper, uint64_t generation) noexcept;
void InvalidateAdaMfg(uint64_t newGeneration) noexcept;
// Observations from the existing Win32 API and Streamline device hooks.
bool IsAdaMfgModule(HMODULE module) noexcept;
void NoteAdaModuleRelease(HMODULE module) noexcept;
void ObserveAdaD3D12Adapter(LUID luid) noexcept;
// Install retained creation observers on a qualified mapped image. This never
// authorizes MFG gate/temporal publication merely because a DLL was loaded.
bool ObserveAdaLoadedModule(HMODULE module, DWORD loadFlags = 0) noexcept;
// Read-only, bounded diagnostics. Never used as publication authority.
void ObserveAdaExportResolution(HMODULE queried, const char* name,
    const void* resolved, const void* caller) noexcept;
}
