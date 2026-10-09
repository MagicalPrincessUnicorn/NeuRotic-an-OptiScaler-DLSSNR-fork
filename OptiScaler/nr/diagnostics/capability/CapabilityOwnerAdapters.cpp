#include "CapabilityOwnerAdapters.h"
#include <charconv>
namespace DlssNr::Capability {
TextRef RecordText(BatchBuilder& b,const WriterPort& p,uint64_t sequence,std::string_view ordinal) noexcept {
    std::array<char,192> text{}; size_t n=p.Session().size(); std::memcpy(text.data(),p.Session().data(),n);
    std::memcpy(text.data()+n,":w",2); n+=2; auto w=std::to_chars(text.data()+n,text.data()+text.size(),p.Id()); n=static_cast<size_t>(w.ptr-text.data());
    std::memcpy(text.data()+n,":s",2); n+=2; auto s=std::to_chars(text.data()+n,text.data()+text.size(),sequence); n=static_cast<size_t>(s.ptr-text.data());
    text[n++]=':'; if(n+ordinal.size()>128) return {}; std::memcpy(text.data()+n,ordinal.data(),ordinal.size()); return b.Text({text.data(),n+ordinal.size()});
}
ObservationBuilder::ObservationBuilder(const WriterPort& p,uint64_t seq,std::string_view name,uint32_t dimensions,ScopeRole role,std::span<const CurrentStamp> context) noexcept : port(p),sequence(seq) {
    subject=values.Text(name); Scope s{}; s.id=RecordText(values,p,seq,"scope"); s.session=values.Text(p.Session()); s.role=role;
    std::array<OwnerStamp,22> bindings{}; size_t count=0;
    for(unsigned i=0;i<22;++i) if(dimensions&(1u<<i)) {
        auto& token=bindings[count++]; token.dimension=static_cast<Dimension>(i);
        if(i==0) { token.status=StampStatus::Known; token.owner=values.Text("diagnostics"); token.instance=values.Text(p.Session()); }
        for(const auto& stamp:context) if(stamp.dimension==token.dimension) { token.status=stamp.status; token.owner=values.Text(stamp.owner); token.instance=values.Text(stamp.instance); token.generation=stamp.generation; }
    }
    s.bindings=values.List<OwnerStamp>({bindings.data(),count}); scope=values.AddScope(s);
    Evidence e{}; e.id=RecordText(values,p,seq,"evidence"); e.scope=scope; e.writer=p.Id(); e.sequence=seq;
    e.method=values.Text("owner_value_copy"); e.lineages=values.List<TextRef>({e.id}); evidence=values.AddEvidence(e);
}
void ObservationBuilder::Source(SourceKind source,Basis basis,Mutation mutation,UpstreamBinding upstream) noexcept {
    // Builder owns the arena; this is initialization, not mutation of a published image.
    auto image=values.Finish(); auto e=image.At<Evidence>(image.evidence[0]);
    e.id=RecordText(values,port,sequence,"source"); e.source=source; e.basis=basis; e.mutation=mutation; e.upstream=upstream;
    evidence=values.AddEvidence(e);
}
void ObservationBuilder::Bind(Dimension dimension,std::string_view owner,std::string_view instance,std::optional<uint64_t> generation) noexcept {
    OwnerStamp stamp; stamp.dimension=dimension;
    if(generation) { stamp.status=StampStatus::Known; stamp.owner=values.Text(owner); stamp.instance=values.Text(instance); stamp.generation=*generation; }
    values.BindScope(scope,stamp);
}
void ObservationBuilder::Limit(std::string_view text) noexcept {
    auto image=values.Finish(); auto e=image.At<Evidence>(image.evidence[image.evidenceCount-1]);
    e.id=RecordText(values,port,sequence,"limited-source"); e.limitations=values.Strings({text}); evidence=values.AddEvidence(e);
}
void ObservationBuilder::Add(CapabilityId id,Assertion assertion,std::string_view field,Collection collection,std::string_view category) noexcept {
    std::array<char,32> name{}; name[0]='f'; auto converted=std::to_chars(name.data()+1,name.data()+name.size(),ordinal++);
    Fact f{}; f.id=RecordText(values,port,sequence,{name.data(),static_cast<size_t>(converted.ptr-name.data())}); f.subject=subject; f.scope=scope; f.writer=port.Id(); f.sequence=sequence; f.capability=id; f.collection=collection; f.assertion=assertion;
    f.qualifiers[0]=values.Text(field); f.qualifiers[static_cast<size_t>(Qualifier::ScopeCategory)]=values.Text(category);
    f.evidence=values.List<TextRef>({evidence}); values.AddFact(f);
}
namespace {
Assertion Boolean(bool v) noexcept { Assertion a; a.kind=AssertionKind::Scalar; a.scalar=ScalarType::Bool; a.unsignedValue=v; return a; }
Assertion Number(uint64_t v) noexcept { Assertion a; a.kind=AssertionKind::Scalar; a.scalar=ScalarType::U64; a.unsignedValue=v; return a; }
Assertion Code(AssertionKind kind,bool positive) noexcept { Assertion a; a.kind=kind; a.code=positive?0:1; return a; }
constexpr uint32_t Domains(CapabilityId id) noexcept { return Registry[static_cast<size_t>(id)].domains; }
}
Batch AdaptConfig(const WriterPort& port,const ConfigObservation& o) noexcept {
    ObservationBuilder b(port,o.sequence,"nr",Domains(CapabilityId::ConfigRequestedEnabled),ScopeRole::OwnerIdentity);
    b.Bind(Dimension::Configuration,"config","nr",o.revision);
    b.Add(CapabilityId::ConfigRequestedEnabled,Code(AssertionKind::Enablement,o.enabled));
    b.Add(CapabilityId::ConfigRequestedRoute,Number(o.route)); return b.values.Finish();
}
Batch AdaptNative(const WriterPort& port,const NativeObservation& o) noexcept {
    ObservationBuilder b(port,o.sequence,"native",Domains(CapabilityId::NativeOwnerState),ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Configuration,"config","nr",o.configRevision);
    b.Bind(Dimension::Lifecycle,"native","session",o.lifecycleGeneration?std::optional<uint64_t>(o.lifecycleGeneration):std::nullopt);
    b.Limit(o.reasonsComplete?o.failure.View():"owner reason exceeded bounded storage");
    constexpr std::array<std::string_view,8> states{"running","transitionPending","outputQuarantined","failed","modelLoaded","preSrDisplayReady","resetPending","nativeRayReconstructionActive"};
    constexpr std::array<std::string_view,7> counters{"frames","gameResets","featureBuilds","featureRebuilds","evaluateFailures","successfulEvaluations","completedPipelineEvaluations"};
    for(size_t i=0;i<states.size();++i) b.Add(CapabilityId::NativeOwnerState,Boolean(o.states[i]),states[i],
        o.observedStates&(1u<<i)?Collection::Known:Collection::NotObserved);
    for(size_t i=0;i<counters.size();++i) b.Add(CapabilityId::NativeCounter,Number(o.counters[i]),counters[i],
        o.observedCounters&(1u<<i)?Collection::Known:Collection::NotObserved);
    // Reasons are owned descriptive limitations, not new registry predicates.
    auto result=b.values.Finish(); return result;
}
Batch AdaptLifecycle(const WriterPort& port,const NativeObservation& o) noexcept {
    ObservationBuilder b(port,o.sequence,"native",Domains(CapabilityId::LifecycleOpen),ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Lifecycle,"native","session",o.lifecycleGeneration?std::optional<uint64_t>(o.lifecycleGeneration):std::nullopt);
    b.Add(CapabilityId::LifecycleOpen,Code(AssertionKind::Availability,o.lifecycleOpen)); return b.values.Finish();
}
Batch AdaptPresent(const WriterPort& port,const PresentObservation& o) noexcept {
    ObservationBuilder b(port,o.sequence,"present",Domains(CapabilityId::PresentCounter),ScopeRole::OwnerIdentity);
    constexpr std::array<std::string_view,8> fields{"modelEvaluations","compositeEvaluations","skippedFrames","modelSubmissions","compositeSubmissions","presentAttempts","consecutiveFallbacks","pendingSlots"};
    for(size_t i=0;i<fields.size();++i) b.Add(CapabilityId::PresentCounter,Number(o.counters[i]),fields[i],
        o.observedCounters&(1u<<i)?Collection::Known:Collection::NotObserved,"process_summary"); return b.values.Finish();
}
Batch AdaptProvider(const WriterPort& port,const ProviderObservation& o) noexcept {
    ObservationBuilder b(port,o.sequence,"fg",Domains(CapabilityId::ProviderOwnerSupport),ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Provider,"fg_owner","fg",o.known?std::optional<uint64_t>(o.generation):std::nullopt);
    b.Add(CapabilityId::ProviderOwnerSupport,Code(AssertionKind::Support,o.supported),{},o.known?Collection::Known:Collection::NotObserved);
    b.Add(CapabilityId::ProviderEnabled,Code(AssertionKind::Enablement,o.enabled),{},o.known?Collection::Known:Collection::NotObserved); return b.values.Finish();
}
Batch AdaptPresentChain(const WriterPort& port,const PresentChainObservation& o) noexcept {
    std::array<char,64> subject{}; std::memcpy(subject.data(),"present-chain-",14); auto n=std::to_chars(subject.data()+14,subject.data()+subject.size(),o.chainIdentity);
    ObservationBuilder b(port,o.sequence,{subject.data(),static_cast<size_t>(n.ptr-subject.data())},Domains(CapabilityId::PresentOwnerState),ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Api,"present","D3D12",0);
    b.Bind(Dimension::Swapchain,"hdr_observer","structural_identity",o.chainIdentity?std::optional<uint64_t>(o.chainIdentity):std::nullopt);
    b.Bind(Dimension::HdrDescriptor,"hdr_observer","descriptor",o.hdrIdentity?std::optional<uint64_t>(o.hdrIdentity):std::nullopt);
    b.Bind(Dimension::Configuration,"config","nr",o.configRevision);
    b.Bind(Dimension::ResourceSet,"present","resources",o.resources?std::optional<uint64_t>(o.resources):std::nullopt);
    constexpr std::array<std::string_view,5> fields{"requested","active","failed","policyBlocked","historyResetPending"};
    if(!o.chainIdentity) b.Limit("Exact chain identity unavailable at this owner return; prior observation replaced.");
    for(size_t i=0;i<fields.size();++i) b.Add(CapabilityId::PresentOwnerState,Boolean(o.states[i]),fields[i],o.chainIdentity?Collection::Known:Collection::NotObserved); return b.values.Finish();
}
Batch AdaptHdr(const WriterPort& port,const HdrObservation& o) noexcept {
    std::array<char,64> subject{}; std::memcpy(subject.data(),"hdr-chain-",10); auto n=std::to_chars(subject.data()+10,subject.data()+subject.size(),o.identity);
    ObservationBuilder b(port,o.sequence,{subject.data(),static_cast<size_t>(n.ptr-subject.data())},Domains(CapabilityId::HdrColorClass)|Domains(CapabilityId::HdrMetadata),ScopeRole::ObservationCorrelation);
    b.Bind(Dimension::Swapchain,"hdr_observer","structural_identity",o.identity?std::optional<uint64_t>(o.identity):std::nullopt);
    b.Bind(Dimension::HdrDescriptor,"hdr_observer","descriptor",o.registered?std::optional<uint64_t>(o.descriptorGeneration):std::nullopt);
    b.Bind(Dimension::HdrMetadata,"hdr_observer","metadata",o.registered?std::optional<uint64_t>(o.metadataGeneration):std::nullopt);
    Assertion a; a.kind=AssertionKind::Scalar; a.scalar=ScalarType::Text; a.text=b.values.Text(o.colorClass.View());
    b.Add(CapabilityId::HdrColorClass,a,"colorClass",o.colorObserved&&!o.transitioning?Collection::Known:Collection::NotObserved);
    constexpr std::array<std::string_view,7> fields{"metadataType","metadataSize","metadataHash","metadataResult","requestedMetadataType","requestedMetadataSize","requestedMetadataHash"};
    for(size_t i=0;i<fields.size();++i) b.Add(CapabilityId::HdrMetadata,Number(o.metadata[i]),fields[i]); return b.values.Finish();
}
}
