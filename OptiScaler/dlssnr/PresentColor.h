#pragma once
#include "HdrObservation.h"

namespace DlssNr::PresentColor
{
enum class Profile : uint32_t { Unsupported, Sdr709, Hdr10Pq2020, ScRgb709 };
struct ModelContract
{
    bool hdr = false;
    float whitePoint = 1.0f;
    bool preserveSignedOriginal = false;
    uint32_t recipe = 0;
};
struct Decision
{
    Profile profile = Profile::Unsupported;
    DXGI_FORMAT workingFormat = DXGI_FORMAT_UNKNOWN;
    bool hdr = false;
    uint32_t recipe = 0;
    float whitePoint = 1.0f;
    const char* reason = "Present color representation is unsupported";
    bool Supported() const { return profile != Profile::Unsupported; }
    ModelContract Model() const { return {hdr, whitePoint, hdr, recipe}; }
};
inline Decision Select(const HdrObservation::Snapshot& s, DXGI_FORMAT actualFormat)
{
    if (!s.registered || s.transitioning || s.format != actualFormat)
        return {Profile::Unsupported, DXGI_FORMAT_UNKNOWN, false, 0, 1.0f,
                "Present color observation is missing, transitioning or mismatched"};
    if (s.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
        (actualFormat == DXGI_FORMAT_R8G8B8A8_UNORM || actualFormat == DXGI_FORMAT_R10G10B10A2_UNORM ||
         actualFormat == DXGI_FORMAT_B8G8R8A8_UNORM))
        return {Profile::Sdr709, DXGI_FORMAT_R8G8B8A8_UNORM, false, 0, 1.0f, "SDR709"};
    if (s.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 && actualFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
        return {Profile::Hdr10Pq2020, DXGI_FORMAT_R16G16B16A16_FLOAT, true, 1, 2.5f, "HDR10 PQ2020 / linear709 FP16"};
    if (s.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 && actualFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
        return {Profile::ScRgb709, DXGI_FORMAT_R16G16B16A16_FLOAT, true, 1, 2.5f, "scRGB linear709 FP16"};
    return {};
}
}
