#pragma once
#include <d3d12.h>
#include <dxgi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Neurotic::D3D12::NativeRtvFacts {
struct ViewRange {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RTV_DIMENSION dimension = D3D12_RTV_DIMENSION_UNKNOWN;
    UINT mip = 0, firstSlice = 0, sliceCount = 1, plane = 0, firstW = 0, wCount = 1;
    UINT64 firstElement = 0; UINT elementCount = 0;
};
inline bool Typeless(DXGI_FORMAT f) noexcept {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return true;
    default: return false;
    }
}
// Represents the actual view, not hardware format support or NR execution eligibility.
inline std::optional<ViewRange> NormalizeView(const D3D12_RESOURCE_DESC& resource, UINT planes,
                                             const D3D12_RENDER_TARGET_VIEW_DESC* supplied) noexcept {
    if (!planes || !resource.Width || !resource.Height || !resource.DepthOrArraySize ||
        !resource.MipLevels || !resource.SampleDesc.Count ||
        !(resource.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) return {};
    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    if (supplied) desc = *supplied;
    else {
        desc.Format = resource.Format;
        switch (resource.Dimension) {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (resource.DepthOrArraySize == 1) desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            else { desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY; desc.Texture1DArray.ArraySize = resource.DepthOrArraySize; }
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (resource.SampleDesc.Count > 1) {
                if (resource.DepthOrArraySize == 1) desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
                else { desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY; desc.Texture2DMSArray.ArraySize = resource.DepthOrArraySize; }
            } else if (resource.DepthOrArraySize == 1) desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            else { desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray.ArraySize = resource.DepthOrArraySize; }
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D; desc.Texture3D.WSize = resource.DepthOrArraySize; break;
        default: return {}; // A buffer has no inherited typed default RTV.
        }
    }
    ViewRange view; view.format = desc.Format == DXGI_FORMAT_UNKNOWN ? resource.Format : desc.Format;
    view.dimension = desc.ViewDimension;
    if (view.format == DXGI_FORMAT_UNKNOWN || Typeless(view.format)) return {};
    const auto array = [&](UINT first, UINT count) { return count && first < resource.DepthOrArraySize && count <= resource.DepthOrArraySize - first; };
    switch (desc.ViewDimension) {
    case D3D12_RTV_DIMENSION_BUFFER:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || !desc.Buffer.NumElements ||
            desc.Buffer.FirstElement > UINT64_MAX - desc.Buffer.NumElements) return {};
        view.firstElement = desc.Buffer.FirstElement; view.elementCount = desc.Buffer.NumElements; return view;
    case D3D12_RTV_DIMENSION_TEXTURE1D:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D || resource.DepthOrArraySize != 1) return {};
        view.mip = desc.Texture1D.MipSlice; break;
    case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D || !array(desc.Texture1DArray.FirstArraySlice, desc.Texture1DArray.ArraySize)) return {};
        view.mip = desc.Texture1DArray.MipSlice; view.firstSlice = desc.Texture1DArray.FirstArraySlice; view.sliceCount = desc.Texture1DArray.ArraySize; break;
    case D3D12_RTV_DIMENSION_TEXTURE2D:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || resource.SampleDesc.Count != 1 || resource.DepthOrArraySize != 1) return {};
        view.mip = desc.Texture2D.MipSlice; view.plane = desc.Texture2D.PlaneSlice; break;
    case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || resource.SampleDesc.Count != 1 || !array(desc.Texture2DArray.FirstArraySlice, desc.Texture2DArray.ArraySize)) return {};
        view.mip = desc.Texture2DArray.MipSlice; view.plane = desc.Texture2DArray.PlaneSlice;
        view.firstSlice = desc.Texture2DArray.FirstArraySlice; view.sliceCount = desc.Texture2DArray.ArraySize; break;
    case D3D12_RTV_DIMENSION_TEXTURE2DMS:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || resource.SampleDesc.Count <= 1 || resource.DepthOrArraySize != 1) return {}; break;
    case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || resource.SampleDesc.Count <= 1 || !array(desc.Texture2DMSArray.FirstArraySlice, desc.Texture2DMSArray.ArraySize)) return {};
        view.firstSlice = desc.Texture2DMSArray.FirstArraySlice; view.sliceCount = desc.Texture2DMSArray.ArraySize; break;
    case D3D12_RTV_DIMENSION_TEXTURE3D: {
        if (resource.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D || desc.Texture3D.MipSlice >= resource.MipLevels) return {};
        view.mip = desc.Texture3D.MipSlice; view.firstW = desc.Texture3D.FirstWSlice;
        const UINT depth = view.mip >= 16 ? 1u : std::max(1u, UINT(resource.DepthOrArraySize) >> view.mip);
        if (view.firstW >= depth) return {};
        view.wCount = desc.Texture3D.WSize == UINT_MAX ? depth - view.firstW : desc.Texture3D.WSize;
        if (!view.wCount || view.wCount > depth - view.firstW) return {}; break;
    }
    default: return {};
    }
    if (view.mip >= resource.MipLevels || view.plane >= planes) return {};
    return view;
}

