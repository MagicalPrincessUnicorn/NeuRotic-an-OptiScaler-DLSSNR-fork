#include "native_fixture.h"
#include "../identity/IdentityTestSupport.h"
#if __has_include(<nr/lifecycle/NativeAllocationRecord.h>)
#include <nr/lifecycle/NativeAllocationRecord.h>
#define NR_ALLOCATION_RECORD 1
#endif
using namespace NrTest002;
struct RejectPublication
{
    NrContextTests::Store& reader;
    template<class T>C::MetadataRef<T> Publish(const T&,C::OwnerDomain){return {};}
    template<class T>auto Resolve(const C::MetadataRef<T>& ref)const{return reader.Resolve(ref);}
};
struct ForeignLineagePublication
{
    NrContextTests::Store& reader;
    template<class T>C::MetadataRef<T> Publish(const T& value,C::OwnerDomain owner)
    {return reader.Publish(value,std::is_same_v<T,Neurotic::Context::TransformLineage>?C::OwnerDomain::Context:owner);}
    template<class T>auto Resolve(const C::MetadataRef<T>& ref)const{return reader.Resolve(ref);}
};
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-allocation",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_ALLOCATION_RECORD
    namespace L=Neurotic::Lifecycle;namespace S=Neurotic::Context;namespace P=Neurotic::Protocol;
    auto fixture=std::make_unique<NativeTests::Fixture>();
    auto host=std::make_unique<L::HostIdentityDomain>(Sym("native.allocation.test"));
    const auto opened=host->OpenSession(LifeFixture::Event(C::OwnerDomain::Session,LifeFixture::Key("session"),1));
    const auto owner=LifeFixture::Binding(C::OwnerDomain::Resource,"allocation.slot");
    const auto resource=host->BindResource(owner);const auto representation=host->BindRepresentation(owner);
    const auto event=[&](std::uint64_t n){return LifeFixture::Event(owner.owner,owner.subject,n);};
    L::NativeAllocationRecord allocation(*resource,*representation,owner);
    int nativeObject=0;auto* native=reinterpret_cast<ID3D12Resource*>(&nativeObject);
    r.Check("T16.allocation-created-once",allocation.Created(native,fixture->target,event(2)),true,"NativeAllocationRecord");
    r.Check("T16.allocation-no-invented-content",!allocation.View()->identity.contentRevision.IsKnown(),true,"NativeAllocationRecord");
    r.Check("T16.allocation-no-pointer-reidentity",!allocation.Created(native,fixture->target,event(3)),true,"NativeAllocationRecord");
    r.Check("T16.allocation-write-actual-event",allocation.Written(native,event(4)),true,"NativeAllocationRecord");
    const auto first=*allocation.View();
    r.Check("T16.allocation-content-complete",S::CompleteContent(first),true,"NativeAllocationRecord");
    r.Check("T16.allocation-duplicate-event-refused",!allocation.Written(native,event(4)),true,"NativeAllocationRecord");
    r.Check("T16.allocation-content-preserved-on-reject",allocation.View()->identity==first.identity,true,"NativeAllocationRecord");
    r.Check("T16.allocation-write-advances-only-content",allocation.Written(native,event(5))&&
        S::SameResourceStructure(first.identity,allocation.View()->identity)&&
        !S::SameContent(first.identity.contentRevision,allocation.View()->identity.contentRevision),true,"NativeAllocationRecord");
    int other=0;
    r.Check("T16.allocation-wrong-native-refused",!allocation.Written(reinterpret_cast<ID3D12Resource*>(&other),event(6)),true,"NativeAllocationRecord");
    const auto use=*fixture->request.representations.Get(0);const auto plan=*S::ResolveMetadata(use.plan,fixture->store);
    const auto source=*S::ResolveMetadata(use.currentSource,fixture->store);
    const auto resultHeader=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Resource,92);
    const auto before=*allocation.View();
    const auto prepared=allocation.PublishPrepared(plan,source,fixture->request.currentContext,
        fixture->request.evaluation,{},resultHeader,fixture->store);
    r.Check("T30.allocation-prepared-exact-plan",prepared&&prepared->plan.record==plan.header.record&&
        prepared->keys==plan.keys,true,"NativeAllocationRecord.PublishPrepared");
    auto targetPlan=plan;targetPlan.purpose=Sym("OutputModelTarget");
    r.Check("T30.allocation-target-not-source-preparation",!allocation.PublishPrepared(targetPlan,source,
        fixture->request.currentContext,fixture->request.evaluation,{},resultHeader,fixture->store),
        true,"NativeAllocationRecord.PublishPrepared");
    auto preparedUse=use;preparedUse.prepared=Known(fixture->store.Publish(fixture->Prepared(plan),C::OwnerDomain::Resource));
    r.Check("T30.profile-source-preparation-positive",P::LineageMatches(preparedUse,plan,fixture->store),true,"LineageMatches");
    preparedUse.purpose=P::Purpose::OutputModelTarget;preparedUse.kind=Sym("ModelTarget");
    preparedUse.prepared=Known(fixture->store.Publish(fixture->Prepared(targetPlan),C::OwnerDomain::Resource));
    r.Check("T30.profile-target-not-prepared-input",!P::LineageMatches(preparedUse,targetPlan,fixture->store),true,"LineageMatches");
    auto wrong=source;wrong.identity.contentRevision=Known(C::ContentRevision{777});
    r.Check("T30.allocation-stale-source-refused",!allocation.PublishPrepared(plan,wrong,fixture->request.currentContext,
        fixture->request.evaluation,{},resultHeader,fixture->store),true,"NativeAllocationRecord.PublishPrepared");
    r.Check("T30.allocation-association-current",allocation.Matches(plan,fixture->request.currentContext,
        fixture->request.evaluation,before.identity),true,"NativeAllocationRecord.Matches");
    r.Check("T30.allocation-wrong-evaluation-refused",!allocation.Matches(plan,fixture->request.currentContext,
        Id<C::EvaluationId>("wrong",1),before.identity),true,"NativeAllocationRecord.Matches");
    allocation.Written(native,event(7));
    r.Check("T30.allocation-write-invalidates-prepared",!allocation.Matches(plan,fixture->request.currentContext,
        fixture->request.evaluation,before.identity),true,"NativeAllocationRecord.Matches");
    auto lineagePlan=plan;
    C::TransformStep inherited;inherited.kind=C::TransformKind::ExposureUndo;inherited.operation=Sym("undo");inherited.version=1;
    inherited.sourceUnits=Known(Sym("exposed"));inherited.targetUnits=Known(Sym("linear"));
    inherited.qualification=Known(fixture->meaning);inherited.alreadyApplied=Known(false);
    auto added=inherited;added.kind=C::TransformKind::ColorTransfer;added.operation=Sym("tone");
    S::TransformLineage delta,total,required,prior;delta.Push(added);required.Push(inherited);required.Push(added);
    lineagePlan.transforms=NrContextTests::PublishList(fixture->store,delta);
    inherited.alreadyApplied=Known(true);added.alreadyApplied=Known(true);prior.Push(inherited);total.Push(added);
    L::NativePreparationLineage lineage{NrContextTests::PublishList(fixture->store,prior),
        NrContextTests::PublishList(fixture->store,required),total};
    const auto inheritedResult=allocation.PublishPrepared(lineagePlan,source,fixture->request.currentContext,
        fixture->request.evaluation,lineage,resultHeader,fixture->store);
    r.Check("T30.allocation-preserves-inherited-lineage",inheritedResult&&inheritedResult->appliedTransforms.count==2,true,"NativeAllocationRecord.PublishPrepared");
    // A real resolver with a deliberately refusing publication boundary.
    RejectPublication reject{fixture->store};
    const auto unavailable=allocation.PublishPrepared(plan,source,fixture->request.currentContext,
        fixture->request.evaluation,{},resultHeader,reject);
    r.Check("T30.allocation-publication-failure-refused",!unavailable,true,"NativeAllocationRecord.PublishPrepared");
    ForeignLineagePublication foreign{fixture->store};
    const auto wrongOwner=allocation.PublishPrepared(lineagePlan,source,fixture->request.currentContext,
        fixture->request.evaluation,lineage,resultHeader,foreign);
    r.Check("T30.allocation-executed-lineage-owner",!wrongOwner,true,"NativeAllocationRecord.PublishPrepared");
    allocation.Revoke();
    r.Check("T16.allocation-revoke-keeps-description",allocation.View()!=nullptr&&!allocation.Written(native,event(8)),true,"NativeAllocationRecord");
    const auto retainedOwner=LifeFixture::Binding(C::OwnerDomain::Resource,"retained.slot");
    const auto retainedResource=host->BindResource(retainedOwner);
    const auto retainedRepresentation=host->BindRepresentation(retainedOwner);
    L::NativeAllocationRecord retained(*retainedResource,*retainedRepresentation,retainedOwner);
    const auto observed=LifeFixture::Event(retainedOwner.owner,retainedOwner.subject,50);
    const auto Register=[]<class Record>(Record& record,ID3D12Resource* physical,const C::ResourceView& view,const L::OwnerEvent& evidence){
        if constexpr(requires{record.Registered(physical,view,evidence);})return record.Registered(physical,view,evidence);
        else return false;
    };
    const bool registered=Register(retained,native,fixture->target,observed);
    r.Check("T16.allocation-first-observed-registration",registered,true,"NativeAllocationRecord.Registered");
    r.Check("T16.allocation-registration-clears-supplied-content",registered&&!retained.View()->identity.contentRevision.IsKnown()&&
        !S::SameResourceStructure(retained.View()->identity,fixture->target.identity),true,"NativeAllocationRecord.Registered");
    r.Check("T16.allocation-registration-cannot-become-creation",registered&&!retained.Created(native,fixture->target,
        LifeFixture::Event(retainedOwner.owner,retainedOwner.subject,51)),true,"NativeAllocationRecord.Registered");
    host->CloseSession(opened.value.KnownPart()->value);
#else
    r.Check("T16.allocation-production",false,true,"NativeAllocationRecord");
#endif
    return r.Finish();});
}
