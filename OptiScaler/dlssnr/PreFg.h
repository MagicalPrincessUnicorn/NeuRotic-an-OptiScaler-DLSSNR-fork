#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <mutex>

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
};
class Ledger
{
    uint64_t constants = 0, tags = 0, consumed = 0, sequence = 0;
    bool foreignConstants = false, foreignTags = false;
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
        const uint64_t key = uint64_t(frame) + 1;
        if (tags != key) foreignTags = false;
        tags = key;
        foreignTags |= viewport != 0;
    }
    uint64_t Current() const { return foreignConstants ? 0 : constants; }
    Frame Claim()
    {
        Frame result {constants, ++sequence};
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
struct Registry
{
    std::mutex mutex;
    Ledger ledger;
    std::atomic<unsigned int> swapchains {0};
    std::atomic<uint64_t> realCalls {0}, submitted {0}, bypassed {0}, rejected {0};
};
inline Registry& State() { static auto* state = new Registry; return *state; }
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
        chain->SetPrivateDataInterface(creationQueueKey, queue.Get());
}
class Owner final : public IUnknown
{
    std::atomic<ULONG> refs {1};
  public:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
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
    if (!chain || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
    if (GetOwner(chain)) return true;
    Microsoft::WRL::ComPtr<Owner> owner;
    owner.Attach(new Owner(queue));
    return SUCCEEDED(chain->SetPrivateDataInterface(ownerKey, owner.Get()));
}
inline bool BypassLate(IDXGISwapChain* chain)
{
    if (!GetOwner(chain)) return false;
    ++State().bypassed;
    return true;
}
}
