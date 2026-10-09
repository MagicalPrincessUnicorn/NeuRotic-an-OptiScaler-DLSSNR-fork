#pragma once
#include "ConsumptionLeaseAdapter.h"
#include <dlssnr/NrGpuSafety.h>

namespace Neurotic::Lifecycle
{
// The resource owner must first validate canonical/native identity bindings. Calling
// with establishCrossQueue=true may invoke the existing OrderBefore GPU dependency.
// This does not create a queue, wait on the CPU, seal a borrowed list or grant reuse.
DependencyFacts ObserveGpuDependency(const DlssNr::GpuSafety::Ticket&,
                                    ID3D12CommandQueue*,bool establishCrossQueue=false);
Fact ObserveTicketRetirement(const DlssNr::GpuSafety::Ticket&);
// External execution proves evaluation/submission only. Release remains Unknown until
// the provider's separate resource-retention owner supplies a release registration.
struct ExternalDependencyFacts { Fact bound,applied,evaluated,submitted,healthy,released; };
ExternalDependencyFacts ObserveExternalDependency(const DlssNr::GpuSafety::ExternalWaitStatus*,
                                                 const DlssNr::GpuSafety::ExternalExecutionStatus*);
}
