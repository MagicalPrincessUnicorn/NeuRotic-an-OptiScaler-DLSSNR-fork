#pragma once
#include "ObservationRefreshReport.h"
#include "CapabilityRefresh.h"
namespace DlssNr::Capability::ObservationRefresh {
void Update(const RefreshState&,bool vulkan,bool manual,bool activationHeld,bool allowAutomatic=true) noexcept;
bool Busy() noexcept;
std::shared_ptr<const ObservationReportBundle> Completed() noexcept;
OwnerContribution CurrentOwner(bool finiteJitter=false,bool finitePreExposure=false) noexcept;
void RequestClose() noexcept;
struct BackendSample {RefreshState state;UsableInputEvidence evidence;bool vulkan=false;};
#ifdef NR_OBSERVATION_REFRESH_TEST
BackendSample ReadTestBackend(bool);
CollectionResult CollectTestBackend(bool);
#endif
}
