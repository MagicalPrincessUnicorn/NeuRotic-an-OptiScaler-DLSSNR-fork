#pragma once
#include <mutex>

namespace Neurotic::Runtime
{
// One native D3D12 core is shared by native passthrough and owned SR/RR.
// Internal wrapper-to-original calls stay on this lease. New nested public
// operations are rejected before touching contexts or provider state.
class NgxCallLease
{
    static std::recursive_mutex& Mutex() { static auto* mutex = new std::recursive_mutex; return *mutex; }
    inline static thread_local unsigned depth_ = 0;
    std::unique_lock<std::recursive_mutex> lock_;
    bool admitted_ = false;
 public:
    NgxCallLease() : lock_(Mutex(), std::defer_lock)
    {
        if (depth_) return;
        lock_.lock(); ++depth_; admitted_ = true;
    }
    ~NgxCallLease() { if (admitted_) --depth_; }
    explicit operator bool() const { return admitted_; }
};
}
