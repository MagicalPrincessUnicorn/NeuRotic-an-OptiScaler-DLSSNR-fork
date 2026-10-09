#pragma once
#include <atomic>
#include <cstdint>
namespace DlssNr::FinalFallback {
// Admission only. These counters do not prove GPU completion or grant release.
inline std::atomic<bool> suppressInGame{false};
// Independent owner: a switch must never reopen captured-image fallback's gate.
inline std::atomic<bool> inputSwitchPending{false};
inline std::atomic<uint64_t> inputInterruptionEpoch{0};
inline std::atomic<bool> shutdownRequested{false};
inline std::atomic<unsigned> callbacks{0};
inline std::atomic<uint64_t> completedCallbacks{0};
inline thread_local unsigned renderDepth=0;
inline thread_local uint64_t admittedInputEpoch=0;
inline uint64_t CurrentInputEpoch() noexcept {
    return renderDepth?admittedInputEpoch:inputInterruptionEpoch.load(std::memory_order_seq_cst);
}
inline bool InGameAllowed() noexcept {return renderDepth!=0||(!suppressInGame.load(std::memory_order_seq_cst)&&!inputSwitchPending.load(std::memory_order_seq_cst));}
class RenderScope {
    bool admitted_=false;
    bool counted_=false;
public:
    RenderScope() noexcept {
        if(renderDepth){++renderDepth;admitted_=true;return;}
        callbacks.fetch_add(1,std::memory_order_seq_cst);
        // Read before admission: an old admitted tail cannot consume the
        // interruption marker belonging to a subsequently closed gate.
        const auto epoch=inputInterruptionEpoch.load(std::memory_order_seq_cst);
        admitted_=!suppressInGame.load(std::memory_order_seq_cst)&&!inputSwitchPending.load(std::memory_order_seq_cst)&&
            epoch==inputInterruptionEpoch.load(std::memory_order_seq_cst);
        if(admitted_){counted_=true;admittedInputEpoch=epoch;++renderDepth;}
        else callbacks.fetch_sub(1,std::memory_order_seq_cst);
    }
    ~RenderScope(){if(admitted_)--renderDepth;if(counted_)callbacks.fetch_sub(1,std::memory_order_seq_cst);}
    bool Admitted() const noexcept {return admitted_;}
    RenderScope(const RenderScope&)=delete;
};
class PresentScope {
    RenderScope admission_;
public:
    PresentScope() noexcept = default;
    ~PresentScope(){completedCallbacks.fetch_add(1,std::memory_order_release);}
    PresentScope(const PresentScope&)=delete;
};
}
