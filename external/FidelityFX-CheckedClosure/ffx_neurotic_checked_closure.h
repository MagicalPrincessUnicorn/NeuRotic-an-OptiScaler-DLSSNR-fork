/* SDK-owned private ABI under construction; capability export is unavailable until
 * concrete SDK wrapper coverage and build tests pass.
 * This is NOT an AMD public API. Do not use to label an old SDK wait as checked.
 * Implement once per pinned SDK build, with reviewed export name and ABI tests.
 * Opaque integers are registry locators; the SDK validates origin/incarnation.
 */
#ifndef NEUROTIC_FSR_CHECKED_DRAIN_V1_H
#define NEUROTIC_FSR_CHECKED_DRAIN_V1_H
#include <stdint.h>
#if defined(_WIN32)
# define FFX_NR_CALL __cdecl
#else
# define FFX_NR_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t FfxNrStatusV1;
enum {
    FFX_NR_COMPLETE = 0, FFX_NR_PENDING = 1, FFX_NR_BUSY = 2,
    FFX_NR_REENTRANT = 3, FFX_NR_TIMEOUT_RETAINED = 4,
    FFX_NR_API_FAILURE_RETAINED = 5, FFX_NR_DEVICE_REMOVED_RETAINED = 6,
    FFX_NR_UNSUPPORTED = 7, FFX_NR_STALE_CONTEXT = 8,
    FFX_NR_INCOMPLETE_COVERAGE = 9, FFX_NR_INVALID_ARGUMENT = 10,
    FFX_NR_EXTERNAL_RECORDING_OUTSTANDING = 11
};
typedef struct FfxNrContextTicketV1 {
    uint64_t owner_id;
    uint64_t incarnation;
    uint64_t registration_generation;
} FfxNrContextTicketV1;

/* Separate additive entrypoints; the original service table layout stays fixed. */
typedef struct FfxNrEnrollRequestV1 {
    uint32_t size, version;
    uint64_t expected_provider_id;
    uint32_t flags; /* v1: zero, fixed size / owned lists / default compositor */
    uint32_t reserved;
} FfxNrEnrollRequestV1;
typedef struct FfxNrDispatchTicketV1 {
    uint32_t size, version;
    FfxNrContextTicketV1 context;
    uint64_t dispatch_id;
    uint64_t registration_generation;
    uint64_t recording_incarnation;
    uint64_t buffer_generation;
    uintptr_t command_list;
    uintptr_t command_queue;
    uintptr_t present_color; /* observed SDK allocation; does not assert host-write exclusion */
    uint64_t frame_id; /* SDK scheduling value, never original game-frame identity */
} FfxNrDispatchTicketV1;
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrEnrollSwapchainV1)(
    void* exact_created_swapchain, const FfxNrEnrollRequestV1*, FfxNrContextTicketV1*);
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrCurrentDispatchV1)(
    const FfxNrContextTicketV1*, FfxNrDispatchTicketV1*);
