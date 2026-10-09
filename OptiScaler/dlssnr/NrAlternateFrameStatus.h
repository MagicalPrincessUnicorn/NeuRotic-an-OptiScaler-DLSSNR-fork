#include <menu/Localization.h>
#pragma once
#include "NrAlternateFrameContract.h"
#include <mutex>
namespace DlssNr::AlternateFrame {
struct Status {StateKind state=StateKind::Disabled;Reason reason=Reason::Off;std::uint64_t fulls=0,carries=0;};
inline std::mutex statusMutex;
inline Status status;
inline void PublishStatus(Status value){std::lock_guard lock(statusMutex);status=value;}
inline Status ReadStatus(){std::lock_guard lock(statusMutex);return status;}
inline const char* ReasonText(Reason r) {
    switch(r) {
    case Reason::Off:return Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off");
    case Reason::InputIdentityUnknown:return Neurotic::UiLiteral("ingame.nralternateframestatus.frame_identity_unavailable_6c9cd4ad", "frame identity unavailable");
    case Reason::UnsupportedApi:return "DirectX 12 required";
    case Reason::UnsupportedRoute:return Neurotic::UiLiteral("ingame.nralternateframestatus.native_post_sr_only_1e5f4cf9", "Native Post-SR only");
    case Reason::UnsupportedPassCount:return Neurotic::UiLiteral("ingame.nralternateframestatus.one_nr_pass_required_23d52257", "one NR pass required");
    case Reason::UnsupportedRaster:return Neurotic::UiLiteral("ingame.nralternateframestatus.full_resolution_required_75fd8de6", "full resolution required");
    case Reason::UnsupportedSceneFormat:return Neurotic::UiLiteral("ingame.nralternateframestatus.supported_floating_point_scene_required_21a3e8f2", "supported floating-point scene required");
    case Reason::SceneDomainUnknown:return Neurotic::UiLiteral("ingame.nralternateframestatus.scene_colour_domain_unknown_cb301747", "scene colour domain unknown");
    case Reason::PresentationHdrDeferred:return Neurotic::UiLiteral("ingame.nralternateframestatus.hdr_presentation_unsupported_4c41bc5d", "HDR presentation unsupported");
    case Reason::RayReconstructionDeferred:return Neurotic::UiLiteral("ingame.nralternateframestatus.ray_reconstruction_unsupported_9f31bf8c", "Ray Reconstruction unsupported");
    case Reason::MissingMotionConvention:return Neurotic::UiLiteral("ingame.nralternateframestatus.original_motion_metadata_unavailable_a86250cc", "original motion metadata unavailable");
    case Reason::MissingDepthEncoding:return Neurotic::UiLiteral("ingame.nralternateframestatus.depth_convention_unavailable_de3f6c30", "depth convention unavailable");
    case Reason::GuideBounds:return Neurotic::UiLiteral("ingame.nralternateframestatus.guide_bounds_unsupported_8ef771de", "guide bounds unsupported");
    case Reason::AnchorNotImmutable:return Neurotic::UiLiteral("ingame.nralternateframestatus.previous_gpu_work_not_reusable_1d999098", "previous GPU work not reusable");
    case Reason::NoAnchor:return Neurotic::UiLiteral("ingame.nralternateframestatus.no_reusable_previous_frame_e6d9fee7", "no reusable previous frame");
    case Reason::ResourceCapacity:return Neurotic::UiLiteral("ingame.nralternateframestatus.gpu_storage_busy_or_at_capacity_ade2ca5e", "GPU storage busy or at capacity");
    case Reason::BudgetHeadroom:return Neurotic::UiLiteral("ingame.nralternateframestatus.gpu_memory_reserve_f95c1fff", "GPU memory reserve");
    case Reason::OutputContractRequiresModelAttempt:return Neurotic::UiLiteral("ingame.nralternateframestatus.output_requires_a_model_evaluation_4bcf3bbc", "output requires a model evaluation");
    case Reason::ExposureRatioOutOfRange:case Reason::ExposureScaleInvalid:case Reason::ExposureScaleTransition:return Neurotic::UiLiteral("ingame.nralternateframestatus.exposure_changed_3303a988", "exposure changed");
    case Reason::ResetDue:return Neurotic::UiLiteral("ingame.nralternateframestatus.history_reset_2c2f4fc3", "history reset");
    case Reason::PredecessorMismatch:return Neurotic::UiLiteral("ingame.nralternateframestatus.source_continuity_changed_61445412", "source continuity changed");
    case Reason::AnchorTooOld:return Neurotic::UiLiteral("ingame.nralternateframestatus.previous_frame_too_old_4d36ab32", "previous frame too old");
    case Reason::ConfigRevoked:return Neurotic::UiLiteral("ingame.nralternateframestatus.settings_or_guides_changed_137cd3a4", "settings or guides changed");
    case Reason::RecordingFailure:case Reason::PartialEffects:return Neurotic::UiLiteral("ingame.nralternateframestatus.gpu_recording_failed_df73db63", "GPU recording failed");
    case Reason::ClockUnknown:return Neurotic::UiLiteral("ingame.nralternateframestatus.frame_timing_unavailable_57d719f2", "frame timing unavailable");
    case Reason::None:return "Alternating";
    case Reason::RefreshDue:return "Alternating";
    case Reason::BootstrapDue:return Neurotic::UiLiteral("ingame.nralternateframestatus.warming_up_06c6d1b6", "Warming up");
    case Reason::RecoveryDue:return "Recovering";
    default:return Neurotic::UiLiteral("ingame.nralternateframestatus.input_unavailable_e6ba52c7", "input unavailable");
    }
}
}
