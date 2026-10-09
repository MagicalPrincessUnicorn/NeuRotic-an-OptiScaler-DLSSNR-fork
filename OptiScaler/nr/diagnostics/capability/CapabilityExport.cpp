#include "CapabilityExport.h"
#include "CapabilityStore.h"
#include "CapabilityWireValidation.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include <map>
#include <stdexcept>
namespace DlssNr::Capability {
namespace {
using Json=Wire::Json;
constexpr std::array<std::string_view,7> BasisNames{"T0_UNTRUSTED_OR_MUTATED","T1_PASSIVE_FILE_OBSERVATION","T2_PLATFORM_IDENTITY","T3_UNMUTATED_VENDOR_QUERY","T4_CONTROLLED_BEHAVIORAL_DIFFERENTIAL","T5_CURRENT_SESSION_OWNER_EVIDENCE","T6_RUNTIME_OR_ACCEPTANCE_EVIDENCE"};
constexpr std::array<std::string_view,3> Mutations{"none_observed","mutated","unknown"},Upstreams{"exact_owner_chain","unverified","not_applicable"},Roles{"owner_identity","observation_correlation","historical"},Statuses{"known","unknown","not_applicable"};
constexpr std::array<std::string_view,5> Collections{"known","not_observed","unavailable","refused","error"},ScalarNames{"bool","u64","i64","f64","text"};
std::string Hex(std::span<const unsigned char> bytes) { constexpr char digits[]="0123456789abcdef"; std::string output; output.reserve(bytes.size()*2); for(auto byte:bytes) {output+=digits[byte>>4];output+=digits[byte&15];} return output; }
std::string Digest(std::string_view value) {
    std::array<unsigned char,32> bytes{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),static_cast<ULONG>(value.size()),bytes.data(),static_cast<ULONG>(bytes.size()))<0) throw std::runtime_error("CAP_DIGEST_UNAVAILABLE");
    return Hex(bytes);
}
struct Encoder {
    ExportProfile profile; std::string nonce; std::map<std::string,std::string> tokens;
    explicit Encoder(ExportProfile p):profile(p) {
        if(p==ExportProfile::Minimal) { std::array<unsigned char,16> salt{}; if(BCryptGenRandom(nullptr,salt.data(),static_cast<ULONG>(salt.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) throw std::runtime_error("CAP_REDACTION_UNAVAILABLE"); nonce=Hex(salt); }
    }
    bool Minimal() const noexcept { return profile==ExportProfile::Minimal; }
    std::string Token(std::string_view role,std::string_view value) {
        if(!Minimal()) return std::string(value);
        auto key=std::string(role)+":"+std::string(value); auto found=tokens.find(key); if(found!=tokens.end()) return found->second;
        auto token="opaque-"+nonce+"-"+std::to_string(tokens.size()+1); tokens.emplace(std::move(key),token); return token;
    }
    Json Strings(const Batch& b,ListRef refs,std::string_view role,bool sensitive=false) {
        Json values=Json::array(); for(auto r:b.List<TextRef>(refs)) {
            if(Minimal()&&sensitive) { values.push_back("Details redacted by minimal export."); break; }
            auto value=b.Text(r); values.push_back(role.empty()?std::string(value):Token(role,value));
        } return values;
    }
    Json Receipt(const OwnerReceipt& receipt,const Batch& b) {
        if(!receipt.id.size) return nullptr;
        return {{"owner_contract",Token("contract",b.Text(receipt.contract))},{"contract_version",Token("version",b.Text(receipt.version))},{"receipt_id",Token("receipt",b.Text(receipt.id))},{"verdict",Minimal()?"redacted":std::string(b.Text(receipt.verdict))},{"lineage_ref",Token("lineage",b.Text(receipt.lineage))},{"sha256",Minimal()||!receipt.sha256.size?Json(nullptr):Json(b.Text(receipt.sha256))}};
    }
    Json AssertionValue(const Assertion& a,const Batch& b) {
        constexpr std::array<std::string_view,8> kinds{"support","availability","enablement","health","result","qualification","owner_receipt","scalar"};
        Json output={{"kind",kinds[static_cast<size_t>(a.kind)]}};
        if(a.kind==AssertionKind::Scalar) {
            output["scalar_type"]=ScalarNames[static_cast<size_t>(a.scalar)];
            switch(a.scalar) {
                case ScalarType::Bool: output["value"]=a.unsignedValue!=0; break;
                case ScalarType::U64: output["value"]=std::to_string(a.unsignedValue); break;
                case ScalarType::I64: output["value"]=std::to_string(a.signedValue); break;
                case ScalarType::F64: output["value"]=a.realValue; break;
                case ScalarType::Text: output["value"]=Minimal()?"redacted":std::string(b.Text(a.text)); break;
            }
        } else if(a.kind==AssertionKind::OwnerReceipt) output["value"]=Receipt(a.receipt,b);
        else if(a.kind==AssertionKind::Qualification) {
            constexpr std::array<std::string_view,3> values{"accepted","rejected","inconclusive"}; output["value"]=values[a.code];
            output["protocol"]=Token("protocol",b.Text(a.protocol)); output["protocol_version"]=Token("protocol_version",b.Text(a.protocolVersion));
            // A tuple digest is part of the qualification receipt, not a local device ID.
            output["tested_tuple_digest"]=Minimal()?Digest(Token("tuple",b.Text(a.testedTupleDigest))):std::string(b.Text(a.testedTupleDigest));
        } else if(a.kind==AssertionKind::Result) {
            constexpr std::array<std::string_view,3> values{"succeeded","failed","inconclusive"};
            constexpr std::array<std::string_view,6> effects{"not_observed","none_proven","possible","recorded","submitted","completion_observed"}; output["value"]=values[a.code]; output["side_effects"]=effects[a.sideEffects];
        } else if(a.kind==AssertionKind::Health) { constexpr std::array<std::string_view,6> values{"nominal","degraded","blocked","quarantined","failed","transition_pending"}; output["value"]=values[a.code]; }
        else { constexpr std::array<std::array<std::string_view,2>,3> values{{{"supported","unsupported"},{"present","absent"},{"enabled","disabled"}}}; output["value"]=values[static_cast<size_t>(a.kind)][a.code]; }
        return output;
    }
    Json Encode(const Snapshot& snapshot) {
        Json doc={{"schema_version","2.0.0"},{"registry_version","1.0.0"},{"record_kind","diagnostic_export"},{"origin_session",Token("session",snapshot.session)},{"snapshot_revision",std::to_string(snapshot.revision)},{"execution_permission",false},{"persist_runtime_readiness",false},{"capture_state","complete"},{"scopes",Json::array()},{"evidence",Json::array()},{"facts",Json::array()},{"streams",Json::array()},{"legacy_fingerprints",Json::array()},{"limitations",Json::array()}};
        if(Minimal()) doc["limitations"].push_back("Minimal profile redacts local identifiers, paths, identity text and artifact hashes. Bindings are opaque per export; outside identity comparison is unavailable.");
        for(const auto& stream:snapshot.streams) {
            const auto& b=stream.batch; auto writer=Token("writer",std::to_string(stream.writer));
            Json scopes=Json::array();
            for(size_t i=0;i<b.scopeCount;++i) {
                const auto& s=b.At<Scope>(b.scopes[i]); auto id=Token("reference",b.Text(s.id)); scopes.push_back(id);
                Json scope={{"scope_id",id},{"origin_session",Token("session",b.Text(s.session))},{"scope_role",Roles[static_cast<size_t>(s.role)]},{"bindings",Json::array()}};
                for(auto stamp:b.List<OwnerStamp>(s.bindings)) {
                    Json binding={{"dimension",DimensionNames[static_cast<size_t>(stamp.dimension)]},{"status",Statuses[static_cast<size_t>(stamp.status)]}};
                    if(stamp.status==StampStatus::Known) { binding["owner"]=Token("owner",b.Text(stamp.owner)); binding["instance"]=Token("instance",b.Text(stamp.instance)); binding["generation"]=std::to_string(stamp.generation); }
                    scope["bindings"].push_back(std::move(binding));
                } doc["scopes"].push_back(std::move(scope));
            }
            Json reasons=Json::array(); if(stream.raced) {doc["capture_state"]="raced"; reasons.push_back("CAP_CAPTURE_RACED");}
            else if(stream.offered!=stream.committed&&doc["capture_state"]=="complete") doc["capture_state"]="gap";
            doc["streams"].push_back({{"writer_id",writer},{"producer_id",ProducerIdNames[static_cast<size_t>(stream.producer)]},{"origin_session",doc["origin_session"]},{"offered_sequence",std::to_string(stream.offered)},{"committed_sequence",std::to_string(stream.committed)},{"state",stream.closed?"closed":stream.offered==stream.committed?"complete":"gap"},{"scope_refs",scopes},{"reason_codes",reasons}});
            for(size_t i=0;i<b.evidenceCount;++i) {
                const auto& e=b.At<Evidence>(b.evidence[i]);
                doc["evidence"].push_back({{"evidence_id",Token("reference",b.Text(e.id))},{"source_kind",SourceKindNames[static_cast<size_t>(e.source)]},{"basis",BasisNames[static_cast<size_t>(e.basis)]},{"mutation",Mutations[static_cast<size_t>(e.mutation)]},{"upstream_binding",Upstreams[static_cast<size_t>(e.upstream)]},{"collection_method",Minimal()?"bounded_value_observation":std::string(b.Text(e.method))},{"lineage_ids",Strings(b,e.lineages,"lineage")},{"parent_evidence_ids",Strings(b,e.parents,"reference")},{"source_locator",Minimal()?"redacted":std::string(b.Text(e.locator))},{"artifact_sha256",Minimal()||!e.sha256.size?Json(nullptr):Json(b.Text(e.sha256))},{"semantic_provenance_ref",Receipt(e.semanticReceipt,b)},{"limitations",Strings(b,e.limitations,"",true)},{"legacy_evidence_ids",Strings(b,e.legacyIds,"legacy_evidence")},{"scope_ref",Token("reference",b.Text(e.scope))},{"writer_id",writer},{"source_sequence",std::to_string(e.sequence)}});
            }
            for(size_t i=0;i<b.factCount;++i) {
                const auto& f=b.At<Fact>(b.facts[i]); Json qualifiers=Json::object();
                for(size_t q=0;q<f.qualifiers.size();++q) if(f.qualifiers[q].size) {
                    auto value=b.Text(f.qualifiers[q]); bool stable=q==0||q==static_cast<size_t>(Qualifier::FeatureNamespace)||q==static_cast<size_t>(Qualifier::FeatureId)||q==static_cast<size_t>(Qualifier::ParameterType)||q==static_cast<size_t>(Qualifier::ParameterPhase)||q==static_cast<size_t>(Qualifier::ScopeCategory);
                    // Registered owner fields are closed literals; identity JSON pointers retain
                    // their domain prefix but obscure the remaining local field identity.
                    if(Minimal()&&q==0&&f.capability==CapabilityId::FingerprintField) {
                        std::string field; for(auto prefix:{"/collector/","/subject/","/adapter/","/driver/","/modules/","/feature_target/"}) if(value.starts_with(prefix)) field=std::string(prefix)+Token("field",value); qualifiers["field"]=field;
                    } else qualifiers[std::string(QualifierNames[q])]=stable?std::string(value):Token("qualifier",value);
                }
                auto assertion=f.collection==Collection::Known?AssertionValue(f.assertion,b):Json(nullptr);
                // Identity scalars may encode LUIDs/addresses numerically. Redact by
                // capability semantics before generic scalar serialization can leak them.
                if(Minimal()&&f.capability==CapabilityId::FingerprintField&&!assertion.is_null()) assertion={{"kind","scalar"},{"scalar_type","text"},{"value","redacted"}};
                doc["facts"].push_back({{"fact_id",Token("reference",b.Text(f.id))},{"capability_id",Lookup(f.capability)->id},{"subject_id",Token("subject",b.Text(f.subject))},{"qualifiers",qualifiers},{"scope_ref",Token("reference",b.Text(f.scope))},{"writer_id",writer},{"source_sequence",std::to_string(f.sequence)},{"collection",Collections[static_cast<size_t>(f.collection)]},{"assertion",assertion},{"evidence_ids",Strings(b,f.evidence,"reference")},{"dependency_fact_ids",Strings(b,f.dependencies,"reference")},{"reason_codes",Strings(b,f.reasons,"")},{"legacy_field_refs",Strings(b,f.legacyFields,"legacy_field")},{"limitations",Strings(b,f.limitations,"",true)}});
            }
        }
        if(!Minimal()) for(const auto& legacy:snapshot.legacyFingerprints) doc["legacy_fingerprints"].push_back({{"attachment_id",legacy.id},{"source_sha256",legacy.sha256},{"role","historical_only"},{"payload",Wire::Parse(legacy.payload)}});
        return doc;
    }
};
}
ExportResult ExportDiagnostic(const Snapshot& snapshot,ExportProfile profile) noexcept {
    try {
        if(snapshot.streams.size()>128||snapshot.legacyFingerprints.size()>16) return {ExportState::Capacity,{},"CAP_EXPORT_CAPACITY"};
        for(const auto& stream:snapshot.streams) {
            if(stream.producer>=ProducerId::Ui||stream.writer==0||stream.committed>stream.offered) return {ExportState::InvalidSnapshot,{},"CAP_INVALID_STREAM"};
            if((stream.batch.factCount||stream.batch.evidenceCount||stream.batch.scopeCount)&&ValidateBatch(stream.batch,stream.producer,stream.writer,stream.committed,snapshot.session)!=PublishResult::Published) return {ExportState::InvalidSnapshot,{},"CAP_INVALID_IMAGE"};
        }
        Encoder encoder(profile); auto doc=encoder.Encode(snapshot);
        if(!Wire::Shape(doc)||!Wire::Semantics(doc)) return {ExportState::InvalidSnapshot,{},"CAP_INVALID_WIRE_IMAGE"};
        auto bytes=doc.dump(2); if(bytes.size()>8*1024*1024) return {ExportState::Capacity,{},"CAP_DOCUMENT_CAPACITY"}; return {ExportState::Encoded,std::move(bytes),{}};
    } catch(const std::bad_alloc&) { return {}; }
      catch(...) { return {ExportState::InvalidSnapshot,{},"CAP_EXPORT_FAILED"}; }
}
ImportResult DecodeHistorical(std::span<const std::byte> bytes) noexcept {
    if(bytes.size()>8*1024*1024) return {ImportState::Capacity,{},"CAP_DOCUMENT_CAPACITY"};
    try {
        const std::string_view text{reinterpret_cast<const char*>(bytes.data()),bytes.size()}; Json doc;
        try { doc=Wire::Parse(text); } catch(const std::length_error&) { return {ImportState::Capacity,{},"CAP_DEPTH_CAPACITY"}; } catch(const std::bad_alloc&) { return {}; } catch(...) { return {ImportState::InvalidSyntax,{},"CAP_JSON_SYNTAX"}; }
        if(doc.is_object()&&doc.value("schema_version",std::string{})=="1.0.0") {
            if(!Wire::LegacyShape(doc)) return {ImportState::InvalidShape,{},"CAP_LEGACY_SHAPE"};
        } else {
            if(!Wire::Shape(doc)) return {ImportState::InvalidShape,{},"CAP_WIRE_SHAPE"};
            if(!Wire::Semantics(doc)) return {ImportState::InvalidReferences,{},"CAP_WIRE_SEMANTICS"};
        }
        auto report=std::shared_ptr<const HistoricalReport>(new HistoricalReport(std::string(text),Digest(text)));
        return {ImportState::Historical,std::move(report),{}};
    } catch(const std::bad_alloc&) { return {}; } catch(...) { return {ImportState::InvalidShape,{},"CAP_IMPORT_FAILED"}; }
}
ExportResult ExportHistoricalArchive(const HistoricalReport& report) noexcept {
    try { return {ExportState::Encoded,std::string(report.Document()),{}}; } catch(...) { return {}; }
}
}
