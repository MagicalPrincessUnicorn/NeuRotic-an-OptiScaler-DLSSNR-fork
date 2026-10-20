#pragma once

#include "MfgControl.h"
#include "MfgAdaUnlock.h"
#include <array>

namespace Neurotic::Mfg
{
struct MfgTraceStamp
{
    uint64_t sequence = 0;
    uint64_t firstAttempt = 0;
};

// Diagnostic deduplication only. Repeated setters do not reset the observation
// budget. A presented counter is since the game's previous query, not a ratio
// or evidence of distinct images. Keep only the first count in the >31 bucket.
class MfgRequestTrace
{
    using Key = std::array<uint64_t, 23>;
    std::mutex mutex_;
    Key key_ {};
    bool haveKey_ = false, setterEmitted_ = false;
    uint64_t sequence_ = 0, firstAttempt_ = 0, latestAttempt_ = 0;
    std::array<uint64_t, 8> presented_ {};

    static Key RequestKey(const MfgRequestReceipt& r, const MfgRuntimeSnapshot& p) noexcept
    {
        return {r.viewport, r.generation, r.callerAbi, r.gameEnabled, r.gameGenerated,
            static_cast<uint64_t>(r.selected), r.forwardedEnabled, r.forwardedGenerated,
            static_cast<uint64_t>(r.selectionStatus), r.setterResult.has_value(),
            static_cast<uint32_t>(r.setterResult.value_or(0)), r.overrideResult.has_value(),
            static_cast<uint32_t>(r.overrideResult.value_or(0)), r.fellBackToGame, r.accepted,
            p.generation, static_cast<uint64_t>(p.status), static_cast<uint64_t>(p.reason),
            p.temporalProfile, p.patchSites, p.maxGenerated, p.providerFingerprint, p.wrapperFingerprint};
    }

public:
    std::optional<MfgTraceStamp> Select(const MfgRequestReceipt& r,
        const MfgRuntimeSnapshot& p, bool statePhase) noexcept
    {
        if (!r.attempt || r.stale || !r.setterResult.has_value()) return std::nullopt;
        const auto key = RequestKey(r, p);
        std::lock_guard lock(mutex_);
        if (r.attempt < latestAttempt_) return std::nullopt;
        if (statePhase)
        {
            // A query cannot be attached to a different, newer setter or to a
            // publication that was not recorded with that setter.
            if (!haveKey_ || key != key_ || r.attempt != latestAttempt_ ||
                !r.observedPresented.has_value()) return std::nullopt;
        }
        else
        {
            latestAttempt_ = r.attempt;
            if (!haveKey_ || key != key_)
            {
                key_ = key;
                haveKey_ = true;
                setterEmitted_ = false;
                presented_.fill(0);
                firstAttempt_ = r.attempt;
                if (++sequence_ == 0) ++sequence_;
            }
            if (setterEmitted_) return std::nullopt;
            setterEmitted_ = true;
            return MfgTraceStamp {sequence_, firstAttempt_};
        }
        // Unknown capacity, known 0..5, and other values stay distinct. At most
        // eight capacity buckets times 33 counter buckets per semantic request.
        const size_t capacity = !r.observedMax ? 0 : *r.observedMax <= 5 ? *r.observedMax + 1 : 7;
        const auto count = *r.observedPresented;
        const uint64_t bit = uint64_t {1} << (count < 32 ? count : 32);
        if (presented_[capacity] & bit) return std::nullopt;
        presented_[capacity] |= bit;
        return MfgTraceStamp {sequence_, firstAttempt_};
    }
};
}
