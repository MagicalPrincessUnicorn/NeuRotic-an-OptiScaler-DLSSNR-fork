#pragma once

#include <windows.h>

#include <cstdint>
#include <array>
#include <atomic>

class StreamlineHooks;
#include <sl_core_types.h>
#include <sl_result.h>

struct ID3D12GraphicsCommandList;

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
    CreationObserverUnavailable, WrapperRoleUnobserved, WrapperTargetAmbiguous, WrapperTrackingUnavailable
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
    case MfgRuntimeReason::WrapperRoleUnobserved: return "waiting for DLSS-G callback";
    case MfgRuntimeReason::WrapperTargetAmbiguous: return "DLSS-G callback targets differ";
    case MfgRuntimeReason::WrapperTrackingUnavailable: return "DLSS-G callback tracking unavailable";
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

// Read-only preparation observations. These are never publication authority.
// The existing snapshot profile/sites/max fields describe retained publication;
// an unvisited measurement here is distinct from a measured zero.
enum class MfgPreparationStage : uint8_t
{
    None, HoldWrapper, WrapperRole, ProviderDiscovery, ProviderTextSnapshot,
    WrapperTextSnapshot, ProviderQualification, GatePlan, PatchCapacity,
    GateAddressMapping, TemporalAllocation, TemporalProtection, TemporalSlotRead,
    ProviderCodeCurrent, WrapperCodeCurrent, Publication
};

constexpr const char* MfgPreparationStageName(MfgPreparationStage stage) noexcept
{
    switch (stage)
    {
    case MfgPreparationStage::HoldWrapper: return "retain selected wrapper";
    case MfgPreparationStage::WrapperRole: return "selected wrapper role/provenance";
    case MfgPreparationStage::ProviderDiscovery: return "provider discovery/temporal descriptors";
    case MfgPreparationStage::ProviderTextSnapshot: return "provider executable text snapshot";
    case MfgPreparationStage::WrapperTextSnapshot: return "wrapper executable text snapshot";
    case MfgPreparationStage::ProviderQualification: return "provider/wrapper qualification";
    case MfgPreparationStage::GatePlan: return "gate plan validation";
    case MfgPreparationStage::PatchCapacity: return "gate plus temporal patch capacity";
    case MfgPreparationStage::GateAddressMapping: return "gate address remapping";
    case MfgPreparationStage::TemporalAllocation: return "temporal allocation";
    case MfgPreparationStage::TemporalProtection: return "temporal allocation protection";
    case MfgPreparationStage::TemporalSlotRead: return "temporal slot alignment/read";
    case MfgPreparationStage::ProviderCodeCurrent: return "provider live code currentness";
    case MfgPreparationStage::WrapperCodeCurrent: return "wrapper live code currentness";
    case MfgPreparationStage::Publication: return "patch transaction";
    default: return "not visited";
    }
}

struct MfgPreparationObservation
{
    MfgPreparationStage stage = MfgPreparationStage::None;
    uint64_t selectedWrapperIdentity = 0;
    uint64_t selectedProviderIdentity = 0;
    uint32_t callerAbi = 0;
    bool wrapperPathKnown = false;
    bool wrapperVersionKnown = false;
    bool temporalKnown = false;
    uint32_t temporalProfile = 0;
    uint32_t temporalDescriptors = 0;
    bool qualificationKnown = false;
    uint32_t qualificationStatus = 0; // MfgQualificationStatus, meaningful only when known.
    bool gateCountsKnown = false;
    uint32_t architectureSites = 0;
    uint32_t wrapperSites = 0;
    bool readOnlyException = false;
};

// Role evidence follows an actual successful existing upstream pointer lookup.
// never from a filename, requested label or provider-family observation alone.
enum class MfgWrapperResolutionSource : uint8_t { None, Plugin, Feature };
enum class MfgWrapperRoleStatus : uint8_t { Unobserved, Observed, Ambiguous, Retired, TrackingUnavailable };
struct MfgWrapperCallbackObservation
{
    bool observed = false;
    MfgWrapperResolutionSource source = MfgWrapperResolutionSource::None;
    // Source/resolver ownership is not authenticated by a post-return observation.
    uint64_t sourcePlugin = 0, resolverOwner = 0, callbackOwner = 0, callback = 0;
};
struct MfgWrapperRoleSnapshot
{
    MfgWrapperRoleStatus status = MfgWrapperRoleStatus::Unobserved;
    uint64_t selectionEpoch = 0;
    uint64_t selectedWrapper = 0;
    uint8_t trackingFailedMask = 0;
    uint64_t untrackedCallbacks[2] {};
    // Actual native ABI roles: SetOptions, GetState. An unvisited role is not
    // an error or a measured zero. Distinct observed targets are unresolved.
    MfgWrapperCallbackObservation callbacks[2];
};

