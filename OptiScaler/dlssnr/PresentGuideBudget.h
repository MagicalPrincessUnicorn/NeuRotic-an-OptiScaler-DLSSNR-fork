#pragma once
#include <cstdint>
#include <limits>

namespace DlssNr::PresentGuideBudget
{
inline constexpr uint64_t LegacyFallbackBytes = 512ull * 1024 * 1024;
struct Observation
{
    bool known = false, fresh = false;
    uint64_t budget = 0, usage = 0;
};
inline Observation ObserveResult(const Observation& previous, bool succeeded,
                                 uint64_t budget, uint64_t usage) noexcept
{
    if (succeeded) return {true, true, budget, usage};
    // A past usage sample is not current process headroom. Retain only its bound.
    return {previous.known, false, previous.budget, 0};
}
inline uint64_t SaturatingAdd(uint64_t left, uint64_t right) noexcept
{
    return right > (std::numeric_limits<uint64_t>::max)() - left ?
        (std::numeric_limits<uint64_t>::max)() : left + right;
}
enum class Refusal : unsigned { None = 0, InvalidCost = 2, CommittedBudget = 3, ProcessBudget = 5 };
struct Decision
{
    bool allowed = false;
    Refusal refusal = Refusal::None;
    uint64_t bytes = 0;
};
inline Decision Evaluate(const Observation& observed, uint64_t committed,
                         uint64_t depth, uint64_t motion, bool matchingReuse, uint64_t reserved = 0) noexcept
{
    // Actual retirement/shape/device checks precede this policy. Reuse allocates
    // nothing, even if a previously admitted pair now exceeds a smaller budget.
    if (matchingReuse) return {true, Refusal::None, 0};
    constexpr auto maximum = (std::numeric_limits<uint64_t>::max)();
    if (!depth || !motion || depth == maximum || motion == maximum || depth > maximum - motion)
        return {false, Refusal::InvalidCost, 0};
    const auto bytes = depth + motion;
    const auto budget = observed.known ? observed.budget : LegacyFallbackBytes;
    if (bytes > budget || SaturatingAdd(committed, reserved) > budget - bytes)
        return {false, Refusal::CommittedBudget, bytes};
    // CurrentUsage already includes resident guide copies: never add committed
    // to usage. Committed also counts our copies that the OS may have evicted.
    if (observed.known && observed.fresh && SaturatingAdd(observed.usage, reserved) > budget - bytes)
        return {false, Refusal::ProcessBudget, bytes};
    return {true, Refusal::None, bytes};
}
}
