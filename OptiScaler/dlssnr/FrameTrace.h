#pragma once

#include "FrameTraceContract.h"
#include <windows.h>
#include <spdlog/spdlog.h>

namespace DlssNr::FrameTrace
{
struct Session
{
    char id[33] {};
    LONGLONG frequency = 0;
    bool armed = false;
    Budget budget;
    Session() noexcept
    {
        const DWORD length = GetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", id, sizeof(id));
        LARGE_INTEGER f {};
        armed = length == 32 && ValidSession(std::string_view(id, 32)) &&
                QueryPerformanceFrequency(&f) && f.QuadPart > 0;
        frequency = f.QuadPart;
    }
};
inline Session& Current() { static Session session; return session; }
inline bool Armed() noexcept { return Current().armed; }

// Scalars/addresses only; this observer never retains or queries game COM resources, creates
// GPU work, waits, changes configuration, or decides whether an evaluation is allowed.
// Existing logger routing/level still applies. An absent/filtered record is NOT evidence.
template<typename... Args>
uint64_t Event(const char* kind, fmt::format_string<Args...> format, Args&&... args) noexcept
{
    auto& session = Current();
    if (!session.armed) return 0;
    const auto sequence = session.budget.Take();
    if (!sequence) return 0;
    LARGE_INTEGER qpc {};
    if (!QueryPerformanceCounter(&qpc)) return 0;
    try
    {
        const auto details = sequence == Budget::Limit ? std::string("reason=event-budget-exhausted") :
            fmt::format(format, std::forward<Args>(args)...);
        spdlog::info("NR_FRAME_TRACE v=1 session={} pid={} tid={} qpc={} frequency={} seq={} native={} "
                     "present={} kind={} classification=unknown {}", session.id, GetCurrentProcessId(),
                     GetCurrentThreadId(), qpc.QuadPart, session.frequency, sequence,
                     nativeObservation, presentObservation,
                     sequence == Budget::Limit ? "trace-ended" : kind, details);
    }
    catch (...) { /* Diagnostics must not change the host's result or exception path. */ }
    return sequence == Budget::Limit ? 0 : sequence;
}
}

// Avoid even evaluating diagnostic arguments on the unarmed path.
#define NR_FRAME_TRACE(kind, ...) \
    do { if (::DlssNr::FrameTrace::Armed()) ::DlssNr::FrameTrace::Event(kind, __VA_ARGS__); } while (false)
