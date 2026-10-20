#include "../vertical-slice/native_fixture.h"
#include <nr/context/NativeSampleQualification.h>
namespace S=Neurotic::Context;namespace P=Neurotic::Protocol;namespace C=Neurotic::Contracts;
using namespace NrTest002;
struct SampleFixture:NativeTests::Fixture
{
    C::MetadataRef<C::NativeSampleIdentityV1> sampleRef;
    explicit SampleFixture(bool nativeBefore=true):NativeTests::Fixture(false,nativeBefore)
    {
        C::NativeSampleIdentityV1 sample;sample.session=frame.sessionId.KnownPart()->value;
        sample.stream=frame.renderStreamId.KnownPart()->value;sample.view=frame.viewId.KnownPart()->value;
        sample.feature=Id<C::ObjectIncarnation>("feature",1);sample.ingress=Id<C::ObjectIncarnation>("ingress",1);
        sample.producerOrdinal=1;sample.callback=Ev(99).record;sampleRef=store.Publish(sample,C::OwnerDomain::Provider);
        frame.nativeSample=sampleRef;frame.baseRealFrameId={};
        auto context=*S::ResolveMetadata(request.context,store);context.frame=store.Publish(frame,C::OwnerDomain::Provider);
        auto motion=*S::ResolveOptional(context.motion,store);motion.framePair={};
        []<class T>(T& value,auto reference){if constexpr(requires{value.nativeSample;})value.nativeSample=reference;}(motion,sampleRef);
        context.motion=Known(store.Publish(motion));request.context=store.Publish(context);
        auto semantic=*S::ResolveMetadata(request.semantic,store);semantic.maximumAge={};
        semantic.nativeFreshness=S::NativeFreshness(sampleRef,sample);request.semantic=store.Publish(semantic);
        for(auto& use:request.representations)
            []<class T>(T& value,auto reference){if constexpr(requires{value.nativeSample;})value.nativeSample=reference;}(use,sampleRef);
        C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::HostReturn;delivery.profile=request.requestedProfile;
        delivery.placement=request.placement;delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
        delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
        C::PlanCommit plan;plan.header=NrContextTests::Header(C::ContractId::C05,C::OwnerDomain::StreamCoordinator,77);
        plan.decision={Sym(C::RoutingDecision::WireName),Key("decision.fixture"),1};
        plan.nativeDelivery=store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
        request.externalPlan=P::Reference<C::ContractId::C05>(plan);
        []<class T>(T& value,auto reference){if constexpr(requires{value.nativePlan;})value.nativePlan=reference;}
            (request,store.Publish(plan,C::OwnerDomain::StreamCoordinator));
    }
};
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<SampleFixture>();
    const auto prepared=P::PrepareNativeProfile(f->request,f->store);
    if(!prepared.projection)std::cerr<<prepared.reason.View()<<'\n';
    check(prepared.projection.has_value(),"N10 sample profile prepares with truthful raw Native motion");
    const auto built=P::BuildRecipe(f->request,f->store);
    if(!built.product)std::cerr<<built.reason.View()<<'\n';
    check(built.product.has_value(),"N10 full recipe builds without synthetic base frame");
    check(built.product&&built.product->Value().recipe.nativeSample==f->frame.nativeSample&&built.product->Value().recipe.nativeDelivery.has_value(),
          "N01 recipe binds exact sample and committed delivery");
    auto after=std::make_unique<SampleFixture>(false);
    const auto afterRecipe=P::BuildRecipe(after->request,after->store);
    check(!afterRecipe.product&&afterRecipe.reason==Sym("Native.PostSrOutputPending"),
          "Native After refuses without authentic post-SR publication");
    check(prepared.projection&&prepared.projection->Value().profileCertificate.nativeFreshness.has_value()&&
          !prepared.projection->Value().profileCertificate.maximumAge.IsKnown(),"N10 profile C04 retains sample freshness");
    const auto key=f->KeyFor();
    check(S::BuildPreparedContentKey(key,f->source,f->target,f->frame,{},false,f->store).has_value(),
          "N10 content key qualifies exact Native sample");
    check(!S::BuildPreparedContentKey(key,f->source,f->target,f->frame,{},true,f->store),
          "N09 real frame-pair conversion still refuses missing association");
    auto stale=f->frame;++stale.nativeSample->revision;
    check(!S::BuildPreparedContentKey(key,f->source,f->target,stale,{},false,f->store),"N12 stale sample cannot reuse content");
    auto wrong=std::make_unique<SampleFixture>();
    []<class T>(T& value){if constexpr(requires{value.nativeSample;})++value.nativeSample->revision;}
        (*wrong->request.representations.Get(0));
    check(!P::PrepareNativeProfile(wrong->request,wrong->store).projection,"N11 other-sample representation refused");
    auto motionWrong=std::make_unique<SampleFixture>();auto context=*S::ResolveMetadata(motionWrong->request.context,motionWrong->store);
    auto motion=*S::ResolveOptional(context.motion,motionWrong->store);
    []<class T>(T& value){if constexpr(requires{value.nativeSample;})value.nativeSample.reset();}(motion);
    context.motion=Known(motionWrong->store.Publish(motion));motionWrong->request.context=motionWrong->store.Publish(context);
    check(!P::PrepareNativeProfile(motionWrong->request,motionWrong->store).projection,"N11 unbound raw motion refused");
    auto planWrong=std::make_unique<SampleFixture>();++planWrong->request.externalPlan.revision;
    check(!P::BuildRecipe(planWrong->request,planWrong->store).product,"N01 stale plan reference refused");
    std::cout<<"Sample profile: "<<count<<" checks, "<<failed<<" failures; CPU metadata only\n";
    return failed?1:0;
}
