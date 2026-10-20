#include <nr/contracts/C05_Routing.h>
#include <nr/contracts/C06_ProtocolInput.h>
#include <nr/context/EvidenceQualification.h>
#include "../context/ContextFixtureStore.h"
#include <iostream>
#if __has_include(<nr/contracts/NativeDelivery.h>)
#include <nr/contracts/NativeDelivery.h>
#define NR_SPECTRE_CONTRACTS 1
#endif
namespace C=Neurotic::Contracts;
namespace S=Neurotic::Context;
template<class T>constexpr bool DeliveryField=requires(T v){v.nativeDelivery;};
template<class T>constexpr bool SampleField=requires(T v){v.nativeSample;};
int main()
{
    int failures=0,checks=0;
    const auto check=[&](bool pass,const char* name){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
    check(DeliveryField<C::PlanCommit>,"N02: C05 has explicit Native delivery binding");
    check(DeliveryField<C::NrExecutionRecipe>,"N02: C06 has same typed delivery binding");
    check(SampleField<C::NrExecutionRecipe>,"N10: C06 has explicit Native sample subject");
    check(!S::SameScope(C::FrameIdentity{},C::FrameIdentity{}),"N11: Unknown final frames never establish scope");
#ifdef NR_SPECTRE_CONTRACTS
    using namespace NrTest002;
    C::NativeDeliveryContractV1 local;
    local.profile={Sym("Native"),Sym("Legacy"),1};
    local.kind=C::NativeDeliveryKind::HostReturn;
    local.placement=C::Placement::NativeAfter;
    local.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
    local.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
    check(S::ValidValues(local),"F08: valid local contract metadata");
    auto invalid=local;invalid.kind=C::NativeDeliveryKind::Invalid;
    check(!S::ValidValues(invalid),"F08: absent kind is never HostReturn");
    invalid=local;invalid.version=2;
    check(!S::ValidValues(invalid),"R07: future delivery version rejected");
    invalid=local;invalid.kind=C::NativeDeliveryKind::FinalConsumerRequired;
    check(!S::ValidValues(invalid),"F08: strict contract requires exact consumer definition");
    invalid=local;C::MetadataRef<C::NativeFinalConsumerContractV1> consumer;
    consumer.owner=C::OwnerDomain::FrameGeneration;consumer.record=Key("consumer");consumer.revision=1;
    invalid.finalConsumer=consumer;
    check(!S::ValidValues(invalid),"F08: HostReturn cannot retain a strict final-consumer contract");
    C::NativeSampleIdentityV1 sample;
    sample.session=Id<C::SessionId>("session",1);sample.stream=Id<C::RenderStreamId>("stream",1);
    sample.view=Id<C::ViewId>("view",1);sample.feature=Id<C::ObjectIncarnation>("feature",1);
    sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;
    sample.callback=Ev(41).record;
    check(S::ValidValues(sample),"N10: canonical local sample without base frame");
    auto other=sample;other.producerOrdinal=2;
    check(sample!=other,"N11: distinct source samples remain distinct");
    check(!sample.SameSample(other),"N11: dedup compares the producer ordinal");
    other=sample;++other.callback.value;
    check(sample.SameSample(other),"N09: callback metadata cannot widen sample dedup");
    other=sample;other.session={};
    check(!other.SameSample(other),"N09: unknown session cannot establish a sample key");
    other=sample;other.version=2;
    check(!S::ValidValues(other),"R07: future sample version rejected");
    other=sample;other.producerOrdinal=0;
    check(!S::ValidValues(other),"N09: unissued source ordinal rejected");
    NrContextTests::Store store;
    const auto ref=store.Publish(local,C::OwnerDomain::StreamCoordinator);
    check(S::ResolveMetadata(ref,store)!=nullptr,"N12: typed immutable metadata resolves as values only");
    auto stale=ref;++stale.revision;
    check(!S::ResolveMetadata(stale,store),"R07: stale delivery reference refused");
#else
    check(false,"N10/F08/R07: versioned production Native contracts are absent");
#endif
    std::cout<<"Spectre contracts: "<<checks<<" checks, "<<failures<<" failures; CPU metadata only\n";
    return failures?1:0;
}
