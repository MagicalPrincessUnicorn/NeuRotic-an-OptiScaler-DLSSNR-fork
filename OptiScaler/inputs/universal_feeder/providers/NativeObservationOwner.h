#pragma once
#include "ObservationPublisher.h"
#include <nr/lifecycle/OwnerPublicationJournal.h>
#include <nr/lifecycle/OwnerMetadataArena.h>
#include <nr/context/NativeSampleQualification.h>
#include <span>

namespace Neurotic::Feed
{
// Concrete bounded source publication at one authenticated host callback. The
// host owns this object and its metadata arena through downstream owner drain.
// Resource projections are supplied by the allocation owner before entry; this
// class never dereferences, retains or derives identities from callback borrows.
// Missing topology/descriptors remain missing. NFC alone interprets semantics.
class NativeObservationOwner final:public PublicationPort
{
  public:
    struct Resource {C::Symbol field;const void* borrow=nullptr;ResourceProjection projection;};
    using Resources=C::BoundedList<Resource,8>;
  private:
    Lifecycle::OwnerPublicationJournal& journal_;
    Lifecycle::OwnerMetadataArena::Publisher metadata_;
    SourceContext source_;
    C::Symbol schema_,boundary_;
    C::GraphicsApi api_;
    C::SourceClass origin_;
    std::optional<std::uint64_t> generation_;
    const void* subject_=nullptr;
    Resources resources_;
    C::BoundedList<C::AcquisitionCandidate,64> candidates_;
    C::BoundedList<C::OwnerReceipt,64> receipts_;
    std::optional<C::RecordHeader> expected_;
    std::optional<C::ObservationSet> observations_;
    bool healthy_=false,begun_=false,closed_=false;
    [[noreturn]]void Refuse(){healthy_=false;throw Lifecycle::MetadataRefusal{};}
    template<class T,std::size_t N>C::MetadataList<T,N> List(const C::BoundedList<T,N>& body)
    try
    {
        if(!healthy_||closed_||!begun_)Refuse();
        C::MetadataList<T,N> result;result.count=static_cast<std::uint32_t>(body.Size());
        if(body.Size())result.backing=metadata_.Publish(body);return result;
    }
    catch(...){Refuse();}
  public:
    NativeObservationOwner(Lifecycle::OwnerPublicationJournal& sourceOwner,Lifecycle::OwnerMetadataArena& arena,
        const SourceContext& source,const SourceDescriptor& descriptor,const void* callbackSubject,const Resources& resources):
        journal_(sourceOwner),metadata_(arena.ForOwner(C::OwnerDomain::Provider)),source_(source),api_(descriptor.api),
        origin_(descriptor.origin),generation_(descriptor.localGeneration),subject_(callbackSubject),resources_(resources)
    {
        if(journal_.Binding().owner!=C::OwnerDomain::Provider||source.seed.provider!=journal_.Binding().subject||
           !subject_||!schema_.Assign(descriptor.schema)||schema_.Empty()||!boundary_.Assign(descriptor.boundary)||boundary_.Empty()||
           api_!=C::GraphicsApi::D3D12||source.callback.Check()!=C::Error::None||source.lineageRoot.Check()!=C::Error::None||
           !Lifecycle::ValidEvidence(source.evidence)||!Context::ResolveMetadata(source.seed.frame,metadata_)||
           !Context::ResolveMetadata(source.seed.masks,metadata_)||!Context::ResolveMetadata(source.generations,metadata_))return;
        if(source.enrolledScope)
        {
            const auto* frame=Context::ResolveMetadata(source.seed.frame,metadata_);
            if(!frame||!Context::ResolveNativeSample(*frame,metadata_)||
               !source.enrolledScope->key||!Context::ValidValues(*source.enrolledScope))return;
        }
        for(std::size_t i=0;i<resources_.Size();++i)
        {
            const auto& resource=*resources_.Get(i);
            if(resource.field.Empty()||!resource.borrow||!Context::ValidValues(resource.projection.view)||
               !Context::ValidValues(resource.projection.consumption))return;
            if(resource.projection.view.IsKnown()&&
               (resource.projection.view.KnownPart()->value.owner!=C::OwnerDomain::Resource||
                !Context::ResolveOptional(resource.projection.view,metadata_)))return;
            if(resource.projection.color&&(!Context::ValidValues(*resource.projection.color)||
                (resource.projection.color->IsKnown()&&!Context::ResolveOptional(*resource.projection.color,metadata_))))return;
            for(std::size_t j=0;j<i;++j)if(resources_.Get(j)->field==resource.field)return;
        }
        healthy_=true;
    }
    NativeObservationOwner(const NativeObservationOwner&)=delete;
    NativeObservationOwner& operator=(const NativeObservationOwner&)=delete;
    bool Healthy()const noexcept{return healthy_;}
    const SourceContext& Source()const noexcept{return source_;}
    std::optional<SourceContext> Begin(const SourceDescriptor& descriptor,const void* subject)override
    {
        if(!healthy_||closed_||begun_||observations_||subject!=subject_||descriptor.schema!=schema_.View()||
           descriptor.api!=api_||descriptor.origin!=origin_||descriptor.boundary!=boundary_.View()||descriptor.localGeneration!=generation_)return {};
        begun_=true;return source_;
    }
    C::RecordHeader Next(C::ContractId contract)override
    try
    {
        if(!healthy_||closed_||!begun_||observations_||
           (contract!=C::ContractId::C01&&contract!=C::ContractId::C11))Refuse();
        expected_=journal_.Header(contract,{source_.callback});return *expected_;
    }
    catch(...){Refuse();}
    std::uint64_t PublicationSequence(const C::RecordHeader& header)const override
    {return healthy_&&!closed_&&expected_&&*expected_==header?header.record.value:0;}
    C::MetadataRef<C::EvidenceVector> StoreEvidence(const C::EvidenceVector& value)override
    try
    {
        if(!healthy_||closed_||!begun_)Refuse();
        auto published=value;
        const auto* frame=Context::ResolveMetadata(source_.seed.frame,metadata_);if(!frame)Refuse();
        if(frame->nativeSample)
        {
            const auto* sample=Context::ResolveNativeSample(*frame,metadata_);
            if(!sample||sample->callback!=source_.callback||value.ageInRealFrames.IsKnown())Refuse();
            const auto freshness=Context::NativeFreshness(*frame->nativeSample,*sample);
            if(value.nativeFreshness&&*value.nativeFreshness!=freshness)Refuse();
            published.nativeFreshness=freshness;
        }
        else if(value.nativeFreshness)Refuse();
        return metadata_.Publish(published);
    }
    catch(...){Refuse();}
    C::MetadataList<C::SemanticClaim,16> StoreClaims(const C::BoundedList<C::SemanticClaim,16>& value)override{return List(value);}
    C::MetadataList<C::OwnerFact,16> StoreFacts(const C::BoundedList<C::OwnerFact,16>& value)override{return List(value);}
    C::MetadataList<C::RecordReference,32> StoreCauses(const C::BoundedList<C::RecordReference,32>& value)override{return List(value);}
    ResourceProjection Project(std::string_view field,const void* borrow)override
    {
        if(!healthy_||closed_||!begun_||!borrow)return {};
        for(const auto& value:resources_)if(value.field.View()==field&&value.borrow==borrow)return value.projection;
        return {};
    }
    void Deliver(const C::AcquisitionCandidate& value,const C::OptionalFact<C::RecordReference>&)override
    {
        if(!healthy_||closed_||observations_||!expected_||value.header!=*expected_||value.Check()!=C::Error::None||
           !Context::ValidValues(value)||value.provider!=source_.seed.provider||value.frame!=source_.seed.frame||
           !candidates_.Push(value))Refuse();
        expected_.reset();
    }
    void DeliverReceipt(const C::OwnerReceipt& value)override
    {
        if(!healthy_||closed_||observations_||!expected_||value.header!=*expected_||value.Check()!=C::Error::None||
           !Context::ValidValues(value)||!receipts_.Push(value))Refuse();
        expected_.reset();
    }
    std::span<const C::AcquisitionCandidate> Candidates()const noexcept{return {candidates_.begin(),candidates_.Size()};}
    std::span<const C::OwnerReceipt> Receipts()const noexcept{return {receipts_.begin(),receipts_.Size()};}
    std::optional<C::ObservationSet> Observations()
    {
        if(!healthy_||!begun_||closed_)return {};
        if(observations_)return observations_;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;C::Symbol type;type.Assign(C::AcquisitionCandidate::WireName);
        for(const auto& value:candidates_)if(!refs.Push({type,value.header.record,value.header.revision}))Refuse();
        C::ObservationSet set;set.header=Next(C::ContractId::C01);set.candidates=List(refs);set.boundary=source_.seed.boundary;
        if(set.Check()!=C::Error::None||!Context::ValidValues(set))Refuse();
        expected_.reset();observations_=set;return observations_;
    }
    void Close()noexcept
    {closed_=true;subject_=nullptr;for(auto& value:resources_)value.borrow=nullptr;}
};
}
