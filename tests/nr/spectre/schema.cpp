#include "../context/ContextFixtureStore.h"
#include <nr/contracts/NativeStageDelivery.h>
#include <nr/contracts/FixtureCodec.h>
#include <nr/context/NativeSampleQualification.h>
using namespace NrTest002;namespace S=Neurotic::Context;namespace Codec=C::Codec;
// Exact pre-amendment FrameIdentity fields. This fixture models the old reader's
// closed field set; it does not change the production codec's compatibility rule.
struct OldFrame:C::FrameIdentity
{
    static constexpr auto Fields()
    {
        return std::tuple{C::Field("sessionId",&OldFrame::sessionId),C::Field("renderStreamId",&OldFrame::renderStreamId),
            C::Field("viewId",&OldFrame::viewId),C::Field("episodeId",&OldFrame::episodeId),
            C::Field("baseRealFrameId",&OldFrame::baseRealFrameId),C::Field("renderId",&OldFrame::renderId),
            C::Field("evaluationId",&OldFrame::evaluationId),C::Field("presentId",&OldFrame::presentId),
            C::Field("generatedFrameId",&OldFrame::generatedFrameId),C::Field("finalSealId",&OldFrame::finalSealId),
            C::Field("associationEvidence",&OldFrame::associationEvidence)};
    }
};
int main()
{
    int count=0,failed=0;const auto check=[&](bool value,const char* name){++count;if(!value){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    C::NativeSampleIdentityV1 sample;sample.session=Id<C::SessionId>("session",1);sample.stream=Id<C::RenderStreamId>("stream",1);
    sample.view=Id<C::ViewId>("view",1);sample.feature=Id<C::ObjectIncarnation>("feature",1);sample.ingress=Id<C::ObjectIncarnation>("ingress",1);
    sample.producerOrdinal=1;sample.callback=Ev(90).record;
    const auto sampleWire=Codec::Encode(sample);const auto sampleRead=Codec::Decode<C::NativeSampleIdentityV1>(sampleWire.text);
    check(sampleWire.error==C::Error::None&&sampleRead.record&&sampleRead.record->Value()==sample,"R07 complete sample round trip");
    NrContextTests::Store store;auto frame=NrContextTests::Frame();frame.baseRealFrameId={};
    frame.nativeSample=store.Publish(sample,C::OwnerDomain::Provider);Codec::MetadataCatalog catalog;
    check(catalog.Add(*frame.nativeSample,sample)==C::Error::None,"R07 sample closure serialized");
    const auto wire=Codec::EncodeWithMetadata(frame,catalog);const auto read=Codec::Decode<C::FrameIdentity>(wire.text);
    check(wire.error==C::Error::None&&read.record&&read.record->Value()==frame,"R07 frame and exact sample closure round trip");
    check(Codec::Decode<OldFrame>(wire.text).error!=C::Error::None,"R07 old reader rejects mandatory Native field");
    OldFrame old;static_cast<C::FrameIdentity&>(old)=NrContextTests::Frame();const auto oldWire=Codec::Encode(old);
    check(oldWire.error==C::Error::None&&Codec::Decode<OldFrame>(oldWire.text).record.has_value(),"R07 old frame fixture remains self-readable");
    check(Codec::Decode<C::FrameIdentity>(oldWire.text).error!=C::Error::None,"R07 no silent missing-field normalization in new reader");
    auto stale=frame;++stale.nativeSample->revision;
    check(Codec::EncodeWithMetadata(stale,catalog).error!=C::Error::None,"R07 stale closure revision refuses");
    C::EvidenceVector evidence;evidence.nativeFreshness=S::NativeFreshness(*frame.nativeSample,sample);
    const auto freshWire=Codec::Encode(evidence);const auto freshRead=Codec::Decode<C::EvidenceVector>(freshWire.text);
    check(freshRead.record&&freshRead.record->Value()==evidence,"R07 explicit freshness round trip");
    evidence.ageInRealFrames=Known(std::uint64_t{0});check(Codec::Encode(evidence).error!=C::Error::None,"R07 mixed freshness units rejected by codec");
    sample.version=2;check(Codec::Encode(sample).error==C::Error::UnsupportedVersion,"R07 future sample version fails closed");
    C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::HostReturn;delivery.profile={Sym("Native"),Sym("Legacy"),1};
    delivery.placement=C::Placement::NativeAfter;delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;
    delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;const auto deliveryWire=Codec::Encode(delivery);
    const auto deliveryRead=Codec::Decode<C::NativeDeliveryContractV1>(deliveryWire.text);
    check(deliveryRead.record&&deliveryRead.record->Value()==delivery,"R07 immutable delivery round trip");
    auto strict=delivery;strict.kind=C::NativeDeliveryKind::FinalConsumerRequired;
    C::MetadataRef<C::NativeFinalConsumerContractV1> final;final.owner=C::OwnerDomain::FrameGeneration;final.record=Key("final");final.revision=1;
    final.schemaVersion.minor=1;strict.finalConsumer=final;
    check(strict.Check()!=C::Error::None,"R07 unsupported final-consumer reference schema rejected");
    C::PlanCommit plan;plan.header=NrContextTests::Header(C::ContractId::C05,C::OwnerDomain::StreamCoordinator,77);
    plan.nativeDelivery=store.Publish(delivery,C::OwnerDomain::StreamCoordinator);plan.nativeDelivery->schemaVersion.minor=1;
    check(plan.Check()!=C::Error::None,"R07 unsupported delivery reference schema rejected");
    C::NrExecutionRecipe recipe;recipe.header=NrContextTests::Header(C::ContractId::C06,C::OwnerDomain::RenderingProtocol,78);
    recipe.settingsSnapshot.owner=C::OwnerDomain::Configuration;
    recipe.nativeDelivery=plan.nativeDelivery;recipe.nativeDelivery->schemaVersion.minor=0;recipe.nativeSample=frame.nativeSample;
    check(recipe.Check()==C::Error::None,"R07 valid recipe metadata shape control");
    recipe.nativeSample->schemaVersion.minor=1;
    check(recipe.Check()!=C::Error::None,"R07 unsupported C06 sample reference schema rejected");
    std::cout<<"Spectre schema: "<<count<<" checks, "<<failed<<" failures; serialization only\n";return failed?1:0;
}
