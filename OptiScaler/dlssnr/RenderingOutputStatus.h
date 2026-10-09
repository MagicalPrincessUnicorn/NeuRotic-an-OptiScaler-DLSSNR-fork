#pragma once
#include "RenderingOutput.h"
#include "../menu/Localization.h"
namespace DlssNr::RenderingOutput {
inline bool Fresh(const Snapshot& snapshot,uint64_t now) noexcept {
    return snapshot.sampledAt && now>=snapshot.sampledAt && now-snapshot.sampledAt<=2000;
}
inline bool Active(const Snapshot& snapshot,uint64_t now) noexcept {
    return Fresh(snapshot,now) && snapshot.requested && snapshot.active &&
        (snapshot.phase==Phase::Active || snapshot.phase==Phase::FallbackActive);
}
inline const char* StatusLabel(const Snapshot& snapshot,uint64_t now) {
    if(!Fresh(snapshot,now))return Neurotic::UiLiteral("ingame.rendering_output.stale","Waiting for current rendering status.");
    switch(snapshot.phase){
    case Phase::Off:return Neurotic::UiLiteral("ingame.rendering_output.off","Neural Rendering is off.");
    case Phase::Quiescing:return Neurotic::UiLiteral("ingame.rendering_output.quiescing","Switching rendering routes.");
    case Phase::StartingFallback:return Neurotic::UiLiteral("ingame.rendering_output.starting_fallback","Starting NR Anything.");
    case Phase::Blocked:return Neurotic::UiLiteral("ingame.rendering_output.blocked","Neural Rendering needs attention.");
    case Phase::FallbackPaused:return Neurotic::UiLiteral("ingame.rendering_output.fallback_paused","NR Anything is paused.");
    case Phase::FallbackActive:if(Active(snapshot,now))return Neurotic::UiLiteral("ingame.rendering_output.fallback_active","NR Anything is producing output.");break;
    case Phase::Active:if(Active(snapshot,now))return Neurotic::UiLiteral("ingame.rendering_output.active","Neural Rendering is producing output.");break;
    default:break;
    }
    return Neurotic::UiLiteral("ingame.rendering_output.waiting","Waiting for completed rendering output.");
}

// Localize known supervision messages only; preserve external diagnostic bytes.
inline std::string ReasonLabel(std::string_view reason) {
    if(reason=="NR Anything is showing the original image while the menu is open or capture is paused")return Neurotic::UiMessage("ingame.rendering_output.reason.comparison_paused","NR Anything is showing the original image while the menu is open or capture is paused");
    if(reason=="Waiting for in-game Present callbacks to finish")return Neurotic::UiMessage("ingame.rendering_output.reason.waiting_for_in_game_present_callbacks_to_finish","Waiting for in-game Present callbacks to finish");
    if(reason=="Waiting for recorded GPU work to become reusable")return Neurotic::UiMessage("ingame.rendering_output.reason.waiting_for_recorded_gpu_work_to_become_reusable","Waiting for recorded GPU work to become reusable");
    if(reason=="NR Anything stopped because its game window is unavailable")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_fallback_stopped_because_its_game_target_is_no_longer_a","NR Anything stopped because its game window is unavailable");
    if(reason=="GPU completion is uncertain; restart the game before recovery")return Neurotic::UiMessage("ingame.rendering_output.reason.gpu_completion_is_uncertain_restart_the_game_before_recovery","GPU completion is uncertain; restart the game before recovery");
    if(reason=="Recover the model or device before starting NR Anything")return Neurotic::UiMessage("ingame.rendering_output.reason.model_or_device_failure_requires_recovery_before_captured_image_fallba","Recover the model or device before starting NR Anything");
    if(reason=="NR Anything stopped after the rendering request or safety state changed")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_fallback_stopped_after_the_rendering_request_or_safety_","NR Anything stopped after the rendering request or safety state changed");
    if(reason=="Waiting for in-game output ownership to settle")return Neurotic::UiMessage("ingame.rendering_output.reason.waiting_for_in_game_output_ownership_to_settle","Waiting for in-game output ownership to settle");
    if(reason=="NR Anything could not start safely. Game output is unavailable.")return Neurotic::UiMessage("ingame.rendering_output.reason.automatic_fallback_handoff_was_cancelled_game_output_is_not_safely_ava","NR Anything could not start safely. Game output is unavailable.");
    if(reason=="NR Anything is missing. Reinstall the complete NeuRotic package.")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_fallback_component_is_missing_reinstall_the_complete_ne","NR Anything is missing. Reinstall the complete NeuRotic package.");
    if(reason=="NR Anything requires a verified DLSS NR model")return Neurotic::UiMessage("ingame.rendering_output.reason.a_verified_dlss_nr_model_is_required_for_captured_image_fallback","NR Anything requires a verified DLSS NR model");
    if(reason=="The game window is not available for Windows capture")return Neurotic::UiMessage("ingame.rendering_output.reason.the_game_window_is_not_available_for_windows_capture","The game window is not available for Windows capture");
    if(reason=="NR Anything could not start with this game window")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_fallback_could_not_start_with_this_game_window","NR Anything could not start with this game window");
    if(reason=="NR Anything produced no fresh output. Toggle Neural Rendering to retry.")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_fallback_did_not_produce_fresh_output_toggle_neural_ren","NR Anything produced no fresh output. Toggle Neural Rendering to retry.");
    if(reason=="Captured-image worker exit is unconfirmed; restart the game before rendering can resume")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_worker_exit_is_unconfirmed_restart_the_game_before_rend","Captured-image worker exit is unconfirmed; restart the game before rendering can resume");
    if(reason=="Automatic output supervision stopped after an internal failure")return Neurotic::UiMessage("ingame.rendering_output.reason.automatic_output_supervision_stopped_after_an_internal_failure","Automatic output supervision stopped after an internal failure");
    if(reason=="Captured-image worker requires a restart")return Neurotic::UiMessage("ingame.rendering_output.reason.captured_image_worker_requires_a_restart","Captured-image worker requires a restart");
    constexpr std::string_view pattern="NR Anything could not obtain safe output ownership: {detail}";
    const auto prefix=pattern.substr(0,pattern.size()-8);
    if(reason.starts_with(prefix)){
        auto message=Neurotic::UiMessage("ingame.rendering_output.reason.ownership_detail","NR Anything could not obtain safe output ownership: {detail}");
        const auto token=message.find("{detail}");
        if(token!=std::string::npos)message.replace(token,8,ReasonLabel(reason.substr(prefix.size())));
        return message;
    }
    return std::string(reason);
}
}
