#pragma once
#include "NativeOwnerSet.h"
#include "NativeSourceTransactionObservation.h"
#include "NativeNgxSourceDeclarations.h"
#include "NativeContextKeyInterner.h"
#include "NativeRepresentationPreparation.h"
#include <dlssnr/NativeNgxCallCapture.h>
#include <inputs/universal_feeder/providers/NativeObservationOwner.h>
#include <inputs/universal_feeder/providers/NgxObservationAdapter.h>
#include <nr/context/NativeContextQualification.h>

namespace Neurotic::Lifecycle
{
// One retained CPU observation closure for one authentic outer Native sample.
// Bootstrap serializes its owner journals and publishes only immutable captured
// Gets here. No external parameter/COM calls, recording, C03 or final-frame claims.
class NativeSourcePublication
{
    friend class NativeProcessBootstrap;
    static constexpr std::size_t ObservationCount=28;
    NativeOwnerSet* owners_;
    std::unique_ptr<OwnerMetadataArena> metadata_;
    std::unique_ptr<Feed::NativeObservationOwner> owner_;
    std::optional<C::ObservationSet> observations_;
    bool attempted_=false,complete_=false;
    std::shared_ptr<const NativeSourceTransactionObservation> transaction_;
    std::optional<C::MetadataRef<C::InterceptedSourceTransactionV1>> transactionSubject_;
    std::shared_ptr<const DlssNr::StreamlineSourceScope::Observation> sourceFrame_;
    std::optional<C::MetadataRef<C::ResourceView>> renderView_,outputView_;
    std::array<std::optional<C::ColorDescription>,2> declaredColors_;
    bool contextAttempted_=false;
    std::optional<Context::SemanticProfile> contextPolicy_;
    std::optional<Context::ContextRequest> contextRequest_;
    Context::NativeContextResult contextResult_;
    bool representationsAttempted_=false;
    std::unique_ptr<NativeContextKeyInterner> representationKeys_;
    NativeRepresentationPreparation representationResult_;
    struct RepresentationStore
    {
        NativeSourcePublication& source;OwnerMetadataArena::Publisher publisher;NativeContextKeyInterner& keys;
        template<class T>auto Publish(const T& value,C::OwnerDomain owner=C::OwnerDomain::Context){return publisher.Publish(value,owner);}
        template<class T>auto Resolve(const C::MetadataRef<T>& reference)const{return source.Resolve(reference);}
        const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& reference)const{return source.ResolveCandidate(reference);}
        template<class T>C::RecordKey Intern(const T& key){return keys.Intern(key);}
    };
    explicit NativeSourcePublication(NativeOwnerSet& owners):
        owners_(&owners),metadata_(owners.NewInvocationMetadata(8*1024*1024,512)){}
    bool Publish(NativeOwnerSet& owners,const C::NativeSampleIdentityV1& sample,const C::ScopeRef& scope,
        const C::OptionalFact<C::ProviderIncarnation>& incarnation,std::uint64_t generation,
        const DlssNr::NativeNgxCreationParameters& creation,
        const DlssNr::NativeNgxCallCapture& call,
        const std::array<const C::ResourceView*,DlssNr::NativeNgxCallCapture::ResourceNames().size()>& views,
        const C::InterceptedSourceTransactionV1* transaction=nullptr,
        const NativeProducerColorBinding* producer=nullptr)noexcept
    {
        if(attempted_)return false;attempted_=true;
        try
        {
            if(sample.Check()!=C::Error::None||!scope.key||!Context::ValidValues(scope)||!Context::Established(incarnation))return false;
            auto provider=metadata_->ForOwner(C::OwnerDomain::Provider);
            auto resource=metadata_->ForOwner(C::OwnerDomain::Resource);
            Feed::SourceContext source;source.callback=sample.callback;source.enrolledScope=scope;
            source.evidence=owners.SourceJournal().Event().evidence;source.lineageRoot=source.evidence.record;
            source.seed.provider=owners.SourceJournal().Binding().subject;source.seed.providerIncarnation=incarnation;
            const auto known=[&]<class T>(const T& value){return C::OptionalFact<T>::FromKnown(value,source.evidence);};
            C::FrameIdentity frame;frame.sessionId=known(sample.session);frame.renderStreamId=known(sample.stream);frame.viewId=known(sample.view);
            if(transaction)
            {
                // Identity was issued by the existing Render/Episode owners at
                // enrollment. Resource supplies the observed device here. This
                // publication grants neither original-frame truth nor use rights.
                auto subject=*transaction;
                if(subject.Check()!=C::Error::None||subject.sample!=sample||!views[0]||
                   !Context::Established(views[0]->device))return false;
                if(Context::Established(subject.device)&&!Context::SameFact(subject.device,views[0]->device))return false;
                subject.device=views[0]->device;
                transactionSubject_=metadata_->ForOwner(C::OwnerDomain::IdentityRegistry).Publish(subject);
                frame.episodeId=C::OptionalFact<C::EpisodeId>::FromKnown(subject.episode,{transactionSubject_->record});
            }
            frame.nativeSample=provider.Publish(sample);source.seed.frame=provider.Publish(frame);
            source.seed.masks=provider.Publish(C::MaskSet{});source.generations=provider.Publish(C::GenerationVector{});
            source.seed.boundary.kind=known(C::BoundaryKind::BeforeUpscale);
            source.seed.boundary.episode=frame.episodeId;
            const auto declarations=DeclareNativeNgxSource(creation,call,owners.Resources(),views,*frame.nativeSample,source.evidence,producer,&sample);
            const auto& declaredViews=declarations?declarations->views:views;
            if(declarations)
            {
                for(unsigned i=0;i<2;++i)declaredColors_[i]=producer?
                    declarations->producerColors[i]:std::optional<C::ColorDescription>{declarations->color};
                source.seed.color=known(provider.Publish(declarations->color));
                source.seed.depth=known(provider.Publish(declarations->depth));
                source.seed.motion=known(provider.Publish(declarations->motion));
                source.jitterBasis=known(resource.Publish(*declaredViews[0]));
                renderView_=source.jitterBasis.KnownPart()->value;
            }
            // Provider source/API version and all downstream frame identities
            // stay unknown. These are source claims, never NFC qualification.
            Feed::NativeObservationOwner::Resources resources;
            const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
            for(std::size_t i=0;i<names.size();++i)
            {
                void* borrow=nullptr;
                if(call.Get(names[i],&borrow)!=0||!borrow){if(declaredViews[i])return false;continue;}
                if(!declaredViews[i]||!Context::CompleteResourceStructure(*declaredViews[i]))return false;
                const auto reference=resource.Publish(*declaredViews[i]);
                if(declarations&&i==1)outputView_=reference;
                Feed::ResourceProjection projection{known(reference),{}};
                if(producer&&i<2)
                {
                    projection.color=C::OptionalFact<C::MetadataRef<C::ColorDescription>>{};
                    if(declarations&&declarations->producerColors[i])
                        projection.color=known(provider.Publish(*declarations->producerColors[i]));
                }
                C::Symbol field;if(!field.Assign(names[i])||!resources.Push({field,borrow,projection}))return false;
            }
            const Feed::SourceDescriptor descriptor{"NGX",C::GraphicsApi::D3D12,"evaluate",generation,
                call.DeclaresPreparedSource()?C::SourceClass::External:C::SourceClass::Native};
            owner_=std::make_unique<Feed::NativeObservationOwner>(owners.SourceJournal(),*metadata_,source,descriptor,&call,resources);
            if(!owner_->Healthy())return false;
            {
                Feed::ObservationScope registration(*owner_);Feed::Callback callback(descriptor,&call);
                if(!callback.Active())return false;
                Feed::ObserveNgxEvaluation(callback,&call,0);
            }
            if(!owner_->Healthy()||owner_->Candidates().size()!=ObservationCount||owner_->Receipts().size()!=ObservationCount)return false;
            observations_=owner_->Observations();complete_=observations_&&observations_->candidates.count==ObservationCount;
            if(complete_)sourceFrame_=call.SourceFrame();
            return complete_;
        }
        catch(...){return false;}
    }
    bool PrepareContext(NativeOwnerSet& owners,const Context::SemanticProfile& policy)noexcept
    {
        if(contextAttempted_||!complete_||owners_!=&owners)return false;
        contextAttempted_=true;
        try
        {
            contextPolicy_=policy; // retained immutable consumer policy meaning
            const auto* render=renderView_?Context::ResolveMetadata(*renderView_,*metadata_):nullptr;
            const auto* output=outputView_?Context::ResolveMetadata(*outputView_,*metadata_):nullptr;
            if(!render||!output||!Context::NativeNgxAdapterProfile(policy)||!Context::ValidProfile(policy))
            {contextResult_.reason.Assign("CTX.Native.DeclarationsUnavailable");return true;}
            auto context=metadata_->ForOwner(C::OwnerDomain::Context);
            const auto& source=owner_->Source();Context::ContextRequest request;
            request.header=owners.ContextJournal().Header(C::ContractId::C02,source.enrolledScope.value_or(C::ScopeRef{source.callback}));
            request.frame=source.seed.frame;request.generations=source.generations;request.boundary=source.seed.boundary;
            request.renderRaster=context.Publish(render->raster);request.outputRaster=context.Publish(output->raster);
            request.continuity=context.Publish(C::EvidenceVector{}); // no invented continuity/reset acknowledgement
            contextRequest_=request;
            const auto certificate=owners.ContextJournal().Header(C::ContractId::C04,request.header.scope);
            contextResult_=Context::BuildNativeContext(*owner_,request,*contextPolicy_,certificate,context);
            return true;
        }
        catch(...){contextResult_.context.reset();contextResult_.certificate.reset();
            contextResult_.reason.Assign("CTX.Native.PreparationFailure");return true;}
    }
  public:
    NativeSourcePublication(const NativeSourcePublication&)=delete;
    NativeSourcePublication& operator=(const NativeSourcePublication&)=delete;
    bool Complete()const noexcept{return complete_;}
    std::optional<C::ColorDescription> DeclaredColor(bool output)const
    {return complete_?declaredColors_[output?1:0]:std::nullopt;}
    const std::shared_ptr<const NativeSourceTransactionObservation>& TransactionObservation()const noexcept{return transaction_;}
    const std::optional<C::MetadataRef<C::InterceptedSourceTransactionV1>>& TransactionSubject()const noexcept{return transactionSubject_;}
    // Historical call nesting retained with this exact Native sample. The
    // enclosing SL outcome can still be Pending here; even Succeeded supplies
    // neither NewGameContent nor canonical/FSR frame identity or use authority.
    const std::shared_ptr<const DlssNr::StreamlineSourceScope::Observation>& SourceFrame()const noexcept
    {return sourceFrame_;}
    // Same-callback input provenance, separate from physical content version.
    bool DescribesInput(const C::NativeSampleIdentityV1& sample,std::string_view field,
        const C::ResourceView& current)const noexcept
    try
    {
        if(!complete_||!Source()||Source()->callback!=sample.callback)return false;
        const auto* frame=Context::ResolveMetadata(Source()->seed.frame,*this);
        const auto* published=frame?Context::ResolveNativeSample(*frame,*this):nullptr;
        if(!published||*published!=sample||!Context::CompleteResourceStructure(current))return false;
        for(const auto& candidate:Candidates())
        {
            const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
            if(!Context::ResolveList(candidate.claims,*this,claims)||!claims||!claims->Size()||
               claims->Get(0)->field.View()!=field||!Context::Established(candidate.payload))continue;
            const auto* ref=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value);
            const auto* view=ref?Context::ResolveMetadata(*ref,*this):nullptr;
            const auto* evidence=Context::ResolveMetadata(candidate.evidence,*this);
            if(view&&ref->owner==C::OwnerDomain::Resource&&Context::CompleteResourceStructure(*view)&&
               Context::SameFact(view->identity.objectIncarnation,current.identity.objectIncarnation)&&
               Context::SameFact(view->identity.resourceIncarnation,current.identity.resourceIncarnation)&&
               Context::SameFact(view->identity.resourceGeneration,current.identity.resourceGeneration)&&
               view->identity.contentRevision==current.identity.contentRevision&&
               Context::SemanticEqual(view->descriptor,current.descriptor)&&
               Context::SameFact(view->device,current.device)&&Context::SameFact(view->mip,current.mip)&&
               Context::SameFact(view->arrayLayer,current.arrayLayer)&&Context::SameFact(view->plane,current.plane)&&
               evidence&&Context::NativeEvidenceFresh(*evidence,*frame,*this))return true;
        }
        return false;
    }
    catch(...){return false;}
    // Nonexecuting, historical C02/C04. Eligibility does not grant a model,
    // current-route admission, C03, content provenance or final-frame truth.
    const Context::NativeContextResult* ContextPreparation()const noexcept{return contextAttempted_?&contextResult_:nullptr;}
    const Context::SemanticProfile* ContextPolicy()const noexcept{return contextPolicy_?&*contextPolicy_:nullptr;}
    const Context::ContextRequest* PreparedContextRequest()const noexcept{return contextRequest_?&*contextRequest_:nullptr;}
    const NativeRepresentationPreparation* RepresentationPreparation()const noexcept{return representationsAttempted_?&representationResult_:nullptr;}
    const Feed::SourceContext* Source()const noexcept{return owner_?&owner_->Source():nullptr;}
    const C::ObservationSet* Observations()const noexcept{return observations_?&*observations_:nullptr;}
    std::span<const C::AcquisitionCandidate> Candidates()const noexcept{return owner_?owner_->Candidates():std::span<const C::AcquisitionCandidate>{};}
    std::span<const C::OwnerReceipt> Receipts()const noexcept{return owner_?owner_->Receipts():std::span<const C::OwnerReceipt>{};}
    const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& reference)const
    {
        if(reference.recordType.View()!=C::AcquisitionCandidate::WireName)return nullptr;
        for(const auto& candidate:Candidates())if(candidate.header.record==reference.record&&candidate.header.revision==reference.revision)return &candidate;
        return nullptr;
    }
    template<class T>Context::MetadataView<T> Resolve(const C::MetadataRef<T>& reference)const{return metadata_->Resolve(reference);}
  private:
    bool PrepareRepresentations(NativeOwnerSet& owners,const DlssNr::NativeNgxCallCapture& call,
        C::Placement placement,const Protocol::NativeEncodeControls& controls)noexcept
    {
        if(representationsAttempted_||!complete_||owners_!=&owners||!contextAttempted_)return false;
        representationsAttempted_=true;
        try
        {
            if(!contextResult_.context||!contextResult_.certificate||!contextPolicy_)
            {representationResult_.reason.Assign("CTX.Native.RepresentationContextMissing");return true;}
            representationKeys_=std::make_unique<NativeContextKeyInterner>(owners.ContextJournal());
            RepresentationStore store{*this,metadata_->ForOwner(C::OwnerDomain::Context),*representationKeys_};
            representationResult_=PrepareCapturedNativeRepresentations(*contextResult_.context,*contextResult_.certificate,*contextPolicy_,
                Candidates(),call,placement,controls,owners.ContextJournal(),store);return true;
        }
        catch(...){representationResult_.bundle.reset();representationResult_.reason.Assign("CTX.Native.RepresentationFailure");return true;}
    }
};
}