// The native object's private-data reference is the lifetime owner. These tokens
// own metadata only; they never AddRef a resource, heap, device or each other cyclically.
class Owner {
    enum class Kind : unsigned { Device, Resource, Heap };
    struct Life;
    struct State;
public:
    using Token = std::shared_ptr<Life>;
    struct CreationTag {
        std::uint32_t magic = 0x4e524343, version = 1;
        std::uint64_t owner = 0;
        std::uintptr_t authority = 0;
        std::uint64_t event = 0, lifetime = 0;
        bool operator==(const CreationTag&) const = default;
    };
    struct Use {
        Token lifetime;
        std::optional<CreationTag> creation;
        std::uintptr_t canonical = 0;
    };
private:
    struct Fact {
        std::weak_ptr<Life> resource; ViewRange view;
        bool known = false, nullResource = false;
        std::optional<CreationTag> creation; std::uintptr_t canonical = 0;
    };
    struct Slot { std::uint64_t stamp = 0; Fact fact; };
    struct Life {
        std::atomic<bool> alive{true}; Kind kind; ID3D12Object* object; std::uint64_t generation;
        const State* authority = nullptr;
        std::uintptr_t canonical = 0;
        CreationTag creation;
        bool creationObserved = false, creationKnown = false, creationPending = false;
        bool creationConflict = false, privateCommitted = false;
        Token device; D3D12_RESOURCE_DESC desc{}; UINT planes = 0;
        SIZE_T start = 0; UINT count = 0, stride = 0;
        std::unordered_map<UINT, Slot> slots;
        Life(Kind k, ID3D12Object* o, std::uint64_t g) : kind(k), object(o), generation(g) {}
    };
    struct State {
        std::mutex mutex; bool active = true; std::uint64_t next = 0, nextCreation = 0;
        std::uint64_t domain;
        explicit State(std::uint64_t id) : domain(id) {}
        std::array<std::unordered_map<ID3D12Object*, std::weak_ptr<Life>>, 3> objects;
        std::array<std::unordered_set<ID3D12Object*>, 3> publishing;
        std::unordered_map<std::uintptr_t, std::weak_ptr<Life>> creationIdentities;
    };
    inline static std::atomic<std::uint64_t> nextDomain_{0};
    std::shared_ptr<State> state_ = std::make_shared<State>(++nextDomain_);
    inline static constexpr GUID key_{0xf301d2bf,0x6a3e,0x4a0f,{0x91,0xcf,0xb2,0x07,0x38,0xd1,0x55,0x6a}};
    inline static constexpr GUID creationKey_{0x35de763a,0x5b90,0x4dc7,{0x9e,0x46,0x7d,0xe8,0xa1,0x23,0x08,0xb9}};
    struct Cookie final : IUnknown {
        std::atomic<ULONG> refs{1}; std::weak_ptr<State> state; Token token;
        Cookie(const std::shared_ptr<State>& s, Token t) : state(s), token(std::move(t)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
            if (!out) return E_POINTER; *out = nullptr;
            if (iid != __uuidof(IUnknown)) return E_NOINTERFACE; *out = this; AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
        ~Cookie() {
            token->alive = false;
            try { if (auto s = state.lock()) { std::lock_guard lock(s->mutex);
                auto& entries = s->objects[unsigned(token->kind)]; auto f = entries.find(token->object);
                if (f != entries.end() && f->second.lock() == token) entries.erase(f);
                auto identity = s->creationIdentities.find(token->canonical);
                if (identity != s->creationIdentities.end() && identity->second.lock() == token) s->creationIdentities.erase(identity);
            }} catch (...) {}
        }
    };
    bool CurrentLocked(const Token& t) const noexcept {
        return state_->active && t && t->authority == state_.get() && t->alive &&
            (t->kind == Kind::Device || (t->device && t->device->authority == state_.get() && t->device->alive));
    }
    bool AssociatedLocked(const Use& use) const noexcept {
        const auto& t = use.lifetime;
        return CurrentLocked(t) && t->kind == Kind::Resource && use.creation &&
            t->creationKnown && !t->creationPending && !t->creationConflict &&
            t->canonical && t->canonical == use.canonical && t->creation == *use.creation;
    }
    template<class Configure> Token Enroll(Kind kind, ID3D12Object* object, const Token& device, Configure configure) {
        if (!object) return {};
        Token token;
        struct Publication {
            std::shared_ptr<State> state; Kind kind; ID3D12Object* object; bool reserved = false;
            ~Publication() { if (reserved) { std::lock_guard lock(state->mutex); state->publishing[unsigned(kind)].erase(object); } }
        } publication{state_, kind, object};
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->active || (kind != Kind::Device && (!CurrentLocked(device) || device->kind != Kind::Device))) return {};
            auto& map = state_->objects[unsigned(kind)]; auto f = map.find(object);
            if (f != map.end()) if (auto existing = f->second.lock(); CurrentLocked(existing))
                return existing->device == device ? existing : Token{};
            if (!state_->publishing[unsigned(kind)].insert(object).second) return {};
            publication.reserved = true;
            token = std::make_shared<Life>(kind, object, ++state_->next);
            token->authority = state_.get(); token->device = device; configure(*token);
        }
        // Foreign SetPrivateDataInterface and every Cookie Release are outside the lock.
        auto* cookie = new Cookie(state_, token);
        HRESULT result = E_FAIL;
        try { result = object->SetPrivateDataInterface(key_, cookie); }
        catch (...) { cookie->Release(); throw; }
        cookie->Release();
        if (FAILED(result)) return {};
        std::lock_guard lock(state_->mutex);
        if (!CurrentLocked(token)) return {};
        state_->objects[unsigned(kind)][object] = token; return token;
    }
    Token HeapLocked(const Token& device, SIZE_T address) const {
        if (!CurrentLocked(device)) return {};
        Token selected;
        for (const auto& [_, weak] : state_->objects[unsigned(Kind::Heap)]) if (auto h = weak.lock();
            CurrentLocked(h) && h->device == device && address >= h->start && h->stride &&
            (address - h->start) % h->stride == 0 && (address - h->start) / h->stride < h->count) {
            if (selected) return {}; selected = std::move(h);
        }
        return selected;
    }
