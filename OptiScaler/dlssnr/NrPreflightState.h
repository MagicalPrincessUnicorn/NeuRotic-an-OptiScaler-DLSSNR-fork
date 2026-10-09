#pragma once

namespace NrPreflightState
{
enum class State { Inactive, Ready, Fault, Detected };

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
