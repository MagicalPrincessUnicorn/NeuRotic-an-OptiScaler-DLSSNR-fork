#pragma once
#include "../../external/FidelityFX-CheckedClosure/CheckedControlledService.h"
#include <type_traits>

namespace DlssNr {
// Private, versioned transport. Neither a successful query nor caller identities
// issue C04. Product authenticates the creating module and its actual owners.
inline constexpr uint64_t NativeControlledFgOptInV1=UINT64_C(0x4e52464750524431);
struct NativeControlledFgEnrollmentV1 {
    uint32_t size=sizeof(NativeControlledFgEnrollmentV1),version=1;
    uint64_t manualOptIn=0;
    uintptr_t sdkModule=0;
    uintptr_t sdkContext=0;
    const FfxNrControlledServiceV1* service=nullptr;
    FfxNrContextTicketV1 context{};
    FfxNrOwnedOutputHandleV1 output{};
    FfxNrAlgorithmHandleV1 algorithm{};
    uint64_t featureId=0;
    uint32_t width=0,height=0,format=0,reserved=0;
};
struct NativeControlledFgObservationV1 {
    uint32_t size=sizeof(NativeControlledFgObservationV1),version=1;
    uint64_t evaluation=0,outputResource=0,outputRevision=0,recordingIncarnation=0;
    uint64_t allocationGeneration=0,algorithmGeneration=0;
    uint32_t priorWritersClosed=0,featureReleased=0,consumerEntered=0,consumerReturned=0;
    uint32_t submissionObserved=0,algorithmReleased=0,resourceRetired=0,historyAcknowledged=0;
    uint32_t sourceExcluded=0,terminal=0,failed=0,consumerRecordingTerminal=0,reserved=0;
    // r25: reserved carries the first consumer-admission refusal code (zero
    // means none observed). The V1 size, field offsets and call ABI are unchanged.
    // This is diagnostic data only; it cannot grant admission or release.
};
struct NativeControlledFgBridgeV1 {
    uint32_t size=sizeof(NativeControlledFgBridgeV1),version=1;
    uint64_t manualOptIn=0;
    uint32_t (__cdecl *begin_selection)(uintptr_t featureHandle)=nullptr;
    uint32_t (__cdecl *install_recording_hooks)(uintptr_t)=nullptr;
    uint32_t (__cdecl *pre_enroll)(const NativeControlledFgEnrollmentV1*)=nullptr;
    uint32_t (__cdecl *finish_prior_writers)(NativeControlledFgObservationV1*)=nullptr;
    FfxNrStatusV1 (__cdecl *begin_consumer)(FfxNrOwnedOutputHandleV1,FfxNrAlgorithmHandleV1,const FfxNrDispatchTicketV1*,void**)=nullptr;
    void (__cdecl *end_consumer)(void*,int32_t)=nullptr;
    FfxNrStatusV1 (__cdecl *begin_submit)(const FfxNrDispatchTicketV1*,void**)=nullptr;
    void (__cdecl *end_submit)(void*,const FfxNrSubmitResultV1*)=nullptr;
    uint32_t (__cdecl *advance)(NativeControlledFgObservationV1*)=nullptr;
    uint32_t (__cdecl *query_terminal)(NativeControlledFgObservationV1*)=nullptr;
};
using QueryNativeControlledFgBridgeV1=uint32_t (__cdecl *)(NativeControlledFgBridgeV1*);
static_assert(std::is_standard_layout_v<NativeControlledFgEnrollmentV1> && std::is_trivially_copyable_v<NativeControlledFgEnrollmentV1>);
static_assert(std::is_standard_layout_v<NativeControlledFgObservationV1> && std::is_trivially_copyable_v<NativeControlledFgObservationV1>);
static_assert(std::is_standard_layout_v<NativeControlledFgBridgeV1> && std::is_trivially_copyable_v<NativeControlledFgBridgeV1>);
}
