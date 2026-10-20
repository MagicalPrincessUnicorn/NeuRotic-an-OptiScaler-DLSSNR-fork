// GPL-3.0. Measurements never confer GPU completion or publication rights.
#pragma once
#include <array>
#include <string>
#include <optional>
#include <cstdint>
#include <chrono>
namespace nrw {
using OptionalMs = std::optional<double>;
inline OptionalMs TimestampSpan(uint64_t first, uint64_t last, uint64_t frequency,
                                uint64_t completed, uint64_t required) {
    if (!frequency || !required || completed == UINT64_MAX || completed < required || last < first)
        return {};
    return double(last-first)*1000.0/double(frequency);
}
struct NrMeasurements {
    OptionalMs prepareMs, evaluateCallMs, submitCallMs, queueWaitMs, publishMs;
    OptionalMs shapeMs, ingressMs, guidePrepareMs, identityMs;
    // Same direct queue, separate adjacent spans. Never sum with CPU wall time.
    std::array<OptionalMs,4> gpu{}; // guide preparation, conversion, provider commands, composition
    uint64_t timestampFrequency=0, completionFence=0, historyEpoch=0, guideUploadBytes=0;
    bool reset=false, gpuCompleted=false;
    std::string adapterLuid;
    std::string guidePreparation="unknown";
};
using MeasureClock = std::chrono::steady_clock;
inline double ElapsedMs(MeasureClock::time_point start) {
    return std::chrono::duration<double,std::milli>(MeasureClock::now()-start).count();
}
}
