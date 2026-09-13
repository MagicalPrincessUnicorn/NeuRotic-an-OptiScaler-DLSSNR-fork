#ifndef NR_GPU_SAFETY_TEST
#include "pch.h"
#include <SysUtils.h>
#include <Logger.h>
#include "FrameTrace.h"
#else
#define NR_FRAME_TRACE(...) do {} while (false)
#endif
#include "NrGpuSafety.h"
#include <windows.h>
#include <detours/detours.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <type_traits>

namespace DlssNr::GpuSafety
{
using Microsoft::WRL::ComPtr;
struct Timeline
{
    ComPtr<ID3D12Fence> fence;
    UINT64 next = 0;
    UINT64 frequency = 0;
};
struct Point { std::shared_ptr<Timeline> timeline; UINT64 value; };
struct Recording
{
    bool sealed = false;
    bool failed = false;
    UINT64 submissions = 0;
    std::vector<Point> points;
};
struct ExternalWait
{
    ComPtr<ID3D12Fence> fence;
    UINT64 value = 0;
    UINT64 token = 0;
    UINT64 sequence = 0;
    std::shared_ptr<ExternalWaitStatus> status;
    ~ExternalWait()
    {
        // Reset/destruction before execution is cancellation, not wait evidence.
        if (status && !status->applied.load()) status->Fail();
    }
};
static std::atomic<unsigned int> observationCookies {0};
struct ExecutionObservation
{
    std::shared_ptr<ExternalExecutionStatus> status = std::make_shared<ExternalExecutionStatus>();
    ExecutionObservation() { ++observationCookies; }
    ~ExecutionObservation()
    {
        if (!status->submitted.load()) status->failed = true;
        --observationCookies;
    }
};
namespace
{
constexpr GUID recordingGuid = {0x5ee0e247, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
constexpr GUID timelineGuid = {0x5ee0e248, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
constexpr GUID externalWaitGuid = {0x5ee0e249, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
constexpr GUID executionObservationGuid = {0x5ee0e24a, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
constexpr GUID hookProofGuid = {0x5ee0e24b, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
ExecuteFn originalExecute = nullptr;
ResetFn originalReset = nullptr;
void* resetTarget = nullptr;
ExecuteFn alternateExecute = nullptr;
ResetFn alternateReset = nullptr;
void* executeTarget = nullptr;
void* alternateExecuteTarget = nullptr;
void* alternateResetTarget = nullptr;
struct HookProof { void* reset; D3D12_COMMAND_LIST_TYPE type; };
// Callbacks can outlive explicit NGX sessions. This small registry and its hooks have process
// lifetime; individual tickets/fences are reclaimed. No owning reference back to a list/queue.
struct Registry
{
    std::recursive_mutex mutex;
    std::mutex hookMutex;
    CompletionSet pending;
    bool failed = false;
};
Registry& State() { static auto* state = new Registry; return *state; }

Ticket Unavailable(const char* reason)
{
#ifndef NR_GPU_SAFETY_TEST
    static std::atomic_flag reported {};
    if (!reported.test_and_set())
        LOG_WARN("DLSS-NR GPU safety: bypassing NR ({})", reason);
#else
    (void)reason;
#endif
    return {};
}

template<class T> class Cookie final : public IUnknown
{
    std::atomic<ULONG> refs {1};
  public:
    std::shared_ptr<T> data;
    explicit Cookie(std::shared_ptr<T> value) : data(std::move(value)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --refs;
        if (!left)
        {
            if constexpr (std::is_same_v<T, Recording>)
            {
                std::lock_guard lock(State().mutex);
                data->sealed = true;
            }
            delete this;
        }
        return left;
    }
};

template<class T> std::shared_ptr<T> Get(ID3D12Object* object, REFGUID key)
{
    IUnknown* unknown = nullptr;
    UINT size = sizeof(unknown);
    if (FAILED(object->GetPrivateData(key, &size, &unknown)) || !unknown) return {};
    auto* cookie = static_cast<Cookie<T>*>(unknown);
    auto data = cookie->data;
    unknown->Release(); // GetPrivateData AddRefs interface-valued private data.
    return data;
}
template<class T> bool Put(ID3D12Object* object, REFGUID key, const std::shared_ptr<T>& data)
{
    auto* cookie = new Cookie<T>(data);
    const HRESULT hr = object->SetPrivateDataInterface(key, cookie);
    cookie->Release();
    return SUCCEEDED(hr);
}
template<class T> T* NativeObject(T* object)
{
#ifndef NR_GPU_SAFETY_TEST
    // Same Streamline native-object interface as Util::CheckForRealObject, without its mutable
    // lazy GUID initialization or per-call log (this runs for every NR dispatch).
    constexpr GUID nativeGuid = {0xadec44e2, 0x61f0, 0x45c3, {0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    T* real = nullptr;
    if (SUCCEEDED(object->QueryInterface(nativeGuid, reinterpret_cast<void**>(&real))) && real)
    {
        real->Release(); // the caller's live wrapper owns the borrowed native object
        return real;
    }
#endif
    return object;
}

bool Completed(const Ticket& t)
{
    if (!t || t->failed) return !t;
    for (const auto& p : t->points)
    {
        const UINT64 done = p.timeline->fence->GetCompletedValue();
        if (done == UINT64_MAX) { State().failed = true; return false; }
        if (done < p.value) return false;
    }
    return true;
}

void ExecuteOn(ExecuteFn forward, ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    auto& s = State();
    std::lock_guard lock(s.mutex);
    CompletionSet uses;
    std::vector<std::pair<ID3D12CommandList*, std::shared_ptr<ExternalWait>>> waits;
    std::vector<std::pair<ID3D12CommandList*, std::shared_ptr<ExecutionObservation>>> observations;
    for (UINT i = 0; i < count; ++i)
    {
        auto ticket = Get<Recording>(lists[i], recordingGuid);
        NR_FRAME_TRACE("queue-execute-enter", "queue={:p} list={:p} ticket={:p} batchCount={} batchIndex={}",
            static_cast<void*>(queue), static_cast<void*>(lists[i]), static_cast<void*>(ticket.get()), count, i);
        if (ticket) uses.push_back(ticket);
        if (auto dependency = Get<ExternalWait>(lists[i], externalWaitGuid))
            waits.push_back({lists[i], std::move(dependency)});
        if (observationCookies.load(std::memory_order_relaxed))
            if (auto observation = Get<ExecutionObservation>(lists[i], executionObservationGuid))
                observations.push_back({lists[i], std::move(observation)});
    }
    if (uses.empty() && waits.empty() && observations.empty()) { forward(queue, count, lists); return; }
    const auto queueType = waits.empty() && observations.empty() ? D3D12_COMMAND_LIST_TYPE_DIRECT : queue->GetDesc().Type;
    for (const auto& [list, observation] : observations)
        if (!SupportedExternalType(queueType) || list->GetType() != queueType) observation->status->failed = true;

    for (const auto& [list, dependency] : waits)
    {
        bool ok = SupportedExternalType(queueType) && list->GetType() == queueType;
        ComPtr<ID3D12Device> producerDevice, consumerDevice;
        if (ok)
            ok = SUCCEEDED(dependency->fence->GetDevice(IID_PPV_ARGS(&producerDevice))) &&
                 SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&consumerDevice))) &&
                 NativeObject(producerDevice.Get()) == NativeObject(consumerDevice.Get());
        const HRESULT waitResult = ok ? queue->Wait(dependency->fence.Get(), dependency->value) : E_INVALIDARG;
        ok = ok && SUCCEEDED(waitResult);
        // Keep every dependency until successful Reset/destruction. If a later
        // list's wait fails, a retry of this batch still needs the earlier waits;
        // clearing cookies piecemeal would let a retry on another queue bypass them.
        NR_FRAME_TRACE("nr-fg-wait-applied", "queue={:p} list={:p} fence={:p} value={} token={} "
            "sequence={} result={} ok={}", static_cast<void*>(queue), static_cast<void*>(list),
            static_cast<void*>(dependency->fence.Get()), dependency->value, dependency->token,
            dependency->sequence, static_cast<unsigned int>(waitResult), ok);
        if (!ok)
        {
            for (const auto& observation : observations) observation.second->status->failed = true;
            for (const auto& pending : waits)
                if (pending.second->status) pending.second->status->Fail();
            // Never submit provider work after its required ordering failed. This
            // API has no HRESULT: retain the dependency and pin affected tickets.
            s.failed = true;
            for (auto& ticket : uses) ticket->failed = true;
#ifndef NR_GPU_SAFETY_TEST
            static std::atomic_flag reported {};
            if (!reported.test_and_set())
                LOG_ERROR("NR-to-FG GPU wait failed; dependent submission refused (HRESULT={:X})",
                          static_cast<unsigned int>(waitResult));
#endif
            return;
        }
        if (dependency->status) dependency->status->applied = true;
    }

    auto timeline = uses.empty() ? std::shared_ptr<Timeline>{} : Get<Timeline>(queue, timelineGuid);
    if (!uses.empty() && !timeline)
    {
        timeline = std::make_shared<Timeline>();
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline->fence))) ||
            !Put(queue, timelineGuid, timeline)) timeline.reset();
        if (timeline && FAILED(queue->GetTimestampFrequency(&timeline->frequency))) timeline->frequency = 0;
    }
    // Recording tickets only observe host ordering. The wait above is the narrow exception: an
    // explicit, exact-resource NR-to-native-FG handoff attached to the provider command list.
    bool ok = uses.empty() || (timeline != nullptr && timeline->next < UINT64_MAX - 1);
    forward(queue, count, lists);
    for (const auto& observation : observations)
        if (!observation.second->status->failed.load()) observation.second->status->submitted = true;
    const UINT64 value = !uses.empty() && ok ? ++timeline->next : 0;
    if (!uses.empty() && ok) ok = SUCCEEDED(queue->Signal(timeline->fence.Get(), value));
    NR_FRAME_TRACE("queue-execute-signaled", "queue={:p} fence={:p} value={} ok={} tickets={}",
        static_cast<void*>(queue), timeline ? static_cast<void*>(timeline->fence.Get()) : nullptr,
        value, ok, uses.size());
    if (!ok)
    {
        s.failed = true;
        for (const auto& observation : observations) observation.second->status->failed = true;
        for (const auto& pending : waits)
            if (pending.second->status) pending.second->status->Fail();
    }
    for (auto& ticket : uses)
    {
        ++ticket->submissions;
        NR_FRAME_TRACE("ticket-submitted", "ticket={:p} queue={:p} submissions={} fence={:p} value={} ok={}",
            static_cast<void*>(ticket.get()), static_cast<void*>(queue), ticket->submissions,
            timeline ? static_cast<void*>(timeline->fence.Get()) : nullptr, value, ok);
        if (!ok) { ticket->failed = true; continue; }
        auto found = std::find_if(ticket->points.begin(), ticket->points.end(),
                                 [&](const Point& p) { return p.timeline == timeline; });
        if (found == ticket->points.end()) ticket->points.push_back({timeline, value});
        else found->value = value; // also covers replay before Reset
    }
}

void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{ ExecuteOn(originalExecute, queue, count, lists); }
void STDMETHODCALLTYPE ExecuteAlternate(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{ ExecuteOn(alternateExecute, queue, count, lists); }

HRESULT ResetOn(ResetFn forward, ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                 ID3D12PipelineState* pipeline)
{
    std::lock_guard lock(State().mutex);
    const HRESULT hr = forward(list, allocator, pipeline);
    NR_FRAME_TRACE("command-list-reset", "list={:p} result={}", static_cast<void*>(list), static_cast<unsigned int>(hr));
    if (SUCCEEDED(hr))
    {
        if (observationCookies.load(std::memory_order_relaxed))
            if (auto observation = Get<ExecutionObservation>(list, executionObservationGuid))
                if (FAILED(list->SetPrivateDataInterface(executionObservationGuid, nullptr)))
                    observation->status->failed = true;
        if (auto t = Get<Recording>(list, recordingGuid))
        {
            if (SUCCEEDED(list->SetPrivateDataInterface(recordingGuid, nullptr))) t->sealed = true;
            else { t->failed = true; State().failed = true; }
        }
        if (auto dependency = Get<ExternalWait>(list, externalWaitGuid))
            if (FAILED(list->SetPrivateDataInterface(externalWaitGuid, nullptr)))
            {
                State().failed = true;
                if (dependency->status) dependency->status->Fail();
            }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                 ID3D12PipelineState* pipeline)
{ return ResetOn(originalReset, list, allocator, pipeline); }
HRESULT STDMETHODCALLTYPE ResetAlternate(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                          ID3D12PipelineState* pipeline)
{ return ResetOn(alternateReset, list, allocator, pipeline); }

bool EnsureHooks(ID3D12GraphicsCommandList* list, bool verifyQueue = false)
{
    std::lock_guard lock(State().hookMutex);
    void* target = (*(void***)list)[10];
    // Preserve the established NR recording fast path. External FG consumers
    // additionally prove the queue implementation for their actual list type.
    if (!verifyQueue && originalReset && target == resetTarget) return true;
    const auto type = list->GetType();
    if (!SupportedExternalType(type)) return false;
    if (auto proof = Get<HookProof>(list, hookProofGuid))
        if (proof->reset == target && proof->type == type) return true;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc {};
    desc.Type = type;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) return false;
    ID3D12CommandQueue* nativeQueue = NativeObject(queue.Get());
    void* queueTarget = (*(void***)nativeQueue)[10];
    const bool haveExecute = (originalExecute && queueTarget == executeTarget) ||
                             (alternateExecute && queueTarget == alternateExecuteTarget);
    const bool haveReset = (originalReset && target == resetTarget) ||
                           (alternateReset && target == alternateResetTarget);
    if ((!haveExecute && originalExecute && alternateExecute) ||
        (!haveReset && originalReset && alternateReset)) return false;
    // DIRECT and COMPUTE can share implementation addresses, or have distinct
    // ones. Hook each address once, retaining the correct original trampoline.
    ExecuteFn* executeSlot = haveExecute ? nullptr : !originalExecute ? &originalExecute : &alternateExecute;
    ResetFn* resetSlot = haveReset ? nullptr : !originalReset ? &originalReset : &alternateReset;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Execute), &self)) return false;
    if (executeSlot || resetSlot)
    {
        if (DetourTransactionBegin() != NO_ERROR) return false;
        LONG result = DetourUpdateThread(GetCurrentThread());
        if (executeSlot)
        {
            *executeSlot = reinterpret_cast<ExecuteFn>(queueTarget);
            if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(executeSlot),
                executeSlot == &originalExecute ? Execute : ExecuteAlternate);
        }
        if (resetSlot)
        {
            *resetSlot = reinterpret_cast<ResetFn>(target);
            if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(resetSlot),
                resetSlot == &originalReset ? Reset : ResetAlternate);
        }
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
        if (result != NO_ERROR)
        {
            if (executeSlot) *executeSlot = nullptr;
            if (resetSlot) *resetSlot = nullptr;
            return false;
        }
        if (executeSlot) (executeSlot == &originalExecute ? executeTarget : alternateExecuteTarget) = queueTarget;
        if (resetSlot) (resetSlot == &originalReset ? resetTarget : alternateResetTarget) = target;
    }
    return Put(list, hookProofGuid, std::make_shared<HookProof>(HookProof {target, type}));
}
}

