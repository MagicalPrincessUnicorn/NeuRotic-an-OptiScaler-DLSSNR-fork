#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "NativeIdentity.h"

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
    bool (*prepareInputs)(const Frame&) = nullptr;
    bool allowOutput = true;
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
    struct Record
    {
        uint64_t key = 0, providerGeneration = 0;
        bool constants = false, tags = false, consumed = false;
        bool foreignConstants = false, foreignTags = false, legacy = false;
    };
    static constexpr size_t capacity = 16;
    std::array<Record, capacity> records {};
    uint64_t latestConstants = 0, latestTags = 0, consumed = 0, sequence = 0;
    Record* Find(uint64_t key, uint64_t providerGeneration = 0)
    {
        if (!key) return nullptr;
        auto& record = records[(key - 1) % capacity];
        return record.key == key && (!providerGeneration || record.providerGeneration == providerGeneration) ? &record : nullptr;
    }
    const Record* Find(uint64_t key, uint64_t providerGeneration = 0) const
    { return const_cast<Ledger*>(this)->Find(key, providerGeneration); }
    Record& Ensure(uint64_t key, uint64_t providerGeneration)
    {
        auto& record = records[(key - 1) % capacity];
        if (record.key != key || record.providerGeneration != providerGeneration)
        {
            record = {};
            record.key = key;
            record.providerGeneration = providerGeneration;
        }
        return record;
    }
  public:
    struct Snapshot
    {
        uint64_t constants, tags, consumed, sequence;
        bool foreignConstants, foreignTags, legacy;
    };
    Snapshot Inspect() const
    {
        const auto* constants = Find(latestConstants);
        const auto* tags = Find(latestTags);
        return {latestConstants, latestTags, consumed, sequence,
            constants && constants->foreignConstants, tags && tags->foreignTags,
            tags && tags->legacy};
    }
    void Constants(uint32_t frame, uint32_t viewport, uint64_t providerGeneration = 0)
    {
        const uint64_t key = uint64_t(frame) + 1;
        auto& record = Ensure(key, providerGeneration);
        record.constants = true;
        record.foreignConstants |= viewport != 0;
        latestConstants = key;
    }
    void Tags(uint32_t frame, uint32_t viewport, uint64_t providerGeneration = 0)
    {
        const uint64_t key = uint64_t(frame) + 1;
        auto& record = Ensure(key, providerGeneration);
        record.tags = true;
        record.legacy = false;
        record.foreignTags |= viewport != 0;
        latestTags = key;
    }
    void LegacyTags(uint32_t viewport, uint64_t providerGeneration = 0)
    {
        if (!latestConstants) { latestTags = 0; return; }
        auto& record = Ensure(latestConstants, providerGeneration);
        record.tags = true;
        record.legacy = true;
        record.foreignTags |= viewport != 0;
        latestTags = latestConstants;
    }
    uint64_t Current(uint64_t providerGeneration = 0) const
    {
        const auto* record = Find(latestConstants, providerGeneration);
        return record && record->constants && !record->foreignConstants ? latestConstants : 0;
    }
    Frame Claim(uint64_t selected = 0, bool requireSelected = false, uint64_t providerGeneration = 0)
    {
        const uint64_t key = selected ? selected : requireSelected ? 0 : latestConstants;
        Frame result {key, ++sequence};
        auto* record = Find(key, providerGeneration);
        const uint32_t advance = static_cast<uint32_t>(key - consumed);
        const bool fresh = key && (!consumed || (advance && advance < 0x80000000u));
        if (requireSelected && !selected) result.refusal = "No successful Streamline PresentStart frame marker";
        else if (!record || !record->constants || !record->tags) {}
        else if (record->foreignConstants || record->foreignTags)
            result.refusal = "Multiple/nonzero Streamline viewports are unsupported";
        else if (record->consumed || !fresh) result.refusal = "Duplicate/stale Streamline real-frame token";
        else { result.valid = true; result.refusal = nullptr; }
        if (record) result.legacyTags = record->legacy;
        // Consume only the explicitly selected Present frame. A refused frame cannot
        // later become eligible, while an overlapping future producer remains intact.
        if (fresh)
        {
            consumed = key;
            if (record) record->consumed = true;
        }
        return result;
    }
    void Reset() { records = {}; latestConstants = latestTags = consumed = 0; }
};

struct PresentIdentity { uint64_t key = 0; bool ambiguous = false; };
inline thread_local PresentIdentity presentingFrame;
inline void PresentStart(uint32_t frame)
{
    const uint64_t key = uint64_t(frame) + 1;
    if (presentingFrame.key && presentingFrame.key != key) presentingFrame.ambiguous = true;
    else presentingFrame.key = key;
}
inline void PresentEnd(uint32_t frame)
{
    if (presentingFrame.key != uint64_t(frame) + 1) presentingFrame.ambiguous = true;
    presentingFrame = {};
}
inline void PresentMarkerFailed() { presentingFrame = {}; }
inline uint64_t PresentFrame() { return presentingFrame.ambiguous ? 0 : presentingFrame.key; }
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
    std::atomic<unsigned int> swapchains {0};
    std::atomic<uint64_t> realCalls {0}, submitted {0}, bypassed {0}, rejected {0};
};
inline Registry& State() { static auto* state = new Registry; return *state; }
// Optional scalar observer called under the existing ledger lock. It cannot affect Claim.
using LedgerObserver = void(*)(const char*, uint32_t, uint32_t, Ledger::Snapshot, Ledger::Snapshot) noexcept;
inline std::atomic<LedgerObserver> ledgerObserver {nullptr};
inline void ObserveLedger(const char* operation, uint32_t frame, uint32_t viewport, Ledger::Snapshot before)
{
    if (auto observer = ledgerObserver.load(std::memory_order_relaxed))
        observer(operation, frame, viewport, before, State().ledger.Inspect());
}
inline void PublishProvider(bool enabled, bool supported)
{
    std::lock_guard lock(State().mutex);
    auto& provider = State().provider;
    if (!provider.known || provider.enabled != enabled || provider.supported != supported)
        ++provider.generation;
    provider.known = true; provider.enabled = enabled; provider.supported = supported;
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
    return state.swapchains == 1 ? state.ledger.Current(state.provider.generation) : 0;
}
inline void ObserveConstants(uint32_t frame, uint32_t viewport)
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto before = state.ledger.Inspect();
    state.ledger.Constants(frame, viewport, state.provider.generation);
    ObserveLedger("constants", frame, viewport, before);
}
inline void ObserveTags(uint32_t frame, uint32_t viewport)
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto before = state.ledger.Inspect();
    state.ledger.Tags(frame, viewport, state.provider.generation);
    ObserveLedger("tags", frame, viewport, before);
}
inline void ObserveLegacyTags(uint32_t viewport)
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto before = state.ledger.Inspect();
    state.ledger.LegacyTags(viewport, state.provider.generation);
    ObserveLedger("legacy-tags", 0, viewport, before);
}
inline Frame Claim(bool requirePresentIdentity = false)
{
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto before = state.ledger.Inspect();
    const auto selected = requirePresentIdentity ? PresentFrame() : 0;
    auto frame = state.ledger.Claim(selected, requirePresentIdentity, state.provider.generation);
    frame.providerGeneration = state.provider.generation;
    ObserveLedger("claim", selected ? static_cast<uint32_t>(selected - 1) : 0, 0, before);
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
