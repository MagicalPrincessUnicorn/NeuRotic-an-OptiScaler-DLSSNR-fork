#pragma once
#ifndef NR_ORCH_INIT_TESTING
#error Synthetic INIT owners must never be compiled into production
#endif
#include <nr/lifecycle/NativeSessionLifetime.h>
#include <nr/protocol/NativeRouteAdmission.h>
#include "../vertical-slice/native_fixture.h"
#include "RoutingFixture.h"
#include <atomic>
namespace Neurotic::Orchestration
{
// TEST TARGET ONLY. No production bootstrap or external readiness is claimed.
class InitialNativeTestAccess
{
  public:
    static std::shared_ptr<Lifecycle::NativeSessionLifetime> Open(std::shared_ptr<InitialNativeOwnerPort> port)
    {
        static std::atomic<std::uint64_t> serial=0;
        auto owners=std::make_unique<Lifecycle::NativeOwnerSet>(Symbol("init.test."+std::to_string(++serial)));
        const auto stream=owners->Topology()->Stream(owners->IdentityJournal().Event());
        const auto view=owners->Topology()->View(owners->IdentityJournal().Event());
        if(!stream.value.IsKnown()||!view.value.IsKnown())throw std::runtime_error("test topology");
        NativeScopeEnrollment scope{*owners->Session(),stream.value.KnownPart()->value,view.value.KnownPart()->value,{}};
        scope.scope.key=NrTest002::Key("ctx.fixture.scope",1);
        return Lifecycle::NativeSessionLifetime::Adopt(owners,scope,std::move(port));
    }
    static void Fault(Lifecycle::NativeSessionLifetime& root,TestFault fault){root.kernel_->SetTestingFault(fault);}
    static std::optional<Protocol::NativeInvocationAdmission> Admit(const std::shared_ptr<Lifecycle::NativeSessionLifetime>& root,
        std::shared_ptr<Protocol::NativeInvocationOwnerPort> port){return Protocol::NativeInvocationIngress::Admit(root,std::move(port));}
    static Lifecycle::FinalizerEvent Event(Lifecycle::NativeSessionLifetime& root,C::MetadataRef<C::GenerationVector> generations)
    {return {root.owners_->FinalizerJournal().Event(),generations};}
    static auto Action(Protocol::NativeInvocationAdmission& a,Protocol::NativeCheckPoint p){return a.Action(p);}
    static bool Start(Protocol::NativeInvocationAdmission& a,const Protocol::NativeOwnerActionGuard& g){return a.StartModel(g);}
    static bool Called(Protocol::NativeInvocationAdmission& a){return a.ModelCalled();}
    static void Returned(Protocol::NativeInvocationAdmission& a){a.ModelReturned();}
    static void DiagnosticNoise(Lifecycle::NativeSessionLifetime& root)
    {for(unsigned i=0;i<32;++i)root.owners_->PolicyJournal().Event();}
    static Lifecycle::FinalRealFrameFinalizer& Finalizer(Lifecycle::NativeSessionLifetime& root)
    {return *root.owners_->Finalizer();}
};
}
namespace InitialNativeTests
{
using namespace NrTest002;
namespace O=Neurotic::Orchestration;
namespace L=Neurotic::Lifecycle;
namespace S=Neurotic::Context;
struct SyntheticOwner final:O::InitialNativeOwnerPort
{
    bool enabled=true,selectionWrong=false,legacyAttempted=false,metadataMissing=false,drainUnknown=false,cancelUnknown=false;
    C::NativeDeliveryKind deliveryKind=C::NativeDeliveryKind::FinalConsumerRequired;
    bool missingDelivery=false,returnUnavailable=false,returnWrongOwner=false,selectionFrameMismatch=false;
    std::optional<std::size_t> missingOwner;
    unsigned activated=0,cancelled=0;
    std::function<void()> beforePrepare,beforeMatches;
    std::function<void(O::NativeDrainReadiness&)> retirement;
    std::weak_ptr<NrContextTests::Store> metadataLifetime;
    struct Prepared final:O::PreparedInitialNative
    {
        SyntheticOwner& owner;
        explicit Prepared(SyntheticOwner& p):owner(p){}
        bool Build(const O::NativeScopeEnrollment& scope)
        {
            NativeTests::Fixture n(false,true);
            n.frame.sessionId=Known(scope.session);n.frame.renderStreamId=Known(scope.stream);n.frame.viewId=Known(scope.view);
            auto context=*S::ResolveMetadata(n.request.context,n.store);context.frame=n.store.Publish(n.frame);
            n.request.context=n.store.Publish(context);
            n.AsNativeSample(1);
            NrRoutingTests::Fixture r;r.store=n.store;r.input.frame=n.frame;r.input.scope=scope.scope;r.input.frameScope=scope.scope;
            C::GenerationVector generations;generations.Insert(Gen(C::IdentityKind::ProviderIncarnation,"provider",1));
            r.generations=r.store.Publish(generations,C::OwnerDomain::IdentityRegistry);
            r.factsA={};r.factsB={};r.Make(r.a,r.factsA,1,"NativeTemporal");r.Make(r.b,r.factsB,2,"Present");
            r.input.intent.mode=Sym("FixedLegacy");r.input.intent.fixedStrategy=Sym("NativeTemporal");
            r.input.intent.fixedPlacement=n.request.placement;r.a.anchor.placement=n.request.placement;
            r.a.anchor.scope=scope.scope;r.a.anchor.strategy=Sym("NativeTemporal");r.a.anchor.profile=n.request.requestedProfile;
            r.a.semantic=n.request.semantic;r.a.context=n.request.currentContext;
            C::NativeDeliveryContractV1 delivery;delivery.kind=owner.deliveryKind;
            delivery.profile=n.request.requestedProfile;delivery.placement=n.request.placement;
            delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
            delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
            C::NativeFinalConsumerContractV1 consumer;consumer.provider=Id<C::ProviderIncarnation>("provider",1);
            consumer.handoff=Id<C::HandoffContractGeneration>("handoff",1);consumer.contract=Sym("fixture.before-fg");
            consumer.contractVersion=1;consumer.extent={1920,1080};
            if(owner.deliveryKind==C::NativeDeliveryKind::FinalConsumerRequired)
                delivery.finalConsumer=r.store.Publish(consumer,C::OwnerDomain::FrameGeneration);
            if(!owner.missingDelivery)r.a.anchor.nativeDelivery=r.store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
            if(owner.deliveryKind==C::NativeDeliveryKind::HostReturn)
            {
                // Explicitly no final-consumer readiness in this CPU owner.
                r.Set(r.factsA,"handoff",Known(false));
                O::OperationalFact returned;returned.field=Sym("hostReturn");returned.plan=r.a.anchor.plan;
                returned.context=n.request.currentContext;returned.generations=r.generations;
                returned.source=r.Owner(owner.returnWrongOwner?C::OwnerDomain::Diagnostics:C::OwnerDomain::Provider,801);
                if(!owner.returnUnavailable)returned.value=Known(true);
                r.factsA.Push(returned);
            }
            for(auto& fact:r.factsA)fact.context=n.request.currentContext;
            if(owner.selectionFrameMismatch)
            {
                auto alternate=*S::ResolveNativeSample(r.input.frame,r.store);
                alternate.feature=Id<C::ObjectIncarnation>("feature",2);
                r.input.frame.nativeSample=r.store.Publish(alternate,C::OwnerDomain::Provider);
            }
            r.Publish();
            if(owner.selectionWrong)r.input.intent.mode=Sym("Auto");
            if(owner.legacyAttempted)r.input.legacyEffective=r.a.anchor;
            if(owner.metadataMissing)r.store.Withdraw(n.request.settings);
            auto store=std::make_shared<NrContextTests::Store>(std::move(r.store));
            owner.metadataLifetime=store;
            auto header=NrContextTests::Header(C::ContractId::C05,C::OwnerDomain::OrchestratorPolicy,900);
            header.scope=scope.scope;
            return Prepare(scope,r.input,n.request,header,store);
        }
        O::Retirement Cancel()noexcept override{++owner.cancelled;return owner.cancelUnknown?O::Retirement::Unknown:O::Retirement::Retired;}
        void Activated()noexcept override{++owner.activated;}
    };
    bool Enabled()noexcept override{return enabled;}
    bool MatchesActive(const O::MaterializationRecord& active)noexcept override
    {
        if(beforeMatches)beforeMatches();
        return enabled&&!selectionWrong&&!legacyAttempted&&!missingDelivery&&deliveryKind==active.delivery.kind;
    }
    std::unique_ptr<O::PreparedInitialNative> Prepare(const O::NativeScopeEnrollment& scope)override
    {
        if(beforePrepare)beforePrepare();
        if(missingOwner)return {};
        auto p=std::make_unique<Prepared>(*this);if(!p->Build(scope))return {};return p;
    }
    O::NativeDrainReadiness ObserveRetirement()noexcept override
    {
        O::NativeDrainReadiness r;r.owners.fill(O::Retirement::Retired);
        r.preparation=r.callbacks=r.unsealedClaims=O::Retirement::Retired;
        if(drainUnknown)r.owners[static_cast<std::size_t>(O::InitialOwner::Provider)]=O::Retirement::Unknown;
        if(retirement)retirement(r);
        return r;
    }
};
struct Fixture
{
    std::shared_ptr<SyntheticOwner> owner=std::make_shared<SyntheticOwner>();
    std::shared_ptr<L::NativeSessionLifetime> root=O::InitialNativeTestAccess::Open(owner);
    O::InitialFixedNativeCoordinator coordinator=root->Coordinator();
    O::InitResult Activate(){return coordinator.TryActivate();}
    void SetFault(O::TestFault fault){O::InitialNativeTestAccess::Fault(*root,fault);}
};
}
