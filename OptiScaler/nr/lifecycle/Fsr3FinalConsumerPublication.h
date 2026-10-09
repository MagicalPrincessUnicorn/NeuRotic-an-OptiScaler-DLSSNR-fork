#pragma once
#include "Fsr3FinalConsumerAdapter.h"
#include "NativeSessionLifetime.h"

namespace Neurotic::Lifecycle
{
// A live borrow from the existing source/Resource owner. That owner retains the
// native resource and its immutable publication through this tail. In particular,
// ProviderReleaseObserved is an enrolled provider-specific fact, never inferred
// here from callback success, context destruction or GpuSafety completion.
class Fsr3FinalResourceGuard
{
  public:
    virtual ~Fsr3FinalResourceGuard()=default;
    virtual const C::ResourceView& CurrentView()const noexcept=0;
    virtual FinalFrameRuntimeProof Proof()const noexcept=0;
    virtual bool ProviderReleaseObserved()const noexcept=0;
    // Permanently closes actual source admissions for this canonical base. The
    // owner must retain the closed state when an earlier prefix is still busy.
    virtual bool ExcludePrimary(const C::FrameIdentity&)=0;
    virtual std::optional<NativeUnsealedRetirement> UnsealedTerminal()const{return {};}
};
class Fsr3FinalResourceOwner
{
  public:
    virtual ~Fsr3FinalResourceOwner()=default;
    virtual std::unique_ptr<Fsr3FinalResourceGuard> Acquire(ID3D12Resource*,ID3D12GraphicsCommandList*,
        const DlssNr::GpuSafety::Ticket&)=0;
};
struct Fsr3TerminalRecording
{
    static bool Terminal(const DlssNr::GpuSafety::Ticket& ticket)
    {return ticket&&DlssNr::GpuSafety::InspectTerminalRecording(ticket).has_value();}
};

// This is a metadata adapter, not a new finalizer or Resource authority. The
// complete immutable Reader and source borrow remain owned by the source port.
// Create refuses unknown/generated source frames and absent returned content.
template<class Reader,class Recording=Fsr3TerminalRecording>
class Fsr3FinalConsumerPublication final:public Fsr3FinalConsumerSource,
    public std::enable_shared_from_this<Fsr3FinalConsumerPublication<Reader,Recording>>
{
    using Self=Fsr3FinalConsumerPublication<Reader,Recording>;
    std::weak_ptr<NativeSessionLifetime> root_;
    PrimaryNrClaimHandle claim_;
    C::FrameIdentity frame_;
    FinalBoundaryObservation boundary_;
    C::ResourceView returned_;
    std::shared_ptr<const Reader> reader_;
    std::shared_ptr<Fsr3FinalResourceOwner> resourceOwner_;
    ID3D12Resource* resource_=nullptr;
    ID3D12GraphicsCommandList* list_=nullptr;
    DlssNr::GpuSafety::Ticket ticket_;
    std::unique_ptr<Fsr3FinalResourceGuard> dispatchGuard_;
    mutable std::recursive_mutex mutex_;
    bool valid_=false,offered_=false,returnedCall_=false,accepted_=false,interrupted_=false,retired_=false;
    std::optional<FinalPacketSnapshot> packet_;
    struct ReceiptPublication
    {
        C::FgHandoffReceipt receipt;
        C::BoundedList<C::RetentionRegistration,16> retentions;
        template<class T>Context::MetadataView<T> Resolve(const C::MetadataRef<T>& ref)const
        {
            if constexpr(std::is_same_v<T,C::BoundedList<C::RetentionRegistration,16>>)
                if(receipt.retentions.backing&&*receipt.retentions.backing==ref)return {ref,&retentions};
            return {};
        }
    };
    std::optional<ReceiptPublication> dispatchReceipt_,releaseReceipt_;
    struct Action final:NativeFinalConsumerAction
    {
        const C::FrameIdentity& frame;
        std::function<FinalizationResult(FinalRealFrameFinalizer&,NativeFinalConsumerEvents&)> operation;
        Action(const C::FrameIdentity& f,decltype(operation) op):frame(f),operation(std::move(op)){}
        const C::FrameIdentity& Frame()const noexcept override{return frame;}
        FinalizationResult Apply(FinalRealFrameFinalizer& f,NativeFinalConsumerEvents& e)override{return operation(f,e);}
    };
    template<class Operation>FinalizationResult Apply(Operation&& op)
    {
        const auto root=root_.lock();if(!root)return {};
        Action action(frame_,std::forward<Operation>(op));return root->ApplyFinalConsumer(claim_,action);
    }
    bool Current(const Fsr3FinalResourceGuard& guard)const
    {return guard.CurrentView()==returned_&&guard.Proof().lease&&guard.Proof().facts&&guard.Proof().dependency;}
    ReceiptPublication Receipt(const C::RecordHeader& header,bool released)const
    {
        ReceiptPublication p;p.receipt.header=header;
        if(dispatchReceipt_){p.receipt.header.record=dispatchReceipt_->receipt.header.record;p.receipt.header.revision=2;}
        p.receipt.provider=boundary_.provider;
        p.receipt.packet={C::Symbol{},packet_->packet.header.record,packet_->packet.header.revision};
        p.receipt.packet.recordType.Assign(C::FinalRealFramePacket::WireName);
        const C::EvidenceRef evidence{header.record};
        p.receipt.accepted=C::OptionalFact<bool>::FromKnown(accepted_,evidence);
        p.receipt.providerReleased=C::OptionalFact<bool>::FromKnown(released,evidence);
        p.retentions=packet_->retentions;
        for(auto& registration:p.retentions)registration.released=C::OptionalFact<bool>::FromKnown(released,evidence);
        p.receipt.retentions.count=static_cast<std::uint32_t>(p.retentions.Size());
        p.receipt.retentions.backing=C::MetadataRef<C::BoundedList<C::RetentionRegistration,16>>{
            C::OwnerDomain::FrameGeneration,p.receipt.header.record,p.receipt.header.revision,{}};
        return p;
    }
    Fsr3FinalConsumerPublication(const std::shared_ptr<NativeSessionLifetime>& root,PrimaryNrClaimHandle claim,
        C::FrameIdentity frame,FinalBoundaryObservation boundary,C::ResourceView returned,
        std::shared_ptr<const Reader> reader,std::shared_ptr<Fsr3FinalResourceOwner> owner,ID3D12Resource* resource)
        :root_(root),claim_(claim),frame_(std::move(frame)),boundary_(std::move(boundary)),returned_(std::move(returned)),
         reader_(std::move(reader)),resourceOwner_(std::move(owner)),resource_(resource){}
  public:
    static std::shared_ptr<Self> Create(const std::shared_ptr<NativeSessionLifetime>& root,PrimaryNrClaimHandle claim,
        const C::FrameIdentity& frame,const FinalBoundaryObservation& boundary,
        const C::MetadataRef<C::NativeOutputContentV1>& returnedPublication,
        std::shared_ptr<const Reader> reader,std::shared_ptr<Fsr3FinalResourceOwner> owner,ID3D12Resource* resource)
    {
        if(!root||!reader||!owner||!resource||!Context::ValidValues(frame)||!Context::Established(frame.baseRealFrameId)||
           frame.generatedFrameId.IsKnown()||frame.associationEvidence.Size()==0)return {};
        const auto* publishedFrame=Context::ResolveMetadata(boundary.frame,*reader);
        const auto* content=Context::ResolveMetadata(returnedPublication,*reader);
        const auto* view=content?Context::ResolveNativeOutput(*content,*reader):nullptr;
        const auto* output=Context::ResolveMetadata(boundary.output,*reader);
        if(!publishedFrame||*publishedFrame!=frame||!view||!output||*output!=*view)return {};
        auto result=std::shared_ptr<Self>(new Self(root,claim,frame,boundary,*view,std::move(reader),std::move(owner),resource));
        result->valid_=root->RetainFinalConsumer(claim,result);
        return result->valid_?result:nullptr;
    }
    const C::FrameIdentity& Frame()const noexcept override{return frame_;}
    bool Valid()const noexcept override{std::lock_guard lock(mutex_);return valid_&&!retired_&&!interrupted_;}
    bool Retired()const noexcept override{std::lock_guard lock(mutex_);return retired_;}
    bool Offer(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::Ticket& ticket)override
    {
        // Declare the borrow before the lock: every refusal/exception destroys
        // it after the publication lock has gone away.
        std::unique_ptr<Fsr3FinalResourceGuard> guard;
        std::lock_guard lock(mutex_);
        if(!valid_||offered_||interrupted_||ticket_||!ticket||!list)return false;
        // Retain the genuine recording even when subsequent validation fails.
        ticket_=ticket;list_=list;
        if(resource!=resource_)return false;
        guard=resourceOwner_->Acquire(resource_,list_,ticket_);
        if(!guard||!Current(*guard))return false;
        const auto result=Apply([&](FinalRealFrameFinalizer& finalizer,NativeFinalConsumerEvents& events)
        {
            auto r=finalizer.PrepareSeal(claim_,boundary_,guard->Proof(),*reader_,events.next());
            if(!r.accepted)return r;
            r=finalizer.CommitSeal(claim_,boundary_.generations,guard->Proof(),*reader_,events.next());
            if(!r.accepted)return r;
            packet_=finalizer.Snapshot(claim_);
            return finalizer.OfferToFg(claim_,guard->Proof(),events.next());
        });
        offered_=result.accepted&&packet_.has_value();
        if(offered_)dispatchGuard_=std::move(guard);
        return offered_;
    }
    bool DispatchCurrent()const noexcept override
    {
        std::lock_guard lock(mutex_);
        if(!offered_||returnedCall_||!dispatchGuard_||!Current(*dispatchGuard_))return false;
        const auto proof=dispatchGuard_->Proof();
        return ValidateForSubmit(*proof.lease,*proof.facts).allowed;
    }
    void DispatchReturned(bool accepted)override
    {
        std::unique_ptr<Fsr3FinalResourceGuard> released;
        std::lock_guard lock(mutex_);if(!offered_||returnedCall_)return;
        released=std::move(dispatchGuard_);
        returnedCall_=true;accepted_=accepted;
        Apply([&](FinalRealFrameFinalizer& finalizer,NativeFinalConsumerEvents& events)
        {
            dispatchReceipt_=Receipt(events.frameGeneration,false);
            auto result=finalizer.ObserveFgReceipt(packet_->packet.seal,dispatchReceipt_->receipt,*dispatchReceipt_,events.next());
            if(result.accepted&&!accepted_)result=finalizer.RejectFgConsumption(packet_->packet.seal,events.next());
            return result;
        });
    }
    void Interrupt()override
    {
        std::lock_guard lock(mutex_);if(retired_||interrupted_||packet_)return;
        interrupted_=true;
        Apply([&](FinalRealFrameFinalizer& finalizer,NativeFinalConsumerEvents& events)
        {return finalizer.InterruptUnsealed(claim_,events.next());});
    }
    void Advance()override
    {
        std::unique_ptr<Fsr3FinalResourceGuard> guard;
        std::lock_guard lock(mutex_);if(retired_||(!packet_&&!interrupted_))return;
        if(dispatchGuard_)return; // The synchronous provider call still owns its borrow.
        if(ticket_&&!Recording::Terminal(ticket_))return;
        guard=resourceOwner_->Acquire(resource_,list_,ticket_);
        if(!guard||!Current(*guard))return;
        if(packet_)
        {
            if(!returnedCall_||!dispatchReceipt_||!guard->ProviderReleaseObserved()||
               !CanOwnerReuse(*guard->Proof().lease,*guard->Proof().facts).allowed)return;
            // All captured provider registrations need exact Resource-owned
            // release evidence, in addition to the selected provider's release.
            for(const auto& expected:packet_->retentions)
            {
                std::size_t matches=0;
                for(const auto& hold:guard->Proof().facts->holds)
                    if(Context::Established(expected.registration)&&hold.registration==expected.registration.KnownPart()->value&&
                       hold.consumer==expected.consumer&&hold.provider==boundary_.provider&&
                       hold.handoff==C::GenerationToken{boundary_.handoff.Describe()}&&hold.kind==HoldKind::Provider&&
                       hold.released==Fact::Yes&&hold.releaseEvidence)++matches;
                if(matches!=1)return;
            }
        }
        else if(!guard->UnsealedTerminal())return;
        const auto observed=Apply([&](FinalRealFrameFinalizer& finalizer,NativeFinalConsumerEvents& events)
        {
            if(packet_)
            {
                if(!releaseReceipt_)releaseReceipt_=Receipt(events.frameGeneration,true);
                auto r=finalizer.ObserveFgReceipt(packet_->packet.seal,releaseReceipt_->receipt,*releaseReceipt_,events.next());
                if(!r.accepted)return r;
                return accepted_?r:finalizer.RejectFgConsumption(packet_->packet.seal,events.next());
            }
            return finalizer.InterruptUnsealed(claim_,events.next());
        });
        const auto root=root_.lock();
        // Keep the seal available while the existing Native history owner is
        // busy/refuses. Acknowledgment retry must precede irreversible retirement.
        if(!observed.accepted||!root||!root->FinalConsumerLogicalClosed(claim_))return;
        const auto result=Apply([&](FinalRealFrameFinalizer& finalizer,NativeFinalConsumerEvents& events)
        {
            if(packet_)
            {
                const auto r=finalizer.ObserveResourceRetirement(packet_->packet.seal,*guard->Proof().lease,*guard->Proof().facts,events.next());
                if(!r.accepted)return r;
            }
            if(!guard->ExcludePrimary(frame_))return FinalizationResult{};
            const auto excluded=C::OptionalFact<bool>::FromKnown(true,C::EvidenceRef{events.identity.record});
            if(packet_)return finalizer.RetireScope(packet_->packet.seal,events.identity,frame_,excluded,events.next());
            const auto proof=guard->Proof();
            return finalizer.RetireInterrupted(claim_,*guard->UnsealedTerminal(),events.identity,frame_,excluded,*reader_,events.next(),&proof);
        });
        retired_=result.retirement.has_value();
    }
};
}
