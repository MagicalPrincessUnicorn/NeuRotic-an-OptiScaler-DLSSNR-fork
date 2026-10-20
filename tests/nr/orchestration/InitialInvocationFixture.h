#pragma once
#include "InitialNativeFixture.h"
#include <functional>
namespace InitialNativeTests
{
namespace P=Neurotic::Protocol;
using Access=O::InitialNativeTestAccess;
struct InvocationStore:NrContextTests::Store
{
    std::function<void()> resolveHook;
    bool failPublication=false;
    explicit InvocationStore(NrContextTests::Store value):NrContextTests::Store(std::move(value)){}
    template<class T>S::MetadataView<T> Resolve(const C::MetadataRef<T>& ref)const
    {if(resolveHook)resolveHook();return NrContextTests::Store::Resolve(ref);}
    template<class T>C::MetadataRef<T> Publish(const T& value,C::OwnerDomain owner=C::OwnerDomain::Context)
    {if(failPublication)throw L::MetadataRefusal{};return NrContextTests::Store::Publish(value,owner);}
};
// Synthetic exterior Resource/Recording/Provider facade. Canonical commitment
// and every primary claim still come from the production root/kernel/finalizer.
struct SyntheticInvocation final:P::NativeInvocationOwnerPort
{
    L::NativeSessionLifetime* root=nullptr;
    std::recursive_mutex mutex;
    std::uint64_t frameNumber=10;
    std::optional<std::uint64_t> evaluationOverride;
    std::optional<O::MaterializationRecord> importedMaterialization;
    std::uint64_t featureIncarnation=1,ingressIncarnation=1;
    std::shared_ptr<InvocationStore> store;
    std::optional<O::MaterializationRecord> preparedRecord;
    std::shared_ptr<P::NativeInvocationSnapshot> snapshot;
    std::optional<L::LiveConsumptionLease> lease;L::OwnerFacts facts;
    std::function<void(P::NativeInvocationSnapshot&)> changePreparation,changeCurrent;
    unsigned guards=0,preparations=0;
    bool failGuard=false,terminal=true;
    std::optional<C::NativeStageDeliveryV1> committedStage;
    L::NativeHistoryState history{Key("strict.history.owner"),Key("strict.history")};
    unsigned consumedCalls=0;
    bool consumptionAllowed=false;
    explicit SyntheticInvocation(L::NativeSessionLifetime* r,std::uint64_t frame=10):root(r),frameNumber(frame){}
    std::optional<C::NativeSampleIdentityV1> SourceSample(const O::MaterializationRecord& enrolled)const override
    {
        const auto& m=importedMaterialization?*importedMaterialization:enrolled;
        auto sample=m.initialSample;
        sample.producerOrdinal=frameNumber;sample.callback=Key("callback",frameNumber);
        sample.feature=Id<C::ObjectIncarnation>("feature",featureIncarnation);
        sample.ingress=Id<C::ObjectIncarnation>("ingress",ingressIncarnation);
        return sample;
    }
    std::shared_ptr<const P::NativeInvocationSnapshot> Prepare(const O::MaterializationRecord& enrolled)override
    {
        const auto& m=importedMaterialization?*importedMaterialization:enrolled;
        std::lock_guard lock(mutex);++preparations;preparedRecord=m;
        if(!m.metadata)return {};
        NativeTests::Fixture n(false,true);
        // The retained preparation catalog contains the original fixture's
        // immutable base entries; append fresh publications after that closure.
        n.store=*std::static_pointer_cast<const NrContextTests::Store>(m.metadata);
        n.frame.sessionId=Known(m.enrollment.session);n.frame.renderStreamId=Known(m.enrollment.stream);n.frame.viewId=Known(m.enrollment.view);
        n.frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",frameNumber));
        n.request.evaluation=Id<C::EvaluationId>("evaluation",evaluationOverride.value_or(frameNumber));n.frame.evaluationId=Known(n.request.evaluation);
        n.request.recipeHeader.record.value+=1000*frameNumber;n.request.profileHeader.record.value+=1000*frameNumber;
        auto context=*S::ResolveMetadata(n.request.context,n.store);context.header.record.value+=1000*frameNumber;
        context.frame=n.store.Publish(n.frame);n.request.currentContext={Sym(C::CanonicalFrameContext::WireName),context.header.record,context.header.revision};
        auto motion=*S::ResolveOptional(context.motion,n.store);motion.framePair.current=n.frame.baseRealFrameId;
        context.motion=Known(n.store.Publish(motion));n.request.context=n.store.Publish(context);
        auto semantic=*S::ResolveMetadata(n.request.semantic,n.store);semantic.header.record.value+=1000*frameNumber;
        semantic.context=n.request.currentContext;n.request.semantic=n.store.Publish(semantic);
        for(auto& use:n.request.representations)use.context=n.request.currentContext;
        n.AsNativeSample(frameNumber,featureIncarnation,ingressIncarnation);
        n.request.externalPlan={Sym(C::PlanCommit::WireName),m.plan.header.record,m.plan.header.revision};
        n.request.nativePlan=n.store.Publish(m.plan,C::OwnerDomain::StreamCoordinator);n.Rebuild();
        snapshot=std::make_shared<P::NativeInvocationSnapshot>();auto& s=*snapshot;
        s.product=*n.product;s.anchor=m.anchor;s.generation=m.plan.routeGeneration.KnownPart()->value;s.frame=n.frame;
        s.consumer=Key("consumer",frameNumber);s.recording=Key("recording",frameNumber);s.reservation=Key("reservation",frameNumber);
        C::GenerationVector vector;vector.Insert(C::GenerationToken{s.generation.Describe()});
        s.generations=n.store.Publish(vector,C::OwnerDomain::IdentityRegistry);s.handoff=Id<C::HandoffContractGeneration>("handoff",1);
        L::LeaseBinding b;b.description.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,700+frameNumber);
        b.description.context=s.product.recipe.context;b.description.candidate={Sym(C::AcquisitionCandidate::WireName),Key("candidate"),1};
        b.description.resource=n.source.identity;b.description.use=C::UsageKind::RetainedProviderConsumption;
        b.description.commandScope.key=s.recording;b.description.device=Known(Id<C::ObjectIncarnation>("device",1));
        b.description.queue=Known(Id<C::ObjectIncarnation>("queue",2));b.description.revoked=Known(false);
        b.description.revocationEpoch=Known(Key("revocation",1));b.description.expirationBoundary=Known(Key("callback-end",frameNumber));
        b.description.submissionRecheckRequired=Known(true);b.description.generations=s.generations;
        b.consumer=s.consumer;b.purpose=Sym("ColorModelInput");b.plan=m.plan.header.record;b.profile=s.product.profileCertificate.header.record;
        b.route=C::GenerationToken{s.generation.Describe()};b.sameRecording=L::SameRecordingUse{s.recording,s.reservation,1,2,Key("provider-hold",frameNumber)};
        s.leases.Push(b);store=std::make_shared<InvocationStore>(std::move(n.store));s.metadata=store;
        if(changePreparation)changePreparation(s);
        if(!SealSnapshot(s,store))return {};
        return std::make_shared<const P::NativeInvocationSnapshot>(s);
    }
    bool EstablishSyntheticLease()
    {
        const auto& b=*snapshot->leases.Get(0);facts={};facts.current=b;
        facts.permission=facts.callbackActive=facts.apiCompatible=facts.registryHealthy=facts.nativeBindingMatches=L::Fact::Yes;
        facts.dependency.ordered=facts.dependency.sameDevice=facts.dependency.queueKnown=facts.dependency.supportedType=L::Fact::Yes;
        facts.dependency.committed=facts.dependency.uniqueSubmission=facts.dependency.nonReplayable=facts.dependency.completed=
            facts.dependency.reusable=L::Fact::No;
        facts.recordingTracked=facts.resourcesRegistered=facts.descriptorsProtected=facts.externalRegistrationsComplete=L::Fact::Yes;
        facts.consumerTicketsRetired=facts.ownerCanRecycle=L::Fact::No;
        L::ExternalHold hold;hold.registration=*b.sameRecording->providerRegistration;hold.consumer=b.consumer;
        hold.target=b.description.resource;hold.kind=L::HoldKind::Provider;hold.provider=Id<C::ProviderIncarnation>("provider",1);
        hold.handoff=Gen(C::IdentityKind::HandoffContractGeneration,"handoff",1);hold.released=L::Fact::No;facts.holds.Push(hold);
        facts.sameRecording=L::SameRecordingFacts{*b.sameRecording,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes};
        facts.sameRecording->compatibleHolds.Push(hold.registration);
        lease=L::BeginConsumption(b,facts);return lease.has_value();
    }
    struct Guard final:P::NativeOwnerActionGuard
    {
        SyntheticInvocation& owner;std::unique_lock<std::recursive_mutex> lock;P::NativeInvocationSnapshot current;
        explicit Guard(SyntheticInvocation& p):owner(p),lock(p.mutex),current(*p.snapshot)
        {++owner.guards;if(owner.changeCurrent)owner.changeCurrent(current);}
        ~Guard(){--owner.guards;}
        const P::NativeInvocationSnapshot& Current()const noexcept override{return current;}
        P::NativeLeaseProof Refresh(P::NativeCheckPoint,const P::NativeResourceUse& use)noexcept override
        {return {use.resource,owner.lease?&*owner.lease:nullptr,&owner.facts};}
    };
    std::unique_ptr<P::NativeOwnerActionGuard> Acquire(const P::NativeInvocationSnapshot&,P::NativeCheckPoint)override
    {if(failGuard)return {};return std::make_unique<Guard>(*this);}
    O::Retirement CancelPreparation()noexcept override{return O::Retirement::Retired;}
    O::Retirement Retire(const P::NativeInvocationSnapshot&,const std::optional<L::PrimaryNrClaimHandle>& claim)noexcept override
    {return claim||!terminal||!history.InvocationClosed()?O::Retirement::Unknown:O::Retirement::Retired;}
    std::optional<C::NativeStageDeliveryV1> CommittedStage()const noexcept override{return committedStage;}
    bool FinalConsumed(const P::NativeInvocationSnapshot& current,const L::PrimaryNrClaimHandle& claim,
        const L::FinalizerConsumptionAcknowledgment& acknowledgment)noexcept override
    {
        if(!consumptionAllowed||!snapshot||!P::SameNativeInvocation(current,*snapshot)||
           !acknowledgment.Matches(Access::Finalizer(*root),claim,current.product.recipe.evaluation))return false;
        ++consumedCalls;return history.Consumed(current.product.recipe.evaluation);
    }
    bool FinalInterrupted(const P::NativeInvocationSnapshot& current,const L::PrimaryNrClaimHandle& claim,
        const L::FinalizerInterruptionAcknowledgment& acknowledgment)noexcept override
    {
        if(!consumptionAllowed||!snapshot||!P::SameNativeInvocation(current,*snapshot)||
           !acknowledgment.Matches(Access::Finalizer(*root),claim,current.product.recipe.evaluation))return false;
        history.Abandon(current.product.recipe.evaluation);return history.InvocationClosed();
    }
};
}
