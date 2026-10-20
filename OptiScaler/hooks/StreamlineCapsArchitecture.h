#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace Neurotic::Hooks
{
// Observe only the active prefix of this declared SDK publication. Inactive
// storage is not evidence of another GPU. Zero active entries and an invalid
// publication remain distinct; fallback decisions belong to the caller.
template<class Caps>
constexpr std::optional<std::uint32_t> HighestUsedArchitecture(const Caps* caps) noexcept
{
    if(!caps)return std::nullopt;
    constexpr auto capacity=std::extent_v<decltype(std::declval<Caps&>().adapters)>;
    static_assert(capacity>0);
    const auto count=static_cast<std::size_t>(caps->gpuCount);
    if(count>capacity)return std::nullopt;
    std::uint32_t highest=0;
    for(std::size_t i=0;i<count;++i)
        if(caps->adapters[i].architecture>highest)highest=caps->adapters[i].architecture;
    return highest;
}
}
