#include "pch.h"
#include "CapabilityStatusCollector.h"
#include "CapabilityConsumerView.h"
#include "CapabilityOwnerAdapters.h"
#include "CapabilityNgxObservation.h"
#include "CapabilityRefresh.h"
#include "../../../Config.h"
#include "../../../dlssnr/DlssNrFeature_Dx12.h"
#include "../../../dlssnr/DlssNrFeature_Vk.h"
#include "../../../dlssnr/DlssNr_Present.h"
#include "../../../dlssnr/HdrObservation.h"
#include <charconv>
#include <random>
namespace DlssNr::Capability {
namespace {
std::atomic<const WriterPort*> hdrPort{nullptr};
std::atomic<const WriterPort*> presentChainPort{nullptr};
struct HdrRefreshOwner {
    std::mutex mutex;
    HdrChangeTracker tracker;
    std::atomic<uint64_t> revision{0};
};
std::atomic<HdrRefreshOwner*> hdrRefreshOwner{nullptr};
std::string NewSession() {
    std::random_device random; std::string session="obs-"; constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<4;++i) { auto value=random(); for(unsigned j=0;j<8;++j) session+=hex[(value>>(j*4))&15]; } return session;
}
void ObserveHdr(const DlssNr::HdrObservation::Snapshot& s) noexcept {
    auto port=hdrPort.load(); if(!port) return; auto seq=ReserveSample(*port); if(seq.state!=SequenceState::Reserved) return;
    if(auto refreshOwner=hdrRefreshOwner.load()) {
        std::lock_guard lock(refreshOwner->mutex);
        const RefreshHdrState state{s.registered,s.colorSpaceObserved,s.transitioning,
            static_cast<uint32_t>(s.colorSpace),static_cast<uint32_t>(s.format),
            static_cast<uint32_t>(s.metadataType),s.metadataSize,s.identityGeneration,s.resizeGeneration,s.metadataHash,
            static_cast<int64_t>(s.colorSpaceResult),static_cast<int64_t>(s.metadataResult)};
        refreshOwner->revision.store(refreshOwner->tracker.Observe(state));
    }
    HdrObservation o; o.sequence=seq.sequence; o.identity=s.identityGeneration; o.descriptorGeneration=s.identityGeneration; o.metadataGeneration=s.metadataGeneration;
    o.registered=s.registered; o.colorObserved=s.colorSpaceObserved; o.transitioning=s.transitioning;
    o.colorClass.Assign(DlssNr::HdrObservation::ColorClassName(DlssNr::HdrObservation::Classify(s.colorSpace)));
    o.metadata={static_cast<uint64_t>(s.metadataType),s.metadataSize,s.metadataHash,static_cast<uint64_t>(static_cast<uint32_t>(s.metadataResult)),static_cast<uint64_t>(s.requestedMetadataType),s.requestedMetadataSize,s.requestedMetadataHash};
    auto image=AdaptHdr(*port,o); (void)TryStageEnvelope(*port,seq.sequence,image);
}
struct Service {
    Store store{NewSession()}; AdapterFactory factory{store};
    HdrRefreshOwner hdrRefresh;
    std::optional<WriterPort> config=factory.RegisterWriter(ProducerId::Config,"nr").port;
    std::optional<WriterPort> native=factory.RegisterWriter(ProducerId::Native,"native").port;
    std::optional<WriterPort> lifecycle=factory.RegisterWriter(ProducerId::Lifecycle,"native").port;
    std::optional<WriterPort> present=factory.RegisterWriter(ProducerId::Present,"present").port;
    std::optional<WriterPort> presentChain=factory.RegisterWriter(ProducerId::Present,"present_chain").port;
    std::optional<WriterPort> provider=factory.RegisterWriter(ProducerId::Provider,"fg").port;
    std::optional<WriterPort> hdr=factory.RegisterWriter(ProducerId::Hdr,"hdr").port;
    Service() { if(hdr) { hdrRefreshOwner.store(&hdrRefresh); hdrPort.store(&*hdr); DlssNr::HdrObservation::SetCapabilityObserver(ObserveHdr); } if(presentChain) presentChainPort.store(&*presentChain); InitializePassiveObservers(factory); }
};
Service& MetadataService() { static Service* service=new Service; return *service; }
}
uint64_t HdrChangeRevision() noexcept { auto owner=hdrRefreshOwner.load(); return owner?owner->revision.load():0; }
void ObservePresentChain(const PresentChainObservation& input) noexcept {
    auto port=presentChainPort.load(); if(!port) return; auto sequence=ReserveSample(*port); if(sequence.state!=SequenceState::Reserved) return;
    auto envelope=input; envelope.sequence=sequence.sequence; auto image=AdaptPresentChain(*port,envelope);
    (void)TryStageEnvelope(*port,sequence.sequence,image);
}
CollectionResult CollectStatus(std::optional<bool> vulkan) noexcept {
    CollectionResult result;
    try {
        auto& service=MetadataService();
        if(service.config) {
            ConfigObservation o;
            { NrConfigSynchronization::Transaction transaction;
                const auto settings=Config::Instance()->GetDlssNrConfigSnapshot();
                o.sequence=ReserveSample(*service.config).sequence; o.revision=settings.ObservationRevision();
                o.enabled=settings.GetDlssNrRuntimeSnapshot().enabled; o.route=settings.DlssNrRoute.value_or_default();
                result.configRevision=o.revision;
            }
            result.publications.push_back(TryPublish(*service.config,o.sequence,AdaptConfig(*service.config,o)));
            // Fresh independent configuration owner context, not the stored scope.
            { NrConfigSynchronization::Transaction transaction;
                const auto revision=NrConfigSynchronization::ObservationRevision();
                result.configurationContext={{Dimension::Process,StampStatus::Known,"diagnostics",std::string(service.config->Session()),0},
                    {Dimension::Configuration,revision?StampStatus::Known:StampStatus::Unknown,revision?"config":"",revision?"nr":"",revision.value_or(0)}};
            }
        }
        if(service.native) {
            const bool useVulkan=vulkan.has_value()?*vulkan:(State::Instance().currentInputApiName==ApiUpscalerInput::DLSS_VK ||
                    State::Instance().currentInputApiName==ApiUpscalerInput::XeSS_VK ||
                    State::Instance().currentInputApiName==ApiUpscalerInput::FFX_VK ||
                    State::Instance().currentInputApiName==ApiUpscalerInput::FSR2X_VK);
            auto o=useVulkan
                ? DlssNr::CopyCapabilityObservationVk(*service.native,service.lifecycle?&*service.lifecycle:nullptr)
                : DlssNr::CopyCapabilityObservation(*service.native,service.lifecycle?&*service.lifecycle:nullptr);
            if(!o.sequence) { result.capture={}; return result; }
            result.publications.push_back(TryPublish(*service.native,o.sequence,AdaptNative(*service.native,o)));
            if(service.lifecycle) { o.sequence=o.lifecycleSequence; result.publications.push_back(TryPublish(*service.lifecycle,o.sequence,AdaptLifecycle(*service.lifecycle,o))); }
        }
        if(service.present) { auto o=DlssNr::CopyPresentCapabilityObservation(*service.present);
            if(!o.sequence) { result.capture={}; return result; }
            result.publications.push_back(TryPublish(*service.present,o.sequence,AdaptPresent(*service.present,o))); }
        if(service.provider) { auto o=DlssNr::PreFg::CopyCapabilityObservation(*service.provider); result.publications.push_back(TryPublish(*service.provider,o.sequence,AdaptProvider(*service.provider,o))); }
        auto drained=service.factory.DrainStagedObservations({}); result.publications.insert(result.publications.end(),drained.begin(),drained.end());
        result.capture=service.store.Capture({}); return result;
    } catch(...) {
        result.capture={};
        // Keep the failure result usable under sustained allocator refusal.
        try { result.capture.reason="CAP_COLLECTION_UNAVAILABLE"; } catch(...) {}
        return result;
    }
}
ConsumerView CapabilityView() noexcept {
    return ConsumerView(+[](const Selection& selection) noexcept -> CaptureResult {
        try { return MetadataService().store.Capture(selection); }
        catch(...) { return {}; }
    });
}
}

