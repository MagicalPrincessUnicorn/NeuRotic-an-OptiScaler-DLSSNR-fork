#include "../OptiScaler/nr/diagnostics/capability/ObservationRefreshReport.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityStore.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityExport.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityWireValidation.h"
#include <cassert>
#include <iostream>
using namespace DlssNr::Capability;
int main() {
    UsableInputEvidence owner{};
    assert(!NormalizeUseEvidence(owner).Any());
    owner.contextMatched=true;owner.requestedEnabled=true;owner.ownerRunning=true;
    owner.presentGuided=true;owner.resources=1;owner.completedBefore=10;owner.completedAfter=11;
    auto valid=NormalizeUseEvidence(owner);assert(!valid.Any()); // Submitted Present work alone proves no per-signal consumption.
    using Signal=DlssNr::SignalReceipts::Signal;using ReceiptState=DlssNr::SignalReceipts::State;
    auto complete=[&](Signal signal){auto& r=owner.signals[size_t(signal)];r.state=ReceiptState::Consumed;r.sequence=11;r.fact.present=true;r.fact.admitted=true;r.epoch.route=2;};
    complete(Signal::Depth);complete(Signal::Motion);complete(Signal::Jitter);
    valid=NormalizeUseEvidence(owner);assert(valid.depth&&valid.motion&&valid.jitter&&!valid.preExposure);
    owner.signals[size_t(Signal::Jitter)].state=ReceiptState::Pending;
    assert(!NormalizeUseEvidence(owner).jitter);complete(Signal::Jitter);
    owner.signals[size_t(Signal::Jitter)].age=2001;assert(!NormalizeUseEvidence(owner).jitter);
    owner.signals[size_t(Signal::Jitter)].age=0;
    owner.resetPending=true;assert(NormalizeUseEvidence(owner).jitter);owner.resetPending=false; // Reset does not cancel input consumption.
    owner.completedAfter=10;assert(!NormalizeUseEvidence(owner).Any());
    owner.nativeRoute=true;owner.lifecycle=1;owner.lifecycleOpen=true;owner.completedAfter=11;
    owner.parametersBefore=20;owner.parametersAfter=21;owner.finiteJitter=true;owner.finitePreExposure=true;
    owner.signals={};complete(Signal::Jitter);complete(Signal::PreExposure);
    valid=NormalizeUseEvidence(owner);assert(valid.jitter && valid.preExposure && !valid.depth);
    owner.contextMatched=false;assert(!NormalizeUseEvidence(owner).Any());
    Store store("report-test");auto bundle=std::make_shared<ObservationReportBundle>();
    bundle->observation.capture=store.Capture({});bundle->operation=1;bundle->context=2;
    bundle->started=100;bundle->completed=2100;bundle->manual=true;bundle->hasDiscovery=true;
    bundle->discovery.count=1;bundle->discovery.candidates[0].depthSupported=true;
    bundle->discovery.candidates[0].token=7;bundle->discovery.candidates[0].mayAdmit=false;
    bundle->nativeScopes=21;bundle->nativeDepthCopies=7;bundle->nativeEstimatedAssociations=4;
    assert(PrepareObservationReport(*bundle));
    auto document=Wire::Json::parse(bundle->utf8);
    assert(document.contains("selected_renderer_signals"));
    bundle->signals=owner.signals;bundle->signalOwnerSupported=true;bundle->signalRoute=2;
    auto& motionReceipt=bundle->signals[size_t(Signal::Motion)];motionReceipt.fact.valuesKnown=true;
    motionReceipt.fact.x=2256;motionReceipt.fact.y=1264;motionReceipt.epoch.route=2;
    assert(PrepareObservationReport(*bundle));document=Wire::Json::parse(bundle->utf8);
    assert(document["selected_renderer_signals"]["signals"][3]["x"]==2256);
    assert(document["selected_renderer_signals"]["signals"][3]["y"]==1264);
    assert(document["schema"]=="nr-observation-refresh-1");
    assert(document["discovery"]["may_admit"]==false);
    assert(document.contains("prepared_external_inputs"));
    assert(document.contains("built_in_vulkan_inputs"));
    assert(document["built_in_vulkan_inputs"]["render_scopes"]=="21");
    assert(document["built_in_vulkan_inputs"]["depth_copies_recorded"]=="7");
    assert(document["built_in_vulkan_inputs"]["estimated_scene_associations"]=="4");
    assert(document["built_in_vulkan_inputs"]["engine_motion"]==false);
    assert(document["built_in_vulkan_inputs"]["display_observed"]==false);
    assert(document["prepared_external_inputs"]["available_at_capture"]==false);
    DlssNr::PreparedGuides::Status prepared;
    prepared.session=9;prepared.capture=8;prepared.updatedTickMs=2100;prepared.active=1;
    prepared.depthReady=1;prepared.motionReady=1;prepared.stage=DlssNr::PreparedGuides::Stage::Delivered;
    prepared.inputFrames=17;prepared.modelCompletions=13;prepared.copybackCompletions=11;
    bundle->preparedExternal=DlssNr::PreparedGuides::ValidateStatus(prepared,2100);
    assert(PrepareObservationReport(*bundle));
    document=Wire::Json::parse(bundle->utf8);
    const auto& external=document["prepared_external_inputs"];
    assert(external["captured_depth_ready"]==true && external["estimated_motion_ready"]==true);
    assert(external["native_ownership"]==false && external["display_observed"]==false);
    assert(external["model_completions"]=="13" && external["copyback_completions"]=="11");
    assert(document["owner_validated_inputs"]["depth"]==false);
    const auto savedExternal=external;
    prepared.copybackCompletions=12; // Producer changes cannot rewrite captured report bytes.
    assert(PrepareObservationReport(*bundle));
    assert(Wire::Json::parse(bundle->utf8)["prepared_external_inputs"]==savedExternal);
    auto nested=document["capability_report"].dump();
    auto historical=DecodeHistorical({reinterpret_cast<const std::byte*>(nested.data()),nested.size()});
    assert(historical.state==ImportState::Historical);
    const auto original=bundle->utf8;
    bundle->discovery.candidates[0].mayAdmit=true;assert(!PrepareObservationReport(*bundle));
    assert(bundle->utf8==original); // refused build never rewrites a complete export
    std::cout<<"owner-positive/negative and nested capability report checks passed\n";
}
