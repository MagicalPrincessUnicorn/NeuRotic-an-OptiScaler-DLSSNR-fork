#pragma once
#include "RoutingTypes.h"
#include <algorithm>
#include <array>
#include <limits>

namespace Neurotic::Orchestration
{
inline bool Yes(const C::OptionalFact<bool>& fact){return Context::Established(fact)&&fact.KnownPart()->value;}
inline bool No(const C::OptionalFact<bool>& fact){return Context::Established(fact)&&!fact.KnownPart()->value;}
inline C::PolicyReason Reason(std::string_view rule,bool hard,C::ReasonCategory category=C::ReasonCategory::InvalidInvariant)
{
    C::PolicyReason reason;reason.rule=Symbol(rule);reason.hard=hard;reason.reason.category=category;
    reason.reason.ownerCode=reason.rule;return reason;
}
template<class Store>bool ValidSignature(const PlanAnchor& anchor,const Store& store)
{
    if(!Context::ValidValues(anchor)||!anchor.scope.key||anchor.strategy.Empty())return false;
    if(anchor.nativeDelivery)
    {
        const auto* delivery=Context::ResolveMetadata(*anchor.nativeDelivery,store);
        if(anchor.nativeDelivery->owner!=C::OwnerDomain::StreamCoordinator||
            anchor.nativeDelivery->schemaVersion!=C::SchemaVersion{1,0}||!delivery||
            delivery->profile!=anchor.profile||delivery->placement!=anchor.placement)return false;
    }
    const auto* sig=Context::ResolveMetadata(anchor.signature,store);
    if(!sig||sig->schema.View()!="ORCH.CompletePlan"||sig->version!=PolicyVersion||!Context::ResolveMetadata(sig->generations,store))return false;
    const C::BoundedList<C::RecordKey,32>* dependencies=nullptr;
    if(!Context::ResolveList(sig->structuralDependencies,store,dependencies)||!dependencies||dependencies->Size()==0)return false;
    for(std::size_t i=0;i<dependencies->Size();++i)for(std::size_t j=0;j<i;++j)
        if(*dependencies->Get(i)==*dependencies->Get(j))return false;
    return true;
}
template<class Store>bool SameSignature(const PlanAnchor& a,const PlanAnchor& b,const Store& store)
{
    if(!ValidSignature(a,store)||!ValidSignature(b,store))return false;
    const auto* x=Context::ResolveMetadata(a.signature,store);const auto* y=Context::ResolveMetadata(b.signature,store);
    const auto* gx=Context::ResolveMetadata(x->generations,store);const auto* gy=Context::ResolveMetadata(y->generations,store);
    if(*gx!=*gy)return false;
    const C::BoundedList<C::RecordKey,32>* dx=nullptr;const C::BoundedList<C::RecordKey,32>* dy=nullptr;
    if(!Context::ResolveList(x->structuralDependencies,store,dx)||!Context::ResolveList(y->structuralDependencies,store,dy)||dx->Size()!=dy->Size())return false;
    for(const auto& key:*dx)if(std::find(dy->begin(),dy->end(),key)==dy->end())return false;
    return true;
}
template<class Store>bool SameAnchor(const PlanAnchor& a,const PlanAnchor& b,const Store& store)
{
    return a.plan==b.plan&&a.scope==b.scope&&a.strategy==b.strategy&&a.profile==b.profile&&a.placement==b.placement&&
        a.nativeDelivery==b.nativeDelivery&&SameSignature(a,b,store);
}
inline bool ExpectedOwner(std::string_view field,C::OwnerDomain owner)
{
    if(field=="implementation"||field=="availability"||field=="health"||field=="warmup")return owner==C::OwnerDomain::Strategy;
    if(field=="api"||field=="resource"||field=="lifetime")return owner==C::OwnerDomain::Resource;
    if(field=="identity")return owner==C::OwnerDomain::IdentityRegistry;
    if(field=="handoff")return owner==C::OwnerDomain::Finalizer;
    if(field=="hostReturn")return owner==C::OwnerDomain::Provider;
    if(field=="freshness")return owner==C::OwnerDomain::Context;
    if(field=="rr")return owner==C::OwnerDomain::RayReconstruction;
    if(field=="fg")return owner==C::OwnerDomain::FrameGeneration;
    return false;
}
template<class Store>const OperationalFact* Fact(const CompletePlan& plan,std::string_view field,const Store& store)
{
    const C::BoundedList<OperationalFact,16>* facts=nullptr;
    if(!Context::ResolveList(plan.facts,store,facts)||!facts)return nullptr;
    const auto* signature=Context::ResolveMetadata(plan.anchor.signature,store);if(!signature)return nullptr;
    const auto* generations=Context::ResolveMetadata(signature->generations,store);if(!generations)return nullptr;
    const OperationalFact* result=nullptr;
    for(const auto& fact:*facts)if(fact.field.View()==field)
    {
        const auto* current=Context::ResolveMetadata(fact.generations,store);
        if(result||!ExpectedOwner(field,fact.source.owner)||fact.plan!=plan.anchor.plan||fact.context!=plan.context||!current||*current!=*generations)return nullptr;
        result=&fact;
    }
    return result;
}
inline bool HasFeature(const CompletePlan& plan,const C::Symbol& feature)
{
    return plan.anchor.strategy==feature||std::find(plan.features.begin(),plan.features.end(),feature)!=plan.features.end();
}
enum class Admission{Eligible,HardInvalid,Temporary};
struct AdmissionResult{Admission state=Admission::HardInvalid;C::PolicyReason reason;bool healthy=false;};
template<class Store>AdmissionResult Admit(const RoutingInput& input,const CompletePlan& plan,const Store& store)
{
    const auto reject=[](std::string_view rule,Admission state){return AdmissionResult{state,Reason(rule,state==Admission::HardInvalid),false};};
    if(!Context::ValidValues(plan)||plan.offer.owner!=C::OwnerDomain::Strategy||!ValidSignature(plan.anchor,store)||plan.anchor.scope!=input.scope)
        return reject("ORCH.R1.MalformedOrScope",Admission::HardInvalid);
    for(const auto field:{"implementation","api","resource","lifetime","identity"})
    {
        const auto* fact=Fact(plan,field,store);
        if(!fact)return reject("ORCH.R1.MissingOwnerFact",Admission::HardInvalid);
        if(!Yes(fact->value)){auto reason=fact->reason;reason.hard=true;return {Admission::HardInvalid,reason,false};}
    }
    const auto* semantic=Context::ResolveMetadata(plan.semantic,store);
    if(!semantic||plan.semantic.owner!=C::OwnerDomain::Context||semantic->header.owner!=C::OwnerDomain::Context||
       semantic->header.scope!=input.scope||semantic->context!=plan.context||semantic->profile!=plan.anchor.profile)
        return reject("ORCH.R1.SemanticBinding",Admission::HardInvalid);
    // NFC scopes eligibility/reasons to required profile fields. Its retained context
    // contradictions can concern unrelated fields and are not a global veto.
    const C::BoundedList<C::PolicyReason,16>* semanticReasons=nullptr;
    if(!Context::ResolveList(semantic->reasons,store,semanticReasons))return reject("ORCH.R1.SemanticReasonsMissing",Admission::HardInvalid);
    if(semanticReasons)for(const auto& reason:*semanticReasons)
        if(reason.hard&&(reason.reason.category==C::ReasonCategory::InvalidInvariant||reason.reason.category==C::ReasonCategory::MissingImplementation))
            return {Admission::HardInvalid,reason,false};
    if(!Context::Established(plan.multipass)||!Context::Established(plan.fgSubplan)||!Context::Established(plan.boundary.kind))
        return reject("ORCH.R1.IncompletePlan",Admission::HardInvalid);
    const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* sources=nullptr;
    const C::BoundedList<C::ContractRef<C::ContractId::C12>,32>* preparations=nullptr;
    if(!Context::ResolveList(plan.sourceBundle,store,sources)||!Context::ResolveList(plan.preparations,store,preparations))
        return reject("ORCH.R1.MissingPlanMetadata",Admission::HardInvalid);
    for(const auto& required:input.intent.require)if(!HasFeature(plan,required))return reject("ORCH.R2.Require",Admission::HardInvalid);
    for(const auto& forbidden:input.intent.forbid)if(HasFeature(plan,forbidden))return reject("ORCH.R2.Forbid",Admission::HardInvalid);
    if(input.intent.mode.View()=="FixedLegacy"&&(!input.intent.fixedStrategy||plan.anchor.strategy!=*input.intent.fixedStrategy||
        (input.intent.fixedPlacement&&plan.anchor.placement!=*input.intent.fixedPlacement)))return reject("ORCH.R2.FixedLegacy",Admission::HardInvalid);
    for(const auto field:{"handoff","rr","fg"})
    {
        if(std::string_view(field)=="rr"&&!Yes(input.mandatoryRR))continue;
        if(std::string_view(field)=="fg"&&!Yes(input.mandatoryFG))continue;
        std::string_view required=field;
        if(required=="handoff"&&plan.anchor.nativeDelivery)
        {
            const auto* delivery=Context::ResolveMetadata(*plan.anchor.nativeDelivery,store);
            if(!delivery)return reject("ORCH.R3.DeliveryMissing",Admission::HardInvalid);
            if(delivery->kind==C::NativeDeliveryKind::HostReturn)required="hostReturn";
        }
        const auto* fact=Fact(plan,required,store);
        if(!fact)return reject("ORCH.R3.MissingHostFact",Admission::HardInvalid);
        if(!Yes(fact->value)){auto reason=fact->reason;reason.hard=true;return {Admission::HardInvalid,reason,false};}
    }
    std::optional<C::PolicyReason> temporaryRestriction;
    for(const auto& soft:plan.softRestrictions)
    {
        const bool allowedCategory=soft.reason.category==C::ReasonCategory::CoverageRestriction||soft.reason.category==C::ReasonCategory::QualityUnqualified||soft.reason.category==C::ReasonCategory::Untested;
        if(soft.hard||!allowedCategory)return {Admission::HardInvalid,soft,false};
        bool enabled=false;
        if(input.intent.tier.View()!="Basic")for(const auto& opt:input.intent.optIns)if(opt.rule==soft.rule&&opt.scope==input.scope)
        {
            const auto* a=Context::ResolveMetadata(opt.generations,store);
            const auto* sig=Context::ResolveMetadata(plan.anchor.signature,store);const auto* b=Context::ResolveMetadata(sig->generations,store);
            if(a&&b&&*a==*b)enabled=true;
        }
        if(!enabled&&!temporaryRestriction)temporaryRestriction=soft;
    }
    // Temporary absence must never conceal a later hard intent, host or invariant failure.
    if(semantic->eligibility!=C::Eligibility::Eligible)return reject("ORCH.R1.SemanticUnavailable",Admission::Temporary);
    if(temporaryRestriction)return {Admission::Temporary,*temporaryRestriction,false};
    for(const auto field:{"availability","freshness","warmup"})
    {
        const auto* fact=Fact(plan,field,store);
        if(!fact)return reject("ORCH.CurrentUse.Unknown",Admission::Temporary);
        if(!Yes(fact->value))return {Admission::Temporary,fact->reason,false};
    }
    const auto* health=Fact(plan,"health",store);
    return {Admission::Eligible,{},health&&Yes(health->value)};
}
inline std::size_t Preference(const RoutingIntent& intent,const CompletePlan& plan)
{
    for(std::size_t i=0;i<intent.prefer.Size();++i)if(HasFeature(plan,*intent.prefer.Get(i)))return i;
    return intent.prefer.Size();
}
inline bool ValidIntent(const RoutingIntent& intent)
{
    if(!Context::ValidValues(intent)||intent.source.owner!=C::OwnerDomain::Configuration||!intent.promotionFrames||
       intent.promotionFrames>1000000||intent.warmupFrames>1000000)return false;
    if(intent.mode.View()!="Shadow"&&intent.mode.View()!="FixedLegacy"&&intent.mode.View()!="LockCurrent")return false;
    if(intent.tier.View()!="Basic"&&intent.tier.View()!="Advanced"&&intent.tier.View()!="Unrestricted")return false;
    for(const auto* list:{&intent.require,&intent.forbid,&intent.prefer})
        for(std::size_t i=0;i<list->Size();++i){if(list->Get(i)->Empty())return false;for(std::size_t j=0;j<i;++j)if(*list->Get(i)==*list->Get(j))return false;}
    return intent.mode.View()!="FixedLegacy"||intent.fixedStrategy.has_value();
}
}
