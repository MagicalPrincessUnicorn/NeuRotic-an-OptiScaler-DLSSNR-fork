#pragma once
#define NR_SPECTRE_SOURCE_TESTING
#include <nr/lifecycle/NativeSourceEnrollment.h>
#include "NativeAdversarialSample.h"
#include "../finalizer/finalizer_fixture.h"
namespace Neurotic::Lifecycle
{
class NativeSourceTestAccess
{
  public:template<class Feature>static auto Open(NativeOwnerSet& owners,DlssNr::NativeFeatureRegistry<Feature>& registry,
    const typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin& pin)
    {return std::unique_ptr<NativeSourceEnrollment<Feature>>(new NativeSourceEnrollment<Feature>(owners,registry,pin));}
};
}
namespace NativeAdversarialGroups
{
inline void LocalQualification(Report& r)
{
    auto f=std::make_unique<SampleFixture>();
    const auto prepared=P::PrepareNativeProfile(f->request,f->store);
    const auto built=P::BuildRecipe(f->request,f->store);
    const auto map=built.product?P::BuildNativeBindingMap(built.product->Value(),f->store):std::nullopt;
    const auto key=S::BuildPreparedContentKey(f->KeyFor(),f->source,f->target,f->frame,{},false,f->store);
    L::NativeHistoryState history(Key("local.history.owner"),Key("local.history"));
    const auto* sample=S::ResolveNativeSample(f->frame,f->store);
    const bool historyUse=sample&&history.Attempt(f->request.evaluation,false,{},*sample)&&
        history.ModelReturned(f->request.evaluation,{},true);
    r.Check("native_local_semantic_use_history",prepared.projection&&built.product&&map&&key&&historyUse&&
        !f->frame.baseRealFrameId.IsKnown()&&prepared.projection->Value().profileCertificate.nativeFreshness.has_value(),true,
        "N10 actual profile, recipe, exact resource mapping, content key and history attempt qualify a Native sample with unknown final association");
    history.Abandon(f->request.evaluation);
    auto final=std::make_unique<FinalizerTests::Fixture>();auto unassociated=f->frame;
    unassociated.sessionId=Known(final->session);final->Frame(unassociated);
    r.Check("local_qualification_not_final_association",history.Pending()&&!S::SameScope(f->frame,f->frame)&&
        !S::BuildPreparedContentKey(f->KeyFor(),f->source,f->target,f->frame,{},true,f->store)&&!final->Claim().accepted,true,
        "N10 the same locally qualified sample does not establish a real-frame pair or strict finalizer admission");
}

inline void FeatureReplacement(Report& r)
{
    L::NativeOwnerSet owners(Sym("adversarial.feature.scope"));DlssNr::NativeFeatureRegistry<unsigned> registry;
    registry.Set(7,11);auto pin=registry.Pin(7,registry.Read(7));
    auto source=L::NativeSourceTestAccess::Open(owners,registry,*pin);const auto old=source->Observe(*pin);
    NrContextTests::Store store;auto frame=NrContextTests::Frame();
    frame.sessionId=Known(old->session);frame.renderStreamId=Known(old->stream);frame.viewId=Known(old->view);
    frame.baseRealFrameId={};frame.nativeSample=store.Publish(*old,C::OwnerDomain::Provider);
    const auto oldRef=frame.nativeSample;registry.Set(7,11);
    auto replacement=registry.Pin(7,registry.Read(7));auto freshSource=L::NativeSourceTestAccess::Open(owners,registry,*replacement);
    const auto fresh=freshSource->Observe(*replacement);auto current=frame;
    current.sessionId=Known(fresh->session);current.renderStreamId=Known(fresh->stream);current.viewId=Known(fresh->view);
    current.nativeSample=store.Publish(*fresh,C::OwnerDomain::Provider);
    const bool oldRefPreserved=oldRef&&S::ResolveMetadata(*oldRef,store);
    r.Check("feature_reuse_old_sample_output_refused",old&&fresh&&oldRefPreserved&&!pin->Current()&&
        !source->Observe(*replacement)&&!S::SameObservationScope(frame,current,store)&&
        fresh->feature!=old->feature&&fresh->ingress!=old->ingress,true,
        "N07 real feature replacement with the same raw handle/value leaves old metadata readable but cannot import its sample/output scope");
    C::EvidenceVector oldOutputEvidence;oldOutputEvidence.nativeFreshness=S::NativeFreshness(*oldRef,*old);
    C::EvidenceVector newOutputEvidence;newOutputEvidence.nativeFreshness=S::NativeFreshness(*current.nativeSample,*fresh);
    r.Check("new_feature_exact_owner_bindings",S::SameObservationScope(current,current,store)&&
        !S::NativeEvidenceFresh(oldOutputEvidence,current,store)&&S::NativeEvidenceFresh(newOutputEvidence,current,store),true,
        "N07 newly issued feature/ingress and callback freshness qualify only the new owner publication");
}

inline void UntouchedRefusal(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),10);auto a=Access::Admit(f.root,owner);
    L::NativeHistoryState history(Key("untouched.history.owner"),Key("untouched.history"));unsigned calls=0,original=0,legacy=0;
    int callerOutput=37;const int before=callerOutput;P::NativeProtocolResult result;
    if(a)result=Invoke(*owner,*a,history,calls,false,false); // No C03: actual observer denies entry.
    if(P::NativeMayContinueOuterEvaluation(result.facts)){++original;callerOutput=before;}
    if(result.legacyAllowed)++legacy;
    r.Check("untouched_refusal_original_once",a&&calls==0&&original==1&&legacy==0&&callerOutput==before&&
        !result.facts.modelAttempted&&!result.facts.commandsRecorded&&!history.RequiresReset(),true,
        "O08 actual C03 refusal precedes all Native effects; exterior original-call shim runs exactly once with unchanged caller data");
    auto broken=result.facts;broken.outputRestoreFailed=true;
    if(P::NativeMayContinueOuterEvaluation(broken))++original;
    r.Check("unrestored_parameter_blocks_original",original==1&&!P::NativeMayContinueOuterEvaluation(broken),true,
        "O08 failed restoration refuses the exterior original-call boundary; no Boolean is used as delivery evidence");
}

