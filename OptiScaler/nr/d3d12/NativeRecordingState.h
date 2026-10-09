#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dlssnr/NativeIdentity.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Neurotic::D3D12
{
// Optional diagnostic output from the real Reset identity checks. No authority.
struct NativeRecordingResetDiagnostic
{
    unsigned stage = 0;
    HRESULT result = S_OK;
    GUID iid{};
    std::uint64_t expectedIdentity = 0, observedIdentity = 0;
};
// One instance belongs to one command-list pointer for its entire lifetime. The
// hook owner maps command lists to instances, calls Begin on Create/Reset, and
// feeds every caller setter. The instance serializes observation and restore.
enum class RootKind { Table, Constants, CBV, SRV, UAV };
struct RootParameter
{
    RootKind kind;
    UINT constants = 0;
};
enum class RestoreMask : unsigned
{
    None = 0, Compute = 1, Graphics = 2, Pipeline = 4, Heaps = 8,
    All = Compute | Graphics | Pipeline | Heaps,
    // Opt-in only with Heaps: reproduce observed heap-invalidated tables by
    // an actual unbind/rebind, never by replaying their stale handles.
    HeapInvalidatedTables = 16
};
inline RestoreMask operator|(RestoreMask a, RestoreMask b)
{
    return static_cast<RestoreMask>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool Has(RestoreMask value, RestoreMask part)
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(part)) != 0;
}
struct NativeStateCaptureDiagnostic
{
    const char* reason = "Native.CommandStateUnavailable";
    UINT parameter = UINT_MAX, known = 0, required = 0;
    // Diagnostic tail only: never used for admission or restoration. The total
    // exposes overwritten entries; sequence and work ordinal have distinct roles.
    struct TraceEntry {
        const char* operation = "none";
        std::uint64_t sequence = 0, workOrdinal = 0, a = 0, b = 0, c = 0;
    };
    std::array<TraceEntry, 64> trace{};
    UINT traceCount = 0;
    std::uint64_t traceTotal = 0;
};

