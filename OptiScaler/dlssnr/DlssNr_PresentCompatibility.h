#pragma once

#include <cstdint>

namespace DlssNr::PresentCompatibility
{
enum class Api : std::uint8_t { D3D11, D3D12 };
enum class PixelPath : std::uint8_t { None, Rgba8Direct, Rgb10Conversion };

struct Capabilities
{
    Api api = Api::D3D12;
    bool directQueue = false;
    bool singleSample = false;
    bool flipModel = false;
    bool sdr = false;
    bool texture2D = false;
    bool nonZeroExtent = false;
    bool sameDevice = false;
    bool rgba8 = false;
    bool rgb10 = false;
    bool rgba8ShaderLoad = false;
    bool rgba8TypedStore = false;
    bool targetShaderLoad = false;
    bool targetTypedStore = false;
    bool sharedResources = false;
    bool synchronization = false;
};

struct Admission
{
    bool supported = false;
    PixelPath path = PixelPath::None;
    const char* reason = "unsupported Present target";
};

constexpr Admission Admit(const Capabilities& value)
{
    if (!value.singleSample || !value.flipModel)
        return {false, PixelPath::None, "target is not single-sample flip-model"};
    if (!value.sdr)
        return {false, PixelPath::None, "HDR or non-SDR color space is unsupported"};
    if (!value.texture2D || !value.nonZeroExtent)
        return {false, PixelPath::None, "target shape is unsupported"};
    if (!value.sameDevice)
        return {false, PixelPath::None, "Present target and NR queue use different devices"};
    if (!value.directQueue)
        return {false, PixelPath::None, "a direct D3D12 queue is unavailable"};
    if (value.api == Api::D3D11 && (!value.sharedResources || !value.synchronization))
        return {false, PixelPath::None, "D3D11 shared-resource synchronization is unavailable"};
    if (!value.rgba8 && !value.rgb10)
        return {false, PixelPath::None, "Present target format is unsupported"};
    if (!value.rgba8ShaderLoad || !value.rgba8TypedStore)
        return {false, PixelPath::None, "RGBA8 model surface capabilities are unavailable"};
    if (value.rgb10 && (!value.targetShaderLoad || !value.targetTypedStore))
        return {false, PixelPath::None, "10-bit conversion capabilities are unavailable"};
    return {true, value.rgb10 ? PixelPath::Rgb10Conversion : PixelPath::Rgba8Direct, "supported"};
}
} // namespace DlssNr::PresentCompatibility
