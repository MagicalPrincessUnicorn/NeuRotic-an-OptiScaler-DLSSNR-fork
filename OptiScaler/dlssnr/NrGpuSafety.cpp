#ifndef NR_GPU_SAFETY_TEST
#include "pch.h"
#include <SysUtils.h>
#include <Logger.h>
#include "FrameTrace.h"
#else
#define NR_FRAME_TRACE(...) do {} while (false)
#endif
#include "NrGpuSafety.h"
#include "NativeIdentity.h"
#include <nr/d3d12/NativeRecordingInterfaces.h>
#if !defined(NR_GPU_SAFETY_TEST) || defined(NR_GPU_SAFETY_DIAGNOSTICS_TEST)
#include "DredDiagnostics.h"
#endif
#include <windows.h>
#include <detours/detours.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <type_traits>

namespace DlssNr::GpuSafety
{
using Microsoft::WRL::ComPtr;
struct Timeline
{
    UINT64 identity = 0;
    ComPtr<ID3D12Fence> fence;
    UINT64 next = 0;
    UINT64 frequency = 0;
};
struct Point { std::shared_ptr<Timeline> timeline; UINT64 value; };
struct DerivedSnapshotJob
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    Ticket producer;
    bool submitted=false,cancelled=false;
};
struct Recording
{
    UINT64 incarnation = 0;
    RecordingTerminalState terminal = RecordingTerminalState::None;
    bool sealed = false;
    bool failed = false;
    UINT64 submissions = 0;
    std::vector<Point> points;
    // GPU payloads are released only with this recording's terminal lifetime.
    // Payloads must not own their retaining ticket (which would form a cycle).
    std::vector<std::shared_ptr<void>> derivedUses;
    // No job owns this source ticket. Its completion fence covers the owned
    // copy too, retaining the copy allocator/list until all that work retires.
    std::vector<std::shared_ptr<DerivedSnapshotJob>> snapshotExports;
};
struct ExternalWait
{
    ComPtr<ID3D12Fence> fence;
    UINT64 value = 0;
    UINT64 token = 0;
    UINT64 sequence = 0;
    std::shared_ptr<ExternalWaitStatus> status;
    std::vector<std::shared_ptr<ExternalWaitStatus>> siblings;
    void Fail()
    {
        if (status) status->Fail();
        for (const auto& child : siblings) child->Fail();
    }
    void Applied()
    {
        if (status) status->MarkApplied();
        for (const auto& child : siblings) child->MarkApplied();
    }
    ~ExternalWait()
    {
        // Reset/destruction before execution is cancellation, not wait evidence.
        if (status && !status->applied.load()) status->Fail();
        for (const auto& child : siblings) if (!child->applied.load()) child->Fail();
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
constexpr GUID recordingStateGuid = {0x5ee0e24c, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using CloseFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
ExecuteFn originalExecute = nullptr;
ResetFn originalReset = nullptr;
CloseFn originalClose = nullptr, alternateClose = nullptr;
void* closeTarget = nullptr;
void* alternateCloseTarget = nullptr;
void* resetTarget = nullptr;
ExecuteFn alternateExecute = nullptr;
ResetFn alternateReset = nullptr;
void* executeTarget = nullptr;
void* alternateExecuteTarget = nullptr;
void* alternateResetTarget = nullptr;
struct HookProof { void* reset; void* close; D3D12_COMMAND_LIST_TYPE type; };
struct RecordingState
{
    bool open=false,active=false;
    UINT64 revision=0;
    std::thread::id owner;
};
// Callbacks can outlive explicit NGX sessions. This small registry and its hooks have process
// lifetime; individual tickets/fences are reclaimed. No owning reference back to a list/queue.
struct Registry
{
    UINT64 nextRecording = 0, nextTimeline = 0;
    std::recursive_mutex mutex;
    std::condition_variable_any actionFinished;
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
                if(data->terminal==RecordingTerminalState::None)
                    data->terminal=RecordingTerminalState::LifetimeEnded;
                for(const auto& job:data->snapshotExports)if(!job->submitted&&!job->cancelled) {
                    job->cancelled=true;job->list.Reset();
                }
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

// Compare the actual interface-valued private cookies. A wrapper must expose
// the same metadata used by the terminal Reset/Execute hooks; pointer aliases
// alone never establish that lifetime contract. Foreign wrapper calls happen
// outside State().mutex, and callers revalidate the recording afterwards.
ComPtr<IUnknown> CookieIdentity(ID3D12Object* object,REFGUID key)
{
    IUnknown* raw=nullptr;UINT bytes=sizeof(raw);
    const auto result=object->GetPrivateData(key,&bytes,&raw);
    ComPtr<IUnknown> value;value.Attach(raw);
    if(FAILED(result)||bytes!=sizeof(raw))value.Reset();
    return value;
}
struct CookieJoin
{
    ComPtr<IUnknown> caller,terminal;
    explicit operator bool()const noexcept{return caller&&caller.Get()==terminal.Get();}
};
CookieJoin JoinCookie(const Neurotic::D3D12::NativeRecordingInterfaces& interfaces,REFGUID key)
{
    CookieJoin joined;
    joined.caller=CookieIdentity(interfaces.caller.Get(),key);
    joined.terminal=CookieIdentity(interfaces.terminal.Get(),key);
    return joined;
}
bool CookieStillCurrent(ID3D12Object* terminal,REFGUID key,const CookieJoin& joined)
{
    return joined&&CookieIdentity(terminal,key).Get()==joined.terminal.Get();
}
void AdvanceRecordingState(const std::shared_ptr<RecordingState>& state)
{
    if(!state)return;
    if(state->revision==UINT64_MAX)State().failed=true;
    else ++state->revision;
}

// The registry lock is released while another thread owns this exact list.
// Unrelated lists/queues continue; no host API call is dropped or replaced.
void AwaitAction(ID3D12Object* list,std::unique_lock<std::recursive_mutex>& lock)
{
    State().actionFinished.wait(lock,[&]{
        const auto state=Get<RecordingState>(list,recordingStateGuid);
        return !state||!state->active||state->owner==std::this_thread::get_id();});
}
template<class T> T* NativeObject(T* object)
{
    // Same Streamline native-object interface as Util::CheckForRealObject, without its mutable
    // lazy GUID initialization or per-call log (this runs for every NR dispatch).
    constexpr GUID nativeGuid = {0xadec44e2, 0x61f0, 0x45c3, {0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    T* real = nullptr;
    if (SUCCEEDED(object->QueryInterface(nativeGuid, reinterpret_cast<void**>(&real))) && real)
    {
        real->Release(); // the caller's live wrapper owns the borrowed native object
        return real;
    }
    return object;
}

bool SameDevice(IUnknown* left, IUnknown* right, NativeIdentity::DeviceComparison* details = nullptr)
{
    // Device interfaces/base wrappers need not share an address. Use the same
    // bounded resolver and canonical COM identity as the Present producer.
    auto comparison = NativeIdentity::CompareDevices(left, right);
    const bool equal = comparison.equal;
    if (details) *details = std::move(comparison);
    return equal;
}

bool Completed(const Ticket& t)
{
    if (!t || t->failed) return !t;
    for (const auto& p : t->points)
    {
        const UINT64 done = p.timeline->fence->GetCompletedValue();
        if (done == UINT64_MAX)
        {
            State().failed = true;
#if !defined(NR_GPU_SAFETY_TEST) || defined(NR_GPU_SAFETY_DIAGNOSTICS_TEST)
            // A provider can terminate on device loss before Present returns. Retain
            // diagnostics at this first observed loss, using the failed fence's device.
            // No device query or report work on normal/diagnostics-disabled frames.
            if (DredDiagnostics::Enabled() && !DredDiagnostics::reported.load())
            {
                ComPtr<ID3D12Device> device;
                if (SUCCEEDED(p.timeline->fence->GetDevice(IID_PPV_ARGS(&device))) && device)
                    DredDiagnostics::Collect(device.Get(), device->GetDeviceRemovedReason());
            }
#endif
            return false;
        }
        if (done < p.value) return false;
    }
    return true;
}

// The source can legally replay after its own execution finishes while the
// separately queued copy is pending. Order that replay after our immutable
// export on the actual DIRECT or COMPUTE queue, without changing OrderBefore's
// older provider-handoff contract.
bool OrderSnapshotReplay(const DerivedSnapshotJob& job,ID3D12CommandQueue* queue)
{
    const auto& ticket=job.producer;
    if(!job.submitted||!job.list||!ticket||State().failed||ticket->failed||!ticket->sealed||
       ticket->submissions!=1||ticket->points.size()!=1)return false;
    const auto type=queue->GetDesc().Type;
    if(!SupportedExternalType(type)||job.list->GetType()!=type)return false;
    const auto& point=ticket->points.front();
    ComPtr<ID3D12Device> producerDevice,consumerDevice;
    if(FAILED(point.timeline->fence->GetDevice(IID_PPV_ARGS(&producerDevice)))||
       FAILED(queue->GetDevice(IID_PPV_ARGS(&consumerDevice)))||
       !SameDevice(producerDevice.Get(),consumerDevice.Get()))return false;
    const auto completed=point.timeline->fence->GetCompletedValue();
    if(completed==UINT64_MAX)return false;
    return completed>=point.value||SUCCEEDED(queue->Wait(point.timeline->fence.Get(),point.value));
}

void ExecuteOn(ExecuteFn forward, ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    auto& s = State();
    std::unique_lock lock(s.mutex);
    // Recheck the whole batch after every wait: another list may acquire a
    // borrow while this wait temporarily releases the registry lock.
    s.actionFinished.wait(lock,[&]{
        for(UINT i=0;i<count;++i)
            if(const auto state=Get<RecordingState>(lists[i],recordingStateGuid))
                if(state->active&&state->owner!=std::this_thread::get_id())return false;
        return true;});
    for(UINT i=0;i<count;++i)
        if(const auto state=Get<RecordingState>(lists[i],recordingStateGuid)) {
            state->open=false;AdvanceRecordingState(state);
        }
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
    for(const auto& ticket:uses)for(const auto& job:ticket->snapshotExports)
    {
        if(job->cancelled)continue;
        if(ticket->submissions==0&&std::count(uses.begin(),uses.end(),ticket)!=1) {
            // More than one first-batch execution cannot export an exact first
            // source result. Cancel only our unused copy; preserve host work.
            job->cancelled=true;job->list.Reset();continue;
        }
        if(ticket->submissions!=0&&!OrderSnapshotReplay(*job,queue)) {
            s.failed=true;ticket->failed=true;job->producer->failed=true;
            return; // Never let replay race the first snapshot copy.
        }
    }
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
                 SameDevice(producerDevice.Get(), consumerDevice.Get());
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
                pending.second->Fail();
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
        dependency->Applied();
    }

    auto timeline = uses.empty() ? std::shared_ptr<Timeline>{} : Get<Timeline>(queue, timelineGuid);
    if (!uses.empty() && !timeline)
    {
        timeline = std::make_shared<Timeline>();
        if(s.nextTimeline==UINT64_MAX) { s.failed=true; for(auto& t:uses)t->failed=true; }
        else timeline->identity=++s.nextTimeline;
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline->fence))) ||
            !Put(queue, timelineGuid, timeline)) timeline.reset();
        if (timeline && FAILED(queue->GetTimestampFrequency(&timeline->frequency))) timeline->frequency = 0;
    }
    // Recording tickets only observe host ordering. The wait above is the narrow exception:
    // an explicit provider handoff or immutable derived-image dependency attached to this list.
    bool ok = uses.empty() || (timeline != nullptr && timeline->next < UINT64_MAX - 1);
    forward(queue, count, lists);
    // These are pre-recorded copies of private staging images, on the actual
    // queue and immediately after the source batch. The parent signal below
    // also covers each copy's allocator/list lifetime. Only owned lists seal.
    for(const auto& ticket:uses)if(ticket->submissions==0)
        for(const auto& job:ticket->snapshotExports)if(!job->submitted&&!job->cancelled) {
            job->submitted=true;ID3D12CommandList* copy[]{job->list.Get()};
            ExecuteOn(forward,queue,1,copy);
            if(!SealOwnedRecording(job->list.Get())||job->producer->failed||
               job->producer->submissions!=1) {
                s.failed=true;ticket->failed=true;job->producer->failed=true;ok=false;
            }
        }
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
        if(ticket->submissions==UINT64_MAX){ticket->failed=true;s.failed=true;continue;}
        ++ticket->submissions;
        NR_FRAME_TRACE("ticket-submitted", "ticket={:p} queue={:p} submissions={} fence={:p} value={} ok={} incarnation={}",
            static_cast<void*>(ticket.get()), static_cast<void*>(queue), ticket->submissions,
            timeline ? static_cast<void*>(timeline->fence.Get()) : nullptr, value, ok, ticket->incarnation);
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
    std::unique_lock lock(State().mutex);
    AwaitAction(list,lock);
    auto state=Get<RecordingState>(list,recordingStateGuid);
    if(state)state->open=false;
    const HRESULT hr = forward(list, allocator, pipeline);
    NR_FRAME_TRACE("command-list-reset", "list={:p} result={}", static_cast<void*>(list), static_cast<unsigned int>(hr));
    if (SUCCEEDED(hr))
    {
        if(state){state->open=true;AdvanceRecordingState(state);}
        if (observationCookies.load(std::memory_order_relaxed))
            if (auto observation = Get<ExecutionObservation>(list, executionObservationGuid))
                if (FAILED(list->SetPrivateDataInterface(executionObservationGuid, nullptr)))
                    observation->status->failed = true;
        if (auto t = Get<Recording>(list, recordingGuid))
        {
            if (SUCCEEDED(list->SetPrivateDataInterface(recordingGuid, nullptr)))
            {t->sealed = true;t->terminal=RecordingTerminalState::Reset;
             for(const auto& job:t->snapshotExports)if(!job->submitted&&!job->cancelled) {
                 job->cancelled=true;job->list.Reset();
             }}
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

HRESULT CloseOn(CloseFn forward,ID3D12GraphicsCommandList* list)
{
    std::unique_lock lock(State().mutex);
    AwaitAction(list,lock);
    if(const auto state=Get<RecordingState>(list,recordingStateGuid)) {
        state->open=false;AdvanceRecordingState(state);
    }
    return forward(list);
}
HRESULT STDMETHODCALLTYPE Close(ID3D12GraphicsCommandList* list){return CloseOn(originalClose,list);}
HRESULT STDMETHODCALLTYPE CloseAlternate(ID3D12GraphicsCommandList* list){return CloseOn(alternateClose,list);}

// Uses the existing bounded, opt-in frame trace. No extra COM/device query.
void ObservationStage(const char** requested, const char* stage, const char* phase,
                      ID3D12GraphicsCommandList* list, HRESULT result = S_OK)
{
    if (!requested) return;
    NR_FRAME_TRACE("nr-api", "api=fg-observation stage={} phase={} list={:p} result={:X}",
        stage, phase, static_cast<void*>(list), static_cast<unsigned int>(result));
    (void)stage; (void)phase; (void)list; (void)result;
}
bool EnsureHooks(ID3D12GraphicsCommandList* list, bool verifyQueue = false, const char** reason = nullptr)
{
    if (reason) *reason = "hook-lock";
    ObservationStage(reason,"hook-lock","enter",list);
    std::lock_guard lock(State().hookMutex);
    ObservationStage(reason,"hook-lock","return",list);
    void* target = (*(void***)list)[10];
    void* close = (*(void***)list)[9];
    // Preserve the established NR recording fast path. External FG consumers
    // additionally prove the queue implementation for their actual list type.
    if (!verifyQueue && originalReset && target == resetTarget && originalClose && close==closeTarget) return true;
    const auto type = list->GetType();
    if (!SupportedExternalType(type)) { if(reason)*reason="unsupported-list-type"; return false; }
    if (auto proof = Get<HookProof>(list, hookProofGuid))
        if (proof->reset == target && proof->close==close && proof->type == type) return true;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc {};
    desc.Type = type;
    if(reason)*reason="consumer-get-device";
    ObservationStage(reason,"get-device","enter",list);
    const HRESULT deviceResult=list->GetDevice(IID_PPV_ARGS(&device));
    ObservationStage(reason,"get-device","return",list,deviceResult);
    if(FAILED(deviceResult))return false;
    if(reason)*reason="consumer-create-queue";
    ObservationStage(reason,"create-queue","enter",list);
    const HRESULT queueResult=device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue));
    ObservationStage(reason,"create-queue","return",list,queueResult);
    if(FAILED(queueResult))return false;
    ID3D12CommandQueue* nativeQueue = NativeObject(queue.Get());
    void* queueTarget = (*(void***)nativeQueue)[10];
    const bool haveExecute = (originalExecute && queueTarget == executeTarget) ||
                             (alternateExecute && queueTarget == alternateExecuteTarget);
    const bool haveReset = (originalReset && target == resetTarget) ||
                           (alternateReset && target == alternateResetTarget);
    const bool haveClose = (originalClose && close==closeTarget) ||
                          (alternateClose && close==alternateCloseTarget);
    if (!haveExecute && originalExecute && alternateExecute)
    { if(reason)*reason="execute-hook-slot-capacity"; return false; }
    if (!haveReset && originalReset && alternateReset)
    { if(reason)*reason="reset-hook-slot-capacity"; return false; }
    if (!haveClose && originalClose && alternateClose)
    { if(reason)*reason="close-hook-slot-capacity"; return false; }
    // DIRECT and COMPUTE can share implementation addresses, or have distinct
    // ones. Hook each address once, retaining the correct original trampoline.
    ExecuteFn* executeSlot = haveExecute ? nullptr : !originalExecute ? &originalExecute : &alternateExecute;
    ResetFn* resetSlot = haveReset ? nullptr : !originalReset ? &originalReset : &alternateReset;
    CloseFn* closeSlot = haveClose ? nullptr : !originalClose ? &originalClose : &alternateClose;
    HMODULE self = nullptr;
    if(reason)*reason="hook-module-pin";
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Execute), &self)) return false;
    if (executeSlot || resetSlot || closeSlot)
    {
        if(reason)*reason="hook-detour-transaction";
        ObservationStage(reason,"detour-transaction","enter",list);
        const LONG begin=DetourTransactionBegin();
        if(begin!=NO_ERROR) {
            ObservationStage(reason,"detour-begin","return",list,HRESULT_FROM_WIN32(begin));
            return false;
        }
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
        if (closeSlot)
        {
            *closeSlot=reinterpret_cast<CloseFn>(close);
            if(result==NO_ERROR)result=DetourAttach(reinterpret_cast<PVOID*>(closeSlot),
                closeSlot==&originalClose?Close:CloseAlternate);
        }
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
        ObservationStage(reason,"detour-transaction","return",list,HRESULT_FROM_WIN32(result));
        if (result != NO_ERROR)
        {
            if (executeSlot) *executeSlot = nullptr;
            if (resetSlot) *resetSlot = nullptr;
            if (closeSlot) *closeSlot = nullptr;
            return false;
        }
        if (executeSlot) (executeSlot == &originalExecute ? executeTarget : alternateExecuteTarget) = queueTarget;
        if (resetSlot) (resetSlot == &originalReset ? resetTarget : alternateResetTarget) = target;
        if (closeSlot) (closeSlot == &originalClose ? closeTarget : alternateCloseTarget) = close;
    }
    if(reason)*reason="hook-proof-cookie-write";
    ObservationStage(reason,"hook-proof-cookie","enter",list);
    const bool stored=Put(list,hookProofGuid,std::make_shared<HookProof>(HookProof {target,close,type}));
    if(reason) {
        NR_FRAME_TRACE("nr-api","api=fg-observation stage=hook-proof-cookie phase=return list={:p} stored={}",
            static_cast<void*>(list),stored);
    }
    return stored;
}
}

