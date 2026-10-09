#pragma once
#include "NativeOwnerSet.h"
#include <nr/contracts/InterceptedSourceTransaction.h>
#include <nr/orchestration/StreamCoordinatorKernel.h>
#include <dlssnr/NativeFeatureRegistry.h>

namespace Neurotic::Lifecycle
{
// The process bootstrap creates one enrollment from its actual source pin and
// retains it with the session. This is source identity, not route admission,
// resource rights, model-attempt authority, or a final-frame association.
template<class Feature>class NativeSourceEnrollment
{
    friend class NativeProcessBootstrap;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeSourceTestAccess;
#endif
    using Registry=DlssNr::NativeFeatureRegistry<Feature>;
    using Pin=typename Registry::CallbackPin;
    NativeOwnerSet& owners_;
    Registry& source_;
    const unsigned int handle_;
    const typename Registry::Snapshot feature_;
    Orchestration::NativeScopeEnrollment scope_;
    C::NativeSampleIdentityV1 sample_;
    C::OptionalFact<C::ProviderIncarnation> incarnation_;
    C::EpisodeId episode_;
    std::shared_ptr<const C::InterceptedSourceTransactionV1> transaction_;
    bool closed_=false;
    NativeSourceEnrollment(NativeOwnerSet& owners,Registry& source,const Pin& pin):
        owners_(owners),source_(source),handle_(pin.Handle()),feature_(pin.Value())
    {
        if(!owners.Ready()||!pin.BelongsTo(source)||!pin.Current()||!pin.Ordinal())
            throw std::invalid_argument("Native source enrollment");
        const auto stream=owners.Topology()->Stream(owners.IdentityJournal().Event());
        const auto view=owners.Topology()->View(owners.IdentityJournal().Event());
        const auto feature=owners.Objects()->Incarnation(owners.IdentityJournal().Event());
        const auto ingress=owners.Objects()->Incarnation(owners.IdentityJournal().Event());
        const auto provider=owners.Provider()->Incarnation(owners.SourceJournal().Event());
        if(stream.status!=IdentityStatus::Ok||view.status!=IdentityStatus::Ok||
           feature.status!=IdentityStatus::Ok||ingress.status!=IdentityStatus::Ok||provider.status!=IdentityStatus::Ok)
            throw std::runtime_error("Native source identity unavailable");
        scope_={*owners.Session(),stream.value.KnownPart()->value,view.value.KnownPart()->value,
            {owners.SourceJournal().Event().evidence.record}};
        sample_.session=scope_.session;sample_.stream=scope_.stream;sample_.view=scope_.view;
        sample_.feature=feature.value.KnownPart()->value;sample_.ingress=ingress.value.KnownPart()->value;
        incarnation_=provider.value;
        const auto episode=owners.Episode()->Episode(owners.IdentityJournal().Event());
        if(episode.status!=IdentityStatus::Ok)throw std::runtime_error("Native source episode unavailable");
        episode_=episode.value.KnownPart()->value;
    }
  public:
    NativeSourceEnrollment(const NativeSourceEnrollment&)=delete;
    NativeSourceEnrollment& operator=(const NativeSourceEnrollment&)=delete;
    const Orchestration::NativeScopeEnrollment& Enrollment()const noexcept{return scope_;}
    const C::OptionalFact<C::ProviderIncarnation>& Incarnation()const noexcept{return incarnation_;}
    // Caller holds the process/source enrollment lock. One outer pin is carried
    // unchanged through pre/SR/post; repeated observations cannot rekey it.
    std::optional<C::NativeSampleIdentityV1> Observe(const Pin& pin)
    {
        if(closed_||!pin.BelongsTo(source_)||!pin.Current()||pin.Handle()!=handle_||
           pin.Value().generation!=feature_.generation||pin.Value().feature!=feature_.feature||
           !pin.Ordinal()||pin.Ordinal()<sample_.producerOrdinal)return {};
        if(pin.Ordinal()==sample_.producerOrdinal)return sample_;
        const auto callback=owners_.SourceJournal().Event().evidence.record;
        const auto issued=owners_.Render()->SourceTransaction(owners_.IdentityJournal().Event());
        if(issued.status!=IdentityStatus::Ok){closed_=true;return {};}
        auto next=sample_;next.producerOrdinal=pin.Ordinal();next.callback=callback;
        // Prepare before updating the enrolled sample. Issuer values never roll
        // back, even if allocation fails; no usable observation escapes then.
        auto record=std::make_shared<C::InterceptedSourceTransactionV1>();
        record->id=issued.value.KnownPart()->value;record->sample=next;record->episode=episode_;
        sample_=next;transaction_=std::move(record);
        return sample_;
    }
    const std::shared_ptr<const C::InterceptedSourceTransactionV1>& Transaction()const noexcept{return transaction_;}
    void Close()noexcept{closed_=true;}
};
}