struct MfgWrapperRoleLease;
struct MfgWrapperResolutionAccess;
struct MfgWrapperBinding;
class MfgWrapperResolution
{
    friend struct MfgWrapperResolutionAccess;
    friend class ::StreamlineHooks;
    friend MfgWrapperResolution InvalidateAdaMfg(uint64_t) noexcept;
    struct PreparedInput
    {
        HMODULE sourcePlugin = nullptr, resolverOwner = nullptr, callbackOwner = nullptr;
        const void* resolverEntry = nullptr;
        void* callback = nullptr;
        uint64_t epoch = 0;
        uint8_t kind = 0;
        MfgWrapperResolutionSource source = MfgWrapperResolutionSource::None;
        bool attempted = false;
        bool ready = false;
    } prepared;
    MfgWrapperRoleLease* retired[2] {};
    // Only existing genuine Streamline producer methods can create or commit
    // evidence. No public raw-pointer setter or lookup-executing helper exists.
    static MfgWrapperResolution FromPluginReturn(const char* name, void* function,
        uint64_t generation, const void* ownOptionsHook, const void* ownStateHook);
    static MfgWrapperResolution FromFeatureReturn(sl::Result result, sl::Feature feature,
        const char* name, void* function, uint64_t generation, const void* ownOptionsHook, const void* ownStateHook);
    uint64_t retiredGeneration = 0;
    uint8_t retiredTracking = 0;
    std::array<void*, 2> retiredUntracked {};
    MfgWrapperBinding Commit(std::atomic<uint64_t>& clock, uint64_t ownAdvance,
        MfgWrapperResolution* invalidated) noexcept;
public:
    void* function = nullptr;
    sl::Result result = sl::Result::eErrorIO;
    MfgWrapperResolution() noexcept = default;
    MfgWrapperResolution(const MfgWrapperResolution&) = delete;
    MfgWrapperResolution& operator=(const MfgWrapperResolution&) = delete;
    MfgWrapperResolution(MfgWrapperResolution&& other) noexcept;
    MfgWrapperResolution& operator=(MfgWrapperResolution&& other) noexcept;
    ~MfgWrapperResolution();
    bool HasNativeObservation() const noexcept { return prepared.attempted; }
};

enum class MfgWrapperBindingStatus : uint8_t
{
    Unobserved, Unchanged, Changed, Ambiguous, AmbiguousUnchanged, Retired,
    TrackingUnavailable, TrackingUnavailableUnchanged
};
struct MfgWrapperBinding
{
    MfgWrapperBindingStatus status = MfgWrapperBindingStatus::Unobserved;
    HMODULE wrapper = nullptr;
    uint64_t generation = 0;
};
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
    MfgPreparationObservation preparation;
    MfgWrapperRoleSnapshot wrapperRole;
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
MfgWrapperResolution InvalidateAdaMfg(uint64_t newGeneration) noexcept;
// Observations from the existing Win32 API and Streamline device hooks.
// Read-only observed-role hint. Publication checks retained input leases under
// its mutex; this hint or a diagnostic snapshot never grants eligibility.
bool IsAdaMfgWrapperModule(HMODULE module) noexcept;
// Broader provider/wrapper family recognition is retained for unload observation.
bool IsAdaMfgModule(HMODULE module) noexcept;
void NoteAdaModuleRelease(HMODULE module) noexcept;
void ObserveAdaD3D12Adapter(LUID luid) noexcept;
// Value-only observation. Device queries and GPU discovery stay outside the
// publication mutex; absence is distinct from a known unsupported adapter.
bool ObservedAdaD3D12Adapter(LUID& luid) noexcept;
// Consume authentic Create identity independently of opt-in/capability gates.
// Null adapter means an entered Create could not verify its command device;
// ordinary startup with no entered provider must not call this observation.
bool ObserveAdaCreateOwner(HMODULE enteredProvider, const LUID* enteredAdapter,
    uint64_t generation) noexcept;
// Install retained creation observers on a qualified mapped image. This never
// authorizes MFG gate/temporal publication merely because a DLL was loaded.
bool ObserveAdaLoadedModule(HMODULE module, DWORD loadFlags = 0) noexcept;
// Read-only, bounded diagnostics. Never used as publication authority.
void ObserveAdaExportResolution(HMODULE queried, const char* name,
    const void* resolved, const void* caller) noexcept;
}
