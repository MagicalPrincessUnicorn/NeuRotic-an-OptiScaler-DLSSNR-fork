#ifndef NR_GPU_SAFETY_TEST
#include "pch.h"
#include <SysUtils.h>
#include <Logger.h>
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
    std::vector<Point> points;
};
namespace
{
constexpr GUID recordingGuid = {0x5ee0e247, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
constexpr GUID timelineGuid = {0x5ee0e248, 0xe5ab, 0x450c, {0x96,0x09,0x13,0xbe,0xe3,0xab,0x11,0x89}};
using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
ExecuteFn originalExecute = nullptr;
ResetFn originalReset = nullptr;
void* resetTarget = nullptr;
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

void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    auto& s = State();
    std::lock_guard lock(s.mutex);
    CompletionSet uses;
    for (UINT i = 0; i < count; ++i)
        if (auto ticket = Get<Recording>(lists[i], recordingGuid)) uses.push_back(ticket);
    if (uses.empty()) { originalExecute(queue, count, lists); return; }

    auto timeline = Get<Timeline>(queue, timelineGuid);
    if (!timeline)
    {
        timeline = std::make_shared<Timeline>();
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline->fence))) ||
            !Put(queue, timelineGuid, timeline)) timeline.reset();
        if (timeline && FAILED(queue->GetTimestampFrequency(&timeline->frequency))) timeline->frequency = 0;
    }
    // Observe completion without inserting dependencies into the host's queue graph. An implicit
    // cross-queue Wait could deadlock a valid host Wait/Signal pair. The host still owns GPU ordering
    // of shared rendering inputs/history; lifetime reclamation covers every observed submission.
    bool ok = timeline != nullptr && timeline->next < UINT64_MAX - 1;
    originalExecute(queue, count, lists);
    const UINT64 value = ok ? ++timeline->next : 0;
    if (ok) ok = SUCCEEDED(queue->Signal(timeline->fence.Get(), value));
    if (!ok) s.failed = true;
    for (auto& ticket : uses)
    {
        if (!ok) { ticket->failed = true; continue; }
        auto found = std::find_if(ticket->points.begin(), ticket->points.end(),
                                 [&](const Point& p) { return p.timeline == timeline; });
        if (found == ticket->points.end()) ticket->points.push_back({timeline, value});
        else found->value = value; // also covers replay before Reset
    }
}

HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                 ID3D12PipelineState* pipeline)
{
    std::lock_guard lock(State().mutex);
    const HRESULT hr = originalReset(list, allocator, pipeline);
    if (SUCCEEDED(hr))
    {
        if (auto t = Get<Recording>(list, recordingGuid))
        {
            if (SUCCEEDED(list->SetPrivateDataInterface(recordingGuid, nullptr))) t->sealed = true;
            else { t->failed = true; State().failed = true; }
        }
    }
    return hr;
}

bool EnsureHooks(ID3D12GraphicsCommandList* list)
{
    std::lock_guard lock(State().hookMutex);
    void* target = (*(void***)list)[10];
    if (originalReset) return target == resetTarget;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC desc {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) return false;
    ID3D12CommandQueue* nativeQueue = NativeObject(queue.Get());
    originalExecute = reinterpret_cast<ExecuteFn>((*(void***)nativeQueue)[10]);
    originalReset = reinterpret_cast<ResetFn>(target);
    resetTarget = target;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Execute), &self))
    { originalExecute = nullptr; originalReset = nullptr; return false; }
    if (DetourTransactionBegin() != NO_ERROR) { originalExecute = nullptr; originalReset = nullptr; return false; }
    LONG result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(&originalExecute), Execute);
    if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(&originalReset), Reset);
    if (result == NO_ERROR) result = DetourTransactionCommit();
    else DetourTransactionAbort();
    if (result != NO_ERROR) { originalExecute = nullptr; originalReset = nullptr; return false; }
    return true;
}
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
    if (auto t = Get<Recording>(list, recordingGuid)) return t;
    std::erase_if(s.pending, [](const Ticket& t) { return t->sealed && Completed(t); });
    if (s.failed) return Unavailable("device loss; process restart required");
    if (s.pending.size() >= 256) return Unavailable("recording capacity exhausted");
    auto ticket = std::make_shared<Recording>();
    if (!Put(list, recordingGuid, ticket)) return Unavailable("command-list lifetime cookie failed");
    s.pending.push_back(ticket);
    return ticket;
}
bool Reusable(const Ticket& ticket)
{
    std::lock_guard lock(State().mutex);
    return !ticket || (ticket->sealed && Completed(ticket));
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