inline void AdmissionCapacity(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    std::array<std::shared_ptr<SyntheticInvocation>,16> owners;
    std::array<std::optional<P::NativeInvocationAdmission>,16> admitted;
    bool all=true;for(std::size_t i=0;i<owners.size();++i)
    {owners[i]=std::make_shared<SyntheticInvocation>(f.root.get(),10+i);admitted[i]=Access::Admit(f.root,owners[i]);all=all&&bool(admitted[i]);}
    auto overflow=std::make_shared<SyntheticInvocation>(f.root.get(),100);auto refused=Access::Admit(f.root,overflow);
    unsigned calls=0;L::NativeHistoryState history(Key("capacity.history.owner"),Key("capacity.history"));
    P::NativeProtocolResult first;const bool ready=admitted[0]&&owners[0]->EstablishSyntheticLease();
    if(ready)first=Invoke(*owners[0],*admitted[0],history,calls,false,true,true);
    r.Check("full_table_no_preparation_or_effect",all&&!refused&&overflow->preparations==0&&ready&&calls==1&&
        first.facts.modelAttempted&&!first.evaluation&&history.Pending(),true,
        "O11 full pre-existing operation table refuses before owner preparation while a reserved slot retains an actual model/result-publication failure");
    bool others=true;for(std::size_t i=1;i<owners.size();++i)others=others&&admitted[i]&&admitted[i]->Product();
    r.Check("reserved_failure_preserves_older_slots",others&&first.reason.View()=="Native.OwnerFailure"&&
        !Access::Admit(f.root,overflow)&&overflow->preparations==0,true,
        "O11 required publication failure cannot evict an older admission or create a new effectful preparation opportunity");
}

