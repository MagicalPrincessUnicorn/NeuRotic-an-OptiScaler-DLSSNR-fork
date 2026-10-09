#pragma once
#include "CapabilityStaging.h"
namespace DlssNr::Capability {
struct OwnedText {
    std::array<char,512> bytes{}; uint16_t size=0;
    bool Assign(std::string_view value) noexcept { size=0; if(value.size()>bytes.size()) return false; std::memcpy(bytes.data(),value.data(),value.size()); size=static_cast<uint16_t>(value.size()); return true; }
    std::string_view View() const noexcept { return {bytes.data(),size}; }
    bool AssignC(const char* value) noexcept { if(!value) return Assign({}); size_t n=0; while(n<=512&&value[n]) ++n; return n<=512?Assign({value,n}):(size=0,false); }
};
struct ConfigObservation { uint64_t sequence=0; std::optional<uint64_t> revision; bool enabled=false; uint32_t route=0; };
struct NativeObservation {
    uint64_t sequence=0,lifecycleSequence=0,lifecycleGeneration=0; std::optional<uint64_t> configRevision;
    bool lifecycleOpen=false;
    std::array<bool,8> states{}; std::array<uint64_t,7> counters{};
    uint8_t observedStates=0xff,observedCounters=0x7f;
    OwnedText failure,layer2Failure; bool reasonsComplete=true;
};
struct PresentObservation { uint64_t sequence=0; std::array<uint64_t,8> counters{}; uint8_t observedCounters=0xff; };
struct PresentChainObservation {
    uint64_t sequence=0,chainIdentity=0,hdrIdentity=0,resources=0;
    std::optional<uint64_t> configRevision; std::array<bool,5> states{};
};
struct ProviderObservation { uint64_t sequence=0; bool known=false,enabled=false,supported=false; uint64_t generation=0; };
struct HdrObservation {
    uint64_t sequence=0,identity=0,descriptorGeneration=0,metadataGeneration=0;
    bool registered=false,colorObserved=false,transitioning=false; OwnedText colorClass;
    std::array<uint64_t,7> metadata{};
};
Batch AdaptConfig(const WriterPort&,const ConfigObservation&) noexcept;
Batch AdaptNative(const WriterPort&,const NativeObservation&) noexcept;
Batch AdaptLifecycle(const WriterPort&,const NativeObservation&) noexcept;
Batch AdaptPresent(const WriterPort&,const PresentObservation&) noexcept;
Batch AdaptPresentChain(const WriterPort&,const PresentChainObservation&) noexcept;
void ObservePresentChain(const PresentChainObservation&) noexcept;
template<class Telemetry> struct PresentObservationScope {
    PresentChainObservation values;
    const Telemetry& telemetry;
    explicit PresentObservationScope(const Telemetry& owner) noexcept:telemetry(owner){}
    ~PresentObservationScope() {
        values.states={telemetry.requested,telemetry.active,telemetry.failed,telemetry.policyBlocked,telemetry.historyResetPending};
        values.resources=telemetry.resourceGeneration;
        ObservePresentChain(values);
    }
};
Batch AdaptProvider(const WriterPort&,const ProviderObservation&) noexcept;
Batch AdaptHdr(const WriterPort&,const HdrObservation&) noexcept;
TextRef RecordText(BatchBuilder&,const WriterPort&,uint64_t,std::string_view ordinal) noexcept;
// Owner adapter helper; no allocation, callbacks, or registration.
class ObservationBuilder {
    const WriterPort& port; uint64_t sequence; TextRef subject;
    unsigned ordinal=0;
  public:
    BatchBuilder values; TextRef scope,evidence;
    ObservationBuilder(const WriterPort&,uint64_t,std::string_view subjectGroup,uint32_t dimensions,
                       ScopeRole role,std::span<const CurrentStamp> bindings={}) noexcept;
    void Source(SourceKind,Basis,Mutation,UpstreamBinding) noexcept;
    void Bind(Dimension,std::string_view owner,std::string_view instance,std::optional<uint64_t> generation) noexcept;
    void Limit(std::string_view) noexcept;
    void Add(CapabilityId,Assertion,std::string_view field={},Collection=Collection::Known,std::string_view category={}) noexcept;
};
}
