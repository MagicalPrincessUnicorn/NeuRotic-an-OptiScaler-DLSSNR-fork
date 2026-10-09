#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

// Temporary camera-cost attribution: fixed storage, no producer logging,
// allocation or new mutex. Times are elapsed host time (including descheduling),
// not CPU utilization. Categories can nest and MUST NOT be summed as frame cost.
namespace Neurotic::HostCost
{
enum class Kind : uint32_t {
    InputBegin, InputFeed, InputEnd, InputWindow, InputQueue, InputCursor,
    InputRaw, InputOriginal, TrackBinding, TrackBarrier, TrackLifecycle,
    TrackOther, TrackSnapshot, RecordingWork, PresentHook, PresentOverlay,
    PresentOriginal, PresentInterval, WindowOriginal, Observation, NgxHook, Upscaler, DiagnosticReport, Count
};
inline constexpr std::array Names {
    "input-begin", "input-feed", "input-end", "input-window", "input-queue", "input-cursor",
    "input-raw", "input-original", "tracker-binding", "tracker-barrier", "tracker-lifecycle",
    "tracker-other", "tracker-snapshot", "recording-work", "present-hook", "present-overlay",
    "present-original", "present-interval", "window-original", "observation-scope", "ngx-hook", "upscaler-provider",
    "diagnostic-report"
};
static_assert(Names.size() == static_cast<size_t>(Kind::Count));
static_assert(std::atomic<uint64_t>::is_always_lock_free);
inline constexpr uint64_t ReportLimit = 600;
inline std::atomic<uint32_t> state {0}; // 0=not started, 1=active, 2=permanently stopped
inline void Activate() noexcept {
    uint32_t expected=0; state.compare_exchange_strong(expected,1,std::memory_order_relaxed);
}
inline bool Enabled() noexcept { return state.load(std::memory_order_relaxed)==1; }
inline void Stop() noexcept { state.store(2,std::memory_order_relaxed); }
struct Values { uint64_t samples=0, totalNs=0, beforeLockNs=0, maxNs=0, maxBeforeLockNs=0; };
struct Bucket {
    std::atomic<uint64_t> samples{0}, totalNs{0}, beforeLockNs{0}, maxNs{0}, maxBeforeLockNs{0};
};
inline std::array<Bucket, static_cast<size_t>(Kind::Count)> buckets;
inline void Max(std::atomic<uint64_t>& field, uint64_t value) noexcept {
    auto old = field.load(std::memory_order_relaxed);
    while (old < value && !field.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}
inline void Record(Kind kind, uint64_t total, uint64_t before=0) noexcept {
    auto& b = buckets[static_cast<size_t>(kind)];
    b.totalNs.fetch_add(total, std::memory_order_relaxed);
    b.beforeLockNs.fetch_add(before, std::memory_order_relaxed);
    Max(b.maxNs, total); Max(b.maxBeforeLockNs, before);
    b.samples.fetch_add(1, std::memory_order_relaxed);
}
// Cumulative atomic fields are read independently. Live rows are best-effort,
// not transactional; completed/quiescent totals are exact. No extrapolation.
inline Values Read(Kind kind) noexcept {
    auto& b = buckets[static_cast<size_t>(kind)];
    return {b.samples.load(std::memory_order_relaxed), b.totalNs.load(std::memory_order_relaxed),
        b.beforeLockNs.load(std::memory_order_relaxed), b.maxNs.load(std::memory_order_relaxed),
        b.maxBeforeLockNs.load(std::memory_order_relaxed)};
}
inline uint64_t Now() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
class Scope {
    Kind kind_; uint64_t start_=0, before_=0; bool active_=false;
    uint64_t (*clock_)() noexcept;
public:
    explicit Scope(Kind kind, uint32_t stride=1, uint64_t (*clock)() noexcept=Now) noexcept
        : kind_(kind), clock_(clock) {
        if (!Enabled()) return;
        if (stride > 1) {
            // Pseudorandom selection avoids repeatedly choosing one operation
            // from a periodic bind/draw/constructor/destructor sequence.
            thread_local uint32_t random = 0x9e3779b9u;
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            if ((random % stride) != 0) return;
        }
        active_=true; start_=clock_();
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    void Acquired() noexcept { if(active_) before_=clock_()-start_; }
    void Finish() noexcept {
        if(active_) { const auto end=clock_(); active_=false; Record(kind_,end-start_,before_); }
    }
    ~Scope() { Finish(); }
};
class ReportGate {
    std::atomic<uint64_t> last_{0}, count_{0};
public:
    bool Exhausted() const noexcept { return count_.load(std::memory_order_relaxed)>=ReportLimit; }
    bool Take(uint64_t now) noexcept {
        if(Exhausted()) return false;
        auto last=last_.load(std::memory_order_relaxed);
        if(now<last || now-last<2000000000ull) return false;
        if(!last_.compare_exchange_strong(last,now,std::memory_order_relaxed)) return false;
        count_.fetch_add(1,std::memory_order_relaxed); return true;
    }
};
}