class NativeRecordingState
{
  public:
    struct OriginalSetters
    {
        std::function<void(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*)> heaps;
        std::function<void(ID3D12GraphicsCommandList*, ID3D12PipelineState*)> pipeline;
        std::function<void(ID3D12GraphicsCommandList*, ID3D12RootSignature*)> computeSignature, graphicsSignature;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> computeTable, graphicsTable;
        std::function<void(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT)> computeConstants, graphicsConstants;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)> computeCbv, computeSrv, computeUav;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)> graphicsCbv, graphicsSrv, graphicsUav;
        std::function<void(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*)> barriers;
    };

  private:
    struct Binding
    {
        RootKind kind = RootKind::Table;
        bool known = false;
        bool heapInvalidated = false;
        D3D12_GPU_DESCRIPTOR_HANDLE table{};
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        std::vector<UINT> constants;
        std::vector<bool> written;
    };
    struct Stage
    {
        std::uint64_t mutationOrdinal = 0;
        ID3D12RootSignature* signature = nullptr;
        bool signatureKnown = false;
        std::vector<Binding> bindings;
    };
    struct State
    {
        struct ResourceKey
        {
            ID3D12Resource* resource = nullptr;
            std::uint64_t generation = 0;
            UINT subresource = 0;
            bool operator==(const ResourceKey&) const = default;
        };
        struct ResourceHash
        {
            std::size_t operator()(const ResourceKey& key) const
            {
                auto h = std::hash<ID3D12Resource*>{}(key.resource);
                h ^= std::hash<std::uint64_t>{}(key.generation) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<UINT>{}(key.subresource) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        ID3D12GraphicsCommandList* list = nullptr;
        std::uint64_t incarnation = 0;
        std::uint64_t workOrdinal = 0;
        std::uint64_t unrestorableGraphicsOrdinal = 0;
        bool active = false;
        bool tainted = false;
        ID3D12PipelineState* pipeline = nullptr;
        bool pipelineKnown = false;
        std::array<ID3D12DescriptorHeap*, 2> heaps{};
        UINT heapCount = 0;
        bool heapsKnown = false;
        Stage compute, graphics;
        struct QueryScope { ID3D12QueryHeap* heap; D3D12_QUERY_TYPE type; UINT index; };
        std::vector<QueryScope> queries;
        std::unordered_map<ResourceKey, D3D12_RESOURCE_STATES, ResourceHash> resources;
        // Snapshots copy these owning references. A caller releasing its last
        // external reference cannot invalidate a stored restore pointer.
        std::unordered_map<IUnknown*, Microsoft::WRL::ComPtr<IUnknown>> retained;
        template<class T> void Retain(T* object)
        {
            if (object) retained.try_emplace(object, object);
        }
    };

  public:
    struct Snapshot
    {
      private:
        friend class NativeRecordingState;
        State state;
        RestoreMask mask = RestoreMask::None;
    };

    bool RegisterLayout(ID3D12RootSignature* signature, std::vector<RootParameter> layout)
    {
        std::lock_guard lock(mutex_);
        if (!signature || layout.size() > 64) return false;
        for (auto p : layout)
            if ((p.kind == RootKind::Constants && (!p.constants || p.constants > 64)) ||
                (p.kind != RootKind::Constants && p.constants)) return false;
        if (!layouts_.emplace(signature, std::move(layout)).second) return false;
        layoutOwners_.try_emplace(signature, signature);
        return true;
    }

    bool Begin(ID3D12GraphicsCommandList* list, std::uint64_t incarnation, ID3D12PipelineState* initialPipeline = nullptr)
    {
        std::lock_guard lock(mutex_);
        if (!list || !incarnation || (state_.list && state_.list != list) ||
            (state_.list && incarnation <= state_.incarnation))
            return false;
        state_ = {};
        state_.list = list;
        state_.Retain(list);
        state_.Retain(initialPipeline);
        state_.incarnation = incarnation;
        state_.pipeline = initialPipeline;
        state_.pipelineKnown = initialPipeline != nullptr;
        state_.active = true;
        traceTotal_ = 0;
        Trace("begin", reinterpret_cast<std::uint64_t>(initialPipeline), incarnation);
        return true;
    }
    void Close(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (Matches(incarnation)) state_.active = false;
    }
    void Taint(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (Matches(incarnation)) state_.tainted = true;
    }
    bool ObserveUnrestorableGraphics(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (state_.unrestorableGraphicsOrdinal == UINT64_MAX) { state_.tainted = true; return false; }
        ++state_.unrestorableGraphicsOrdinal;
        return true;
    }
    bool ObserveWork(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted || state_.workOrdinal == UINT64_MAX)
        {
            if (Matches(incarnation)) state_.tainted = true;
            return false;
        }
        ++state_.workOrdinal;
        return true;
    }
    std::optional<std::uint64_t> Activity(ID3D12GraphicsCommandList* list, std::uint64_t incarnation) const
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.list != list || state_.tainted) return {};
        return state_.workOrdinal;
    }
    static bool SupportedQuery(D3D12_QUERY_TYPE type)
    {
        switch(type) {
        case D3D12_QUERY_TYPE_OCCLUSION: case D3D12_QUERY_TYPE_BINARY_OCCLUSION:
        case D3D12_QUERY_TYPE_TIMESTAMP: case D3D12_QUERY_TYPE_PIPELINE_STATISTICS:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0: case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1:
        case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2: case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3:
        case D3D12_QUERY_TYPE_PIPELINE_STATISTICS1: return true;
        default: return false;
        }
    }
    bool ObserveQuery(std::uint64_t incarnation, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index, bool begin)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted) return false;
        const auto refuse = [&] { state_.tainted = true; return false; };
        if (!heap || !SupportedQuery(type) || state_.workOrdinal == UINT64_MAX) return refuse();
        const auto found = std::find_if(state_.queries.begin(),state_.queries.end(),[&](const auto& query) {
            return query.heap == heap && query.index == index;
        });
        if (begin) {
            if (type == D3D12_QUERY_TYPE_TIMESTAMP || found != state_.queries.end() || state_.queries.size() >= 64) return refuse();
            state_.Retain(heap);
            state_.queries.push_back({heap,type,index});
        } else if (type == D3D12_QUERY_TYPE_TIMESTAMP) {
            if (found != state_.queries.end()) return refuse();
        } else {
            if (found == state_.queries.end() || found->type != type) return refuse();
            state_.queries.erase(found);
        }
        ++state_.workOrdinal;
        return true;
    }
    bool ObserveQueryResolve(std::uint64_t incarnation, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT first, UINT count)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted) return false;
        const std::uint64_t end = static_cast<std::uint64_t>(first) + count;
        if (!heap || !SupportedQuery(type) || end > static_cast<std::uint64_t>(UINT_MAX) + 1 ||
            state_.workOrdinal == UINT64_MAX || std::any_of(state_.queries.begin(),state_.queries.end(),[&](const auto& query) {
                return query.heap == heap && query.index >= first && query.index < end;
            })) { state_.tainted = true; return false; }
        // Completed queries may come from an earlier command list. Observing
        // this effect does not certify heap bounds, argument validity or results.
        ++state_.workOrdinal;
        return true;
    }
    bool ObservePipeline(std::uint64_t incarnation, ID3D12PipelineState* pipeline)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (!pipeline) { state_.tainted = true; return false; }
        state_.pipeline = pipeline;
        state_.Retain(pipeline);
        state_.pipelineKnown = true;
        Trace("pipeline", reinterpret_cast<std::uint64_t>(pipeline));
        return true;
    }
    bool ObserveDescriptorHeaps(std::uint64_t incarnation, UINT count, ID3D12DescriptorHeap* const* heaps)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (count > 2 || (count && !heaps)) { state_.tainted = true; return false; }
        for (UINT i = 0; i < count; ++i)
            if (!heaps[i]) { state_.tainted = true; return false; }
        // Redundantly binding the same heaps does not invalidate D3D12 tables.
        // This does not fill any table that was already unknown.
        if (state_.heapsKnown && state_.heapCount == count &&
            (!count || std::equal(heaps, heaps + count, state_.heaps.begin())))
        {
            Trace("heaps-same", count ? reinterpret_cast<std::uint64_t>(heaps[0]) : 0,
                count > 1 ? reinterpret_cast<std::uint64_t>(heaps[1]) : 0, count);
            return true;
        }
        Trace("heaps-changed", count ? reinterpret_cast<std::uint64_t>(heaps[0]) : 0,
            count > 1 ? reinterpret_cast<std::uint64_t>(heaps[1]) : 0, count);
        state_.heaps = {};
        for (UINT i = 0; i < count; ++i)
        {
            state_.heaps[i] = heaps[i];
            state_.Retain(heaps[i]);
        }
        state_.heapCount = count;
        state_.heapsKnown = true;
        // D3D12 invalidates tables on a heap switch in both bind points.
        for (auto* stage : {&state_.compute, &state_.graphics})
            for (auto& binding : stage->bindings)
                if (binding.kind == RootKind::Table)
                {
                    binding.heapInvalidated = binding.known || binding.heapInvalidated;
                    binding.known = false;
                }
        return true;
    }
    bool ObserveComputeRootSignature(std::uint64_t incarnation, ID3D12RootSignature* signature)
    { std::lock_guard lock(mutex_); return ObserveSignature(incarnation, state_.compute, signature); }
    bool ObserveGraphicsRootSignature(std::uint64_t incarnation, ID3D12RootSignature* signature)
    { std::lock_guard lock(mutex_); return ObserveSignature(incarnation, state_.graphics, signature); }
    bool ObserveComputeTable(std::uint64_t incarnation, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
    { std::lock_guard lock(mutex_); return ObserveTable(incarnation, state_.compute, index, table); }
    bool ObserveGraphicsTable(std::uint64_t incarnation, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
    { std::lock_guard lock(mutex_); return ObserveTable(incarnation, state_.graphics, index, table); }
    bool ObserveComputeConstants(std::uint64_t incarnation, UINT index, UINT count, const void* values, UINT offset)
    { std::lock_guard lock(mutex_); return ObserveConstants(incarnation, state_.compute, index, count, values, offset); }
    bool ObserveGraphicsConstants(std::uint64_t incarnation, UINT index, UINT count, const void* values, UINT offset)
    { std::lock_guard lock(mutex_); return ObserveConstants(incarnation, state_.graphics, index, count, values, offset); }
    bool ObserveComputeDescriptor(std::uint64_t incarnation, UINT index, RootKind kind, D3D12_GPU_VIRTUAL_ADDRESS address)
    { std::lock_guard lock(mutex_); return ObserveDescriptor(incarnation, state_.compute, index, kind, address); }
    bool ObserveGraphicsDescriptor(std::uint64_t incarnation, UINT index, RootKind kind, D3D12_GPU_VIRTUAL_ADDRESS address)
    { std::lock_guard lock(mutex_); return ObserveDescriptor(incarnation, state_.graphics, index, kind, address); }
    // Register only an authoritative known state for a resource the inserted
    // operation may transition. The generation prevents pointer reuse.
    std::optional<D3D12_RESOURCE_STATES> KnownSingleSubresourceState(
        std::uint64_t incarnation, ID3D12Resource* resource) const
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || !state_.active || state_.tainted || !resource) return {};
        const auto desc=resource->GetDesc();
        if(desc.MipLevels!=1 || desc.DepthOrArraySize!=1 || desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D) return {};
        std::optional<D3D12_RESOURCE_STATES> result;
        for(const auto& [key,value]:state_.resources) if(key.resource==resource &&
            (key.subresource==0 || key.subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)) {
            if(result && *result!=value) return {};
            result=value;
        }
        return result;
    }
    bool WatchResource(std::uint64_t incarnation, ID3D12Resource* resource, std::uint64_t generation,
                       UINT subresource, D3D12_RESOURCE_STATES knownState)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (!resource || !generation) { state_.tainted = true; return false; }
        const bool added = state_.resources.emplace(State::ResourceKey{resource, generation, subresource}, knownState).second;
        if (added) state_.Retain(resource);
        if (!added) state_.tainted = true;
        return added;
    }
    bool ObserveTransition(std::uint64_t incarnation, ID3D12Resource* resource, std::uint64_t generation,
                           UINT subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        auto it = state_.resources.find({resource, generation, subresource});
        if (it == state_.resources.end() || it->second != before)
        {
            state_.tainted = true;
            return false;
        }
        it->second = after;
        return true;
    }
    bool ObserveBarriers(std::uint64_t incarnation, UINT count, const D3D12_RESOURCE_BARRIER* barriers)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (count && !barriers) { state_.tainted = true; return false; }
        for (UINT i = 0; i < count; ++i)
        {
            const auto& barrier = barriers[i];
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                // A split transition has a pending interval. Until that interval
                // is modeled, neither half authenticates an ordinary final state.
                if (barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE)
                { state_.tainted = true; return false; }
                const auto& transition = barrier.Transition;
                if (!transition.pResource) { state_.tainted = true; return false; }
                for (auto& [key, current] : state_.resources)
                {
                    if (key.resource != transition.pResource ||
                        (key.subresource != transition.Subresource &&
                         transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)) continue;
                    if (current != transition.StateBefore) { state_.tainted = true; return false; }
                    current = transition.StateAfter;
                }
            }
            else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
            {
                for (const auto& [key, current] : state_.resources)
                    if (!barrier.Aliasing.pResourceBefore || !barrier.Aliasing.pResourceAfter ||
                        key.resource == barrier.Aliasing.pResourceBefore ||
                        key.resource == barrier.Aliasing.pResourceAfter)
                    { state_.tainted = true; return false; }
            }
            else if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_UAV)
            { state_.tainted = true; return false; }
        }
        return true;
    }

    std::optional<Snapshot> Capture(ID3D12GraphicsCommandList* list, std::uint64_t incarnation, RestoreMask mask,
                                    NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        std::lock_guard lock(mutex_);
        if (diagnostic)
        {
            *diagnostic = {};
            diagnostic->traceTotal = traceTotal_;
            diagnostic->traceCount = static_cast<UINT>(std::min<std::uint64_t>(traceTotal_, trace_.size()));
            for (UINT i=0; i<diagnostic->traceCount; ++i)
                diagnostic->trace[i] = trace_[(traceTotal_-diagnostic->traceCount+i)%trace_.size()];
        }
        const auto refuse = [&](const char* reason) -> std::optional<Snapshot> {
            if (diagnostic) diagnostic->reason = reason;
            return {};
        };
        if (!Matches(incarnation) || list != state_.list || state_.tainted || mask == RestoreMask::None)
            return refuse("Native.State.InactiveOrTainted");
        if (!state_.queries.empty()) return refuse("Native.State.QueryOpen");
        if (Has(mask, RestoreMask::Pipeline) && !state_.pipelineKnown) return refuse("Native.State.PipelineUnknown");
        if (Has(mask, RestoreMask::Heaps) && !state_.heapsKnown) return refuse("Native.State.HeapsUnknown");
        const bool invalidated = Has(mask, RestoreMask::HeapInvalidatedTables);
        if (invalidated && (!Has(mask, RestoreMask::Heaps) || !state_.heapsKnown || !state_.heapCount))
            return refuse("Native.State.InvalidationRequiresBoundHeaps");
        if (Has(mask, RestoreMask::Compute) && !Complete(state_.compute, invalidated))
        { DiagnoseStage(state_.compute, true, false, diagnostic, invalidated); return {}; }
        if (Has(mask, RestoreMask::Graphics) && !Complete(state_.graphics, invalidated))
        { DiagnoseStage(state_.graphics, false, false, diagnostic, invalidated); return {}; }
        // A heap change affects tables at both bind points, even if one root signature is untouched.
        if (Has(mask, RestoreMask::Heaps))
        {
            if (state_.compute.signatureKnown && !CompleteTables(state_.compute, invalidated))
            { DiagnoseStage(state_.compute, true, true, diagnostic, invalidated); return {}; }
            if (state_.graphics.signatureKnown && !CompleteTables(state_.graphics, invalidated))
            { DiagnoseStage(state_.graphics, false, true, diagnostic, invalidated); return {}; }
        }
        Snapshot result;
        result.state = state_;
        result.mask = mask;
        return result;
    }

    std::optional<Snapshot> CaptureRestorable(ID3D12GraphicsCommandList* list, std::uint64_t incarnation,
                                              RestoreMask mask, const OriginalSetters& original,
                                              NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        auto snapshot = Capture(list, incarnation, mask, diagnostic);
        if (!snapshot) return {};
        if (!CanRestore(*snapshot, original))
        { if (diagnostic) diagnostic->reason = "Native.State.OriginalSetterMissing"; return {}; }
        // The caller must supply an original barrier path before it may change
        // any watched resource after this preflight.
        if (!snapshot->state.resources.empty() && !original.barriers)
        { if (diagnostic) diagnostic->reason = "Native.State.BarrierSetterMissing"; return {}; }
        return snapshot;
    }

    bool Restore(const Snapshot& snapshot, const OriginalSetters& original)
    {
        std::lock_guard lock(mutex_);
        const auto& saved = snapshot.state;
        if (!Matches(saved.incarnation) || state_.list != saved.list || state_.tainted || !saved.active)
            return false;
        if (!state_.queries.empty() || !saved.queries.empty()) return false;
        // A partial restore may leave a bind point untouched only if no setter
        // changed it since capture. Set-away/set-back is still a mutation.
        if ((!Has(snapshot.mask, RestoreMask::Compute) && state_.compute.mutationOrdinal != saved.compute.mutationOrdinal) ||
            (!Has(snapshot.mask, RestoreMask::Graphics) && state_.graphics.mutationOrdinal != saved.graphics.mutationOrdinal) ||
            state_.compute.mutationOrdinal == UINT64_MAX || state_.graphics.mutationOrdinal == UINT64_MAX ||
            state_.unrestorableGraphicsOrdinal != saved.unrestorableGraphicsOrdinal)
        { state_.tainted = true; return false; }
        // The preflight covers exactly the watched resources present in the
        // snapshot. A newly enrolled resource has no authenticated prior state.
        if (state_.resources.size() != saved.resources.size())
        { state_.tainted = true; return false; }
        // Heap restoration also restores both sets of tables. Without restoring
        // a stage's signature, those tables must still name the same root layout.
        if (Has(snapshot.mask, RestoreMask::Heaps) &&
            ((!Has(snapshot.mask, RestoreMask::Compute) && state_.compute.signature != saved.compute.signature) ||
             (!Has(snapshot.mask, RestoreMask::Graphics) && state_.graphics.signature != saved.graphics.signature)))
        { state_.tainted = true; return false; }
        for (const auto& [key, prior] : saved.resources)
        {
            auto it = state_.resources.find(key);
            if (it == state_.resources.end()) { state_.tainted = true; return false; }
            if (it->second != prior && !original.barriers) { state_.tainted = true; return false; }
        }
        if (!CanRestore(snapshot, original)) { state_.tainted = true; return false; }
        try
        {
            auto* list = saved.list;
            // Force real table invalidation even if inserted work leaves exactly
            // the same heap array bound. Then replay only authenticated tables.
            if (Has(snapshot.mask, RestoreMask::HeapInvalidatedTables) &&
                (HasInvalidatedTables(saved.compute) || HasInvalidatedTables(saved.graphics)))
                original.heaps(list, 0, saved.heaps.data());
            if (Has(snapshot.mask, RestoreMask::Heaps)) original.heaps(list, saved.heapCount, saved.heaps.data());
            if (Has(snapshot.mask, RestoreMask::Pipeline)) original.pipeline(list, saved.pipeline);
            if (Has(snapshot.mask, RestoreMask::Compute)) original.computeSignature(list, saved.compute.signature);
            if (Has(snapshot.mask, RestoreMask::Graphics)) original.graphicsSignature(list, saved.graphics.signature);
            if (Has(snapshot.mask, RestoreMask::Compute) || Has(snapshot.mask, RestoreMask::Heaps))
                RestoreBindings(list, saved.compute, original, true, Has(snapshot.mask, RestoreMask::Compute));
            if (Has(snapshot.mask, RestoreMask::Graphics) || Has(snapshot.mask, RestoreMask::Heaps))
                RestoreBindings(list, saved.graphics, original, false, Has(snapshot.mask, RestoreMask::Graphics));
            for (const auto& [key, prior] : saved.resources)
            {
                const auto after = state_.resources.at(key);
                if (after == prior) continue;
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {key.resource, key.subresource, after, prior};
                original.barriers(list, 1, &barrier);
            }
            // Only the values actually restored may replace current knowledge.
            // In particular, do not rewind work or unmasked state to the snapshot.
            if (Has(snapshot.mask, RestoreMask::Pipeline))
            {
                state_.pipeline = saved.pipeline;
                state_.pipelineKnown = saved.pipelineKnown;
            }
            if (Has(snapshot.mask, RestoreMask::Heaps))
            {
                state_.heaps = saved.heaps;
                state_.heapCount = saved.heapCount;
                state_.heapsKnown = saved.heapsKnown;
            }
            auto restoreStage = [&](Stage& current, const Stage& prior, RestoreMask part) {
                if (Has(snapshot.mask, part))
                {
                    const auto nextMutation = current.mutationOrdinal + 1;
                    current = prior;
                    current.mutationOrdinal = nextMutation;
                }
                else if (Has(snapshot.mask, RestoreMask::Heaps))
                    for (std::size_t i = 0; i < prior.bindings.size(); ++i)
                        if (prior.bindings[i].kind == RootKind::Table) current.bindings[i] = prior.bindings[i];
            };
            restoreStage(state_.compute, saved.compute, RestoreMask::Compute);
            restoreStage(state_.graphics, saved.graphics, RestoreMask::Graphics);
            for (const auto& [key, prior] : saved.resources) state_.resources.at(key) = prior;
            Trace("restore", static_cast<std::uint64_t>(snapshot.mask));
            return true;
        }
        catch (...) { state_.tainted = true; return false; }
    }

  private:
    static void DiagnoseStage(const Stage& stage, bool compute, bool tablesOnly, NativeStateCaptureDiagnostic* d, bool invalidated)
    {
        if (!d) return;
        if (!stage.signatureKnown)
        { d->reason = compute ? "Native.State.ComputeSignatureUnknown" : "Native.State.GraphicsSignatureUnknown"; return; }
        for (UINT i = 0; i < stage.bindings.size(); ++i)
        {
            const auto& b = stage.bindings[i];
            if (RestorableBinding(b, invalidated) || (tablesOnly && b.kind != RootKind::Table)) continue;
            d->parameter = i;
            if (b.kind == RootKind::Constants)
            {
                d->reason = compute ? "Native.State.ComputeConstants" : "Native.State.GraphicsConstants";
                d->known = UINT(std::count(b.written.begin(), b.written.end(), true));
                d->required = UINT(b.written.size());
            }
            else
            {
                d->reason = b.kind == RootKind::Table ?
                    (compute ? "Native.State.ComputeTable" : "Native.State.GraphicsTable") :
                    (compute ? "Native.State.ComputeDescriptor" : "Native.State.GraphicsDescriptor");
                d->known = 0; d->required = 1;
            }
            return;
        }
    }
    bool Matches(std::uint64_t incarnation) const
    { return state_.active && incarnation && state_.incarnation == incarnation; }
    bool ObserveSignature(std::uint64_t incarnation, Stage& stage, ID3D12RootSignature* signature)
    {
        if (!Matches(incarnation)) return false;
        if (stage.mutationOrdinal == UINT64_MAX) { state_.tainted = true; return false; }
        ++stage.mutationOrdinal;
        if (!signature) { state_.tainted = true; return false; }
        auto it = layouts_.find(signature);
        if (it == layouts_.end()) { state_.tainted = true; return false; }
        // D3D12 preserves root arguments on a redundant signature set. Keep
        // partial/unknown arguments partial; the observed setter still advances
        // the mutation ordinal above for omitted-stage restoration checks.
        if (stage.signatureKnown && stage.signature == signature)
        {
            Trace(&stage==&state_.compute ? "compute-signature-same" : "graphics-signature-same",
                reinterpret_cast<std::uint64_t>(signature), it->second.size());
            return true;
        }
        Trace(&stage==&state_.compute ? "compute-signature-changed" : "graphics-signature-changed",
            reinterpret_cast<std::uint64_t>(signature), it->second.size());
        stage.signature = signature;
        state_.Retain(signature);
        stage.signatureKnown = true;
        stage.bindings.clear();
        for (auto p : it->second)
        {
            Binding b;
            b.kind = p.kind;
            if (p.kind == RootKind::Constants)
            {
                b.constants.resize(p.constants);
                b.written.resize(p.constants);
            }
            stage.bindings.push_back(std::move(b));
        }
        return true;
    }
    bool ObserveTable(std::uint64_t incarnation, Stage& stage, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
    {
        if (!Matches(incarnation)) return false;
        if (stage.mutationOrdinal == UINT64_MAX) { state_.tainted = true; return false; }
        ++stage.mutationOrdinal;
        if (!state_.heapsKnown || index >= stage.bindings.size() ||
            stage.bindings[index].kind != RootKind::Table || !table.ptr)
        { state_.tainted = true; return false; }
        stage.bindings[index].heapInvalidated = false;
        stage.bindings[index].table = table;
        stage.bindings[index].known = true;
        Trace(&stage==&state_.compute ? "compute-table" : "graphics-table", index, table.ptr);
        return true;
    }
    bool ObserveConstants(std::uint64_t incarnation, Stage& stage, UINT index, UINT count, const void* values, UINT offset)
    {
        if (!Matches(incarnation)) return false;
        if (stage.mutationOrdinal == UINT64_MAX) { state_.tainted = true; return false; }
        ++stage.mutationOrdinal;
        if (index >= stage.bindings.size() ||
            stage.bindings[index].kind != RootKind::Constants || !values || !count)
        { state_.tainted = true; return false; }
        auto& b = stage.bindings[index];
        if (offset > b.constants.size() || count > b.constants.size() - offset)
        { state_.tainted = true; return false; }
        auto* source = static_cast<const UINT*>(values);
        for (UINT i = 0; i < count; ++i)
        {
            b.constants[offset + i] = source[i];
            b.written[offset + i] = true;
        }
        b.known = std::all_of(b.written.begin(), b.written.end(), [](bool v) { return v; });
        Trace(&stage==&state_.compute ? "compute-constants" : "graphics-constants", index, count, offset);
        return true;
    }
    bool ObserveDescriptor(std::uint64_t incarnation, Stage& stage, UINT index, RootKind kind,
                           D3D12_GPU_VIRTUAL_ADDRESS address)
    {
        if (!Matches(incarnation)) return false;
        if (stage.mutationOrdinal == UINT64_MAX) { state_.tainted = true; return false; }
        ++stage.mutationOrdinal;
        if (index >= stage.bindings.size() ||
            stage.bindings[index].kind != kind || kind == RootKind::Table || kind == RootKind::Constants)
        { state_.tainted = true; return false; }
        stage.bindings[index].address = address;
        stage.bindings[index].known = true;
        Trace(&stage==&state_.compute ? "compute-descriptor" : "graphics-descriptor", index, address,
            static_cast<std::uint64_t>(kind));
        return true;
    }
    static bool HasInvalidatedTables(const Stage& stage)
    {
        return std::any_of(stage.bindings.begin(), stage.bindings.end(), [](const Binding& b) {
            return b.kind == RootKind::Table && !b.known && b.heapInvalidated;
        });
    }
    static bool RestorableBinding(const Binding& b, bool invalidated)
    {
        return b.known || (invalidated && b.kind == RootKind::Table && b.heapInvalidated);
    }
    static bool Complete(const Stage& stage, bool invalidated)
    {
        if (!stage.signatureKnown) return false;
        return std::all_of(stage.bindings.begin(), stage.bindings.end(), [invalidated](const Binding& b) { return RestorableBinding(b, invalidated); });
    }
    static bool CompleteTables(const Stage& stage, bool invalidated)
    {
        return std::all_of(stage.bindings.begin(), stage.bindings.end(),
                           [invalidated](const Binding& b) { return b.kind != RootKind::Table || RestorableBinding(b, invalidated); });
    }
    static bool CanBindings(const Stage& stage, const OriginalSetters& o, bool compute, bool all, bool invalidated)
    {
        for (const auto& b : stage.bindings)
        {
            if (!all && b.kind != RootKind::Table) continue;
            if (!b.known)
            {
                if (RestorableBinding(b, invalidated)) continue;
                return false;
            }
            switch (b.kind)
            {
            case RootKind::Table: if (!(compute ? o.computeTable : o.graphicsTable)) return false; break;
            case RootKind::Constants: if (!(compute ? o.computeConstants : o.graphicsConstants)) return false; break;
            case RootKind::CBV: if (!(compute ? o.computeCbv : o.graphicsCbv)) return false; break;
            case RootKind::SRV: if (!(compute ? o.computeSrv : o.graphicsSrv)) return false; break;
            case RootKind::UAV: if (!(compute ? o.computeUav : o.graphicsUav)) return false; break;
            }
        }
        return true;
    }
    static void RestoreBindings(ID3D12GraphicsCommandList* list, const Stage& stage,
                                const OriginalSetters& o, bool compute, bool all)
    {
        for (UINT i = 0; i < stage.bindings.size(); ++i)
        {
            const auto& b = stage.bindings[i];
            if (!all && b.kind != RootKind::Table) continue;
            if (!b.known && b.kind == RootKind::Table && b.heapInvalidated) continue;
            switch (b.kind)
            {
            case RootKind::Table: (compute ? o.computeTable : o.graphicsTable)(list, i, b.table); break;
            case RootKind::Constants:
                (compute ? o.computeConstants : o.graphicsConstants)(list, i, UINT(b.constants.size()), b.constants.data(), 0);
                break;
            case RootKind::CBV: (compute ? o.computeCbv : o.graphicsCbv)(list, i, b.address); break;
            case RootKind::SRV: (compute ? o.computeSrv : o.graphicsSrv)(list, i, b.address); break;
            case RootKind::UAV: (compute ? o.computeUav : o.graphicsUav)(list, i, b.address); break;
            }
        }
    }
    static bool CanRestore(const Snapshot& s, const OriginalSetters& o)
    {
        const auto& x = s.state;
        if (Has(s.mask, RestoreMask::Heaps) && !o.heaps) return false;
        if (Has(s.mask, RestoreMask::Pipeline) && !o.pipeline) return false;
        if (Has(s.mask, RestoreMask::Compute) && !o.computeSignature) return false;
        if (Has(s.mask, RestoreMask::Graphics) && !o.graphicsSignature) return false;
        if ((Has(s.mask, RestoreMask::Compute) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.compute, o, true, Has(s.mask, RestoreMask::Compute), Has(s.mask, RestoreMask::HeapInvalidatedTables))) return false;
        if ((Has(s.mask, RestoreMask::Graphics) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.graphics, o, false, Has(s.mask, RestoreMask::Graphics), Has(s.mask, RestoreMask::HeapInvalidatedTables))) return false;
        return true;
    }
    void Trace(const char* operation, std::uint64_t a=0, std::uint64_t b=0, std::uint64_t c=0)
    {
        // Saturation leaves an explicitly finite tail; never changes authority.
        if (traceTotal_ == UINT64_MAX) return;
        trace_[traceTotal_%trace_.size()] = {operation, traceTotal_+1, state_.workOrdinal, a, b, c};
        ++traceTotal_;
    }
    std::array<NativeStateCaptureDiagnostic::TraceEntry, 64> trace_{};
    std::uint64_t traceTotal_ = 0;
    std::unordered_map<ID3D12RootSignature*, std::vector<RootParameter>> layouts_;
    std::unordered_map<ID3D12RootSignature*, Microsoft::WRL::ComPtr<ID3D12RootSignature>> layoutOwners_;
    mutable std::mutex mutex_;
    State state_;
};

