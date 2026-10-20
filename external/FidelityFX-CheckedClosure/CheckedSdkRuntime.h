#pragma once
#include "CheckedSdkRegistry.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
using CheckedWaitCallback = int32_t(*)(wchar_t*,uint64_t);
bool waitForFenceValue(ID3D12Fence*,UINT64,DWORD,CheckedWaitCallback,const bool);

namespace ffx::nr {
inline thread_local CreatedContext* activeContext=nullptr;
inline thread_local CheckedClosure::Token* activeToken=nullptr;
inline thread_local FfxNrDispatchTicketV1* currentDispatch=nullptr;
inline thread_local ID3D12GraphicsCommandList* lastResetList=nullptr;
inline thread_local uint64_t lastResetIncarnation=0;
inline void recordingReset(ID3D12GraphicsCommandList* list) {
    if(activeContext) { lastResetList=list; lastResetIncarnation=activeContext->nextRecording.fetch_add(1); }
}
inline bool enterOwnedConsumer(CreatedContext*,const FfxNrDispatchTicketV1&);
inline void exitOwnedConsumer(CreatedContext*);
class DispatchScope final {
    CheckedClosure::Token callback_;
    FfxNrDispatchTicketV1 ticket_{};
    FfxNrDispatchTicketV1* previous_=nullptr;
public:
    DispatchScope(ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,ID3D12Resource* color,uint64_t frame) {
        if(!activeContext || !activeToken) return;
        callback_=activeToken->callback(); if(!callback_) return;
        ticket_.size=sizeof(ticket_); ticket_.version=1; ticket_.context=activeContext->identity;
        ticket_.dispatch_id=activeContext->nextDispatch.fetch_add(1);
        ticket_.registration_generation=activeContext->identity.registration_generation;
        if(lastResetList!=list || !lastResetIncarnation) { activeContext->closure.recordFailure(E_FAIL); return; }
        ticket_.recording_incarnation=lastResetIncarnation;
        ticket_.buffer_generation=1; ticket_.command_list=reinterpret_cast<uintptr_t>(list);
        ticket_.command_queue=reinterpret_cast<uintptr_t>(queue); ticket_.frame_id=frame;
        ticket_.present_color=reinterpret_cast<uintptr_t>(color);
        previous_=currentDispatch; currentDispatch=&ticket_;
        if(!enterOwnedConsumer(activeContext,ticket_)) activeContext->closure.recordFailure(E_FAIL);
    }
    ~DispatchScope() { if(callback_) { exitOwnedConsumer(activeContext); currentDispatch=previous_; } }
    const FfxNrDispatchTicketV1& ticket() const { return ticket_; }
};
inline thread_local std::shared_ptr<CreatedContext> workerContext;
inline thread_local void* workerExact=nullptr;
inline thread_local unsigned workerSlot=UINT32_MAX;
// Retain a registry entry at thread startup; worker loops never repeat registry lookups.
class WorkerScope final {
    std::shared_ptr<CreatedContext> previous_;
    void* previousExact_;
    unsigned previousSlot_;
public:
    explicit WorkerScope(void* exact,unsigned slot=UINT32_MAX):previous_(workerContext),previousExact_(workerExact),previousSlot_(workerSlot) {
        workerContext=createdRegistry().findForDestruction(exact); workerExact=exact; workerSlot=slot;
    }
    ~WorkerScope() { workerContext=std::move(previous_); workerExact=previousExact_; workerSlot=previousSlot_; }
};
inline std::shared_ptr<CreatedContext> workerOwner(void* exact) {
    return workerExact==exact && workerContext ? workerContext : createdRegistry().find(exact);
}
inline DWORD waitWorkerEvent(void* exact,HANDLE event) {
    auto context=workerOwner(exact);
    const bool bounded=(context && (context->boundedWorkers.load() || context->enrolled.load() || context->enrolling.load())) ||
        (registryLookupBusy && checkedModeRequested.load());
    if(context && context->boundedWorkers.load() && workerSlot<context->workerAcknowledged.size())
        context->workerAcknowledged[workerSlot]=context->workerWakeEpoch.load();
    const auto result=WaitForSingleObject(event,bounded?50:INFINITE);
    // Idle timeout is a periodic shutdown observation, not a failure.
    if(result==WAIT_FAILED && context && (context->enrolled.load() || context->boundedWorkers.load())) context->closure.recordFailure(HRESULT_FROM_WIN32(GetLastError()));
    return result;
}
inline HRESULT createCheckedEvent(bool initial,HANDLE& out,const wchar_t* name=nullptr) {
    out=CreateEventW(nullptr,FALSE,initial?TRUE:FALSE,name);
    return out ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
inline BOOL publishEvent(HANDLE event,CreatedContext* owner=nullptr) {
    const auto result=SetEvent(event);
    const auto error=result?S_OK:HRESULT_FROM_WIN32(GetLastError());
    if(!owner) owner=activeContext ? activeContext : workerContext.get();
    if(owner && (owner->enrolled.load() || owner->enrolling.load() || owner->boundedWorkers.load())) owner->closure.recordFailure(error);
    return result;
}
// Configure starts the interpolation thread; that thread creates the presenter.
// Their handles are published asynchronously. Waiting here observes startup,
// never retries configuration, enrollment side effects, or GPU work.
inline FfxNrStatusV1 waitWorkerPublication(CreatedContext& owner,DWORD budget=250) {
    const auto start=GetTickCount64();
    for(;;) {
        if(owner.quarantined.load() || owner.closure.failed())return FFX_NR_API_FAILURE_RETAINED;
        if(owner.workerHandles[0].load() && owner.workerHandles[1].load())return FFX_NR_COMPLETE;
        if(GetTickCount64()-start>=budget) {
            owner.closure.recordFailure(HRESULT_FROM_WIN32(WAIT_TIMEOUT));owner.quarantined=true;
            return FFX_NR_TIMEOUT_RETAINED;
        }
        Sleep(1);
    }
}
inline FfxNrStatusV1 acknowledgeWorkers(CreatedContext& owner,const HANDLE* events,DWORD budget=250) {
    const auto start=GetTickCount64();
    const auto epoch=owner.workerWakeEpoch.fetch_add(1)+1;
    owner.boundedWorkers=true; // sticky after the first enrollment attempt
    auto fail=[&](FfxNrStatusV1 status,HRESULT error) {
        owner.closure.recordFailure(error); owner.quarantined=true; return status;
    };
    if(!epoch) return fail(FFX_NR_API_FAILURE_RETAINED,E_UNEXPECTED);
    for(unsigned i=0;i<2;++i) if(!publishEvent(events[i],&owner)) return fail(FFX_NR_API_FAILURE_RETAINED,HRESULT_FROM_WIN32(GetLastError()));
    for(;;) {
        if(owner.closure.failed()) return fail(FFX_NR_API_FAILURE_RETAINED,E_FAIL);
        if(owner.workerAcknowledged[0].load()==epoch && owner.workerAcknowledged[1].load()==epoch) return FFX_NR_COMPLETE;
        if(GetTickCount64()-start>=budget) return fail(FFX_NR_TIMEOUT_RETAINED,HRESULT_FROM_WIN32(WAIT_TIMEOUT));
        Sleep(1);
    }
}
class WorkScope final {
    std::shared_ptr<CreatedContext> context_;
    CheckedClosure::Token token_;
    CreatedContext* previous_=nullptr;
    CheckedClosure::Token* previousToken_=nullptr;
    bool installed_=false,allowed_=true,legacy_=false;
public:
    explicit WorkScope(void* exact,int job=-1):context_(job>=0 ? workerOwner(exact) : createdRegistry().find(exact)) {
        if(activeContext && (!context_ || context_.get()!=activeContext)) { allowed_=false; return; }
        if(!context_) { if(registryLookupBusy && checkedModeRequested.load()) allowed_=false; return; }
        if(context_->quarantined.load() || (job<0 && context_->writerActive.load())) { allowed_=false; return; }
        if(!context_->enrolled.load()) {
            context_->legacyRoots.fetch_add(1); legacy_=true;
            if(!context_->enrolling.load() && !context_->enrolled.load()) return;
            context_->legacyRoots.fetch_sub(1); legacy_=false;
            if(context_->enrolling.load()) { allowed_=false; return; }
        }
        token_=job>=0 ? context_->take(unsigned(job)) :
            (activeContext==context_.get() && activeToken ? activeToken->child() : context_->closure.enterRoot());
        allowed_=bool(token_); if(!allowed_) return;
        previous_=activeContext; previousToken_=activeToken;
        activeContext=context_.get(); activeToken=&token_; installed_=true;
    }
    ~WorkScope() { if(installed_) { activeContext=previous_; activeToken=previousToken_; } if(legacy_) context_->legacyRoots.fetch_sub(1); }
    explicit operator bool() const { return allowed_; }
    CheckedClosure::Token detach() { return std::move(token_); }
};
inline bool selected(void* exact) { auto context=createdRegistry().find(exact); return context ? context->enrolled.load() : registryLookupBusy && checkedModeRequested.load(); }
inline bool workerStopping(void* exact,bool legacy) {
    auto context=workerOwner(exact);
    return context && context->enrolled.load() ? context->workerShutdownRequested.load() : legacy;
}
class PassiveScope final {
    std::shared_ptr<CreatedContext> retained_;
    CreatedContext* previous_;
public:
    explicit PassiveScope(void* exact):retained_(workerOwner(exact)),previous_(activeContext) {
        if(retained_ && retained_->enrolled.load()) activeContext=retained_.get();
    }
    ~PassiveScope() { activeContext=previous_; }
};
inline bool publishJob(unsigned slot) {
    return !activeContext || (activeToken && activeContext->publish(slot,activeToken->child()));
}
inline FenceObservation observeFence(void* pointer) {
    auto fence=static_cast<ID3D12Fence*>(pointer);
    const auto value=fence->GetCompletedValue();
    if(value==UINT64_MAX) return {value,int32_t(DXGI_ERROR_DEVICE_REMOVED)};
    ID3D12Device* device=nullptr; auto result=fence->GetDevice(IID_PPV_ARGS(&device));
    if(FAILED(result)) return {value,result};
    result=device->GetDeviceRemovedReason(); device->Release(); return {value,result};
}
inline FenceObservation observeWorker(void* handle) {
    const auto status=WaitForSingleObject(handle,0);
    return {status==WAIT_OBJECT_0?1ULL:0ULL,status==WAIT_FAILED?HRESULT_FROM_WIN32(GetLastError()):S_OK};
}
inline void noteWorker(void* exact,unsigned slot,HANDLE handle) {
    auto context=createdRegistry().find(exact); if(context && slot<context->workerHandles.size()) context->workerHandles[slot]=handle;
}
inline void recordFence(ID3D12CommandQueue* queue,ID3D12Fence* fence,uint64_t target,HRESULT result) {
    if(!activeContext) return;
    activeContext->closure.recordFailure(result);
    Domain domain{};
    domain.evidence.domain_id=reinterpret_cast<uintptr_t>(fence);
    domain.evidence.domain_generation=1;
    domain.evidence.fence_id=domain.evidence.domain_id; domain.evidence.fence_generation=1;
    domain.evidence.queue_id=reinterpret_cast<uintptr_t>(queue); domain.evidence.queue_generation=queue?1:0;
    domain.evidence.domain_kind=queue?1u:2u;
    domain.evidence.required_target=target; domain.evidence.last_attempted_target=target;
    domain.evidence.last_successful_target=SUCCEEDED(result)?target:0;
    domain.evidence.last_api_result=result; domain.observe=observeFence; domain.context=fence;
    activeContext->closure.updateDomain(domain);
}
inline HRESULT signal(ID3D12CommandQueue* queue,ID3D12Fence* fence,uint64_t value) {
    if(activeContext && value==UINT64_MAX) { activeContext->closure.recordFailure(E_INVALIDARG); return E_INVALIDARG; }
    const auto result=queue->Signal(fence,value); recordFence(queue,fence,value,result); return result;
}
inline FfxNrStatusV1 appendTerminalMarkers(CreatedContext& context) {
    if(context.markersPublished) return FFX_NR_COMPLETE;
    context.markersPublished=true;
    for(unsigned i=0;i<context.queues.size();++i) {
        if(!context.queues[i]) continue;
        auto queue=static_cast<ID3D12CommandQueue*>(context.queues[i]);
        auto fence=static_cast<ID3D12Fence*>(context.queueMarkers[i]);
        if(!fence) { context.closure.recordFailure(E_FAIL); return FFX_NR_API_FAILURE_RETAINED; }
        const auto result=queue->Signal(fence,1);
        context.closure.recordFailure(result);
        Domain domain{}; domain.context=fence; domain.observe=observeFence;
        domain.evidence.domain_id=reinterpret_cast<uintptr_t>(fence); domain.evidence.domain_generation=1;
        domain.evidence.fence_id=domain.evidence.domain_id; domain.evidence.fence_generation=1;
        domain.evidence.queue_id=reinterpret_cast<uintptr_t>(queue); domain.evidence.queue_generation=1;
        domain.evidence.domain_kind=4; domain.evidence.logical_role_bits=1u<<(16+i);
        domain.evidence.required_target=1; domain.evidence.last_attempted_target=1;
        domain.evidence.last_successful_target=SUCCEEDED(result)?1:0; domain.evidence.last_api_result=result;
        context.closure.updateDomain(domain);
    }
    return FFX_NR_COMPLETE;
}
inline HRESULT signal(ID3D12Fence* fence,uint64_t value) {
    if(activeContext && value==UINT64_MAX) { activeContext->closure.recordFailure(E_INVALIDARG); return E_INVALIDARG; }
    const auto result=fence->Signal(value); recordFence(nullptr,fence,value,result); return result;
}
inline HRESULT queueWait(ID3D12CommandQueue* queue,ID3D12Fence* fence,uint64_t value) {
    const auto result=queue->Wait(fence,value); if(activeContext) activeContext->closure.recordFailure(result); return result;
}
inline HRESULT queueWaitForPreviousUse(ID3D12CommandQueue* queue,ID3D12Fence* fence,uint64_t value) {
    // Only callers whose zero means "no previous use" may use this helper.
    // Required dependency waits continue through queueWait, including zero.
    if(value) return queueWait(queue,fence,value);
    HRESULT result=E_INVALIDARG;
    if(queue && fence) {
        result=observeFence(fence).removedReason;
        if(SUCCEEDED(result)) {
            ID3D12Device* device=nullptr;
            result=queue->GetDevice(IID_PPV_ARGS(&device));
            if(SUCCEEDED(result)) result=device?device->GetDeviceRemovedReason():E_FAIL;
            if(device) device->Release();
        }
    }
    // This no-op does not publish a signal, dependency, or completed domain.
    if(activeContext) activeContext->closure.recordFailure(result);
    return result;
}
inline HRESULT observeResult(HRESULT result) {
    if(activeContext) activeContext->closure.recordFailure(result);
    return result;
}
inline DWORD waitHandle(HANDLE handle,DWORD milliseconds) {
    const bool checked=activeContext || (registryLookupBusy && checkedModeRequested.load());
    const auto result=WaitForSingleObject(handle,checked && milliseconds==INFINITE?2000:milliseconds);
    if(activeContext && result!=WAIT_OBJECT_0) activeContext->closure.recordFailure(
        result==WAIT_FAILED?HRESULT_FROM_WIN32(GetLastError()):HRESULT_FROM_WIN32(WAIT_TIMEOUT));
    return result;
}
inline bool waitFence(ID3D12Fence* fence,uint64_t value,DWORD milliseconds=INFINITE,
                      CheckedWaitCallback callback=nullptr,bool eventWait=false) {
    if(!activeContext) return waitForFenceValue(fence,value,milliseconds,callback,eventWait);
    // Checked mode uses finite observation and never leaves an event registration outstanding.
    const auto limit=milliseconds==INFINITE ? 2000u : milliseconds;
    const auto start=GetTickCount64();
    do {
        const auto observation=observeFence(fence);
        if(observation.value==UINT64_MAX || observation.removedReason<0) {
            activeContext->closure.recordFailure(observation.removedReason); return false;
        }
        if(observation.value>=value) return true;
        if(GetTickCount64()-start>=limit) { activeContext->closure.recordFailure(HRESULT_FROM_WIN32(WAIT_TIMEOUT)); return false; }
        Sleep(1);
    } while(true);
}
}

#include "CheckedOwnedOutputImpl.h"