std::shared_ptr<ExternalExecutionStatus> ObserveExternalExecution(ID3D12GraphicsCommandList* list, const char** reason)
{
    if (reason) *reason = "null-list";
    if (!list) return {};
    list = NativeObject(list);
    if (reason) *reason = "unsupported-list-type";
    if (!SupportedExternalType(list->GetType())) return {};
    if (reason) *reason = "submission/reset-hooks-unavailable";
    if (!EnsureHooks(list, true)) return {};
    std::lock_guard lock(State().mutex);
    if (reason) *reason = "gpu-safety-registry-failed";
    if (State().failed) return {};
    auto observation = std::make_shared<ExecutionObservation>();
    if (reason) *reason = "observation-cookie-write-failed";
    if (!Put(list, executionObservationGuid, observation)) return {};
    if (reason) *reason = "waiting for successful FG evaluation and queue submission";
    return observation->status;
}

Ticket Record(ID3D12GraphicsCommandList* list)
{
    if (!list || (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT &&
                  list->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)) return {};
    list = NativeObject(list);
    if (!EnsureHooks(list)) return Unavailable("command-list completion hooks unavailable");
    auto& s = State();
    std::lock_guard lock(s.mutex);
    if (s.failed) return Unavailable("submission/fence failure; process restart required");
    if (auto t = Get<Recording>(list, recordingGuid))
    {
        NR_FRAME_TRACE("ticket-record-existing", "list={:p} ticket={:p}", static_cast<void*>(list), static_cast<void*>(t.get()));
        return t;
    }
    std::erase_if(s.pending, [](const Ticket& t) { return t->sealed && Completed(t); });
    if (s.failed) return Unavailable("device loss; process restart required");
    if (s.pending.size() >= 256) return Unavailable("recording capacity exhausted");
    auto ticket = std::make_shared<Recording>();
    if (!Put(list, recordingGuid, ticket)) return Unavailable("command-list lifetime cookie failed");
    s.pending.push_back(ticket);
    NR_FRAME_TRACE("ticket-record-new", "list={:p} ticket={:p}", static_cast<void*>(list), static_cast<void*>(ticket.get()));
    return ticket;
}
bool SealOwnedRecording(ID3D12GraphicsCommandList* list)
{
    if (!list) return false;
    list = NativeObject(list);
    auto& s = State();
    std::lock_guard lock(s.mutex);
    auto ticket = Get<Recording>(list, recordingGuid);
    if (!ticket || ticket->failed || ticket->submissions == 0) return false;
    if (FAILED(list->SetPrivateDataInterface(recordingGuid, nullptr)))
    {
        ticket->failed = true;
        s.failed = true;
        return false;
    }
    return ticket->sealed;
}
bool OrderedOn(const Ticket& ticket, ID3D12CommandQueue* queue)
{
    if (!ticket || !queue) return false;
    std::lock_guard lock(State().mutex);
    if (State().failed || ticket->failed || ticket->submissions != 1 || ticket->points.size() != 1)
        return false;
    auto timeline = Get<Timeline>(NativeObject(queue), timelineGuid);
    return timeline && timeline == ticket->points.front().timeline &&
           timeline->fence->GetCompletedValue() != UINT64_MAX;
}
bool Reusable(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return !ticket || (ticket->sealed && Completed(ticket));
}
bool OrderBefore(const Ticket& ticket, ID3D12CommandQueue* consumer)
{
    if (!ticket || !consumer) return false;
    std::lock_guard lock(State().mutex);
    if (State().failed || ticket->failed || ticket->submissions != 1 || ticket->points.size() != 1)
        return false;
    const auto& point = ticket->points.front();
    if (point.timeline->fence->GetCompletedValue() == UINT64_MAX) return false;
    if (Get<Timeline>(NativeObject(consumer), timelineGuid) == point.timeline)
    {
        // Already submitted on this queue: later consumer work follows the producer.
        // Reset/destruction seals recording lifetime, not queue submission order.
        // Reusable/Readable still require sealing; observed replay still refuses.
        NR_FRAME_TRACE("nr-guide-same-queue-order", "queue={:p} fence={:p} value={} sealed={}",
            static_cast<void*>(consumer), static_cast<void*>(point.timeline->fence.Get()), point.value, ticket->sealed);
        return true;
    }
    if (!ticket->sealed) return false; // cross-queue replay cannot be ordered by this fence alone
    if (consumer->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
    ComPtr<ID3D12Device> producerDevice, consumerDevice;
    if (FAILED(point.timeline->fence->GetDevice(IID_PPV_ARGS(&producerDevice))) ||
        FAILED(consumer->GetDevice(IID_PPV_ARGS(&consumerDevice))) ||
        NativeObject(producerDevice.Get()) != NativeObject(consumerDevice.Get())) return false;
    const auto result = consumer->Wait(point.timeline->fence.Get(), point.value);
    NR_FRAME_TRACE("nr-guide-gpu-wait", "queue={:p} fence={:p} value={} result={}",
        static_cast<void*>(consumer), static_cast<void*>(point.timeline->fence.Get()),
        point.value, static_cast<unsigned int>(result));
    return SUCCEEDED(result);
}
bool BindExternalWait(ID3D12GraphicsCommandList* list, ID3D12Fence* producerFence,
                      UINT64 producerValue, UINT64 token, UINT64 sequence,
                      std::shared_ptr<ExternalWaitStatus> status)
{
    const auto refuse = [&](const char* reason) {
        if (status) status->Fail();
        NR_FRAME_TRACE("nr-fg-wait-bind-refused", "reason={} list={:p} type={} token={} sequence={}",
            reason, static_cast<void*>(list), list ? static_cast<unsigned int>(list->GetType()) : UINT_MAX, token, sequence);
#ifndef NR_GPU_SAFETY_TEST
        static std::atomic<uint64_t> refusals {0};
        const auto count = ++refusals;
        if (count <= 8 || count % 120 == 0)
            LOG_WARN("NR Present wait binding refused: reason={} list={:p} type={} token={} sequence={} count={}",
                reason, static_cast<void*>(list), list ? static_cast<unsigned int>(list->GetType()) : UINT_MAX, token, sequence, count);
#else
        (void)reason;
#endif
        return false;
    };
    if (!list || !producerFence || !producerValue || producerValue == UINT64_MAX)
        return refuse("invalid-dependency");
    list = NativeObject(list);
    if (!SupportedExternalType(list->GetType())) return refuse("unsupported-list-type");
    producerFence = NativeObject(producerFence);
    if (!EnsureHooks(list, true)) return refuse("completion-hooks-unavailable");
    std::lock_guard lock(State().mutex);
    if (State().failed) return refuse("gpu-safety-registry-failed");
    auto previous = Get<ExternalWait>(list, externalWaitGuid);
    if (previous)
    {
        if (previous->token == token && previous->sequence == sequence)
            return refuse("duplicate-dependency");
        const auto completed = previous->fence->GetCompletedValue();
        if (completed == UINT64_MAX) return refuse("previous-producer-device-lost");
        if (completed < previous->value) return refuse("previous-producer-incomplete");
        // A completed producer needs no future queue wait, including list replay.
        // Replace only after this proof; do not erase an unfinished dependency.
        // An unobserved old wait still fails its readiness proof on destruction.
    }
    ComPtr<ID3D12Device> listDevice, fenceDevice;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&listDevice))) ||
        FAILED(producerFence->GetDevice(IID_PPV_ARGS(&fenceDevice))) ||
        NativeObject(listDevice.Get()) != NativeObject(fenceDevice.Get())) return refuse("foreign-or-unavailable-device");
    auto dependency = std::make_shared<ExternalWait>();
    dependency->fence = producerFence;
    dependency->value = producerValue;
    dependency->token = token;
    dependency->sequence = sequence;
    dependency->status = status;
    const bool bound = Put(list, externalWaitGuid, dependency);
    if (dependency->status)
    {
        if (bound) dependency->status->bound = true;
        else dependency->status->Fail();
    }
    NR_FRAME_TRACE("nr-fg-wait-bound", "list={:p} fence={:p} value={} token={} sequence={} ok={}",
        static_cast<void*>(list), static_cast<void*>(producerFence), producerValue, token, sequence, bound);
    if (!bound) return refuse("private-dependency-write-failed");
    return true;
}
SlotSnapshot InspectSlots(const Ticket* tickets, unsigned int count)
{
    std::lock_guard lock(State().mutex);
    SlotSnapshot snapshot;
    snapshot.registryFailed = State().failed;
    for (unsigned int i = 0; i < count; ++i)
    {
        const auto& ticket = tickets[i];
        if (!ticket) { ++snapshot.reusable; continue; }
        bool failed = ticket->failed;
        bool pending = false;
        for (const auto& point : ticket->points)
        {
            const UINT64 done = point.timeline->fence->GetCompletedValue();
            failed = failed || done == UINT64_MAX;
            pending = pending || done < point.value;
        }
        if (failed) ++snapshot.failed;
        else if (pending) ++snapshot.gpuPending;
        else if (ticket->sealed) ++snapshot.reusable;
        else if (ticket->points.empty()) ++snapshot.unsubmittedUnsealed;
        else ++snapshot.completedUnsealed;
    }
    return snapshot;
}
bool Readable(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return ticket && !ticket->points.empty() && ticket->sealed && Completed(ticket);
}
UINT64 TimestampFrequency(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return ticket && ticket->points.size() == 1 ? ticket->points[0].timeline->frequency : 0;
}
CompletionSet Pending()
{
    std::lock_guard lock(State().mutex);
    std::erase_if(State().pending, [](const Ticket& t) { return t->sealed && Completed(t); });
    return State().pending;
}
bool Reusable(const CompletionSet& tickets)
{
    return std::all_of(tickets.begin(), tickets.end(), [](const Ticket& t) { return Reusable(t); });
}
bool Drain(unsigned int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;)
    {
        {
            std::lock_guard lock(State().mutex);
            if (State().failed) return false;
            bool complete = true;
            for (const auto& t : State().pending)
            {
                // An unsubmitted live recording cannot be drained by signaling any queue.
                if (!t->sealed && t->points.empty()) return false;
                complete = Completed(t) && complete;
            }
            if (complete) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        Sleep(1);
    }
}
void NewSession()
{
    std::lock_guard lock(State().mutex);
    // Called only after successful host shutdown. Old completed recordings must not be replayed.
    State().pending.clear();
}
}