struct NativeRecordingIdentity
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> nativeList;
    Microsoft::WRL::ComPtr<IUnknown> comIdentity;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<IUnknown> deviceIdentity;
    // This is the exact base interface used by the current hooks. It does not
    // certify coverage of methods added in any later interface version.
    GUID interfaceIid = __uuidof(ID3D12GraphicsCommandList);
    UINT interfaceVersion = 0;
    // Qualified at the real Reset, retained with this native list's lifetime.
    // Another list may use a different table with the same checked functions.
    void** commandTable = nullptr;
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
};

struct NativeRecordingObservation
{
    ID3D12GraphicsCommandList* nativeList = nullptr;
    std::uint64_t incarnation = 0;
    std::uint64_t workOrdinal = 0;
    bool active = false;
    bool completeCoverage = false;
    std::uint64_t hookGeneration = 0;
    bool trackingBeganBeforeRecording = false;
    bool missingRequiredHistory = true;
    std::shared_ptr<const NativeRecordingIdentity> identity;
};

// The hook owner feeds this adapter only from the actual command-list detours.
// Lists seen before a hooked Reset remain unknown; an address alone does not
// authenticate a recording incarnation.
class NativeRecordingObserver
{
    struct Entry
    {
        std::uint64_t incarnation = 0;
        std::uint64_t hookGeneration = 0;
        bool completeAtReset = false;
        std::shared_ptr<const NativeRecordingIdentity> identity;
        NativeRecordingState state;
    };
    mutable std::mutex mutex_;
    std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<Entry>> entries_;
    std::unordered_map<ID3D12RootSignature*, std::vector<RootParameter>> layouts_;
    std::unordered_map<ID3D12RootSignature*, Microsoft::WRL::ComPtr<ID3D12RootSignature>> layoutOwners_;
    std::uint64_t nextIncarnation_ = 0;
    std::uint64_t hookGeneration_ = 1;
    bool completeHookCoverage_ = false;
    std::function<bool(const NativeRecordingIdentity&)> coveredRoute_;

