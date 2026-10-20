#pragma once
#include "InitialInvocationFixture.h"
namespace NativeAdversarialGroups {
using namespace InitialNativeTests;
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
}
