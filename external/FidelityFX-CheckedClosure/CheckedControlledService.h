#pragma once
/* Private controlled transport only. Query success is NOT C04 qualification.
 * Native pointers are borrowed. Callback handles are borrowed until callback return;
 * retain explicitly to keep them. All ownership and destruction stays in SDK module.
 * Caller must retain the module and callback user state until terminal drainage. */
#include "ffx_neurotic_checked_closure.h"
#define FFX_NR_CONTROLLED_OPT_IN_V1 UINT64_C(0x4e52464743545231)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FfxNrOwnedOutputHandleV1 { uint64_t id, cookie; } FfxNrOwnedOutputHandleV1;
typedef struct FfxNrAlgorithmHandleV1 { uint64_t id, cookie; } FfxNrAlgorithmHandleV1;
typedef struct FfxNrResourceDescriptionV1 {
 uint32_t dimension; uint64_t alignment,width; uint32_t height;
 uint16_t depth_or_array_size,mip_levels; uint32_t format,sample_count,sample_quality,layout,flags;
} FfxNrResourceDescriptionV1;
typedef struct FfxNrOwnedOutputSnapshotV1 {
 FfxNrContextTicketV1 context; uintptr_t resource; FfxNrResourceDescriptionV1 description;
 uint64_t allocation_generation; uintptr_t queue,writer_list; uint64_t writer_recording;
 uintptr_t consumer_list; uint64_t consumer_recording;
 uint32_t writer_active,writer_submitted,writer_retired,callback_active,admission_open,action_active,sdk_complete,failed;
 uint32_t ordinary_escape_excluded,recycling_excluded;
} FfxNrOwnedOutputSnapshotV1;
typedef struct FfxNrOwnedOutputRetirementV1 { uint32_t admission_closed,actions_quiescent,sdk_complete,failed; } FfxNrOwnedOutputRetirementV1;
typedef struct FfxNrOwnedWriteReceiptV1 {
 int32_t prepare_result,callback_result,close_result,signal_result; uint32_t execute_invoked; uintptr_t fence; uint64_t target;
} FfxNrOwnedWriteReceiptV1;
typedef struct FfxNrOwnedWriterRetirementReceiptV1 {
 uintptr_t list,queue,fence; uint64_t target,old_recording,new_recording;
 int32_t reset_result,close_result; uint32_t completed,old_recording_retired;
} FfxNrOwnedWriterRetirementReceiptV1;
typedef struct FfxNrAlgorithmSnapshotV1 {
 FfxNrContextTicketV1 swapchain; uint64_t algorithm_generation,allocation_generation; uintptr_t input;
 uint32_t registration_created,registration_complete,active,failed,admission_closed,destroy_attempted,released;
} FfxNrAlgorithmSnapshotV1;
typedef struct FfxNrAlgorithmReleaseReceiptV1 {
 uint64_t algorithm_generation; FfxNrDrainHandleV1 drain; uint64_t evidence_revision,cleanup_operations;
 int32_t actual_result; uint32_t whole_context_destroyed;
} FfxNrAlgorithmReleaseReceiptV1;
typedef struct FfxNrControlledConsumerObserverV1 {
 uint32_t size,version; void* user;
 FfxNrStatusV1 (FFX_NR_CALL *begin_consumer)(void*,FfxNrOwnedOutputHandleV1,FfxNrAlgorithmHandleV1,const FfxNrDispatchTicketV1*,void**);
 void (FFX_NR_CALL *end_consumer)(void*,void*,int32_t);
} FfxNrControlledConsumerObserverV1;
typedef struct FfxNrControlledWriterCallbacksV1 {
 uint32_t size,version;
 int32_t (FFX_NR_CALL *prepare)(void*,FfxNrOwnedOutputHandleV1,uintptr_t);
 int32_t (FFX_NR_CALL *write)(void*,FfxNrOwnedOutputHandleV1,uintptr_t);
 int32_t (FFX_NR_CALL *begin_submit)(void*,FfxNrOwnedOutputHandleV1,void**);
 void (FFX_NR_CALL *end_submit)(void*,void*,const FfxNrOwnedWriteReceiptV1*);
} FfxNrControlledWriterCallbacksV1;
#define FFX_NR_CONTROLLED_CONTRACT_ID_V1 UINT64_C(0x4e5257434f4e5431)
/* Reviewed transport/route semantics, not a hardware qualification or C04 certificate. */
typedef struct FfxNrControlledContractV1 {
 uint32_t size,version; uint64_t contract_id;
 uint32_t contract_revision,machine,pointer_bytes,route_flags;
} FfxNrControlledContractV1;
typedef struct FfxNrControlledServiceV1 {
 uint32_t size,version; uint64_t manual_opt_in;
 FfxNrGetContextV1 get_context;
 FfxNrStatusV1 (FFX_NR_CALL *select_algorithm)(void*,void*,const FfxNrControlledConsumerObserverV1*,FfxNrAlgorithmHandleV1*);
 FfxNrStatusV1 (FFX_NR_CALL *set_submit_observer)(const FfxNrContextTicketV1*,const FfxNrSubmissionObserverV1*);
 FfxNrEnrollSwapchainV1 enroll;
 FfxNrStatusV1 (FFX_NR_CALL *write_owned_output)(const FfxNrContextTicketV1*,const FfxNrControlledWriterCallbacksV1*,void*,FfxNrOwnedWriteReceiptV1*);
 FfxNrStatusV1 (FFX_NR_CALL *finish_writer)(const FfxNrContextTicketV1*,FfxNrOwnedWriterRetirementReceiptV1*);
 FfxNrCurrentDispatchV1 current_dispatch;
 FfxNrStatusV1 (FFX_NR_CALL *validate_dispatch)(const FfxNrDispatchTicketV1*);
 FfxNrBeginDrainV1 begin; FfxNrPollDrainV1 poll; FfxNrReadDomainsV1 read_domains; FfxNrConsumeForDestroyV1 consume;
 FfxNrStatusV1 (FFX_NR_CALL *output_retain)(FfxNrOwnedOutputHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *output_release)(FfxNrOwnedOutputHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *output_inspect)(FfxNrOwnedOutputHandleV1,FfxNrOwnedOutputSnapshotV1*);
 FfxNrStatusV1 (FFX_NR_CALL *output_begin_writer_action)(FfxNrOwnedOutputHandleV1,uintptr_t,uint64_t);
 FfxNrStatusV1 (FFX_NR_CALL *output_begin_consumer_action)(FfxNrOwnedOutputHandleV1,const FfxNrDispatchTicketV1*);
 FfxNrStatusV1 (FFX_NR_CALL *output_end_action)(FfxNrOwnedOutputHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *output_close_admission)(FfxNrOwnedOutputHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *output_inspect_retirement)(FfxNrOwnedOutputHandleV1,FfxNrOwnedOutputRetirementV1*);
 FfxNrStatusV1 (FFX_NR_CALL *output_inspect_submission)(FfxNrOwnedOutputHandleV1,FfxNrSubmitResultV1*);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_retain)(FfxNrAlgorithmHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_release)(FfxNrAlgorithmHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_inspect)(FfxNrAlgorithmHandleV1,FfxNrAlgorithmSnapshotV1*);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_owns)(FfxNrAlgorithmHandleV1,FfxNrOwnedOutputHandleV1,uint32_t*);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_close_admission)(FfxNrAlgorithmHandleV1);
 FfxNrStatusV1 (FFX_NR_CALL *algorithm_release_after_drain)(FfxNrAlgorithmHandleV1,const FfxNrDrainHandleV1*,uint64_t,FfxNrAlgorithmReleaseReceiptV1*);
 FfxNrStatusV1 (FFX_NR_CALL *query_contract)(FfxNrControlledContractV1*);
} FfxNrControlledServiceV1;
FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryControlledServiceV1(FfxNrControlledServiceV1*);
#ifdef __cplusplus
}
#endif

