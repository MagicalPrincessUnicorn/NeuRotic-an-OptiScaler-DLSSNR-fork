#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>

namespace DlssNr::Capability {
enum class ProducerId : uint8_t { Identity, Ngx, Config, Parameters, Native, Present, Lifecycle, Semantic, Discovery, Auxiliary, Provider, Hdr, Orchestration, Qualification, Ui, Count };
enum class Dimension : uint8_t { Process, GameSession, Api, Adapter, Driver, Device, Queue, Swapchain, Provider, Module, Feature, Route, View, Pass, Frame, Configuration, Lifecycle, HdrDescriptor, HdrMetadata, History, ResourceSet, ParameterBlock, Count };
enum class StampStatus : uint8_t { Known, Unknown, NotApplicable };
enum class ScopeRole : uint8_t { OwnerIdentity, ObservationCorrelation, Historical };
enum class SourceKind : uint8_t { OwnerObservation, RawApiBoundary, VendorQuery, EffectivePolicy, FileObservation, PlatformObservation, SyntheticDefault, Inference, HistoricalImport, UserReport, Fixture, QualificationReceipt, Count };
enum class Basis : uint8_t { T0, T1, T2, T3, T4, T5, T6 };
enum class Mutation : uint8_t { NoneObserved, Mutated, Unknown };
enum class UpstreamBinding : uint8_t { ExactOwnerChain, Unverified, NotApplicable };
enum class AssertionKind : uint8_t { Support, Availability, Enablement, Health, Result, Qualification, OwnerReceipt, Scalar };
enum class ScalarType : uint8_t { Bool, U64, I64, F64, Text };
enum class Collection : uint8_t { Known, NotObserved, Unavailable, Refused, Error };
enum class Qualifier : uint8_t { Field, FeatureNamespace, FeatureId, ModuleRole, ParameterId, ParameterName, ParameterType, ParameterPhase, OperationId, ViewId, PassId, Protocol, GameBuild, ScopeCategory, Count };
enum class CapabilityId : uint8_t; // Frozen registry supplies definitions.
struct TextRef { uint32_t offset=0, size=0; };
struct ListRef { uint32_t offset=0, count=0; };
struct OwnerStamp { Dimension dimension{}; StampStatus status=StampStatus::Unknown; TextRef owner,instance; uint64_t generation=0; };
struct Scope { TextRef id,session; ScopeRole role=ScopeRole::OwnerIdentity; ListRef bindings; };
struct OwnerReceipt { TextRef contract,version,id,verdict,lineage,sha256; };
struct Assertion {
    AssertionKind kind=AssertionKind::Scalar; ScalarType scalar=ScalarType::Bool;
    uint8_t code=0, sideEffects=0; uint64_t unsignedValue=0; int64_t signedValue=0; double realValue=0;
    TextRef text,protocol,protocolVersion,testedTupleDigest; OwnerReceipt receipt;
};
struct Evidence {
    TextRef id,scope; uint64_t writer=0,sequence=0;
    SourceKind source=SourceKind::OwnerObservation; Basis basis=Basis::T5;
    Mutation mutation=Mutation::NoneObserved; UpstreamBinding upstream=UpstreamBinding::NotApplicable;
    TextRef method,locator,sha256; ListRef lineages,parents,limitations,legacyIds;
    OwnerReceipt semanticReceipt;
};
struct Fact {
    TextRef id,subject,scope; CapabilityId capability{}; uint64_t writer=0,sequence=0;
    std::array<TextRef,static_cast<size_t>(Qualifier::Count)> qualifiers{};
    Collection collection=Collection::NotObserved; Assertion assertion;
    ListRef evidence,dependencies,reasons,limitations,legacyFields;
};
// All references are arena offsets, never object addresses. A copy is self contained.
struct Batch {
    static constexpr uint32_t Capacity=65536;
    alignas(8) std::array<std::byte,Capacity> arena{};
    uint32_t used=8; bool overflow=false;
    std::array<uint32_t,32> scopes{},facts{}; std::array<uint32_t,64> evidence{};
    uint8_t scopeCount=0,factCount=0,evidenceCount=0;
    std::string_view Text(TextRef r) const noexcept {
        if(r.offset>used || r.size>used-r.offset) return {};
        return {reinterpret_cast<const char*>(arena.data()+r.offset),r.size};
    }
    template<class T> std::span<const T> List(ListRef r) const noexcept {
        if(r.offset%alignof(T) || r.offset>used || r.count>(used-r.offset)/sizeof(T)) return {};
        return {reinterpret_cast<const T*>(arena.data()+r.offset),r.count};
    }
    template<class T> const T& At(uint32_t offset) const noexcept { return *reinterpret_cast<const T*>(arena.data()+offset); }
};
static_assert(std::is_trivially_copyable_v<Batch>);
class BatchBuilder {
    Batch batch;
    uint32_t Allocate(size_t size,size_t alignment) noexcept {
        const auto pos=(batch.used+static_cast<uint32_t>(alignment)-1)&~(static_cast<uint32_t>(alignment)-1);
        if(size>Batch::Capacity-pos) { batch.overflow=true; return 0; }
        batch.used=pos+static_cast<uint32_t>(size); return pos;
    }
  public:
    TextRef Text(std::string_view value) noexcept {
        if(value.size()>512) { batch.overflow=true; return {}; }
        auto o=Allocate(value.size(),1); if(o) std::memcpy(batch.arena.data()+o,value.data(),value.size());
        return {o,static_cast<uint32_t>(value.size())};
    }
    template<class T> ListRef List(std::span<const T> values) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        auto o=Allocate(values.size_bytes(),alignof(T)); if(o) std::memcpy(batch.arena.data()+o,values.data(),values.size_bytes());
        return {o,static_cast<uint32_t>(values.size())};
    }
    template<class T> ListRef List(std::initializer_list<T> values) noexcept { return List<T>({values.begin(),values.size()}); }
    ListRef Strings(std::initializer_list<std::string_view> values) noexcept {
        std::array<TextRef,32> refs{}; if(values.size()>refs.size()) { batch.overflow=true; return {}; }
        size_t i=0; for(auto v:values) refs[i++]=Text(v); return List<TextRef>({refs.data(),i});
    }
    TextRef AddScope(const Scope& s) noexcept { Add(s,batch.scopes,batch.scopeCount); return s.id; }
    TextRef AddEvidence(const Evidence& e) noexcept { Add(e,batch.evidence,batch.evidenceCount); return e.id; }
    void BindScope(TextRef scopeId,const OwnerStamp& stamp) noexcept {
        for(size_t i=0;i<batch.scopeCount;++i) {
            const auto& scope=batch.At<Scope>(batch.scopes[i]); if(batch.Text(scope.id)!=batch.Text(scopeId)) continue;
            for(const auto& binding:batch.List<OwnerStamp>(scope.bindings)) if(binding.dimension==stamp.dimension) { const_cast<OwnerStamp&>(binding)=stamp; return; }
        } batch.overflow=true;
    }
    void AddFact(const Fact& f) noexcept { Add(f,batch.facts,batch.factCount); }
    Batch Finish() const noexcept { return batch; }
  private:
    template<class T,size_t N> void Add(const T& v,std::array<uint32_t,N>& table,uint8_t& count) noexcept {
        if(count==N) { batch.overflow=true; return; } auto o=Allocate(sizeof(T),alignof(T));
        if(o) { std::memcpy(batch.arena.data()+o,&v,sizeof(T)); table[count++]=o; }
    }
};
enum class PublishResult { Published, Contended, StaleSequence, Closed, InvalidBatch, ProducerNotAllowed, CapacityExceeded, Unavailable };
enum class StageResult { Staged, Contended, StaleSequence, Closed, Capacity };
enum class SequenceState { Reserved, Closed, Exhausted };
struct SequenceResult { SequenceState state=SequenceState::Closed; uint64_t sequence=0; };
enum class Resolution { Unknown, Resolved, Inferred, Conflict };
enum class CaptureState { Complete, Raced, Gap, Unavailable, Closed };
struct Selection { std::vector<CapabilityId> capabilities; };
struct StreamImage { uint64_t writer=0,offered=0,committed=0; ProducerId producer{}; bool closed=false,raced=false; Batch batch; };
struct LegacyAttachment { std::string id,sha256,payload; };
struct Snapshot { std::string session; uint64_t revision=0; std::vector<StreamImage> streams; std::vector<LegacyAttachment> legacyFingerprints; };
struct CaptureResult { CaptureState state=CaptureState::Unavailable; std::shared_ptr<const Snapshot> snapshot; std::string reason; };
struct CurrentStamp { Dimension dimension{}; StampStatus status=StampStatus::Unknown; std::string owner,instance; uint64_t generation=0; };
enum class QueryMode { CurrentEvidence, ExplainAsObserved };
struct QueryRequest {
    CapabilityId capability{}; std::string subject,session;
    std::array<std::string,static_cast<size_t>(Qualifier::Count)> qualifiers{};
    std::vector<CurrentStamp> bindings; QueryMode mode=QueryMode::CurrentEvidence;
};
// Assertion text fields are materialized into the result's own arena.
struct QueryResult {
    Resolution resolution=Resolution::Unknown; std::optional<Assertion> assertion; Batch values;
    std::vector<Assertion> candidates; std::vector<std::string> acceptedFactIds,ignoredFactIds,evidenceIds,lineageIds,reasons;
    std::string originSession; uint64_t snapshotRevision=0; static constexpr bool executionPermission=false;
};
}