static bool PrepareRecordingHooksObserved(ID3D12GraphicsCommandList* list,const char** reason)
{
    if(reason)*reason="recording-interface-unavailable";
    const auto interfaces=Neurotic::D3D12::NativeRecordingInterfaces::Resolve(list);
    if(FAILED(interfaces.result)||!interfaces.terminal)return false;
    list=interfaces.terminal.Get();
    if(!EnsureHooks(list,false,reason))return false;
    if(reason)*reason="recording-registry-or-cookie-write";
    ObservationStage(reason,"recording-cookie","enter",list);
    std::shared_ptr<RecordingState> recordingState;UINT64 revision=0;
    {
        std::lock_guard lock(State().mutex);
        if(State().failed) { if(reason)*reason="gpu-safety-registry-failed"; return false; }
        if(reason)*reason="recording-cookie-write";
        recordingState=Get<RecordingState>(list,recordingStateGuid);
        if(!recordingState) {
            recordingState=std::make_shared<RecordingState>();
            if(!Put(list,recordingStateGuid,recordingState))return false;
        }
        revision=recordingState->revision;
    }
    ObservationStage(reason,"recording-cookie","return",list);
    if(reason)*reason="recording-cookie-continuity-or-revision";
    ObservationStage(reason,"recording-continuity","enter",list);
    const auto joined=JoinCookie(interfaces,recordingStateGuid);
    std::lock_guard lock(State().mutex);
    const bool current=!State().failed&&CookieStillCurrent(list,recordingStateGuid,joined)&&
        Get<RecordingState>(list,recordingStateGuid)==recordingState&&recordingState->revision==revision;
    if(reason) {
        NR_FRAME_TRACE("nr-api","api=fg-observation stage=recording-continuity phase=return "
            "list={:p} callerCookie={} terminalCookie={} sameCookie={} revision={} currentRevision={} healthy={} current={}",
            static_cast<void*>(list),bool(joined.caller),bool(joined.terminal),bool(joined),revision,
            recordingState->revision,!State().failed,current);
    }
    return current;
}
bool PrepareRecordingHooks(ID3D12GraphicsCommandList* list)
{ return PrepareRecordingHooksObserved(list,nullptr); }

