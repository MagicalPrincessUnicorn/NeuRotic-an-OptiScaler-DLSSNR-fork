#ifndef NR_GPU_SAFETY_TEST
#include "pch.h"
#endif
#include "GpuSafetyLeaseAdapter.h"

namespace Neurotic::Lifecycle
{
static Fact Observed(bool value) { return value?Fact::Yes:Fact::No; }
DependencyFacts ObserveGpuDependency(const DlssNr::GpuSafety::Ticket& ticket,
                                    ID3D12CommandQueue* consumer,bool establishCrossQueue)
{
    const auto state=DlssNr::GpuSafety::InspectRecording(ticket,consumer,establishCrossQueue);
    DependencyFacts result;
    if(!state.valid||!state.registryHealthy)return result;
    result.ordered=Observed(state.orderedForConsumer);result.committed=Observed(state.submitted);
    result.uniqueSubmission=Observed(state.uniqueSubmission);result.completed=Observed(state.completed);
    result.nonReplayable=Observed(state.nonReplayable);result.reusable=Observed(state.reusable);
    result.sameDevice=Observed(state.sameDevice);result.queueKnown=Observed(state.queueKnown);
    result.supportedType=Observed(state.supportedType);result.crossQueue=state.queueKnown&&!state.sameQueue;
    return result;
}
Fact ObserveTicketRetirement(const DlssNr::GpuSafety::Ticket& ticket)
{
    const auto state=DlssNr::GpuSafety::InspectRecording(ticket);
    return state.valid&&state.registryHealthy?Observed(state.reusable):Fact::Unknown;
}
ExternalDependencyFacts ObserveExternalDependency(const DlssNr::GpuSafety::ExternalWaitStatus* wait,
                                                 const DlssNr::GpuSafety::ExternalExecutionStatus* execution)
{
    ExternalDependencyFacts result{Fact::Unknown,Fact::Unknown,Fact::Unknown,Fact::Unknown,Fact::Unknown,Fact::Unknown};
    if(!wait||!execution)return result;
    // Read failures last: a published failure must not be converted to a success snapshot.
    result.bound=Observed(wait->bound.load());result.applied=Observed(wait->applied.load());
    result.evaluated=Observed(execution->evaluated.load());result.submitted=Observed(execution->submitted.load());
    result.healthy=Observed(!wait->failed.load()&&!execution->failed.load());
    if(result.healthy!=Fact::Yes)result.bound=result.applied=result.evaluated=result.submitted=Fact::Unknown;
    return result;
}
}
