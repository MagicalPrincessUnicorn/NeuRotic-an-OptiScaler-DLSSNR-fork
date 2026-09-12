#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace DlssNr::FrameTrace
{
// Observation identifiers are deliberately NOT real-frame tokens or admission decisions.
inline bool ValidSession(std::string_view value) noexcept
{
    if (value.size() != 32) return false;
    for (const char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

class Budget
{
    std::atomic<uint64_t> next {0};
  public:
    static constexpr uint64_t Limit = 65536;
    // Saturates, including under contention. Limit is reserved for the terminal marker.
    uint64_t Take() noexcept
    {
        auto value = next.load(std::memory_order_relaxed);
        while (value < Limit)
            if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
        return 0;
    }
};

inline thread_local uint64_t nativeObservation = 0;
inline thread_local uint64_t presentObservation = 0;

class Context
{
    uint64_t& current;
    uint64_t previous;
  public:
    Context(uint64_t& target, uint64_t value) noexcept : current(target), previous(target) { current = value; }
    ~Context() { current = previous; }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
}
