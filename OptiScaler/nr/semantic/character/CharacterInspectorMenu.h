#include <menu/Localization.h>
#pragma once
#include "CharacterInspectorSettings.h"
#include "CharacterInspectorDraw.h"
#include "CharacterInspectorOverlay.h"
#include "CharacterRuntime.h"
#include "menu/SleekUi.h"
#include "menu/WindowSectionHeader.h"

namespace Neurotic::Semantic::Character {
template<class Option> void InspectorReset(Option& option) {
    const float required=ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetStyle().ItemSpacing.x;
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    if(ImGui::GetItemRectMax().x+required<=right) ImGui::SameLine();
    if(ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset"))) option.reset();
}
template<class Option> bool InspectorToggle(const char* label,Option& option,bool resettable=true) {
    ImGui::PushID(label);
    bool value=option.value_or_default();
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    const float reset=resettable?ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x+ImGui::GetStyle().FramePadding.x*2:0;
    const bool changed=ImGui::Checkbox("##Value",&value);
    if(changed) option=value;
    ImGui::SameLine();
    const float caption=ImGui::GetCursorScreenPos().x;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+(std::max)(1.f,right-caption-reset-ImGui::GetStyle().ItemSpacing.x));
    ImGui::TextUnformatted(label);ImGui::PopTextWrapPos();
    if(resettable) {
        ImGui::SameLine();ImGui::SetCursorScreenPos({right-reset,ImGui::GetItemRectMin().y});
        if(ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")))option.reset();
    }
    ImGui::PopID();
    return changed;
}
template<class Option> void InspectorSlider(const char* label,Option& option,int minimum,int maximum) {
    ImGui::PushID(label);ImGui::TextWrapped("%s",Neurotic::Translate(label).c_str());
    const auto& style=ImGui::GetStyle();
    const float reset=ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x+style.FramePadding.x*2+style.ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(1.f,ImGui::GetContentRegionAvail().x-reset));
    int value=static_cast<int>((std::clamp)(option.value_or_default(),uint32_t(minimum),uint32_t(maximum)));
    if(ImGui::SliderInt("##Value",&value,minimum,maximum))option=static_cast<uint32_t>(value);
    ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")))option.reset();ImGui::PopID();
}
// Content-sized cards use the active product palette and native scroll/navigation.
struct InspectorCard {
    explicit InspectorCard(const char* id,const char* title) {
        ImGui::BeginChild(id,{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|
            ImGuiChildFlags_AlwaysAutoResize|ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        Neurotic::Sleek::WindowSectionHeader(title);
    }
    ~InspectorCard(){ImGui::EndChild();}
};
inline const char* InspectorSessionStatus(const RuntimeView& runtime,bool enabled) {
    if(!enabled)return Neurotic::UiLiteral("ingame.objectruleeditor.disabled_1b52e3fc", "Disabled");
    switch(runtime.worker) {
    case WorkerState::Starting:return "Starting";
    case WorkerState::Ready:return runtime.sourceQualified?Neurotic::UiLiteral("ingame.characterinspectormenu.live_inspection_77380ed1", "Live inspection"):Neurotic::UiLiteral("ingame.characterinspectormenu.waiting_for_game_frames_9dfac9b7", "Waiting for game frames");
    case WorkerState::Fault:return Neurotic::UiLiteral("ingame.characterinspectormenu.needs_attention_a6586758", "Needs attention");
    case WorkerState::Stopping:return "Stopping";
    default:return Neurotic::UiLiteral("ingame.characterinspectormenu.enabled_not_running_eddd133f", "Enabled, not running");
    }
}
// Only an explicit enable transition starts capture. Rendering a saved On value
// does not issue duplicate worker-start requests.
template<class ConfigT> void RenderInspectorMenu(ConfigT& config,bool collapsible=true) {
    if(collapsible&&!ImGui::CollapsingHeader(Neurotic::UiLiteral("ingame.characterinspectormenu.inspector_experimental_658f29b5", "Inspector (Experimental)")))return;
    ResetInspectorPreviewSession(); // Retired UI session state cannot survive into live inspection.
    const auto runtime=TryCharacterRuntimeView();
    {
        InspectorCard card("##InspectorSession","People & objects");
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.live_detection_tracking_and_optional_pose_estima_3108cd8d", "Live detection, tracking and optional pose estimates. Configure each object's look in Object Rules."));
        const bool activationChanged=InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.enable_character_inspector_e79eef0a", "Enable Character Inspector"),config.CharacterInspectorEnabled,false);
        const auto settings=ReadSettings(config);
        if(activationChanged && settings.enabled)StartCharacterWorkerAtDefaultLocation();
        if(!settings.enabled)StopCharacterWorker();
        const bool fault=runtime.worker==WorkerState::Fault;
        ImGui::PushStyleColor(ImGuiCol_Text,fault?ImVec4(1,.35f,.3f,1):ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextWrapped("%s",Neurotic::Translate(InspectorSessionStatus(runtime,settings.enabled)).c_str());ImGui::PopStyleColor();
        ImGui::TextWrapped("%s",Neurotic::Translate(runtime.reason.c_str()).c_str());
        if(settings.enabled && runtime.worker==WorkerState::Stopped && !CharacterWorkerRequested())
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.toggle_off_and_on_to_start_live_inspection_for_t_26f15f33", "Toggle off and on to start live inspection for this session."));
        if(runtime.worker==WorkerState::Ready)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.u_boxes_u_fresh_samples_u_ms_sample_age_5e49e46c", "%u boxes | %u fresh samples | %u ms sample age"),runtime.returnedDetections,runtime.results,runtime.replyAgeMs);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.analysis_runs_independently_of_frame_generation__95cdba99", "Analysis runs independently of frame generation. Generated outputs reuse the latest admitted tracking result."));
    }
    const bool wide=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*44;
    if(ImGui::BeginTable("##InspectorControls",wide?2:1,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();{
            InspectorCard card("##InspectorDetection","Detection");
            InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.people_and_objects_25988585", "People and objects"),config.CharacterInspectorDetectObjects);
            InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.torso_estimate_dd4114ee", "Torso estimate"),config.CharacterInspectorTorsoEstimate);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.maximum_detections_f2ca570b", "Maximum detections"),config.CharacterInspectorMaximumPersons,1,16);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.tracking_interval_ms_df0e26cf", "Tracking interval (ms)"),config.CharacterInspectorUpdateIntervalMs,20,50);
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.people_only_or_up_to_80_fixed_object_categories__28807bf7", "People only or up to 80 fixed object categories. Pose estimates add processing cost; detections can be mistaken."));
        }
        ImGui::TableNextColumn();{
            InspectorCard card("##InspectorOverlay","Overlay & labels");
            InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.visible_character_boxes_ebb015c2", "Visible character boxes"),config.CharacterInspectorBodyBoxes);
            InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.bridge_brief_detection_misses_59114347", "Bridge brief detection misses"),config.CharacterInspectorSmartBoxHandoff);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.miss_grace_ms_14f52172", "Miss grace (ms)"),config.CharacterInspectorBoxHoldMs,0,100);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.outline_thickness_px_4561a5d4", "Outline thickness (px)"),config.CharacterInspectorBoxThickness,1,8);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.maximum_labels_1d660339", "Maximum labels"),config.CharacterInspectorMaximumLabels,0,16);
            InspectorToggle(Neurotic::UiLiteral("ingame.characterinspectormenu.scale_labels_to_output_resolution_a4dacf58", "Scale labels to output resolution"),config.CharacterInspectorAutoLabelScale);
            InspectorSlider(Neurotic::UiLiteral("ingame.characterinspectormenu.label_scale_a4218679", "Label scale (%)"),config.CharacterInspectorLabelScalePercent,50,300);
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.lost_tracking_clears_a_box_a_short_grace_period__6708b2c6", "Lost tracking clears a box. A short grace period bridges reliable detection misses; zero turns it off."));
        }
        ImGui::EndTable();
    }
    {
        InspectorCard card("##InspectorDetails",Neurotic::UiLiteral("ingame.characterinspectormenu.session_details_f0f7142f", "Session details"));
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.image_source_s_fce08735", "Image source: %s"),Neurotic::Translate(!runtime.sourceQualified?Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"):runtime.earlySource?Neurotic::UiLiteral("ingame.characterinspectormenu.before_upscaling_c474926c", "Before upscaling"):Neurotic::UiLiteral("ingame.characterinspectormenu.displayed_frame_6c3afdb5", "Displayed frame")).c_str());
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.provider_s_76e3f62b", "Provider: %s"),Neurotic::Translate(runtime.worker==WorkerState::Ready?Neurotic::UiLiteral("ingame.characterinspectormenu.opencv_cpu_b768e033", "OpenCV CPU"):Neurotic::UiLiteral("ingame.characterinspectormenu.not_running_6d4a9cdb", "Not running")).c_str());
        ImGui::PushID("InspectorProvider");
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.requested_provider_s_7800811d", "Requested provider: %s"),Neurotic::Translate(config.CharacterInspectorProvider.value_or_default().c_str()).c_str());
        InspectorReset(config.CharacterInspectorProvider);ImGui::PopID();
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.samples_u_processed_u_fresh_u_expired_dfa21606", "Samples: %u processed / %u fresh / %u expired"),runtime.processed,runtime.results,runtime.expired);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.processing_u_ms_sample_age_u_ms_c7d1827f", "Processing: %u ms | sample age: %u ms"),runtime.inferenceMs,runtime.replyAgeMs);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.detector_u_candidates_u_boxes_1f_ms_11642e3e", "Detector: %u candidates / %u boxes / %.1f ms"),runtime.detectorCandidates,runtime.returnedDetections,runtime.detectorMs);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.tracking_u_updates_1f_ms_84a1550b", "Tracking: %u updates / %.1f ms"),runtime.detectionUpdates,runtime.trackingMs);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.pose_u_attempts_u_usable_1f_ms_5e220b09", "Pose: %u attempts / %u usable / %.1f ms"),runtime.poseAttempts,runtime.usablePoses,runtime.poseMs);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.characterinspectormenu.dx11_d3d12_and_vulkan_frame_paths_are_available__29e4bb60", "DX11, D3D12 and Vulkan frame paths are available. Admission depends on the current source; this panel does not certify image quality or GPU support."));
    }
}
}
