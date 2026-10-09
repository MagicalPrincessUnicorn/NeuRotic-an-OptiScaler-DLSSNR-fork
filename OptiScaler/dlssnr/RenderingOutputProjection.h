#pragma once
#include "RenderingOutput.h"
namespace DlssNr::RenderingOutput {
inline Snapshot Project(Snapshot result,uint64_t now,bool requested,bool gated,bool childExitUnproven,bool supervisionFailed,unsigned route=2,unsigned launchError=0) {
    if(result.route!=route){
        result.active=false;result.producer=Producer::None;result.completed=0;result.fallbackEligible=false;
        result.phase=Phase::Waiting;result.reason.clear();result.sampledAt=now;result.route=route;
    }
    result.requested=requested;
    if(childExitUnproven){
        result.active=false;result.phase=Phase::Blocked;result.sampledAt=now;
        result.reason="Captured-image worker exit is unconfirmed; restart the game before rendering can resume";
    }else if(launchError&&requested){
        result.active=false;result.phase=Phase::Blocked;result.sampledAt=now;
        result.producer=Producer::None;result.completed=0;result.fallbackEligible=false;
        result.reason="Automatic output supervision stopped after an internal failure";
    }else if(supervisionFailed&&requested){
        result.active=false;result.phase=Phase::Blocked;result.sampledAt=now;
    }else if(!requested){
        result.active=false;result.phase=gated?Phase::Quiescing:Phase::Off;result.producer=Producer::None;
        result.sampledAt=now;result.reason.clear();
    }
    return result;
}
}
