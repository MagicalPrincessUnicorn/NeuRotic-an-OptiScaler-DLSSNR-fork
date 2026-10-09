// Private source interface. Implementations and virtual dispatch execute in the creating SDK module.
// This grants no external FG algorithm release and is not an exported production capability.
#pragma once
#include "ffx_neurotic_checked_closure.h"
#include <d3d12.h>
#include <memory>
namespace ffx::nr {
inline constexpr uint32_t ControlledOutputEnrollment=1;
struct OwnedOutputSnapshot {
    FfxNrContextTicketV1 context{};
    ID3D12Resource* resource=nullptr;
    D3D12_RESOURCE_DESC description{};
    uint64_t allocationGeneration=0;
    ID3D12CommandQueue* queue=nullptr;
    ID3D12GraphicsCommandList* writerList=nullptr;
    uint64_t writerRecording=0;
    ID3D12GraphicsCommandList* consumerList=nullptr;
    uint64_t consumerRecording=0;
    bool writerActive=false,writerSubmitted=false,writerRetired=false,callbackActive=false,admissionOpen=false,actionActive=false,sdkComplete=false,failed=false;
    bool ordinaryEscapeExcluded=false,recyclingExcluded=false;
};
struct OwnedOutputRetirement { bool admissionClosed=false,actionsQuiescent=false,sdkComplete=false,failed=false; };
class OwnedOutputLease {
public:
    virtual ~OwnedOutputLease()=default;
    virtual OwnedOutputSnapshot inspect() const noexcept=0; // atomics only; safe under Resource lock
    virtual bool beginWriterAction(ID3D12GraphicsCommandList*,uint64_t recordingIncarnation) noexcept=0;
    virtual bool beginConsumerAction(const FfxNrDispatchTicketV1&) noexcept=0;
    virtual void endAction() noexcept=0;
    virtual void closeAdmission() noexcept=0;
    virtual OwnedOutputRetirement inspectRetirement() const noexcept=0;
    virtual bool inspectSubmission(FfxNrSubmitResultV1&) const noexcept=0;
};
struct OwnedWriteReceipt {
    HRESULT prepareResult=E_FAIL,callbackResult=E_FAIL,closeResult=E_FAIL,signalResult=E_FAIL;
    bool executeInvoked=false;
    ID3D12Fence* fence=nullptr;
    uint64_t target=0;
};
struct OwnedWriterRetirementReceipt {
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12CommandQueue* queue=nullptr;
    ID3D12Fence* fence=nullptr;
    uint64_t target=0,oldRecording=0,newRecording=0;
    HRESULT resetResult=E_FAIL,closeResult=E_FAIL;
    bool completed=false,oldRecordingRetired=false;
};
struct OwnedWriterCallbacks {
    // prepare installs recording hooks before SDK Reset; recording is still closed.
    HRESULT (*prepare)(void*,const std::shared_ptr<OwnedOutputLease>&,ID3D12GraphicsCommandList*)=nullptr;
    HRESULT (*write)(void*,const std::shared_ptr<OwnedOutputLease>&,ID3D12GraphicsCommandList*)=nullptr;
    HRESULT (*beginSubmit)(void*,const std::shared_ptr<OwnedOutputLease>&,void**)=nullptr;
    void (*endSubmit)(void*,void*,const OwnedWriteReceipt&)=nullptr;
};
}
