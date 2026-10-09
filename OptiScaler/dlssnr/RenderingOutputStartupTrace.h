#pragma once
#include <cstdint>
#include <string>
#include <optional>
namespace DlssNr::RenderingOutput {
// Scalar transition identity only: clocks/frame totals never cause a log per frame.
// This receipt does not authorize a rendering transition or release any owner.
struct StartupState {
    uint64_t context=0;
    unsigned route=2;
    bool requested=false,recent=false,windowValid=false,visible=false,iconic=false,foreground=false;
    bool effect=false,unsafe=false,hardFailure=false,attempted=false,gated=false,callbacksReady=false;
    bool controller=false,connected=false,busy=false,ready=false,workerActive=false;
    bool modelSent=false,catalogSent=false,startSent=false,paused=false,childExitUnproven=false;
    std::string stage,workerPhase,reason;
    bool operator==(const StartupState&) const=default;
};
class StartupTrace {
    std::optional<StartupState> last_;
public:
    bool Observe(const StartupState& state) {
        if(last_&&*last_==state)return false;
        last_=state;return true;
    }
};
struct StartupObservation {
    bool requested=false,callbackSeen=false,supervisorRunning=false,supervisionFailed=false;
    bool childExitUnproven=false,sampleFresh=false,sampleMatchesRequest=false;
    unsigned route=2;
};
struct StartupRequestIdentity {
    uint64_t generation=0,target=0;
    unsigned route=2;
    bool requested=false,vulkan=false;
    bool operator==(const StartupRequestIdentity&) const=default;
};
inline bool StartupRequestMatches(const StartupRequestIdentity& sample,const StartupRequestIdentity& current) {
    return sample==current;
}
inline const char* StartupObservationStage(const StartupObservation& observation) {
    if(observation.childExitUnproven)return "child-exit-unconfirmed";
    if(observation.requested&&observation.supervisionFailed)return "supervisor-failed";
    if(!observation.requested)return "off";
    if(observation.route!=3)return "different-route";
    if(!observation.callbackSeen)return "awaiting-first-callback";
    if(!observation.supervisorRunning)return "supervisor-not-running";
    if(!observation.sampleFresh||!observation.sampleMatchesRequest)return "awaiting-current-sample";
    return "sampled";
}
// Read-only scalar receipt. It also works when no Present callback started Run.
std::string StartupDiagnostics();
}
