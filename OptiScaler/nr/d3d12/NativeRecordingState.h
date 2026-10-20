#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <dlssnr/NativeIdentity.h>
#include "NativeRecordingInterfaces.h"
#include "NativeIndirectSignatures.h"
#include "NativeRootLayouts.h"
#include "NativeReplayFailure.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Neurotic::D3D12
{
// Caller owns the native resource. Descriptor/plane discovery happens before
// the final pure-journal read, never while holding observer/state locks.
struct NativeResourceStateQuery
{
    ID3D12Resource* resource = nullptr;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    UINT totalSubresources = 0;
};
// Diagnostic call scope only; it grants no state or execution authority.
enum class NativeRecordingPhase { Caller, Reset, UpstreamEvaluation, NeuralRendering };
class NativeRecordingPhaseScope
{
    inline static thread_local NativeRecordingPhase current_ = NativeRecordingPhase::Caller;
    NativeRecordingPhase prior_;
  public:
    explicit NativeRecordingPhaseScope(NativeRecordingPhase phase) : prior_(current_) { current_ = phase; }
    ~NativeRecordingPhaseScope() { current_ = prior_; }
    static NativeRecordingPhase Current() { return current_; }
    NativeRecordingPhaseScope(const NativeRecordingPhaseScope&) = delete;
    NativeRecordingPhaseScope& operator=(const NativeRecordingPhaseScope&) = delete;
};
inline const char* NativeRecordingPhaseName(NativeRecordingPhase phase)
{
    switch (phase) {
    case NativeRecordingPhase::Reset: return "Reset";
    case NativeRecordingPhase::UpstreamEvaluation: return "UpstreamEvaluation";
    case NativeRecordingPhase::NeuralRendering: return "NeuralRendering";
    default: return "Caller";
    }
}
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
    // Observed dynamic blend state; deliberately excluded from All.
    BlendFactor = 64,
    // Recreate arguments made undefined by an observed root change. Unknown
    // values from missing observation never qualify; default capture is strict.
    RootInvalidatedArguments = 128
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
    bool blendKnown = false;
    std::array<std::uint32_t,4> blendBits{};
    NativeRecordingPhase blendPhase = NativeRecordingPhase::Caller;
    std::uint64_t blendMutation = 0, blendObservedSequence = 0;
    // Distinct later unsupported effects remain visible after the sticky first cause.
    std::array<std::uint64_t,2> unsupportedEffectBits{};
    // Observed BeginQuery arguments only; no heap descriptor or query result is
    // queried here. Raw addresses are historical diagnostics, not COM rights.
    struct OpenQuery {
        std::uint64_t heapAddress = 0;
        D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
        UINT index = 0;
        std::uint64_t beginObservedWorkOrdinal = 0, beginMutationSerial = 0;
        NativeRecordingPhase beginPhase = NativeRecordingPhase::Caller;
    };
    std::array<OpenQuery,4> openQueries{};
    UINT openQueryTotal = 0, openQueryDetails = 0;
    std::uint64_t queryCaptureWorkOrdinal = 0;
    // Diagnostic tail only: never used for admission or restoration. The total
    // exposes overwritten entries; sequence and work ordinal have distinct roles.
    struct TraceEntry {
        const char* operation = "none";
        std::uint64_t sequence = 0, workOrdinal = 0, a = 0, b = 0, c = 0;
        std::array<std::uint32_t,4> blendBits{}; // populated only for blend-factor events
    };
    std::array<TraceEntry, 64> trace{};
    UINT traceCount = 0;
    std::uint64_t traceTotal = 0;
};

