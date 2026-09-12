#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "NativeIdentity.h"
#include "FgLifecycleContract.h"

namespace DlssNr::PreFg
{
// A provider frame is not a DXGI presentation count. Only successful, frame-tagged
// constants/resources from the game's Streamline instance can populate this ledger.
struct Frame
{
    uint64_t key = 0;
    uint64_t sequence = 0;
    bool valid = false;
    const char* refusal = "No matching Streamline frame tags/constants";
    bool legacyTags = false;
    bool outputSubmitted = false;
    uint64_t providerGeneration = 0;
    FgLifecycle::Snapshot diagnosticClaim {};
    uint64_t nativeFgGeneration = 0;
    uint64_t nativeFgInstance = 0;
    uint64_t completionReservation = 0;
    ID3D12Resource* outputResource = nullptr; // borrowed while the swapchain owns its buffers
    bool (*prepareInputs)(const Frame&) = nullptr;
    bool allowOutput = true;
};

// Native DLSSG can evaluate asynchronously on a provider-owned queue after the
// application Present call has entered Streamline. Carry the NR copyback fence
// by exact backbuffer identity so that only the matching provider evaluation can
// consume it. The ledger deliberately retains no backbuffer reference.
struct NativeFgState
{
    struct Entry { uintptr_t handle = 0; uint64_t instance = 0; };
    std::array<Entry, 8> entries {};
    uint64_t generation = 0;
    uint64_t nextInstance = 0;
    uint64_t Find(uintptr_t handle) const
    {
        for (const auto& entry : entries)
            if (entry.instance && entry.handle == handle) return entry.instance;
        return 0;
    }
    uint64_t Create(uintptr_t handle)
    {
        if (!handle) return 0;
        ++generation;
        for (auto& entry : entries)
            if (entry.instance && entry.handle == handle)
            { entry.instance = ++nextInstance; return entry.instance; }
        for (auto& entry : entries)
            if (!entry.instance)
            { entry = {handle, ++nextInstance}; return entry.instance; }
        return 0;
    }
    bool Release(uintptr_t handle, uint64_t expected)
    {
        for (auto& entry : entries)
            if (entry.handle == handle && entry.instance == expected)
            { entry = {}; ++generation; return true; }
        return false;
    }
    FgLifecycle::Snapshot Read() const
    {
        FgLifecycle::Snapshot result {generation};
        for (const auto& entry : entries)
            if (entry.instance) { ++result.active; result.instance = entry.instance; }
        if (result.active != 1) result.instance = 0;
        return result;
    }
};

struct CompletionDependency
{
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    uint64_t value = 0;
    uint64_t token = 0;
    uint64_t sequence = 0;
};
enum class CompletionClaimResult { None, Ready, Refused };
struct CompletionClaim
{
    CompletionClaimResult result = CompletionClaimResult::None;
    CompletionDependency dependency;
};
class CompletionLedger
{
    struct Entry
    {
        ID3D12Resource* resource = nullptr;
        uint64_t reservation = 0;
        uint64_t providerGeneration = 0;
        uint64_t nativeFgGeneration = 0;
        uint64_t nativeFgInstance = 0;
        uint64_t token = 0;
        uint64_t sequence = 0;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        uint64_t value = 0;
        bool ready = false;
    };
    std::array<Entry, 8> entries {};
    uint64_t nextReservation = 0;
  public:
    uint64_t Reserve(ID3D12Resource* resource, uint64_t providerGeneration,
                     uint64_t nativeFgGeneration, uint64_t nativeFgInstance,
                     uint64_t token, uint64_t sequence)
    {
        if (!resource || !providerGeneration || !nativeFgGeneration || !nativeFgInstance) return 0;
        for (const auto& entry : entries)
            if (entry.reservation && entry.resource == resource) return 0;
        for (auto& entry : entries)
            if (!entry.reservation)
            {
                uint64_t reservation = ++nextReservation;
                if (!reservation) reservation = ++nextReservation;
                entry.resource = resource;
                entry.reservation = reservation;
                entry.providerGeneration = providerGeneration;
                entry.nativeFgGeneration = nativeFgGeneration;
                entry.nativeFgInstance = nativeFgInstance;
                entry.token = token;
                entry.sequence = sequence;
                return reservation;
            }
        return 0;
    }
    bool Commit(uint64_t reservation, ID3D12Resource* resource, ID3D12Fence* fence, uint64_t value)
    {
        if (!reservation || !resource || !fence || !value) return false;
        for (auto& entry : entries)
            if (entry.reservation == reservation && entry.resource == resource && !entry.ready)
            { entry.fence = fence; entry.value = value; entry.ready = true; return true; }
        return false;
    }
    void Cancel(uint64_t reservation)
    {
        for (auto& entry : entries)
            if (entry.reservation == reservation) { entry = {}; return; }
    }
    CompletionClaim Claim(ID3D12Resource* resource, uint64_t providerGeneration,
                          uint64_t nativeFgGeneration, uint64_t nativeFgInstance)
    {
        CompletionClaim claim;
        for (auto& entry : entries)
        {
            if (!entry.reservation || entry.resource != resource) continue;
            if (entry.providerGeneration != providerGeneration ||
                entry.nativeFgGeneration != nativeFgGeneration ||
                entry.nativeFgInstance != nativeFgInstance || !entry.ready ||
                !entry.fence || !entry.value)
            { claim.result = CompletionClaimResult::Refused; return claim; }
            claim.result = CompletionClaimResult::Ready;
            claim.dependency.fence = entry.fence;
            claim.dependency.value = entry.value;
            claim.dependency.token = entry.token;
            claim.dependency.sequence = entry.sequence;
            entry = {};
            return claim;
        }
        return claim;
    }
    void Reset() { for (auto& entry : entries) entry = {}; }
    unsigned int Count() const
    {
        unsigned int count = 0;
        for (const auto& entry : entries) if (entry.reservation) ++count;
        return count;
    }
};
// Keep the game's original image visible while private model initialization and
// frame admission settle. A failure restarts qualification, never reuses output.
class StartupGate
{
    unsigned int successful = 0;
  public:
    static constexpr unsigned int required = 8;
    bool Ready() const { return successful >= required; }
    unsigned int Count() const { return successful; }
    void Reset() { successful = 0; }
    void Observe(bool validModelAndPresent)
    {
        if (!validModelAndPresent) Reset();
        else if (successful < required) ++successful;
    }
};
// Present-scoped provenance only; zero outside the provider's synchronous call.
// An asynchronous provider must supply its own token mapping, never inherit this by time proximity.
inline thread_local const Frame* forwardingFrame = nullptr;
class ForwardFrame
{
    const Frame* previous;
  public:
    explicit ForwardFrame(const Frame& frame) : previous(forwardingFrame) { forwardingFrame = &frame; }
    ~ForwardFrame() { forwardingFrame = previous; }
    ForwardFrame(const ForwardFrame&) = delete;
    ForwardFrame& operator=(const ForwardFrame&) = delete;
};
class Ledger
{
    uint64_t constants = 0, tags = 0, consumed = 0, sequence = 0;
    bool foreignConstants = false, foreignTags = false;
    bool legacy = false;
  public:
    void Constants(uint32_t frame, uint32_t viewport)
    {
        const uint64_t key = uint64_t(frame) + 1;
        if (constants != key) foreignConstants = false;
        constants = key;
        foreignConstants |= viewport != 0;
    }
    void Tags(uint32_t frame, uint32_t viewport)
    {
        legacy = false;
        const uint64_t key = uint64_t(frame) + 1;
        if (tags != key) foreignTags = false;
        tags = key;
        foreignTags |= viewport != 0;
    }
    void LegacyTags(uint32_t viewport)
    {
        if (constants) Tags(static_cast<uint32_t>(constants - 1), viewport);
        else tags = 0;
        legacy = true;
    }
    uint64_t Current() const { return foreignConstants ? 0 : constants; }
    Frame Claim()
    {
        Frame result {constants, ++sequence};
        result.legacyTags = legacy;
        const uint32_t advance = static_cast<uint32_t>(constants - consumed);
        const bool fresh = constants && (!consumed || (advance && advance < 0x80000000u));
        if (foreignConstants || foreignTags) result.refusal = "Multiple/nonzero Streamline viewports are unsupported";
        else if (!constants || constants != tags) {}
        else if (!fresh) result.refusal = "Duplicate/stale Streamline real-frame token";
        else { result.valid = true; result.refusal = nullptr; }
        // A refused frame cannot later become eligible by a second Present call.
        if (fresh) consumed = constants;
        return result;
    }
    void Reset() { constants = tags = consumed = 0; foreignConstants = foreignTags = false; }
};
struct ProviderState
{
    bool known = false, enabled = false, supported = false;
    uint64_t generation = 0;
};
struct Registry
{
    std::mutex mutex;
    Ledger ledger;
    ProviderState provider;
    NativeFgState nativeFg;
    CompletionLedger completions;
    std::atomic<unsigned int> swapchains {0};
    std::atomic<uint64_t> realCalls {0}, submitted {0}, bypassed {0}, rejected {0};
};
inline Registry& State() { static auto* state = new Registry; return *state; }
inline void PublishProvider(bool enabled, bool supported)
{
    std::lock_guard lock(State().mutex);
    auto& provider = State().provider;
    if (!provider.known || provider.enabled != enabled || provider.supported != supported)
    {
        ++provider.generation;
        State().completions.Reset();
    }
    provider.known = true; provider.enabled = enabled; provider.supported = supported;
}
inline void PublishNativeFgCreated(uintptr_t handle)
{
    std::lock_guard lock(State().mutex);
    State().nativeFg.Create(handle);
    State().completions.Reset();
}
inline uint64_t NativeFgInstance(uintptr_t handle)
{
    std::lock_guard lock(State().mutex);
    return State().nativeFg.Find(handle);
}
inline void PublishNativeFgReleased(uintptr_t handle, uint64_t expected)
{
    std::lock_guard lock(State().mutex);
    if (State().nativeFg.Release(handle, expected)) State().completions.Reset();
}
inline FgLifecycle::Snapshot NativeFg()
{
    std::lock_guard lock(State().mutex);
    return State().nativeFg.Read();
}
inline uint64_t ReserveCompletion(ID3D12Resource* resource, const Frame& frame)
{
    std::lock_guard lock(State().mutex);
    return State().completions.Reserve(resource, frame.providerGeneration, frame.nativeFgGeneration,
        frame.nativeFgInstance, frame.key, frame.sequence);
}
inline bool CommitCompletion(uint64_t reservation, ID3D12Resource* resource, ID3D12Fence* fence, uint64_t value)
{
    std::lock_guard lock(State().mutex);
    return State().completions.Commit(reservation, resource, fence, value);
}
inline void CancelCompletion(uint64_t reservation)
{
    std::lock_guard lock(State().mutex);
    State().completions.Cancel(reservation);
}
inline CompletionClaim ClaimCompletion(ID3D12Resource* resource, uint64_t providerGeneration,
                                       uintptr_t nativeHandle)
{
    std::lock_guard lock(State().mutex);
    const auto native = State().nativeFg.Read();
    const auto instance = State().nativeFg.Find(nativeHandle);
    return State().completions.Claim(resource, providerGeneration, native.generation,
        native.active == 1 && instance == native.instance ? instance : 0);
}
inline unsigned int PendingCompletions()
{
    std::lock_guard lock(State().mutex);
    return State().completions.Count();
}
inline void RejectCompletion(ID3D12Resource* resource, uint64_t providerGeneration,
                             uintptr_t nativeHandle, uint64_t token, uint64_t sequence)
{
    std::lock_guard lock(State().mutex);
    const auto native = State().nativeFg.Read();
    const auto instance = State().nativeFg.Find(nativeHandle);
    if (native.active == 1 && instance == native.instance)
        State().completions.Reserve(resource, providerGeneration, native.generation,
            instance, token, sequence); // deliberately uncommitted: fail closed until invalidation
}
inline void ResetCompletions()
{
    std::lock_guard lock(State().mutex);
    State().completions.Reset();
}
inline ProviderState Provider()
{
    std::lock_guard lock(State().mutex);
    return State().provider;
}
inline uint64_t CurrentFrame()
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return state.swapchains == 1 ? state.ledger.Current() : 0;
}
inline void ObserveConstants(uint32_t frame, uint32_t viewport)
{
    std::lock_guard lock(State().mutex);
    State().ledger.Constants(frame, viewport);
}
inline void ObserveTags(uint32_t frame, uint32_t viewport)
{
    std::lock_guard lock(State().mutex);
    State().ledger.Tags(frame, viewport);
}
inline void ObserveLegacyTags(uint32_t viewport)
{
    std::lock_guard lock(State().mutex);
    State().ledger.LegacyTags(viewport);
}
inline Frame Claim()
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto frame = state.ledger.Claim();
    if (state.swapchains != 1)
    {
        frame.valid = false;
        frame.refusal = "No unique Streamline presentation owner";
    }
    ++state.realCalls;
    return frame;
}

