#define main inherited_native_context_main
#include "../vertical-slice/native_context_chain.cpp"
#undef main
#include <nr/context/RepresentationPlanner.h>
#include "../representations/RepresentationFixture.h"
#include <iostream>
struct SourceReader
{
    Fixture& fixture;
    template<class T>auto Resolve(const C::MetadataRef<T>& reference)const{return fixture.context.Resolve(reference);}
    const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& reference)const
    {
        for(const auto& value:fixture.port->Candidates())if(reference.recordType.View()==C::AcquisitionCandidate::WireName&&
            value.header.record==reference.record&&value.header.revision==reference.revision)return &value;
        return nullptr;
    }
};
template<class Requirement>bool Purpose(Requirement& value,std::optional<C::Symbol> purpose)
{
    if constexpr(requires{value.consumerPurpose=purpose;}){value.consumerPurpose=purpose;return true;}
    else return false;
}
template<class Requirement>bool Target(Requirement& value,const C::AccessRequirements& access)
{
    if constexpr(requires{value.intent;value.targetAccess=access;})
    {value.intent=decltype(value.intent)::AllocateTarget;value.targetAccess=access;return true;}
    else return false;
}
template<class Path>bool TargetPath(const std::optional<Path>& value)
{
    if constexpr(requires{Path::TargetAllocation;})return value==Path::TargetAllocation;
    else return false;
}
template<class Key>bool TargetKey(Key& value)
{
    if constexpr(requires{value.intent;}){value.intent=decltype(value.intent)::AllocateTarget;return true;}
    else return false;
}
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<Fixture>(false,"evaluate",true,false,false,true,true,false,true,true,true);
    f->profile.sourceSchemas.Get(0)->domain=S::SourceSchemaDomain::NativeNgxAdapter;
    const auto qualified=f->Run();
    check(qualified.context&&qualified.certificate,"Native representation fixture has genuine context positive control");
    if(!qualified.context||!qualified.certificate)return 1;
    SourceReader reader{*f};const C::AcquisitionCandidate* color=nullptr;
    for(const auto& candidate:f->port->Candidates())
    {
        const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
        if(S::ResolveList(candidate.claims,reader,claims)&&claims&&claims->Size()&&claims->Get(0)->field.View()=="Color")color=&candidate;
    }
    if(!color)return 1;
    const auto reference=std::get<C::MetadataRef<C::ResourceView>>(color->payload.KnownPart()->value);
    const auto* resource=S::ResolveMetadata(reference,reader);
    auto requestStorage=std::make_unique<S::RepresentationRequirement>();auto& request=*requestStorage;request.requested=request.required=true;
    request.kind=C::SemanticKind::Color;request.field=Sym("color.domain");
    request.selected={Sym(C::AcquisitionCandidate::WireName),color->header.record,color->header.revision};
    request.mapping.source=resource->raster;request.mapping.target=resource->raster;
    request.mapping.scale=Known(C::Vec2{1,1});request.mapping.offset=Known(C::Vec2{0,0});
    request.mapping.mappingEvidence=Known(C::RecordReference{C::ContractId::C02,Sym(C::CanonicalFrameContext::WireName),
        qualified.context->header.record,qualified.context->header.revision});
    request.output=resource->descriptor;request.precision=Sym("RGBA16F");request.capabilities.Push(C::ResourceCapability::Sampled);
    S::SourceRepresentationFacts facts;facts.source=resource->identity;facts.precision=Known(request.precision);facts.capabilities=Known(request.capabilities);
    const auto derive=[&]{return S::DeriveStructuralRepresentationPlan(*qualified.context,*qualified.certificate,f->profile,request,facts,reader);};
    const auto baseline=derive();
    check(baseline.path==S::PreparationPath::StructuralOnly&&baseline.key&&baseline.key->purpose==f->profile.purpose,
          "existing planner purpose default preserved");
    check(Purpose(request,Sym("ColorModelInput")),"C12 can declare consumer purpose separately from C04 policy");
    const auto colorPlan=derive();
    check(colorPlan.key&&colorPlan.key->purpose==Sym("ColorModelInput")&&
          colorPlan.path==S::PreparationPath::StructuralOnly&&!colorPlan.sourceIdentity.contentRevision.IsKnown(),
          "structural C12 carries consumer purpose without current pixels");
    check(S::CertificateMatches(*qualified.certificate,*qualified.context,f->profile,reader)&&
          qualified.certificate->purpose==Sym("RENDER.NativeSemantics"),"consumer purpose never rewrites semantic policy or certificate");
    Purpose(request,Sym("OutputModelTarget"));const auto outputPlan=derive();
    check(outputPlan.key&&colorPlan.key&&outputPlan.key->purpose==Sym("OutputModelTarget")&&*outputPlan.key!=*colorPlan.key,
          "consumer purpose participates in complete plan equality");
    Purpose(request,C::Symbol{});
    check(derive().path==S::PreparationPath::Unavailable,"explicit empty consumer purpose refuses");
    Purpose(request,{});const auto old=derive();
    check(old.key&&baseline.key&&*old.key==*baseline.key,"absent consumer purpose retains original plan identity");
    Purpose(request,Sym("OutputModelTarget"));request.allocationClass=Sym("Native.ModelTarget.v1");
    request.capabilities.Push(C::ResourceCapability::Storage);facts.capabilities=Known(request.capabilities);
    const auto directTargetSource=derive();const auto consumeRequest=std::make_unique<S::RepresentationRequirement>(request);
    C::AccessRequirements access;access.device=resource->device;access.callbackScope=color->descriptiveLifetime.callbackScope;
    access.uses.Push(C::UsageKind::WriteExclusive);access.requiresExactContent=Known(false);
    access.retirementContract=Known(Sym("Native.TargetRetirement.v1"));
    check(Target(request,access),"explicit target-allocation intent exists");
    const auto sameDescriptor=derive();
    check(sameDescriptor.path==S::PreparationPath::StructuralOnly&&TargetPath(sameDescriptor.plannedPath)&&
          sameDescriptor.allocation&&sameDescriptor.evidence.conversions==0&&sameDescriptor.evidence.preparedLookups==0&&
          !sameDescriptor.prepared&&sameDescriptor.delta.Size()==0,"target intent wins over apparently direct source use");
    check(directTargetSource.key&&sameDescriptor.key&&*directTargetSource.key!=*sameDescriptor.key,
          "allocation intent participates in complete plan-key equality");
    request.capabilities={};request.capabilities.Push(C::ResourceCapability::Storage);
    C::BoundedList<C::ResourceCapability,4> sourceCaps;sourceCaps.Push(C::ResourceCapability::Sampled);facts.capabilities=Known(sourceCaps);
    const auto allocation=derive();
    check(allocation.path==S::PreparationPath::StructuralOnly&&TargetPath(allocation.plannedPath)&&
          allocation.allocation&&allocation.allocation->capabilities==request.capabilities&&
          allocation.access==access&&allocation.sourceIdentity==resource->identity&&
          !allocation.sourceIdentity.contentRevision.IsKnown()&&facts.capabilities.KnownPart()->value.Size()==1&&
          *facts.capabilities.KnownPart()->value.Get(0)==C::ResourceCapability::Sampled,
          "target Storage requirement never fabricates source UAV or current content");
    const auto targetRequestStorage=std::make_unique<S::RepresentationRequirement>(request);const auto& targetRequest=*targetRequestStorage;
    check(S::DeriveRepresentationPlan(*qualified.context,*qualified.certificate,f->profile,request,facts,{},reader).path==
          S::PreparationPath::Unavailable,"allocation-only planning cannot claim an executable source preparation");
    request=targetRequest;Purpose(request,Sym("ColorModelInput"));
    check(derive().path==S::PreparationPath::Unavailable,"target allocation cannot masquerade as consumer source purpose");
    request=targetRequest;Purpose(request,{});
    check(derive().path==S::PreparationPath::Unavailable,"target allocation needs explicit target purpose");
    request=targetRequest;auto wrong=access;wrong.device={};Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"target access needs exact source device");
    request=targetRequest;wrong=access;wrong.callbackScope={};Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"target access needs exact callback");
    request=targetRequest;wrong=access;wrong.queue=resource->device;Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"target cannot invent queue ownership from the device");
    request=targetRequest;wrong=access;wrong.uses.Push(C::UsageKind::CallbackRead);Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"target access cannot mix source read rights");
    request=targetRequest;wrong=access;wrong.requiresExactContent=Known(true);Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"allocation never requires existing target pixels");
    request=targetRequest;wrong=access;wrong.retirementContract={};Target(request,wrong);
    check(derive().path==S::PreparationPath::Unavailable,"target access needs declared retirement contract");
    request=targetRequest;request.consumerTransform=S::FusionRule{};
    check(derive().path==S::PreparationPath::Unavailable,"allocation cannot carry a consumer conversion declaration");
    request=targetRequest;request.capabilities={};request.capabilities.Push(C::ResourceCapability::Sampled);
    check(derive().path==S::PreparationPath::Unavailable,"writable target requires Storage capability explicitly");
    request=targetRequest;C::TransformStep step;step.kind=C::TransformKind::ColorTransfer;step.operation=Sym("fixture.encode");step.version=1;
    step.sourceUnits=Known(Sym("color"));step.targetUnits=Known(Sym("encoded"));step.alreadyApplied=Known(false);
    step.qualification=request.mapping.mappingEvidence;request.transforms.Push(step);
    check(derive().path==S::PreparationPath::Unavailable,"target allocation cannot carry a requested pixel transform");
    request=*consumeRequest;Target(request,access);request.allocationClass={};
    check(derive().path==S::PreparationPath::Unavailable,"target allocation needs an allocation class");
    NrRepresentationTests::Fixture contentFixture;auto contentKey=contentFixture.KeyFor();
    check(S::BuildPreparedContentKey(contentKey,contentFixture.source,contentFixture.target,
          contentFixture.frame,{},false,contentFixture.store).has_value(),"source-preparation content-key positive control");
    check(TargetKey(contentKey),"content key retains allocation intent");
    check(!S::BuildPreparedContentKey(contentKey,contentFixture.source,contentFixture.target,
          contentFixture.frame,{},false,contentFixture.store),"allocation-only plan refuses prepared source-content identity");
    std::cout<<"Native representation purpose: "<<count<<" checks, "<<failed<<" failures; synthetic CPU context only\n";
    return failed?1:0;
}
