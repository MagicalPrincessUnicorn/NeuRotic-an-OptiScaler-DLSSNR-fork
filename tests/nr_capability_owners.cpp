#include "../OptiScaler/nr/diagnostics/capability/CapabilityOwnerAdapters.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityQuery.h"
#include <cassert>
#include <iostream>
using namespace DlssNr::Capability;
static const WriterPort* chainWriter=nullptr;
void DlssNr::Capability::ObservePresentChain(const PresentChainObservation& input) noexcept {
    if(!chainWriter) return;
    auto sample=ReserveSample(*chainWriter); auto o=input; o.sequence=sample.sequence;
    (void)TryStageEnvelope(*chainWriter,sample.sequence,AdaptPresentChain(*chainWriter,o));
}
int main()
{
    Store store("session"); AdapterFactory factory(store);
    auto port=factory.RegisterWriter(ProducerId::Config,"nr").port; auto sequence=ReserveSample(*port);
    ConfigObservation config{sequence.sequence,7,true,1}; auto batch=AdaptConfig(*port,config);
    assert(TryPublish(*port,sequence.sequence,batch)==PublishResult::Published);
    QueryRequest q; q.capability=CapabilityId::ConfigRequestedEnabled; q.subject="nr"; q.session="session";
    q.bindings={{Dimension::Process,StampStatus::Known,"diagnostics","session",0},
                {Dimension::Configuration,StampStatus::Known,"config","nr",7}};
    { auto snapshot=store.Capture({}); auto result=Query(*snapshot.snapshot,q); assert(result.resolution==Resolution::Resolved); }
    ++q.bindings[1].generation; { auto snapshot=store.Capture({}); assert(Query(*snapshot.snapshot,q).resolution==Resolution::Unknown); }
    NativeObservation native; char reason[]="owner failure"; assert(native.failure.Assign(reason)); reason[0]='X';
    assert(native.failure.View()=="owner failure"); std::string tooLong(513,'x'); assert(!native.failure.Assign(tooLong)); assert(native.failure.View().empty());
    auto np=factory.RegisterWriter(ProducerId::Native,"native").port;
    native.sequence=ReserveSample(*np).sequence;native.observedStates=0xfb;native.observedCounters=0x41;
    native.counters[0]=77;
    const auto partial=AdaptNative(*np,native);
    assert(partial.At<Fact>(partial.facts[2]).collection==Collection::NotObserved);
    assert(partial.At<Fact>(partial.facts[8]).collection==Collection::Known);
    assert(partial.At<Fact>(partial.facts[8]).assertion.unsignedValue==77);
    for(unsigned i=9;i<14;++i)assert(partial.At<Fact>(partial.facts[i]).collection==Collection::NotObserved);
    assert(TryPublish(*np,native.sequence,partial)==PublishResult::Published);
    auto pp=factory.RegisterWriter(ProducerId::Present,"present").port;
    PresentObservation present;present.sequence=ReserveSample(*pp).sequence;present.observedCounters=0xfb;
    const auto partialPresent=AdaptPresent(*pp,present);
    assert(partialPresent.At<Fact>(partialPresent.facts[2]).collection==Collection::NotObserved);
    assert(partialPresent.At<Fact>(partialPresent.facts[3]).collection==Collection::Known);
    assert(TryPublish(*pp,present.sequence,partialPresent)==PublishResult::Published);
    auto provider=factory.RegisterWriter(ProducerId::Provider,"fg").port; auto ps=ReserveSample(*provider);
    ProviderObservation p{ps.sequence,false,true,false,8}; auto pb=AdaptProvider(*provider,p);
    assert(TryPublish(*provider,ps.sequence,pb)==PublishResult::Published);
    q.capability=CapabilityId::ProviderOwnerSupport; q.subject="fg";
    { auto snapshot=store.Capture({}); q.mode=QueryMode::ExplainAsObserved; auto result=Query(*snapshot.snapshot,q); assert(!result.assertion&&result.candidates.empty()); }
    auto hp=factory.RegisterWriter(ProducerId::Hdr,"hdr").port; auto hs=ReserveSample(*hp);
    HdrObservation hdr; hdr.sequence=hs.sequence; hdr.identity=100; hdr.descriptorGeneration=9; hdr.metadataGeneration=3; hdr.colorObserved=true; hdr.colorClass.Assign("HDR10/PQ");
    auto hb=AdaptHdr(*hp,hdr); assert(TryPublish(*hp,hs.sequence,hb)==PublishResult::Published);
    q.capability=CapabilityId::HdrColorClass; q.subject="hdr-chain-100"; q.qualifiers[0]="colorClass";
    { auto snapshot=store.Capture({}); auto result=Query(*snapshot.snapshot,q); assert(!result.assertion && result.candidates.size()==1); }
    q.subject="hdr-chain-101"; { auto snapshot=store.Capture({}); assert(Query(*snapshot.snapshot,q).candidates.empty()); }
    auto chain=factory.RegisterWriter(ProducerId::Present,"present_chain").port; chainWriter=&*chain;
    struct Telemetry { bool requested=true,active=true,failed=false,policyBlocked=false,historyResetPending=false; uint64_t resourceGeneration=1; } telemetry;
    {
        PresentObservationScope capture(telemetry);
        capture.values.chainIdentity=100; capture.values.hdrIdentity=100;
    }
    factory.DrainStagedObservations({});
    q.capability=CapabilityId::PresentOwnerState; q.subject="present-chain-100"; q.qualifiers[0]="active";
    { auto snapshot=store.Capture({}); assert(Query(*snapshot.snapshot,q).candidates.size()==1); }
    auto refuse=[&] { PresentObservationScope capture(telemetry); telemetry.requested=false; telemetry.active=false; return; };
    refuse(); factory.DrainStagedObservations({});
    { auto snapshot=store.Capture({}); assert(Query(*snapshot.snapshot,q).candidates.empty()); }
    chainWriter=nullptr;
    std::cout << "capability owners: PASS\n";
}
