#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dlssnr/NativeIdentity.h>
#include "NativeIndirectSignatures.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
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
    // Opt-in only with Heaps: reproduce observed undefined tables by
    // an actual heap unbind/rebind, never by replaying their stale handles.
    // Admission requires an observed heap change after the table was created.
    HeapInvalidatedTables = 16,
    // Separate opt-in: an observed nonredundant root set makes its tables
    // undefined. Recreate that state via heap invalidation, never stale handles.
    RootInvalidatedTables = 32,
    // Ordinary dynamic blend state only; deliberately excluded from All.
    BlendFactor = 64
};
inline RestoreMask operator|(RestoreMask a, RestoreMask b)
{
    return static_cast<RestoreMask>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool Has(RestoreMask value, RestoreMask part)
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(part)) != 0;
}
struct NativeRecordingInvalidation
{
    const char* reason = "none";
    UINT slot = UINT_MAX;
    std::uint64_t sequence = 0, workOrdinal = 0;
};
struct NativeStateCaptureDiagnostic
{
    const char* reason = "Native.CommandStateUnavailable";
    UINT parameter = UINT_MAX, known = 0, required = 0;
    // Read-only snapshot of this recording; never an admission certificate.
    bool recordingPresent = false, rawActive = false, tainted = false;
    bool historyKnown = false, historyComplete = false;
    std::uint64_t incarnation = 0, hookGeneration = 0;
    NativeRecordingInvalidation firstInvalidation;
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
    // Evidence only. No diagnostic field participates in an admission decision.
    struct RestoreDiagnostic
    {
        const char* reason = "Native.Restore.NotAttempted";
        std::uint64_t savedComputeMutation = 0, currentComputeMutation = 0;
        std::uint64_t savedGraphicsMutation = 0, currentGraphicsMutation = 0;
        std::uint64_t savedBlendMutation = 0, currentBlendMutation = 0;
        std::uint64_t savedUnrestorableOrdinal = 0, currentUnrestorableOrdinal = 0;
        std::uint64_t savedWorkOrdinal = 0, currentWorkOrdinal = 0;
        std::uint64_t savedIncarnation = 0, currentIncarnation = 0;
        bool active = false, tainted = false;
        UINT lastUnrestorableSlot = UINT_MAX;
        std::uint64_t lastUnrestorableSequence = 0, lastUnrestorableOrdinal = 0, lastUnrestorableWorkOrdinal = 0;
        std::uint64_t lastUnrestorableA = 0, lastUnrestorableB = 0, lastUnrestorableC = 0;
        NativeStateCaptureDiagnostic tail;
    };
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
        std::function<void(ID3D12GraphicsCommandList*, const float*)> blendFactor;
    };

  private:
    struct Binding
    {
        RootKind kind = RootKind::Table;
        bool known = false;
        bool tableInvalidated = false;
        bool tableRootInvalidated = false;
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
        // Recording order only. Discard changes contents, not saved bindings;
        // restoration cannot undo a discard recorded after its snapshot.
        std::uint64_t discardOrdinal = 0;
        std::uint64_t unrestorableGraphicsOrdinal = 0;
        // Diagnostic witness only; survives overwriting the general trace tail.
        UINT lastUnrestorableSlot = UINT_MAX;
        std::uint64_t lastUnrestorableSequence = 0, lastUnrestorableOrdinal = 0, lastUnrestorableWorkOrdinal = 0;
        std::uint64_t lastUnrestorableA = 0, lastUnrestorableB = 0, lastUnrestorableC = 0;
        bool active = false;
        bool tainted = false;
        NativeRecordingInvalidation firstInvalidation;
        ID3D12PipelineState* pipeline = nullptr;
        bool pipelineKnown = false;
        std::array<ID3D12DescriptorHeap*, 2> heaps{};
        UINT heapCount = 0;
        bool heapsKnown = false;
        std::array<float,4> blendFactor{};
        bool blendKnown = false, blendOrdinary = false;
        std::uint64_t blendMutation = 0;
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
        if (layouts_.contains(signature)) return false;
        // Allocate/rehash both nodes while the owner is empty. A failed map
        // insertion must not unwind a COM-owning temporary under this lock.
        auto [owner, inserted] = layoutOwners_.try_emplace(signature);
        if (!inserted) return false;
        try { layouts_.emplace(signature, std::move(layout)); }
        catch (...) { layoutOwners_.erase(owner); throw; }
        owner->second = signature;
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
    void Taint(std::uint64_t incarnation, const char* reason = "Unclassified", UINT slot = UINT_MAX)
    {
        std::lock_guard lock(mutex_);
        if (Matches(incarnation)) Invalidate(reason, slot);
    }
    void AttributeInvalidation(std::uint64_t incarnation, UINT slot)
    {
        std::lock_guard lock(mutex_);
        if (Matches(incarnation) && state_.tainted && state_.firstInvalidation.slot == UINT_MAX)
            state_.firstInvalidation.slot = slot;
    }
    void Diagnose(NativeStateCaptureDiagnostic* diagnostic) const
    {
        if (!diagnostic) return;
        std::lock_guard lock(mutex_);
        FillCaptureDiagnostic(diagnostic);
    }
    bool ObserveUnrestorableGraphics(std::uint64_t incarnation, UINT slot = UINT_MAX,
                                    std::uint64_t a = 0, std::uint64_t b = 0, std::uint64_t c = 0)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (state_.unrestorableGraphicsOrdinal == UINT64_MAX) { Invalidate(__func__); return false; }
        ++state_.unrestorableGraphicsOrdinal;
        Trace("graphics-unrestorable", slot, state_.unrestorableGraphicsOrdinal, a);
        state_.lastUnrestorableSlot = slot;
        state_.lastUnrestorableSequence = traceTotal_;
        state_.lastUnrestorableOrdinal = state_.unrestorableGraphicsOrdinal;
        state_.lastUnrestorableWorkOrdinal = state_.workOrdinal;
        state_.lastUnrestorableA = a; state_.lastUnrestorableB = b; state_.lastUnrestorableC = c;
        return true;
    }
    bool ObserveBlendFactor(std::uint64_t incarnation, const float* values)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (state_.blendMutation == UINT64_MAX || !CopyBlend(values, state_.blendFactor))
        { Invalidate(__func__); return false; }
        ++state_.blendMutation;
        state_.blendKnown = true;
        // Conservative replay policy, not an API-validity judgment. In
        // particular -0 and the provider's negative tuple are never replayed.
        state_.blendOrdinary = std::all_of(state_.blendFactor.begin(),state_.blendFactor.end(),
            [](float value) { return std::bit_cast<std::uint32_t>(value) <= 0x3f800000u; });
        Trace("blend-factor",state_.blendMutation,state_.blendOrdinary,values ? 1 : 0);
        return true; // The original call still passes through exactly once.
    }
    // The optional immutable effects come only from observed successful native
    // signature creation. Missing/unsupported/mismatched metadata retains the
    // original conservative fallback. No GPU argument/count contents are read.
    bool ObserveIndirect(std::uint64_t incarnation, const NativeIndirectEffects* effects = nullptr,
                         std::uint64_t signatureIdentity = 0)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted) return false;
        if (state_.workOrdinal == UINT64_MAX || state_.unrestorableGraphicsOrdinal == UINT64_MAX ||
            state_.compute.mutationOrdinal == UINT64_MAX || state_.graphics.mutationOrdinal == UINT64_MAX)
        { Invalidate(__func__); return false; }
        ++state_.workOrdinal;
        // Every restore mask rejects a snapshot spanning indirect work, even
        // if subsequent explicit setters restore the same apparent values.
        ++state_.unrestorableGraphicsOrdinal;
        // Validate the entire effect set before granting any knowledge. A root
        // mismatch cannot be repaired by assuming the current layout is similar.
        Stage* selected = effects ? (effects->compute ? &state_.compute : &state_.graphics) : nullptr;
        bool authenticated = effects && (effects->resets.empty() ? !effects->root :
            selected->signatureKnown && effects->root.Get() == selected->signature);
        if (authenticated)
            for (const auto& reset : effects->resets)
            {
                if (reset.parameter >= selected->bindings.size()) { authenticated = false; break; }
                const auto& binding = selected->bindings[reset.parameter];
                const auto kind = reset.kind == NativeIndirectRootKind::Constants ? RootKind::Constants :
                    reset.kind == NativeIndirectRootKind::CBV ? RootKind::CBV :
                    reset.kind == NativeIndirectRootKind::SRV ? RootKind::SRV : RootKind::UAV;
                if (static_cast<unsigned>(reset.kind) > static_cast<unsigned>(NativeIndirectRootKind::UAV) || binding.kind != kind ||
                    (kind == RootKind::Constants && (!reset.count || reset.offset > binding.constants.size() ||
                     reset.count > binding.constants.size() - reset.offset)) ||
                    (kind != RootKind::Constants && (reset.offset || reset.count)))
                { authenticated = false; break; }
            }
        if (authenticated)
        {
            if (!effects->resets.empty()) ++selected->mutationOrdinal;
            for (const auto& reset : effects->resets)
            {
                auto& binding = selected->bindings[reset.parameter];
                if (binding.kind == RootKind::Constants)
                {
                    std::fill_n(binding.constants.begin() + reset.offset, reset.count, 0u);
                    std::fill_n(binding.written.begin() + reset.offset, reset.count, true);
                    binding.known = std::all_of(binding.written.begin(),binding.written.end(),[](bool x) { return x; });
                }
                else { binding.address = 0; binding.known = true; }
            }
            Trace(effects->compute ? "indirect-compute-reset" : "indirect-graphics-reset",
                signatureIdentity,effects->resets.size(),reinterpret_cast<std::uint64_t>(effects->root.Get()));
            return true;
        }
        const auto invalidate = [](Stage& stage) {
            ++stage.mutationOrdinal;
            for (auto& binding : stage.bindings)
            {
                // Preserve authentic table knowledge, including genuine heap
                // invalidation. Skipping a table never promotes unknown state.
                if (binding.kind == RootKind::Table) continue;
                binding.known = false;
                binding.tableInvalidated = false;
                std::fill(binding.written.begin(), binding.written.end(), false);
            }
        };
        invalidate(state_.compute);
        invalidate(state_.graphics);
        Trace("indirect-arguments-unknown",signatureIdentity);
        return true;
    }
    bool ObserveDiscard(std::uint64_t incarnation, ID3D12Resource* resource, bool regionSpecified)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted) return false;
        if (!resource || state_.discardOrdinal == UINT64_MAX || state_.workOrdinal == UINT64_MAX)
        { Invalidate("ObserveDiscard", 51); return false; }
        ++state_.discardOrdinal;
        ++state_.workOrdinal;
        // Do not dereference resource/region or certify image initialization,
        // barrier validity, execution or completion. Those owners are unchanged.
        Trace("discard-resource", reinterpret_cast<std::uint64_t>(resource), regionSpecified, state_.discardOrdinal);
        return true;
    }
    bool ObserveWork(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted || state_.workOrdinal == UINT64_MAX)
        {
            if (Matches(incarnation)) Invalidate(__func__);
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
        const auto refuse = [&] { Invalidate("ObserveQuery"); return false; };
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
            })) { Invalidate(__func__); return false; }
        // Completed queries may come from an earlier command list. Observing
        // this effect does not certify heap bounds, argument validity or results.
        ++state_.workOrdinal;
        return true;
    }
    bool ObservePipeline(std::uint64_t incarnation, ID3D12PipelineState* pipeline)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (!pipeline) { Invalidate(__func__); return false; }
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
        if (count > 2 || (count && !heaps)) { Invalidate(__func__); return false; }
        for (UINT i = 0; i < count; ++i)
            if (!heaps[i]) { Invalidate(__func__); return false; }
        // Heap bindings are per type: reversing the same valid two-heap
        // set is redundant too. Preserve existing unknown/partial state.
        if (state_.heapsKnown && state_.heapCount == count &&
            (!count || std::equal(heaps, heaps + count, state_.heaps.begin()) ||
             (count == 2 && heaps[0] == state_.heaps[1] && heaps[1] == state_.heaps[0])))
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
                    // A genuine heap-set change makes every table undefined,
                    // including slots without a previously captured value.
                    binding.tableInvalidated = true;
                    binding.tableRootInvalidated = false;
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
        if (!resource || !generation) { Invalidate(__func__); return false; }
        const bool added = state_.resources.emplace(State::ResourceKey{resource, generation, subresource}, knownState).second;
        if (added) state_.Retain(resource);
        if (!added) Invalidate(__func__);
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
            Invalidate(__func__);
            return false;
        }
        it->second = after;
        return true;
    }
    bool ObserveBarriers(std::uint64_t incarnation, UINT count, const D3D12_RESOURCE_BARRIER* barriers)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation)) return false;
        if (count && !barriers) { Invalidate(__func__); return false; }
        for (UINT i = 0; i < count; ++i)
        {
            const auto& barrier = barriers[i];
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                // A split transition has a pending interval. Until that interval
                // is modeled, neither half authenticates an ordinary final state.
                if (barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE)
                { Invalidate(__func__); return false; }
                const auto& transition = barrier.Transition;
                if (!transition.pResource) { Invalidate(__func__); return false; }
                for (auto& [key, current] : state_.resources)
                {
                    if (key.resource != transition.pResource ||
                        (key.subresource != transition.Subresource &&
                         transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)) continue;
                    if (current != transition.StateBefore) { Invalidate(__func__); return false; }
                    current = transition.StateAfter;
                }
            }
            else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
            {
                for (const auto& [key, current] : state_.resources)
                    if (!barrier.Aliasing.pResourceBefore || !barrier.Aliasing.pResourceAfter ||
                        key.resource == barrier.Aliasing.pResourceBefore ||
                        key.resource == barrier.Aliasing.pResourceAfter)
                    { Invalidate(__func__); return false; }
            }
            else if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_UAV)
            { Invalidate(__func__); return false; }
        }
        return true;
    }

    std::optional<Snapshot> Capture(ID3D12GraphicsCommandList* list, std::uint64_t incarnation, RestoreMask mask,
                                    NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        std::lock_guard lock(mutex_);
        if (diagnostic) FillCaptureDiagnostic(diagnostic);
        const auto refuse = [&](const char* reason) -> std::optional<Snapshot> {
            if (diagnostic) diagnostic->reason = reason;
            return {};
        };
        if (!Matches(incarnation) || list != state_.list || state_.tainted || mask == RestoreMask::None)
            return refuse("Native.State.InactiveOrTainted");
        if (!state_.queries.empty()) return refuse("Native.State.QueryOpen");
        if (Has(mask, RestoreMask::Pipeline) && !state_.pipelineKnown) return refuse("Native.State.PipelineUnknown");
        if (Has(mask, RestoreMask::Heaps) && !state_.heapsKnown) return refuse("Native.State.HeapsUnknown");
        if (Has(mask, RestoreMask::BlendFactor) && (!state_.blendKnown || !state_.blendOrdinary))
            return refuse("Native.State.BlendFactorUnknownOrOpaque");
        const bool invalidated = Has(mask, RestoreMask::HeapInvalidatedTables) || Has(mask, RestoreMask::RootInvalidatedTables);
        if (invalidated && (!Has(mask, RestoreMask::Heaps) || !state_.heapsKnown || !state_.heapCount))
            return refuse("Native.State.InvalidationRequiresBoundHeaps");
        if (Has(mask, RestoreMask::Compute) && !Complete(state_.compute, mask))
        { DiagnoseStage(state_.compute, true, false, diagnostic, mask); return {}; }
        if (Has(mask, RestoreMask::Graphics) && !Complete(state_.graphics, mask))
        { DiagnoseStage(state_.graphics, false, false, diagnostic, mask); return {}; }
        // A heap change affects tables at both bind points, even if one root signature is untouched.
        if (Has(mask, RestoreMask::Heaps))
        {
            if (state_.compute.signatureKnown && !CompleteTables(state_.compute, mask))
            { DiagnoseStage(state_.compute, true, true, diagnostic, mask); return {}; }
            if (state_.graphics.signatureKnown && !CompleteTables(state_.graphics, mask))
            { DiagnoseStage(state_.graphics, false, true, diagnostic, mask); return {}; }
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

    void DiagnoseRestore(const Snapshot& snapshot, RestoreDiagnostic* diagnostic) const
    {
        if (!diagnostic) return;
        std::lock_guard lock(mutex_);
        FillRestoreDiagnostic(snapshot, diagnostic, true);
    }
    bool Restore(const Snapshot& snapshot, const OriginalSetters& original,
                 RestoreDiagnostic* diagnostic = nullptr)
    {
        std::lock_guard lock(mutex_);
        const auto& saved = snapshot.state;
        if (diagnostic) { *diagnostic = {}; FillRestoreDiagnostic(snapshot, diagnostic, false); }
        auto refuse = [&](const char* reason) {
            if (diagnostic) { FillRestoreDiagnostic(snapshot, diagnostic, true); diagnostic->reason = reason; }
            return false;
        };
        if (!Matches(saved.incarnation) || state_.list != saved.list || state_.tainted || !saved.active)
            return refuse("Native.Restore.InactiveOrTainted");
        if (!state_.queries.empty() || !saved.queries.empty()) return refuse("Native.Restore.QueryOpen");
        if (state_.discardOrdinal != saved.discardOrdinal)
        { Invalidate("Native.Restore.DiscardBoundary", 51); return refuse("Native.Restore.DiscardBoundary"); }
        if ((!Has(snapshot.mask, RestoreMask::BlendFactor) && state_.blendMutation != saved.blendMutation) ||
            state_.blendMutation == UINT64_MAX)
        { Invalidate("Native.Restore.BlendFactorBoundary"); return refuse("Native.Restore.BlendFactorBoundary"); }
        // A partial restore may leave a bind point untouched only if no setter
        // changed it since capture. Set-away/set-back is still a mutation.
        if ((!Has(snapshot.mask, RestoreMask::Compute) && state_.compute.mutationOrdinal != saved.compute.mutationOrdinal) ||
            (!Has(snapshot.mask, RestoreMask::Graphics) && state_.graphics.mutationOrdinal != saved.graphics.mutationOrdinal) ||
            state_.compute.mutationOrdinal == UINT64_MAX || state_.graphics.mutationOrdinal == UINT64_MAX ||
            state_.unrestorableGraphicsOrdinal != saved.unrestorableGraphicsOrdinal)
        { Invalidate("Native.Restore.MutationBoundary"); return refuse("Native.Restore.MutationBoundary"); }
        // The preflight covers exactly the watched resources present in the
        // snapshot. A newly enrolled resource has no authenticated prior state.
        if (state_.resources.size() != saved.resources.size())
        { Invalidate("Native.Restore.ResourceSetChanged"); return refuse("Native.Restore.ResourceSetChanged"); }
        // Heap restoration also restores both sets of tables. Without restoring
        // a stage's signature, those tables must still name the same root layout.
        if (Has(snapshot.mask, RestoreMask::Heaps) &&
            ((!Has(snapshot.mask, RestoreMask::Compute) && state_.compute.signature != saved.compute.signature) ||
             (!Has(snapshot.mask, RestoreMask::Graphics) && state_.graphics.signature != saved.graphics.signature)))
        { Invalidate("Native.Restore.RootIdentityChanged"); return refuse("Native.Restore.RootIdentityChanged"); }
        for (const auto& [key, prior] : saved.resources)
        {
            auto it = state_.resources.find(key);
            if (it == state_.resources.end()) { Invalidate("Native.Restore.ResourceMissing"); return refuse("Native.Restore.ResourceMissing"); }
            if (it->second != prior && !original.barriers) { Invalidate("Native.Restore.BarrierSetterMissing"); return refuse("Native.Restore.BarrierSetterMissing"); }
        }
        if (!CanRestore(snapshot, original)) { Invalidate("Native.Restore.OriginalSetterMissing"); return refuse("Native.Restore.OriginalSetterMissing"); }
        try
        {
            auto* list = saved.list;
            // Force real table invalidation even if inserted work leaves exactly
            // the same heap array bound. Then replay only authenticated tables.
            if ((Has(snapshot.mask, RestoreMask::HeapInvalidatedTables) || Has(snapshot.mask, RestoreMask::RootInvalidatedTables)) &&
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
            if (Has(snapshot.mask, RestoreMask::BlendFactor)) original.blendFactor(list, saved.blendFactor.data());
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
            if (Has(snapshot.mask, RestoreMask::BlendFactor))
            {
                state_.blendFactor = saved.blendFactor;
                state_.blendKnown = saved.blendKnown;
                state_.blendOrdinary = saved.blendOrdinary;
                ++state_.blendMutation; // Never rewind the observation history.
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
            if (diagnostic) diagnostic->reason = "Native.Restore.Restored";
            return true;
        }
        catch (...) { Invalidate("Native.Restore.ReplayException"); return refuse("Native.Restore.ReplayException"); }
    }

  private:
    static bool CopyBlend(const float* values, std::array<float,4>& result) noexcept
    {
        if (!values) { result = {1.f,1.f,1.f,1.f}; return true; }
        __try { std::memcpy(result.data(),values,sizeof(result)); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // All callers hold mutex_. First cause survives later failures and ring wrap.
    void Invalidate(const char* reason, UINT slot = UINT_MAX)
    {
        if (!state_.tainted)
        {
            Trace("invalidation", slot);
            state_.firstInvalidation = {reason, slot, traceTotal_, state_.workOrdinal};
        }
        state_.tainted = true;
    }
    void FillCaptureDiagnostic(NativeStateCaptureDiagnostic* d) const
    {
        *d = {};
        d->recordingPresent = state_.list != nullptr;
        d->rawActive = state_.active; d->tainted = state_.tainted;
        d->incarnation = state_.incarnation;
        d->firstInvalidation = state_.firstInvalidation;
        d->traceTotal = traceTotal_;
        d->traceCount = static_cast<UINT>(std::min<std::uint64_t>(traceTotal_, trace_.size()));
        for (UINT i = 0; i < d->traceCount; ++i)
            d->trace[i] = trace_[(traceTotal_ - d->traceCount + i) % trace_.size()];
    }
    void FillRestoreDiagnostic(const Snapshot& snapshot, RestoreDiagnostic* d, bool tail) const
    {
        const auto& saved = snapshot.state;
        d->savedComputeMutation = saved.compute.mutationOrdinal;
        d->currentComputeMutation = state_.compute.mutationOrdinal;
        d->savedGraphicsMutation = saved.graphics.mutationOrdinal;
        d->currentGraphicsMutation = state_.graphics.mutationOrdinal;
        d->savedBlendMutation = saved.blendMutation;
        d->currentBlendMutation = state_.blendMutation;
        d->savedUnrestorableOrdinal = saved.unrestorableGraphicsOrdinal;
        d->currentUnrestorableOrdinal = state_.unrestorableGraphicsOrdinal;
        d->savedWorkOrdinal = saved.workOrdinal; d->currentWorkOrdinal = state_.workOrdinal;
        d->savedIncarnation = saved.incarnation; d->currentIncarnation = state_.incarnation;
        d->active = state_.active; d->tainted = state_.tainted;
        d->lastUnrestorableSlot = state_.lastUnrestorableSlot;
        d->lastUnrestorableSequence = state_.lastUnrestorableSequence;
        d->lastUnrestorableOrdinal = state_.lastUnrestorableOrdinal;
        d->lastUnrestorableWorkOrdinal = state_.lastUnrestorableWorkOrdinal;
        d->lastUnrestorableA = state_.lastUnrestorableA;
        d->lastUnrestorableB = state_.lastUnrestorableB;
        d->lastUnrestorableC = state_.lastUnrestorableC;
        if (!tail) return;
        FillCaptureDiagnostic(&d->tail);
    }
    static void DiagnoseStage(const Stage& stage, bool compute, bool tablesOnly, NativeStateCaptureDiagnostic* d, RestoreMask mask)
    {
        if (!d) return;
        if (!stage.signatureKnown)
        { d->reason = compute ? "Native.State.ComputeSignatureUnknown" : "Native.State.GraphicsSignatureUnknown"; return; }
        for (UINT i = 0; i < stage.bindings.size(); ++i)
        {
            const auto& b = stage.bindings[i];
            if (RestorableBinding(b, mask) || (tablesOnly && b.kind != RootKind::Table)) continue;
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
        if (stage.mutationOrdinal == UINT64_MAX) { Invalidate(__func__); return false; }
        if (!signature) { Invalidate(__func__); return false; }
        auto it = layouts_.find(signature);
        if (it == layouts_.end()) { Invalidate(__func__); return false; }
        // D3D12 preserves state on an exact redundant signature set. Keep
        // partial/unknown arguments partial and retain the trace, but do not
        // report a mutation of an omitted stage when no state changed.
        if (stage.signatureKnown && stage.signature == signature)
        {
            Trace(&stage==&state_.compute ? "compute-signature-same" : "graphics-signature-same",
                reinterpret_cast<std::uint64_t>(signature), it->second.size());
            return true;
        }
        ++stage.mutationOrdinal;
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
            // This actual changed-root call is provenance for undefined tables,
            // distinct from missed observation and from a real heap switch.
            b.tableRootInvalidated = p.kind == RootKind::Table;
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
        if (stage.mutationOrdinal == UINT64_MAX) { Invalidate(__func__); return false; }
        ++stage.mutationOrdinal;
        if (!state_.heapsKnown || index >= stage.bindings.size() ||
            stage.bindings[index].kind != RootKind::Table || !table.ptr)
        { Invalidate(__func__); return false; }
        stage.bindings[index].tableInvalidated = false;
        stage.bindings[index].tableRootInvalidated = false;
        stage.bindings[index].table = table;
        stage.bindings[index].known = true;
        Trace(&stage==&state_.compute ? "compute-table" : "graphics-table", index, table.ptr);
        return true;
    }
    bool ObserveConstants(std::uint64_t incarnation, Stage& stage, UINT index, UINT count, const void* values, UINT offset)
    {
        if (!Matches(incarnation)) return false;
        if (stage.mutationOrdinal == UINT64_MAX) { Invalidate(__func__); return false; }
        ++stage.mutationOrdinal;
        if (index >= stage.bindings.size() ||
            stage.bindings[index].kind != RootKind::Constants || !values || !count)
        { Invalidate(__func__); return false; }
        auto& b = stage.bindings[index];
        if (offset > b.constants.size() || count > b.constants.size() - offset)
        { Invalidate(__func__); return false; }
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
        if (stage.mutationOrdinal == UINT64_MAX) { Invalidate(__func__); return false; }
        ++stage.mutationOrdinal;
        if (index >= stage.bindings.size() ||
            stage.bindings[index].kind != kind || kind == RootKind::Table || kind == RootKind::Constants)
        { Invalidate(__func__); return false; }
        stage.bindings[index].address = address;
        stage.bindings[index].known = true;
        Trace(&stage==&state_.compute ? "compute-descriptor" : "graphics-descriptor", index, address,
            static_cast<std::uint64_t>(kind));
        return true;
    }
    static bool HasInvalidatedTables(const Stage& stage)
    {
        return std::any_of(stage.bindings.begin(), stage.bindings.end(), [](const Binding& b) {
            return b.kind == RootKind::Table && !b.known && (b.tableInvalidated || b.tableRootInvalidated);
        });
    }
    static bool RestorableBinding(const Binding& b, RestoreMask mask)
    {
        return b.known || (b.kind == RootKind::Table &&
            ((Has(mask, RestoreMask::HeapInvalidatedTables) && b.tableInvalidated) ||
             (Has(mask, RestoreMask::RootInvalidatedTables) && b.tableRootInvalidated)));
    }
    static bool Complete(const Stage& stage, RestoreMask mask)
    {
        if (!stage.signatureKnown) return false;
        return std::all_of(stage.bindings.begin(), stage.bindings.end(), [mask](const Binding& b) { return RestorableBinding(b, mask); });
    }
    static bool CompleteTables(const Stage& stage, RestoreMask mask)
    {
        return std::all_of(stage.bindings.begin(), stage.bindings.end(),
                           [mask](const Binding& b) { return b.kind != RootKind::Table || RestorableBinding(b, mask); });
    }
    static bool CanBindings(const Stage& stage, const OriginalSetters& o, bool compute, bool all, RestoreMask mask)
    {
        for (const auto& b : stage.bindings)
        {
            if (!all && b.kind != RootKind::Table) continue;
            if (!b.known)
            {
                if (RestorableBinding(b, mask)) continue;
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
            if (!b.known && b.kind == RootKind::Table && (b.tableInvalidated || b.tableRootInvalidated)) continue;
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
        if (Has(s.mask, RestoreMask::BlendFactor) && (!x.blendKnown || !x.blendOrdinary || !o.blendFactor)) return false;
        if ((Has(s.mask, RestoreMask::Compute) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.compute, o, true, Has(s.mask, RestoreMask::Compute), s.mask)) return false;
        if ((Has(s.mask, RestoreMask::Graphics) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.graphics, o, false, Has(s.mask, RestoreMask::Graphics), s.mask)) return false;
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
        // Retain only after both allocating insertions finish. Until then,
        // rollback destroys an empty owner and cannot call foreign Release.
        auto [owner, inserted] = layoutOwners_.try_emplace(signature);
        if (!inserted) return false;
        try { layouts_.emplace(signature, std::move(layout)); }
        catch (...) { layoutOwners_.erase(owner); throw; }
        owner->second = signature;
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
        // These owners outlive the lock guard. COM Release may reenter the
        // observer; neither old nor failed candidate entries retire under it.
        std::unique_ptr<Entry> retired, candidate;
        std::lock_guard lock(mutex_);
        if (!list || nextIncarnation_ == UINT64_MAX) return false;
        if (auto existing = entries_.find(list); existing != entries_.end())
        {
            retired = std::move(existing->second);
            entries_.erase(existing);
        }
        try
        {
            candidate = std::make_unique<Entry>();
            for (const auto& [signature, layout] : layouts_)
                if (!candidate->state.RegisterLayout(signature, layout)) return false;
            const auto next = ++nextIncarnation_;
            if (!candidate->state.Begin(list, next, initialPipeline)) return false;
            candidate->incarnation = next;
            candidate->hookGeneration = hookGeneration_;
            candidate->completeAtReset = completeHookCoverage_ &&
                (!coveredRoute_ || (identity && coveredRoute_(*identity)));
            candidate->identity = std::move(identity);
            // Allocate the map node without passing a COM-owning temporary.
            // Failure leaves the old recording absent, never still restorable.
            auto entry = entries_.try_emplace(list).first;
            entry->second = std::move(candidate);
            return true;
        }
        catch (...) { return false; }
    }

  public:
    void Close(ID3D12GraphicsCommandList* list)
    {
        std::unique_ptr<Entry> retired;
        {
            std::lock_guard lock(mutex_);
            auto it = entries_.find(list);
            // Remove authority while locked, then release foreign COM objects
            // after unlocking. Existing snapshots keep their independent holds.
            if (it != entries_.end())
            {
                retired = std::move(it->second);
                entries_.erase(it);
            }
        }
    }
    bool Work(ID3D12GraphicsCommandList* list)
    {
        return WithCurrent(list, [](NativeRecordingState& state, std::uint64_t incarnation) {
            return state.ObserveWork(incarnation);
        });
    }
    NativeRecordingObservation Observe(ID3D12GraphicsCommandList* list,
                                       NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        std::lock_guard lock(mutex_);
        if (diagnostic) *diagnostic = {};
        auto it = entries_.find(list);
        if (it == entries_.end()) return {};
        auto ordinal = it->second->state.Activity(list, it->second->incarnation);
        const bool historyComplete = CurrentCoverage(*it->second);
        if (diagnostic)
        {
            it->second->state.Diagnose(diagnostic);
            diagnostic->historyKnown = true;
            diagnostic->historyComplete = historyComplete;
            diagnostic->hookGeneration = it->second->hookGeneration;
        }
        return {list, it->second->incarnation, ordinal.value_or(0), ordinal.has_value(),
                historyComplete && ordinal.has_value(), it->second->hookGeneration,
                true, !historyComplete, it->second->identity};
    }
    template <typename Action>
    bool WithCurrent(ID3D12GraphicsCommandList* list, Action&& action, UINT slot = UINT_MAX)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end()) return false;
        const bool wasActive = slot != UINT_MAX &&
            it->second->state.Activity(list, it->second->incarnation).has_value();
        try
        {
            const bool accepted = action(it->second->state, it->second->incarnation);
            // Only the call which invalidated a usable recording can attribute
            // its slot. Later failures cannot relabel an existing first cause.
            if (wasActive && !accepted)
                it->second->state.AttributeInvalidation(it->second->incarnation, slot);
            return accepted;
        }
        catch (...)
        {
            it->second->state.Taint(it->second->incarnation, "ObservationException", slot);
            return false;
        }
    }
    std::optional<NativeRecordingState::Snapshot> CaptureRestorable(
        ID3D12GraphicsCommandList* list, RestoreMask mask,
        const NativeRecordingState::OriginalSetters& original, NativeStateCaptureDiagnostic* diagnostic = nullptr) const
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end()) { if (diagnostic) *diagnostic = {}; return {}; }
        const bool coverage = CurrentCoverage(*it->second);
        std::optional<NativeRecordingState::Snapshot> captured;
        if (coverage)
            captured = it->second->state.CaptureRestorable(list, it->second->incarnation, mask, original, diagnostic);
        else if (diagnostic)
        {
            it->second->state.Diagnose(diagnostic);
            diagnostic->reason = "Native.State.CoverageIncomplete";
        }
        if (diagnostic)
        {
            diagnostic->historyKnown = true; diagnostic->historyComplete = coverage;
            diagnostic->hookGeneration = it->second->hookGeneration;
        }
        return captured;
    }
    void DiagnoseRestore(ID3D12GraphicsCommandList* list, const NativeRecordingState::Snapshot& snapshot,
                         NativeRecordingState::RestoreDiagnostic* diagnostic) const
    {
        if (!diagnostic) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it != entries_.end()) it->second->state.DiagnoseRestore(snapshot, diagnostic);
    }
    bool Restore(ID3D12GraphicsCommandList* list, std::uint64_t incarnation,
                 const NativeRecordingState::Snapshot& snapshot,
                 const NativeRecordingState::OriginalSetters& original,
                 NativeRecordingState::RestoreDiagnostic* diagnostic = nullptr)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        auto refuse = [&](const char* reason) {
            if (diagnostic) {
                if (it != entries_.end()) it->second->state.DiagnoseRestore(snapshot, diagnostic);
                diagnostic->reason = reason;
            }
            return false;
        };
        if (it == entries_.end()) return refuse("Native.Restore.ObserverEntryMissing");
        if (!CurrentCoverage(*it->second)) return refuse("Native.Restore.ObserverCoverageIncomplete");
        if (it->second->incarnation != incarnation) return refuse("Native.Restore.ObserverIncarnationChanged");
        if (!it->second->state.Restore(snapshot, original, diagnostic)) return false;
        if (!CurrentCoverage(*it->second)) return refuse("Native.Restore.CoverageLostAfterReplay");
        return true;
    }

  private:
    bool CurrentCoverage(const Entry& entry) const
    {
        return completeHookCoverage_ && entry.completeAtReset && entry.hookGeneration == hookGeneration_ &&
            (!coveredRoute_ || (entry.identity && coveredRoute_(*entry.identity)));
    }
};
}
