#pragma once

#include <cmath>
#include <optional>
#include <cstdint>

namespace NrPreflightSignals
{
// Presentation-only freshness. Counts are evidence of progress, never a score
// or permission to acquire inputs. The renderer remains the qualification owner.
class FreshObservation
{
    uint64_t owner_ = 0, count_ = 0, changedAt_ = 0, sampledAt_ = 0;
    bool seen_ = false;
public:
    bool Update(uint64_t owner, uint64_t count, uint64_t now, bool available) noexcept
    {
        if (!available) { *this = {}; return false; }
        if (!seen_ || owner != owner_ || count < count_ || now < sampledAt_ || now - sampledAt_ > 2000) {
            owner_ = owner; count_ = count; changedAt_ = 0; seen_ = true;
        } else if (count > count_) {
            count_ = count; changedAt_ = now;
        }
        sampledAt_ = now;
        return changedAt_ && now >= changedAt_ && now - changedAt_ <= 2000;
    }
};

struct Pair
{
    float x = 0.0f;
    float y = 0.0f;
};

// A read-only record of the latest native DLSS parameter block. Presence refers to
// source keys, not the effective defaults used by the renderer or model success.
struct NativeInputs
{
    unsigned long long observations = 0;
    std::optional<Pair> jitter;
    std::optional<Pair> motionScale;
    std::optional<float> preExposure;
    bool exposureTexture = false;
    bool jitterSupplied = false, motionScaleSupplied = false, preExposureSupplied = false;

    void Record(bool jitterXPresent, float jitterX, bool jitterYPresent, float jitterY,
                bool motionXPresent, float motionX, bool motionYPresent, float motionY,
                bool preExposurePresent, float preExposureValue, bool texturePresent)
    {
        ++observations;
        jitterSupplied = jitterXPresent || jitterYPresent;
        motionScaleSupplied = motionXPresent || motionYPresent;
        preExposureSupplied = preExposurePresent;
        jitter = jitterXPresent && jitterYPresent && std::isfinite(jitterX) && std::isfinite(jitterY)
            ? std::optional<Pair>{{jitterX, jitterY}} : std::nullopt;
        motionScale = motionXPresent && motionYPresent && std::isfinite(motionX) && std::isfinite(motionY)
            ? std::optional<Pair>{{motionX, motionY}} : std::nullopt;
        preExposure = preExposurePresent && std::isfinite(preExposureValue)
            ? std::optional<float>{preExposureValue} : std::nullopt;
        exposureTexture = texturePresent;
    }
};
} // namespace NrPreflightSignals
