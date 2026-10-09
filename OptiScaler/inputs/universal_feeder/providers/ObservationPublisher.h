#pragma once
// Source acquisition only. No canonical semantics, native retention or execution rights.
#include <nr/contracts/C01_Acquisition.h>
#include <nr/contracts/C11_Diagnostics.h>
#include <nr/context/ContextMetadata.h>
#include <optional>

namespace Neurotic::Feed
{
namespace C=Contracts;
struct SourceDescriptor
{
    std::string_view schema;
    C::GraphicsApi api;
    std::string_view boundary;
    std::optional<std::uint64_t> localGeneration;
    C::SourceClass origin=C::SourceClass::Native;
};
// Supplied by the existing source owner under its own synchronization. No pointer-derived IDs.
// seed contains owner-declared descriptors/association, never inherited evaluation values.
struct SourceContext
{
    C::AcquisitionCandidate seed;
    C::RecordKey lineageRoot;
    C::RecordKey callback;
    C::EvidenceRef evidence;
    C::MetadataRef<C::GenerationVector> generations;
    // Optional Native topology enrollment supplied by the actual source owner.
    // It scopes C02/C04 across callbacks; it is not route currency or admission.
    // The callback and Native sample publication still identify this invocation.
    std::optional<C::ScopeRef> enrolledScope;
    // Independent Resource-owner basis for source jitter. It grants no access
    // and does not replace the motion descriptor's coordinate grid.
    C::OptionalFact<C::MetadataRef<C::ResourceView>> jitterBasis;
};
struct ResourceProjection
{
    C::OptionalFact<C::MetadataRef<C::ResourceView>> view;
    C::OptionalFact<C::RecordReference> consumption;
    // Present-but-unknown deliberately clears a seed declaration for this role.
    std::optional<C::OptionalFact<C::MetadataRef<C::ColorDescription>>> color;
};
// Explicit owner port. Implementations may only copy bounded CPU facts and look up metadata
// already established by owners. Project must not query/AddRef/retain the borrowed native object.
// Storage retains immutable metadata; Deliver cannot alter rendering or provider state.
class PublicationPort
{
  public:
    virtual ~PublicationPort()=default;
    virtual std::optional<SourceContext> Begin(const SourceDescriptor&,const void* callbackSubject)=0;
    virtual C::RecordHeader Next(C::ContractId)=0;
    // Legacy ports use their advancing record revision. Concrete journals with
    // immutable revision-one records override this with their publication order.
    virtual std::uint64_t PublicationSequence(const C::RecordHeader& header)const{return header.revision;}
    virtual C::MetadataRef<C::EvidenceVector> StoreEvidence(const C::EvidenceVector&)=0;
    virtual C::MetadataList<C::SemanticClaim,16> StoreClaims(const C::BoundedList<C::SemanticClaim,16>&)=0;
    virtual C::MetadataList<C::OwnerFact,16> StoreFacts(const C::BoundedList<C::OwnerFact,16>&)=0;
    virtual C::MetadataList<C::RecordReference,32> StoreCauses(const C::BoundedList<C::RecordReference,32>&)=0;
    virtual ResourceProjection Project(std::string_view field,const void* callbackBorrow)=0;
    virtual void Deliver(const C::AcquisitionCandidate&,const C::OptionalFact<C::RecordReference>&)=0;
    virtual void DeliverReceipt(const C::OwnerReceipt&)=0;
};
namespace Detail
{
inline thread_local PublicationPort* observer=nullptr;
struct Alias {C::RecordKey root;C::EvidenceRef evidence;C::Symbol transform;bool valid=false;};
inline thread_local const Alias* alias=nullptr;
inline thread_local unsigned nesting=0;
inline thread_local std::uint64_t serial=0,activeRegistration=0;
}
// Host scopes registration to the current callback/thread and owns the port for the whole scope.
// Off is nullptr. Nested registrations restore the previous observer; no process-global frame bag.
class ObservationScope
{
    PublicationPort* previous_;
    std::uint64_t previousRegistration_;
  public:
    explicit ObservationScope(PublicationPort& port) noexcept:previous_(Detail::observer),previousRegistration_(Detail::activeRegistration)
    {
        if(Detail::serial==(std::numeric_limits<std::uint64_t>::max)()){Detail::observer=nullptr;Detail::activeRegistration=0;}
        else {Detail::observer=&port;Detail::activeRegistration=++Detail::serial;}
    }
    ~ObservationScope(){Detail::observer=previous_;Detail::activeRegistration=previousRegistration_;}
    ObservationScope(const ObservationScope&)=delete;
    ObservationScope& operator=(const ObservationScope&)=delete;
};
inline bool Observing() noexcept{return Detail::observer!=nullptr;}
inline const C::AcquisitionCandidate* CurrentCreation(const C::AcquisitionCandidate& creation,
    const C::OptionalFact<C::ProviderIncarnation>& current) noexcept
{
    return Context::Established(current)&&Context::SameFact(creation.providerIncarnation,current)?&creation:nullptr;
}
class Callback
{
    PublicationPort* port_=nullptr;
    SourceDescriptor source_;
    std::optional<SourceContext> context_;
    Detail::Alias alias_;
    bool translated_=false;
    std::size_t emitted_=0;
    std::uint64_t registration_=0;
    template<class T>C::OptionalFact<T> Known(const T& value)const
    {return C::OptionalFact<T>::FromKnown(value,context_->evidence);}
    void Emit(std::string_view field,C::SemanticKind semantic,const C::OptionalFact<C::ScalarValue>& raw,
        const C::OptionalFact<C::ScalarValue>& effective,const C::OptionalFact<C::OwnerValueReference>& overrideSource,
        const ResourceProjection* resource=nullptr,const C::OptionalFact<C::Vec2>* jitter=nullptr,
        std::optional<C::SourceClass> resourceOrigin={})
    {
        if(!Active()||emitted_==64)return;
        if(translated_&&!alias_.valid)return; // missing/overflow lineage cannot become an independent source
        C::AcquisitionCandidate candidate=context_->seed;
        candidate.header=port_->Next(C::ContractId::C01);if(!candidate.sourceSchema.Assign(source_.schema))return;
        // Provider version belongs to the source owner; adapter version cannot establish it.
        candidate.observationRevision=candidate.header.revision;
        candidate.semantic=semantic;
        // Clear all current-evaluation values. Only explicit owner semantic declarations survive.
        candidate.payload={};candidate.contentRevision={};candidate.jitter={};candidate.cameraTransform={};
        candidate.cameraCut={};candidate.rrActive={};candidate.fgActive={};candidate.claims={};candidate.cost={};
        if(semantic!=C::SemanticKind::Color)candidate.color={};
        if(semantic!=C::SemanticKind::Depth)candidate.depth={};
        if(semantic!=C::SemanticKind::Motion&&semantic!=C::SemanticKind::Jitter)candidate.motion={};
        if(semantic!=C::SemanticKind::Exposure)candidate.exposure={};
        candidate.descriptiveLifetime.callbackScope=Known(context_->callback);
        C::BoundedList<C::SemanticClaim,16> claims;
        C::SemanticClaim claim;if(!claim.field.Assign(field))return;
        claim.raw=translated_?C::OptionalFact<C::ScalarValue>{}:raw;
        claim.effective=translated_&&!effective.IsKnown()?raw:effective;claim.overrideSource=overrideSource;
        if(!Context::ValidValues(claim)||!claims.Push(claim))return;
        const auto add=[&](std::string_view name,const C::ScalarValue& value){C::SemanticClaim c;
            c.field.Assign(name);c.raw=Known(value);return claims.Push(c);};
        C::Symbol api;api.Assign(C::EnumName(source_.api));C::Symbol boundary;boundary.Assign(source_.boundary);
        if(!add("source.api",C::ScalarValue{api})||!add("source.boundary",C::ScalarValue{boundary})||
           !add("adapter.version",C::ScalarValue{std::uint64_t{1}}))return;
        if(source_.localGeneration&&!add("source.localGeneration",C::ScalarValue{*source_.localGeneration}))return;
        C::EvidenceVector evidence;
        evidence.provenance=Known(translated_?C::SourceClass::Derived:resourceOrigin.value_or(source_.origin));
        evidence.semanticCertainty=Known(C::SemanticCertainty::Claimed);
        evidence.access=Known(C::DescriptiveAccess::CallbackBorrow);
        if(!evidence.lineage.Push(translated_?alias_.root:context_->lineageRoot))return;
        if(translated_)
        {
            evidence.aliasOf=C::OptionalFact<C::RecordKey>::FromKnown(alias_.root,alias_.evidence);
            if(!add("source.transform",C::ScalarValue{alias_.transform}))return;
        }
        C::OptionalFact<C::RecordReference> right;
        if(resource)
        {
            if(semantic==C::SemanticKind::Color&&resource->color)candidate.color=*resource->color;
            if(const auto* view=resource->view.KnownPart())
                candidate.payload=C::OptionalFact<C::CandidateValue>::FromKnown(C::CandidateValue{view->value},view->evidence);
            right=resource->consumption; // descriptive transport only, no C03 validation here
            if(right.IsKnown()&&(right.KnownPart()->value.contract!=C::ContractId::C03||
                !Context::ValidValues(right)))right={};
        }
        else if(raw.IsKnown())candidate.payload=C::OptionalFact<C::CandidateValue>::FromKnown(
            C::CandidateValue{raw.KnownPart()->value},raw.KnownPart()->evidence);
        if(jitter)
        {
            candidate.jitter=*jitter;
            if(context_->jitterBasis.IsKnown())
            {
                const auto& view=context_->jitterBasis.KnownPart()->value;
                if(!Context::Established(context_->jitterBasis)||view.owner!=C::OwnerDomain::Resource||
                   !Lifecycle::ValidMetadataDescriptor(view))return;
                candidate.payload=C::OptionalFact<C::CandidateValue>::FromKnown(C::CandidateValue{view},context_->jitterBasis.KnownPart()->evidence);
                candidate.motion={};
                C::Symbol pixels,basis;pixels.Assign("Pixels");basis.Assign("payload.raster");
                if(!add("jitter.units",C::ScalarValue{pixels})||!add("jitter.basis",C::ScalarValue{basis}))return;
            }
            else if(candidate.motion.IsKnown())
            {
                C::Symbol pixels,basis;pixels.Assign("Pixels");basis.Assign("motion.currentRaster");
                if(!add("jitter.units",C::ScalarValue{pixels})||!add("jitter.basis",C::ScalarValue{basis}))return;
            }
        }
        candidate.claims=port_->StoreClaims(claims);candidate.evidence=port_->StoreEvidence(evidence);
        if(!Context::ValidValues(candidate))return;
        ++emitted_;port_->Deliver(candidate,right);
        // C11 describes publication; it cannot grant resource access or influence rendering.
        C::OwnerReceipt receipt;receipt.header=port_->Next(C::ContractId::C11);
        receipt.operation.Assign("FEED.SourceObservation");receipt.ownerSequence=port_->PublicationSequence(receipt.header);
        receipt.relevantGenerations=context_->generations;
        C::BoundedList<C::RecordReference,32> causes;C::Symbol type;type.Assign(C::AcquisitionCandidate::WireName);
        causes.Push({C::ContractId::C01,type,candidate.header.record,candidate.header.revision});
        if(right.IsKnown())causes.Push(right.KnownPart()->value);
        receipt.causalRecords=port_->StoreCauses(causes);
        C::BoundedList<C::OwnerFact,16> facts;C::OwnerFact fact;fact.field.Assign("rawKnown");fact.value=Known(C::ScalarValue{raw.IsKnown()});facts.Push(fact);
        receipt.facts=port_->StoreFacts(facts);
        if(Context::ValidValues(receipt))port_->DeliverReceipt(receipt);
    }
  public:
    Callback(SourceDescriptor source,const void* subject) noexcept:port_(Detail::observer),source_(source),registration_(Detail::activeRegistration)
    {
        if(!port_)return;
        if(Detail::alias){translated_=true;alias_=*Detail::alias;}
        try
        {
            if(source.schema.empty()||source.boundary.empty()||C::EnumName(source.api).empty())return;
            context_=port_->Begin(source,subject);
            if(context_&&(context_->seed.provider.Check()!=C::Error::None||
                context_->lineageRoot.Check()!=C::Error::None||context_->callback.Check()!=C::Error::None||
                !Lifecycle::ValidEvidence(context_->evidence)))context_.reset();
        }catch(...){context_.reset();}
    }
    Callback(const Callback&)=delete;Callback& operator=(const Callback&)=delete;
    bool Active()const noexcept{return context_.has_value()&&port_&&port_==Detail::observer&&registration_==Detail::activeRegistration;}
    Detail::Alias Translation(std::string_view transform)const noexcept
    {
        Detail::Alias result;
        if(!Active()||!result.transform.Assign(transform)||transform.empty())return result;
        if(translated_&&!alias_.valid)return result;
        result.root=translated_?alias_.root:context_->lineageRoot;result.evidence=context_->evidence;result.valid=true;return result;
    }
    C::EvidenceRef Evidence()const noexcept{return context_?context_->evidence:C::EvidenceRef{};}
    void Scalar(std::string_view field,const C::OptionalFact<C::ScalarValue>& raw,
        const C::OptionalFact<C::ScalarValue>& effective={},const C::OptionalFact<C::OwnerValueReference>& overrideSource={})noexcept
    {try{Emit(field,C::SemanticKind::Other,raw,effective,overrideSource);}catch(...){context_.reset();}}
    template<class T> void Value(std::string_view field,T value)noexcept
    {
        if(!Active())return;
        if constexpr(std::is_same_v<T,bool>)Scalar(field,Known(C::ScalarValue{value}));
        else if constexpr(std::is_floating_point_v<T>)Scalar(field,std::isfinite(value)?Known(C::ScalarValue{static_cast<double>(value)}):C::OptionalFact<C::ScalarValue>{});
        else Scalar(field,Known(C::ScalarValue{static_cast<std::uint64_t>(value)}));
    }
    void Missing(std::string_view field)noexcept{Scalar(field,{});}
    void Resource(std::string_view field,C::SemanticKind kind,const void* borrow)noexcept
    {
        try{if(!Active())return;const auto projected=borrow?port_->Project(field,borrow):ResourceProjection{};
            Emit(field,kind,Known(C::ScalarValue{borrow!=nullptr}),{},{},&projected);}catch(...){context_.reset();}
    }
    void Jitter(const C::OptionalFact<C::Vec2>& raw)noexcept
    {try{Emit("jitter",C::SemanticKind::Jitter,{},{},{},nullptr,&raw);}catch(...){context_.reset();}}
    // Prepared producers carry independent origins for captured depth and
    // estimated motion. This entry cannot elevate any resource to Native.
    void PreparedResource(std::string_view field,C::SemanticKind kind,const void* borrow,C::SourceClass origin)noexcept
    {
        try{
            if(!Active()||source_.schema!="NeuRotic.PreparedGuides"||
               (origin!=C::SourceClass::HostObserved&&origin!=C::SourceClass::External&&origin!=C::SourceClass::Derived))return;
            const auto projected=borrow?port_->Project(field,borrow):ResourceProjection{};
            Emit(field,kind,Known(C::ScalarValue{borrow!=nullptr}),{},{},&projected,nullptr,origin);
        }catch(...){context_.reset();}
    }
};
class TranslationScope
{
    const Detail::Alias* previous_;
    Detail::Alias current_;
  public:
    TranslationScope(const Callback& parent,std::string_view transform)noexcept:
        previous_(Detail::alias),current_(parent.Translation(transform))
    {if(++Detail::nesting>16)current_.valid=false;Detail::alias=&current_;}
    ~TranslationScope(){Detail::alias=previous_;--Detail::nesting;}
    TranslationScope(const TranslationScope&)=delete;TranslationScope& operator=(const TranslationScope&)=delete;
};
}