inline void SharedClosure(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    auto native=std::make_shared<SyntheticInvocation>(f.root.get(),10);native->terminal=false;
    auto a=Access::Admit(f.root,native);const auto sample=a?a->Sample():std::nullopt;a.reset();
    auto final=std::make_unique<FinalizerTests::Fixture>();auto frame=*S::ResolveMetadata(final->request.recipe.frame,final->store);
    if(sample)frame.sessionId=Known(sample->session);final->Frame(frame);
    auto& shared=Access::Finalizer(*f.root);
    const auto event=[&]{return Access::Event(*f.root,final->request.generations);};
    const auto claim=shared.TryClaimPrimary(final->request,final->store,event());
    f.coordinator.Revoke(O::RevocationCause::Disabled);const auto held=f.root->AdvanceClosure();
    final->candidate.result.stage=C::OutcomeStage::NotAttempted;final->candidate.result.historyAdvancement=Known(C::HistoryAdvancement::NotAdvanced);
    final->candidate.result.composition=Known(C::CompositionStatus::NotAttempted);final->candidate.result.output={};
    final->candidate.acceptedFinalCandidate=Known(false);
    const bool submitted=claim.claim&&shared.SubmitEvaluationResult(*claim.claim,final->candidate,final->store,event()).accepted;
    L::ProvisionalReleaseProof proof;proof.owner=final->candidate.result.header;
    proof.notSubmitted=proof.nonReplayable=proof.notAdvanced=Known(true);
    const bool released=claim.claim&&shared.ReleaseUncommittedClaim(*claim.claim,proof,event()).accepted;
    r.Check("unrelated_final_client_progress_after_native_stop",sample&&claim.claim&&!held.closed&&!held.finalizerStopped&&submitted&&released,true,
        "L14 actual shared finalizer accepts an unrelated client's truthful unattempted result/release after Native closes new starts");
    native->terminal=true;const auto closed=f.root->AdvanceClosure();
    r.Check("shared_finalizer_stops_after_all_clients",closed.closed&&closed.finalizerStopped,true,
        "L14 finalizer stop and session close occur after the Native owner tail and unrelated final client have both retired");
}

