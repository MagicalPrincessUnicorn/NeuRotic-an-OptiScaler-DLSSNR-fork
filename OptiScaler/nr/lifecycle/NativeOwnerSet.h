#pragma once
#include "SessionComposition.h"
#include "OwnerPublicationJournal.h"
#include "OwnerMetadataArena.h"
#include "NativeResourceRegistry.h"

namespace Neurotic::Orchestration { class StreamCoordinatorKernel; }
namespace Neurotic::Lifecycle
{
// Cold process-owned composition. The actual bootstrap supplies an OS-unique
// namespace. All registration completes here, never once per frame. The same
// set is retained through finalizer, provider, metadata and GPU drain.
class NativeOwnerSet
{
    friend class Orchestration::StreamCoordinatorKernel;
  public:
    static constexpr std::size_t AllocationSlots=NativeResourceRegistry::Capacity;
    using AllocationOwner=NativeResourceRegistry::AllocationOwner;
  private:
    OwnerPublicationJournal sessionJournal_,identityJournal_,finalizerJournal_,historyJournal_,strategyJournal_,contextJournal_,sourceJournal_;
    OwnerPublicationJournal routeJournal_,protocolJournal_,configurationJournal_,policyJournal_,runtimeJournal_,fgJournal_,finalConsumerIdentityJournal_;
    std::unique_ptr<SessionComposition> composition_;
    NativeResourceRegistry resources_;
    std::optional<HistoryIssuer> history_;
    std::optional<TopologyIssuer> topology_;
    std::optional<RenderIssuer> render_;
    std::optional<EpisodeIssuer> episode_;
    std::optional<ObjectIssuer> objects_;
    std::optional<EvaluationIssuer> evaluation_;
    std::optional<ProviderIssuer> provider_;
    std::optional<ProviderIssuer> runtime_;
    std::optional<RouteIssuer> route_;
    std::optional<ModelIssuer> model_;
    std::optional<HandoffIssuer> handoff_;
    std::optional<RasterIssuer> raster_;
    std::optional<RepresentationIssuer> contextRepresentation_;
    bool ready_=false;
  public:
    explicit NativeOwnerSet(const C::Symbol& uniqueNamespace):
        sessionJournal_(uniqueNamespace,C::OwnerDomain::Session,1),
        identityJournal_(uniqueNamespace,C::OwnerDomain::IdentityRegistry,2),
        finalizerJournal_(uniqueNamespace,C::OwnerDomain::Finalizer,3),
        historyJournal_(uniqueNamespace,C::OwnerDomain::History,4),
        strategyJournal_(uniqueNamespace,C::OwnerDomain::Strategy,5),
        contextJournal_(uniqueNamespace,C::OwnerDomain::Context,6),
        sourceJournal_(uniqueNamespace,C::OwnerDomain::Provider,7),
        routeJournal_(uniqueNamespace,C::OwnerDomain::StreamCoordinator,8),
        protocolJournal_(uniqueNamespace,C::OwnerDomain::RenderingProtocol,9),
        configurationJournal_(uniqueNamespace,C::OwnerDomain::Configuration,10),
        policyJournal_(uniqueNamespace,C::OwnerDomain::OrchestratorPolicy,11),
        runtimeJournal_(uniqueNamespace,C::OwnerDomain::Provider,13),
        fgJournal_(uniqueNamespace,C::OwnerDomain::FrameGeneration,14),
        finalConsumerIdentityJournal_(uniqueNamespace,C::OwnerDomain::IdentityRegistry,15)
    {
        composition_=std::make_unique<SessionComposition>(uniqueNamespace,sessionJournal_.Event(),finalizerJournal_.Binding(),64);
        if(!composition_->Ready())return;
        history_=composition_->RegisterHistory(historyJournal_.Binding());if(!history_)return;
        topology_=composition_->RegisterTopology(identityJournal_.Binding());
        render_=composition_->RegisterRender(identityJournal_.Binding());
        episode_=composition_->RegisterEpisode(identityJournal_.Binding());
        objects_=composition_->RegisterObject(identityJournal_.Binding());
        evaluation_=composition_->RegisterEvaluation(strategyJournal_.Binding());
        provider_=composition_->RegisterProvider(sourceJournal_.Binding());
        runtime_=composition_->RegisterProvider(runtimeJournal_.Binding());
        route_=composition_->RegisterRoute(routeJournal_.Binding());
        model_=composition_->RegisterModel(strategyJournal_.Binding());
        handoff_=composition_->RegisterHandoff(finalizerJournal_.Binding());
        raster_=composition_->RegisterRaster(contextJournal_.Binding());
        contextRepresentation_=composition_->RegisterRepresentation(contextJournal_.Binding());
        if(!topology_||!render_||!episode_||!objects_||!evaluation_||!provider_||!runtime_||!route_||!model_||!handoff_||!raster_||!contextRepresentation_)return;
        resources_.deviceJournal_=OwnerPublicationJournal(uniqueNamespace,C::OwnerDomain::IdentityRegistry,12);
        resources_.deviceIssuer_=composition_->RegisterObject(resources_.deviceJournal_.Binding());
        if(!resources_.deviceIssuer_)return;
        for(std::size_t i=0;i<AllocationSlots;++i)
        {
            auto& slot=resources_.slots_[i].owner;slot.journal=OwnerPublicationJournal(uniqueNamespace,C::OwnerDomain::Resource,100+i);
            slot.resource=composition_->RegisterResource(slot.journal.Binding());
            slot.representation=composition_->RegisterRepresentation(slot.journal.Binding());
            if(!slot.resource||!slot.representation)return;
        }
        composition_->FinishRegistration();resources_.active_=true;ready_=true;
    }
    NativeOwnerSet(const NativeOwnerSet&)=delete;
    NativeOwnerSet& operator=(const NativeOwnerSet&)=delete;
    bool Ready()const noexcept{return ready_&&composition_&&composition_->Ready();}
    const std::optional<C::SessionId>& Session()const noexcept{return composition_->Session();}
    FinalRealFrameFinalizer* Finalizer()noexcept{return Ready()?composition_->Finalizer():nullptr;}
    const std::optional<HistoryIssuer>& History()const noexcept{return history_;}
    const std::optional<TopologyIssuer>& Topology()const noexcept{return topology_;}
    const std::optional<RenderIssuer>& Render()const noexcept{return render_;}
    const std::optional<EpisodeIssuer>& Episode()const noexcept{return episode_;}
    const std::optional<ObjectIssuer>& Objects()const noexcept{return objects_;}
    const std::optional<EvaluationIssuer>& Evaluation()const noexcept{return evaluation_;}
    const std::optional<ProviderIssuer>& Provider()const noexcept{return provider_;}
    const std::optional<ProviderIssuer>& RuntimeProvider()const noexcept{return runtime_;}
    const std::optional<ModelIssuer>& Model()const noexcept{return model_;}
    const std::optional<HandoffIssuer>& Handoff()const noexcept{return handoff_;}
    const std::optional<RasterIssuer>& Raster()const noexcept{return raster_;}
    const std::optional<RepresentationIssuer>& ContextRepresentation()const noexcept{return contextRepresentation_;}
    OwnerPublicationJournal& HistoryJournal()noexcept{return historyJournal_;}
    OwnerPublicationJournal& StrategyJournal()noexcept{return strategyJournal_;}
    OwnerPublicationJournal& ContextJournal()noexcept{return contextJournal_;}
    OwnerPublicationJournal& SourceJournal()noexcept{return sourceJournal_;}
    OwnerPublicationJournal& RuntimeJournal()noexcept{return runtimeJournal_;}
    OwnerPublicationJournal& IdentityJournal()noexcept{return identityJournal_;}
    OwnerPublicationJournal& ProtocolJournal()noexcept{return protocolJournal_;}
    OwnerPublicationJournal& ConfigurationJournal()noexcept{return configurationJournal_;}
    OwnerPublicationJournal& PolicyJournal()noexcept{return policyJournal_;}
    OwnerPublicationJournal& FinalizerJournal()noexcept{return finalizerJournal_;}
    OwnerPublicationJournal& FrameGenerationJournal()noexcept{return fgJournal_;}
    // Serialized by the existing kernel's finalizerEvents lock; it cannot race
    // the source IdentityJournal used for ordinary topology/render admissions.
    OwnerPublicationJournal& FinalConsumerIdentityJournal()noexcept{return finalConsumerIdentityJournal_;}
    NativeResourceRegistry& Resources()noexcept{return resources_;}
#ifdef NR_NATIVE_OWNER_TESTING
    // Isolated issuer tests only; production resource adapters share Resources().
    AllocationOwner* Allocation(std::size_t index)noexcept{return Ready()&&index<AllocationSlots?&resources_.slots_[index].owner:nullptr;}
#endif
    std::unique_ptr<OwnerMetadataArena> NewInvocationMetadata(std::size_t bytes,std::size_t entries)
    {
        if(!Ready())throw MetadataRefusal{};
        // Called by the cold bounded invocation-pool owner before admissions.
        return std::make_unique<OwnerMetadataArena>(sessionJournal_.Event().evidence.record,bytes,entries);
    }
  private:
    const std::optional<RouteIssuer>& Route()const noexcept{return route_;}
    OwnerPublicationJournal& RouteJournal()noexcept{return routeJournal_;}
    bool CloseDrained()
    {if(!Ready()||!composition_->CloseDrained())return false;resources_.CloseAdmissions();ready_=false;return true;}
};
}
