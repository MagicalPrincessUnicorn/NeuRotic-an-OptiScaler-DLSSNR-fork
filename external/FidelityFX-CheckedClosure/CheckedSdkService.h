// Compiled into the creating SDK module, never the product adapter.
#pragma once
#include "CheckedSdkRuntime.h"
#include <cstring>
#include <cstdio>
namespace ffx::nr {
inline int64_t nowQpc() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }
inline FfxNrStatusV1 FFX_NR_CALL serviceGetContext(void* exact,FfxNrContextTicketV1* out) {
    if(!out) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(exact); if(!context) return FFX_NR_STALE_CONTEXT;
    *out=context->identity; return FFX_NR_COMPLETE;
}
inline FfxNrStatusV1 FFX_NR_CALL serviceEnroll(void* exact,const FfxNrEnrollRequestV1* request,FfxNrContextTicketV1* out) {
    if(!request || !out || request->size!=sizeof(*request) || request->version!=1 || (request->flags & ~ControlledOutputEnrollment) || request->reserved) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(exact); if(!context) return FFX_NR_STALE_CONTEXT;
    std::unique_lock lifecycle(context->lifecycleMutex,std::try_to_lock);
    if(!lifecycle) return FFX_NR_BUSY;
    if(context->destroying.load()) return FFX_NR_STALE_CONTEXT;
    if(context->quarantined.load()) return FFX_NR_API_FAILURE_RETAINED;
    if(context->provider!=request->expected_provider_id || !context->enrollOwner || !context->retainPin || !context->releasePin || context->rawListEscaped.load()) {
        std::fprintf(stderr,"W03_ENROLL_REFUSAL phase=service owner=%llu provider_match=%u callback=%u retain_pin=%u release_pin=%u raw_list_escaped=%u\n",
            static_cast<unsigned long long>(context->identity.owner_id),unsigned(context->provider==request->expected_provider_id),unsigned(bool(context->enrollOwner)),unsigned(bool(context->retainPin)),unsigned(bool(context->releasePin)),unsigned(context->rawListEscaped.load()));
        std::fflush(stderr);return FFX_NR_UNSUPPORTED;
    }
    if((request->flags&ControlledOutputEnrollment) && context->rawBufferEscaped.load()) {
        std::fprintf(stderr,"W03_ENROLL_REFUSAL phase=service owner=%llu raw_buffer_escaped=1\n",static_cast<unsigned long long>(context->identity.owner_id));std::fflush(stderr);return FFX_NR_UNSUPPORTED;
    }
    checkedModeRequested=true;
    if(context->enrolled.load()) { if(context->enrollmentFlags.load()!=request->flags) { std::fprintf(stderr,"W03_ENROLL_REFUSAL phase=service flags_changed=1\n");std::fflush(stderr);return FFX_NR_UNSUPPORTED; } *out=context->identity; return FFX_NR_COMPLETE; }
    bool expected=false; if(!context->enrolling.compare_exchange_strong(expected,true)) return FFX_NR_BUSY;
    if(context->enrolled.load()) { context->enrolling=false; *out=context->identity; return FFX_NR_COMPLETE; }
    if(context->legacyRoots.load()) { context->enrolling=false; return FFX_NR_BUSY; }
    context->enrollmentFlags=request->flags;
    context->retainPin(context->object); // lifecycle claim excludes wrapper Release before the first raw access
    const auto status=context->enrollOwner(context->object);
    if(status==FFX_NR_COMPLETE) { context->closure.sealInventory(); context->enrolled=true; *out=context->identity; }
    else if(!context->quarantined.load()) {
        // Enrollment has not admitted a root or submitted work: reclaim only its new observation objects.
        for(auto& handle:context->workerObservations) { if(handle) CloseHandle(handle); handle=nullptr; }
        for(unsigned i=0;i<context->queues.size();++i) {
            if(context->queueMarkers[i]) static_cast<ID3D12Fence*>(context->queueMarkers[i])->Release();
            if(context->queues[i]) static_cast<ID3D12CommandQueue*>(context->queues[i])->Release();
            context->queueMarkers[i]=nullptr; context->queues[i]=nullptr;
        }
        context->closure.discardUnenrolledInventory();
    }
    context->enrolling=false;
    if(status!=FFX_NR_COMPLETE && !context->quarantined.load()) context->releasePin(context->object);
    return status;
}
inline FfxNrStatusV1 serviceWriteOwnedOutput(const FfxNrContextTicketV1* ticket,const OwnedWriterCallbacks* callbacks,void* user,OwnedWriteReceipt* receipt) {
    if(!ticket || !callbacks || !receipt || !callbacks->prepare || !callbacks->write || !callbacks->beginSubmit || !callbacks->endSubmit) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(*ticket);if(!context || !context->enrolled.load() || context->quarantined.load()) return FFX_NR_STALE_CONTEXT;
    if(!(context->enrollmentFlags.load()&ControlledOutputEnrollment) || !context->writeOwnedOutput) return FFX_NR_UNSUPPORTED;
    WorkScope root(context->object);if(!root) return FFX_NR_BUSY;
    return context->writeOwnedOutput(context->object,*callbacks,user,*receipt);
}
inline FfxNrStatusV1 FFX_NR_CALL serviceCurrent(const FfxNrContextTicketV1* context,FfxNrDispatchTicketV1* out) {
    if(!context || !out || out->size!=sizeof(*out) || out->version!=1) return FFX_NR_INVALID_ARGUMENT;
    if(!currentDispatch || !activeContext || !activeContext->enrolled.load()) return FFX_NR_UNSUPPORTED;
    const auto id=activeContext->identity;
    if(id.owner_id!=context->owner_id || id.incarnation!=context->incarnation || id.registration_generation!=context->registration_generation) return FFX_NR_STALE_CONTEXT;
    *out=*currentDispatch; return FFX_NR_COMPLETE;
}
inline FfxNrStatusV1 serviceFinishWriter(const FfxNrContextTicketV1* ticket,OwnedWriterRetirementReceipt* receipt) {
    if(!ticket || !receipt)return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(*ticket);if(!context || !context->enrolled || !context->finishWriter)return FFX_NR_STALE_CONTEXT;
    WorkScope root(context->object);if(!root)return FFX_NR_BUSY;
    return context->finishWriter(context->object,*receipt);
}
inline FfxNrStatusV1 FFX_NR_CALL serviceValidate(const FfxNrDispatchTicketV1* ticket) {
    if(!ticket || ticket->size!=sizeof(*ticket) || ticket->version!=1) return FFX_NR_INVALID_ARGUMENT;
    if(!currentDispatch || !activeContext) return FFX_NR_STALE_CONTEXT;
    return std::memcmp(ticket,currentDispatch,sizeof(*ticket))==0 ? FFX_NR_COMPLETE:FFX_NR_STALE_CONTEXT;
}
inline FfxNrStatusV1 FFX_NR_CALL serviceObserver(const FfxNrContextTicketV1* ticket,const FfxNrSubmissionObserverV1* observer) {
    if(!ticket || !observer || observer->size!=sizeof(*observer) || observer->version!=1 || !observer->begin_submit || !observer->end_submit) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(*ticket); if(!context) return FFX_NR_STALE_CONTEXT;
    bool expected=false; if(!context->enrolling.compare_exchange_strong(expected,true)) return FFX_NR_BUSY;
    if(context->enrolled.load() || context->quarantined.load() || context->legacyRoots.load()) { context->enrolling=false; return FFX_NR_BUSY; }
    context->observer=*observer; context->enrolling=false; return FFX_NR_COMPLETE;
}
inline FfxNrStatusV1 FFX_NR_CALL servicePoll(const FfxNrDrainHandleV1* handle,int64_t deadline,FfxNrDrainSummaryV1* out) {
    if(!handle || !out || out->size!=sizeof(*out) || out->version!=1) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(handle->context); if(!context || !context->enrolled.load()) return FFX_NR_STALE_CONTEXT;
    const auto valid=context->closure.validate(*handle); if(valid!=FFX_NR_COMPLETE) return valid;
    if(activeContext==context.get()) return FFX_NR_REENTRANT;
    if(context->polling.exchange(true)) return FFX_NR_BUSY;
    struct PollGuard { std::atomic<bool>& flag; ~PollGuard(){flag=false;} } guard{context->polling};
    auto summarize=[&](FfxNrStatusV1 status) {
        const auto result=context->closure.summary(*out,status);
        if(result==FFX_NR_BUSY) return result;
        out->worker_count=2;
        for(auto worker:context->workerObservations) if(worker && WaitForSingleObject(worker,0)==WAIT_OBJECT_0) ++out->terminated_worker_count;
        out->coverage_flags=0; return result;
    };
    if(nowQpc()>=deadline) return summarize(FFX_NR_TIMEOUT_RETAINED);
    if(!context->closure.prefixFinished()) return summarize(FFX_NR_PENDING);
    if(!context->stopWorkers) return summarize(FFX_NR_INCOMPLETE_COVERAGE);
    const auto stopped=context->stopWorkers(context->object);
    if(stopped!=FFX_NR_COMPLETE) return summarize(stopped);
    appendTerminalMarkers(*context);
    const auto result=context->closure.poll(*handle,nowQpc(),deadline);
    return summarize(result);
}
inline FfxNrStatusV1 FFX_NR_CALL serviceBegin(const FfxNrDrainRequestV1* request,FfxNrDrainHandleV1* handle,FfxNrDrainSummaryV1* out) {
    if(!request || !handle || !out || out->size!=sizeof(*out) || out->version!=1 || request->size!=sizeof(*request) || request->version!=1 || request->mode!=1 || request->flags || !request->operation_nonce) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(request->context); if(!context || !context->enrolled.load()) return FFX_NR_STALE_CONTEXT;
    if(activeContext==context.get()) return FFX_NR_REENTRANT;
    if(context->algorithm) context->algorithm->closeAdmission();
    *handle=context->closure.begin(request->operation_nonce);
    if(!handle->drain_id) return FFX_NR_BUSY;
    return servicePoll(handle,request->absolute_deadline_qpc,out);
}
inline FfxNrStatusV1 FFX_NR_CALL serviceRead(const FfxNrDrainHandleV1* handle,uint64_t revision,uint32_t first,FfxNrDomainEvidenceV1* out,uint32_t capacity,uint32_t* written) {
    if(!handle || !written) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(handle->context); if(!context) return FFX_NR_STALE_CONTEXT;
    return context->closure.read(*handle,revision,first,out,capacity,*written);
}
inline FfxNrStatusV1 FFX_NR_CALL serviceConsume(const FfxNrDrainHandleV1* handle,uint64_t revision) {
    if(!handle) return FFX_NR_INVALID_ARGUMENT;
    auto context=createdRegistry().find(handle->context); if(!context) return FFX_NR_STALE_CONTEXT;
    if(!context->releasePin) return FFX_NR_INCOMPLETE_COVERAGE;
    if(context->algorithm && !context->algorithm->inspect().released) return FFX_NR_PENDING;
    const auto result=context->closure.consume(*handle,revision);
    if(result!=FFX_NR_COMPLETE) return result;
    if(context->ownedOutput) static_cast<OwnedOutputImplementation*>(context->ownedOutput.get())->sdkComplete();
    context->destroyReady=true;
    context->releasePin(context->object); return FFX_NR_COMPLETE;
}
}
