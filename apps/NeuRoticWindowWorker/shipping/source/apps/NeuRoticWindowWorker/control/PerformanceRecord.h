// GPL-3.0. Completed/failed frame observations; no media-time or scanout inference.
#pragma once
#include "ControlCore.h"
namespace nrw {
inline Json Measurement(OptionalMs value){return value?Json(*value):Json(nullptr);}
inline Json CaptureStatisticsJson(const CaptureStatistics& s) {
    return {{"arrivalCallbacks",s.arrivals},{"dequeuedSamples",s.dequeued},{"supersededSamples",s.superseded},
            {"presentCalls",s.presentCalls},{"foregroundHwnd",s.foregroundHwnd},{"foregroundPid",s.foregroundPid},
            {"sourceGuiFlags",s.guiFlags},{"outputVisible",s.outputVisible},{"interactionReason",s.interactionReason}};
}
inline Json PerformanceRecord(const CapturedFrame& frame,const NrResult& result,uint64_t revision,
                              const std::string& provider,double captureMs,double guidanceMs,
                              OptionalMs presentMs,OptionalMs publicationAge,bool published) {
    const auto& m=result.measurements;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    const auto stamp=frame.stamp.timestampQpc;
    Json captureTime=nullptr;
    if(stamp && frequency.QuadPart>0)captureTime=(stamp/uint64_t(frequency.QuadPart))*10000000+
        (stamp%uint64_t(frequency.QuadPart))*10000000/uint64_t(frequency.QuadPart);
    Json work=nullptr;if(result.workWidth&&result.workHeight)work=Json::array({result.workWidth,result.workHeight});
    return {{"schema_version",1},{"record_type","frame_result"},{"synthetic",false},
        {"session_id",std::to_string(frame.stamp.session)},{"epoch",frame.stamp.session},
        {"capture_sequence",frame.stamp.sequence},{"history_epoch",m.historyEpoch},{"settings_revision",revision},
        {"capture_timestamp_100ns",captureTime},{"media_pts_100ns",nullptr},{"content_id",nullptr},
        {"equality_evidence","unknown"},{"provider_sha256",provider.empty()?Json(nullptr):Json(provider)},
        {"adapter_luid",m.adapterLuid.empty()?Json(nullptr):Json(m.adapterLuid)},
        {"capture_extent",{frame.stamp.width,frame.stamp.height}},{"nr_input_extent",work},{"nr_output_extent",work},
        {"output_extent",{frame.stamp.width,frame.stamp.height}},
        {"outcome",published?"enhanced_published":result.completed?"enhanced_completed_not_published":"failed"},
        {"nr_submitted",result.submitted},{"nr_gpu_completed",m.gpuCompleted},{"reason",result.reason},
        {"reset_reason",m.reset?Json("first-frame-or-input-discontinuity"):Json(nullptr)},
        {"cpu",{{"callback_ms",nullptr},{"prepare_ms",Measurement(m.prepareMs)},
            {"evaluate_call_ms",Measurement(m.evaluateCallMs)},{"submit_ms",Measurement(m.submitCallMs)},
            {"fence_wait_ms",Measurement(m.queueWaitMs)},{"present_call_ms",nullptr}}},
        {"gpu",{{"capture_copy_ms",nullptr},{"conversion_ms",Measurement(m.gpu[1])},
            {"depth_ms",nullptr},{"motion_ms",nullptr},{"nr_ms",Measurement(m.gpu[2])},{"compose_ms",Measurement(m.gpu[3])}}},
        {"capture_to_publication_ms",published?Measurement(publicationAge):Json(nullptr)},
        {"observed_display_latency_ms",nullptr},{"pending_count",0},
        {"nr_in_flight_count",result.submitted&&!m.gpuCompleted?1:0},{"owned_resource_bytes",nullptr},
        {"clock_note","WGC compositor QPC to accepted Present host return; not decoder PTS or scanout. GPU spans use the private direct queue frequency after its fence."},
        {"note","Coverage is admitted capture samples. GPU NR brackets provider-recorded commands; auxiliary inference is not inferred from neutral uploads."},
        {"host",{{"capture_ms",captureMs},{"guidance_ms",guidanceMs},{"nr_process_ms",result.milliseconds},
            {"present_stage_ms",Measurement(presentMs)},{"d11_publication_ms",Measurement(m.publishMs)},
            {"shape_ms",Measurement(m.shapeMs)},{"ingress_ms",Measurement(m.ingressMs)},
            {"guide_prepare_ms",Measurement(m.guidePrepareMs)},{"identity_check_ms",Measurement(m.identityMs)},
            {"guide_upload_bytes",m.guideUploadBytes},{"guide_preparation",m.guidePreparation},
            {"gpu_guide_prepare_ms",Measurement(m.gpu[0])},
            {"gpu_guide_upload_ms",Measurement(m.guidePreparation=="cpu-upload" ? m.gpu[0] : OptionalMs{})},
            {"gpu_timestamp_frequency",m.timestampFrequency},{"completion_fence",m.completionFence}}}};
}
}
