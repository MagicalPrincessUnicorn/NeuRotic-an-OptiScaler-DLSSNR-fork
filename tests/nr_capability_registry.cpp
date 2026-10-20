#define main capability_core_scenarios
#include "nr_capability.cpp"
#undef main
#include <bit>
int main()
{
    unsigned missingGates=0;
    // Literal subject/context fixtures exercise every declared direct profile. Removing
    // provenance or producer validation lets the fixture origin below resolve incorrectly.
    for(size_t index=0;index<Registry.size();++index) for(size_t profile=0;profile<Registry[index].profileCount;++profile) {
        const auto& reg=Registry[index]; Store store("session"); AdapterFactory factory(store);
        auto producer=static_cast<ProducerId>(std::countr_zero(reg.producers)); auto port=factory.RegisterWriter(producer,"nr").port; auto seq=ReserveSample(*port);
        BatchBuilder b; QueryRequest q; q.capability=static_cast<CapabilityId>(index); q.subject="nr"; q.session="session";
        Scope s{}; s.id=b.Text(port->RecordId(seq.sequence,"scope")); s.session=b.Text("session");
        std::array<OwnerStamp,22> stamps{}; size_t used=0;
        for(unsigned dim=0;dim<22;++dim) if(reg.domains&(1u<<dim)) {
            stamps[used++]={static_cast<Dimension>(dim),StampStatus::Known,b.Text("owner"),b.Text("instance"),0};
            q.bindings.push_back({static_cast<Dimension>(dim),StampStatus::Known,"owner","instance",0});
        }
        s.bindings=b.List<OwnerStamp>({stamps.data(),used}); auto scope=b.AddScope(s);
        Evidence e{}; e.id=b.Text(port->RecordId(seq.sequence,"evidence")); e.scope=scope; e.writer=port->Id(); e.sequence=seq.sequence;
        e.source=reg.profiles[profile].source; e.basis=reg.profiles[profile].basis; e.upstream=UpstreamBinding::ExactOwnerChain;
        e.method=b.Text("test_copy"); e.lineages=b.Strings({"lineage"});
        if(reg.id=="nr.cap.route.owner_health") e.semanticReceipt={b.Text("ChatB"),b.Text("1"),b.Text("receipt"),b.Text("nominal"),b.Text("lineage"),{}};
        auto evidence=b.AddEvidence(e);
        Fact f{}; f.id=b.Text(port->RecordId(seq.sequence,"fact")); f.subject=b.Text("nr"); f.scope=scope; f.writer=port->Id(); f.sequence=seq.sequence; f.capability=q.capability; f.collection=Collection::Known;
        for(unsigned key=0;key<14;++key) if(reg.qualifiers&(1u<<key)) { q.qualifiers[key]="value"; f.qualifiers[key]=b.Text("value"); }
        if(!reg.fields.empty()) { q.qualifiers[0]=reg.fields.substr(0,reg.fields.find('|')); f.qualifiers[0]=b.Text(q.qualifiers[0]); }
        if(reg.id=="nr.cap.fingerprint.field") { q.qualifiers[0]="/collector/name"; f.qualifiers[0]=b.Text(q.qualifiers[0]); }
        if(reg.qualifiers&(1u<<static_cast<unsigned>(Qualifier::FeatureId))) { q.qualifiers[static_cast<size_t>(Qualifier::FeatureId)]="18"; f.qualifiers[static_cast<size_t>(Qualifier::FeatureId)]=b.Text("18"); }
        if(reg.qualifiers&(1u<<static_cast<unsigned>(Qualifier::ParameterPhase))) { q.qualifiers[static_cast<size_t>(Qualifier::ParameterPhase)]="evaluation"; f.qualifiers[static_cast<size_t>(Qualifier::ParameterPhase)]=b.Text("evaluation"); }
        if(reg.id=="nr.cap.present.counter") { q.qualifiers[static_cast<size_t>(Qualifier::ScopeCategory)]="process_summary"; f.qualifiers[static_cast<size_t>(Qualifier::ScopeCategory)]=b.Text("process_summary"); }
        f.assertion.kind=reg.kind; f.assertion.scalar=reg.scalar;
        if(reg.kind==AssertionKind::Qualification) { f.assertion.protocol=b.Text("test"); f.assertion.protocolVersion=b.Text("1"); f.assertion.testedTupleDigest=b.Text(std::string(64,'a')); }
        if(reg.kind==AssertionKind::OwnerReceipt) f.assertion.receipt={b.Text("owner"),b.Text("1"),b.Text("receipt"),b.Text("opaque"),b.Text("lineage"),{}};
        f.evidence=b.List<TextRef>({evidence}); b.AddFact(f); auto batch=b.Finish();
        assert(TryPublish(*port,seq.sequence,batch)==PublishResult::Published);
        if(reg.id=="nr.cap.fingerprint.field") {
            auto bad=batch; auto& invalid=*reinterpret_cast<Fact*>(bad.arena.data()+bad.facts[0]);
            bad.arena[invalid.qualifiers[0].offset]=std::byte{'x'};
            assert(ValidateBatch(bad,producer,port->Id(),seq.sequence,"session")==PublishResult::InvalidBatch);
        }
        { auto snap=store.Capture({}); assert(Query(*snap.snapshot,q).resolution==Resolution::Resolved); }
        if(reg.id=="nr.cap.route.owner_health"||reg.id=="nr.cap.vendor.feature_support"||reg.id.starts_with("nr.cap.qualification.")||reg.id=="nr.cap.parameter.causal_effect") {
            auto snapshot=store.Capture({}); Snapshot malformed=*snapshot.snapshot;
            auto& image=malformed.streams[0].batch;
            auto& source=*reinterpret_cast<Evidence*>(image.arena.data()+image.evidence[0]);
            if(reg.id=="nr.cap.route.owner_health") source.semanticReceipt={};
            else {
                auto parent=source; parent.id=source.method; // Unique local ID in this immutable query fixture.
                parent.source=SourceKind::OwnerObservation; parent.basis=Basis::T5; parent.parents={};
                const auto offset=(image.used+static_cast<uint32_t>(alignof(Evidence)-1))&~static_cast<uint32_t>(alignof(Evidence)-1);
                std::memcpy(image.arena.data()+offset,&parent,sizeof(parent)); image.evidence[image.evidenceCount++]=offset; image.used=offset+sizeof(parent);
                auto refOffset=(image.used+static_cast<uint32_t>(alignof(TextRef)-1))&~static_cast<uint32_t>(alignof(TextRef)-1);
                std::memcpy(image.arena.data()+refOffset,&parent.id,sizeof(TextRef)); image.used=refOffset+sizeof(TextRef);
                source.parents={refOffset,1};
            }
            if(Query(malformed,q).resolution!=Resolution::Unknown) { ++missingGates; std::cerr<<"missing closure gate: "<<reg.id<<"\n"; }
            if(reg.id=="nr.cap.vendor.feature_support") {
                source.source=SourceKind::Inference; source.upstream=UpstreamBinding::Unverified;
                assert(Query(malformed,q).resolution==Resolution::Inferred);
            }
        }
        auto seq2=ReserveSample(*port); (void)seq2;
        // Full-image stream replacement is already tested independently; this snapshot
        // mutation isolates eligibility from publication and does not grant a live port.
        auto snap=store.Capture({}); Snapshot candidate=*snap.snapshot;
        candidate.streams[0].offered=candidate.streams[0].committed;
        auto& copy=candidate.streams[0].batch;
        auto& source=*reinterpret_cast<Evidence*>(copy.arena.data()+copy.evidence[0]); source.source=SourceKind::Fixture;
        assert(Query(candidate,q).resolution==Resolution::Unknown);
        auto& fact=*reinterpret_cast<Fact*>(copy.arena.data()+copy.facts[0]); source.source=e.source;
        fact.dependencies=fact.evidence;
        auto& ref=*reinterpret_cast<TextRef*>(copy.arena.data()+fact.dependencies.offset); ref=fact.id;
        assert(ValidateBatch(copy,producer,port->Id(),seq.sequence,"session")==PublishResult::InvalidBatch);
        fact.dependencies={}; source.parents=fact.evidence;
        ref=source.id;
        assert(ValidateBatch(copy,producer,port->Id(),seq.sequence,"session")==PublishResult::InvalidBatch);
    }
    assert(missingGates==0);
    std::cout << "capability registry profiles: PASS\n";
}
