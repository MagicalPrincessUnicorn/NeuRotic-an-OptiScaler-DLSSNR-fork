#include "../representations/RepresentationFixture.h"
#include <nr/contracts/NativeStageDelivery.h>
#include <nr/contracts/FixtureCodec.h>
using namespace NrTest002;namespace Codec=C::Codec;namespace S=Neurotic::Context;
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<NrRepresentationTests::Fixture>();Codec::MetadataCatalog catalog;
    const auto publish=[&]<class T>(const T& body,C::OwnerDomain owner){auto ref=f->store.Publish(body,owner);
        if(catalog.Add(ref,body)!=C::Error::None)throw std::runtime_error("catalog fixture refusal");return ref;};
    C::NativeSampleIdentityV1 sample;sample.session=f->frame.sessionId.KnownPart()->value;
    sample.stream=f->frame.renderStreamId.KnownPart()->value;sample.view=f->frame.viewId.KnownPart()->value;
    sample.feature=Id<C::ObjectIncarnation>("feature",1);sample.ingress=Id<C::ObjectIncarnation>("ingress",1);
    sample.producerOrdinal=1;sample.callback=Ev(90).record;
    C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::HostReturn;
    delivery.profile=f->profile;delivery.placement=C::Placement::NativeAfter;
    delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
    C::NativeOutputContentV1 output;output.view=publish(f->target,C::OwnerDomain::Resource);
    output.recording=Key("recording",1);output.producerOrdinal=4;output.semanticDomain=Sym("SceneLinear");
    C::NativeStageDeliveryV1 stage;stage.publication={C::OwnerDomain::Provider,Sym(C::NativeStageDeliveryV1::WireName),{1,0},Key("stage",1),1};
    stage.plan={Sym("PlanCommit"),Key("plan"),1};stage.route=Id<C::RouteGeneration>("route",1);
    stage.delivery=publish(delivery,C::OwnerDomain::StreamCoordinator);stage.recipe={Sym("NrExecutionRecipe"),Key("recipe"),1};
    stage.executionResult={Sym("EvaluationResult"),Key("execution"),1};stage.sample=publish(sample,C::OwnerDomain::Provider);
    stage.evaluation=Id<C::EvaluationId>("evaluation",1);stage.admissionEpoch=Key("epoch",1);
    stage.inputBindings={C::OwnerDomain::RenderingProtocol,Sym("RENDER.BindingPlan"),{1,0},Key("bindings"),1};
    stage.nativeOutput=publish(output,C::OwnerDomain::Resource);stage.returnedOutput=stage.nativeOutput;
    stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=Id<C::ObjectIncarnation>("caller",1);
    stage.lastRecording=output.recording;stage.lastRecordedOrdinal=5;stage.hostResult=0xffff'ffefu;
    stage.outcome=C::NativeStageOutcome::HostReturnRecorded;stage.reason=Sym("Native.HostReturnRecorded");
    C::BoundedList<C::OwnerValueReference,16> causes;causes.Push({C::OwnerDomain::Provider,Sym("Host.ReturnOperation"),{1,0},Key("operation"),1});
    stage.causes.count=1;stage.causes.backing=publish(causes,C::OwnerDomain::Provider);
    const auto wire=Codec::EncodeWithMetadata(stage,catalog);if(wire.error!=C::Error::None)std::cerr<<"encode error "<<static_cast<int>(wire.error)<<'\n';
    const auto read=Codec::Decode<C::NativeStageDeliveryV1>(wire.text);
    check(wire.error==C::Error::None&&read.record&&read.record->Value()==stage,"R07 complete stage closure round trip");
    check(wire.text.find("FinalSealId")==std::string::npos,"N03 stage delivery contains no final-seal claim");
    auto missing=stage;missing.returnedOutput.reset();check(Codec::EncodeWithMetadata(missing,catalog).error!=C::Error::None,"N03 local success requires returned output");
    auto stale=stage;++stale.nativeOutput->revision;check(Codec::EncodeWithMetadata(stale,catalog).error!=C::Error::None,"N02 stale output publication refuses");
    auto wrong=stage;wrong.sample.owner=C::OwnerDomain::Diagnostics;check(Codec::EncodeWithMetadata(wrong,catalog).error!=C::Error::None,"N03 diagnostic sample cannot become stage authority");
    auto future=stage;future.version=2;check(Codec::EncodeWithMetadata(future,catalog).error==C::Error::UnsupportedVersion,"R07 future stage version refuses");
    future=stage;future.publication.schemaVersion.minor=1;check(Codec::EncodeWithMetadata(future,catalog).error==C::Error::UnsupportedVersion,"R07 future publication schema refuses");
    auto invalid=stage;invalid.executionResult.recordType=Sym("FinalRealFramePacket");
    check(Codec::EncodeWithMetadata(invalid,catalog).error==C::Error::WrongRecordType,"N03 C13 cannot replace C07 reference");
    invalid=stage;invalid.causes={};check(Codec::EncodeWithMetadata(invalid,catalog).error!=C::Error::None,"N03 successful stage requires causal owner evidence");
    std::cout<<"Stage schema: "<<count<<" checks, "<<failed<<" failures; serialized values do not grant permission\n";return failed?1:0;
}
