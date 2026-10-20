#pragma once
#include "../context/ContextFixtureStore.h"
#include <nr/orchestration/RoutingTypes.h>
namespace NrRoutingTests
{
using namespace NrTest002;
namespace O=Neurotic::Orchestration;
struct Fixture
{
    NrContextTests::Store store;O::RoutingInput input;O::CompletePlan a,b;
    C::BoundedList<O::OperationalFact,16> factsA,factsB;
    C::MetadataRef<C::GenerationVector> generations;
    C::OwnerValueReference Owner(C::OwnerDomain owner,std::uint64_t id)
    {return {owner,Sym("fixture.owner"),{1,0},Key("owner",id),1};}
    Fixture()
    {
        input.scope.key=Key("stream.scope",1);input.intent.source=Owner(C::OwnerDomain::Configuration,1);
        input.mandatoryRR=Known(false);input.mandatoryFG=Known(false);
        input.incumbent.source=Owner(C::OwnerDomain::StreamCoordinator,2);input.incumbent.structurallyCurrent=Known(true);
        input.frame=NrContextTests::Frame();input.provenRealFrame=Known(true);input.repeatedPresent=Known(false);
        input.frameSource=Owner(C::OwnerDomain::IdentityRegistry,3);input.frameSource.recordType=Sym("NR.C14.FrameProgress");input.frameScope=input.scope;
        C::GenerationVector vector;vector.Insert(Gen(C::IdentityKind::ProviderIncarnation,"provider",1));
        generations=store.Publish(vector,C::OwnerDomain::IdentityRegistry);
        Make(a,factsA,1,"Native");Make(b,factsB,2,"Present");Publish();
    }
    void Make(O::CompletePlan& p,C::BoundedList<O::OperationalFact,16>& facts,std::uint64_t id,std::string_view strategy)
    {
        p.offer=Owner(C::OwnerDomain::Strategy,100+id);p.anchor.plan=Key("plan",id);p.anchor.scope=input.scope;
        p.anchor.strategy=Sym(strategy);p.anchor.profile={Sym("model"),Sym("temporal"),1};
        p.anchor.placement=id==1?C::Placement::NativeBefore:C::Placement::UnifiedPresent;
        C::StructuralSignature sig;sig.schema=Sym("ORCH.CompletePlan");sig.generations=generations;
        C::BoundedList<C::RecordKey,32> dependencies;dependencies.Push(Key("strategy.version",id));
        sig.structuralDependencies=NrContextTests::PublishList(store,dependencies);
        p.anchor.signature=store.Publish(sig,C::OwnerDomain::Strategy);
        p.context={Sym(C::CanonicalFrameContext::WireName),Key("context",1),1};
        C::QualificationCertificate cert;cert.header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,20+id);
        cert.header.scope=input.scope;cert.context=p.context;cert.profile=p.anchor.profile;cert.purpose=Sym("render");
        cert.eligibility=C::Eligibility::Eligible;cert.signature=p.anchor.signature;
        p.semantic=store.Publish(cert,C::OwnerDomain::Context);
        p.features.Push(Sym("NR"));p.evidenceRank=Known(uint32_t{0});p.measuredCost=Known(1.0);
        p.multipass=Known(std::optional<C::ContractRef<C::ContractId::C09>>{});
        p.fgSubplan=Known(std::optional<C::RecordReference>{});p.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);
        p.boundary.episode=input.frame.episodeId;p.currentContent=Known(C::ContentRevision{1});
        for(const auto field:{"implementation","api","resource","lifetime","identity","handoff","availability","freshness","health","warmup","rr","fg"})
        {
            O::OperationalFact fact;fact.field=Sym(field);fact.plan=p.anchor.plan;fact.context=p.context;fact.generations=generations;fact.value=Known(true);
            C::OwnerDomain owner=C::OwnerDomain::Strategy;
            const std::string_view f=field;
            if(f=="api"||f=="resource"||f=="lifetime")owner=C::OwnerDomain::Resource;
            if(f=="identity")owner=C::OwnerDomain::IdentityRegistry;
            if(f=="handoff")owner=C::OwnerDomain::Finalizer;
            if(f=="freshness")owner=C::OwnerDomain::Context;
            if(f=="rr")owner=C::OwnerDomain::RayReconstruction;
            if(f=="fg")owner=C::OwnerDomain::FrameGeneration;
            fact.source=Owner(owner,200+id);fact.reason.rule=Sym("owner."+std::string(field));
            fact.reason.reason.ownerCode=Sym("owner.refusal");facts.Push(fact);
        }
    }
    void Publish()
    {
        a.facts=NrContextTests::PublishList(store,factsA);b.facts=NrContextTests::PublishList(store,factsB);
        input.offers={};input.offers.Push(store.Publish(a,C::OwnerDomain::Strategy));input.offers.Push(store.Publish(b,C::OwnerDomain::Strategy));
    }
    void Set(C::BoundedList<O::OperationalFact,16>& facts,std::string_view field,C::OptionalFact<bool> value)
    {for(auto& fact:facts)if(fact.field.View()==field)fact.value=value;}
    void Lock(){input.intent.mode=Sym("LockCurrent");input.incumbent.current=a.anchor;input.incumbent.wasLocked=true;}
};
inline bool Chose(const O::ShadowDecision& d,std::uint64_t id){return d.valid&&d.proposed&&d.proposed->plan==Key("plan",id);}
inline bool Reason(const O::ShadowDecision& d,std::string_view text)
{for(const auto& rule:d.rules)if(rule.View()==text)return true;return false;}
}
