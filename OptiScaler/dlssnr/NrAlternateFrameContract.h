#pragma once
#include <nr/contracts/Identity.h>
#include "NativeTemporalSource.h"
#include <cstdint>
#include <optional>

namespace DlssNr::AlternateFrame
{
// The existing Native feature owner issues observed SR source identities.
// Callback ordinals and presentation counters cannot be converted into these.
using SourceRef=DlssNr::NativeTemporalSource;
using ViewRef=Neurotic::Contracts::ViewId;
using InvocationRef=Neurotic::Contracts::EvaluationId;
inline constexpr const char* PolicyId="AFNR_POLICY_V3";
inline constexpr const char* ReentryPolicy="OrdinaryCurrentInputs_NoSyntheticReset_V1";
inline constexpr unsigned BootstrapFulls=3, RecoveryFulls=8;
inline constexpr std::int64_t MaximumAgeUs=50000;
inline constexpr std::uint64_t AllocationCap=640ull*1024*1024;
enum class StateKind {Disabled,Suspended,Bootstrap,Eligible,RefreshDue,Recovery};
enum class DecisionKind {Full,Carry,NoSourceDecision};
enum class SourceEventClass {Real,Generated,Duplicate};
enum class Outcome {FullEvaluated,Carried,FullFallbackBeforeCarry,FailureAfterPossibleEffects};
enum class Coverage {Unknown,Complete,Partial,Zero};
enum class Reason {
    None,Off,UnsupportedRoute,UnsupportedApi,UnsupportedPassCount,UnsupportedRaster,
    UnsupportedSampleCount,UnsupportedSceneFormat,SceneDomainUnknown,SceneDomainContradicted,
    PresentationHdrDeferred,RayReconstructionDeferred,InputIdentityUnknown,PredecessorMismatch,
    DuplicateContradiction,MissingMotionConvention,MissingDepthEncoding,GuideBounds,MappingNonfinite,
    AnchorBounds,AnchorInvalid,DepthMismatch,OriginalInputNonfinite,ResidualNonfinite,OutputRange,
    ExposureScaleInvalid,ExposureScaleTransition,ExposureRatioOutOfRange,ResetDue,BootstrapDue,
    RefreshDue,RecoveryDue,AnchorTooOld,ClockUnknown,NoAnchor,AnchorNotImmutable,ProducerNotOrdered,
    ProducerNotSubmitted,ReaderReservationUnavailable,ResourceCapacity,BudgetHeadroom,BudgetUnknown,
    OutputContractRequiresModelAttempt,ConfigRevoked,RecordingFailure,PartialEffects,DeviceRemoved,
    DiagnosticSampleUnavailable
};
enum class Readiness {Unavailable,CompletedImmutable,DependencyOrderedImmutable};
struct DecisionInputs {
    bool enabled=false,supported=false,clockKnown=false,resetDue=false,invalidate=false;
    bool anchorReady=false,contradictoryDuplicate=false;
    SourceEventClass eventClass=SourceEventClass::Real;
    std::optional<SourceRef> source,predecessor;
    std::int64_t timeUs=0;
    std::uint64_t scope=0; // existing owner's compatibility generation
    Reason refusal=Reason::UnsupportedRoute;
};
struct Decision {DecisionKind kind=DecisionKind::Full;Reason reason=Reason::None;bool capture=false;};
struct PolicyState {
    StateKind state=StateKind::Disabled;
    unsigned cleanFulls=0,required=BootstrapFulls;
    std::uint64_t association=0,scope=0,lastCoverageObservation=0,fullCalls=0,omissions=0;
    std::optional<SourceRef> current,lastSource,anchor,lastModelSource;
    std::int64_t currentTimeUs=0,anchorTimeUs=0;
    bool pending=false,everActive=false,recoverAfterPending=false;
    Reason reason=Reason::Off;
};
enum class EventKind {Admit,CleanFull,CarryCommitted,FailedAfterEffects,FullFailed,ZeroCoverage,AnchorUnavailable};
struct PolicyEvent {
    EventKind kind=EventKind::Admit;
    DecisionInputs input;
    std::uint64_t association=0,observation=0;
    bool anchorAvailable=false;
};
}
