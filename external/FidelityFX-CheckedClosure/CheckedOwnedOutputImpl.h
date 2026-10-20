// Implementation is instantiated in the creating SDK TU; clients include only the abstract interface.
#pragma once
#include "CheckedSdkRuntime.h"
#include <cstring>
namespace ffx::nr {
class OwnedOutputImplementation;
inline thread_local OwnedOutputImplementation* currentOwnedWriter=nullptr;
#ifdef FFX_NR_TEST_ACTION_ADMISSION
inline void (*testActionAcquiring)(OwnedOutputLease*)=nullptr;
#endif
class OwnedOutputImplementation final : public OwnedOutputLease {
    std::shared_ptr<CreatedContext> anchor_;
    const FfxNrContextTicketV1 context_;
    ID3D12Resource* const resource_;
    const D3D12_RESOURCE_DESC description_;
    ID3D12CommandQueue* const queue_;
    const uint64_t generation_;
    inline static std::atomic<uint64_t> nextGeneration_{1};
    std::atomic<ID3D12GraphicsCommandList*> writerList_{nullptr},consumerList_{nullptr};
    std::atomic<uint64_t> writerRecording_{0},consumerRecording_{0};
    std::atomic<bool> writing_{false};
    std::atomic<bool> writerRetired_{false};
    FfxNrSubmitResultV1 submission_{};
    std::atomic<bool> submissionPublished_{false};
    std::atomic<bool> submitted_{false},callback_{false},open_{true},complete_{false},failed_{false};
    // 0 idle, 1 acquiring (token not yet published), 2 active, 3 ending.
    std::atomic<unsigned> action_{0};
    CheckedClosure::Token actionToken_;
public:
    OwnedOutputImplementation(std::shared_ptr<CreatedContext> owner,ID3D12Resource* resource,ID3D12CommandQueue* queue):
        anchor_(std::move(owner)),context_(anchor_->identity),resource_(resource),description_(resource->GetDesc()),queue_(queue),generation_(nextGeneration_.fetch_add(1)) { resource_->AddRef(); }
    ~OwnedOutputImplementation() override { resource_->Release(); }
    OwnedOutputSnapshot inspect() const noexcept override {
        OwnedOutputSnapshot s{};s.context=context_;s.resource=resource_;s.description=description_;s.allocationGeneration=generation_;s.queue=queue_;
        s.writerList=writerList_.load();s.writerRecording=writerRecording_.load();s.consumerList=consumerList_.load();s.consumerRecording=consumerRecording_.load();
        s.writerActive=writing_.load();s.writerSubmitted=submitted_.load();s.writerRetired=writerRetired_.load();s.callbackActive=callback_.load();s.admissionOpen=open_.load();s.actionActive=action_.load()!=0;s.sdkComplete=complete_.load();s.failed=failed_.load();
        s.ordinaryEscapeExcluded=true;s.recyclingExcluded=true;return s;
    }
    bool beginWriterAction(ID3D12GraphicsCommandList* list,uint64_t recording) noexcept override {
        if(currentOwnedWriter!=this || !activeContext || !activeToken || !writing_.load() || submitted_.load() || !open_.load() ||
           activeContext->ownedOutput.get()!=this || writerList_.load()!=list || !recording || writerRecording_.load()!=recording) return false;
        unsigned expected=0;if(!action_.compare_exchange_strong(expected,1)) return false;
#ifdef FFX_NR_TEST_ACTION_ADMISSION
        if(testActionAcquiring) testActionAcquiring(this);
#endif
        if(!open_.load()) {action_=0;return false;}
        actionToken_=activeToken->borrowList();if(!actionToken_) {action_=0;return false;}action_=2;return true;
    }
    void openWriter() noexcept {currentOwnedWriter=this;writing_=true;}
    void closeWriter() noexcept {writing_=false;currentOwnedWriter=nullptr;}
    bool beginConsumerAction(const FfxNrDispatchTicketV1& ticket) noexcept override {
        if(!currentDispatch || !activeToken || !activeContext || !open_.load() || !callback_.load() ||
           std::memcmp(&ticket,currentDispatch,sizeof(ticket)) || ticket.present_color!=reinterpret_cast<uintptr_t>(resource_) ||
           activeContext->ownedOutput.get()!=this || ticket.context.owner_id!=context_.owner_id) return false;
        unsigned expected=0;if(!action_.compare_exchange_strong(expected,1)) return false;
#ifdef FFX_NR_TEST_ACTION_ADMISSION
        if(testActionAcquiring) testActionAcquiring(this);
#endif
        if(!open_.load()) {action_=0;return false;}
        actionToken_=activeToken->borrowList();
        if(!actionToken_) { action_=0;return false; }action_=2;return true;
    }
    void endAction() noexcept override { unsigned expected=2;if(action_.compare_exchange_strong(expected,3)) {actionToken_={};action_=0;} }
    void closeAdmission() noexcept override {open_=false;}
    OwnedOutputRetirement inspectRetirement() const noexcept override {return {!open_.load(),action_.load()==0,complete_.load(),failed_.load()};}
    bool inspectSubmission(FfxNrSubmitResultV1& out)const noexcept override {if(!submissionPublished_.load())return false;out=submission_;return true;}
    void publishSubmission(const FfxNrSubmitResultV1& receipt)noexcept {if(submissionPublished_.load())return;submission_=receipt;submissionPublished_=true;}
    void writerRecording(ID3D12GraphicsCommandList* list,uint64_t recording) noexcept {writerList_=list;writerRecording_=recording;}
    void writerSubmitted(bool okay) noexcept {submitted_=okay;if(!okay) failed_=true;}
    void writerRetired() noexcept {writerRetired_=true;}
    bool enter(const FfxNrDispatchTicketV1& ticket) noexcept {
        if(!submitted_.load() || !open_.load() || failed_.load() || ticket.present_color!=reinterpret_cast<uintptr_t>(resource_)) return false;
        consumerList_=reinterpret_cast<ID3D12GraphicsCommandList*>(ticket.command_list);consumerRecording_=ticket.recording_incarnation;callback_=true;return true;
    }
    void leave() noexcept {callback_=false;open_=false;}
    void sdkComplete() noexcept {open_=false;complete_=true;anchor_.reset();}
};
inline bool enterOwnedConsumer(CreatedContext* owner,const FfxNrDispatchTicketV1& ticket) {
    if(!owner || !(owner->enrollmentFlags.load()&ControlledOutputEnrollment)) return true;
    return owner->ownedOutput && static_cast<OwnedOutputImplementation*>(owner->ownedOutput.get())->enter(ticket);
}
inline void exitOwnedConsumer(CreatedContext* owner) {
    if(owner && owner->ownedOutput) static_cast<OwnedOutputImplementation*>(owner->ownedOutput.get())->leave();
}
}
