#define NR_SOURCE_BOUND_CONSUMER_TESTING 1
#define NR_SOURCE_TRANSACTION_TESTING 1
#include <nr/lifecycle/NativeSourceTransactionObservation.h>
#include <nr/lifecycle/SourceBoundConsumerTypes.h>
#include "finalizer_fixture.h"
using namespace NrTest002;
namespace X=Neurotic::Context;
namespace Neurotic::Lifecycle
{
struct SourceTransactionTestAccess
{
    static auto Observe(const C::InterceptedSourceTransactionV1& t)
    {return std::shared_ptr<NativeSourceTransactionObservation>(new NativeSourceTransactionObservation(t,{}));}
    static SourceTransactionExclusion Exclude(const C::InterceptedSourceTransactionV1& t)
    {NativeSourceTransactionObservation source(t,{});return source.ExcludeTransaction();}
};
struct SourceBoundConsumerTestAccess
{
    template<class R>static auto Reserve(FinalRealFrameFinalizer& f,const SourceBoundPrimaryRequest& r,const R& reader,const FinalizerEvent& e)
    {return f.ReserveSourceBoundPrimary(r,reader,e);}
    template<class R>static auto Candidate(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const FinalCandidateSubmission& c,
        const C::NativeStageDeliveryV1& s,const R& r,const FinalizerEvent& e){return f.SubmitSourceBoundCandidate(h,c,s,r,e);}
    template<class R>static auto Authorize(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const FinalBoundaryObservation& b,
        const FinalFrameRuntimeProof& p,const R& r,const FinalizerEvent& e){return f.AuthorizeSourceBoundConsumer(h,b,p,r,e);}
    static auto Close(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const FinalizerEvent& e){return f.CloseSourceBoundAdmission(h,e);}
    static auto Fail(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const FinalizerEvent& e){return f.FailSourceBound(h,e);}
    static auto Current(FinalRealFrameFinalizer& f,const SourceBoundConsumerAuthorization& a,const FinalFrameRuntimeProof& p)
    {return f.SourceBoundAuthorizationCurrent(a,p);}
    static auto Dispatch(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const SourceBoundDispatchOutcome& o,const FinalizerEvent& e)
    {return f.ObserveSourceBoundDispatch(h,o,e);}
    static auto Submit(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const SourceBoundSubmissionOutcome& o,const FinalizerEvent& e)
    {return f.ObserveSourceBoundSubmission(h,o,e);}
    static auto Release(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const LiveConsumptionLease& l,const OwnerFacts& o,const FinalizerEvent& e)
    {return f.ObserveSourceBoundProviderRelease(h,l,o,e);}
    static auto ResourceRetire(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const LiveConsumptionLease& l,const OwnerFacts& o,const FinalizerEvent& e)
    {return f.ObserveSourceBoundResourceRetirement(h,l,o,e);}
    static auto Logical(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const FinalizerEvent& e)
    {return f.AcknowledgeSourceBoundLogicalClosed(h,e);}
    static auto Retire(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,const C::RecordHeader& owner,
        const C::InterceptedSourceTransactionV1& t,const C::OptionalFact<bool>& excluded,const FinalizerEvent& e)
    {return f.RetireSourceBound(h,owner,SourceTransactionTestAccess::Exclude(t),excluded,e);}
};
}
using Access=L::SourceBoundConsumerTestAccess;
struct SourceFixture:FinalizerTests::Fixture
{
    L::SourceBoundPrimaryRequest source;
    C::InterceptedSourceTransactionV1 transaction;
    C::NativeStageDeliveryV1 stage;
    C::SourceBoundConsumerContractV1 contract;
    C::RecordKey consumerRecording=Key("sdk.recording");
    L::FinalFrameRuntimeProof Proof()const{return {&*lease,&facts,&dependency,&consumerRecording};}
    SourceFixture(std::size_t capacity=8,std::uint64_t callback=1,std::uint64_t transactionId=1):Fixture(capacity)
    {
        auto frame=*store.Resolve(request.recipe.frame).value;
        frame.baseRealFrameId={};frame.associationEvidence={};
        C::NativeSampleIdentityV1 sample;sample.session=session;sample.stream=frame.renderStreamId.KnownPart()->value;
        sample.view=frame.viewId.KnownPart()->value;sample.feature=Id<C::ObjectIncarnation>("source.feature",1);
        sample.ingress=Id<C::ObjectIncarnation>("source.ingress",1);sample.producerOrdinal=1;sample.callback=Key("source.callback",callback);
        frame.nativeSample=store.Publish(sample,C::OwnerDomain::Provider);Frame(frame);
        request.recipe.nativeSample=frame.nativeSample;
        C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::FinalConsumerRequired;
        delivery.profile=request.recipe.profile;delivery.placement=request.recipe.placement;
        delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
        delivery.finalConsumer=request.expectedConsumer;request.recipe.nativeDelivery=store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
        transaction.id=Id<C::InterceptedSourceTransactionIdV1>("source.transaction",transactionId);transaction.sample=sample;
        transaction.episode=frame.episodeId.KnownPart()->value;transaction.device=Known(Id<C::ObjectIncarnation>("device",1));
        source.transaction=store.Publish(transaction,C::OwnerDomain::IdentityRegistry);
        source.recipe=request.recipe;source.plan=request.plan;source.generations=request.generations;
        contract.profile=request.recipe.profile;contract.consumer=*request.expectedConsumer;
        C::QualificationCertificate certificate;certificate.header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Strategy,81);
        certificate.context=request.recipe.context;certificate.profile=contract.profile;certificate.purpose=Sym("RENDER.NativeBindings");
        certificate.eligibility=C::Eligibility::Eligible;certificate.nativeFreshness=X::NativeFreshness(*frame.nativeSample,sample);
        C::StructuralSignature signature;signature.schema=Sym("RENDER.NativeBindings");signature.version=1;
        signature.generations=request.generations;certificate.signature=store.Publish(signature,C::OwnerDomain::Strategy);
        contract.profileQualification=store.Publish(certificate,C::OwnerDomain::Strategy);
        certificate.header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::FrameGeneration,82);
        certificate.purpose=Sym(C::SourceBoundConsumerContractV1::WireName);
        contract.consumerQualification=store.Publish(certificate,C::OwnerDomain::FrameGeneration);
        source.contract=store.Publish(contract,C::OwnerDomain::FrameGeneration);
        boundary.owner.owner=C::OwnerDomain::FrameGeneration;
        auto output=*store.Resolve(boundary.output).value;
        output.descriptor.mipLevels=Known(std::uint32_t{1});output.descriptor.arrayLayers=Known(std::uint32_t{1});
        output.mip=Known(std::uint32_t{0});output.arrayLayer=Known(std::uint32_t{0});output.plane=Known(std::uint32_t{0});
        output.adapter=Known(Sym("adapter"));output.raster.allocation=Known(C::Extent{1920,1080});
        output.raster.active=Known(C::Rectangle{0,0,1920,1080});output.raster.origin=Known(C::RasterOrigin::TopLeft);
        output.raster.axisSigns=Known(C::Vec2{1,1});
        boundary.output=store.Publish(output,C::OwnerDomain::Resource);candidate.result.output=Known(boundary.output);
        C::NativeOutputContentV1 content;content.view=boundary.output;content.recording=Key("recording");content.producerOrdinal=2;
        content.semanticDomain=Sym("SceneLinear");
        stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage"),1};
        stage.plan=source.recipe.committedPlan;stage.route=source.plan.routeGeneration.KnownPart()->value;
        stage.delivery=*source.recipe.nativeDelivery;stage.recipe={Sym(C::NrExecutionRecipe::WireName),source.recipe.header.record,source.recipe.header.revision};
        stage.executionResult={Sym(C::EvaluationResult::WireName),candidate.result.header.record,candidate.result.header.revision};
        stage.sample=*source.recipe.nativeSample;stage.evaluation=source.recipe.evaluation;stage.admissionEpoch=Key("admission");
        stage.placement=source.recipe.placement;stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("binding"),1};
        stage.nativeOutput=store.Publish(content,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
        stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=sample.ingress;stage.lastRecording=content.recording;
        stage.lastRecordedOrdinal=2;stage.outcome=C::NativeStageOutcome::HostReturnRecorded;stage.reason=Sym("Native.HostReturnRecorded");
        C::BoundedList<C::OwnerValueReference,16> causes;causes.Push({C::OwnerDomain::Provider,Sym("ReturnOperation"),{1,0},Key("operation"),1});
        stage.causes.count=1;stage.causes.backing=store.Publish(causes,C::OwnerDomain::Provider);
        PublishStage();
    }
    void PublishStage(){auto ref=store.Publish(stage,C::OwnerDomain::Provider);stage.publication.record=ref.record;stage.publication.revision=++ref.revision;store.Replace(ref,stage);}
    void AssociateEvaluation()
    {
        const auto evaluationOwner=LifeFixture::Binding(C::OwnerDomain::Strategy,"nr.evaluation");
        auto issuer=host->BindEvaluation(evaluationOwner);if(!issuer)throw std::runtime_error("evaluation issuer");
        source.evaluationAssociation=issuer->NativeEvaluation(transaction.sample,transaction.episode,
            LifeFixture::Event(evaluationOwner.owner,evaluationOwner.subject,1));
        if(!source.evaluationAssociation)throw std::runtime_error("evaluation association");
        auto frame=*store.Resolve(source.recipe.frame).value;frame.evaluationId={};
        source.recipe.frame=store.Publish(frame);
        source.recipe.evaluation=source.evaluationAssociation->Evaluation();
        candidate.result.originalLineage=source.recipe.frame;candidate.result.evaluation=source.recipe.evaluation;
        boundary.frame=store.Publish(frame,C::OwnerDomain::Presentation);
        stage.evaluation=source.recipe.evaluation;PublishStage();
    }
    auto Reserve(){return Access::Reserve(*finalizer,source,store,Event());}
    auto Bind(const L::SourceBoundClaimHandle& h){return Access::Candidate(*finalizer,h,candidate,stage,store,Event());}
    auto Authorize(const L::SourceBoundClaimHandle& h){return Access::Authorize(*finalizer,h,boundary,Proof(),store,Event());}
};
void Run(Report& r)
{
    static_assert(!std::is_convertible_v<L::SourceBoundClaimHandle,L::PrimaryNrClaimHandle>);
    static_assert(!std::is_convertible_v<L::SourceBoundConsumerAuthorization,C::FinalRealFramePacket>);
    std::string prefix;
    auto check=[&](const char* id,bool result){r.Check(prefix+id,result,true,"separate finalizer contract with scripted existing CPU owners");};
    static_assert(!std::is_default_constructible_v<L::NativeEvaluationAssociation>);
    static_assert(!std::is_constructible_v<L::NativeEvaluationAssociation,C::NativeSampleIdentityV1,C::EpisodeId,C::EvaluationId>);
    {
        auto issuance=std::make_unique<SourceFixture>();
        const auto binding=LifeFixture::Binding(C::OwnerDomain::Strategy,"association.issuer");
        auto issuer=issuance->host->BindEvaluation(binding);
        const auto event=LifeFixture::Event(binding.owner,binding.subject,1);
        auto otherSession=issuance->transaction.sample;++otherSession.session.value;
        check("association-wrong-session-not-issued",!issuer->NativeEvaluation(otherSession,issuance->transaction.episode,event));
        auto issued=issuer->NativeEvaluation(issuance->transaction.sample,issuance->transaction.episode,event);
        check("association-issued-after-scope-refusal",issued.has_value());
        check("association-event-cannot-reissue",!issuer->NativeEvaluation(issuance->transaction.sample,issuance->transaction.episode,event));
        auto foreignEvent=LifeFixture::Event(C::OwnerDomain::FrameGeneration,binding.subject,2);
        check("association-foreign-owner-not-issued",!issuer->NativeEvaluation(issuance->transaction.sample,issuance->transaction.episode,foreignEvent));
    }
    auto associated=std::make_unique<SourceFixture>();associated->AssociateEvaluation();
    auto associatedClaim=associated->Reserve();
    check("owner-associated-unknown-upstream",associatedClaim.accepted);
    check("owner-associated-repeat-idempotent",associated->Reserve().claim==associatedClaim.claim);
    auto association=associated->source.evaluationAssociation;
    associated->source.evaluationAssociation.reset();check("repeat-cannot-drop-association",!associated->Reserve().accepted);
    associated->source.evaluationAssociation=association;
    auto repeatedTransaction=associated->transaction;++repeatedTransaction.id.value;
    associated->source.transaction=associated->store.Publish(repeatedTransaction,C::OwnerDomain::IdentityRegistry);
    check("same-sample-different-transaction-refused",associated->Reserve().reason==L::FinalizationReason::CompetingPrimary);
    auto republished=std::make_unique<SourceFixture>(8,2,2);republished->AssociateEvaluation();republished->serial=100;
    check("same-sample-new-callback-transaction-refused",
        Access::Reserve(*associated->finalizer,republished->source,republished->store,republished->Event()).reason==L::FinalizationReason::CompetingPrimary);
    auto unbound=std::make_unique<SourceFixture>();unbound->AssociateEvaluation();
    unbound->source.evaluationAssociation.reset();check("unknown-upstream-needs-owner-association",!unbound->Reserve().accepted);
    auto conflict=std::make_unique<SourceFixture>();conflict->AssociateEvaluation();
    auto conflictFrame=*conflict->store.Resolve(conflict->source.recipe.frame).value;
    conflictFrame.evaluationId=Known(Id<C::EvaluationId>("foreign.evaluation",1));
    conflict->source.recipe.frame=conflict->store.Publish(conflictFrame);
    check("known-conflicting-upstream-refused",!conflict->Reserve().accepted);
    auto wrongEvaluation=std::make_unique<SourceFixture>();wrongEvaluation->AssociateEvaluation();
    ++wrongEvaluation->source.recipe.evaluation.value;
    check("association-recipe-mismatch-refused",!wrongEvaluation->Reserve().accepted);
    auto wrongSample=std::make_unique<SourceFixture>();wrongSample->AssociateEvaluation();
    wrongSample->source.evaluationAssociation=association;
    ++wrongSample->transaction.sample.producerOrdinal;
    wrongSample->source.transaction=wrongSample->store.Publish(wrongSample->transaction,C::OwnerDomain::IdentityRegistry);
    check("association-other-sample-refused",!wrongSample->Reserve().accepted);
    auto f=std::make_unique<SourceFixture>();auto claim=f->Reserve();check("reserve-unknown-original",claim.accepted&&claim.newPrimaryClaim&&claim.claim.has_value());
    if(!claim.claim)throw std::runtime_error("source reservation fixture");
    auto duplicate=f->Reserve();check("same-primary-reservation",duplicate.accepted&&!duplicate.newPrimaryClaim&&duplicate.claim==claim.claim);
    check("strict-still-refuses",!f->Claim().accepted);
    check("no-authorization-before-candidate",!f->Authorize(*claim.claim).accepted);
    auto bound=f->Bind(*claim.claim);check("actual-return-candidate-bound",bound.accepted&&!bound.authorization);
    auto authorized=f->Authorize(*claim.claim);check("pre-consumption-authorization",authorized.accepted&&authorized.authorization.has_value());
    check("authorization-closes-admission",authorized.admissionsClosed);
    check("not-consumer-recorded",authorized.phase==L::SourceBoundPhase::ConsumerAuthorized);
    if(!authorized.authorization)throw std::runtime_error("source authorization fixture");
    check("runtime-authority-current",Access::Current(*f->finalizer,*authorized.authorization,f->Proof()));
    f->facts.dependency.completed=L::Fact::Yes;
    check("gpu-complete-not-promoted",Access::Close(*f->finalizer,*claim.claim,f->Event()).phase==L::SourceBoundPhase::ConsumerAuthorized);
    f->facts.current.description.revoked=Known(true);check("revocation-refuses-action",!Access::Current(*f->finalizer,*authorized.authorization,f->Proof()));
    f->facts.current.description.revoked=Known(false);
    const auto holds=f->facts.holds;auto replacement=*holds.Get(0);replacement.registration=Key("replacement.hold");
    f->facts.holds={};f->facts.holds.Push(replacement);
    check("retention-registration-frozen",!Access::Current(*f->finalizer,*authorized.authorization,f->Proof()));f->facts.holds=holds;
    auto closed=Access::Close(*f->finalizer,*claim.claim,f->Event());check("closure-recorded",closed.accepted&&closed.admissionsClosed);
    check("closure-refuses-new-authorization",!f->Authorize(*claim.claim).accepted);
    check("existing-authorized-lease-may-drain",Access::Current(*f->finalizer,*authorized.authorization,f->Proof()));
    auto failed=Access::Fail(*f->finalizer,*claim.claim,f->Event());check("failure-sticky",failed.accepted&&failed.phase==L::SourceBoundPhase::FailedRetained);
    check("failure-revokes-unconsumed-authorization",!Access::Current(*f->finalizer,*authorized.authorization,f->Proof()));
    check("failure-no-primary-retry",!f->Reserve().newPrimaryClaim);
    f->finalizer->StopAdmissions();check("unresolved-source-prevents-drain",!f->finalizer->DrainStatus()->safelyDrained);
    auto capacity=std::make_unique<SourceFixture>(1);auto first=capacity->Reserve();
    // The bounded primary table is also checked directly with a zero-slot fixture.
    auto noCapacity=std::make_unique<SourceFixture>(0);
    check("capacity-refuses",noCapacity->Reserve().reason==L::FinalizationReason::Capacity);
    check("capacity-keeps-live-primary",capacity->Reserve().claim==first.claim);
    auto wrong=std::make_unique<SourceFixture>();auto original=wrong->source;
    wrong->source.knownDerivedFrom=C::RecordReference{C::ContractId::C07,Sym(C::EvaluationResult::WireName),Key("prior.nr"),1};
    check("known-derived-refused",!wrong->Reserve().accepted);wrong->source=original;
    auto frame=*wrong->store.Resolve(wrong->source.recipe.frame).value;frame.generatedFrameId=Known(Id<C::GeneratedFrameId>("generated",1));
    wrong->source.recipe.frame=wrong->store.Publish(frame);check("known-generated-refused",!wrong->Reserve().accepted);
    wrong->source=original;wrong->source.contract.owner=C::OwnerDomain::Provider;check("foreign-contract-refused",!wrong->Reserve().accepted);
    wrong->source=original;wrong->source.recipe.placement=C::Placement::NativeBefore;check("unsupported-before-placement-refused",!wrong->Reserve().accepted);
    wrong->source=original;wrong->store.Withdraw(wrong->contract.consumerQualification);check("missing-consumer-qualification",!wrong->Reserve().accepted);
    auto absent=std::make_unique<SourceFixture>();absent->store.Withdraw(absent->contract.profileQualification);check("missing-profile-qualification",!absent->Reserve().accepted);
    auto mismatched=std::make_unique<SourceFixture>();auto mc=mismatched->Reserve();mismatched->stage.evaluation.value++;
    check("return-evaluation-mismatch",!mismatched->Bind(*mc.claim).accepted);
    auto replaced=std::make_unique<SourceFixture>();auto rc=replaced->Reserve();check("selection-first-bind",replaced->Bind(*rc.claim).accepted);
    replaced->candidate.result.header.revision++;check("selection-root-frozen",!replaced->Bind(*rc.claim).accepted);
    auto rights=std::make_unique<SourceFixture>();auto lc=rights->Reserve();rights->Bind(*lc.claim);rights->facts.holds={};
    check("missing-retention-refused",!rights->Authorize(*lc.claim).accepted);
    auto recording=std::make_unique<SourceFixture>();auto rcl=recording->Reserve();recording->Bind(*rcl.claim);
    recording->dependency.recording=Known(Key("other.recording"));check("wrong-producer-recording-refused",!recording->Authorize(*rcl.claim).accepted);
    auto output=std::make_unique<SourceFixture>();auto oc=output->Reserve();output->Bind(*oc.claim);
    auto changed=*output->store.Resolve(output->boundary.output).value;auto revision=changed.identity.contentRevision.KnownPart()->value;++revision.value;changed.identity.contentRevision=Known(revision);
    output->boundary.output=output->store.Publish(changed,C::OwnerDomain::Resource);check("overwritten-output-refused",!output->Authorize(*oc.claim).accepted);
    auto outer=std::make_unique<SourceFixture>();auto sc=outer->Reserve();outer->stage.outcome=C::NativeStageOutcome::DeliveryRejected;
    outer->PublishStage();auto rejected=outer->Bind(*sc.claim);
    check("outer-failure-retained",!rejected.accepted&&rejected.phase==L::SourceBoundPhase::FailedRetained&&rejected.admissionsClosed);
    check("outer-failure-no-new-claim",!outer->Reserve().newPrimaryClaim);
    auto closure=std::make_unique<SourceFixture>();auto cc=closure->Reserve();closure->Bind(*cc.claim);
    Access::Close(*closure->finalizer,*cc.claim,closure->Event());check("close-wins-before-consume",!closure->Authorize(*cc.claim).accepted);
    auto draining=std::make_unique<SourceFixture>();auto dc=draining->Reserve();Access::Close(*draining->finalizer,*dc.claim,draining->Event());
    check("closed-primary-tail-evidence-retained",draining->Bind(*dc.claim).accepted);
    check("closed-primary-tail-cannot-authorize",!draining->Authorize(*dc.claim).accepted);
    for(auto effect:{L::SourceBoundDispatchEffect::ReturnedAccepted,L::SourceBoundDispatchEffect::ReturnedRejected,
        L::SourceBoundDispatchEffect::InterruptedUnknown})
    {
        prefix=std::to_string(static_cast<int>(effect))+"-";
        auto terminal=std::make_unique<SourceFixture>();terminal->AssociateEvaluation();auto c=terminal->Reserve();terminal->Bind(*c.claim);
        auto a=terminal->Authorize(*c.claim);if(!a.authorization)throw std::runtime_error("terminal authorization");
        auto& t=*terminal;const auto& h=*c.claim;
        L::SourceBoundDispatchOutcome dispatch{Key("sdk.call"),Key("sdk.recording"),Id<C::ObjectIncarnation>("queue",1),effect};
        if(effect!=L::SourceBoundDispatchEffect::InterruptedUnknown)dispatch.actualResult=effect==L::SourceBoundDispatchEffect::ReturnedAccepted?0:1;
        L::SourceBoundSubmissionOutcome submission{dispatch.call,dispatch.recording,Key("sdk.submission"),dispatch.queue,
            L::Fact::Yes,L::Fact::Yes,L::Fact::Yes};
        auto owner=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,97);
        owner.scope=t.source.recipe.header.scope;
        const auto excluded=C::OptionalFact<bool>::FromKnown(true,C::EvidenceRef{owner.record});
        auto wrongRecording=dispatch;wrongRecording.recording=Key("different.consumer.recording");
        check("terminal-dispatch-wrong-recording",!Access::Dispatch(*t.finalizer,h,wrongRecording,t.Event()).accepted);
        check("terminal-submission-requires-dispatch",!Access::Submit(*t.finalizer,h,submission,t.Event()).accepted);
        check("terminal-dispatch-recorded",Access::Dispatch(*t.finalizer,h,dispatch,t.Event()).accepted);
        check("terminal-dispatch-retry",Access::Dispatch(*t.finalizer,h,dispatch,t.Event()).accepted);
        auto changedDispatch=dispatch;changedDispatch.effect=effect==L::SourceBoundDispatchEffect::ReturnedAccepted?
            L::SourceBoundDispatchEffect::ReturnedRejected:L::SourceBoundDispatchEffect::ReturnedAccepted;
        check("terminal-conflicting-dispatch-refused",!Access::Dispatch(*t.finalizer,h,changedDispatch,t.Event()).accepted);
        check("terminal-no-second-dispatch-authority",!Access::Current(*t.finalizer,*a.authorization,t.Proof()));
        check("terminal-callback-is-not-release",!Access::Release(*t.finalizer,h,*t.lease,t.facts,t.Event()).accepted);
        auto hold=*t.facts.holds.Get(0);hold.released=L::Fact::Yes;hold.releaseEvidence=Key("actual.release");
        t.facts.holds={};t.facts.holds.Push(hold);
        check("terminal-release-before-submission-retained",Access::Release(*t.finalizer,h,*t.lease,t.facts,t.Event()).accepted);
        check("terminal-missing-submission-retains",!Access::ResourceRetire(*t.finalizer,h,*t.lease,t.facts,t.Event()).accepted);
        auto changedSubmission=submission;changedSubmission.recording=Key("other.sdk.recording");
        check("terminal-wrong-recording-refused",!Access::Submit(*t.finalizer,h,changedSubmission,t.Event()).accepted);
        check("terminal-submission-recorded",Access::Submit(*t.finalizer,h,submission,t.Event()).accepted);
        check("terminal-submission-retry",Access::Submit(*t.finalizer,h,submission,t.Event()).accepted);
        changedSubmission=submission;changedSubmission.signalSucceeded=L::Fact::No;
        check("terminal-conflicting-submission-refused",!Access::Submit(*t.finalizer,h,changedSubmission,t.Event()).accepted);
        check("terminal-gpu-pending-retained",!Access::ResourceRetire(*t.finalizer,h,*t.lease,t.facts,t.Event()).accepted);
        L::CloseAdmission(*t.lease);t.facts.dependency.completed=L::Fact::Yes;t.facts.dependency.reusable=L::Fact::Yes;
        t.facts.consumerTicketsRetired=L::Fact::Yes;t.facts.ownerCanRecycle=L::Fact::Yes;
        check("terminal-resource-retired",Access::ResourceRetire(*t.finalizer,h,*t.lease,t.facts,t.Event()).accepted);
        check("terminal-logical-busy-retains",!Access::Retire(*t.finalizer,h,owner,t.transaction,Known(true),t.Event()).accepted);
        check("terminal-logical-ack",Access::Logical(*t.finalizer,h,t.Event()));
        check("terminal-no-exclusion-retains",!Access::Retire(*t.finalizer,h,owner,t.transaction,Known(false),t.Event()).accepted);
        auto wrongTransaction=t.transaction;wrongTransaction.id.value++;
        check("terminal-other-source-refused",!Access::Retire(*t.finalizer,h,owner,wrongTransaction,Known(true),t.Event()).accepted);
        auto wrongScope=owner;wrongScope.scope.key=Key("unrelated.source.scope");
        check("terminal-wrong-exclusion-scope",!Access::Retire(*t.finalizer,h,wrongScope,t.transaction,excluded,t.Event()).accepted);
        check("terminal-wrong-exclusion-evidence",!Access::Retire(*t.finalizer,h,owner,t.transaction,Known(true),t.Event()).accepted);
        auto done=Access::Retire(*t.finalizer,h,owner,t.transaction,excluded,t.Event());
        check("terminal-receipt-without-seal",done.terminal&&done.terminal->consumed==(effect==L::SourceBoundDispatchEffect::ReturnedAccepted));
        check("terminal-retry-identical",Access::Retire(*t.finalizer,h,owner,t.transaction,excluded,t.Event()).terminal==done.terminal);
        check("terminal-tombstone-prevents-reuse",!t.Reserve().accepted);
        check("terminal-strict-still-unavailable",!t.Claim().accepted);
        t.finalizer->StopAdmissions();check("terminal-source-drained",t.finalizer->DrainStatus()->safelyDrained);
    }
}
int main(int argc,char** argv)
{return Guard([&]{Report r("source-bound-consumer",argc==2?std::string_view(argv[1]):std::string_view{});Run(r);return r.Finish();});}
