#pragma once
#include "../nr/diagnostics/capability/CapabilityOwnerAdapters.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <climits>
#include <mutex>
#include "NativeIdentity.h"
#include "FgLifecycleContract.h"
#include "StartupReadiness.h"

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
    StartupReadiness* readiness = nullptr; // owner presentation mutex held
    ReadinessIdentity readinessIdentity {};
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
    bool overflow = false;
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
        overflow = true; // unknown live handles cannot later become a unique provider
        return 0;
    }
    bool Release(uintptr_t handle, uint64_t expected)
    {
        if (!handle || !expected) return false;
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
        if (overflow) ++result.active;
        if (result.active != 1) result.instance = 0;
        return result;
    }
};

enum class CompletionKind { Output, Probe };
struct CompletionDependency
{
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    uint64_t value = 0;
    uint64_t token = 0;
    uint64_t sequence = 0;
    uint64_t reservation = 0;
    CompletionKind kind = CompletionKind::Output;
    bool retiring = false; // ordering an already-started group, never new NR admission
    std::shared_ptr<GpuSafety::ExternalWaitStatus> status;
};
enum class CompletionClaimResult { None, Ready, Refused };
struct CompletionClaim
{
    CompletionClaimResult result = CompletionClaimResult::None;
    const char* reason = "no-packet";
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
        unsigned int claimed = 0, generated = 0;
        bool allowMfg = false;
        bool invalidated = false;
        bool closing = false;
        CompletionKind kind = CompletionKind::Output;
        std::shared_ptr<GpuSafety::ExternalWaitStatus> status;
    };
    std::array<Entry, 8> entries {};
    uint64_t nextReservation = 0;
    static bool Completed(const Entry& entry)
    {
        if (!entry.ready || !entry.fence || !entry.value) return false;
        const auto done = entry.fence->GetCompletedValue();
        return done != UINT64_MAX && done >= entry.value;
    }
    void RetireInvalidated()
    {
        for (auto& entry : entries)
            if ((entry.invalidated || entry.closing) && Completed(entry)) entry = {};
    }
  public:
    uint64_t Reserve(ID3D12Resource* resource, uint64_t providerGeneration,
                     uint64_t nativeFgGeneration, uint64_t nativeFgInstance,
                     uint64_t token, uint64_t sequence, CompletionKind kind = CompletionKind::Output,
                     std::atomic<uint64_t>* failureEpoch = nullptr, bool allowMfg = false)
    {
        if (!resource || !providerGeneration || !nativeFgGeneration || !nativeFgInstance || !token || !sequence)
            return 0;
        RetireInvalidated();
        for (auto& entry : entries)
            if (entry.reservation && entry.resource == resource)
            {
                // A fresh Present may retire even an unconsumed packet (the provider
                // can skip FG). Duplicate evaluations still cannot claim it twice.
                if (sequence <= entry.sequence || !Completed(entry)) return 0;
                entry = {};
            }
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
                entry.kind = kind;
                entry.allowMfg = allowMfg;
                entry.status = std::make_shared<GpuSafety::ExternalWaitStatus>();
                entry.status->failureEpoch = failureEpoch;
                return reservation;
            }
        return 0;
    }
    bool Commit(uint64_t reservation, ID3D12Resource* resource, ID3D12Fence* fence, uint64_t value,
                CompletionKind kind = CompletionKind::Output)
    {
        if (!reservation || !resource || !fence || !value || value == UINT64_MAX) return false;
        for (auto& entry : entries)
            if (entry.reservation == reservation && entry.resource == resource && !entry.ready)
            {
                entry.fence = fence; entry.value = value; entry.ready = true; entry.kind = kind;
                return !entry.invalidated && !entry.closing;
            }
        return false;
    }
    void Cancel(uint64_t reservation)
    {
        for (auto& entry : entries)
            if (entry.reservation == reservation) { entry = {}; return; }
    }
    void RetireBefore(uint64_t sequence)
    {
        for (auto& entry : entries)
            if (entry.reservation && entry.sequence < sequence && Completed(entry)) entry = {};
    }
    std::shared_ptr<GpuSafety::ExternalWaitStatus> Status(uint64_t reservation) const
    {
        for (const auto& entry : entries)
            if (entry.reservation == reservation && !entry.invalidated && !entry.closing) return entry.status;
        return {};
    }
    CompletionClaim Claim(ID3D12Resource* resource, uint64_t providerGeneration,
                          uint64_t nativeFgGeneration, uint64_t nativeFgInstance,
                          uint64_t exactToken = 0, uint64_t exactSequence = 0,
                          unsigned int generated = 1, unsigned int index = 1)
    {
        CompletionClaim claim;
        RetireInvalidated();
        for (auto& entry : entries)
        {
            if (!entry.reservation || entry.resource != resource) continue;
            // Settings/off revokes future work, not the ordering obligation of
            // child 1 already handed to this exact live native consumer. Only
            // its ordered remaining indices can finish under the old packet.
            const bool retiringTail = entry.closing && entry.claimed > 0 &&
                entry.generated == generated && index > 1;
            const char* refusal = entry.invalidated ? "packet-invalidated" :
                entry.closing && !retiringTail ? "admission-closed-no-started-tail" :
                entry.providerGeneration != providerGeneration && !retiringTail ? "provider-generation" :
                entry.nativeFgGeneration != nativeFgGeneration ? "native-generation" :
                entry.nativeFgInstance != nativeFgInstance ? "native-instance" :
                !entry.ready || !entry.fence || !entry.value ? "producer-not-committed" :
                exactToken && entry.token != exactToken ? "frame-token" :
                exactSequence && entry.sequence != exactSequence ? "frame-sequence" :
                !generated || generated > (entry.allowMfg ? 5u : 1u) ? "generated-count" :
                !index || index > generated || index != entry.claimed + 1 ? "child-index" :
                entry.generated && entry.generated != generated ? "group-count-changed" :
                entry.status->failed.load() ? "wait-group-failed" : nullptr;
            if (refusal)
            { claim.result = CompletionClaimResult::Refused; claim.reason = refusal; return claim; }
            if (!entry.generated)
            {
                entry.generated = generated;
                entry.status->expectedWaits = generated;
            }
            claim.result = CompletionClaimResult::Ready;
            claim.reason = retiringTail ? "retiring-tail" : "current-group";
            claim.dependency.fence = entry.fence;
            claim.dependency.value = entry.value;
            claim.dependency.token = entry.token;
            claim.dependency.sequence = entry.sequence;
            claim.dependency.reservation = entry.reservation;
            claim.dependency.kind = entry.kind;
            claim.dependency.retiring = retiringTail;
            if (generated == 1) claim.dependency.status = entry.status;
            else
            {
                claim.dependency.status = std::make_shared<GpuSafety::ExternalWaitStatus>();
                claim.dependency.status->group = entry.status;
            }
            ++entry.claimed;
            return claim;
        }
        return claim;
    }
    void Reset()
    {
        // Invalidation cannot erase a GPU dependency. Keep a refusal until the
        // producer completes, or the caller cancels before any copyback submission.
        for (auto& entry : entries) if (entry.reservation) entry.invalidated = true;
        RetireInvalidated();
    }
    void CloseAdmission()
    {
        for (auto& entry : entries) if (entry.reservation) entry.closing = true;
        RetireInvalidated();
    }
    unsigned int Count() const
    {
        unsigned int count = 0;
        for (const auto& entry : entries) if (entry.reservation) ++count;
        return count;
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
    unsigned int requestedGenerated = 0, observedGenerated = 0;
};
struct ConsumerKey
{
    uint64_t provider = 0, generation = 0, instance = 0;
    bool operator==(const ConsumerKey&) const = default;
};
struct ConsumerPath
{
    ConsumerKey key {};
    unsigned int type = UINT_MAX;
    uintptr_t implementation = 0;
    uint64_t revision = 0;
    std::shared_ptr<GpuSafety::ExternalExecutionStatus> observation;
    bool Ready(const ConsumerKey& expected) const
    {
        return key == expected && GpuSafety::SupportedExternalType(static_cast<D3D12_COMMAND_LIST_TYPE>(type)) &&
            observation && observation->Ready();
    }
    void Clear() { key = {}; type = UINT_MAX; implementation = 0; observation.reset(); ++revision; }
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
    std::atomic<uint64_t> readinessEpoch {1};
    std::atomic<bool> observeConsumer {false};
    ConsumerPath consumer;
};
inline Registry& State() { static auto* state = new Registry; return *state; }
inline void RevokeReadiness() { ++State().readinessEpoch; }
inline void StopConsumerObservation()
{
    State().observeConsumer = false;
    std::lock_guard lock(State().mutex);
    State().consumer.Clear();
}
inline bool ConsumerReady(const Frame& frame)
{
    std::lock_guard lock(State().mutex);
    return State().consumer.Ready({frame.providerGeneration, frame.nativeFgGeneration, frame.nativeFgInstance});
}
inline std::shared_ptr<GpuSafety::ExternalExecutionStatus> ObserveConsumer(uintptr_t handle,
                                                                        ID3D12GraphicsCommandList* list)
{
    if (!State().observeConsumer.load() || !list) return {};
    const auto nativeList = NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list);
    if (!nativeList.object)
    {
        std::lock_guard lock(State().mutex);
        State().consumer.Clear(); RevokeReadiness(); return {};
    }
    const auto type = static_cast<unsigned int>(nativeList.object->GetType());
    const auto implementation = reinterpret_cast<uintptr_t>((*(void***)nativeList.object.Get())[10]);
    ConsumerKey key;
    uint64_t revision;
    bool changed;
    {
        std::lock_guard lock(State().mutex);
        const auto native = State().nativeFg.Read();
        if (!State().provider.enabled || native.active != 1 ||
            State().nativeFg.Find(handle) != native.instance || !State().observeConsumer.load()) return {};
        key = {State().provider.generation, native.generation, native.instance};
        auto& path = State().consumer;
        changed = path.key != key || path.type != type || path.implementation != implementation;
        // An unsupported implementation stays bypassed until the provider/type
        // changes. Do not create queues or submit NR probes on every failed frame.
        if (!changed && !path.observation) return {};
        if (!changed && path.observation && !path.observation->failed.load()) return path.observation;
        path.key = key; path.type = type; path.implementation = implementation;
        path.observation.reset(); revision = ++path.revision;
        RevokeReadiness();
    }
    // Never acquire GPU hook locks while holding the frame ledger lock.
    const char* reason = nullptr;
    auto observation = GpuSafety::ObserveExternalExecution(nativeList.object.Get(), &reason);
    {
        std::lock_guard lock(State().mutex);
        auto& path = State().consumer;
        if (path.revision != revision || path.key != key || !State().observeConsumer.load()) return {};
        path.observation = observation;
    }