typedef struct FfxNrSubmitResultV1 {
    uint32_t size, version;
    FfxNrDispatchTicketV1 dispatch;
    int32_t callback_result, close_result, signal_result;
    uint32_t execute_invoked, output_published;
} FfxNrSubmitResultV1;
typedef struct FfxNrSubmissionObserverV1 {
    uint32_t size, version;
    void* user;
    /* begin holds the real Resource action lock until end; failed admission executes nothing. */
    FfxNrStatusV1 (FFX_NR_CALL *begin_submit)(void* user, const FfxNrDispatchTicketV1*, void** call_guard);
    /* end releases the call guard before application journal publication. */
    void (FFX_NR_CALL *end_submit)(void* user, void* call_guard, const FfxNrSubmitResultV1*);
} FfxNrSubmissionObserverV1;
typedef struct FfxNrDispatchServiceV1 {
    uint32_t size, version;
    FfxNrEnrollSwapchainV1 enroll;
    FfxNrCurrentDispatchV1 current_dispatch;
    FfxNrStatusV1 (FFX_NR_CALL *validate_dispatch)(const FfxNrDispatchTicketV1*);
    FfxNrStatusV1 (FFX_NR_CALL *set_observer)(const FfxNrContextTicketV1*, const FfxNrSubmissionObserverV1*);
} FfxNrDispatchServiceV1;
FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryDispatchServiceV1(FfxNrDispatchServiceV1*);
typedef struct FfxNrBuildInfoV1 {
    uint32_t size, version;                 /* version = 1; reserved fields zero */
    uint8_t build_id[32];                    /* compiled identity, NOT final PE SHA */
    uint8_t source_contract_digest[32];
    uint64_t provider_id;
    uint64_t supported_feature_bits;       /* contract-defined; unknown bits refuse */
    uint64_t qpc_frequency;                /* absolute deadlines use this clock */
    uint32_t abi_variant;                   /* exact old / v2 template ABI */
    uint32_t reserved;
} FfxNrBuildInfoV1;
typedef struct FfxNrDrainRequestV1 {
    uint32_t size, version;
    FfxNrContextTicketV1 context;
    uint64_t operation_nonce;              /* exact retry idempotence key */
    int64_t absolute_deadline_qpc;
    uint32_t mode;                         /* 1 = terminal close; no reopen */
    uint32_t flags;                        /* must be zero in v1 */
} FfxNrDrainRequestV1;
typedef struct FfxNrDrainHandleV1 {
    FfxNrContextTicketV1 context;
    uint64_t drain_id;
    uint64_t closed_epoch;
} FfxNrDrainHandleV1;
typedef struct FfxNrDrainSummaryV1 {
    uint32_t size, version;
    FfxNrDrainHandleV1 drain;
    FfxNrStatusV1 status;
    int32_t device_removed_reason;         /* HRESULT bit pattern */
    uint64_t evidence_revision;
    uint64_t failure_sequence;             /* first latched failure, 0 only if none */
    uint32_t domain_count;
    uint32_t root_count, child_count, active_callback_count;
    uint32_t worker_count, terminated_worker_count;
    uint32_t outstanding_list_borrow_count;
    uint32_t coverage_flags;               /* complete set defined by matched contract */
    /* No providerReleased boolean: algorithm release is a separate contract. */
} FfxNrDrainSummaryV1;
typedef struct FfxNrDomainEvidenceV1 {
    uint32_t size, version;
    uint32_t domain_kind, logical_role_bits;
    uint64_t domain_id, domain_generation;
    uint64_t queue_id, queue_generation;    /* zero only for explicitly CPU domain */
    uint64_t fence_id, fence_generation;
    uint64_t required_target, last_attempted_target, last_successful_target;
    uint64_t observed_completed_value;
    int32_t last_api_result, device_removed_reason;
    uint32_t evidence_flags, reserved;
} FfxNrDomainEvidenceV1;

/* Nonblocking owner lookup; implementation must validate against creation registry.
 * It must not reinterpret/dereference arbitrary caller pointers to discover ownership.
 */
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrGetContextV1)(
    void* exact_created_context, FfxNrContextTicketV1* out_ticket);
/* Begin closes new roots; never calls user callbacks or performs an infinite wait. */
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrBeginDrainV1)(
    const FfxNrDrainRequestV1*, FfxNrDrainHandleV1*, FfxNrDrainSummaryV1*);
/* Poll permits a new deadline for the SAME closed drain, never reopens the context. */
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrPollDrainV1)(
    const FfxNrDrainHandleV1*, int64_t absolute_deadline_qpc, FfxNrDrainSummaryV1*);
/* Returns an immutable snapshot revision. Host must collect all domain_count records.
 * Truncation/epoch mismatch cannot be interpreted as accepted complete evidence.
 */
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrReadDomainsV1)(
    const FfxNrDrainHandleV1*, uint64_t evidence_revision, uint32_t first,
    FfxNrDomainEvidenceV1* out_records, uint32_t capacity, uint32_t* written);
/* Consumes the exact Complete receipt; no new GPU work is allowed during destruction.
 * Actual wrapper/provider destruction integration owns final context invalidation.
 */
typedef FfxNrStatusV1 (FFX_NR_CALL *FfxNrConsumeForDestroyV1)(
    const FfxNrDrainHandleV1*, uint64_t complete_evidence_revision);
typedef struct FfxNrClosureServiceV1 {
    uint32_t size, version;
    FfxNrBuildInfoV1 build;
    FfxNrGetContextV1 get_context;
    FfxNrBeginDrainV1 begin;
    FfxNrPollDrainV1 poll;
    FfxNrReadDomainsV1 read_domains;
    FfxNrConsumeForDestroyV1 consume_for_destroy;
} FfxNrClosureServiceV1;
/* Proposed symbol. Caller supplies a zero-initialized, size/versioned table.
 * Query succeeds only for an actually implemented/qualified closure ABI.
 */
FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryClosureServiceV1(
    FfxNrClosureServiceV1* in_out_service);
#ifdef __cplusplus
}
#endif
#endif
