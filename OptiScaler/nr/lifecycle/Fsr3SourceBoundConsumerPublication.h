#pragma once
#include "Fsr3SourceBoundConsumerAdapter.h"
#include "NativeSessionLifetime.h"
#include "NativeSourceTransactionObservation.h"

namespace Neurotic::Lifecycle
{
// Concrete publication into the existing source-bound claim. This owns no
// finalizer, frame identity or Resource authority. A C03 owner is mandatory.
template<class Reader,class ResourceOwner=SelectedFsr3ResourceOwner,class Session=NativeSessionLifetime>
class Fsr3SourceBoundConsumerPublication final:public Fsr3SourceBoundConsumerSource,
    public std::enable_shared_from_this<Fsr3SourceBoundConsumerPublication<Reader,ResourceOwner,Session>>
{
    using Self=Fsr3SourceBoundConsumerPublication;
    std::weak_ptr<Session> root_;
    SourceBoundClaimHandle claim_;
    std::shared_ptr<const Reader> reader_;
    std::shared_ptr<NativeSourceTransactionObservation> source_;
    std::shared_ptr<ResourceOwner> owner_;
    FinalBoundaryObservation boundary_;
    mutable std::recursive_mutex mutex_;
    std::unique_ptr<typename ResourceOwner::Guard> dispatch_;
    std::optional<SourceBoundConsumerAuthorization> authorization_;
    bool retained_=false,offered_=false,returned_=false,submission_=false,retired_=false;
    std::uint32_t lastOfferFailure_=0;
    struct Action final:NativeSourceBoundConsumerAction
    {
        const C::InterceptedSourceTransactionV1& subject;
        std::function<SourceBoundResult(FinalRealFrameFinalizer&,NativeFinalConsumerEvents&)> operation;
        Action(const C::InterceptedSourceTransactionV1& value,decltype(operation) op):subject(value),operation(std::move(op)){}
        const C::InterceptedSourceTransactionV1& Transaction()const noexcept override{return subject;}
        SourceBoundResult Apply(FinalRealFrameFinalizer& f,NativeFinalConsumerEvents& e)override{return operation(f,e);}
        using NativeSourceBoundConsumerAction::Authorize;
        using NativeSourceBoundConsumerAction::Current;
        using NativeSourceBoundConsumerAction::Dispatch;
        using NativeSourceBoundConsumerAction::Submission;
        using NativeSourceBoundConsumerAction::Release;
        using NativeSourceBoundConsumerAction::ResourceRetirement;
        using NativeSourceBoundConsumerAction::Retire;
    };
    template<class Operation>SourceBoundResult Apply(Operation&& operation)const
    {
        auto root=root_.lock();if(!root)return {};
        Action action(source_->Subject(),std::forward<Operation>(operation));
        return root->ApplySourceBoundConsumer(claim_,action);
    }
    Fsr3SourceBoundConsumerPublication(std::shared_ptr<Session> root,SourceBoundClaimHandle claim,
        std::shared_ptr<const Reader> reader,std::shared_ptr<NativeSourceTransactionObservation> source,
        std::shared_ptr<ResourceOwner> owner,FinalBoundaryObservation boundary):root_(root),claim_(claim),
        reader_(std::move(reader)),source_(std::move(source)),owner_(std::move(owner)),boundary_(std::move(boundary)){}
  public:
    static std::shared_ptr<Self> Create(std::shared_ptr<Session> root,SourceBoundClaimHandle claim,
        std::shared_ptr<const Reader> reader,std::shared_ptr<NativeSourceTransactionObservation> source,
        std::shared_ptr<ResourceOwner> owner,FinalBoundaryObservation boundary)
    {
        if(!root||!reader||!source||!owner)return {};
        const auto* frame=Context::ResolveMetadata(boundary.frame,*reader);
        const auto* sample=frame?Context::ResolveNativeSample(*frame,*reader):nullptr;
        const auto* output=Context::ResolveMetadata(boundary.output,*reader);
        if(!sample||*sample!=source->Subject().sample||!output||*output!=owner->Endpoint())return {};
        auto result=std::shared_ptr<Self>(new Self(root,claim,std::move(reader),std::move(source),std::move(owner),std::move(boundary)));
        result->retained_=root->RetainSourceBoundConsumer(claim,result);
        return result->retained_?result:nullptr;
    }
    const C::InterceptedSourceTransactionV1& Transaction()const noexcept override{return source_->Subject();}
    bool Retired()const noexcept override{std::lock_guard lock(mutex_);return retired_;}
    // Diagnostic only: preserve the first refusal without repeating owner actions.
    std::uint32_t LastOfferFailure()const noexcept{std::lock_guard lock(mutex_);return lastOfferFailure_;}
    bool Offer(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::Ticket& ticket,
        const SelectedFsr3DispatchAdmission& sdk)override
    {
        std::unique_ptr<typename ResourceOwner::Guard> guard;
        std::lock_guard lock(mutex_);
        if(!retained_||offered_||returned_||retired_){if(!lastOfferFailure_)lastOfferFailure_=201;return false;}
        offered_=true; // no retry after a potentially effectful callback offer
        std::uint32_t diagnostic=0;
        if constexpr(requires{owner_->AcquireSourceBound(resource,list,ticket,sdk,&diagnostic);})
            guard=owner_->AcquireSourceBound(resource,list,ticket,sdk,&diagnostic);
        else guard=owner_->AcquireSourceBound(resource,list,ticket,sdk);
        if(!guard){if(!lastOfferFailure_)lastOfferFailure_=diagnostic?diagnostic:100;return false;}
        auto result=Apply([&](auto& finalizer,auto& events){
            return Action::Authorize(finalizer,claim_,boundary_,guard->Proof(),*reader_,events.next());});
        if(!result.accepted||!result.authorization){if(!lastOfferFailure_)lastOfferFailure_=1000+static_cast<std::uint32_t>(result.reason);return false;}
        authorization_=result.authorization;dispatch_=std::move(guard);return true;
    }
    bool DispatchCurrent()const noexcept override
    try
    {
        std::lock_guard lock(mutex_);
        if(returned_||retired_||!authorization_||!dispatch_)return false;
        return Apply([&](auto& finalizer,auto&){SourceBoundResult result;
            result.accepted=Action::Current(finalizer,*authorization_,dispatch_->Proof());return result;}).accepted;
    }
    catch(...){return false;}
    void DispatchReturned(SourceBoundDispatchEffect effect,std::optional<std::int32_t> actualResult)override
    {
        std::unique_ptr<typename ResourceOwner::Guard> released;
        std::lock_guard lock(mutex_);if(returned_||!offered_)return;
        returned_=true;released=std::move(dispatch_);
        const auto* queue=owner_->QueueIdentity();if(!queue)return;
        SourceBoundDispatchOutcome observed;observed.call=owner_->DispatchCall();
        observed.recording=owner_->ConsumerRecording();observed.queue=*queue;
        observed.effect=effect;observed.actualResult=actualResult;
        Apply([&](auto& f,auto& e){return Action::Dispatch(f,claim_,observed,e.next());});
    }
    void Advance()override
    {
        std::unique_ptr<typename ResourceOwner::Guard> guard;
        std::lock_guard lock(mutex_);if(retired_||!returned_||dispatch_)return;
        if(!submission_)
        {
            // This is read from the retained actual SDK submission owner; the
            // publication has no API accepting caller-supplied success flags.
            const auto observed=owner_->ObserveSubmission();if(!observed)return;
            const auto result=Apply([&](auto& f,auto& e){return Action::Submission(f,claim_,*observed,e.next());});
            if(!result.accepted)return;
            submission_=true;owner_->CloseAdmission();
        }
        guard=owner_->AcquireRetirement();if(!guard)return;
        const auto proof=guard->Proof();if(!proof.lease||!proof.facts)return;
        auto result=Apply([&](auto& f,auto& e){
            auto released=Action::Release(f,claim_,*proof.lease,*proof.facts,e.next());
            if(!released.accepted)return released;
            return Action::ResourceRetirement(f,claim_,*proof.lease,*proof.facts,e.next());});
        if(!result.accepted)return;
        result=Apply([&](auto& f,auto& e){
            const auto excluded=source_->ExcludeTransaction();
            return Action::Retire(f,claim_,e.identity,excluded,
                C::OptionalFact<bool>::FromKnown(true,C::EvidenceRef{e.identity.record}),e.next());});
        retired_=result.terminal.has_value();
    }
};
}
