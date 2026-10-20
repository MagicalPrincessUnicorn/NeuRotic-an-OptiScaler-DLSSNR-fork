#define main InheritedSampleProfileMain
#include "sample_profile.cpp"
#undef main
#include <nr/protocol/NativeTemporalBindingMapper.h>
static void UnknownInputs(SampleFixture& f)
{
    for(auto& use:f.request.representations)
    {
        auto view=*S::ResolveMetadata(use.currentSource,f.store);view.identity.contentRevision={};
        use.currentSource=f.store.Publish(view,C::OwnerDomain::Resource);use.currentContent={};
        auto plan=*S::ResolveMetadata(use.plan,f.store);plan.source=view.identity;plan.keys.content=view.identity;
        use.plan=f.store.Publish(plan,C::OwnerDomain::Context);
    }
}
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<SampleFixture>();const auto exact=P::BuildRecipe(f->request,f->store);
    check(exact.product&&P::BuildNativeBindingMap(exact.product->Value(),f->store).has_value(),"exact-content recipe and binding control");
    const auto initial=P::PrepareNativeProfile(f->request,f->store);
    check(initial.projection&&initial.projection->Value().profileCertificate.purpose.View()=="RENDER.NativeStructuralBindings",
          "nonexecuting profile certificate is explicitly structural");
    if(exact.product)
    {
        auto reused=exact.product->Value();reused.profileCertificate.purpose=Sym("RENDER.NativeStructuralBindings");
        check(!P::BuildNativeBindingMap(reused,f->store),"structural certificate cannot enter Native binding map");
    }
    UnknownInputs(*f);const auto prepared=P::PrepareNativeProfile(f->request,f->store);
    check(prepared.projection.has_value(),"unknown content permits nonexecuting profile preparation");
    check(!P::BuildRecipe(f->request,f->store).product,"unknown contents still refuse executable recipe");
    check(!P::Qualify(f->request,f->store).Empty(),"executable qualification remains strict");
    auto unknown=f->source;unknown.identity.contentRevision={};
    check(!S::BuildPreparedContentKey(f->KeyFor(),unknown,f->target,f->frame,{},false,f->store),"unknown source cannot obtain prepared-content key");
    auto mismatched=std::make_unique<SampleFixture>();UnknownInputs(*mismatched);
    mismatched->request.representations.Get(0)->currentContent=Known(C::ContentRevision{99});
    check(!P::PrepareNativeProfile(mismatched->request,mismatched->store).projection,"structural profile cannot accept an invented current revision");
    auto incomplete=std::make_unique<SampleFixture>();UnknownInputs(*incomplete);
    auto& bad=*incomplete->request.representations.Get(0);auto view=*S::ResolveMetadata(bad.currentSource,incomplete->store);
    view.descriptor.api={};bad.currentSource=incomplete->store.Publish(view,C::OwnerDomain::Resource);
    check(!P::PrepareNativeProfile(incomplete->request,incomplete->store).projection,"structural profile requires complete descriptor");
    auto noGeneration=std::make_unique<SampleFixture>();UnknownInputs(*noGeneration);
    auto& unversioned=*noGeneration->request.representations.Get(0);auto unversionedView=*S::ResolveMetadata(unversioned.currentSource,noGeneration->store);
    unversionedView.identity.resourceGeneration={};unversioned.currentSource=noGeneration->store.Publish(unversionedView,C::OwnerDomain::Resource);
    auto unversionedPlan=*S::ResolveMetadata(unversioned.plan,noGeneration->store);unversionedPlan.source=unversionedView.identity;
    unversionedPlan.keys.content=unversionedView.identity;unversioned.plan=noGeneration->store.Publish(unversionedPlan,C::OwnerDomain::Context);
    check(!P::PrepareNativeProfile(noGeneration->request,noGeneration->store).projection,"structural profile requires resource generation");
    auto foreign=std::make_unique<SampleFixture>();auto& resource=*foreign->request.representations.Get(0);
    resource.currentSource=foreign->store.Publish(*S::ResolveMetadata(resource.currentSource,foreign->store),C::OwnerDomain::Context);
    check(!P::PrepareNativeProfile(foreign->request,foreign->store).projection,"structural resource publication must retain Resource ownership");
    auto cached=std::make_unique<SampleFixture>();UnknownInputs(*cached);
    auto& cachedUse=*cached->request.representations.Get(0);const auto& cachedPlan=*S::ResolveMetadata(cachedUse.plan,cached->store);
    C::PreparedView cache;cache.header=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Resource,919);
    cache.header.scope=cachedPlan.header.scope;cache.plan=P::Reference<C::ContractId::C12>(cachedPlan);cache.keys=cachedPlan.keys;
    cache.view=cached->store.Publish(cached->target,C::OwnerDomain::Resource);
    cachedUse.prepared=Known(cached->store.Publish(cache,C::OwnerDomain::Resource));
    check(!P::PrepareNativeProfile(cached->request,cached->store).projection,"unknown input revision cannot justify prepared-content reuse");
    auto current=std::make_unique<SampleFixture>();auto fresh=P::BuildRecipe(current->request,current->store);
    check(fresh.product&&fresh.product->Value().profileCertificate.purpose.View()=="RENDER.NativeBindings",
          "fresh exact-content recipe emits execution-purpose certificate");
    std::cout<<"Structural profile: "<<count<<" checks, "<<failed<<" failures; CPU metadata only\n";
    return failed?1:0;
}
