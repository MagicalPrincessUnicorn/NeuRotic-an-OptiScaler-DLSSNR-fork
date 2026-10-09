#pragma once
#include "FinalRealFrameFinalizer.h"

namespace Neurotic::Lifecycle
{
// Process/session-owned composition root. Exactly one canonical identity host is
// constructed here; neither Native execution nor Finalizer creates another.
// Startup/destruction and registration are serialized by the Process owner.
// uniqueNamespace is established at process bootstrap, never from a pointer,
// frame counter, provider token or a diagnostic record.
class SessionComposition
{
    HostIdentityDomain identity_;
    std::optional<C::SessionId> session_;
    std::unique_ptr<FinalRealFrameFinalizer> finalizer_;
    bool registering_=true;
  public:
    SessionComposition(const C::Symbol& uniqueNamespace,const OwnerEvent& start,
        const OwnerBinding& finalizerOwner,std::size_t finalizerCapacity):identity_(uniqueNamespace)
    {
        if(!finalizerCapacity)return;
        const auto opened=identity_.OpenSession(start);
        if(opened.status!=IdentityStatus::Ok||!opened.value.IsKnown())return;
        const auto session=opened.value.KnownPart()->value;
        const auto issuer=identity_.BindFinalizer(finalizerOwner);
        if(!issuer){identity_.CloseSession(session);return;}
        try{finalizer_=std::make_unique<FinalRealFrameFinalizer>(session,finalizerOwner,*issuer,finalizerCapacity);session_=session;}
        catch(...){identity_.CloseSession(session);throw;}
    }
    SessionComposition(const SessionComposition&)=delete;
    SessionComposition& operator=(const SessionComposition&)=delete;
    bool Ready()const noexcept{return session_.has_value()&&finalizer_!=nullptr;}
    const std::optional<C::SessionId>& Session()const noexcept{return session_;}
    FinalRealFrameFinalizer* Finalizer()noexcept{return Ready()?finalizer_.get():nullptr;}
    std::optional<TopologyIssuer> RegisterTopology(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindTopology(owner):std::nullopt;}
    std::optional<RenderIssuer> RegisterRender(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindRender(owner):std::nullopt;}
    std::optional<EpisodeIssuer> RegisterEpisode(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindEpisode(owner):std::nullopt;}
    std::optional<ObjectIssuer> RegisterObject(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindObject(owner):std::nullopt;}
    std::optional<EvaluationIssuer> RegisterEvaluation(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindEvaluation(owner):std::nullopt;}
    std::optional<ResourceIssuer> RegisterResource(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindResource(owner):std::nullopt;}
    std::optional<ProviderIssuer> RegisterProvider(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindProvider(owner):std::nullopt;}
    std::optional<RepresentationIssuer> RegisterRepresentation(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindRepresentation(owner):std::nullopt;}
    std::optional<HistoryIssuer> RegisterHistory(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindHistory(owner):std::nullopt;}
    std::optional<RasterIssuer> RegisterRaster(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindRaster(owner):std::nullopt;}
    std::optional<RouteIssuer> RegisterRoute(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindRoute(owner):std::nullopt;}
    std::optional<ModelIssuer> RegisterModel(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindModel(owner):std::nullopt;}
    std::optional<HandoffIssuer> RegisterHandoff(const OwnerBinding& owner)
    {return registering_&&Ready()?identity_.BindHandoff(owner):std::nullopt;}
    void FinishRegistration()noexcept{registering_=false;}
    bool CloseDrained()
    {
        registering_=false;if(!Ready())return false;
        if(!finalizer_->StopAdmissions())return false;
        const auto drained=finalizer_->DrainStatus();
        if(!drained||!drained->safelyDrained)return false;
        if(identity_.CloseSession(*session_)!=IdentityStatus::Ok)return false;
        session_.reset();return true;
    }
    // Destruction closes only value bookkeeping. The Process owner must retain
    // the composition root until CloseDrained succeeds or a real device/session
    // teardown proves all external owners gone. No destructor frees GPU work.
};
}