#if defined(LOG_INFO) && !defined(NR_GPU_SAFETY_TEST)
    if (changed)
        LOG_INFO("NR Present FG consumer: listType={} provider={} generation={} instance={} observation={} reason={}",
            type, key.provider, key.generation, key.instance, observation != nullptr, reason);
#endif
    (void)changed;
    return observation;
}
// Optional scalar observer called under the existing ledger lock. It cannot affect Claim.
using LedgerObserver = void(*)(const char*, uint32_t, uint32_t, Ledger::Snapshot, Ledger::Snapshot) noexcept;
inline std::atomic<LedgerObserver> ledgerObserver {nullptr};
inline void ObserveLedger(const char* operation, uint32_t frame, uint32_t viewport, Ledger::Snapshot before)
{
    if (auto observer = ledgerObserver.load(std::memory_order_relaxed))
        observer(operation, frame, viewport, before, State().ledger.Inspect());
}
inline void PublishProvider(bool enabled, bool supported, unsigned int requestedGenerated = 1)
{
    std::lock_guard lock(State().mutex);
    auto& provider = State().provider;
    if (!provider.known || provider.enabled != enabled || provider.supported != supported ||
        provider.requestedGenerated != requestedGenerated)
    {
        ++provider.generation;
        RevokeReadiness();
        State().completions.CloseAdmission();
        provider.observedGenerated = 0;
    }
    provider.known = true; provider.enabled = enabled; provider.supported = supported;
    provider.requestedGenerated = requestedGenerated;
}
// Post-override NGX parameters are observations, not the game request or a
// capability maximum. A transition revokes readiness without erasing the NR
// dependency already submitted for the current real input.
inline void ObserveGeneratedCount(uintptr_t handle, unsigned int generated, unsigned int index)
{
    std::lock_guard lock(State().mutex);
    const auto native = State().nativeFg.Read();
    if (!State().provider.enabled || native.active != 1 ||
        State().nativeFg.Find(handle) != native.instance) return;
    const auto observed = generated >= 1 && generated <= 5 && index >= 1 && index <= generated ? generated : 0;
    auto& provider = State().provider;
    if (provider.observedGenerated != observed)
    {
        provider.observedGenerated = observed;
        RevokeReadiness();
#if defined(LOG_INFO) && !defined(NR_GPU_SAFETY_TEST)
        LOG_INFO("NR Present FG multiplier observation: requestedGenerated={} ngxGenerated={} ngxIndex={} "
                 "supportedFixedCount={} provider={}; readiness revoked", provider.requestedGenerated,
                 generated, index, observed, provider.generation);
#endif
    }
}
inline void PublishNativeFgCreated(uintptr_t handle)
{
    std::lock_guard lock(State().mutex);
    State().nativeFg.Create(handle);
    State().provider.observedGenerated = 0;
    RevokeReadiness();
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
    if (State().nativeFg.Release(handle, expected))
    { RevokeReadiness(); State().completions.Reset(); }
}
inline FgLifecycle::Snapshot NativeFg()
{
    std::lock_guard lock(State().mutex);
    return State().nativeFg.Read();
}
inline uint64_t ReserveCompletion(ID3D12Resource* resource, const Frame& frame)
{
    std::lock_guard lock(State().mutex);
    const auto& provider = State().provider;
    const auto native = State().nativeFg.Read();
    if (!provider.known || !provider.enabled || !provider.supported ||
        provider.generation != frame.providerGeneration || native.active != 1 ||
        native.generation != frame.nativeFgGeneration || native.instance != frame.nativeFgInstance)
        return 0;
    return State().completions.Reserve(resource, frame.providerGeneration, frame.nativeFgGeneration,
        frame.nativeFgInstance, frame.key, frame.sequence,
        frame.allowOutput ? CompletionKind::Output : CompletionKind::Probe, &State().readinessEpoch, true);
}
inline bool CommitCompletion(uint64_t reservation, ID3D12Resource* resource, ID3D12Fence* fence, uint64_t value,
                             CompletionKind kind = CompletionKind::Output)
{
    std::lock_guard lock(State().mutex);
    return State().completions.Commit(reservation, resource, fence, value, kind);
}
inline std::shared_ptr<GpuSafety::ExternalWaitStatus> CompletionStatus(uint64_t reservation)
{
    std::lock_guard lock(State().mutex);
    return State().completions.Status(reservation);
}
inline void CancelCompletion(uint64_t reservation)
{
    std::lock_guard lock(State().mutex);
    State().completions.Cancel(reservation);
}
inline CompletionClaim ClaimCompletion(ID3D12Resource* resource, uint64_t providerGeneration,
                                       uintptr_t nativeHandle, unsigned int generated = 1, unsigned int index = 1)
{
    std::lock_guard lock(State().mutex);
    const auto native = State().nativeFg.Read();
    const auto instance = State().nativeFg.Find(nativeHandle);
    const auto* frame = forwardingFrame;
    auto result = State().completions.Claim(resource, providerGeneration, native.generation,
        native.active == 1 && instance == native.instance ? instance : 0,
        frame ? frame->key : 0, frame ? frame->sequence : 0, generated, index);
    if (result.result == CompletionClaimResult::Refused) RevokeReadiness();
    return result;
}
inline unsigned int PendingCompletions()
{
    std::lock_guard lock(State().mutex);
    return State().completions.Count();
}
inline void RetireFrameCompletions(uint64_t sequence)
{
    std::lock_guard lock(State().mutex);
    State().completions.RetireBefore(sequence);
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
    RevokeReadiness();
    State().completions.Reset();
}
inline void CloseCompletionAdmission()
{
    std::lock_guard lock(State().mutex);
    RevokeReadiness();
    State().completions.CloseAdmission();
}
inline ProviderState Provider()
{
    std::lock_guard lock(State().mutex);
    return State().provider;
}
inline Capability::ProviderObservation CopyCapabilityObservation(const Capability::WriterPort& port) noexcept
{
    std::lock_guard lock(State().mutex);
    const auto p=State().provider;
    return {Capability::ReserveSample(port).sequence,p.known,p.enabled,p.supported,p.generation};
}
// Correlate a Native capture with the actual enclosing temporal evaluation,
// not constants another thread may already have published for the next frame.
// This token is correlation only; the guide bridge still proves submission/order.
class NativeFrameScope
{
    inline static thread_local NativeFrameScope* current = nullptr;
    NativeFrameScope* previous;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t key = 0;
    uint64_t providerGeneration = 0;
  public:
    NativeFrameScope() : previous(current) { current = this; }
    void Configure(bool temporal, uint32_t frame, ID3D12GraphicsCommandList* commandList)
    {
        if (!temporal || !commandList) return;
        list = NativeIdentity::Resolve<ID3D12GraphicsCommandList>(commandList).object;
        if (list && list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT)
        { key = uint64_t(frame) + 1; providerGeneration = Provider().generation; }
    }
    NativeFrameScope(const NativeFrameScope&) = delete;
    NativeFrameScope& operator=(const NativeFrameScope&) = delete;
    ~NativeFrameScope() { list.Reset(); current = previous; }
    static bool Active() { return current != nullptr; }
    static uint64_t Generation() { return current ? current->providerGeneration : 0; }
    static uint64_t Frame(ID3D12GraphicsCommandList* commandList)
    {
        if (!current || !current->key || !commandList) return 0;
        const auto native = NativeIdentity::Resolve<ID3D12GraphicsCommandList>(commandList).object;
        return native.Get() == current->list.Get() ? current->key : 0;
    }
};
struct NativeFrameIdentity { uint64_t key = 0, providerGeneration = 0; };
inline NativeFrameIdentity CurrentFrameIdentity(ID3D12GraphicsCommandList* commandList = nullptr)
{
    const bool scoped = NativeFrameScope::Active();
    const auto scopedKey = scoped ? NativeFrameScope::Frame(commandList) : 0;
    const auto scopedGeneration = scoped ? NativeFrameScope::Generation() : 0;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    // An in-flight old callback must keep its original producer epoch even if
    // FG turns off before capture. Otherwise key zero relabels it as a fresh
    // ordinary interval, allowing the new provider to admit stale metadata.
    if (scopedGeneration && scopedGeneration != state.provider.generation)
        return {0, scopedGeneration};
    // Native capture only carries a provider token while that provider will also
    // select an exact frame at Present. Games such as DD2 disable FG for dialogue
    // and menus while continuing to publish Streamline constants; retaining that
    // unclaimed token would reject the otherwise unique Native/Present interval.
    if (state.swapchains != 1 || !state.provider.enabled) return {0, state.provider.generation};
    return {scoped ? (scopedGeneration == state.provider.generation ? scopedKey : 0)
                  : state.ledger.Current(state.provider.generation), state.provider.generation};
}
inline uint64_t CurrentFrame(ID3D12GraphicsCommandList* commandList = nullptr)
{ return CurrentFrameIdentity(commandList).key; }
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
    bool presentPolicyActive = false;
    uint64_t providerGeneration = 0;
    double previousPresentMs = 0.0;
    StartupReadiness startup;
    unsigned int startupRoute = 0;
    uint64_t startupResume = 0;
    uint64_t probes = 0, readinessTransitions = 0;
    double providerChangedMs = 0.0, probeSubmittedMs = 0.0;
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

