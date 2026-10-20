#pragma once
#include "NativeRtvFacts.h"
#include <dlssnr/NativeIdentity.h>
#include <wrl/client.h>
#include <windows.h>
#include <cstring>

namespace Neurotic::D3D12::NativeRtvObservation {
using Microsoft::WRL::ComPtr;
namespace F = NativeRtvFacts;
struct Route {
    std::array<void*, 5> target{};
    std::array<std::array<unsigned char, 16>, 5> patch{};
    bool Current() const noexcept {
        // Completed creation events are immutable contract evidence. Continuous
        // route coverage is required for descriptor mutations, not to re-attest
        // the allocator after an already witnessed successful creation.
        for (size_t i = 0; i < 4; ++i) {
            std::array<unsigned char, 16> bytes{}; SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), target[i], bytes.data(), bytes.size(), &read) ||
                read != bytes.size() || bytes != patch[i]) return false;
        } return true;
    }
};
inline bool Pin(void* address) noexcept {
    HMODULE module = nullptr;
    return address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(address), &module);
}
struct Store {
    std::mutex mutex; std::shared_ptr<const Route> route; std::shared_ptr<F::Owner> owner;
};
inline Store& State() { static auto* value = new Store; return *value; }
inline void Retire() noexcept {
    try { std::shared_ptr<F::Owner> old;
        { auto& s = State(); std::lock_guard lock(s.mutex); old = std::move(s.owner); }
        if (old) old->Retire();
    } catch (...) {}
}
// Called only after our known installation transaction and other device-hook
// installations have completed. Arbitrary later patch changes are not refreshed.
inline bool Configure(const std::array<void*, 5>& targets, bool installed) noexcept {
    if (!installed) { Retire(); return false; }
    try {
        auto route = std::make_shared<Route>(); route->target = targets;
        for (size_t i = 0; i < targets.size(); ++i) {
            SIZE_T read = 0;
            if (!Pin(targets[i]) || !ReadProcessMemory(GetCurrentProcess(), targets[i], route->patch[i].data(), 16, &read) || read != 16) {
                Retire(); return false;
            }
        }
        auto owner = std::make_shared<F::Owner>(); std::shared_ptr<F::Owner> old;
        { auto& s = State(); std::lock_guard lock(s.mutex);
            old = std::move(s.owner); s.route = std::move(route); s.owner = std::move(owner);
        }
        if (old) old->Retire(); return true;
    } catch (...) { Retire(); return false; }
}
// Explicit device enrollment can start a fresh observation epoch after a real
// retirement. Late callbacks only capture an existing epoch and cannot reopen it.
inline void ActivateKnownRoute() noexcept {
    try { auto& s = State(); std::shared_ptr<const Route> route;
        { std::lock_guard lock(s.mutex); if (s.owner) return; route = s.route; }
        if (!route || !route->Current()) return;
        auto owner = std::make_shared<F::Owner>();
        { std::lock_guard lock(s.mutex); if (!s.owner && s.route == route) s.owner = std::move(owner); }
    } catch (...) {}
}
struct Device {
    std::shared_ptr<F::Owner> facts; std::shared_ptr<const Route> route;
    F::Owner::Token token; ComPtr<ID3D12Device> native;
    explicit operator bool() const noexcept { return facts && route && token && native; }
};
inline Device CaptureDevice(ID3D12Device* device) {
    Device result;
    { auto& s = State(); std::lock_guard lock(s.mutex); result.facts = s.owner; result.route = s.route; }
    if (!result.facts || !result.route) return {};
    result.native = DlssNr::NativeIdentity::Resolve<ID3D12Device>(device).object;
    if (!result.native) return {};
    auto** table = *reinterpret_cast<void***>(result.native.Get());
    constexpr unsigned slots[]{14,20,23,24,27};
    for (size_t i = 0; i < 4; ++i) if (table[slots[i]] != result.route->target[i]) return {};
    result.token = result.facts->EnrollDevice(result.native.Get());
    if (!result.token) return {};
    return result;
}
struct Resource {
    Device device; F::Owner::Token token; F::Owner::Use use;
    ComPtr<ID3D12Resource> native; ComPtr<IUnknown> identity;
};
inline Resource CaptureResource(ID3D12Resource* resource) {
    Resource result; result.native = DlssNr::NativeIdentity::Resolve<ID3D12Resource>(resource).object;
    if (!result.native || FAILED(result.native.As(&result.identity))) return {};
    ComPtr<ID3D12Device> device;
    if (FAILED(result.native->GetDevice(IID_PPV_ARGS(&device)))) return {};
    result.device = CaptureDevice(device.Get()); if (!result.device) return {};
    const auto canonical = reinterpret_cast<std::uintptr_t>(result.identity.Get());
    const auto tag = F::Owner::ReadCreation(result.native.Get());
    result.use = result.device.facts->FindAssociated(result.device.token, tag, canonical);
    if (result.use.lifetime) { result.token = result.use.lifetime; return result; }
    // An unresolved reference to an existing event must not replace that
    // event's lifetime cookie or infer a second resource from a proxy address.
    if (tag && result.device.facts->InThisDomain(*tag)) return result;
    const auto desc = result.native->GetDesc();
    UINT planes = 0;
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) planes = 1;
    else {
        D3D12_FEATURE_DATA_FORMAT_INFO format{desc.Format, 0};
        if (SUCCEEDED(result.device.native->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &format, sizeof(format)))) planes = format.PlaneCount;
    }
    result.token = result.device.facts->EnrollResource(result.native.Get(), result.device.token, desc, planes, canonical);
    result.use = result.device.facts->Associate(result.token, tag, canonical);
    return result;
}
// Unconditional and separate from optional diagnostic suppression. An outer
// forwarded call is one documented creation, even if its implementation reenters.
class CommittedCallScope {
    inline static thread_local unsigned depth_ = 0;
    bool outer_ = depth_++ == 0;
public:
    CommittedCallScope() = default;
    ~CommittedCallScope() { --depth_; }
    bool Outermost() const noexcept { return outer_; }
    CommittedCallScope(const CommittedCallScope&) = delete;
    CommittedCallScope& operator=(const CommittedCallScope&) = delete;
};
inline void CommittedReturned(const CommittedCallScope& scope, HRESULT result, ID3D12Device* device,
    const D3D12_HEAP_PROPERTIES* properties, D3D12_HEAP_FLAGS flags, void** output) noexcept {
    if (!scope.Outermost() || result != S_OK || !properties || !output || !*output) return;
    try {
        auto context = CaptureDevice(device); if (!context) return;
        ComPtr<ID3D12Resource> resource;
        if (FAILED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&resource)))) return;
        auto captured = CaptureResource(resource.Get());
        if (!captured.token || captured.device.facts != context.facts || captured.device.token != context.token) return;
        // The actual successful documented call supplies creation kind. Neither
        // on-demand QI/GetHeapProperties nor old diagnostic pointer events do so.
        context.facts->ObserveCommitted(result, captured.native.Get(), captured.token,
            reinterpret_cast<std::uintptr_t>(captured.identity.Get()), flags);
    } catch (...) {}
}
template<class Original> HRESULT CreateHeap(Original original, ID3D12Device* device,
    const D3D12_DESCRIPTOR_HEAP_DESC* desc, REFIID iid, void** output) {
    const auto result = original(device, desc, iid, output);
    if (SUCCEEDED(result) && output && *output) try {
        auto context = CaptureDevice(device); if (!context) return result;
        ComPtr<ID3D12DescriptorHeap> heap;
        if (SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&heap)))) {
            const auto actual = heap->GetDesc();
            if (context && actual.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
                const auto start = heap->GetCPUDescriptorHandleForHeapStart();
                context.facts->EnrollHeap(heap.Get(), context.token, start.ptr, actual.NumDescriptors,
                    context.native->GetDescriptorHandleIncrementSize(actual.Type));
            }
        }
    } catch (...) {}
    return result;
}
template<class Original> void CreateRtv(Original original, ID3D12Device* device, ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    Device context; Resource source; F::Owner::Write write;
    std::optional<D3D12_RENDER_TARGET_VIEW_DESC> copied;
    // Foreign discovery/ownership finishes before destination invalidation.
    // The source remains owned until publication and the genuine call finish.
    try { context = CaptureDevice(device); if (context) source = CaptureResource(resource); }
    catch (...) {}
    try { if (desc) copied = *desc; if (context) write = context.facts->BeginWrite(context.token, destination.ptr); }
    catch (...) { if (context) context.facts->ForgetViews(context.token); }
    original(device, resource, desc, destination);
    try { if (context) {
        if (!resource) context.facts->CompleteNull(write, copied ? &*copied : nullptr);
        else if (source.token && source.device.facts == context.facts)
            context.facts->CompleteRtv(write, source.use, copied ? &*copied : nullptr);
    }
    } catch (...) { if (context) context.facts->ForgetViews(context.token); }
}
template<class Original> void CopyRanges(Original original, ID3D12Device* device,
    UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE* dst, const UINT* ds,
    UINT ns, const D3D12_CPU_DESCRIPTOR_HANDLE* src, const UINT* ss, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    Device context; F::Owner::Copy copy;
    try { if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) { context = CaptureDevice(device);
        if (context) copy = context.facts->BeginCopyRanges(context.token, nd, dst, ds, ns, src, ss);
    }} catch (...) { if (context) context.facts->ForgetViews(context.token); }
    original(device, nd, dst, ds, ns, src, ss, type);
    try { if (context) context.facts->CompleteCopy(copy); } catch (...) { if (context) context.facts->ForgetViews(context.token); }
}
template<class Original> void CopySimple(Original original, ID3D12Device* device, UINT count,
    D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    Device context; F::Owner::Copy copy;
    try { if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) { context = CaptureDevice(device);
        if (context) copy = context.facts->BeginCopySimple(context.token, count, dst.ptr, src.ptr);
    }} catch (...) { if (context) context.facts->ForgetViews(context.token); }
    original(device, count, dst, src, type);
    try { if (context) context.facts->CompleteCopy(copy); } catch (...) { if (context) context.facts->ForgetViews(context.token); }
}
struct Protected {
    std::shared_ptr<F::Owner> facts; std::shared_ptr<const Route> route;
    F::Owner::Token device; std::vector<F::Owner::Use> resources;
    bool Allows(SIZE_T descriptor) const {
        return facts && route && route->Current() && facts->ClearIsDisjoint(device, resources, descriptor);
    }
    bool IsNoWrite(SIZE_T descriptor) const { return facts && facts->IsNoWrite(device, descriptor); }
    bool Current() const { return facts && facts->AssociationsCurrent(device, resources); }
};
inline Protected Protect(std::span<ID3D12Resource* const> originals) {
    Protected result;
    for (auto* resource : originals) {
        if (!resource) continue; // Absent/unused guides are not unknown resources.
        const auto captured = CaptureResource(resource); if (!captured.token) return {};
        if (!result.facts) { result.facts = captured.device.facts; result.route = captured.device.route; result.device = captured.device.token; }
        if (result.facts != captured.device.facts || result.device != captured.device.token) return {};
        result.resources.push_back(captured.use);
    }
    return result;
}
}
