#pragma once

#include "SynchronizedOptional.h"
#include <algorithm>
#include <atomic>
#include <cstdint>

// CPU-only NR transactions shared by production Config and standalone tests.
class NrConfigState
{
  public:
    struct RuntimeSnapshot
    {
        bool enabled = false;
        uint64_t resumeGeneration = 0;
    };

    void SetEnabled(NrOptional<bool>& option, bool enabled)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        option = enabled;
        PublishEnabled(enabled);
    }
    void LoadEnabled(NrOptional<bool>& option, const std::optional<bool>& enabled)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        option.set_from_config(enabled);
        PublishEnabled(option.value_or_default());
    }
    RuntimeSnapshot Snapshot() const noexcept
    {
        const uint64_t state = _runtimeState.load(std::memory_order_acquire);
        return { (state & 1u) != 0, state >> 1u };
    }

    // Atomic-only: called inside the connection-policy commit. Preserve the
    // user's enabled setting and let each existing owner retire its session.
    bool RequestNewSession() noexcept
    {
        uint64_t current = _runtimeState.load(std::memory_order_acquire);
        for (;;)
        {
            if ((current >> 1u) == (UINT64_MAX >> 1u))
                return false;
            if (_runtimeState.compare_exchange_weak(current, current + 2u,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                return true;
        }
    }

    static void SetRoutingMode(NrOptional<int32_t>& mode, NrOptional<bool>& beforeSr, int32_t value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        mode = std::clamp(value, 0, 1);
        beforeSr = mode.value_or_default() != 0;
    }
    static void LoadRoutingMode(NrOptional<int32_t>& mode, NrOptional<bool>& beforeSr, int32_t value)
    {
        NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
        mode.set_from_config(std::clamp(value, 0, 1));
        beforeSr.set_from_config(mode.value_or_default() != 0);
    }

  private:
    std::atomic<uint64_t> _runtimeState { 0 };

    void PublishEnabled(bool enabled) noexcept
    {
        uint64_t current = _runtimeState.load(std::memory_order_acquire);
        for (;;)
        {
            if ((current & 1u) == static_cast<uint64_t>(enabled))
                return;
            const uint64_t next = enabled ? ((((current >> 1u) + 1u) << 1u) | 1u)
                                          : (current & ~uint64_t { 1u });
            if (_runtimeState.compare_exchange_weak(current, next, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                return;
        }
    }
};