public:
    struct Write { Token heap; UINT index = 0; std::uint64_t stamp = 0; };
private:
    struct CopyEntry { Write destination; Token sourceHeap; UINT sourceIndex = 0; std::uint64_t sourceStamp = 0; Fact fact; };
public:
    struct Copy { std::vector<CopyEntry> entries; };
    Owner() = default; Owner(const Owner&) = delete; Owner& operator=(const Owner&) = delete;
    ~Owner() { Retire(); }
    bool Current(const Token& token) const { std::lock_guard lock(state_->mutex); return CurrentLocked(token); }
    Token EnrollDevice(ID3D12Object* device) { return Enroll(Kind::Device, device, {}, [](auto&){}); }
    Token EnrollResource(ID3D12Object* resource, const Token& device, const D3D12_RESOURCE_DESC& desc, UINT planes,
                         std::uintptr_t canonical = 0) {
        // Zero means plane discovery is unavailable. Identity remains useful for
        // a protected whole resource; no RTV range can be normalized from zero.
        return Enroll(Kind::Resource, resource, device, [&](auto& t){ t.desc = desc; t.planes = planes; t.canonical = canonical; });
    }
    // Byte-only value read. A tag is not authority without a matching live
    // observer record and the canonical identity of the currently owned reference.
    static std::optional<CreationTag> ReadCreation(ID3D12Object* resource) {
        if (!resource) return {};
        UINT size = 0;
        const auto sized = resource->GetPrivateData(creationKey_, &size, nullptr);
        if ((FAILED(sized) && sized != DXGI_ERROR_MORE_DATA) || size != sizeof(CreationTag)) return {};
        CreationTag tag{}; size = sizeof(tag);
        if (FAILED(resource->GetPrivateData(creationKey_, &size, &tag)) || size != sizeof(tag) ||
            tag.magic != 0x4e524343 || tag.version != 1) return {};
        return tag;
    }
    Use Associate(const Token& resource, const std::optional<CreationTag>& tag, std::uintptr_t canonical) const {
        Use use{resource, tag, canonical}; std::lock_guard lock(state_->mutex);
        // Association is separate from eligibility: a shared creation remains
        // an observed event, but is not a private-allocation certificate.
        if (!AssociatedLocked(use)) { use.creation.reset(); use.canonical = 0; }
        return use;
    }
    bool InThisDomain(const CreationTag& tag) const noexcept {
        return tag.owner == state_->domain && tag.authority == reinterpret_cast<std::uintptr_t>(state_.get());
    }
    Use FindAssociated(const Token& device, const std::optional<CreationTag>& tag, std::uintptr_t canonical) const {
        if (!tag || !InThisDomain(*tag)) return {};
        std::lock_guard lock(state_->mutex);
        const auto found = state_->creationIdentities.find(canonical);
        if (found == state_->creationIdentities.end()) return {};
        Use use{found->second.lock(), tag, canonical};
        if (!AssociatedLocked(use) || use.lifetime->device != device) return {};
        return use;
    }
    bool ObserveCommitted(HRESULT result, ID3D12Object* object, const Token& resource,
                          std::uintptr_t canonical, D3D12_HEAP_FLAGS flags) {
        if (result != S_OK || !object || !canonical) return false;
        CreationTag tag;
        {
            std::lock_guard lock(state_->mutex);
            if (!CurrentLocked(resource) || resource->kind != Kind::Resource) return false;
            if (resource->creationObserved || resource->creationConflict ||
                (resource->canonical && resource->canonical != canonical)) {
                resource->creationConflict = true; resource->creationKnown = false; return false;
            }
            const auto previous = state_->creationIdentities.find(canonical);
            if (previous != state_->creationIdentities.end()) if (auto other = previous->second.lock(); CurrentLocked(other) && other != resource) {
                other->creationConflict = true; other->creationKnown = false;
                resource->creationConflict = true; return false;
            }
            resource->canonical = canonical;
            tag.owner = state_->domain; tag.authority = reinterpret_cast<std::uintptr_t>(state_.get());
            tag.event = ++state_->nextCreation; tag.lifetime = resource->generation;
            resource->creation = tag; resource->creationObserved = true; resource->creationPending = true;
            resource->privateCommitted = !(flags & (D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER));
            state_->creationIdentities[canonical] = resource;
        }
        HRESULT stored = E_FAIL;
        try { stored = object->SetPrivateData(creationKey_, sizeof(tag), &tag); }
        catch (...) {} // Metadata failure never changes the successful original API result.
        std::lock_guard lock(state_->mutex);
        if (!CurrentLocked(resource) || resource->creation != tag || resource->creationConflict) return false;
        resource->creationPending = false; resource->creationKnown = SUCCEEDED(stored);
        return resource->creationKnown;
    }
    Token EnrollHeap(ID3D12Object* heap, const Token& device, SIZE_T start, UINT count, UINT stride) {
        if (!start || !count || !stride || SIZE_T(count - 1) > (SIZE_MAX - start) / stride) return {};
        { std::lock_guard lock(state_->mutex);
            if (!CurrentLocked(device) || (device->stride && device->stride != stride)) return {};
            device->stride = stride;
        }
        return Enroll(Kind::Heap, heap, device, [&](auto& t){ t.start = start; t.count = count; t.stride = stride; });
    }
    Write BeginWrite(const Token& device, SIZE_T address) {
        std::lock_guard lock(state_->mutex); auto heap = HeapLocked(device, address); if (!heap) return {};
        const auto index = UINT((address - heap->start) / heap->stride);
        auto& slot = heap->slots[index]; slot.fact = {}; ++slot.stamp;
        return {std::move(heap), index, slot.stamp};
    }
    bool CompleteRtv(const Write& write, const Use& use, const D3D12_RENDER_TARGET_VIEW_DESC* desc) {
        // Resource metadata is immutable; no foreign discovery in publication.
        const auto& resource = use.lifetime;
        if (!resource) return false; const auto view = NormalizeView(resource->desc, resource->planes, desc);
        std::lock_guard lock(state_->mutex);
        if (!view || !CurrentLocked(write.heap) || !CurrentLocked(resource) || write.heap->device != resource->device) return false;
        auto found = write.heap->slots.find(write.index);
        if (found == write.heap->slots.end() || found->second.stamp != write.stamp) return false;
        found->second.fact = {resource, *view, true, false, use.creation, use.canonical}; return true;
    }
    bool CompleteRtv(const Write& write, const Token& resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc) {
        return CompleteRtv(write, Use{resource, {}, 0}, desc);
    }
    bool CompleteNull(const Write& write, const D3D12_RENDER_TARGET_VIEW_DESC* desc) {
        // A deliberately created null RTV discards writes. It is distinct from
        // an unobserved resource or descriptor; no subresource is fabricated.
        if (!desc || desc->Format == DXGI_FORMAT_UNKNOWN || Typeless(desc->Format) ||
            desc->ViewDimension < D3D12_RTV_DIMENSION_BUFFER || desc->ViewDimension > D3D12_RTV_DIMENSION_TEXTURE3D) return false;
        std::lock_guard lock(state_->mutex); if (!CurrentLocked(write.heap)) return false;
        auto found = write.heap->slots.find(write.index);
        if (found == write.heap->slots.end() || found->second.stamp != write.stamp) return false;
        Fact fact; fact.known = true; fact.nullResource = true; fact.view.dimension = desc->ViewDimension; fact.view.format = desc->Format;
        found->second.fact = fact; return true;
    }
    void ForgetViews(const Token& device) noexcept {
        try { std::lock_guard lock(state_->mutex);
            for (auto& [_, weak] : state_->objects[unsigned(Kind::Heap)])
                if (auto h = weak.lock(); h && h->device == device) { for (auto& [__, slot] : h->slots) { ++slot.stamp; slot.fact = {}; } }
        } catch (...) {}
    }
    Copy BeginCopyRanges(const Token& device, UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE* dst, const UINT* ds,
                         UINT ns, const D3D12_CPU_DESCRIPTOR_HANDLE* src, const UINT* ss) {
        Copy copy;
        try {
            if ((nd && !dst) || (ns && !src)) { ForgetViews(device); return {}; }
            std::vector<SIZE_T> destinations, sources;
            const auto expand = [&](UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* starts, const UINT* sizes, std::vector<SIZE_T>& out) {
                std::lock_guard lock(state_->mutex);
                if (!CurrentLocked(device) || !device->stride) return false;
                for (UINT i = 0; i < n; ++i) {
                    const UINT count = sizes ? sizes[i] : 1;
                    if (count && SIZE_T(count - 1) > (SIZE_MAX - starts[i].ptr) / device->stride) return false;
                    for (UINT j = 0; j < count; ++j) out.push_back(starts[i].ptr + SIZE_T(j) * device->stride);
                } return true;
            };
            if (!expand(nd, dst, ds, destinations) || !expand(ns, src, ss, sources) || destinations.size() != sources.size()) {
                ForgetViews(device); return {};
            }
            copy.entries.resize(sources.size());
            // Snapshot all sources before invalidating any destination.
            {
                std::lock_guard lock(state_->mutex);
                for (size_t i = 0; i < sources.size(); ++i) {
                    auto& entry = copy.entries[i]; entry.sourceHeap = HeapLocked(device, sources[i]);
                    if (!entry.sourceHeap) continue;
                    entry.sourceIndex = UINT((sources[i] - entry.sourceHeap->start) / entry.sourceHeap->stride);
                    const auto f = entry.sourceHeap->slots.find(entry.sourceIndex);
                    if (f != entry.sourceHeap->slots.end()) { entry.sourceStamp = f->second.stamp; entry.fact = f->second.fact; }
                }
            }
            for (size_t i = 0; i < destinations.size(); ++i) copy.entries[i].destination = BeginWrite(device, destinations[i]);
        } catch (...) { ForgetViews(device); return {}; }
        return copy;
    }
    Copy BeginCopySimple(const Token& device, UINT count, SIZE_T dst, SIZE_T src) {
        const D3D12_CPU_DESCRIPTOR_HANDLE d{dst}, s{src};
        return BeginCopyRanges(device, 1, &d, &count, 1, &s, &count);
    }
    void CompleteCopy(const Copy& copy) {
        std::lock_guard lock(state_->mutex);
        for (const auto& entry : copy.entries) {
            const auto& write = entry.destination;
            if (!CurrentLocked(write.heap)) continue;
            const auto dst = write.heap->slots.find(write.index);
            if (dst == write.heap->slots.end() || dst->second.stamp != write.stamp || !entry.fact.known || !CurrentLocked(entry.sourceHeap)) continue;
            const auto src = entry.sourceHeap->slots.find(entry.sourceIndex);
            if (src == entry.sourceHeap->slots.end() || src->second.stamp != entry.sourceStamp) continue;
            const auto resource = entry.fact.resource.lock();
            if (entry.fact.nullResource || CurrentLocked(resource)) dst->second.fact = entry.fact;
        }
    }
    // The current Post request protects whole resources. A descriptor range is
    // never narrowed to manufacture a disjoint subresource within that footprint.
    bool ClearIsDisjoint(const Token& device, std::span<const Use> protectedResources, SIZE_T address) const {
        std::lock_guard lock(state_->mutex);
        if (protectedResources.empty()) return false;
        auto heap = HeapLocked(device, address); if (!heap) return false;
        const auto slot = heap->slots.find(UINT((address - heap->start) / heap->stride));
        if (slot == heap->slots.end() || !slot->second.fact.known) return false;
        const auto target = slot->second.fact.resource.lock();
        if (!slot->second.fact.nullResource && (!CurrentLocked(target) || target->device != device)) return false;
        const Use targetUse{target, slot->second.fact.creation, slot->second.fact.canonical};
        if (!slot->second.fact.nullResource && (!AssociatedLocked(targetUse) || !target->privateCommitted)) return false;
        for (const auto& use : protectedResources) {
            const auto& protectedResource = use.lifetime;
            if (!CurrentLocked(protectedResource) || protectedResource->kind != Kind::Resource || protectedResource->device != device) return false;
            if (!slot->second.fact.nullResource && (!AssociatedLocked(use) || !protectedResource->privateCommitted || protectedResource == target ||
                use.canonical == targetUse.canonical || use.creation->owner != targetUse.creation->owner ||
                use.creation->authority != targetUse.creation->authority || use.creation->event == targetUse.creation->event)) return false;
        }
        return true;
    }
    bool ClearIsDisjoint(const Token& device, std::span<const Token> resources, SIZE_T address) const {
        std::vector<Use> uses; uses.reserve(resources.size());
        for (const auto& resource : resources) uses.push_back({resource, {}, 0});
        return ClearIsDisjoint(device, uses, address);
    }
    bool IsNoWrite(const Token& device, SIZE_T address) const {
        std::lock_guard lock(state_->mutex); const auto heap = HeapLocked(device, address);
        if (!heap) return false;
        const auto slot = heap->slots.find(UINT((address - heap->start) / heap->stride));
        return slot != heap->slots.end() && slot->second.fact.known && slot->second.fact.nullResource;
    }
    bool AssociationsCurrent(const Token& device, std::span<const Use> uses) const {
        std::lock_guard lock(state_->mutex);
        if (!CurrentLocked(device) || uses.empty()) return false;
        for (const auto& use : uses)
            if (!AssociatedLocked(use) || !use.lifetime->privateCommitted || use.lifetime->device != device) return false;
        return true;
    }
    void Retire() noexcept {
        try { std::lock_guard lock(state_->mutex); state_->active = false;
            for (auto& map : state_->objects) { for (auto& [_, weak] : map) if (auto t = weak.lock()) t->alive = false; map.clear(); }
            state_->creationIdentities.clear();
        } catch (...) {}
    }
};
}