std::shared_ptr<ExternalExecutionStatus> ObserveExternalExecution(ID3D12GraphicsCommandList* list, const char** reason)
{
    if(reason)*reason="list-observation-interface-unavailable";
    ObservationStage(reason,"consumer-interface","enter",list);
    const auto interfaces=Neurotic::D3D12::NativeRecordingInterfaces::Resolve(list);
    ObservationStage(reason,"consumer-interface","return",list,interfaces.result);
    if(FAILED(interfaces.result)||!interfaces.terminal)return {};
    list=interfaces.terminal.Get();
    if(!PrepareRecordingHooksObserved(interfaces.caller.Get(),reason)||!EnsureHooks(list,true,reason))return {};
    auto observation=std::make_shared<ExecutionObservation>();
    std::shared_ptr<RecordingState> recordingState;UINT64 revision=0;
    {
        std::lock_guard lock(State().mutex);
        if(reason)*reason="gpu-safety-registry-failed";
        if(State().failed)return {};
        recordingState=Get<RecordingState>(list,recordingStateGuid);
        if(!recordingState)return {};
        revision=recordingState->revision;
        if(reason)*reason="observation-cookie-write-failed";
        ObservationStage(reason,"execution-cookie","enter",list);
        const bool stored=Put(list,executionObservationGuid,observation);
        if(reason) {
            NR_FRAME_TRACE("nr-api","api=fg-observation stage=execution-cookie phase=return list={:p} stored={}",
                static_cast<void*>(list),stored);
        }
        if(!stored)return {};
    }
    ObservationStage(reason,"execution-continuity","enter",list);
    const auto joined=JoinCookie(interfaces,executionObservationGuid);
    std::lock_guard lock(State().mutex);
    if(reason) {
        NR_FRAME_TRACE("nr-api","api=fg-observation stage=execution-continuity phase=joined "
            "list={:p} caller={:p} callerCookie={} terminalCookie={} sameCookie={} revision={} currentRevision={} healthy={}",
            static_cast<void*>(list),static_cast<void*>(interfaces.caller.Get()),bool(joined.caller),
            bool(joined.terminal),bool(joined),revision,recordingState->revision,!State().failed);
    }
    if(State().failed||!CookieStillCurrent(list,executionObservationGuid,joined)||
       Get<ExecutionObservation>(list,executionObservationGuid)!=observation||
       Get<RecordingState>(list,recordingStateGuid)!=recordingState||recordingState->revision!=revision) {
        observation->status->failed=true;
        if(reason)*reason="observation-cookie-continuity-changed";
        return {};
    }
    if(reason)*reason="waiting for successful FG evaluation and queue submission";
    ObservationStage(reason,"execution-continuity","return",list);
    return observation->status;
}

