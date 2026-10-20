#include "../OptiScaler/nr/diagnostics/capability/CapabilityExport.h"
#define main capability_core_scenarios
#include "nr_capability.cpp"
#undef main
#include "../external/nlohmann/json.hpp"
#include <fstream>
#include "../OptiScaler/nr/diagnostics/capability/CapabilityNgxObservation.h"
using Json=nlohmann::json;
ImportResult Import(std::string_view text) { return DecodeHistorical({reinterpret_cast<const std::byte*>(text.data()),text.size()}); }
int main()
{
    Store store("session"); AdapterFactory factory(store); auto port=factory.RegisterWriter(ProducerId::Config,"nr").port;
    auto seq=ReserveSample(*port); assert(TryPublish(*port,seq.sequence,Image(*port,seq.sequence))==PublishResult::Published);
    auto snapshot=store.Capture({});
    auto exported=ExportDiagnostic(*snapshot.snapshot,ExportProfile::Minimal); assert(exported.state==ExportState::Encoded);
    auto second=ExportDiagnostic(*snapshot.snapshot,ExportProfile::Minimal); assert(exported.utf8!=second.utf8);
    auto doc=Json::parse(exported.utf8); assert(doc["execution_permission"]==false&&doc["persist_runtime_readiness"]==false);
    assert(doc["origin_session"]!="session"); assert(doc["facts"][0]["scope_ref"]==doc["scopes"][0]["scope_id"]);
    auto imported=Import(exported.utf8); assert(imported.state==ImportState::Historical&&imported.report->IsHistorical());
    std::ofstream("builds/capability-facts/tests/export-minimal.json")<<exported.utf8;
    auto archive=ExportDiagnostic(*snapshot.snapshot,ExportProfile::LocalArchive); assert(archive.state==ExportState::Encoded);
    auto mutated=Json::parse(archive.utf8); mutated["record_kind"]="runtime_snapshot"; assert(Import(mutated.dump()).state==ImportState::Historical);
    mutated["execution_permission"]=true; assert(Import(mutated.dump()).state==ImportState::InvalidShape);
    assert(Import("{\"schema_version\":\"2.0.0\",\"schema_version\":\"2.0.0\"}").state==ImportState::InvalidSyntax);
    mutated=doc; mutated["snapshot_revision"]="18446744073709551616"; assert(Import(mutated.dump()).state!=ImportState::Historical);
    mutated=doc; mutated["facts"][0]["evidence_ids"]={"missing"}; assert(Import(mutated.dump()).state==ImportState::InvalidReferences);
    mutated=doc; mutated["streams"][0]["producer_id"]="present"; assert(Import(mutated.dump()).state==ImportState::InvalidReferences);
    mutated=doc; mutated["scopes"][0]["bindings"].push_back(mutated["scopes"][0]["bindings"][0]); assert(Import(mutated.dump()).state==ImportState::InvalidReferences);
    mutated=doc; mutated["facts"][0]["dependency_fact_ids"]=Json::array({doc["facts"][0]["fact_id"]}); assert(Import(mutated.dump()).state==ImportState::InvalidReferences);
    mutated=doc; mutated["facts"][0]["assertion"]["value"]="unsupported"; assert(Import(mutated.dump()).state==ImportState::InvalidShape);
    auto malformed=*snapshot.snapshot; malformed.streams[0].batch.factCount=33;
    assert(ExportDiagnostic(malformed,ExportProfile::Minimal).state==ExportState::InvalidSnapshot);
    for(unsigned producer=0;producer<9;++producer) {
        Store owners("owner-session"); AdapterFactory adapters(owners);
        const std::array<ProducerId,9> ids{ProducerId::Config,ProducerId::Native,ProducerId::Lifecycle,ProducerId::Present,ProducerId::Present,ProducerId::Provider,ProducerId::Hdr,ProducerId::Ngx,ProducerId::Parameters};
        auto writer=adapters.RegisterWriter(ids[producer],"owner").port; auto sample=ReserveSample(*writer); Batch image;
        switch(producer) {
            case 0: image=AdaptConfig(*writer,{sample.sequence,7,true,1}); break;
            case 1: { NativeObservation o; o.sequence=sample.sequence; o.failure.Assign("C:/private/model.bin pointer=0x1234"); image=AdaptNative(*writer,o); break; }
            case 2: { NativeObservation o; o.sequence=sample.sequence; image=AdaptLifecycle(*writer,o); break; }
            case 3: { PresentObservation o; o.sequence=sample.sequence; image=AdaptPresent(*writer,o); break; }
            case 4: { PresentChainObservation o; o.sequence=sample.sequence; o.chainIdentity=17; image=AdaptPresentChain(*writer,o); break; }
            case 5: image=AdaptProvider(*writer,{sample.sequence,true,true,false,9}); break;
            case 6: { HdrObservation o; o.sequence=sample.sequence; o.identity=18; o.colorClass.Assign("HDR10/PQ"); image=AdaptHdr(*writer,o); break; }
            case 7: { RequirementsEnvelope o; o.sequence=sample.sequence; o.outputDefined=true; image=AdaptRequirements(*writer,o); break; }
            case 8: { ParameterEnvelope o; o.sequence=sample.sequence; o.name.Assign("private-key"); o.type.Assign("pointer"); o.phase.Assign("evaluation"); o.readbackSucceeded=true; image=AdaptParameter(*writer,o); break; }
        }
        assert(TryPublish(*writer,sample.sequence,image)==PublishResult::Published);
        auto captured=owners.Capture({}); auto wire=ExportDiagnostic(*captured.snapshot,ExportProfile::Minimal);
        assert(wire.state==ExportState::Encoded&&Import(wire.utf8).state==ImportState::Historical);
        assert(wire.utf8.find("C:/private")==std::string::npos&&wire.utf8.find("0x1234")==std::string::npos&&wire.utf8.find("private-key")==std::string::npos);
        std::ofstream("builds/capability-facts/tests/export-owner-"+std::to_string(producer)+".json")<<wire.utf8;
    }
    assert(Import(std::string(8*1024*1024+1,' ')).state==ImportState::Capacity);
    assert(Import("{\"bad\":\"\xff\"}").state==ImportState::InvalidSyntax);
    mutated=doc; mutated["limitations"]={std::string(513,'x')}; assert(Import(mutated.dump()).state==ImportState::InvalidShape);
    std::ifstream legacyFile("tests/capability/legacy-examples.json"); Json examples; legacyFile>>examples;
    for(const auto& example:examples["examples"]) {
        auto historical=Import(example.dump()); assert(historical.state==ImportState::Historical);
        assert(ExportHistoricalArchive(*historical.report).utf8==example.dump());
        Snapshot attached=*snapshot.snapshot; attached.legacyFingerprints.push_back({"legacy",std::string(64,'a'),example.dump()});
        auto local=ExportDiagnostic(attached,ExportProfile::LocalArchive); assert(local.state==ExportState::Encoded);
        assert(Json::parse(local.utf8)["legacy_fingerprints"][0]["payload"]==example);
        auto minimal=ExportDiagnostic(attached,ExportProfile::Minimal); assert(minimal.state==ExportState::Encoded&&Json::parse(minimal.utf8)["legacy_fingerprints"].empty());
    }
    {
        Store identities("session"); AdapterFactory adapters(identities); auto writer=adapters.RegisterWriter(ProducerId::Identity,"device").port; auto sample=ReserveSample(*writer);
        const auto domains=RequiredDomains(*Lookup(CapabilityId::FingerprintField),"/adapter/luid");
        ObservationBuilder b(*writer,sample.sequence,"device",domains,ScopeRole::ObservationCorrelation);
        Assertion value; value.kind=AssertionKind::Scalar; value.scalar=ScalarType::U64; value.unsignedValue=9876543210123456;
        b.Add(CapabilityId::FingerprintField,value,"/adapter/luid");
        assert(TryPublish(*writer,sample.sequence,b.values.Finish())==PublishResult::Published);
        auto captured=identities.Capture({}); auto minimal=ExportDiagnostic(*captured.snapshot,ExportProfile::Minimal);
        assert(minimal.state==ExportState::Encoded&&minimal.utf8.find("9876543210123456")==std::string::npos);
        auto local=ExportDiagnostic(*captured.snapshot,ExportProfile::LocalArchive); assert(local.utf8.find("9876543210123456")!=std::string::npos);
    }
    std::cout << "capability export/history: PASS\n";
}
