#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace Neurotic::FrameCadence
{
// Application Present entry intervals, never GPU completion or display timing.
// One owner and provider generation per history; no allocation on the hot path.
struct Snapshot
{
    static constexpr size_t capacity = 360;
    uintptr_t owner = 0;
    uint64_t generation = 0;
    size_t count = 0, head = 0;
    double observedAt = 0;
    std::array<float, capacity> intervals {};
    float At(size_t i) const { return intervals[(head + capacity - count + i) % capacity]; }
    double Latest() const { return count ? At(count - 1) : 0; }
    double Average(size_t limit = capacity) const
    {
        const auto n = (std::min)(count, limit);
        double sum = 0;
        for (size_t i = count - n; i < count; ++i) sum += At(i);
        return n ? sum / n : 0;
    }
};
class Clock
{
    std::mutex mutex_;
    Snapshot value_;
public:
    void Observe(uintptr_t owner, uint64_t generation, double now)
    {
        std::lock_guard lock(mutex_);
        if (!owner || !std::isfinite(now) || now < 0) { value_ = {}; return; }
        if (value_.owner != owner || value_.generation != generation || now < value_.observedAt)
        {
            value_ = {};
            value_.owner = owner;
            value_.generation = generation;
            value_.observedAt = now;
            return;
        }
        if (now == value_.observedAt) return;
        value_.intervals[value_.head] = static_cast<float>(now - value_.observedAt);
        value_.head = (value_.head + 1) % Snapshot::capacity;
        value_.count = (std::min)(value_.count + 1, Snapshot::capacity);
        value_.observedAt = now;
    }
    Snapshot Read(uintptr_t owner, double now)
    {
        std::lock_guard lock(mutex_);
        // Stale evidence never turns into an estimated game rate. A real stall
        // remains in history when the next application Present arrives.
        return owner && owner == value_.owner && std::isfinite(now) &&
            now >= value_.observedAt && now - value_.observedAt <= 250.0 ? value_ : Snapshot {};
    }
    void Invalidate(uintptr_t owner = 0)
    {
        std::lock_guard lock(mutex_);
        if (!owner || value_.owner == owner) value_ = {};
    }
};
inline Clock& GameFrames() { static Clock clock; return clock; }
// A render call can only consume evidence for its own swapchain owner. Other
// APIs and offscreen renderers keep the explicitly labelled callback clock.
inline thread_local uintptr_t outputOwner = 0;
struct OutputScope
{
    uintptr_t previous;
    explicit OutputScope(uintptr_t owner) : previous(outputOwner) { outputOwner = owner; }
    ~OutputScope() { outputOwner = previous; }
    OutputScope(const OutputScope&) = delete;
    OutputScope& operator=(const OutputScope&) = delete;
};
}