  public:
    void SetHookCoverage(bool complete, std::function<bool(const NativeRecordingIdentity&)> route = {})
    {
        std::lock_guard lock(mutex_);
        // Installing, replacing or losing hooks retires authority for every
        // active epoch. Even false -> true cannot repair already-missed calls.
        if (hookGeneration_ != UINT64_MAX) ++hookGeneration_;
        completeHookCoverage_ = complete && hookGeneration_ != UINT64_MAX;
        coveredRoute_ = std::move(route);
    }
    bool RegisterLayout(ID3D12RootSignature* signature, std::vector<RootParameter> layout)
    {
        std::lock_guard lock(mutex_);
        if (!signature) return false;
        if (auto existing = layouts_.find(signature); existing != layouts_.end())
            return existing->second.size() == layout.size() &&
                std::equal(existing->second.begin(), existing->second.end(), layout.begin(),
                    [](const auto& a, const auto& b) { return a.kind == b.kind && a.constants == b.constants; });
        for (auto& [list, entry] : entries_)
            if (!entry->state.RegisterLayout(signature, layout)) return false;
        layouts_.emplace(signature, std::move(layout));
        layoutOwners_.try_emplace(signature, signature);
        return true;
    }
    bool Reset(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline)
    {
        return ResetWithIdentity(list, initialPipeline, {});
    }
    // Called only after the original native Reset succeeds. Identity discovery
    // never issues a Reset itself and never certifies unseen earlier recording.
    bool ResetNative(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline, UINT interfaceVersion = 0,
                     NativeRecordingResetDiagnostic* diagnostic = nullptr, void** qualifiedTable = nullptr)
    {
        const auto fail = [&](unsigned stage, HRESULT result, const GUID& iid = GUID{}, std::uint64_t expected = 0, std::uint64_t observed = 0) {
            if (diagnostic) *diagnostic = {stage, result, iid, expected, observed};
            Close(list); return false;
        };
        try
        {
            const auto native = DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list);
            // A detour on a wrapper or another interface cannot authenticate the
            // base interface's native-this calling convention for this journal.
            if (!native.object) return fail(1, native.result, __uuidof(ID3D12GraphicsCommandList));
            if (native.object.Get() != list) return fail(2, native.result, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<std::uint64_t>(list), reinterpret_cast<std::uint64_t>(native.object.Get()));
            auto identity = std::make_shared<NativeRecordingIdentity>();
            identity->nativeList = native.object;
            identity->interfaceVersion = interfaceVersion;
            identity->commandTable = qualifiedTable;
            if (qualifiedTable && *reinterpret_cast<void***>(list) != qualifiedTable)
                return fail(9, E_FAIL, GUID{}, reinterpret_cast<std::uint64_t>(qualifiedTable), reinterpret_cast<std::uint64_t>(*reinterpret_cast<void***>(list)));
            auto hr = native.object.As(&identity->comIdentity);
            if (FAILED(hr)) return fail(3, hr, __uuidof(IUnknown));
            Microsoft::WRL::ComPtr<ID3D12Device> incomingDevice;
            hr = list->GetDevice(IID_PPV_ARGS(&incomingDevice));
            if (FAILED(hr)) return fail(4, hr, __uuidof(ID3D12Device));
            auto device = DlssNr::NativeIdentity::Resolve<ID3D12Device>(incomingDevice.Get());
            if (!device.object) return fail(5, device.result, __uuidof(ID3D12Device));
            hr = device.object.As(&identity->deviceIdentity);
            if (FAILED(hr)) return fail(6, hr, __uuidof(IUnknown));
            identity->device = device.object;
            identity->type = list->GetType();
            if (!ResetWithIdentity(list, initialPipeline, std::move(identity)))
            { if (diagnostic) *diagnostic = {7, S_OK}; return false; }
            return true;
        }
        catch (...) { return fail(8, E_FAIL); }
    }

  private:
    bool ResetWithIdentity(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline,
                           std::shared_ptr<const NativeRecordingIdentity> identity)
    {
        std::lock_guard lock(mutex_);
        if (!list || nextIncarnation_ == UINT64_MAX) return false;
        try
        {
            auto& entry = entries_[list];
            if (!entry)
            {
                entry = std::make_unique<Entry>();
                for (const auto& [signature, layout] : layouts_)
                    if (!entry->state.RegisterLayout(signature, layout)) return false;
            }
            const auto next = ++nextIncarnation_;
            if (!entry->state.Begin(list, next, initialPipeline)) return false;
            entry->incarnation = next;
            entry->hookGeneration = hookGeneration_;
            entry->completeAtReset = completeHookCoverage_ && (!coveredRoute_ || (identity && coveredRoute_(*identity)));
            entry->identity = std::move(identity);
            return true;
        }
        catch (...) { return false; }
    }

  public:
    void Close(ID3D12GraphicsCommandList* list)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        // Closed epochs cannot be restored. Drop their live observer ownership;
        // any extant snapshot independently retains the native objects it needs.
        if (it != entries_.end()) entries_.erase(it);
    }
    bool Work(ID3D12GraphicsCommandList* list)
    {
        return WithCurrent(list, [](NativeRecordingState& state, std::uint64_t incarnation) {
            return state.ObserveWork(incarnation);
        });
    }
    NativeRecordingObservation Observe(ID3D12GraphicsCommandList* list) const
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end()) return {};
        auto ordinal = it->second->state.Activity(list, it->second->incarnation);
        const bool historyComplete = CurrentCoverage(*it->second);
        return {list, it->second->incarnation, ordinal.value_or(0), ordinal.has_value(),
                historyComplete && ordinal.has_value(), it->second->hookGeneration,
                true, !historyComplete, it->second->identity};
    }
    template <typename Action>
    bool WithCurrent(ID3D12GraphicsCommandList* list, Action&& action)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end()) return false;
        try { return action(it->second->state, it->second->incarnation); }
        catch (...)
        {
            it->second->state.Taint(it->second->incarnation);
            return false;
        }
    }
    std::optional<NativeRecordingState::Snapshot> CaptureRestorable(
        ID3D12GraphicsCommandList* list, RestoreMask mask,
        const NativeRecordingState::OriginalSetters& original, NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end() || !CurrentCoverage(*it->second)) return {};
        return it->second->state.CaptureRestorable(list, it->second->incarnation, mask, original, diagnostic);
    }
    bool Restore(ID3D12GraphicsCommandList* list, std::uint64_t incarnation,
                 const NativeRecordingState::Snapshot& snapshot,
                 const NativeRecordingState::OriginalSetters& original)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end() || !CurrentCoverage(*it->second) || it->second->incarnation != incarnation) return false;
        return it->second->state.Restore(snapshot, original) && CurrentCoverage(*it->second);
    }

  private:
    bool CurrentCoverage(const Entry& entry) const
    {
        return completeHookCoverage_ && entry.completeAtReset && entry.hookGeneration == hookGeneration_ &&
            (!coveredRoute_ || (entry.identity && coveredRoute_(*entry.identity)));
    }
};
}
