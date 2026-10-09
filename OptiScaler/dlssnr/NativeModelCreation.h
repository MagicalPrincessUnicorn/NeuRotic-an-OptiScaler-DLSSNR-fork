#pragma once
#include "NrGpuSafety.h"

namespace DlssNr
{
// One model owner's opaque creation attempt. This is not a route, resource
// lease, model evaluation, or provider-release receipt. The owning renderer
// serializes this object and retains its feature through its normal GPU drain.
// There is deliberately no retry/reset operation after a possible opaque call.
class NativeModelCreation
{
    GpuSafety::Ticket recording_;
    void* feature_=nullptr;
    bool attempted_=false;
    bool revoked_=false;
  public:
    NativeModelCreation()=default;
    NativeModelCreation(const NativeModelCreation&)=delete;
    NativeModelCreation& operator=(const NativeModelCreation&)=delete;
    template<class Create>bool TryCreate(const GpuSafety::LocalRecordingAction& action,Create&& create)
    {
        if(revoked_||attempted_||!action.Current())return false;
        recording_=action.RecordingTicket();attempted_=true;
        feature_=create(); // an exception keeps the attempt and exact recording retained
        return feature_!=nullptr;
    }
    bool Attempted()const noexcept{return attempted_;}
    void Revoke()noexcept{revoked_=true;}
    bool ReadyFor(const GpuSafety::LocalRecordingAction& action,void* feature)const
    {
        if(revoked_||!attempted_||!feature_||feature!=feature_||!action.Current()||
           action.RecordingTicket()==recording_)return false;
        const auto state=GpuSafety::InspectRecording(recording_);
        // Reusable alone includes a canceled unsubmitted list. Such a list did
        // not execute the model's initialization commands and cannot be ready.
        return state.valid&&state.registryHealthy&&state.uniqueSubmission&&
            state.completed&&state.nonReplayable;
    }
};
}
