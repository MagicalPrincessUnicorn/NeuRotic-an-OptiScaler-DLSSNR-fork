#pragma once
#include "NativeSourcePublication.h"
#include "NativeSessionLifetime.h"
#include <nr/protocol/LegacySettingsProjection.h>
#include <functional>

namespace Neurotic::Lifecycle
{
class NativeInvocationOwner;
// Concrete initial owner port. Only the authentic source bootstrap supplies a
// live renderer borrow, retained source and allocation-owner facts. No public
// opening from readiness booleans or serialized PlanCommit is provided.
class NativeInitialMaterialization final:public Orchestration::InitialNativeOwnerPort
{
    friend class NativeProcessBootstrap;
    friend class NativeInvocationOwner;
    using O=Orchestration::InitialNativeOwnerPort;
    struct Store
    {
      private:
        friend class NativeProcessBootstrap;
        friend class NativeInitialMaterialization;
        std::optional<C::NativeSampleIdentityV1> selectedSrSample_;
        std::optional<C::ResourceView> selectedSrOutput_;
        std::optional<NativeEvaluationAssociation> evaluationAssociation_;
        // Private source selection, sealed before the first route activation.
        // The callable rechecks the retained SDK allocation and writer action;
        // a serialized contract or caller readiness flag cannot install it.
        std::optional<C::NativeFinalConsumerContractV1> selectedConsumer_;
        std::optional<C::RecordKey> selectedEnrollment_;
        std::function<bool(ID3D12GraphicsCommandList*,ID3D12Resource*)> selectedWriterCurrent_;
        friend class NativeInvocationOwner;
      public:
        // Bound only by the source bootstrap after the selected operation's
        // committed Resource publication. This is metadata provenance, not a
        // resource-use grant or proof of original game contents.
        bool SelectedSrOutput(const C::NativeSampleIdentityV1& sample,const C::ResourceView& view)const
        {
            return selectedSrSample_&&selectedSrOutput_&&*selectedSrSample_==sample&&
                Context::CompleteContent(view)&&view==*selectedSrOutput_;
        }
        NativeOwnerSet& owners;
        std::shared_ptr<NativeSourcePublication> source;
        // The coordinator's accepted initial route owns its original arena.
        // Later callback metadata may resolve those C05/C04 references, while
        // C01 candidate lookup remains restricted to this callback's source.
        std::shared_ptr<const Store> committedRoute;
        std::shared_ptr<const void> call;
        std::unique_ptr<OwnerMetadataArena> metadata;
        Protocol::NativeProfilePreparationRequest request;
        Orchestration::RoutingInput routing;
        Protocol::RuntimeContract runtime;
        Protocol::SettingsSnapshot settings;
        Protocol::NativeEncodeControls encode;
        std::uint64_t resumeGeneration=0;
        bool possibleEffects=false;
        C::Symbol reason;
        explicit Store(NativeOwnerSet& owner):owners(owner),metadata(owner.NewInvocationMetadata(8*1024*1024,512)){}
        template<class T>auto Resolve(const C::MetadataRef<T>& ref)const
        {
            auto value=metadata->Resolve(ref);if(value.value)return value;
            value=source->Resolve(ref);if(value.value)return value;
            return committedRoute?committedRoute->Resolve(ref):value;
        }
        const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& ref)const
        {return source->ResolveCandidate(ref);}
        template<class T>auto Publish(const T& value,C::OwnerDomain domain=C::OwnerDomain::Context)
        {return metadata->ForOwner(domain).Publish(value);}
        static C::OwnerValueReference Value(OwnerPublicationJournal& journal,std::string_view name)
        {return {journal.Binding().owner,Protocol::Symbol(name),{1,0},journal.Event().evidence.record,1};}
        void DeclareRuntime()
        {
            runtime.source=Value(owners.RuntimeJournal(),"Native.Feature18Runtime");
            runtime.family=Protocol::Symbol("AlphaFeature18");runtime.abiVersion=1;runtime.featureKind=18;
            // Revisions, availability and runtime incarnation are not declarations.
        }
        template<class Snapshot>bool CaptureSettings(const Snapshot& snapshot)
        {
            const auto reference=Value(owners.ConfigurationJournal(),"NR.RenderSettings");
            auto projected=Protocol::ProjectLegacySettings(snapshot,reference,C::EvidenceRef{reference.record});
            if(!projected)return false;settings=std::move(*projected);
            resumeGeneration=snapshot.GetDlssNrRuntimeSnapshot().resumeGeneration;return true;
        }
        bool BindProfile(const Orchestration::NativeScopeEnrollment& enrolled,
            const C::OptionalFact<C::ProviderIncarnation>& incarnation,const C::OwnerValueReference& allocation)
        {
            const auto* prepared=source->RepresentationPreparation();
            if(!prepared||!prepared->bundle||!Context::Established(incarnation))return false;
            const auto& bundle=*prepared->bundle;
            const auto* context=Context::ResolveMetadata(bundle.context,*this);
            const auto* semantic=Context::ResolveMetadata(bundle.semantic,*this);
            const auto* frame=context?Context::ResolveMetadata(context->frame,*this):nullptr;
            if(!context||!semantic||!frame||!Context::ResolveNativeSample(*frame,*this))return false;
            runtime.incarnation=incarnation;
            runtime.available=C::OptionalFact<bool>::FromKnown(true,C::EvidenceRef{runtime.source.record});
            request.runtime=runtime;request.requestedProfile=semantic->profile;
            request.context=bundle.context;request.currentContext=Protocol::Reference<C::ContractId::C02>(*context);
            request.semantic=bundle.semantic;request.representations=bundle.representations;
            request.settings=Publish(settings,C::OwnerDomain::Configuration);
            request.profileHeader=owners.StrategyJournal().Header(C::ContractId::C04,enrolled.scope);
            if(!Context::Established(frame->episodeId))return false;
            evaluationAssociation_=owners.Evaluation()->NativeEvaluation(*Context::ResolveNativeSample(*frame,*this),
                frame->episodeId.KnownPart()->value,owners.StrategyJournal().Event());
            if(!evaluationAssociation_)return false;
            request.evaluation=evaluationAssociation_->Evaluation(); // nonexecuting profile projection only
            C::BoundedList<C::RecordKey,32> dependencies;dependencies.Push(settings.source.record);
            for(const auto& use:bundle.representations)
            {const auto* plan=Context::ResolveMetadata(use.plan,*this);if(!plan||!dependencies.Push(plan->keys.plan))return false;}
            C::StructuralSignature history;history.schema=Protocol::Symbol("Native.InitialHistory");history.version=1;
            history.generations=context->generations;history.structuralDependencies.count=static_cast<std::uint32_t>(dependencies.Size());
            history.structuralDependencies.backing=Publish(dependencies,C::OwnerDomain::History);
            C::HistoryKey key;key.owner=owners.HistoryJournal().Binding().subject;key.slot=Protocol::Symbol("Native.Primary");
            key.dependencies=Publish(history,C::OwnerDomain::History); // no reset/advance acknowledgement
            request.history=Publish(key,C::OwnerDomain::History);
            auto plan=std::make_unique<Orchestration::CompletePlan>();
            plan->offer=Value(owners.StrategyJournal(),"Native.InitialOffer");
            plan->anchor.plan=plan->offer.record;plan->anchor.scope=enrolled.scope;plan->anchor.strategy=request.strategy;
            plan->anchor.profile=request.requestedProfile;plan->anchor.placement=request.placement;
            C::StructuralSignature signature;signature.schema=Protocol::Symbol("ORCH.CompletePlan");signature.version=1;
            signature.generations=context->generations;signature.structuralDependencies=history.structuralDependencies;
            plan->anchor.signature=Publish(signature,C::OwnerDomain::Strategy);
            C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::HostReturn;
            delivery.profile=request.requestedProfile;delivery.placement=request.placement;
            delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
            if(selectedConsumer_)
            {
                if(!selectedEnrollment_||selectedEnrollment_->Check()!=C::Error::None||!selectedWriterCurrent_||
                   request.placement!=C::Placement::NativeAfter)return false;
                delivery.kind=C::NativeDeliveryKind::FinalConsumerRequired;
                delivery.finalConsumer=Publish(*selectedConsumer_,C::OwnerDomain::FrameGeneration);
            }
            plan->anchor.nativeDelivery=Publish(delivery,C::OwnerDomain::StreamCoordinator);
            plan->context=request.currentContext;plan->semantic=request.semantic;
            const auto evidence=C::EvidenceRef{plan->offer.record};
            plan->multipass=C::OptionalFact<std::optional<C::ContractRef<C::ContractId::C09>>>::FromKnown({},evidence);
            plan->fgSubplan=C::OptionalFact<std::optional<C::RecordReference>>::FromKnown({},evidence);
            if(delivery.finalConsumer)
                plan->fgSubplan=C::OptionalFact<std::optional<C::RecordReference>>::FromKnown(
                    C::RecordReference{C::ContractId::C11,Protocol::Symbol(C::NativeFinalConsumerContractV1::WireName),
                        delivery.finalConsumer->record,delivery.finalConsumer->revision},evidence);
            plan->boundary=context->boundary;
            C::BoundedList<C::ContractRef<C::ContractId::C01>,64> sources;
            for(const auto& candidate:source->Candidates())if(!sources.Push(Protocol::Reference<C::ContractId::C01>(candidate)))return false;
            plan->sourceBundle.count=static_cast<std::uint32_t>(sources.Size());plan->sourceBundle.backing=Publish(sources,C::OwnerDomain::Provider);
            C::BoundedList<C::ContractRef<C::ContractId::C12>,32> preparations;
            for(const auto& use:bundle.representations)
            {const auto* p=Context::ResolveMetadata(use.plan,*this);if(!p||!preparations.Push(Protocol::Reference<C::ContractId::C12>(*p)))return false;}
            plan->preparations.count=static_cast<std::uint32_t>(preparations.Size());
            plan->preparations.backing=Publish(preparations,C::OwnerDomain::Context);
            C::BoundedList<Orchestration::OperationalFact,16> facts;
            const auto fact=[&](std::string_view field,const C::OwnerValueReference& owner){
                Orchestration::OperationalFact value;value.source=owner;value.field=Protocol::Symbol(field);
                value.plan=plan->anchor.plan;value.context=request.currentContext;value.generations=context->generations;
                value.value=C::OptionalFact<bool>::FromKnown(true,C::EvidenceRef{owner.record});return facts.Push(value);};
            const auto strategy=Value(owners.StrategyJournal(),"Native.PreparedModel");
            const auto identity=Value(owners.IdentityJournal(),"Native.EnrolledSource");
            const auto current=Value(owners.ContextJournal(),"Native.CurrentContext");
            const auto returned=Value(owners.SourceJournal(),"Native.NgxReturnBoundary");
            // These are preparation observations under genuine owner borrows,
            // not C03 use, execution, HostReturnRecorded, or GPU/provider release.
            for(const auto field:{"implementation","availability","health","warmup"})if(!fact(field,strategy))return false;
            for(const auto field:{"api","resource","lifetime"})if(!fact(field,allocation))return false;
            if(!fact("identity",identity)||!fact("freshness",current)||!fact("hostReturn",returned))return false;
            if(selectedConsumer_)
            {
                const auto fg=Value(owners.FrameGenerationJournal(),"Native.ControlledFsr3Enrollment");
                const auto handoff=Value(owners.FinalizerJournal(),"Native.ControlledFsr3Handoff");
                if(!fact("handoff",handoff)||!fact("fg",fg))return false;
            }
            plan->facts.count=static_cast<std::uint32_t>(facts.Size());plan->facts.backing=Publish(facts,C::OwnerDomain::Strategy);
            routing.scope=enrolled.scope;routing.frameScope=enrolled.scope;routing.frame=*frame;routing.frameSource=returned;
            routing.intent.source=Value(owners.ConfigurationJournal(),"Native.FixedIntent");routing.intent.mode=Protocol::Symbol("FixedLegacy");
            routing.intent.fixedStrategy=request.strategy;routing.intent.fixedPlacement=request.placement;
            // This entry point is the authentic SuperSampling HostReturn
            // service. Its fixed request has no NR-owned RR/FG subplan; this
            // says nothing about the game's later FG/final-consumer readiness.
            const auto intentEvidence=C::EvidenceRef{routing.intent.source.record};
            routing.mandatoryRR=C::OptionalFact<bool>::FromKnown(false,intentEvidence);
            routing.mandatoryFG=C::OptionalFact<bool>::FromKnown(selectedConsumer_.has_value(),intentEvidence);
            routing.incumbent.source=Value(owners.PolicyJournal(),"Native.InitialEmpty");
            return routing.offers.Push(Publish(*plan,C::OwnerDomain::Strategy));
        }
    };
    struct Prepared final:Orchestration::PreparedInitialNative
    {
        std::shared_ptr<Store> store;
        std::function<Orchestration::NativeDrainReadiness()> retirement;
        Prepared(std::shared_ptr<Store> value,std::function<Orchestration::NativeDrainReadiness()> observe):
            store(std::move(value)),retirement(std::move(observe)){}
        bool Build(const Orchestration::NativeScopeEnrollment& scope)
        {
            store->reason.Assign("Native.Initialization.Closure");
            return Prepare(scope,store->routing,store->request,store->owners.PolicyJournal().Header(C::ContractId::C05,scope.scope),store);
        }
        Orchestration::Retirement Cancel()noexcept override
        {try{return retirement&&retirement().AllRetired()?Orchestration::Retirement::Retired:Orchestration::Retirement::Outstanding;}
         catch(...){return Orchestration::Retirement::Outstanding;}}
        void Activated()noexcept override{}
    };
    std::unique_ptr<Prepared> pending_;
    std::shared_ptr<Store> active_;
    bool borrowed_=false,same_=false;
    std::function<Orchestration::NativeDrainReadiness()> retirement_;
    explicit NativeInitialMaterialization(std::function<Orchestration::NativeDrainReadiness()> observe):retirement_(std::move(observe)){}
    void ReleasePhysicalCapture()noexcept{if(active_)active_->call.reset();}
    static bool SameSettings(const Store& a,const Store& b)
    {
        if(a.request.requestedProfile!=b.request.requestedProfile||a.request.placement!=b.request.placement||
           a.selectedConsumer_!=b.selectedConsumer_||a.selectedEnrollment_!=b.selectedEnrollment_||
           a.encode!=b.encode||a.resumeGeneration!=b.resumeGeneration||a.settings.controls.Size()!=b.settings.controls.Size())return false;
        for(std::size_t i=0;i<a.settings.controls.Size();++i)
        {
            const auto& x=*a.settings.controls.Get(i);const auto& y=*b.settings.controls.Get(i);
            if(x.field!=y.field||!Context::SemanticEqual(x.raw,y.raw)||!Context::SemanticEqual(x.effective,y.effective))return false;
        }
        return Context::SemanticEqual(a.runtime.incarnation,b.runtime.incarnation);
    }
    bool Stage(const Orchestration::NativeScopeEnrollment& scope,std::shared_ptr<Store> store)
    {
        borrowed_=true;
        if(active_){same_=SameSettings(*active_,*store);return same_;}
        auto prepared=std::make_unique<Prepared>(store,retirement_);if(!prepared->Build(scope))return false;
        active_=std::move(store);pending_=std::move(prepared);same_=true;return true;
    }
    void EndBorrow()noexcept{borrowed_=false;same_=false;}
  public:
    bool Enabled()noexcept override{return borrowed_&&same_;}
    bool MatchesActive(const Orchestration::MaterializationRecord& record)noexcept override
    {return Enabled()&&active_&&record.anchor.profile==active_->request.requestedProfile&&
        record.delivery.kind==(active_->selectedConsumer_?C::NativeDeliveryKind::FinalConsumerRequired:C::NativeDeliveryKind::HostReturn)&&
        record.delivery.placement==active_->request.placement;}
    std::unique_ptr<Orchestration::PreparedInitialNative> Prepare(const Orchestration::NativeScopeEnrollment&)override
    {return Enabled()?std::move(pending_):nullptr;}
    Orchestration::NativeDrainReadiness ObserveRetirement()noexcept override
    {
        // Inspection never clears a live preparation reservation. The actual
        // bootstrap binds this observer privately to its existing owners.
        if(borrowed_||!retirement_)return {};
        try{return retirement_();}catch(...){return {};}
    }
};
}