// Stored on the underlying swapchain through forwarded DXGI private data. This
// retains its creation queue, never the swapchain or a backbuffer (no reference cycle).
inline constexpr GUID ownerKey = {0x76c1f5de, 0x14ac, 0x4583, {0x91,0xef,0x6d,0x43,0x8f,0x8e,0xa8,0x41}};
inline constexpr GUID creationQueueKey = {0x76c1f5df, 0x14ac, 0x4583, {0x91,0xef,0x6d,0x43,0x8f,0x8e,0xa8,0x41}};
inline void RememberQueue(IDXGISwapChain* chain, IUnknown* device)
{
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    if (chain && device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue))))
    {
        const auto native = NativeIdentity::Resolve<ID3D12CommandQueue>(queue.Get());
        if (native.object) chain->SetPrivateDataInterface(creationQueueKey, native.object.Get());
    }
}
class Owner final : public IUnknown
{
    std::atomic<ULONG> refs {1};
  public:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    std::recursive_mutex presentationMutex;
    bool lastFg = false;
    uint64_t providerGeneration = 0;
    double previousPresentMs = 0.0;
    StartupGate startup;
    unsigned int startupRoute = 0;
    uint64_t startupResume = 0;
    explicit Owner(ID3D12CommandQueue* value) : queue(value) { ++State().swapchains; }
    ~Owner() { --State().swapchains; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --refs;
        if (!remaining) delete this;
        return remaining;
    }
};
inline Microsoft::WRL::ComPtr<Owner> GetOwner(IDXGISwapChain* chain)
{
    Microsoft::WRL::ComPtr<Owner> result;
    UINT bytes = sizeof(Owner*);
    if (chain) chain->GetPrivateData(ownerKey, &bytes, result.GetAddressOf());
    return result;
}
inline bool Register(IDXGISwapChain* chain, ID3D12CommandQueue* queue)
{
    if (!chain || !queue) return false;
    const auto native = NativeIdentity::Resolve<ID3D12CommandQueue>(queue);
    if (!native.object || native.object->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
    if (GetOwner(chain)) return true;
    Microsoft::WRL::ComPtr<Owner> owner;
    owner.Attach(new Owner(native.object.Get()));
    return SUCCEEDED(chain->SetPrivateDataInterface(ownerKey, owner.Get()));
}
inline bool BypassLate(IDXGISwapChain* chain)
{
    if (!GetOwner(chain)) return false;
    ++State().bypassed;
    return true;
}
}
