#pragma once

#include "FrameTraceContract.h"
#include "../nr/diagnostics/ContractObservation.h"
#include <windows.h>
#include <spdlog/spdlog.h>

namespace DlssNr::FrameTrace
{
struct Session
{
    char id[33] {};
    LONGLONG frequency = 0;
    bool armed = false;
    bool waitForEnable = false;
    bool association = false;
    std::atomic<bool> capturing {false};
    std::atomic<bool> waitingReported {false};
    Budget budget;
    Session() noexcept
    {
        const DWORD length = GetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", id, sizeof(id));
        char trigger[16] {};
        const DWORD triggerLength = GetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_TRIGGER", trigger, sizeof(trigger));
        char profile[32] {};
        const DWORD profileLength = GetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_PROFILE", profile, sizeof(profile));
        char contractObservation[16] {};
        const DWORD contractObservationLength = GetEnvironmentVariableA(
            "NEUROTIC_CONTRACT_OBSERVATION", contractObservation, sizeof(contractObservation));
        const std::string_view contractObservationMode =
            contractObservationLength < sizeof(contractObservation)
                ? std::string_view(contractObservation, contractObservationLength)
                : std::string_view {};
        Neurotic::Diagnostics::M0::ConfigureGlobal(
            Neurotic::Diagnostics::M0::ParseMode(contractObservationMode));
        association = profileLength == 17 && std::string_view(profile, 17) == "frame-association";
        waitForEnable = triggerLength == 9 && std::string_view(trigger, 9) == "nr-enable";
        LARGE_INTEGER f {};
        armed = length == 32 && ValidSession(std::string_view(id, 32)) &&
                (triggerLength == 0 || waitForEnable) && (profileLength == 0 || association) &&
                QueryPerformanceFrequency(&f) && f.QuadPart > 0;
        frequency = f.QuadPart;
        capturing.store(armed && !waitForEnable, std::memory_order_relaxed);
    }
};
inline Session& Current() { static Session session; return session; }
inline bool AssociationRequested() noexcept { return Current().armed && Current().association; }
inline bool Armed() noexcept
{
    auto& session = Current();
    if (!session.armed) return false;
    if (session.capturing.load(std::memory_order_acquire)) return true;
    // One handshake outside the event budget lets the launcher distinguish a valid
    // deferred capture from a lost environment. No logger work in the constructor.
    if (!session.waitingReported.load(std::memory_order_relaxed) &&
        !session.waitingReported.exchange(true, std::memory_order_relaxed))
    {
        LARGE_INTEGER qpc {};
        if (QueryPerformanceCounter(&qpc))
            try
            {
                spdlog::info("NR_FRAME_TRACE_CONTROL v=1 session={} pid={} qpc={} frequency={} "
                             "state=waiting-for-nr-enable", session.id, GetCurrentProcessId(),
                             qpc.QuadPart, session.frequency);
            }
            catch (...) { /* A diagnostic failure never changes rendering. */ }
    }
    return false;
}

inline bool Accepts(const char* kind) noexcept
{
    return Armed() && (!Current().association || AssociationEvent(kind));
}

// NR-DIAG-001 BEGIN: FrameTrace remains the integration seam, not the M0 transport.
inline bool ContractObservationEvent(std::string_view kind) noexcept
{
    return kind == "ngx-evaluate-enter" || kind == "nr-present-enter" ||
           kind == "fg-present-enter" || kind == "original-present-enter";
}

inline void ObserveContractBoundary(const char* kind) noexcept
{
    (void) Current(); // Initializes the process-scoped Off/Shadow setting exactly once.
    if (kind != nullptr && Neurotic::Diagnostics::M0::Enabled() && ContractObservationEvent(kind))
        (void) Neurotic::Diagnostics::M0::ObserveBoundary(kind, nativeObservation, presentObservation);
}

template<class Builder>
auto WithM0Publisher(Builder&& builder) noexcept
{
    (void) Current();
    return Neurotic::Diagnostics::M0::WithPublisher(std::forward<Builder>(builder));
}
// NR-DIAG-001 END

// Scalars/addresses only; this observer never retains or queries game COM resources, creates
// GPU work, waits, changes configuration, or decides whether an evaluation is allowed.
// Existing logger routing/level still applies. An absent/filtered record is NOT evidence.
template<typename... Args>
uint64_t LogEvent(const char* kind, spdlog::format_string_t<Args...> format, Args&&... args) noexcept
{
    auto& session = Current();
    if (!Accepts(kind)) return 0;
    const auto sequence = session.budget.Take();
    if (!sequence) return 0;
    LARGE_INTEGER qpc {};
    if (!QueryPerformanceCounter(&qpc)) return 0;
    try
    {
        const auto details = sequence == Budget::Limit ? std::string("reason=event-budget-exhausted") :
            spdlog::fmt_lib::format(format, std::forward<Args>(args)...);
        spdlog::info("NR_FRAME_TRACE v=1 session={} pid={} tid={} qpc={} frequency={} seq={} native={} "
                     "present={} kind={} classification=unknown {}", session.id, GetCurrentProcessId(),
                     GetCurrentThreadId(), qpc.QuadPart, session.frequency, sequence,
                     nativeObservation, presentObservation,
                     sequence == Budget::Limit ? "trace-ended" : kind, details);
    }
    catch (...) { /* Diagnostics must not change the host's result or exception path. */ }
    return sequence == Budget::Limit ? 0 : sequence;
}

template<typename... Args>
uint64_t Event(const char* kind, spdlog::format_string_t<Args...> format, Args&&... args) noexcept
{
    ObserveContractBoundary(kind);
    return LogEvent(kind, format, std::forward<Args>(args)...);
}

// Called after the existing user enable setter publishes its state. Config loading
// does not call this setter. Capture is one-shot: disable/route changes do not reset
// the budget, so transitions remain in the same trace. No configuration is written.
inline void OnNrEnable(bool wasEnabled, bool enabled) noexcept
{
    if (wasEnabled || !enabled) return;
    auto& session = Current();
    if (!session.armed || !session.waitForEnable) return;
    bool expected = false;
    if (session.capturing.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        Event("trace-started", "trigger=nr-enable previousEnabled=false enabled=true profile={}",
            session.association ? "frame-association" : "full");
}
}

// Avoid even evaluating diagnostic arguments on the unarmed path.
#define NR_FRAME_TRACE(kind, ...) \
    do { \
        const char* const _nrFrameTraceKind = (kind); \
        ::DlssNr::FrameTrace::ObserveContractBoundary(_nrFrameTraceKind); \
        if (::DlssNr::FrameTrace::Accepts(_nrFrameTraceKind)) \
            ::DlssNr::FrameTrace::LogEvent(_nrFrameTraceKind, __VA_ARGS__); \
    } while (false)
