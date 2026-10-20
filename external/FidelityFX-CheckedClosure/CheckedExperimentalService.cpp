#include "CheckedExperimentalService.h"
#include "CheckedAlgorithmState.h"
#include "CheckedSdkService.h"
namespace ffx::nr {
static FfxNrStatusV1 selectAlgorithm(void* exact,void* swapchain,const AlgorithmConsumerObserver* observer,std::shared_ptr<AlgorithmLease>* out) {
    if(!observer || !observer->begin_consumer || !observer->end_consumer || !out)return FFX_NR_INVALID_ARGUMENT;
    auto algorithm=algorithmRegistry().find(exact);auto owner=createdRegistry().find(swapchain);
    if(!algorithm || !owner)return FFX_NR_STALE_CONTEXT;
    std::unique_lock lock(owner->lifecycleMutex,std::try_to_lock);
    if(!lock)return FFX_NR_BUSY;
    if(owner->destroying || owner->enrolling || owner->enrolled || owner->legacyRoots || owner->algorithm || owner->rawBufferEscaped || owner->rawListEscaped || !owner->retainPin || !owner->releasePin)return FFX_NR_UNSUPPORTED;
    if(!algorithm->select(owner,*observer))return FFX_NR_UNSUPPORTED;
    owner->algorithm=algorithm;*out=algorithm;return FFX_NR_COMPLETE;
}
static FfxNrStatusV1 enrollAlgorithm(void* exact,const FfxNrEnrollRequestV1* request,FfxNrContextTicketV1* out) {
    if(!request || request->flags!=ControlledOutputEnrollment){std::fprintf(stderr,"W03_ENROLL_REFUSAL phase=experimental request_or_flags=1\n");std::fflush(stderr);return FFX_NR_UNSUPPORTED;}
    auto owner=createdRegistry().find(exact);if(!owner || !owner->algorithm){std::fprintf(stderr,"W03_ENROLL_REFUSAL phase=experimental context_found=%u algorithm_found=%u registry_busy=%u\n",unsigned(bool(owner)),unsigned(owner&&bool(owner->algorithm)),unsigned(registryLookupBusy));std::fflush(stderr);return FFX_NR_UNSUPPORTED;}
    return serviceEnroll(exact,request,out);
}
}
extern "C" FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryExperimentalServiceV1(ffx::nr::ExperimentalServiceV1* table) {
    using namespace ffx::nr;
    if(!table || table->size!=sizeof(*table) || table->version!=1 || table->manualOptIn!=ExperimentalManualOptInV1)return FFX_NR_UNSUPPORTED;
    ExperimentalServiceV1 out{};out.manualOptIn=ExperimentalManualOptInV1;
    out.get_context=serviceGetContext;out.select_algorithm=selectAlgorithm;out.set_submit_observer=serviceObserver;out.enroll=enrollAlgorithm;
    out.write_owned_output=serviceWriteOwnedOutput;out.current_dispatch=serviceCurrent;out.validate_dispatch=serviceValidate;
    out.finish_writer=serviceFinishWriter;
    out.begin=serviceBegin;out.poll=servicePoll;out.read_domains=serviceRead;out.consume=serviceConsume;*table=out;return FFX_NR_COMPLETE;
}
