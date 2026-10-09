#pragma once
#include <NVNGX_Parameter.h>
#include <memory>

namespace Neurotic::Runtime
{
// Only creation scalars cross the call boundary. Evaluation resources, callbacks
// and caller-owned parameter storage are deliberately not retained.
inline std::shared_ptr<NVSDK_NGX_Parameter> SnapshotCreateParameters(const NVSDK_NGX_Parameter& source)
{
    auto result = std::make_shared<NVNGX_Parameters>(API::DX12, false);
    for (const char* key : { NVSDK_NGX_Parameter_Width, NVSDK_NGX_Parameter_Height,
          NVSDK_NGX_Parameter_OutWidth, NVSDK_NGX_Parameter_OutHeight,
          NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags })
    {
        unsigned int value = 0;
        if (source.Get(key, &value) != NVSDK_NGX_Result_Success) return {};
        result->Set(key, value);
    }
    for (const char* key : {
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
          "RayReconstruction.Hint.Render.Preset.DLAA", "RayReconstruction.Hint.Render.Preset.UltraQuality",
          "RayReconstruction.Hint.Render.Preset.Quality", "RayReconstruction.Hint.Render.Preset.Balanced",
          "RayReconstruction.Hint.Render.Preset.Performance", "RayReconstruction.Hint.Render.Preset.UltraPerformance",
          "DLSS.Use.HW.Depth", "DLSS.Denoise.Mode", "DLSS.Roughness.Mode",
          "CreationNodeMask", "VisibilityNodeMask", "OptiScaler.SupportsUpscaleSize",
          "FSR.upscaleSize.width", "FSR.upscaleSize.height" })
    {
        unsigned int value = 0;
        if (source.Get(key, &value) == NVSDK_NGX_Result_Success) result->Set(key, value);
    }
    return result;
}
}
