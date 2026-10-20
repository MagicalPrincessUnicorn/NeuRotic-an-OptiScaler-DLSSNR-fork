// Matched C++ toolchain only. Explicit manual opt-in; not a qualified public ABI.
#pragma once
#include "CheckedAlgorithm.h"
namespace ffx::nr {
inline constexpr uint64_t ExperimentalManualOptInV1=0x4e52464745585031ull;
struct ExperimentalServiceV1 {
    uint32_t size=sizeof(ExperimentalServiceV1),version=1;
    uint64_t manualOptIn=0;
    FfxNrStatusV1 (*get_context)(void*,FfxNrContextTicketV1*)=nullptr;
    FfxNrStatusV1 (*select_algorithm)(void* exactCreatedAlgorithm,void* exactCreatedSwapchain,const AlgorithmConsumerObserver*,std::shared_ptr<AlgorithmLease>*)=nullptr;
    FfxNrStatusV1 (*set_submit_observer)(const FfxNrContextTicketV1*,const FfxNrSubmissionObserverV1*)=nullptr;
    FfxNrStatusV1 (*enroll)(void*,const FfxNrEnrollRequestV1*,FfxNrContextTicketV1*)=nullptr;
    FfxNrStatusV1 (*write_owned_output)(const FfxNrContextTicketV1*,const OwnedWriterCallbacks*,void*,OwnedWriteReceipt*)=nullptr;
    FfxNrStatusV1 (*finish_writer)(const FfxNrContextTicketV1*,OwnedWriterRetirementReceipt*)=nullptr;
    FfxNrStatusV1 (*current_dispatch)(const FfxNrContextTicketV1*,FfxNrDispatchTicketV1*)=nullptr;
    FfxNrStatusV1 (*validate_dispatch)(const FfxNrDispatchTicketV1*)=nullptr;
    FfxNrBeginDrainV1 begin=nullptr;
    FfxNrPollDrainV1 poll=nullptr;
    FfxNrReadDomainsV1 read_domains=nullptr;
    FfxNrConsumeForDestroyV1 consume=nullptr;
};
}
extern "C" FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryExperimentalServiceV1(ffx::nr::ExperimentalServiceV1*);