class NativeRecordingState
{
  public:
    // Internal insertion contract. Statistics include the inserted work; the
    // caller's live query elements remain untouched and are never replayed.
    enum class QueryPolicy { RequireClosed, PreservePipelineStatistics };
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
    using RootOwner = std::shared_ptr<Microsoft::WRL::ComPtr<ID3D12RootSignature>>;
    using ObjectOwner = std::shared_ptr<const Microsoft::WRL::ComPtr<IUnknown>>;
    struct PreparedOwners
    {
        std::array<ObjectOwner,2> owners{};
        std::array<IUnknown*,2> objects{};
        UINT count=0;
        // Acquisition may call arbitrary COM code. Populate caller-owned
        // storage before selecting an observer Entry; retain partial successes
        // through the actual original setter even if another acquisition fails.
        void Acquire(IUnknown* object)
        {
            if(!object)return;
            for(UINT i=0;i<count;++i)if(objects[i]==object)return;
            if(count==objects.size())throw std::runtime_error("native ownership batch overflow");
            owners[count]=std::make_shared<const Microsoft::WRL::ComPtr<IUnknown>>(object);
            objects[count]=object;++count;
        }
    };
    struct PreparedBarriers
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        std::vector<std::pair<IUnknown*, ObjectOwner>> owners;
        bool valid = true;
        void Acquire(UINT count, const D3D12_RESOURCE_BARRIER* input)
        {
            if (count && !input) { valid = false; return; }
            if (!count) return;
            barriers.assign(input, input + count);
            const auto retain = [&](ID3D12Resource* resource) {
                if (!resource) return;
                if (std::any_of(owners.begin(), owners.end(), [&](const auto& item) { return item.first == resource; })) return;
                auto owner = std::make_shared<const Microsoft::WRL::ComPtr<IUnknown>>(resource);
                owners.emplace_back(resource, std::move(owner));
            };
            for (const auto& barrier : barriers)
                if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) retain(barrier.Transition.pResource);
                else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
                { retain(barrier.Aliasing.pResourceBefore); retain(barrier.Aliasing.pResourceAfter); }
        }
    };
    struct OriginalSetters
    {
        std::function<void(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*)> heaps;
        std::function<void(ID3D12GraphicsCommandList*, ID3D12PipelineState*)> pipeline;
        std::function<void(ID3D12GraphicsCommandList*, ID3D12StateObject*)> stateObject;
        std::function<void(ID3D12GraphicsCommandList*, ID3D12RootSignature*)> computeSignature, graphicsSignature;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> computeTable, graphicsTable;
        std::function<void(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT)> computeConstants, graphicsConstants;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)> computeCbv, computeSrv, computeUav;
        std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)> graphicsCbv, graphicsSrv, graphicsUav;
        std::function<void(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*)> barriers;
        std::function<void(ID3D12GraphicsCommandList*, const float*)> blendFactor;
        std::function<void(ID3D12GraphicsCommandList*, float, float, float)> depthBias;
        // The observer supplies an entry/generation lease. These callbacks run
        // outside the State lock; commit calls only the supplied metadata action.
        std::function<bool()> validate;
        std::function<bool(const std::function<bool()>&)> commit;
    };

  private:
    struct Binding
    {
        RootKind kind = RootKind::Table;
        bool known = false;
        bool tableInvalidated = false;
        bool tableRootInvalidated = false;
        // For non-table arguments only: unwritten values are genuinely
        // undefined after a real root change, not missing observations.
        bool rootInvalidated = false;
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
        ID3D12StateObject* stateObject = nullptr;
        enum class PipelineKind { Ordinary, StateObject } pipelineKind = PipelineKind::Ordinary;
        bool pipelineKnown = false;
        struct PipelineCommand {
            UINT slot=25;
            ID3D12PipelineState* pipeline=nullptr;
            ID3D12StateObject* stateObject=nullptr;
            std::array<std::uint32_t,3> biasBits{};
        };
        // Only real state setters, never work. Retained objects below own every
        // pointer through capture/retirement. Opaque compute/RT transitions and
        // same-pointer PSO binds stay in order; no numeric defaults are invented.
        std::vector<PipelineCommand> pipelineRecipe;
        bool pipelineBaselineKnown=false,depthBiasObserved=false;
        enum class BiasProvenance { Unknown, ProvenUnusedAfterNullDirectReset, AuthenticHistory };
        BiasProvenance biasProvenance=BiasProvenance::Unknown;
        std::array<ID3D12DescriptorHeap*, 2> heaps{};
        UINT heapCount = 0;
        bool heapsKnown = false;
        std::array<float,4> blendFactor{};
        bool blendKnown = false;
        std::uint64_t blendMutation = 0;
        std::uint64_t blendObservedSequence = 0;
        NativeRecordingPhase blendPhase = NativeRecordingPhase::Caller;
        std::array<std::uint64_t,2> unsupportedEffectBits{};
        Stage compute, graphics;
        struct QueryScope {
            ID3D12QueryHeap* heap; D3D12_QUERY_TYPE type; UINT index;
            std::uint64_t beginObservedWorkOrdinal, beginMutationSerial;
            NativeRecordingPhase beginPhase;
            bool operator==(const QueryScope&) const = default;
        };
        std::vector<QueryScope> queries;
        std::unordered_map<ResourceKey, D3D12_RESOURCE_STATES, ResourceHash> resources;
        struct ResourceFacts
        {
            std::optional<D3D12_RESOURCE_STATES> uniform;
            std::unordered_map<UINT, D3D12_RESOURCE_STATES> subresources;
        };
        // Observations of completed native barrier calls, distinct from the
        // explicitly watched resources which participate in state restoration.
        std::unordered_map<ID3D12Resource*, ResourceFacts> observedResourceStates;
        // Snapshots copy these owning references. A caller releasing its last
        // external reference cannot invalidate a stored restore pointer.
        std::unordered_map<IUnknown*, ObjectOwner> retained;
        // Enrolled root owners are acquired before observer/state locks.
        // Snapshots copy these shared owners without foreign root AddRef.
        std::unordered_map<IUnknown*, RootOwner> roots;
        template<class T> void Retain(T* object,const ObjectOwner& prepared={})
        {
            if(!object||roots.contains(object)||retained.contains(object))return;
            if(!prepared||prepared->Get()!=object)throw std::runtime_error("native prepared object owner unavailable");
            retained.try_emplace(object,prepared);
        }
    };

  public:
    struct Retirement
    {
      private:
        friend class NativeRecordingState;
        State state;
    };
    bool AdoptObjectOwners(std::uint64_t incarnation,const PreparedOwners& prepared)
    {
        std::lock_guard lock(mutex_);
        if(!Matches(incarnation))return false;
        for(UINT i=0;i<prepared.count;++i)state_.Retain(prepared.objects[i],prepared.owners[i]);
        return true;
    }
    struct Snapshot
    {
      private:
        friend class NativeRecordingState;
        State state;
        RestoreMask mask = RestoreMask::None;
        QueryPolicy queryPolicy = QueryPolicy::RequireClosed;
      public:
        std::uint64_t Incarnation() const noexcept { return state.incarnation; }
        UINT HeapCount() const noexcept { return state.heapCount; }
        std::array<ID3D12DescriptorHeap*,2> HeapArguments() const noexcept { return state.heaps; }
    };

    bool HasLayout(ID3D12RootSignature* signature) const
    {
        std::lock_guard lock(mutex_);
        return layouts_.contains(signature);
    }
    RootOwner KnownRootOwner(ID3D12RootSignature* signature) const
    {
        std::lock_guard lock(mutex_);
        const auto found = state_.roots.find(signature);
        if (found != state_.roots.end()) return found->second;
        const auto layoutOwner = layoutOwners_.find(signature);
        return layoutOwner == layoutOwners_.end() ? RootOwner{} : layoutOwner->second;
    }
    bool AdoptRootOwner(std::uint64_t incarnation, ID3D12RootSignature* signature, const RootOwner& owner)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || !signature || !owner || owner->Get() != signature) return false;
        state_.roots.try_emplace(signature, owner);
        return true;
    }
    bool RegisterLayout(ID3D12RootSignature* signature, std::vector<RootParameter> layout,
                        RootOwner preparedOwner = {})
    {
        // The observer supplies an existing owner. Standalone callers acquire
        // theirs before this state lock; rejected owners retire after unlocking.
        if (!signature) return false;
        if (!preparedOwner) preparedOwner = std::make_shared<Microsoft::WRL::ComPtr<ID3D12RootSignature>>(signature);
        if (preparedOwner->Get() != signature) return false;
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
        owner->second = std::move(preparedOwner);
        return true;
    }

    bool Begin(ID3D12GraphicsCommandList* list, std::uint64_t incarnation, ID3D12PipelineState* initialPipeline = nullptr,
               bool directResetDefaults = false)
    {
        PreparedOwners prepared;prepared.Acquire(list);prepared.Acquire(initialPipeline);
        Retirement retired;
        return BeginPrepared(list,incarnation,initialPipeline,directResetDefaults,prepared,retired);
    }
    bool BeginPrepared(ID3D12GraphicsCommandList* list,std::uint64_t incarnation,
                       ID3D12PipelineState* initialPipeline,bool directResetDefaults,
                       const PreparedOwners& prepared,Retirement& retired)
    {
        std::lock_guard lock(mutex_);
        if (!list || !incarnation || retired.state.list || (state_.list && state_.list != list) ||
            (state_.list && incarnation <= state_.incarnation))return false;
        AdvanceMutation();
        retired.state=std::move(state_);state_={};
        state_.list=list;
        for(UINT i=0;i<prepared.count;++i)state_.Retain(prepared.objects[i],prepared.owners[i]);
        if(!state_.retained.contains(list)||(initialPipeline&&!state_.retained.contains(initialPipeline)))return false;
        state_.incarnation = incarnation;
        state_.pipeline = initialPipeline;
        state_.pipelineKnown = initialPipeline != nullptr;
        if(initialPipeline){
            if(!AppendPipelineCommand({25,initialPipeline,nullptr,{}}))return false;
            state_.pipelineBaselineKnown=true;
            state_.biasProvenance=State::BiasProvenance::AuthenticHistory;
        }else if(directResetDefaults){
            // The runtime dummy is not an invented zero-bias PSO. With complete
            // DIRECT Reset coverage and no host ordinary/bias setter, RT-only
            // continuations have no application rasterizer-bias restore obligation.
            state_.biasProvenance=State::BiasProvenance::ProvenUnusedAfterNullDirectReset;
        }
        // Only an observed successful DIRECT Reset authenticates these defaults.
        // Generic Begin and identity-free Reset must retain unknown blend state.
        if (directResetDefaults)
        {
            state_.blendFactor = {D3D12_DEFAULT_BLEND_FACTOR_RED, D3D12_DEFAULT_BLEND_FACTOR_GREEN,
                                  D3D12_DEFAULT_BLEND_FACTOR_BLUE, D3D12_DEFAULT_BLEND_FACTOR_ALPHA};
            state_.blendKnown = true;
            state_.blendPhase = NativeRecordingPhase::Reset;
        }
        state_.active = true;
        traceTotal_ = 0;
        Trace("begin", reinterpret_cast<std::uint64_t>(initialPipeline), incarnation);
        return true;
    }
    void Close(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (Matches(incarnation)) state_.active = false;
    }
    void Taint(std::uint64_t incarnation, const char* reason = "Unclassified", UINT slot = UINT_MAX)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (Matches(incarnation)) {
            if (slot < 128 && reason && std::strcmp(reason,"UnsupportedEffect") == 0)
                state_.unsupportedEffectBits[slot / 64] |= std::uint64_t{1} << (slot % 64);
            Invalidate(reason, slot);
        }
    }
    void AttributeInvalidation(std::uint64_t incarnation, UINT slot)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation)) return false;
        if (state_.blendMutation == UINT64_MAX || !CopyBlend(values, state_.blendFactor))
        { Invalidate(__func__); return false; }
        ++state_.blendMutation;
        state_.blendKnown = true;
        // The public setter stores four FLOATs. Numeric range/sign does not
        // establish a private protocol or invalidate authentic observed state.
        // Preserve argument bytes without arithmetic, clamping or a value list.
        state_.blendPhase = NativeRecordingPhaseScope::Current();
        Trace("blend-factor",state_.blendMutation,static_cast<unsigned>(state_.blendPhase),values ? 1 : 0);
        state_.blendObservedSequence = traceTotal_;
        std::memcpy(trace_[(traceTotal_ - 1) % trace_.size()].blendBits.data(),state_.blendFactor.data(),sizeof(state_.blendFactor));
        return true; // The original call still passes through exactly once.
    }
    // The optional immutable effects come only from observed successful native
    // signature creation. Missing/unsupported/mismatched metadata retains the
    // original conservative fallback. No GPU argument/count contents are read.
    bool ObserveIndirect(std::uint64_t incarnation, const NativeIndirectEffects* effects = nullptr,
                         std::uint64_t signatureIdentity = 0)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
                binding.rootInvalidated = false;
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
    // A completed enhanced barrier before capture does not erase binding
    // knowledge. This observes ordering only: it neither translates layouts
    // into legacy resource states nor certifies resource contents/completion.
    bool ObserveEnhancedBarriers(std::uint64_t incarnation, UINT count, const D3D12_BARRIER_GROUP* groups)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation) || state_.tainted) return false;
        const auto refuse = [&] { Invalidate("ObserveEnhancedBarriers", 80); return false; };
        if (count && !groups) return refuse();
        bool effect = false;
        const auto split = [](auto before, auto after) {
            return ((before | after) & D3D12_BARRIER_SYNC_SPLIT) != 0;
        };
        // Different resource pointers can share placed/reserved memory. This
        // journal owns no allocation-disjointness proof, so resource-specific
        // enhanced work cannot preserve any existing watched legacy authority.
        // With no watched resources, binding-only admission remains available.
        const bool hasWatchedResources = !state_.resources.empty();
        for (UINT g = 0; g < count; ++g)
        {
            const auto& group = groups[g];
            switch (group.Type)
            {
            case D3D12_BARRIER_TYPE_GLOBAL:
                if (group.NumBarriers && !group.pGlobalBarriers) return refuse();
                for (UINT n = 0; n < group.NumBarriers; ++n)
                    if (split(group.pGlobalBarriers[n].SyncBefore, group.pGlobalBarriers[n].SyncAfter)) return refuse();
                break;
            case D3D12_BARRIER_TYPE_BUFFER:
                if (group.NumBarriers && !group.pBufferBarriers) return refuse();
                for (UINT n = 0; n < group.NumBarriers; ++n)
                {
                    const auto& b = group.pBufferBarriers[n];
                    if (!b.pResource || split(b.SyncBefore, b.SyncAfter) || hasWatchedResources) return refuse();
                }
                break;
            case D3D12_BARRIER_TYPE_TEXTURE:
                if (group.NumBarriers && !group.pTextureBarriers) return refuse();
                for (UINT n = 0; n < group.NumBarriers; ++n)
                {
                    const auto& b = group.pTextureBarriers[n];
                    if (!b.pResource || split(b.SyncBefore, b.SyncAfter) || hasWatchedResources ||
                        (b.Flags & ~D3D12_TEXTURE_BARRIER_FLAG_DISCARD) != 0) return refuse();
                }
                break;
            default: return refuse();
            }
            effect |= group.NumBarriers != 0;
        }
        if (!effect) return true;
        if (state_.workOrdinal == UINT64_MAX || state_.unrestorableGraphicsOrdinal == UINT64_MAX) return refuse();
        ++state_.workOrdinal;
        ++state_.unrestorableGraphicsOrdinal;
        Trace("enhanced-barrier", count, state_.unrestorableGraphicsOrdinal);
        state_.lastUnrestorableSlot = 80;
        state_.lastUnrestorableSequence = traceTotal_;
        state_.lastUnrestorableOrdinal = state_.unrestorableGraphicsOrdinal;
        state_.lastUnrestorableWorkOrdinal = state_.workOrdinal;
        state_.lastUnrestorableA = count; state_.lastUnrestorableB = state_.lastUnrestorableC = 0;
        return true;
    }
    bool ObserveWork(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation) || state_.tainted || state_.workOrdinal == UINT64_MAX)
        {
            if (Matches(incarnation)) Invalidate(__func__);
            return false;
        }
        ++state_.workOrdinal;
        return true;
    }
    bool ObserveWriteBufferImmediate(std::uint64_t incarnation)
    {
        // Data work remains in the original stream. Binding restoration never
        // repeats the writes or assumes the destination is a breadcrumb buffer.
        return ObserveWork(incarnation);
    }
    std::optional<std::uint64_t> Activity(ID3D12GraphicsCommandList* list, std::uint64_t incarnation) const
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.list != list || state_.tainted || restoreInProgress_) return {};
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
        PreparedOwners prepared;if(begin)prepared.Acquire(heap);
        if(!AdoptObjectOwners(incarnation,prepared))return false;
        return ObserveQueryPrepared(incarnation,heap,type,index,begin);
    }
    bool ObserveQueryPrepared(std::uint64_t incarnation, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index, bool begin)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation) || state_.tainted) return false;
        const auto refuse = [&] { Invalidate("ObserveQuery"); return false; };
        if (!heap || !SupportedQuery(type) || state_.workOrdinal == UINT64_MAX) return refuse();
        const auto found = std::find_if(state_.queries.begin(),state_.queries.end(),[&](const auto& query) {
            return query.heap == heap && query.index == index;
        });
        if (begin) {
            if (type == D3D12_QUERY_TYPE_TIMESTAMP || found != state_.queries.end() || state_.queries.size() >= 64) return refuse();
            state_.Retain(heap);
            state_.queries.push_back({heap,type,index,state_.workOrdinal+1,mutationSerial_,NativeRecordingPhaseScope::Current()});
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
    bool ObserveDepthBias(std::uint64_t incarnation,float bias,float clamp,float slope)
    {
        std::lock_guard lock(mutex_);if(Matches(incarnation))AdvanceMutation();
        if(!Matches(incarnation))return false;
        const std::array<std::uint32_t,3> bits={std::bit_cast<std::uint32_t>(bias),
            std::bit_cast<std::uint32_t>(clamp),std::bit_cast<std::uint32_t>(slope)};
        if(!AppendPipelineCommand({82,nullptr,nullptr,bits}))return false;
        state_.depthBiasObserved=true;
        if(!state_.pipelineBaselineKnown)state_.biasProvenance=State::BiasProvenance::Unknown;
        Trace("depth-bias",bits[0],bits[1],bits[2]);return true;
    }
    bool ObservePipeline(std::uint64_t incarnation, ID3D12PipelineState* pipeline)
    {
        PreparedOwners prepared;prepared.Acquire(pipeline);
        if(!AdoptObjectOwners(incarnation,prepared))return false;
        return ObservePipelinePrepared(incarnation,pipeline);
    }
    bool ObservePipelinePrepared(std::uint64_t incarnation, ID3D12PipelineState* pipeline)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation)) return false;
        if (!pipeline) { Invalidate(__func__); return false; }
        // A genuine ordinary assignment supersedes prior bias/RT obligations.
        // Graphics defines its described bias; compute cannot rasterize before
        // another defining graphics bind. No object-type or numeric guess needed.
        state_.pipelineRecipe.clear();
        if(!AppendPipelineCommand({25,pipeline,nullptr,{}}))return false;
        state_.pipelineBaselineKnown=true;
        state_.depthBiasObserved=false;
        state_.biasProvenance=State::BiasProvenance::AuthenticHistory;
        state_.pipeline = pipeline;
        state_.stateObject = nullptr;
        state_.pipelineKind = State::PipelineKind::Ordinary;
        state_.Retain(pipeline);
        state_.pipelineKnown = true;
        Trace("pipeline", reinterpret_cast<std::uint64_t>(pipeline));
        return true;
    }
    bool ObserveStateObject(std::uint64_t incarnation, ID3D12StateObject* object)
    {
        PreparedOwners prepared;prepared.Acquire(object);
        if(!AdoptObjectOwners(incarnation,prepared))return false;
        return ObserveStateObjectPrepared(incarnation,object);
    }
    bool ObserveStateObjectPrepared(std::uint64_t incarnation, ID3D12StateObject* object)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
        if (!Matches(incarnation)) return false;
        if (!object) { Invalidate(__func__); return false; }
        state_.Retain(object);
        if(!AppendPipelineCommand({75,nullptr,object,{}}))return false;
        state_.stateObject = object;
        state_.pipeline = nullptr;
        state_.pipelineKind = State::PipelineKind::StateObject;
        state_.pipelineKnown = true;
        Trace("pipeline-state-object",reinterpret_cast<std::uint64_t>(object));
        return true;
    }
    bool ObserveDescriptorHeaps(std::uint64_t incarnation, UINT count, ID3D12DescriptorHeap* const* heaps)
    {
        PreparedOwners prepared;if(count<=2&&(!count||heaps))for(UINT i=0;i<count;++i)prepared.Acquire(heaps[i]);
        if(!AdoptObjectOwners(incarnation,prepared))return false;
        return ObserveDescriptorHeapsPrepared(incarnation,count,heaps);
    }
    bool ObserveDescriptorHeapsPrepared(std::uint64_t incarnation, UINT count, ID3D12DescriptorHeap* const* heaps)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
            // The set is unchanged, so tables remain valid. Preserve the exact
            // latest argument order for the authenticated outer replay bridge.
            for(UINT i=0;i<count;++i)state_.heaps[i]=heaps[i];
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
    {
        auto owner=KnownRootOwner(signature);
        if(signature&&!owner)owner=std::make_shared<Microsoft::WRL::ComPtr<ID3D12RootSignature>>(signature);
        if(signature&&!AdoptRootOwner(incarnation,signature,owner))return false;
        return ObserveComputeRootSignaturePrepared(incarnation,signature);
    }
    bool ObserveComputeRootSignaturePrepared(std::uint64_t incarnation, ID3D12RootSignature* signature)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveSignature(incarnation, state_.compute, signature); }
    bool ObserveGraphicsRootSignature(std::uint64_t incarnation, ID3D12RootSignature* signature)
    {
        auto owner=KnownRootOwner(signature);
        if(signature&&!owner)owner=std::make_shared<Microsoft::WRL::ComPtr<ID3D12RootSignature>>(signature);
        if(signature&&!AdoptRootOwner(incarnation,signature,owner))return false;
        return ObserveGraphicsRootSignaturePrepared(incarnation,signature);
    }
    bool ObserveGraphicsRootSignaturePrepared(std::uint64_t incarnation, ID3D12RootSignature* signature)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveSignature(incarnation, state_.graphics, signature); }
    bool ObserveComputeTable(std::uint64_t incarnation, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveTable(incarnation, state_.compute, index, table); }
    bool ObserveGraphicsTable(std::uint64_t incarnation, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveTable(incarnation, state_.graphics, index, table); }
    bool ObserveComputeConstants(std::uint64_t incarnation, UINT index, UINT count, const void* values, UINT offset)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveConstants(incarnation, state_.compute, index, count, values, offset); }
    bool ObserveGraphicsConstants(std::uint64_t incarnation, UINT index, UINT count, const void* values, UINT offset)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveConstants(incarnation, state_.graphics, index, count, values, offset); }
    bool ObserveComputeDescriptor(std::uint64_t incarnation, UINT index, RootKind kind, D3D12_GPU_VIRTUAL_ADDRESS address)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveDescriptor(incarnation, state_.compute, index, kind, address); }
    bool ObserveGraphicsDescriptor(std::uint64_t incarnation, UINT index, RootKind kind, D3D12_GPU_VIRTUAL_ADDRESS address)
    { std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation(); return ObserveDescriptor(incarnation, state_.graphics, index, kind, address); }
    // Register only an authoritative known state for a resource the inserted
    // operation may transition. The generation prevents pointer reuse.
    std::uint64_t ResourceFactSerial(std::uint64_t incarnation) const
    {
        std::lock_guard lock(mutex_);
        return Matches(incarnation) && !state_.tainted ? mutationSerial_ : 0;
    }
    void ForgetResourceFacts(std::uint64_t incarnation)
    {
        std::lock_guard lock(mutex_);
        if (Matches(incarnation)) { state_.observedResourceStates.clear(); AdvanceMutation(); }
    }
    bool CompleteBarriers(std::uint64_t incarnation, std::uint64_t serial, const PreparedBarriers& prepared)
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.tainted) return false;
        // Reentrant changes during the original call make its relative order
        // unknown. Forget facts; never publish its earlier StateAfter as latest.
        if (!prepared.valid || !serial || serial != mutationSerial_)
        { state_.observedResourceStates.clear(); return false; }
        for (const auto& [object, owner] : prepared.owners) state_.Retain(object, owner);
        for (const auto& barrier : prepared.barriers)
        {
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
            {
                if (!barrier.Aliasing.pResourceBefore || !barrier.Aliasing.pResourceAfter)
                    state_.observedResourceStates.clear();
                else
                { state_.observedResourceStates.erase(barrier.Aliasing.pResourceBefore); state_.observedResourceStates.erase(barrier.Aliasing.pResourceAfter); }
            }
            else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                const auto& transition = barrier.Transition;
                if (!transition.pResource) { state_.observedResourceStates.clear(); return false; }
                if (barrier.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE)
                { state_.observedResourceStates.erase(transition.pResource); continue; }
                auto& facts = state_.observedResourceStates[transition.pResource];
                if (transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                { facts.uniform = transition.StateAfter; facts.subresources.clear(); }
                else facts.subresources[transition.Subresource] = transition.StateAfter;
            }
            else if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_UAV)
            { state_.observedResourceStates.clear(); return false; }
        }
        return true;
    }
    std::optional<D3D12_RESOURCE_STATES> KnownResourceState(
        std::uint64_t incarnation, std::uint64_t workOrdinal, ID3D12Resource* resource,
        UINT subresource, UINT totalSubresources) const
    {
        std::lock_guard lock(mutex_);
        return KnownResourceStateLocked(incarnation,workOrdinal,resource,subresource,totalSubresources);
    }
    bool KnownResourceStates(std::uint64_t incarnation, std::uint64_t workOrdinal,
        std::span<const NativeResourceStateQuery> queries,
        std::span<std::optional<D3D12_RESOURCE_STATES>> values, std::uint64_t& mutationSerial) const
    {
        std::lock_guard lock(mutex_);
        if(queries.size()!=values.size()||!Matches(incarnation)||state_.tainted||
           restoreInProgress_||state_.workOrdinal!=workOrdinal)return false;
        for(std::size_t i=0;i<queries.size();++i)
            values[i]=KnownResourceStateLocked(incarnation,workOrdinal,queries[i].resource,
                queries[i].subresource,queries[i].totalSubresources);
        mutationSerial=mutationSerial_;
        return true;
    }
    bool ResourceFactsCurrent(std::uint64_t incarnation, std::uint64_t workOrdinal,
                              std::uint64_t mutationSerial) const
    {
        std::lock_guard lock(mutex_);
        return Matches(incarnation)&&!state_.tainted&&!restoreInProgress_&&
            state_.workOrdinal==workOrdinal&&mutationSerial_==mutationSerial;
    }
    bool ReadResourceFactCursor(std::uint64_t incarnation,std::uint64_t& workOrdinal,
                                std::uint64_t& mutationSerial) const
    {
        std::lock_guard lock(mutex_);
        if(!Matches(incarnation)||state_.tainted||restoreInProgress_)return false;
        workOrdinal=state_.workOrdinal;mutationSerial=mutationSerial_;return true;
    }
  private:
    std::optional<D3D12_RESOURCE_STATES> KnownResourceStateLocked(
        std::uint64_t incarnation, std::uint64_t workOrdinal, ID3D12Resource* resource,
        UINT subresource, UINT totalSubresources) const
    {
        if (!Matches(incarnation) || state_.tainted || !resource || state_.workOrdinal != workOrdinal) return {};
        const auto found = state_.observedResourceStates.find(resource);
        if (found == state_.observedResourceStates.end()) return {};
        const auto& facts = found->second;
        if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            if (!totalSubresources || subresource >= totalSubresources) return {};
            const auto specific = facts.subresources.find(subresource);
            return specific == facts.subresources.end() ? facts.uniform : std::optional(specific->second);
        }
        // A uniform ALL transition covers every plane/mip/array slice. Without
        // one, the exact resource plane count must establish complete coverage.
        if (!facts.uniform && (!totalSubresources || facts.subresources.size() != totalSubresources)) return {};
        auto value = totalSubresources && facts.subresources.size() == totalSubresources
            ? std::optional<D3D12_RESOURCE_STATES>{} : facts.uniform;
        for (const auto& [index, state] : facts.subresources)
        {
            if (totalSubresources && index >= totalSubresources) return {};
            if (value && *value != state) return {};
            value = state;
        }
        return value;
    }
  public:
    std::optional<D3D12_RESOURCE_STATES> KnownSingleSubresourceState(
        std::uint64_t incarnation, ID3D12Resource* resource) const
    {
        if(!resource)return {};
        const auto desc=resource->GetDesc();
        return KnownSingleSubresourceStatePrepared(incarnation,resource,desc);
    }
    std::optional<D3D12_RESOURCE_STATES> KnownSingleSubresourceStatePrepared(
        std::uint64_t incarnation,ID3D12Resource* resource,const D3D12_RESOURCE_DESC& desc)const
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || !state_.active || state_.tainted || !resource) return {};
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
        PreparedOwners prepared;prepared.Acquire(resource);
        if(!AdoptObjectOwners(incarnation,prepared))return false;
        return WatchResourcePrepared(incarnation,resource,generation,subresource,knownState);
    }
    bool WatchResourcePrepared(std::uint64_t incarnation, ID3D12Resource* resource, std::uint64_t generation,
                       UINT subresource, D3D12_RESOURCE_STATES knownState)
    {
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
        std::lock_guard lock(mutex_); if (Matches(incarnation)) AdvanceMutation();
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
                                    NativeStateCaptureDiagnostic* diagnostic = nullptr,
                                    QueryPolicy queryPolicy = QueryPolicy::RequireClosed) const
    {
        std::lock_guard lock(mutex_);
        if (diagnostic) FillCaptureDiagnostic(diagnostic);
        const auto refuse = [&](const char* reason) -> std::optional<Snapshot> {
            if (diagnostic) diagnostic->reason = reason;
            return {};
        };
        if (restoreInProgress_) return refuse("Native.State.RestoreInProgress");
        if (!Matches(incarnation) || list != state_.list || state_.tainted || mask == RestoreMask::None)
            return refuse("Native.State.InactiveOrTainted");
        if (!state_.queries.empty() &&
            (queryPolicy != QueryPolicy::PreservePipelineStatistics ||
             !std::all_of(state_.queries.begin(), state_.queries.end(), [](const auto& query) {
                 return query.type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS ||
                        query.type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS1;
             }))) return refuse("Native.State.QueryOpen");
        if (Has(mask, RestoreMask::Pipeline) && !state_.pipelineKnown) return refuse("Native.State.PipelineUnknown");
        if(Has(mask,RestoreMask::Pipeline)&&!state_.pipelineBaselineKnown&&
            !(state_.biasProvenance==State::BiasProvenance::ProvenUnusedAfterNullDirectReset&&
              state_.pipelineKind==State::PipelineKind::StateObject&&!state_.depthBiasObserved))
            return refuse("Native.State.DepthBiasBaselineUnknown");
        if (Has(mask, RestoreMask::Heaps) && !state_.heapsKnown) return refuse("Native.State.HeapsUnknown");
        if (Has(mask, RestoreMask::BlendFactor) && !state_.blendKnown)
            return refuse("Native.State.BlendFactorUnknown");
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
            if (!CompleteTables(state_.compute, mask))
            { DiagnoseStage(state_.compute, true, true, diagnostic, mask); return {}; }
            if (!CompleteTables(state_.graphics, mask))
            { DiagnoseStage(state_.graphics, false, true, diagnostic, mask); return {}; }
        }
        Snapshot result;
        result.state = state_;
        result.mask = mask;
        result.queryPolicy = queryPolicy;
        return result;
    }

    std::optional<Snapshot> CaptureRestorable(ID3D12GraphicsCommandList* list, std::uint64_t incarnation,
                                              RestoreMask mask, const OriginalSetters& original,
                                              NativeStateCaptureDiagnostic* diagnostic = nullptr,
                                              QueryPolicy queryPolicy = QueryPolicy::RequireClosed) const
    {
        auto snapshot = Capture(list, incarnation, mask, diagnostic, queryPolicy);
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
                 RestoreDiagnostic* diagnostic = nullptr) try
    {
        OriginalSetters replay = original;
        struct LeaseRetirement {
            NativeRecordingState& owner; bool started = false;
            ~LeaseRetirement() { if (started) { std::lock_guard lock(owner.mutex_); owner.restoreInProgress_ = false; } }
        } retirement{*this};
        std::unique_lock lock(mutex_);
        const auto& saved = snapshot.state;
        if (diagnostic) { *diagnostic = {}; FillRestoreDiagnostic(snapshot, diagnostic, false); }
        auto refuse = [&](const char* reason) {
            if (diagnostic) { FillRestoreDiagnostic(snapshot, diagnostic, true); diagnostic->reason = reason; }
            return false;
        };
        if (restoreInProgress_ || mutationSerial_ == UINT64_MAX)
            return refuse("Native.Restore.LeaseUnavailable");
        if (!Matches(saved.incarnation) || state_.list != saved.list || state_.tainted || !saved.active)
            return refuse("Native.Restore.InactiveOrTainted");
        if (snapshot.queryPolicy == QueryPolicy::PreservePipelineStatistics)
        {
            // Compare live Begin identities, not counter values or a global
            // mutation total. Balanced independent queries may have executed;
            // ending/restarting even the same caller element is not continuity.
            // The current query journal is never replaced by the snapshot.
            if (state_.queries != saved.queries) return refuse("Native.Restore.QueryScopeChanged");
        }
        else if (!state_.queries.empty() || !saved.queries.empty()) return refuse("Native.Restore.QueryOpen");
        if (state_.discardOrdinal != saved.discardOrdinal)
        { Invalidate("Native.Restore.DiscardBoundary", 51); return refuse("Native.Restore.DiscardBoundary"); }
        if ((!Has(snapshot.mask, RestoreMask::BlendFactor) && state_.blendMutation != saved.blendMutation) ||
            state_.blendMutation == UINT64_MAX)
        { Invalidate("Native.Restore.BlendFactorBoundary"); return refuse("Native.Restore.BlendFactorBoundary"); }
        // Pipeline/bias outside this requested mask remain the authentic CURRENT
        // values. Existing mutationSerial_ still rejects any reentrant mutation
        // during replay; a masked restore never rewinds unrelated provenance.
        if(Has(snapshot.mask,RestoreMask::Pipeline)&&!saved.pipelineBaselineKnown&&
            saved.biasProvenance!=State::BiasProvenance::ProvenUnusedAfterNullDirectReset&&
            state_.depthBiasObserved!=saved.depthBiasObserved)
        {Invalidate("Native.Restore.DepthBiasBaselineUnknown");return refuse("Native.Restore.DepthBiasBaselineUnknown");}
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
        // Freeze current resource transitions before releasing the lock. No
        // mutable journal container may be read across a foreign callback.
        std::vector<D3D12_RESOURCE_BARRIER> transitions;
        for (const auto& [key, prior] : saved.resources)
        {
            const auto after = state_.resources.at(key);
            if (after == prior) continue;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {key.resource, key.subresource, after, prior};
            transitions.push_back(barrier);
        }
        const auto serial = mutationSerial_;
        restoreInProgress_ = retirement.started = true;
        lock.unlock();
        const auto valid = [&] {
            {
                std::lock_guard stateLock(mutex_);
                if (!Matches(saved.incarnation) || state_.list != saved.list || state_.tainted ||
                    mutationSerial_ != serial) return false;
            }
            return !original.validate || original.validate();
        };
        struct ReplayLeaseLost {};
        const auto protect = [&](auto& function) {
            if (!function) return;
            const auto dispatch = function;
            function = [dispatch, valid](auto... arguments) {
                if (!valid()) throw ReplayLeaseLost{};
                dispatch(arguments...);
                // Check after EACH dispatch: a real callback Reset must stop
                // the next old-state setter before it enters the new epoch.
                if (!valid()) throw ReplayLeaseLost{};
            };
        };
        try
        {
            protect(replay.heaps); protect(replay.pipeline); protect(replay.stateObject);
            protect(replay.computeSignature); protect(replay.graphicsSignature);
            protect(replay.computeTable); protect(replay.graphicsTable);
            protect(replay.computeConstants); protect(replay.graphicsConstants);
            protect(replay.computeCbv); protect(replay.computeSrv); protect(replay.computeUav);
            protect(replay.graphicsCbv); protect(replay.graphicsSrv); protect(replay.graphicsUav);
            protect(replay.barriers); protect(replay.blendFactor); protect(replay.depthBias);
            auto* list = saved.list;
            // Force real table invalidation even if inserted work leaves exactly
            // the same heap array bound. Then replay only authenticated tables.
            if ((Has(snapshot.mask, RestoreMask::HeapInvalidatedTables) || Has(snapshot.mask, RestoreMask::RootInvalidatedTables)) &&
                (HasInvalidatedTables(saved.compute) || HasInvalidatedTables(saved.graphics)))
                replay.heaps(list, 0, saved.heaps.data());
            if (Has(snapshot.mask, RestoreMask::Heaps)) replay.heaps(list, saved.heapCount, saved.heaps.data());
            if (Has(snapshot.mask, RestoreMask::Pipeline)) {
                if(saved.pipelineBaselineKnown){
                    for(const auto& command:saved.pipelineRecipe){
                        if(command.slot==25)replay.pipeline(list,command.pipeline);
                        else if(command.slot==75)replay.stateObject(list,command.stateObject);
                        else replay.depthBias(list,std::bit_cast<float>(command.biasBits[0]),
                            std::bit_cast<float>(command.biasBits[1]),std::bit_cast<float>(command.biasBits[2]));
                    }
                }else if (saved.pipelineKind == State::PipelineKind::StateObject) replay.stateObject(list, saved.stateObject);
                else replay.pipeline(list, saved.pipeline);
            }
            // Inserted work may leave the SAME signature bound and populate
            // previously undefined arguments. A redundant set preserves those
            // values: use a genuine null -> saved root transition, then replay
            // only the caller's defined arguments. Every call stays protected
            // by the existing lease and terminal-effect validation. A stage
            // participating only through heap/table replay is never reset here.
            if (Has(snapshot.mask, RestoreMask::Compute))
            {
                if (Has(snapshot.mask, RestoreMask::RootInvalidatedArguments) && HasRootInvalidatedArguments(saved.compute))
                    replay.computeSignature(list, nullptr);
                replay.computeSignature(list, saved.compute.signature);
            }
            if (Has(snapshot.mask, RestoreMask::Graphics))
            {
                if (Has(snapshot.mask, RestoreMask::RootInvalidatedArguments) && HasRootInvalidatedArguments(saved.graphics))
                    replay.graphicsSignature(list, nullptr);
                replay.graphicsSignature(list, saved.graphics.signature);
            }
            if (Has(snapshot.mask, RestoreMask::Compute) || Has(snapshot.mask, RestoreMask::Heaps))
                RestoreBindings(list, saved.compute, replay, true, Has(snapshot.mask, RestoreMask::Compute));
            if (Has(snapshot.mask, RestoreMask::Graphics) || Has(snapshot.mask, RestoreMask::Heaps))
                RestoreBindings(list, saved.graphics, replay, false, Has(snapshot.mask, RestoreMask::Graphics));
            if (Has(snapshot.mask, RestoreMask::BlendFactor)) replay.blendFactor(list, saved.blendFactor.data());
            for (const auto& barrier : transitions) replay.barriers(list, 1, &barrier);
            const auto commit = [&] {
                std::lock_guard stateLock(mutex_);
                if (!Matches(saved.incarnation) || state_.list != saved.list || state_.tainted ||
                    mutationSerial_ != serial) return false;
            // Only the values actually restored may replace current knowledge.
            // In particular, do not rewind work or unmasked state to the snapshot.
            if (Has(snapshot.mask, RestoreMask::Pipeline))
            {
                state_.pipeline = saved.pipeline;
                state_.stateObject = saved.stateObject;
                state_.pipelineKind = saved.pipelineKind;
                state_.pipelineKnown = saved.pipelineKnown;
                state_.pipelineRecipe=saved.pipelineRecipe;
                state_.pipelineBaselineKnown=saved.pipelineBaselineKnown;
                state_.depthBiasObserved=saved.depthBiasObserved;
                // Commit the restored HOST provenance, not the inserted setters.
                // The existing global mutation serial advances at commit below.
                state_.biasProvenance=saved.biasProvenance;
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
                state_.blendPhase = saved.blendPhase;
                state_.blendObservedSequence = saved.blendObservedSequence;
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
            // Original replay setters bypass the ordinary post-call hook. Join
            // only the resource transitions actually completed by this restore.
            for (const auto& barrier : transitions)
            {
                const auto& transition = barrier.Transition;
                auto& facts = state_.observedResourceStates[transition.pResource];
                if (transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                { facts.uniform=transition.StateAfter; facts.subresources.clear(); }
                else facts.subresources[transition.Subresource]=transition.StateAfter;
            }
            Trace("restore", static_cast<std::uint64_t>(snapshot.mask));
            AdvanceMutation();
            if (diagnostic) diagnostic->reason = "Native.Restore.Restored";
            return true;
            };
            if (original.commit ? original.commit(commit) : commit()) return true;
            throw ReplayLeaseLost{};
        }
        catch (const NativeReplayFailure& failure)
        {
            lock.lock();
            if (Matches(saved.incarnation)) Invalidate(failure.reason, failure.slot);
            return refuse(failure.reason);
        }
        catch (const ReplayLeaseLost&)
        {
            lock.lock();
            if (Matches(saved.incarnation)) Invalidate("Native.Restore.ReplayLeaseLost");
            return refuse("Native.Restore.ReplayLeaseLost");
        }
        catch (...)
        {
            lock.lock();
            if (Matches(saved.incarnation)) Invalidate("Native.Restore.ReplayException");
            return refuse("Native.Restore.ReplayException");
        }
    }
    catch (...)
    {
        if (diagnostic) diagnostic->reason = "Native.Restore.PreparationException";
        return false;
    }

  private:
    friend class NativeRecordingObserver;
    // Internal live-setter evidence, never work/capture admission. Public
    // Activity/Capture remain unavailable throughout this entry's replay.
    std::optional<std::uint64_t> HeapBindingEpoch(ID3D12GraphicsCommandList* list, std::uint64_t incarnation) const
    {
        std::lock_guard lock(mutex_);
        if (!Matches(incarnation) || state_.list != list || state_.tainted) return {};
        return incarnation;
    }
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
        d->blendKnown = state_.blendKnown;
        std::memcpy(d->blendBits.data(),state_.blendFactor.data(),sizeof(d->blendBits));
        d->blendPhase = state_.blendPhase;
        d->blendMutation = state_.blendMutation;
        d->blendObservedSequence = state_.blendObservedSequence;
        d->unsupportedEffectBits = state_.unsupportedEffectBits;
        d->openQueryTotal = static_cast<UINT>(state_.queries.size());
        d->openQueryDetails = std::min<UINT>(d->openQueryTotal,static_cast<UINT>(d->openQueries.size()));
        d->queryCaptureWorkOrdinal = state_.workOrdinal;
        for(UINT i=0;i<d->openQueryDetails;++i)
        {
            const auto& query=state_.queries[i];
            d->openQueries[i]={reinterpret_cast<std::uint64_t>(query.heap),query.type,query.index,
                query.beginObservedWorkOrdinal,query.beginMutationSerial,query.beginPhase};
        }
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
        const auto it = layouts_.find(signature);
        // The first actual null set must remain distinct from an untouched
        // Reset-unbound stage when checking tables affected by heap replay.
        const bool changed = stage.signature != signature;
        const bool firstUnbound = !signature && !stage.mutationOrdinal;
        if (!changed && !firstUnbound && (stage.signatureKnown || it == layouts_.end()))
        {
            Trace(&stage==&state_.compute ? "compute-signature-same" : "graphics-signature-same",
                reinterpret_cast<std::uint64_t>(signature), it == layouts_.end() ? 0 : it->second.size());
            return true;
        }
        if (changed || firstUnbound)
        {
            ++stage.mutationOrdinal;
            stage.signature = signature;
            if (const auto owner = layoutOwners_.find(signature); owner != layoutOwners_.end())
                state_.roots.try_emplace(signature, owner->second);
            if(signature&&!state_.roots.contains(signature))
            {Invalidate("Native.State.RootOwnerUnavailable");return false;}
        }
        // Missing immutable layout metadata describes current knowledge only.
        // It cannot invalidate an authentic saved stage or clear other taint.
        stage.signatureKnown = it != layouts_.end();
        stage.bindings.clear();
        if (!stage.signatureKnown)
        {
            Trace(&stage==&state_.compute ? "compute-signature-unknown" : "graphics-signature-unknown",
                reinterpret_cast<std::uint64_t>(signature), changed);
            return true;
        }
        Trace(changed ? (&stage==&state_.compute ? "compute-signature-changed" : "graphics-signature-changed") :
                        (&stage==&state_.compute ? "compute-signature-layout" : "graphics-signature-layout"),
            reinterpret_cast<std::uint64_t>(signature), it->second.size());
        for (auto p : it->second)
        {
            Binding b;
            b.kind = p.kind;
            // Late metadata plus a redundant root set supplies layout shape,
            // never provenance for arguments invalidated by a real root change.
            b.tableRootInvalidated = changed && p.kind == RootKind::Table;
            b.rootInvalidated = changed && p.kind != RootKind::Table;
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
        if (!stage.signature || !state_.heapsKnown || !table.ptr)
        { Invalidate(__func__); return false; }
        if (!stage.signatureKnown)
        {
            Trace(&stage==&state_.compute ? "compute-table-unknown" : "graphics-table-unknown", index, table.ptr);
            return true;
        }
        if (index >= stage.bindings.size() || stage.bindings[index].kind != RootKind::Table)
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
        if (!stage.signature || !values || !count || count > UINT_MAX - offset)
        { Invalidate(__func__); return false; }
        if (!stage.signatureKnown)
        {
            // Do not read payload or apply the previous root's parameter bounds.
            Trace(&stage==&state_.compute ? "compute-constants-unknown" : "graphics-constants-unknown", index, count, offset);
            return true;
        }
        if (index >= stage.bindings.size() || stage.bindings[index].kind != RootKind::Constants)
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
        if (!stage.signature || (kind != RootKind::CBV && kind != RootKind::SRV && kind != RootKind::UAV))
        { Invalidate(__func__); return false; }
        if (!stage.signatureKnown)
        {
            Trace(&stage==&state_.compute ? "compute-descriptor-unknown" : "graphics-descriptor-unknown", index, address,
                static_cast<std::uint64_t>(kind));
            return true;
        }
        if (index >= stage.bindings.size() || stage.bindings[index].kind != kind)
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
    static bool HasRootInvalidatedArguments(const Stage& stage)
    {
        return std::any_of(stage.bindings.begin(), stage.bindings.end(), [](const Binding& b) {
            return b.kind != RootKind::Table && !b.known && b.rootInvalidated;
        });
    }
    static bool RestorableBinding(const Binding& b, RestoreMask mask)
    {
        return b.known || (b.kind != RootKind::Table && b.rootInvalidated &&
            Has(mask, RestoreMask::RootInvalidatedArguments)) || (b.kind == RootKind::Table &&
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
        // Only the untouched Reset-unbound stage can have no known layout yet
        // safely contribute no tables to a heap snapshot or replay.
        if (!stage.signatureKnown)
            return !stage.signature && !stage.mutationOrdinal && stage.bindings.empty();
        return std::all_of(stage.bindings.begin(), stage.bindings.end(),
                           [mask](const Binding& b) { return b.kind != RootKind::Table || RestorableBinding(b, mask); });
    }
    static bool CanBindings(const Stage& stage, const OriginalSetters& o, bool compute, bool all, RestoreMask mask)
    {
        if (all ? !Complete(stage, mask) : !CompleteTables(stage, mask)) return false;
        for (const auto& b : stage.bindings)
        {
            if (!all && b.kind != RootKind::Table) continue;
            if (!b.known)
            {
                if (!RestorableBinding(b, mask)) return false;
                // A partially defined constant still needs a real setter for
                // its observed words, even though the whole slot is not known.
                if (b.kind != RootKind::Constants ||
                    std::none_of(b.written.begin(), b.written.end(), [](bool v) { return v; })) continue;
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
            // Admission has proved every non-known argument either genuinely
            // undefined or a partial constant. Never emit default-filled data.
            if (!b.known && b.kind != RootKind::Constants) continue;
            switch (b.kind)
            {
            case RootKind::Table: (compute ? o.computeTable : o.graphicsTable)(list, i, b.table); break;
            case RootKind::Constants:
                for (UINT word = 0; word < b.written.size();)
                {
                    if (!b.written[word]) { ++word; continue; }
                    const UINT start = word;
                    while (word < b.written.size() && b.written[word]) ++word;
                    (compute ? o.computeConstants : o.graphicsConstants)(
                        list, i, word - start, b.constants.data() + start, start);
                }
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
        if (Has(s.mask, RestoreMask::Pipeline) &&
            (!x.pipelineKnown || (x.pipelineKind == State::PipelineKind::StateObject ?
                (!x.stateObject || !o.stateObject) : (!x.pipeline || !o.pipeline)))) return false;
        if(Has(s.mask,RestoreMask::Pipeline)&&x.pipelineBaselineKnown){
            if(x.pipelineRecipe.empty()||x.pipelineRecipe.front().slot!=25)return false;
            for(const auto& command:x.pipelineRecipe){
                if(command.slot==25){if(!command.pipeline||!o.pipeline)return false;}
                else if(command.slot==75){if(!command.stateObject||!o.stateObject)return false;}
                else if(command.slot==82){if(!o.depthBias)return false;}
                else return false;
            }
        }
        if (Has(s.mask, RestoreMask::Compute) && !o.computeSignature) return false;
        if (Has(s.mask, RestoreMask::Graphics) && !o.graphicsSignature) return false;
        if (Has(s.mask, RestoreMask::BlendFactor) && (!x.blendKnown || !o.blendFactor)) return false;
        if ((Has(s.mask, RestoreMask::Compute) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.compute, o, true, Has(s.mask, RestoreMask::Compute), s.mask)) return false;
        if ((Has(s.mask, RestoreMask::Graphics) || Has(s.mask, RestoreMask::Heaps)) &&
            !CanBindings(x.graphics, o, false, Has(s.mask, RestoreMask::Graphics), s.mask)) return false;
        return true;
    }
    bool AppendPipelineCommand(State::PipelineCommand command)
    {
        try{state_.pipelineRecipe.push_back(command);}
        catch(...){Invalidate("Native.State.PipelineRecipeAllocation");return false;}
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
    std::unordered_map<ID3D12RootSignature*, RootOwner> layoutOwners_;
    void AdvanceMutation() noexcept
    {
        if (mutationSerial_ != UINT64_MAX) ++mutationSerial_;
        else state_.tainted = true;
    }
    mutable std::mutex mutex_;
    std::uint64_t mutationSerial_ = 0;
    bool restoreInProgress_ = false;
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

struct NativeRecordingInterfaces;
struct NativeRecordingObservation
{
    // Observer internals use the terminal list. A rendering facade preserves
    // its execution/caller correlation here and retains the explicit binding;
    // identity->nativeList always stays the actual terminal journal identity.
    ID3D12GraphicsCommandList* nativeList = nullptr;
    std::uint64_t incarnation = 0;
    std::uint64_t workOrdinal = 0;
    bool active = false;
    bool completeCoverage = false;
    std::uint64_t hookGeneration = 0;
    bool trackingBeganBeforeRecording = false;
    bool missingRequiredHistory = true;
    std::shared_ptr<const NativeRecordingIdentity> identity;
    std::shared_ptr<const NativeRecordingInterfaces> interfaces;
};

// The hook owner feeds this adapter only from the actual command-list detours.
// Lists seen before a hooked Reset remain unknown; an address alone does not
// authenticate a recording incarnation.
class NativeRecordingObserver
{
    friend class NativeWrapperHeapBindings;
    struct Entry
    {
        std::uint64_t incarnation = 0;
        std::uint64_t hookGeneration = 0;
        bool completeAtReset = false;
        std::shared_ptr<const NativeRecordingIdentity> identity;
        NativeRecordingState state;
    };
    mutable std::mutex mutex_;
    std::unordered_map<ID3D12GraphicsCommandList*, std::shared_ptr<Entry>> entries_;
    std::uint64_t nextIncarnation_ = 0;
    std::uint64_t nextEnrollment_ = 0;
    // Only pending discovery callbacks occupy this map. Values are scoped
    // lifecycle tokens and initial routes, never address-lifetime eligibility flags.
    struct PendingEnrollment { std::uint64_t token, hookGeneration; void** commandTable; };
    std::unordered_map<ID3D12GraphicsCommandList*,PendingEnrollment> pendingEnrollment_;
    std::uint64_t hookGeneration_ = 1;
    bool completeHookCoverage_ = false;
    std::function<bool(const NativeRecordingIdentity&)> coveredRoute_;

  public:
    struct Enrollment
    {
        ID3D12GraphicsCommandList* list;
        std::uint64_t token,hookGeneration;
        constexpr Enrollment(ID3D12GraphicsCommandList* value=nullptr,std::uint64_t sequence=0,std::uint64_t generation=0)
            :list(value),token(sequence),hookGeneration(generation){}
    };
    struct RouteHistoryWitness
    {
        ID3D12GraphicsCommandList* list = nullptr;
        std::uint64_t incarnation = 0, enrollment = 0, hookGeneration = 0;
        void** commandTable = nullptr;
    };
    RouteHistoryWitness CaptureRouteHistory(ID3D12GraphicsCommandList* list) const
    {
        std::lock_guard lock(mutex_);
        RouteHistoryWitness witness{list,0,0,hookGeneration_};
        if (auto entry=entries_.find(list);entry!=entries_.end())
        {
            witness.incarnation=entry->second->incarnation;
            if (completeHookCoverage_ && entry->second->completeAtReset &&
                entry->second->hookGeneration==hookGeneration_ && entry->second->identity)
                witness.commandTable=entry->second->identity->commandTable;
        }
        if (auto pending=pendingEnrollment_.find(list);pending!=pendingEnrollment_.end() &&
            pending->second.hookGeneration==hookGeneration_)
        { witness.enrollment=pending->second.token;witness.commandTable=pending->second.commandTable; }
        return witness;
    }
    void InvalidateRouteHistory(const RouteHistoryWitness& witness)
    {
        std::lock_guard lock(mutex_);
        if (!witness.list || witness.hookGeneration != hookGeneration_) return;
        if (auto entry=entries_.find(witness.list);entry!=entries_.end() &&
            witness.incarnation && entry->second->incarnation==witness.incarnation) entry->second->completeAtReset=false;
        if (auto pending=pendingEnrollment_.find(witness.list);pending!=pendingEnrollment_.end() &&
            witness.enrollment && pending->second.token==witness.enrollment) pendingEnrollment_.erase(pending);
    }
    Enrollment BeginEnrollment(ID3D12GraphicsCommandList* list)
    {
        std::shared_ptr<Entry> retired;
        std::lock_guard lock(mutex_);
        if(!list)return {};
        pendingEnrollment_.erase(list);
        if(auto entry=entries_.find(list);entry!=entries_.end()) {
            entry->second->state.Close(entry->second->incarnation);
            retired=std::move(entry->second);entries_.erase(entry);
        }
        if(nextEnrollment_==UINT64_MAX)return {};
        const auto token=++nextEnrollment_;
        try {pendingEnrollment_.emplace(list,PendingEnrollment{token,hookGeneration_,*reinterpret_cast<void***>(list)});}
        catch(...){return {};}
        return {list,token,hookGeneration_};
    }
    bool EnrollmentCurrent(const Enrollment& enrollment)const
    {
        std::lock_guard lock(mutex_);
        const auto pending=pendingEnrollment_.find(enrollment.list);
        return enrollment.token&&pending!=pendingEnrollment_.end()&&pending->second.token==enrollment.token&&
            enrollment.hookGeneration==hookGeneration_;
    }
    void InvalidatePendingEffect(ID3D12GraphicsCommandList* list)
    {
        // A real setter/work callback during discovery is not a Reset default.
        // Preserve that original host call, but do not publish unseen state as
        // complete history. Queries and diagnostic markers never call this.
        std::lock_guard lock(mutex_);pendingEnrollment_.erase(list);
    }
    void FinishEnrollment(const Enrollment& enrollment)
    {
        std::lock_guard lock(mutex_);
        const auto pending=pendingEnrollment_.find(enrollment.list);
        if(pending!=pendingEnrollment_.end()&&pending->second.token==enrollment.token)pendingEnrollment_.erase(pending);
    }
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
        // Creation observes shape while the caller owns the actual root. No
        // observer entry or global owner is needed, including while NR is off.
        return NativeRootLayouts::Store(signature, layout);
    }
    template <typename Action>
    bool WithRootLayout(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature,
                        Action&& action, UINT slot = UINT_MAX,
                        NativeRecordingState::RootOwner* forwardingOwner = nullptr)
    {
        if (!signature) return WithCurrent(list, action, slot);
        NativeRecordingState::RootOwner owner;
        struct TransferForForwarding
        {
            NativeRecordingState::RootOwner& source;
            NativeRecordingState::RootOwner* destination;
            ~TransferForForwarding() { if (destination) *destination = std::move(source); }
        } transfer{owner, forwardingOwner};
        bool handled = false;
        const bool knownAccepted = WithCurrent(list, [&](auto& state, auto incarnation) {
            owner = state.KnownRootOwner(signature);
            if (!state.HasLayout(signature)) return true;
            handled = true;
            return action(state, incarnation);
        }, slot);
        // Execute the normal known-root action in one epoch, without vector
        // copies, foreign metadata queries, or fresh COM ownership acquisition.
        if (handled || !knownAccepted) return knownAccepted;
        std::optional<std::vector<RootParameter>> layout;
        try
        {
            layout = NativeRootLayouts::Read(signature).layout;
            if (!owner) owner = std::make_shared<Microsoft::WRL::ComPtr<ID3D12RootSignature>>(signature);
        }
        catch (...)
        {
            return WithCurrent(list, [&](auto& state, auto incarnation) {
                state.Taint(incarnation, "ObservationException", slot); return false;
            }, slot);
        }
        // Discovery/AddRef may synchronously complete a genuine Reset/Close.
        // Select the current epoch; no old Entry, arguments or coverage cross.
        // The shared owner survives every lock and failed insertion. Native
        // hooks take it across the original setter; observer-only adapters can
        // release after this observation (their actual setter already ran).
        return WithCurrent(list, [&](auto& state, auto incarnation) {
            if (!state.AdoptRootOwner(incarnation, signature, owner)) return false;
            if (layout && !state.HasLayout(signature) && !state.RegisterLayout(signature, *layout, owner)) return false;
            return action(state, incarnation);
        }, slot);
    }
    bool Reset(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline)
    {
        return ResetWithIdentity(list, initialPipeline, {});
    }
    // Called only after the original native Reset succeeds. Identity discovery
    // never issues a Reset itself and never certifies unseen earlier recording.
    bool ResetNative(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline, UINT interfaceVersion = 0,
                     NativeRecordingResetDiagnostic* diagnostic = nullptr, void** qualifiedTable = nullptr,
                     Enrollment enrollment = {})
    {
        if(!enrollment.token)enrollment=BeginEnrollment(list);
        const auto fail = [&](unsigned stage, HRESULT result, const GUID& iid = GUID{}, std::uint64_t expected = 0, std::uint64_t observed = 0) {
            if (diagnostic) *diagnostic = {stage, result, iid, expected, observed};
            FinishEnrollment(enrollment); return false;
        };
        try
        {
            if(!list)return fail(1,E_POINTER,__uuidof(ID3D12GraphicsCommandList));
            if(!EnrollmentCurrent(enrollment))return fail(7,E_ABORT);
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
            if (!ResetWithIdentity(list, initialPipeline, std::move(identity),enrollment))
            { if (diagnostic) *diagnostic = {7, S_OK}; return false; }
            return true;
        }
        catch (...) { return fail(8, E_FAIL); }
    }

  private:
    bool ResetWithIdentity(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initialPipeline,
                           std::shared_ptr<const NativeRecordingIdentity> identity,Enrollment enrollment={})
    {
        if(!enrollment.token)enrollment=BeginEnrollment(list);
        struct EnrollmentRetirement {
            NativeRecordingObserver& owner;Enrollment enrollment;
            ~EnrollmentRetirement(){owner.FinishEnrollment(enrollment);}
        } finish{*this,enrollment};
        NativeRecordingState::PreparedOwners prepared;
        prepared.Acquire(list);prepared.Acquire(initialPipeline);
        NativeRecordingState::Retirement retiredState;
        // These owners outlive the lock guard. COM Release may reenter the
        // observer; neither old nor failed candidate entries retire under it.
        std::shared_ptr<Entry> retired, candidate;
        std::lock_guard lock(mutex_);
        const auto pending=pendingEnrollment_.find(list);
        if(!list||!enrollment.token||nextIncarnation_==UINT64_MAX||
           pending==pendingEnrollment_.end()||pending->second.token!=enrollment.token||
           enrollment.hookGeneration!=hookGeneration_)return false;
        try
        {
            candidate = std::make_shared<Entry>();
            const auto next = ++nextIncarnation_;
            if (!candidate->state.BeginPrepared(list,next,initialPipeline,
                identity&&identity->type==D3D12_COMMAND_LIST_TYPE_DIRECT,prepared,retiredState))return false;
            candidate->incarnation = next;
            candidate->hookGeneration = hookGeneration_;
            candidate->completeAtReset = completeHookCoverage_ &&
                (!coveredRoute_ || (identity && coveredRoute_(*identity)));
            candidate->identity = std::move(identity);
            // Allocate the map node without passing a COM-owning temporary.
            // Failure leaves the old recording absent, never still restorable.
            auto entry = entries_.try_emplace(list).first;
            entry->second = std::move(candidate);
            pendingEnrollment_.erase(pending);
            return true;
        }
        catch (...) { return false; }
    }

  public:
    void Close(ID3D12GraphicsCommandList* list)
    {
        const auto enrollment=BeginEnrollment(list);
        FinishEnrollment(enrollment);
    }
    bool Work(ID3D12GraphicsCommandList* list)
    {
        return WithCurrent(list, [](NativeRecordingState& state, std::uint64_t incarnation) {
            return state.ObserveWork(incarnation);
        });
    }
    bool ReadCurrentEpoch(ID3D12GraphicsCommandList* list, std::uint64_t& incarnation,
                          std::uint64_t& workOrdinal, std::uint64_t& hookGeneration) const
    {
        std::lock_guard lock(mutex_);
        const auto found=entries_.find(list);
        if(found==entries_.end()||!CurrentCoverage(*found->second))return false;
        const auto ordinal=found->second->state.Activity(list,found->second->incarnation);
        if(!ordinal)return false;
        incarnation=found->second->incarnation;workOrdinal=*ordinal;hookGeneration=found->second->hookGeneration;
        // No interface resolution or owning-reference copy/release on this path.
        return true;
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
    // The actual Core facade delegates here. Preserve caller correlation
    // while retaining the distinct terminal journal and its owned interface pair.
    NativeRecordingObservation ObserveCaller(ID3D12GraphicsCommandList* commandList)const
    {
    auto interfaces = std::make_shared<const Neurotic::D3D12::NativeRecordingInterfaces>(
        Neurotic::D3D12::NativeRecordingInterfaces::Resolve(commandList));
    if (FAILED(interfaces->result) || !interfaces->terminal) return {};
    auto observed = Observe(interfaces->terminal.Get());
    if (observed.identity && observed.identity->nativeList.Get() != interfaces->terminal.Get()) return {};
    // Receipts correlate the unchanged execution/caller interface. The owned
    // terminal identity remains explicit; these pointers are never equated.
    observed.nativeList = interfaces->caller.Get();
    observed.interfaces = std::move(interfaces);
    return observed;
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
        const NativeRecordingState::OriginalSetters& original, NativeStateCaptureDiagnostic* diagnostic = nullptr,
        NativeRecordingState::QueryPolicy queryPolicy = NativeRecordingState::QueryPolicy::RequireClosed) const
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(list);
        if (it == entries_.end()) { if (diagnostic) *diagnostic = {}; return {}; }
        const bool coverage = CurrentCoverage(*it->second);
        std::optional<NativeRecordingState::Snapshot> captured;
        if (coverage)
            captured = it->second->state.CaptureRestorable(list, it->second->incarnation, mask, original, diagnostic, queryPolicy);
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
        auto replay = original;
        std::shared_ptr<Entry> selected;
        std::uint64_t generation = 0;
        const auto refuse = [&](const char* reason) {
            if (diagnostic) {
                if (selected) selected->state.DiagnoseRestore(snapshot, diagnostic);
                else *diagnostic = {};
                diagnostic->reason = reason;
            }
            return false;
        };
        {
            std::lock_guard lock(mutex_);
            const auto entry = entries_.find(list);
            if (entry == entries_.end()) return refuse("Native.Restore.ObserverEntryMissing");
            selected = entry->second;
            if (!CurrentCoverage(*selected)) return refuse("Native.Restore.ObserverCoverageIncomplete");
            if (selected->incarnation != incarnation) return refuse("Native.Restore.ObserverIncarnationChanged");
            generation = hookGeneration_;
        }
        const auto current = [this, list, selected, incarnation, generation] {
            const auto entry = entries_.find(list);
            return entry != entries_.end() && entry->second == selected &&
                selected->incarnation == incarnation && hookGeneration_ == generation && CurrentCoverage(*selected);
        };
        replay.validate = [this, current, prior = original.validate] {
            if (prior && !prior()) return false;
            std::lock_guard lock(mutex_);
            return current();
        };
        replay.commit = [this, current, prior = original.validate, priorCommit = original.commit]
            (const std::function<bool()>& apply) {
            const auto ownedApply = [&] {
                if (prior && !prior()) return false;
                std::lock_guard lock(mutex_);
                return current() && apply();
            };
            // A supplied callback runs outside this lock. The final metadata
            // action holds observer then state locks and executes no COM/API.
            return priorCommit ? priorCommit(ownedApply) : ownedApply();
        };
        if (!selected->state.Restore(snapshot, replay, diagnostic)) return false;
        {
            std::lock_guard lock(mutex_);
            if (!current()) return refuse("Native.Restore.CoverageLostAfterReplay");
        }
        return true;
    }

  private:
    std::optional<std::uint64_t> HeapBindingEpoch(ID3D12GraphicsCommandList* list) const
    {
        std::lock_guard lock(mutex_);
        const auto entry = entries_.find(list);
        if (entry == entries_.end() || !CurrentCoverage(*entry->second)) return {};
        return entry->second->state.HeapBindingEpoch(list, entry->second->incarnation);
    }
    bool CurrentCoverage(Entry& entry) const
    {
        if (!completeHookCoverage_ || !entry.completeAtReset || entry.hookGeneration != hookGeneration_) return false;
        if (coveredRoute_ && (!entry.identity || !coveredRoute_(*entry.identity)))
        {
            // Callers hold mutex_. A restored route cannot prove the missing
            // history of this recording; only a genuine new Reset reenrolls it.
            entry.completeAtReset = false;
            return false;
        }
        return true;
    }
};
}
