#pragma once
#include "NrScreenshotSrPair.h"
#include <nvsdk_ngx.h>
#include <array>
namespace DlssNr {
inline constexpr std::array<const char*,12> VkNrPerformanceCreationKeys={
    NVSDK_NGX_Parameter_Width,NVSDK_NGX_Parameter_Height,NVSDK_NGX_Parameter_OutWidth,
    NVSDK_NGX_Parameter_OutHeight,NVSDK_NGX_Parameter_PerfQualityValue,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance};
template<class Params> bool ReadVkNrPerformanceSignature(Params* params,std::array<unsigned,12>& signature){
    if(!params)return false;
    signature={};for(size_t i=0;i<signature.size();++i)
        if(static_cast<unsigned>(params->Get(VkNrPerformanceCreationKeys[i],&signature[i]))!=1&&i<6)return false;
    return signature[0]&&signature[1]&&signature[2]&&signature[3];
}
}
