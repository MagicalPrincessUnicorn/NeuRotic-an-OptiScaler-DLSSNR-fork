#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <atomic>

namespace DlssNr::AdvisorSampling
{
inline std::atomic_bool TemporarySettings { false };
inline std::atomic_uint64_t ConfigurationGeneration { 0 };
inline bool ObserveNativeCadence(bool enabled, unsigned int route, uint32_t swapchains)
{ return TemporarySettings.load() && enabled && route == 0 && swapchains == 1; }
enum class CadenceSource { Unknown, ApplicationPresentFgOff, VerifiedPreFgPresent };
struct Cadence
{
    uint64_t sequence = 0, providerGeneration = 0, resourceGeneration = 0;
    unsigned int route = 0;
    double intervalMs = 0;
    bool native = false;
    uint64_t configurationGeneration = 0;
    CadenceSource source = CadenceSource::Unknown;
};
struct Window
{
    uint64_t lastSequence = 0;
    unsigned int warmFrames = 0, samples = 0;
    double totalMs = 0, baselineMedianMs = 0;
    bool Consume(const Cadence& value)
    {
        if (!value.sequence || value.sequence <= lastSequence) return false;
        lastSequence = value.sequence;
        return value.native && std::isfinite(value.intervalMs) && value.intervalMs > 0;
    }
    bool Stall(double interval) const { return interval > (std::max)(500.0, baselineMedianMs * 8.0); }
    bool RejectStall(bool measuring, double tickMs, bool fresh, double intervalMs) const
    { return measuring && (Stall(tickMs) || (fresh && Stall(intervalMs))); }
    bool WarmupFrame(bool matching, bool fresh, double intervalMs, double tickMs)
    {
        if (matching && !Stall(intervalMs) && !Stall(tickMs)) ++warmFrames;
        else if (fresh || Stall(tickMs)) warmFrames = 0;
        return warmFrames >= 30;
    }
    bool StartupExpired(double elapsed) const { return elapsed >= 15.0; }
    bool Complete(double elapsed) const { return samples >= 120 && elapsed >= 3.0; }
    double Fps() const { return samples && totalMs > 0 ? 1000.0 * samples / totalMs : 0; }
};
}
