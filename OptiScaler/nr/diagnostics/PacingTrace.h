#pragma once
#include <nr/diagnostics/VulkanHostCost.h>
#include <Logger.h>
#include <bit>

namespace Neurotic::PacingTrace
{
inline bool TraceEnabled() noexcept {
    if(loggerClosed.load(std::memory_order_relaxed))return false;
    try { return spdlog::should_log(spdlog::level::trace); } catch(...) { return false; }
}
inline bool Take(std::atomic<unsigned>& count) noexcept {
    auto old=count.load(std::memory_order_relaxed);
    while(old<64&&!count.compare_exchange_weak(old,old+1,std::memory_order_relaxed)){}
    return old<64;
}
template<class Call> auto Sleep(Call&& original) -> decltype(original()) {
    // Same original call, no new provider invocation or pacing decision.
    HostCost::Scope elapsed(HostCost::Kind::LatencySleep,1,HostCost::VulkanDetailClock,
                            TraceEnabled());
    return original();
}
template<class Call> auto Mode(const char* api,unsigned requested,unsigned forwarded,
    unsigned requestedLowLatency,unsigned forwardedLowLatency,bool hookOverride,Call&& original) -> decltype(original()) {
    const bool trace=TraceEnabled();
    const auto result=original();
    if(trace && !loggerClosed.load(std::memory_order_relaxed)){
        const auto error=GetLastError();
        try {
            static std::atomic<unsigned> receipts{0};
            if(Take(receipts))LOG_TRACE("NR_PACING mode api={} requestedIntervalUs={} forwardedIntervalUs={} requestedLowLatencyRaw={} forwardedLowLatencyRaw={} hookOverride={} actualResult={} coverage=first64-per-callsite noPacingChange=true",
                api,requested,forwarded,requestedLowLatency,forwardedLowLatency,hookOverride,static_cast<int>(result));
        }catch(...){/* Reporting cannot alter the original call's result. */}
        SetLastError(error);
    }
    return result;
}
inline void ExplicitCap(float cap,bool frameGeneration) noexcept {
    if(!TraceEnabled())return;
    const auto error=GetLastError();
    try {
        const uint64_t signature=std::bit_cast<uint32_t>(cap)|(uint64_t(frameGeneration)<<32);
        static std::atomic<uint64_t> previous{UINT64_MAX};
        static std::atomic<unsigned> receipts{0};
        if(previous.exchange(signature,std::memory_order_relaxed)!=signature&&Take(receipts))
            LOG_TRACE("NR_PACING explicit-limiter configuredFps={} frameGeneration={} origin=actual-FrameLimit-sleep-read coverage=first64-changes noPacingChange=true",cap,frameGeneration);
    }catch(...){/* Diagnostic failure cannot change the existing cap. */}
    SetLastError(error);
}
}
