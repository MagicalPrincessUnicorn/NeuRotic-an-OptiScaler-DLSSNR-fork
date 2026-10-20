#include "../OptiScaler/nr/diagnostics/capability/CapabilityStore.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityQuery.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityStaging.h"
#include <cassert>
#include <iostream>
using namespace DlssNr::Capability;

Batch Image(const WriterPort& w, uint64_t seq, bool enabled = true, Collection state = Collection::Known)
{
    BatchBuilder b;
    Scope s{}; s.id=b.Text(w.RecordId(seq,"scope")); s.session=b.Text(w.Session());
    OwnerStamp p{Dimension::Process, StampStatus::Known,b.Text("process"),b.Text("test"),0};
    OwnerStamp c{Dimension::Configuration, StampStatus::Known,b.Text("config"),b.Text("nr"),0};
    s.bindings=b.List<OwnerStamp>({p,c}); auto sr=b.AddScope(s);
    Evidence e{}; e.id=b.Text(w.RecordId(seq,"evidence")); e.scope=sr; e.writer=w.Id(); e.sequence=seq;
    e.method=b.Text("test_copy"); e.lineages=b.Strings({"test-lineage"}); auto er=b.AddEvidence(e);
    Fact f{}; f.id=b.Text(w.RecordId(seq,"fact")); f.subject=b.Text("nr"); f.capability=CapabilityId::ConfigRequestedEnabled;
    f.scope=sr; f.writer=w.Id(); f.sequence=seq; f.collection=state;
    f.assertion.kind=AssertionKind::Enablement; f.assertion.code=enabled?0:1;
    f.evidence=b.List<TextRef>({er}); b.AddFact(f); return b.Finish();
}
QueryRequest Request()
{
    QueryRequest q; q.capability=CapabilityId::ConfigRequestedEnabled; q.subject="nr"; q.session="session";
    q.bindings={{Dimension::Process,StampStatus::Known,"process","test",0},
                {Dimension::Configuration,StampStatus::Known,"config","nr",0}}; return q;
}
int main()
{
    {
        Store dependencies("session"); AdapterFactory adapters(dependencies);
        auto first=adapters.RegisterWriter(ProducerId::Config,"nr").port;
        auto second=adapters.RegisterWriter(ProducerId::Config,"nr").port;
        auto a=ReserveSample(*first),z=ReserveSample(*second);
        auto dependency=Image(*second,z.sequence); auto& dep=*reinterpret_cast<Fact*>(dependency.arena.data()+dependency.facts[0]);
        dep.capability=CapabilityId::ConfigRequestedRoute; dep.assertion.kind=AssertionKind::Scalar; dep.assertion.scalar=ScalarType::U64;
        assert(TryPublish(*second,z.sequence,dependency)==PublishResult::Published);
        auto image=Image(*first,a.sequence); auto& fact=*reinterpret_cast<Fact*>(image.arena.data()+image.facts[0]);
        auto id=second->RecordId(z.sequence,"fact"); auto at=image.used; std::memcpy(image.arena.data()+at,id.data(),id.size()); image.used+=static_cast<uint32_t>(id.size());
        TextRef ref{at,static_cast<uint32_t>(id.size())}; auto aligned=(image.used+static_cast<uint32_t>(alignof(TextRef)-1))&~static_cast<uint32_t>(alignof(TextRef)-1);
        std::memcpy(image.arena.data()+aligned,&ref,sizeof(ref)); image.used=aligned+sizeof(ref); fact.dependencies={aligned,1};
        assert(TryPublish(*first,a.sequence,image)==PublishResult::Published);
        auto complete=dependencies.Capture({}); assert(Query(*complete.snapshot,Request()).resolution==Resolution::Resolved);
        auto filtered=dependencies.Capture({{CapabilityId::ConfigRequestedEnabled}});
        assert(Query(*filtered.snapshot,Request()).resolution==Resolution::Resolved);
    }
    {
        Store many("session"); AdapterFactory adapters(many);
        for(unsigned i=0;i<20;++i) {
            auto writer=adapters.RegisterWriter(ProducerId::Config,"nr").port;
            auto sample=ReserveSample(*writer);
            assert(TryPublish(*writer,sample.sequence,Image(*writer,sample.sequence))==PublishResult::Published);
        }
        auto captured=many.Capture({}); auto equal=Query(*captured.snapshot,Request());
        assert(equal.resolution==Resolution::Resolved&&equal.acceptedFactIds.size()==20&&equal.candidates.size()==1);
    }
    Store store("session"); AdapterFactory factory(store);
    auto a=factory.RegisterWriter(ProducerId::Config,"nr"); assert(a.port);
    auto seq=ReserveSample(*a.port); assert(seq.sequence==1);
    assert(TryPublish(*a.port,seq.sequence,Image(*a.port,seq.sequence))==PublishResult::Published);
    auto q=Request(); auto snap=store.Capture({}); assert(snap.snapshot);
    assert(Query(*snap.snapshot,q).resolution==Resolution::Resolved); // known generation zero
    q.bindings[1].generation=1; assert(Query(*snap.snapshot,q).resolution==Resolution::Unknown);
    q=Request(); q.bindings[1].status=StampStatus::Unknown; assert(Query(*snap.snapshot,q).resolution==Resolution::Unknown);
    q=Request();
    auto dropped=ReserveSample(*a.port); (void)dropped;
    auto gap=store.Capture({}); assert(Query(*gap.snapshot,q).resolution==Resolution::Unknown);
    assert(Query(*snap.snapshot,q).resolution==Resolution::Resolved); // immutable, as-of only
    auto latest=ReserveSample(*a.port);
    assert(TryPublish(*a.port,latest.sequence,Image(*a.port,latest.sequence,false))==PublishResult::Published);
    assert(TryPublish(*a.port,seq.sequence,Image(*a.port,seq.sequence))==PublishResult::StaleSequence);
    auto other=factory.RegisterWriter(ProducerId::Config,"nr"); auto os=ReserveSample(*other.port);
    assert(TryPublish(*other.port,os.sequence,Image(*other.port,os.sequence,true))==PublishResult::Published);
    { auto conflict=store.Capture({}); assert(Query(*conflict.snapshot,q).resolution==Resolution::Conflict); }
    CloseWriter(*other.port);
    { auto current=store.Capture({}); auto r=Query(*current.snapshot,q); assert(r.resolution==Resolution::Resolved && r.assertion->code==1); }
    auto unknown=ReserveSample(*a.port);
    assert(TryPublish(*a.port,unknown.sequence,Image(*a.port,unknown.sequence,true,Collection::Unavailable))==PublishResult::Published);
    { auto current=store.Capture({}); assert(Query(*current.snapshot,q).resolution==Resolution::Unknown); }
    auto wrong=factory.RegisterWriter(ProducerId::Provider,"nr"); auto ws=ReserveSample(*wrong.port);
    assert(TryPublish(*wrong.port,ws.sequence,Image(*wrong.port,ws.sequence))==PublishResult::ProducerNotAllowed);
    auto staged=ReserveSample(*a.port); auto batch=Image(*a.port,staged.sequence);
    assert(TryStageEnvelope(*a.port,staged.sequence,batch)==StageResult::Staged);
    auto newer=ReserveSample(*a.port); (void)newer; auto drain=factory.DrainStagedObservations({});
    assert(!drain.empty() && drain[0]==PublishResult::StaleSequence);
    auto third=store.Capture({}); auto fourth=store.Capture({});
    assert(third.snapshot && fourth.snapshot); assert(!store.Capture({}).snapshot);
    CloseWriter(*a.port); assert(ReserveSample(*a.port).state==SequenceState::Closed);
    auto replacement=factory.RegisterWriter(ProducerId::Config,"nr"); assert(replacement.port->Id()!=a.port->Id());
    std::cout << "capability core: PASS\n";
    return 0;
}
