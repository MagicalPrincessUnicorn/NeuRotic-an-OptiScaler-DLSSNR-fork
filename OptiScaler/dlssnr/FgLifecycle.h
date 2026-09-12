#pragma once
#include "FgLifecycleContract.h"
#include "FrameTrace.h"
#include <mutex>

namespace DlssNr::FgLifecycle
{
inline bool OptIn(const char* name) noexcept
{
    char value[2] {};
    return GetEnvironmentVariableA(name, value, 2) == 1 && value[0] == '1';
}
inline bool Enabled() noexcept { static const bool enabled = OptIn("NEUROTIC_FG_LIFECYCLE"); return enabled; }
struct Journal
{
    std::mutex mutex;
    Instances instances;
    Budget budget;
    HANDLE file = INVALID_HANDLE_VALUE;
    uint64_t operations = 0, presents = 0, states = 0, options = 0, firstOutput = UINT64_MAX;
    uint64_t lastOptionsGeneration = UINT64_MAX;
    std::string lastOptions;
    explicit Journal(const wchar_t* filename = L"FG-LIFECYCLE.log")
    {
        if (!Enabled()) return;
        wchar_t directory[2048] {};
        const auto length = GetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY", directory, 2048);
        if (!length || length >= 2048 || !FrameTrace::Current().armed) return;
        const auto path = std::wstring(directory) + L"\\" + filename;
        file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    // File ownership ends with the process; no teardown order dependency on logger/COM.
    template<typename... Args>
    void Write(const char* kind, spdlog::format_string_t<Args...> format, Args&&... args)
    {
        if (file == INVALID_HANDLE_VALUE) return;
        const auto sequence = budget.Take();
        if (!sequence) return;
        LARGE_INTEGER qpc {}; QueryPerformanceCounter(&qpc);
        auto details = spdlog::fmt_lib::format(format, std::forward<Args>(args)...);
        if (details.size() > 2048) details = details.substr(0, 2000) + " truncated=true";
        if (sequence == Budget::limit) { kind = "journal-ended"; details = "reason=budget-exhausted"; }
        const auto snapshot = instances.Read();
        const auto line = spdlog::fmt_lib::format(
            "NR_FG_LIFECYCLE v=1 session={} pid={} tid={} qpc={} frequency={} seq={} generation={} "
            "instance={} active={} kind={} {}\n", FrameTrace::Current().id, GetCurrentProcessId(),
            GetCurrentThreadId(), qpc.QuadPart, FrameTrace::Current().frequency, sequence,
            snapshot.generation, snapshot.instance, snapshot.active, kind, details);
        DWORD written = 0;
        if (!WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size())
        { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
    }
};
inline Journal& Current() { static Journal journal; return journal; }
template<typename F> inline void Observe(F&& function) noexcept
{
    if (!Enabled()) return;
    try { auto& journal = Current(); std::lock_guard lock(journal.mutex); function(journal); }
    catch (...) { /* Diagnostic failures must not change a host API result. */ }
}
template<typename... Args>
void Event(const char* kind, spdlog::format_string_t<Args...> format, Args&&... args) noexcept
{ Observe([&](Journal& j) { j.Write(kind, format, std::forward<Args>(args)...); }); }
inline Snapshot Read() noexcept
{ Snapshot value; Observe([&](Journal& j) { value = j.instances.Read(); }); return value; }
inline uint64_t Find(uintptr_t handle) noexcept
{ uint64_t id = 0; Observe([&](Journal& j) { id = j.instances.Find(handle); }); return id; }
inline uint64_t Begin(const char* kind, uintptr_t handle = 0) noexcept
{
    uint64_t operation = 0;
    Observe([&](Journal& j) { operation = ++j.operations;
        j.Write(kind, "operation={} handle={} handleInstance={}", operation, handle, j.instances.Find(handle)); });
    NR_FRAME_TRACE("fg-operation-begin", "operation={} operationKind={} handle={}", operation, kind, handle);
    return operation;
}
inline void Created(uint64_t operation, uintptr_t handle, uint32_t result, bool success, void* list) noexcept
{
    Observe([&](Journal& j) {
        const auto previous = j.instances.Find(handle);
        const auto id = j.instances.Create(handle, success);
        j.Write("fg-create-end", "operation={} handle={} handleInstance={} previousInstance={} success={} "
            "result={} list={:p} association=unknown", operation, handle, id, previous, success, result, list);
    });
}
inline void Released(uint64_t operation, uintptr_t handle, uint64_t id, uint32_t result, bool success) noexcept
{
    Observe([&](Journal& j) { j.instances.Release(handle, id, success);
        j.Write("fg-release-end", "operation={} handle={} handleInstance={} result={} success={}",
            operation, handle, id, result, success); });
}
inline void Options(uint64_t operation, uint32_t viewport, int mode, unsigned count, unsigned parallel,
                    unsigned flags, unsigned ui, int result, uint64_t providerGeneration) noexcept
{
    NR_FRAME_TRACE("fg-options-end", "operation={} viewport={} mode={} count={} parallel={} flags={} ui={} "
        "result={} providerGeneration={} generation={}", operation, viewport, mode, count, parallel, flags,
        ui, result, providerGeneration, Read().generation);
    Observe([&](Journal& j) {
        const auto value = spdlog::fmt_lib::format("viewport={} mode={} count={} parallel={} flags={} ui={} result={}",
            viewport, mode, count, parallel, flags, ui, result);
        const auto generation = j.instances.Read().generation;
        if (j.lastOptions != value || j.lastOptionsGeneration != generation || ++j.options % 120 == 0)
            j.Write("options-end", "operation={} providerGeneration={} {} sample=changes-and-every-120",
                operation, providerGeneration, value);
        j.lastOptions = value; j.lastOptionsGeneration = generation;
    });
}
inline void Output(const Snapshot& claim, uint64_t provider, uint64_t token, void* chain, void* queue) noexcept
{
    Observe([&](Journal& j) {
        if (j.firstOutput != claim.generation)
            j.Write("first-nr-output", "claimGeneration={} claimInstance={} providerGeneration={} token={} "
                "swapchain={:p} queue={:p} stage=copyback-submitted-before-provider-present",
                claim.generation, claim.instance, provider, token, chain, queue);
        j.firstOutput = claim.generation;
    });
}
inline void Present(const Snapshot& claim, uint64_t provider, uint64_t token, void* chain, void* queue,
                    bool output, HRESULT result) noexcept
{
    Observe([&](Journal& j) {
        if (++j.presents <= 8 || j.presents % 120 == 0 || FAILED(result))
            j.Write("present-end",
                "claimGeneration={} claimInstance={} providerGeneration={} token={} swapchain={:p} "
                "queue={:p} output={} result={} association=present-call-scope",
                claim.generation, claim.instance, provider, token, chain, queue, output, static_cast<uint32_t>(result));
    });
}
inline void Completion(uint32_t viewport, int result, unsigned version, void* fence, uint64_t value,
                       unsigned presented) noexcept
{
    NR_FRAME_TRACE("fg-state", "viewport={} result={} version={} fence={:p} value={} presented={} "
        "generation={} query=existing association=unknown", viewport, result, version, fence, value,
        presented, Read().generation);
    Observe([&](Journal& j) { if (++j.states <= 8 || j.states % 120 == 0 || result != 0)
        j.Write("fg-state", "viewport={} result={} version={} fence={:p} value={} presented={} "
            "query=existing association=unknown sample=first-8-and-every-120", viewport, result, version,
            fence, value, presented); });
}
}
#define NR_FG_EVENT(kind, ...) \
    do { if (::DlssNr::FgLifecycle::Enabled()) ::DlssNr::FgLifecycle::Event(kind, __VA_ARGS__); } while (false)