inline void CallbackMetadataTail(Report& r)
{
    DlssNr::NativeFeatureRegistry<unsigned> registry;registry.Set(7,11);auto callback=registry.Pin(7,registry.Read(7));
    auto metadata=std::make_shared<L::OwnerMetadataArena>(Key("late.reader"),65536,4);
    auto publisher=metadata->ForOwner(C::OwnerDomain::Context);const auto ref=publisher.Publish(C::GenerationVector{});
    auto reader=metadata;std::weak_ptr<L::OwnerMetadataArena> weak=metadata;
    Fixture f;f.owner->retirement=[&](auto& facts){
        facts.callbacks=callback?O::Retirement::Outstanding:O::Retirement::Retired;
        facts.owners[static_cast<std::size_t>(O::InitialOwner::Metadata)]=weak.expired()?O::Retirement::Retired:O::Retirement::Outstanding;
    };
    auto physical=std::make_unique<FinalizerTests::Fixture>();physical->facts.dependency.completed=L::Fact::Yes;
    physical->facts.consumerTicketsRetired=physical->facts.ownerCanRecycle=L::Fact::Yes;
    physical->facts.dependency.reusable=L::Fact::Yes;L::CloseAdmission(*physical->lease);
    for(auto& hold:physical->facts.holds){hold.released=L::Fact::Yes;hold.releaseEvidence=Key("exact.release");}
    const bool gpuTerminal=L::CanOwnerReuse(*physical->lease,physical->facts).allowed;
    const bool readerValid=S::ResolveMetadata(ref,*reader)!=nullptr;
    auto cold=f.root;metadata.reset();const auto callbackHeld=cold->AdvanceClosure();
    r.Check("terminal_gpu_does_not_end_callback_reader",gpuTerminal&&readerValid&&!callbackHeld.closed&&!registry.BeginShutdown(),true,
        "L15 unit owner facts already permit C03 retirement, but real source callback and retained metadata reader still block aggregate closure");
    callback.reset();const auto readerHeld=cold->AdvanceClosure();reader.reset();const auto closed=cold->AdvanceClosure();
    auto shutdown=registry.BeginShutdown();if(shutdown)shutdown->Complete();
    r.Check("cold_root_waits_exact_cpu_tails",!readerHeld.closed&&closed.closed&&closed.finalizerStopped&&weak.expired()&&bool(shutdown),true,
        "L15 retained cold root closes only after both actual callback pin and metadata owner die; no GPU or provider effect is inferred");
}
inline void OrdinalIsNotAssociation(Report& r)
{
    auto f=std::make_unique<FinalizerTests::Fixture>();
    const auto binding=LifeFixture::Binding(C::OwnerDomain::IdentityRegistry,"adversarial.real.source");
    const auto render=f->host->BindRender(binding);
    const auto issued=render?render->NewBase({LifeFixture::Event(binding.owner,binding.subject,1),L::RenderObservation::NewGameContent}):L::Outcome<C::BaseRealFrameId>{};
    const auto rendered=render?render->Render(LifeFixture::Event(binding.owner,binding.subject,2)):L::Outcome<C::RenderId>{};
    auto frame=*S::ResolveMetadata(f->request.recipe.frame,f->store);
    L::AssociationAuthority authority;authority.binding=binding;
    authority.currentScope={frame.sessionId,frame.renderStreamId,frame.viewId,Known(Id<C::ObjectIncarnation>("topology",1))};
    L::AssociationClaim association;association.relation=L::AssociationRelation::RenderToBase;
    association.sourceScope=association.targetScope=authority.currentScope;
    association.ownerEvidence=LifeFixture::Event(binding.owner,binding.subject,3);
    if(issued.value.IsKnown())association.target=C::OptionalFact<C::StableIdentity>::FromKnown(
        issued.value.KnownPart()->value.Describe(),issued.value.KnownPart()->evidence);
    if(rendered.value.IsKnown())association.source=C::OptionalFact<C::StableIdentity>::FromKnown(
        rendered.value.KnownPart()->value.Describe(),rendered.value.KnownPart()->evidence);
    const auto associated=L::MakeAssociation(authority,association);
    if(associated.value.IsKnown())
    {
        const auto& projection=associated.value.KnownPart()->value.frame;
        frame.baseRealFrameId=projection.baseRealFrameId;frame.renderId=projection.renderId;
        frame.associationEvidence=projection.associationEvidence;
    }
    f->Frame(frame);
    const auto claim=f->Claim();const bool produced=claim.claim&&f->Produce(*claim.claim).accepted;
    const auto correct=f->boundary;
    // Deliberately copy the literal Native producer ordinal into a typed value.
    // That value is still not the identity issued by the enrolled real source.
    NativeTests::Fixture native(false,true);native.AsNativeSample(frame.baseRealFrameId.KnownPart()->value.value);
    const auto* sample=S::ResolveNativeSample(native.frame,native.store);
    auto counterfeit=frame;counterfeit.baseRealFrameId=Known(Id<C::BaseRealFrameId>("native.callback.ordinal",sample->producerOrdinal));
    f->boundary.frame=f->store.Publish(counterfeit,C::OwnerDomain::Presentation);
    const bool refused=claim.claim&&!f->Prepare(*claim.claim).accepted;f->boundary=correct;
    const bool prepared=claim.claim&&f->Prepare(*claim.claim).accepted;
    r.Check("native_ordinal_cannot_match_real_association",associated.value.IsKnown()&&issued.value.IsKnown()&&produced&&sample&&
        sample->producerOrdinal==frame.baseRealFrameId.KnownPart()->value.value&&refused&&prepared,true,
        "N09 equal numeric Native ordinal cannot replace the complete issuer-owned base association at the same finalizer boundary");
    auto unknown=frame;unknown.baseRealFrameId={};f->boundary.frame=f->store.Publish(unknown,C::OwnerDomain::Presentation);
    // A fresh owner is needed because successful Prepare intentionally caches the exact boundary.
    auto noBase=std::make_unique<FinalizerTests::Fixture>();unknown.sessionId=Known(noBase->session);noBase->Frame(unknown);
    r.Check("unknown_final_association_stays_unknown",!S::SameScope(unknown,unknown)&&!noBase->Claim().accepted,true,
        "N09 no local ordinal, dimensions or queue facts are supplied as missing strict final scope");
}
}
