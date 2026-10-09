#pragma once
#include "ConnectionPolicy.h"
#include "../FinalFallbackControl.h"

namespace DlssNr::Connections {
struct InputRestriction {InputPhase phase=InputPhase::RestartRequired;std::string reason;};
// Control state only. The existing supervisor supplies owner-specific proof;
// this object never destroys resources or waits on the game's device.
class InputSelectionPump {
    uint64_t serial_ = 0, started_ = 0;
    std::string pendingReason_;
    void Release() noexcept {
        if(serial_)EndInputSelection();
        serial_=0;
    }
public:
    InputSelectionPump() = default;
    InputSelectionPump(const InputSelectionPump&) = delete;
    InputSelectionPump& operator=(const InputSelectionPump&) = delete;
    ~InputSelectionPump() {
        if(serial_)RefuseInputSelection(serial_,InputPhase::Blocked,"Input change interrupted; previous input retained");
        Release();
    }
    template<class Restriction,class Proof>
    void Poll(uint64_t now,Restriction&& restrictRequest,Proof&& proof,
              bool (*advanceSession)() noexcept) {
        auto request=QueryInputSelection();
        if(serial_&&(serial_!=request.serial||request.phase!=InputPhase::Switching))Release();
        if(request.phase!=InputPhase::Waiting&&request.phase!=InputPhase::Switching)return;
        if(serial_&&now-started_>=2000) {
            RefuseInputSelection(serial_,InputPhase::Blocked,"Input unchanged: "+pendingReason_);
            Release();return;
        }
        const auto restriction=restrictRequest(request);
        if(!restriction.reason.empty()) {
            RefuseInputSelection(request.serial,restriction.phase,restriction.reason);
            Release();return;
        }
        if(!serial_) {
            if(!BeginInputSelection(request.serial)) {
                RefuseInputSelection(request.serial,InputPhase::Blocked,"Input ownership is quarantined; previous input retained");
                return;
            }
            serial_=request.serial;started_=now;pendingReason_="Waiting for NR callbacks to finish";
        }
        std::string reason="Waiting for NR callbacks to finish";
        if(!FinalFallback::callbacks.load(std::memory_order_seq_cst)&&proof(reason)) {
            if(CommitInputSelection(serial_,advanceSession)){Release();return;}
            reason="Input owner is still active";
        }
        pendingReason_=std::move(reason);
    }
};
}
