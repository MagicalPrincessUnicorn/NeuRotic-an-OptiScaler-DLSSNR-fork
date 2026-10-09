#include "CapabilityNgxObservation.h"
#include <charconv>
namespace DlssNr::Capability {
namespace {
constexpr std::array<std::string_view,3> ApiNames{"D3D11","D3D12","Vulkan"};
struct PassivePorts {
    std::array<std::optional<WriterPort>,3> raw,effective;
    std::optional<WriterPort> parameter,synthetic;
};
std::atomic<PassivePorts*> passivePorts{nullptr};
void ObserveRequirements(const RequirementsEnvelope& input) noexcept {
    auto* ports=passivePorts.load(); if(!ports) return; auto index=static_cast<size_t>(input.api); if(index>=3) return;
    auto& port=input.origin==RequirementsOrigin::Raw?ports->raw[index]:ports->effective[index]; if(!port) return;
    auto seq=ReserveSample(*port); if(seq.state!=SequenceState::Reserved) return; auto envelope=input; envelope.sequence=seq.sequence;
    auto image=AdaptRequirements(*port,envelope); (void)TryStageEnvelope(*port,seq.sequence,image);
}
void ObserveParameter(const ParameterEnvelope& input) noexcept {
    auto* ports=passivePorts.load(); if(!ports) return; auto& port=input.synthetic?ports->synthetic:ports->parameter; if(!port) return;
    auto seq=ReserveSample(*port); if(seq.state!=SequenceState::Reserved) return; auto envelope=input; envelope.sequence=seq.sequence;
    auto image=AdaptParameter(*port,envelope); (void)TryStageEnvelope(*port,seq.sequence,image);
}
}
void InitializePassiveObservers(AdapterFactory& factory) noexcept {
    if(passivePorts.load()) return;
    try {
        auto ports=std::make_unique<PassivePorts>();
        for(size_t i=0;i<3;++i) { ports->raw[i]=factory.RegisterWriter(ProducerId::Ngx,ApiNames[i]).port; ports->effective[i]=factory.RegisterWriter(ProducerId::Ngx,ApiNames[i]).port; }
        ports->parameter=factory.RegisterWriter(ProducerId::Parameters,"parameter_readback").port;
        ports->synthetic=factory.RegisterWriter(ProducerId::Parameters,"synthetic_defaults").port;
        // Process-retained CPU cells, matching the status service's late-callback lifetime.
        passivePorts.store(ports.release()); SetRequirementsObserver(ObserveRequirements); SetParameterObserver(ObserveParameter);
    } catch(...) { /* No stream is required for rendering. */ }
}
Batch AdaptRequirements(const WriterPort& port,const RequirementsEnvelope& o) noexcept {
    const auto index=static_cast<size_t>(o.api); if(index>=3) { Batch failed; failed.overflow=true; return failed; }
    auto id=o.origin==RequirementsOrigin::Raw?CapabilityId::NgxRawRequirements:CapabilityId::NgxEffectiveRequirements;
    std::array<char,32> subject{}; std::memcpy(subject.data(),"ngx-",4); auto api=ApiNames[index]; std::memcpy(subject.data()+4,api.data(),api.size());
    ObservationBuilder b(port,o.sequence,{subject.data(),4+api.size()},Lookup(id)->domains,ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Api,"ngx_boundary",api,0);
    b.Source(o.origin==RequirementsOrigin::Raw?SourceKind::RawApiBoundary:SourceKind::EffectivePolicy,
             o.origin==RequirementsOrigin::Raw?Basis::T5:Basis::T0,
             o.origin==RequirementsOrigin::Raw?Mutation::Unknown:Mutation::Mutated,UpstreamBinding::Unverified);
    constexpr std::array<std::string_view,3> fields{"return_code","FeatureSupported","MinHWArchitecture"};
    const std::array<uint64_t,3> values{o.result,o.supported,o.minimumArchitecture};
    // Build qualifiers with no pointer retention or additional API access.
    for(size_t i=0;i<3;++i) {
        Assertion a; a.kind=AssertionKind::Scalar; a.scalar=ScalarType::U64; a.unsignedValue=values[i];
        b.Add(id,a,fields[i],i==0||o.outputDefined?Collection::Known:Collection::NotObserved);
    }
    auto result=b.values.Finish();
    std::array<char,32> feature{}; auto n=std::to_chars(feature.data(),feature.data()+feature.size(),o.feature);
    auto append=[&](std::string_view text){ TextRef ref{result.used,static_cast<uint32_t>(text.size())}; if(text.size()>Batch::Capacity-result.used) {result.overflow=true; return TextRef{};} std::memcpy(result.arena.data()+result.used,text.data(),text.size()); result.used+=ref.size; return ref; };
    auto featureNamespace=append("NVSDK_NGX_Feature"); auto featureId=append({feature.data(),static_cast<size_t>(n.ptr-feature.data())});
    for(size_t i=0;i<result.factCount;++i) { auto& f=*reinterpret_cast<Fact*>(result.arena.data()+result.facts[i]); f.qualifiers[static_cast<size_t>(Qualifier::FeatureNamespace)]=featureNamespace; f.qualifiers[static_cast<size_t>(Qualifier::FeatureId)]=featureId; }
    return result;
}
Batch AdaptParameter(const WriterPort& port,const ParameterEnvelope& o) noexcept {
    auto id=o.synthetic?CapabilityId::ParameterSyntheticDefaults:CapabilityId::ParameterStorage;
    ObservationBuilder b(port,o.sequence,"parameters",Lookup(id)->domains,ScopeRole::ObservationCorrelation);
    Assertion a;
    if(o.synthetic) { b.Source(SourceKind::SyntheticDefault,Basis::T0,Mutation::Mutated,UpstreamBinding::NotApplicable); a.kind=AssertionKind::Availability; a.code=0; }
    else { a.kind=AssertionKind::Result; a.code=o.readbackSucceeded?0:1; a.sideEffects=o.possibleWrite?2:0; }
    b.Add(id,a); auto result=b.values.Finish();
    if(!o.synthetic) {
        auto append=[&](std::string_view text){ TextRef ref{result.used,static_cast<uint32_t>(text.size())}; std::memcpy(result.arena.data()+result.used,text.data(),text.size()); result.used+=ref.size; return ref; };
        auto& f=*reinterpret_cast<Fact*>(result.arena.data()+result.facts[0]); f.qualifiers[static_cast<size_t>(Qualifier::ParameterName)]=append(o.name.View()); f.qualifiers[static_cast<size_t>(Qualifier::ParameterType)]=append(o.type.View()); f.qualifiers[static_cast<size_t>(Qualifier::ParameterPhase)]=append(o.phase.View());
    } return result;
}
}
