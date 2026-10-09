#pragma once
#include <cstdint>
#include <type_traits>

namespace DlssNr
{
// Read-only snapshot of the existing command-list observer. This diagnostic
// cannot install hooks, authorize an invocation, or advance a recording.
struct NativeRecordingObservationV1
{
    std::uint32_t size=sizeof(NativeRecordingObservationV1),version=1;
    std::uint64_t commandList=0,incarnation=0,workOrdinal=0,hookGeneration=0;
    // 1 active, 2 complete coverage, 4 tracking began before recording,
    // 8 missing required history. A usable observation has exactly flags=7.
    std::uint32_t flags=0,reserved=0;
};
static_assert(sizeof(NativeRecordingObservationV1)==48);
static_assert(std::is_trivially_copyable_v<NativeRecordingObservationV1>);
using QueryNativeRecordingV1=unsigned(__cdecl*)(std::uint64_t,NativeRecordingObservationV1*,std::uint32_t) noexcept;
// Bounded, read-only hook diagnostics. Address records/counters describe hook
// activity only; they never grant recording coverage or source identity.
struct NativeRecordingDiagnosticV1
{
    std::uint32_t size=sizeof(NativeRecordingDiagnosticV1),version=1;
    std::uint64_t queriedList=0,queriedVtable=0,installedVtable=0;
    std::uint64_t installAttempts=0,installSuccesses=0,closeCalls=0;
    std::uint64_t resetCalls=0,resetSuccesses=0,enrollmentSuccesses=0,queryInterfaceCalls=0;
    std::uint64_t firstRefusalSequence=0,droppedEvents=0;
    std::uint64_t globalInstallAttempts=0,globalInstallSuccesses=0,lastInstallList=0;
    std::uint64_t queriedResetTarget=0,installedResetTarget=0,resetPatchTarget=0;
    std::uint32_t flags=0,firstRefusal=0,failedSlot=UINT32_MAX,enrollmentStage=0;
    std::int32_t firstResult=0,lastResetResult=0;
    unsigned char failedIid[16]{};
    std::uint64_t expectedIdentity=0,observedIdentity=0,patchTarget=0;
    unsigned char expectedBytes[16]{},observedBytes[16]{};
};
static_assert(sizeof(NativeRecordingDiagnosticV1)==248);
static_assert(std::is_trivially_copyable_v<NativeRecordingDiagnosticV1>);
using QueryNativeRecordingDiagnosticV1=unsigned(__cdecl*)(std::uint64_t,NativeRecordingDiagnosticV1*,std::uint32_t) noexcept;
struct NativeObservedIdentityV1
{std::uint64_t value=0;char nameSpace[96]{},issuer[96]{};};
static_assert(sizeof(NativeObservedIdentityV1)==200);
// Read-only diagnostic ABI. Native pointers correlate this local caller only;
// none of these fields issue source, Resource, consumer or retirement rights.
struct NativeHostReturnObservationV1
{
    std::uint32_t size=sizeof(NativeHostReturnObservationV1),version=1,status=0,stageOutcome=0;
    std::uint64_t sequence=0,featureHandle=0,commandList=0,outputResource=0;
    std::uint64_t callbackRecord=0,evaluation=0,lastRecordedOrdinal=0;
    std::uint32_t hostResult=0,restorationFlags=0,runtimePreparation=0,reserved=0;
    char sourceNamespace[96]{},reason[128]{};
    NativeObservedIdentityV1 outputObjectIncarnation,outputResourceIncarnation,outputViewIncarnation;
    NativeObservedIdentityV1 outputResourceGeneration,outputRepresentationGeneration,recordingIdentity;
    std::uint64_t outputContentRevision=0,recordingIncarnation=0;
    std::uint32_t provenanceFlags=0,reserved2=0;
    char sourceIssuer[96]{};
};
static_assert(sizeof(NativeHostReturnObservationV1)==1632);
static_assert(std::is_trivially_copyable_v<NativeHostReturnObservationV1>);
// status: 0 unavailable, 1 no invocation, 2 pending, 3 rejected,
// 4 actual HostReturnRecorded. stageOutcome: existing NativeStageOutcome.
// restorationFlags: 1 CPU, 2 command state, 4 actual outer return observed,
// 8 outer returned success. A previous success never matches a new baseline.
// featureHandle is NVSDK_NGX_Handle::Id, never the handle's pointer.
// provenanceFlags==3 means exact current returned Resource (bit1) and its
// same actual selected recording observation (bit2); zero means unavailable.
inline bool MatchesNativeReturnObservation(const NativeHostReturnObservationV1& value,
    std::uint64_t afterSequence,std::uint64_t handle,std::uint64_t list,std::uint64_t output)noexcept
{
    return value.version==1&&value.size==sizeof(value)&&value.sequence>afterSequence&&
        handle&&list&&output&&value.featureHandle==handle&&value.commandList==list&&value.outputResource==output;
}
using QueryNativeReturnV1=unsigned(__cdecl*)(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,
    NativeHostReturnObservationV1*,std::uint32_t);
}
