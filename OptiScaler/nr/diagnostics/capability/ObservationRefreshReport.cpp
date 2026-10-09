#include "ObservationRefreshReport.h"
#include "CapabilityExport.h"
#include "CapabilityWireValidation.h"
#include "../../../dlssnr/RenderingOutputStatus.h"
namespace DlssNr::Capability {
bool PrepareObservationReport(ObservationReportBundle& bundle) noexcept {
    try {
        if(!bundle.observation.capture.snapshot || bundle.discovery.count>64 || bundle.completed<bundle.started)return false;
        for(uint32_t i=0;i<bundle.discovery.count;i++)if(bundle.discovery.candidates[i].mayAdmit)return false;
        auto exported=ExportDiagnostic(*bundle.observation.capture.snapshot,ExportProfile::Minimal);
        if(exported.state!=ExportState::Encoded)return false;
        using Json=Wire::Json;
        Json document={{"schema","nr-observation-refresh-1"},{"operation",std::to_string(bundle.operation)},
            {"context_revision",std::to_string(bundle.context)},{"manual",bundle.manual},
            {"capture_start_tick_ms",std::to_string(bundle.started)},{"capture_end_tick_ms",std::to_string(bundle.completed)},
            {"completed_utc_ms",std::to_string(bundle.completedUtc)},
            {"capability_report",Json::parse(exported.utf8)},
            {"owner_validated_inputs",{{"depth",bundle.owner.depth},{"motion",bundle.owner.motion},
                {"jitter",bundle.owner.jitter},{"pre_exposure",bundle.owner.preExposure}}}};
        const auto& output=bundle.renderingOutput;
        const auto producer=[](RenderingOutput::Producer value){switch(value){
            case RenderingOutput::Producer::Native:return "Native";
            case RenderingOutput::Producer::Present:return "Present";
            case RenderingOutput::Producer::BuiltIn:return "BuiltIn";
            case RenderingOutput::Producer::CapturedImage:return "CapturedImage";
            default:return "None";}};
        const auto phase=[](RenderingOutput::Phase value){switch(value){
            case RenderingOutput::Phase::Waiting:return "Waiting";
            case RenderingOutput::Phase::Active:return "Active";
            case RenderingOutput::Phase::Quiescing:return "Quiescing";
            case RenderingOutput::Phase::StartingFallback:return "StartingFallback";
            case RenderingOutput::Phase::FallbackActive:return "FallbackActive";
            case RenderingOutput::Phase::Blocked:return "Blocked";
            case RenderingOutput::Phase::FallbackPaused:return "FallbackPaused";
            default:return "Off";}};
        document["rendering_output"]={{"snapshot_tick_ms",std::to_string(bundle.renderingOutputCapturedTickMs)},
            {"sampled_tick_ms",std::to_string(output.sampledAt)},
            {"fresh_at_capture",RenderingOutput::Fresh(output,bundle.renderingOutputCapturedTickMs)},
            {"active_at_capture",RenderingOutput::Active(output,bundle.renderingOutputCapturedTickMs)},
              {"requested",output.requested},{"phase",phase(output.phase)},{"producer",producer(output.producer)},
              {"requested_route",output.route},{"activation","explicit-mode-selection"},
            {"completed",std::to_string(output.completed)},{"source_frames",std::to_string(output.sourceFrames)},
            {"fallback_eligible",output.fallbackEligible},{"comparison_bypass",output.comparisonBypass},
            {"display_observed",output.displayObserved},{"reason",output.reason}};
        document["built_in_vulkan_inputs"]={
            {"selected_at_capture",bundle.nativeCaptureSelected},{"captured_frames",std::to_string(bundle.nativeCaptures)},
            {"copyback_completed",std::to_string(bundle.nativeDeliveries)},{"reason",bundle.nativeCaptureReason},
            {"render_scopes",std::to_string(bundle.nativeScopes)},
            {"depth_copies_recorded",std::to_string(bundle.nativeDepthCopies)},
            {"estimated_scene_associations",std::to_string(bundle.nativeEstimatedAssociations)},
            {"depth_source","captured attachment"},{"motion_source","image-derived optical flow"},
            {"engine_motion",false},{"native_camera",false},{"display_observed",false}};
        // This is the effective prepared owner frozen during collection. Export
        // does not query a live owner or infer GPU/display success from counters.
        const auto& builtIn=bundle.preparedBuiltIn;
        const auto& v2=builtIn.status;
        auto origin=[](PreparedGuides::Origin value){switch(value){
            case PreparedGuides::Origin::Native:return "Native";
            case PreparedGuides::Origin::Observed:return "Observed";
            case PreparedGuides::Origin::Derived:return "Derived";
            case PreparedGuides::Origin::External:return "External";
            default:return "Unknown";}};
        document["built_in_inputs"]={
            {"status_version",2},{"available_at_capture",builtIn.available},{"fresh_at_capture",builtIn.fresh},
            {"snapshot_tick_ms",std::to_string(bundle.preparedBuiltInCapturedTickMs)},
            {"updated_tick_ms",std::to_string(v2.updatedTickMs)},
            {"source_api",PreparedGuides::SourceApiName(v2.sourceApi)},{"source_api_code",v2.sourceApi},
            {"selected_source",Connections::SourceName(v2.selectedSource)},
            {"effective_transport",Connections::TransportName(v2.effectiveTransport)},
            {"stage",PreparedGuides::StageName(v2.stage)},{"stage_code",static_cast<uint32_t>(v2.stage)},
            {"reason",v2.reason},{"session",std::to_string(v2.session)},{"capture",std::to_string(v2.capture)},
            {"generation",std::to_string(v2.generation)},{"producer_identity",std::to_string(v2.producerIdentity)},
            {"candidate_id",std::to_string(v2.candidateId)},{"candidate_confidence",v2.candidateConfidence},
            {"depth_direction",v2.depthDirection},{"depth_origin",origin(v2.depthOrigin)},
            {"motion_origin",origin(v2.motionOrigin)},{"camera_origin",origin(v2.cameraOrigin)},
            {"creation_ready",v2.creationReady!=0},{"guide_ready",v2.guideReady!=0},
            {"model_preparing",v2.modelPreparing!=0},{"output_valid",v2.outputValid!=0},
            {"restart_required",v2.restartRequired!=0},{"display_observed",v2.displayObserved!=0},
            {"capture_width",v2.captureWidth},{"capture_height",v2.captureHeight},
            {"work_width",v2.workWidth},{"work_height",v2.workHeight},
            {"output_width",v2.outputWidth},{"output_height",v2.outputHeight},
            {"capture_age_ms",std::to_string(v2.captureAgeMs)},
            {"input_frames",std::to_string(v2.inputFrames)},
            {"model_completions",std::to_string(v2.modelCompletions)},
            {"copyback_completions",std::to_string(v2.copybackCompletions)}};
        const auto& prepared=bundle.preparedExternal;
        const auto& status=prepared.status;
        document["prepared_external_inputs"]={
            {"source","ReShade prepared inputs"},{"available_at_capture",prepared.available},
            {"fresh_at_capture",prepared.fresh},{"native_ownership",false},{"display_observed",false},
            {"active_at_capture",status.active!=0},{"source_api",PreparedGuides::SourceApiName(status.sourceApi)},
            {"session",std::to_string(status.session)},{"capture",std::to_string(status.capture)},
            {"updated_tick_ms",std::to_string(status.updatedTickMs)},
            {"captured_depth_ready",status.depthReady!=0},{"estimated_motion_ready",status.motionReady!=0},
            {"stage",PreparedGuides::StageName(status.stage)},{"reason",status.reason},
            {"input_frames",std::to_string(status.inputFrames)},
            {"model_completions",std::to_string(status.modelCompletions)},
            {"copyback_completions",std::to_string(status.copybackCompletions)}};
        auto& discovery=document["discovery"];
        discovery={{"observed",bundle.hasDiscovery},{"scope","create_only"},{"may_admit",false},
            {"owner_join","unsupported"},{"complete_transport",bundle.hasDiscovery && bundle.discovery.coverage.complete},
            {"enqueued",std::to_string(bundle.discovery.coverage.enqueued)},
            {"dropped",std::to_string(bundle.discovery.coverage.dropped)},
            {"drop_exact",bundle.discovery.coverage.dropExact},{"loss_mask",bundle.discovery.coverage.lossMask},
            {"family_states",bundle.discovery.coverage.familyStates},{"candidates",Json::array()},{"quota_refusals",Json::array()}};
        for(uint32_t i=0;i<bundle.discovery.count;i++) {
            const auto& candidate=bundle.discovery.candidates[i];
            discovery["candidates"].push_back({{"token",std::to_string(candidate.token)},
                {"sequence",std::to_string(candidate.sequence)},{"epoch",std::to_string(candidate.epoch)},
                {"descriptor_known",candidate.descriptorKnown},{"dimension",candidate.descriptor.dimension},
                {"width",std::to_string(candidate.descriptor.width)},{"height",candidate.descriptor.height},
                {"format",candidate.descriptor.format},{"depth_supported",candidate.depthSupported},
                {"retention",static_cast<uint32_t>(candidate.retention)},{"may_admit",false}});
        }
        for(uint32_t i=0;i<CandidateObserver::BudgetCauseCount;i++)
            discovery["quota_refusals"].push_back({{"reason",CandidateObserver::BudgetCauseNames[i]},
                {"count",std::to_string(bundle.discovery.coverage.budgetRefused[i])},
                {"exact",bundle.discovery.coverage.budgetRefusalExact[i]}});
        auto encoded=document.dump();if(encoded.size()>4*1024*1024)return false;
        bundle.utf8=std::move(encoded);return true;
    }catch(...) {return false;}
}
}
