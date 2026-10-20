#include "../../../dlssnr/NrDiagnosticSignals.h"
#pragma once
#include "CapabilityStatusCollector.h"
#include "../candidate/Contract.h"
#include "../../../dlssnr/PreparedGuideStatus.h"
#include "../../../dlssnr/PreparedGuideStatusV2.h"
#include "../../../dlssnr/RenderingOutput.h"
namespace DlssNr::Capability {
struct OwnerContribution {
    bool depth=false,motion=false,jitter=false,preExposure=false;
    bool Any() const noexcept {return depth || motion || jitter || preExposure;}
};
struct UsableInputEvidence {
    DiagnosticSignals::Readings signals{};
    bool contextMatched=false,requestedEnabled=false,nativeRoute=false,ownerRunning=false,ownerFailed=false;
    bool resetPending=false,lifecycleOpen=false,presentGuided=false,finiteJitter=false,finitePreExposure=false;
    uint64_t lifecycle=0,resources=0,completedBefore=0,completedAfter=0,parametersBefore=0,parametersAfter=0;
};
inline OwnerContribution NormalizeUseEvidence(const UsableInputEvidence& evidence) noexcept {
    OwnerContribution result;
    if(!evidence.contextMatched || !evidence.requestedEnabled || !evidence.ownerRunning || evidence.ownerFailed ||
       evidence.completedAfter<=evidence.completedBefore)return result;
    using S=SignalReceipts::Signal;
    result.depth=DiagnosticSignals::Used(evidence.signals,S::Depth);
    result.motion=DiagnosticSignals::Used(evidence.signals,S::Motion);
    result.jitter=DiagnosticSignals::Used(evidence.signals,S::Jitter);
    result.preExposure=DiagnosticSignals::Used(evidence.signals,S::PreExposure);
    return result;
}
struct ObservationReportBundle {
    DiagnosticSignals::Readings signals{};
    uint32_t signalRoute=0;
    bool signalOwnerSupported=false;
    CollectionResult observation;
    CandidateObserver::Snapshot discovery;
    OwnerContribution owner;
    PreparedGuides::Snapshot preparedExternal;
    PreparedGuides::SnapshotV2 preparedBuiltIn;
    RenderingOutput::Snapshot renderingOutput;
    uint64_t renderingOutputCapturedTickMs=0;
    uint64_t preparedBuiltInCapturedTickMs=0;
    bool nativeCaptureSelected=false;
    uint64_t nativeCaptures=0,nativeDeliveries=0;
    uint64_t nativeScopes=0,nativeDepthCopies=0,nativeEstimatedAssociations=0;
    std::string nativeCaptureReason;
    uint64_t operation=0,context=0,started=0,completed=0,completedUtc=0;
    bool manual=false,hasDiscovery=false;
    std::string utf8;
};
bool PrepareObservationReport(ObservationReportBundle&) noexcept;
}
