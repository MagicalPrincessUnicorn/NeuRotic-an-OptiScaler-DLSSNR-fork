#pragma once

namespace NrPreflightState
{
enum class State { Inactive, Ready, Fault, Detected, Pending, Off, NotApplicable, Usable, Consumed, Stale, Unavailable, NotReported };
inline const char* Name(State state) noexcept {
    switch(state){case State::Ready:case State::Consumed:return "Consumed (completed receipt)";
    case State::Detected:return "Detected";case State::Usable:return "Validated, not consumed";
    case State::NotReported:return "Not reported";
    case State::Pending:return "Pending initialization / runtime";case State::Off:return "Off";
    case State::NotApplicable:return "Not applicable";case State::Stale:return "Stale";
    case State::Fault:return "Invalid or blocked";case State::Unavailable:return "Unavailable";
    default:return "No current signal";}
}

// Observation and consumption are separate facts. A valid detected input only
// turns green when the effective owner reports that it is in use.
constexpr State Usage(State observed, bool inUse) noexcept
{
    return observed == State::Ready || observed == State::Detected
        ? (inUse ? State::Ready : State::Detected) : observed;
}

// An expected signal is only judged after its owner has made an observation.
// A supplied but invalid value is a fault, never a ready/defaulted value.
constexpr State Signal(bool expected, bool observed, bool supplied, bool valid) noexcept
{
    if (!expected || !observed) return State::Inactive;
    return supplied && valid ? State::Ready : State::Fault;
}

constexpr State Delivery(bool expected, unsigned long long attempts,
                         unsigned long long completions, bool failed) noexcept
{
    if (!expected) return State::Inactive;
    if (failed) return State::Fault;
    if (completions) return State::Ready;
    return attempts ? State::Fault : State::Inactive;
}
} // namespace NrPreflightState