Ticket Record(ID3D12GraphicsCommandList* list)
{
    const auto interfaces=Neurotic::D3D12::NativeRecordingInterfaces::Resolve(list);
    if(FAILED(interfaces.result)||!interfaces.terminal)return Unavailable("recording observation interface unavailable");
    list=interfaces.terminal.Get();
    if(!SupportedExternalType(list->GetType()))return {};
    if(!PrepareRecordingHooks(interfaces.caller.Get()))return Unavailable("completion hooks or wrapper recording cookie continuity unavailable");
    Ticket ticket;std::shared_ptr<RecordingState> recordingState;UINT64 revision=0;
    {
        auto& s=State();std::lock_guard lock(s.mutex);
        if(s.failed)return Unavailable("submission/fence failure; process restart required");
        recordingState=Get<RecordingState>(list,recordingStateGuid);
        if(!recordingState)return Unavailable("command-list recording state unavailable");
        revision=recordingState->revision;
        ticket=Get<Recording>(list,recordingGuid);
        if(!ticket) {
            std::erase_if(s.pending,[](const Ticket& value){return value->sealed&&Completed(value);});
            if(s.failed)return Unavailable("device loss; process restart required");
            if(s.pending.size()>=256)return Unavailable("recording capacity exhausted");
            ticket=std::make_shared<Recording>();
            if(s.nextRecording==UINT64_MAX){s.failed=true;return {};}
            ticket->incarnation=++s.nextRecording;
            if(!Put(list,recordingGuid,ticket))return Unavailable("command-list lifetime cookie failed");
            s.pending.push_back(ticket);
        }
    }
    const auto joined=JoinCookie(interfaces,recordingGuid);
    std::lock_guard lock(State().mutex);
    if(State().failed||ticket->failed||ticket->sealed||
       !CookieStillCurrent(list,recordingGuid,joined)||Get<Recording>(list,recordingGuid)!=ticket||
       Get<RecordingState>(list,recordingStateGuid)!=recordingState||recordingState->revision!=revision)
        return Unavailable("wrapper recording cookie continuity changed during preparation");
    NR_FRAME_TRACE("ticket-record", "list={:p} ticket={:p} incarnation={}",
        static_cast<void*>(list),static_cast<void*>(ticket.get()),ticket->incarnation);
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
    ticket->terminal=RecordingTerminalState::OwnerSeal;
    return ticket->sealed;
}
bool CancelOwnedUnsubmittedRecording(ID3D12GraphicsCommandList* list,const Ticket& expected,
                                    ID3D12CommandAllocator* allocator)
{
    if(!list||!expected||!allocator)return false;
    list=NativeObject(list);
    auto& s=State();
    // Serialize identity/proof through Close and Reset against observed Execute.
    // This owner never cancels submitted, foreign or failed recordings.
    std::lock_guard lock(s.mutex);
    const auto state=Get<RecordingState>(list,recordingStateGuid);
    if(s.failed||expected->failed||expected->sealed||expected->submissions||
       !expected->points.empty()||Get<Recording>(list,recordingGuid)!=expected||
       !state||state->active)return false;
    ComPtr<ID3D12Device> device;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||!device||
       FAILED(device->GetDeviceRemovedReason()))return false;
    if(state->open&&FAILED(list->Close()))return false;
    if(FAILED(list->Reset(allocator,nullptr)))return false;
    return expected->sealed&&expected->terminal==RecordingTerminalState::Reset&&
        !expected->failed&&!expected->submissions&&Reusable(expected);
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
uint64_t TicketIdentity(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return ticket ? ticket->incarnation : 0;
}
bool Reusable(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return !ticket || (ticket->sealed && Completed(ticket));
}
std::optional<TerminalRecordingEvidence> InspectTerminalRecording(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    if(!ticket||State().failed||ticket->failed||!ticket->sealed||
       ticket->terminal==RecordingTerminalState::None||!Completed(ticket)||State().failed)return {};
    TerminalRecordingEvidence result{ticket->incarnation,ticket->submissions,ticket->terminal,{}};
    for(const auto& point:ticket->points)
    {
        const auto observed=point.timeline->fence->GetCompletedValue();
        if(observed==UINT64_MAX||observed<point.value)return {};
        result.completion.push_back({point.timeline->identity,point.value,observed});
    }
    if(result.submissions&&!result.completion.size())return {};
    return result;
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
static bool BindExternalWaitForRecording(ID3D12GraphicsCommandList* list, ID3D12Fence* producerFence,
                      UINT64 producerValue, UINT64 token, UINT64 sequence,
                      std::shared_ptr<ExternalWaitStatus> status,const Ticket& expectedRecording)
{
    ComPtr<ID3D12Device> listDevice, fenceDevice;
    HRESULT listDeviceResult = E_PENDING, fenceDeviceResult = E_PENDING;
    NativeIdentity::DeviceComparison devices;
    const auto refuse = [&](const char* reason) {
        if (status) status->Fail();
        NR_FRAME_TRACE("nr-fg-wait-bind-refused", "reason={} list={:p} type={} token={} sequence={}",
            reason, static_cast<void*>(list), list ? static_cast<unsigned int>(list->GetType()) : UINT_MAX, token, sequence);
#ifndef NR_GPU_SAFETY_TEST
        static std::atomic<uint64_t> refusals {0};
        const auto count = ++refusals;
        if (count <= 8 || count % 120 == 0)
            LOG_WARN("NR Present wait binding refused: reason={} list={:p} type={} token={} sequence={} count={} "
                "listGetDevice=0x{:X} fenceGetDevice=0x{:X} listResolve=0x{:X} fenceResolve=0x{:X} "
                "listLayers={} fenceLayers={} nativeListDevice={:p} nativeFenceDevice={:p} "
                "listReShadeLayers={} fenceReShadeLayers={}",
                reason, static_cast<void*>(list), list ? static_cast<unsigned int>(list->GetType()) : UINT_MAX,
                token, sequence, count, static_cast<unsigned int>(listDeviceResult),
                static_cast<unsigned int>(fenceDeviceResult), static_cast<unsigned int>(devices.left.result),
                static_cast<unsigned int>(devices.right.result), devices.left.layers, devices.right.layers,
                static_cast<void*>(devices.left.object.Get()), static_cast<void*>(devices.right.object.Get()),
                devices.left.reshadeLayers, devices.right.reshadeLayers);
#else
        (void)reason;
#endif
        return false;
    };
    if (!list || !producerFence || !producerValue || producerValue == UINT64_MAX)
        return refuse("invalid-dependency");
    const auto recordingInterfaces=Neurotic::D3D12::NativeRecordingInterfaces::Resolve(list);
    const auto nativeFence = NativeIdentity::Resolve<ID3D12Fence>(producerFence);
    if (FAILED(recordingInterfaces.result) || !recordingInterfaces.terminal) return refuse("list-identity-unresolved");
    if (FAILED(nativeFence.result) || !nativeFence.object) return refuse("fence-identity-unresolved");
    list = recordingInterfaces.terminal.Get();
    if (!SupportedExternalType(list->GetType())) return refuse("unsupported-list-type");
    producerFence = nativeFence.object.Get();
    if (!PrepareRecordingHooks(recordingInterfaces.caller.Get()) || !EnsureHooks(list, true))
        return refuse("completion-hooks-or-recording-cookie-continuity-unavailable");
    // Last alien-cookie Release must occur after this lock is destroyed.
    CookieJoin bindingJoin;
    std::unique_lock lock(State().mutex);
    if (State().failed) return refuse("gpu-safety-registry-failed");
    if(expectedRecording&&(expectedRecording->failed||expectedRecording->sealed||expectedRecording->submissions||
       Get<Recording>(list,recordingGuid)!=expectedRecording))return refuse("consumer-recording-changed");
    const auto recordingState=Get<RecordingState>(list,recordingStateGuid);
    if(!recordingState)return refuse("recording-state-unavailable");
    const auto revision=recordingState->revision;
    const auto verifyBinding=[&](const std::shared_ptr<ExternalWait>& expected) {
        lock.unlock();
        bindingJoin=JoinCookie(recordingInterfaces,externalWaitGuid);
        lock.lock();
        const bool current=!State().failed&&CookieStillCurrent(list,externalWaitGuid,bindingJoin)&&
            (!expectedRecording||(!expectedRecording->failed&&!expectedRecording->sealed&&!expectedRecording->submissions&&
               Get<Recording>(list,recordingGuid)==expectedRecording))&&
            Get<ExternalWait>(list,externalWaitGuid)==expected&&
            Get<RecordingState>(list,recordingStateGuid)==recordingState&&recordingState->revision==revision;
        if(!current)expected->Fail();
        return current;
    };
    auto previous = Get<ExternalWait>(list, externalWaitGuid);
    if (previous)
    {
        if (previous->token == token && previous->sequence == sequence)
        {
            // Several indexed MFG evaluations may record on one unchanged list.
            // Reuse only this exact dependency and aggregate, never a generic
            // duplicate or a different producer disguised by the same token.
            if (!status || !status->group || !previous->status ||
                previous->status->group != status->group || previous->status == status ||
                previous->fence.Get() != producerFence || previous->value != producerValue ||
                status->group->failed.load() || status->bound.load() ||
                previous->status->applied.load() ||
                previous->siblings.size() + 1 >= status->group->expectedWaits)
                return refuse("duplicate-dependency");
            previous->siblings.push_back(status);
            if(!verifyBinding(previous))return refuse("dependency-cookie-continuity-changed");
            status->MarkBound();
            return true;
        }
        const auto completed = previous->fence->GetCompletedValue();
        if (completed == UINT64_MAX) return refuse("previous-producer-device-lost");
        if (completed < previous->value) return refuse("previous-producer-incomplete");
        // A completed producer needs no future queue wait, including list replay.
        // Replace only after this proof; do not erase an unfinished dependency.
        // An unobserved old wait still fails its readiness proof on destruction.
    }
    listDeviceResult = list->GetDevice(IID_PPV_ARGS(&listDevice));
    fenceDeviceResult = producerFence->GetDevice(IID_PPV_ARGS(&fenceDevice));
    if (FAILED(listDeviceResult) || FAILED(fenceDeviceResult)) return refuse("device-query-failed");
    if (!SameDevice(listDevice.Get(), fenceDevice.Get(), &devices))
        return refuse(FAILED(devices.left.result) || FAILED(devices.right.result) ||
            !devices.left.object || !devices.right.object ? "device-identity-unresolved" : "foreign-device");
#ifndef NR_GPU_SAFETY_TEST
    if (devices.left.reshadeLayers || devices.right.reshadeLayers)
    {
        static std::atomic<unsigned int> reconciled {0};
        if (++reconciled <= 4)
            LOG_INFO("NR Present FG device reconciled: ReShade listLayers={} fenceLayers={} nativeDevice={:p} "
                     "token={} sequence={}; queue wait still required",
                devices.left.reshadeLayers, devices.right.reshadeLayers,
                static_cast<void*>(devices.left.object.Get()), token, sequence);
    }
#endif
    auto dependency = std::make_shared<ExternalWait>();
    dependency->fence = producerFence;
    dependency->value = producerValue;
    dependency->token = token;
    dependency->sequence = sequence;
    dependency->status = status;
    const bool written=Put(list,externalWaitGuid,dependency);
    const bool bound=written&&verifyBinding(dependency);
    if (dependency->status)
    {
        if (bound) dependency->status->MarkBound();
        else dependency->status->Fail();
    }
    NR_FRAME_TRACE("nr-fg-wait-bound", "list={:p} fence={:p} value={} token={} sequence={} ok={}",
        static_cast<void*>(list), static_cast<void*>(producerFence), producerValue, token, sequence, bound);
    if (!bound) return refuse("private-dependency-write-failed");
    return true;
}
bool BindExternalWait(ID3D12GraphicsCommandList* list,ID3D12Fence* producerFence,
                      UINT64 producerValue,UINT64 token,UINT64 sequence,std::shared_ptr<ExternalWaitStatus> status)
{
    return BindExternalWaitForRecording(list,producerFence,producerValue,token,sequence,std::move(status),{});
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
PendingSnapshot InspectPending(unsigned int maxRecordings, unsigned int maxPoints)
{
    std::lock_guard lock(State().mutex);
    PendingSnapshot result;
    result.slots.registryFailed = State().failed;
    // Hard bounds keep shutdown logging finite even if a caller asks for more.
    maxRecordings = (std::min)(maxRecordings, 64u);
    maxPoints = (std::min)(maxPoints, 16u);
    for (const auto& ticket : State().pending)
    {
        const auto slot = InspectSlots(&ticket, 1);
        result.slots.reusable += slot.reusable;
        result.slots.gpuPending += slot.gpuPending;
        result.slots.completedUnsealed += slot.completedUnsealed;
        result.slots.unsubmittedUnsealed += slot.unsubmittedUnsealed;
        result.slots.failed += slot.failed;
        if (slot.reusable) continue;
        if (result.recordings.size() >= maxRecordings) { ++result.omitted; continue; }
        RetainedRecordingSnapshot record;
        record.incarnation = ticket->incarnation;
        record.submissions = ticket->submissions;
        record.state = ticket->terminal;
        record.sealed = ticket->sealed;
        record.failed = slot.failed != 0;
        for (const auto& point : ticket->points)
        {
            if (record.completion.size() >= maxPoints) { ++record.omittedPoints; continue; }
            record.completion.push_back({point.timeline->identity, point.value,
                point.timeline->fence->GetCompletedValue()});
        }
        result.recordings.push_back(std::move(record));
    }
    return result;
}
bool Readable(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return ticket && !ticket->points.empty() && ticket->sealed && Completed(ticket);
}
RecordingSnapshot InspectRecording(const Ticket& ticket, ID3D12CommandQueue* consumer,
                                   bool establishCrossQueue)
{
    std::lock_guard lock(State().mutex);
    RecordingSnapshot result;
    result.registryHealthy = !State().failed;
    if (!ticket || ticket->failed || !result.registryHealthy) return result;
    result.valid = true;
    result.submitted = ticket->submissions != 0;
    result.uniqueSubmission = ticket->submissions == 1 && ticket->points.size() == 1;
    result.nonReplayable = ticket->sealed;
    const bool retiredPoints = Completed(ticket);
    result.registryHealthy = !State().failed;
    if (!result.registryHealthy) { result.valid = false; return result; }
    result.completed = result.submitted && retiredPoints;
    result.reusable = result.nonReplayable && retiredPoints; // includes owner-cancelled unsubmitted lists
    if (!consumer) return result;
    consumer = NativeObject(consumer);
    result.queueKnown = true;
    result.supportedType = SupportedExternalType(consumer->GetDesc().Type);
    if (!result.uniqueSubmission || !result.supportedType) return result;
    const auto& point = ticket->points.front();
    result.sameQueue = Get<Timeline>(consumer, timelineGuid) == point.timeline;
    ComPtr<ID3D12Device> producerDevice, consumerDevice;
    result.sameDevice = SUCCEEDED(point.timeline->fence->GetDevice(IID_PPV_ARGS(&producerDevice))) &&
        SUCCEEDED(consumer->GetDevice(IID_PPV_ARGS(&consumerDevice))) &&
        NativeObject(producerDevice.Get()) == NativeObject(consumerDevice.Get());
    if (result.sameDevice)
        result.orderedForConsumer = establishCrossQueue ? OrderBefore(ticket, consumer) : OrderedOn(ticket, consumer);
    return result;
}
bool MatchesRecording(const Ticket& ticket, ID3D12GraphicsCommandList* list, ID3D12CommandQueue* queue)
{
    if(!ticket||!list||!queue)return false;
    std::lock_guard lock(State().mutex);
    list=NativeObject(list);queue=NativeObject(queue);
    if(State().failed||ticket->failed||ticket->sealed||Get<Recording>(list,recordingGuid)!=ticket ||
       !SupportedExternalType(list->GetType())||list->GetType()!=queue->GetDesc().Type)return false;
    ComPtr<ID3D12Device> listDevice,queueDevice;
    return SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&listDevice)))&&
        SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice)))&&
        NativeObject(listDevice.Get())==NativeObject(queueDevice.Get());
}
bool MatchesLocalRecording(const Ticket& ticket, ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    if(!ticket||!list||!resource)return false;
    std::lock_guard lock(State().mutex);
    list=NativeObject(list);resource=NativeObject(resource);
    if(State().failed||ticket->failed||ticket->sealed||ticket->submissions!=0||
       Get<Recording>(list,recordingGuid)!=ticket||!SupportedExternalType(list->GetType()))return false;
    ComPtr<ID3D12Device> listDevice,resourceDevice;
    return SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&listDevice)))&&
        SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&resourceDevice)))&&
        NativeObject(listDevice.Get())==NativeObject(resourceDevice.Get());
}
struct LocalRecordingAction::Impl
{
    struct Lease
    {
        ComPtr<ID3D12GraphicsCommandList> list;
        Ticket ticket;
        std::shared_ptr<RecordingState> state;
        UINT64 revision=0;
        bool published=false;
        // Call only under the registry lock, after all foreign calls/releases.
        bool Valid(bool active)const noexcept
        {
            return state&&ticket&&state->open&&state->revision==revision&&
                !State().failed&&!ticket->failed&&!ticket->sealed&&ticket->submissions==0&&
                (active ? state->active&&state->owner==std::this_thread::get_id() : !state->active);
        }
        bool Current()const
        {
            std::lock_guard lock(State().mutex);
            if(!published||!Valid(true))return false;
            try {
                const auto observedState=Get<RecordingState>(list.Get(),recordingStateGuid);
                const auto observedTicket=Get<Recording>(list.Get(),recordingGuid);
                // A cookie lookup or its Release can synchronously Close/Reset.
                // No foreign validation follows this final memory-only check.
                return observedState==state&&observedTicket==ticket&&Valid(true);
            }catch(...){return false;}
        }
        ~Lease()
        {
            if(!published)return;
            std::lock_guard lock(State().mutex);
            state->active=false;state->owner={};
            State().actionFinished.notify_all();
        }
    };
    std::vector<ComPtr<ID3D12Resource>> resources;
    // Destroy the lease reference before this handle's resource references.
    // A surviving parent/child retains exclusivity; the final handle ends it.
    std::shared_ptr<Lease> lease;
};
LocalRecordingAction::LocalRecordingAction(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
LocalRecordingAction::~LocalRecordingAction()=default;
bool LocalRecordingAction::Current()const
{
    return impl_->lease->Current();
}
bool LocalRecordingAction::CurrentFor(ID3D12Resource* resource)const
{
    if(!resource)return false;
    const auto native=NativeObject(resource);
    return std::any_of(impl_->resources.begin(),impl_->resources.end(),
        [native](const auto& held){return held.Get()==native;})&&Current();
}
const Ticket& LocalRecordingAction::RecordingTicket()const noexcept{return impl_->lease->ticket;}
ID3D12GraphicsCommandList* LocalRecordingAction::CommandList()const noexcept{return impl_->lease->list.Get();}
std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket& ticket,
    ID3D12GraphicsCommandList* list,ID3D12Resource* resource)
{
    return BeginLocalAction(ticket,list,std::span<ID3D12Resource* const>(&resource,1));
}
std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket& ticket,
    ID3D12GraphicsCommandList* list,std::span<ID3D12Resource* const> resources)
{
    if(!list||resources.empty()||!ticket)return {};
    list=NativeObject(list);
    std::lock_guard lock(State().mutex);
    const auto state=Get<RecordingState>(list,recordingStateGuid);
    if(!state||!state->open||state->active)return {};
    auto impl=std::make_unique<LocalRecordingAction::Impl>();
    impl->lease=std::make_shared<LocalRecordingAction::Impl::Lease>();
    auto& lease=*impl->lease;
    lease.state=state;lease.ticket=ticket;lease.revision=state->revision;
    lease.list=list;
    for(auto* resource:resources)
    {
        if(!resource)return {};
        resource=NativeObject(resource);
        if(!resource||!MatchesLocalRecording(ticket,list,resource))return {};
        impl->resources.emplace_back(resource);
    }
    auto action=std::unique_ptr<LocalRecordingAction>(new LocalRecordingAction(std::move(impl)));
    const auto observedState=Get<RecordingState>(list,recordingStateGuid);
    const auto observedTicket=Get<Recording>(list,recordingGuid);
    // All resource/device queries, AddRefs and temporary releases precede this
    // publication. Reentry must not publish into a changed or closed recording.
    if(observedState!=state||observedTicket!=ticket||!lease.Valid(false))return {};
    state->active=true;state->owner=std::this_thread::get_id();
    lease.published=true;
    return action;
}
std::unique_ptr<LocalRecordingAction> BorrowLocalAction(const LocalRecordingAction& owner,
    std::span<ID3D12Resource* const> resources)
{
    if(resources.empty())return {};
    std::lock_guard lock(State().mutex);
    // Retain the capability before foreign preparation; a caller retiring its
    // parent handle during a callback cannot destroy this lease underneath us.
    auto lease=owner.impl_->lease;
    if(!lease->Current())return {};
    auto impl=std::make_unique<LocalRecordingAction::Impl>();
    impl->lease=lease;
    for(auto* resource:resources)
    {
        if(!resource)return {};
        resource=NativeObject(resource);
        if(!resource||!MatchesLocalRecording(lease->ticket,lease->list.Get(),resource))return {};
        impl->resources.emplace_back(resource);
    }
    auto action=std::unique_ptr<LocalRecordingAction>(new LocalRecordingAction(std::move(impl)));
    if(!lease->Current())return {};
    return action;
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
bool CanYieldOutput()
{
    std::unique_lock lock(State().mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    return !State().failed && std::all_of(State().pending.begin(), State().pending.end(),
        [](const Ticket& ticket) { return Reusable(ticket); });
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

namespace {
struct DerivedBudget {
    std::atomic<uint64_t> bytes{0};
    std::atomic<unsigned> anchors{0},scratch{0};
};
DerivedBudget& ArtifactBudget(){static auto* budget=new DerivedBudget;return *budget;}
}
struct DerivedAllocation {
    uint64_t bytes=0;
    DerivedArtifactKind kind=DerivedArtifactKind::Anchor;
    bool bound=false;
    ~DerivedAllocation(){auto& b=ArtifactBudget();b.bytes-=bytes;(kind==DerivedArtifactKind::Anchor?b.anchors:b.scratch)--;}
};
DerivedAllocationRef ReserveDerivedAllocation(DerivedArtifactKind kind,uint64_t bytes) {
    std::lock_guard lock(State().mutex);constexpr uint64_t cap=640ull*1024*1024;
    auto& b=ArtifactBudget();auto& slots=kind==DerivedArtifactKind::Anchor?b.anchors:b.scratch;
    if(!bytes||bytes>cap||slots>=2||b.bytes>cap-bytes)return {};
    try{auto r=std::make_shared<DerivedAllocation>();r->bytes=bytes;r->kind=kind;b.bytes+=bytes;++slots;return r;}
    catch(...){return {};}
}
namespace {
struct DerivedBacking {
    DerivedAllocationRef allocation;
    std::vector<ComPtr<ID3D12Resource>> resources;
    std::shared_ptr<void> pipeline;
    uint64_t bytes=0;
    DerivedArtifactKind kind=DerivedArtifactKind::Anchor;
};
}
struct DerivedArtifact {
    std::shared_ptr<DerivedBacking> backing;
    Ticket producer;
    CompletionSet readers;
    bool readerPreparing=false;
};
bool RetainDerivedUse(const LocalRecordingAction& action,std::shared_ptr<void> use)
{
    std::lock_guard lock(State().mutex);
    if(!use||!action.Current()||action.RecordingTicket()->derivedUses.size()>=64)return false;
    try{action.RecordingTicket()->derivedUses.push_back(std::move(use));return true;}
    catch(...){return false;}
}
DerivedArtifactRef CreateDerivedArtifact(const LocalRecordingAction& action,
    std::span<ID3D12Resource* const> resources,DerivedArtifactKind kind,
    std::shared_ptr<void> pipeline,uint64_t auxiliaryBytes,DerivedAllocationRef allocation)
{
    std::lock_guard lock(State().mutex);
    constexpr uint64_t cap=640ull*1024*1024;
    if(!action.Current()||resources.empty()||auxiliaryBytes>cap)return {};
    try {
        auto backing=std::make_shared<DerivedBacking>();backing->bytes=auxiliaryBytes;
        backing->kind=kind;backing->pipeline=std::move(pipeline);
        for(auto* resource:resources) {
            if(!action.CurrentFor(resource))return {};
            ComPtr<ID3D12Device> device;
            if(FAILED(resource->GetDevice(IID_PPV_ARGS(&device))))return {};
            const auto desc=resource->GetDesc();const auto resourceAllocation=device->GetResourceAllocationInfo(0,1,&desc);
            if(resourceAllocation.SizeInBytes==UINT64_MAX||resourceAllocation.SizeInBytes>cap-backing->bytes)return {};
            backing->bytes+=resourceAllocation.SizeInBytes;backing->resources.emplace_back(resource);
        }
        if(!allocation)allocation=ReserveDerivedAllocation(kind,backing->bytes);
        if(!allocation||allocation->bound||allocation->kind!=kind||allocation->bytes<backing->bytes)return {};
        backing->allocation=std::move(allocation);
        auto artifact=std::make_shared<DerivedArtifact>();artifact->backing=backing;artifact->producer=action.RecordingTicket();
        if(!RetainDerivedUse(action,backing))return {};
        backing->allocation->bound=true;
        return artifact;
    }catch(...){return {};}
}
bool DerivedArtifactReusable(const DerivedArtifactRef& artifact)
{
    std::lock_guard lock(State().mutex);
    return artifact&&!artifact->readerPreparing&&Reusable(artifact->producer)&&Reusable(artifact->readers);
}
bool RearmDerivedArtifact(const DerivedArtifactRef& artifact,const LocalRecordingAction& producer)
{
    std::lock_guard lock(State().mutex);
    if(!DerivedArtifactReusable(artifact)||!producer.Current())return false;
    for(const auto& resource:artifact->backing->resources)if(!producer.CurrentFor(resource.Get()))return false;
    if(!RetainDerivedUse(producer,artifact->backing))return false;
    artifact->producer=producer.RecordingTicket();artifact->readers.clear();return true;
}
bool PrepareDerivedSnapshot(const LocalRecordingAction& source,std::span<const DerivedSnapshotCopy> copies,
    DerivedArtifactRef& snapshot,std::shared_ptr<void> retainedPipeline,DerivedAllocationRef allocation)
{
    if(!source.Current()||copies.empty()||copies.size()>2)return false;
    try {
        std::vector<ID3D12Resource*> resources,destinations;
        for(const auto& pair:copies) {
            if(!pair.source||!pair.destination||pair.source==pair.destination||
               !source.CurrentFor(pair.source)||!source.CurrentFor(pair.destination))return false;
            if(std::find(resources.begin(),resources.end(),pair.source)!=resources.end()||
               std::find(resources.begin(),resources.end(),pair.destination)!=resources.end())return false;
            const auto a=pair.source->GetDesc(),b=pair.destination->GetDesc();
            if(a.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||a.Dimension!=b.Dimension||
               a.Width!=b.Width||a.Height!=b.Height||a.Format!=b.Format||a.DepthOrArraySize!=1||
               b.DepthOrArraySize!=1||a.MipLevels!=1||b.MipLevels!=1||a.SampleDesc.Count!=1||b.SampleDesc.Count!=1)return false;
            resources.push_back(pair.source);resources.push_back(pair.destination);destinations.push_back(pair.destination);
        }
        ComPtr<ID3D12Device> device;
        if(FAILED(source.CommandList()->GetDevice(IID_PPV_ARGS(&device))))return false;
        const auto type=source.CommandList()->GetType();if(!SupportedExternalType(type))return false;
        auto job=std::make_shared<DerivedSnapshotJob>();
        if(FAILED(device->CreateCommandAllocator(type,IID_PPV_ARGS(&job->allocator)))||
           FAILED(device->CreateCommandList(0,type,job->allocator.Get(),nullptr,IID_PPV_ARGS(&job->list)))||
           !PrepareRecordingHooks(job->list.Get())||FAILED(job->list->Close())||
           FAILED(job->list->Reset(job->allocator.Get(),nullptr)))return false;
        job->producer=Record(job->list.Get());
        auto action=BeginLocalAction(job->producer,job->list.Get(),resources);if(!action)return false;
        auto retained=std::make_shared<std::vector<ComPtr<ID3D12Resource>>>();
        for(auto* resource:resources)retained->emplace_back(resource);
        if(!RetainDerivedUse(source,retained)||!RetainDerivedUse(*action,retained))return false;
        auto result=snapshot;
        if(result) {if(!RearmDerivedArtifact(result,*action))return false;}
        else result=CreateDerivedArtifact(*action,destinations,DerivedArtifactKind::Anchor,
            std::move(retainedPipeline),65536,std::move(allocation));
        if(!result)return false;
        const auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
            job->list->ResourceBarrier(1,&barrier);
        };
        for(const auto& pair:copies) {
            transition(pair.source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(pair.destination,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            job->list->CopyResource(pair.destination,pair.source);
            transition(pair.destination,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            transition(pair.source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        action.reset();if(FAILED(job->list->Close()))return false;
        std::lock_guard lock(State().mutex);
        if(!source.Current()||source.RecordingTicket()->snapshotExports.size()>=2)return false;
        source.RecordingTicket()->snapshotExports.push_back(job);
        snapshot=std::move(result);return true;
    }catch(...){return false;}
}
std::unique_ptr<DerivedReadLease> ReserveDerivedRead(const DerivedArtifactRef& artifact,
    const LocalRecordingAction& consumer,ID3D12CommandQueue* queue)
{
    // Hold the artifact across foreign callbacks without releasing its backing
    // under the registry guard. Preparing readers block rearm/another reader.
    const auto heldArtifact=artifact;
    std::unique_lock lock(State().mutex);
    const auto refuse=[&](const char* why,const RecordingSnapshot& proof=RecordingSnapshot{}) -> std::unique_ptr<DerivedReadLease> {
#ifndef NR_GPU_SAFETY_TEST
        static uint64_t refusals=0;
        const auto count=++refusals;
        if(count<=8||count%120==0)try {
            LOG_INFO("NR alternate reuse refused: reason={} count={} queueKnown={} valid={} submitted={} unique={} nonReplayable={} completed={} sameDevice={} sameQueue={} ordered={} registryHealthy={}",
                why,count,queue!=nullptr,proof.valid,proof.submitted,proof.uniqueSubmission,proof.nonReplayable,
                proof.completed,proof.sameDevice,proof.sameQueue,proof.orderedForConsumer,proof.registryHealthy);
        }catch(...) { /* Diagnostics never change the reservation result. */ }
#else
        (void)why;(void)proof;
#endif
        return {};
    };
    if(!heldArtifact||!consumer.Current())return refuse("reader-unavailable");
    if(heldArtifact->producer==consumer.RecordingTicket())return refuse("same-recording");
    for(const auto& resource:heldArtifact->backing->resources)if(!consumer.CurrentFor(resource.Get()))return refuse("resource-access");
    const auto proof=InspectRecording(heldArtifact->producer,queue);
    if(!proof.valid)return refuse("producer-unavailable",proof);
    if(!proof.submitted)return refuse("producer-unsubmitted",proof);
    if(!proof.uniqueSubmission)return refuse("producer-replayed",proof);
    if(!proof.nonReplayable)return refuse("producer-still-replayable",proof);
    // Native NGX has no queue at recording time. CurrentFor proves the exact
    // resources/device, and BindExternalWait below enforces the submitted
    // producer's fence on whichever queue actually executes this reader.
    // A supplied queue retains its existing same-queue admission requirement.
    if(queue&&(!proof.sameDevice||!proof.sameQueue||!proof.orderedForConsumer))return refuse("queue-mismatch",proof);
    std::erase_if(heldArtifact->readers,[](const Ticket& ticket){return Reusable(ticket);});
    // Private images undergo state transitions on the reader list. Do not let
    // two independently submitted readers race those transitions.
    if(heldArtifact->readerPreparing||!heldArtifact->readers.empty())return refuse("previous-reader-pending",proof);
    struct PreparationRetirement {
        const DerivedArtifactRef& artifact;
        ~PreparationRetirement(){std::lock_guard guard(State().mutex);artifact->readerPreparing=false;}
    } preparation{heldArtifact};
    heldArtifact->readerPreparing=true;
    try {
        auto result=std::unique_ptr<DerivedReadLease>(new DerivedReadLease(heldArtifact->backing,
            proof.completed?DerivedReadiness::CompletedImmutable:DerivedReadiness::DependencyOrderedImmutable));
        heldArtifact->readers.reserve(heldArtifact->readers.size()+1);
        if(!RetainDerivedUse(consumer,heldArtifact->backing))return refuse("reader-retention",proof);
        // Bind the already-submitted producer to the actual consumer execution.
        // A missing recording-time queue or a later host queue change cannot
        // race the producer: the existing execute hook enforces this dependency
        // on the same-device queue that really executes the reader. These
        // are recording incarnations, never invented frame/provider tokens.
        const auto producer=heldArtifact->producer,reader=consumer.RecordingTicket();
        const auto timeline=producer->points.front().timeline;
        const auto value=producer->points.front().value;
        // There must be NO enclosing recursive registry guard during wrapper
        // resolution/cookie joining. The preparation reservation remains owned
        // while unlocked; no second reader or producer rearm can commit.
        lock.unlock();
        const bool bound=proof.completed||BindExternalWaitForRecording(consumer.CommandList(),timeline->fence.Get(),value,
            producer->incarnation,reader->incarnation,{},reader);
        lock.lock();
        if(!bound)return refuse("dependency-binding",proof);
        if(!consumer.Current()||heldArtifact->producer!=producer||!heldArtifact->readerPreparing||
           !heldArtifact->readers.empty())return refuse("reader-changed-during-preparation",proof);
        heldArtifact->readers.push_back(reader);return result;
    }catch(...){if(!lock.owns_lock())lock.lock();return refuse("reader-allocation",proof);}
}
uint64_t DerivedAllocatedBytes(){return ArtifactBudget().bytes.load();}
}
