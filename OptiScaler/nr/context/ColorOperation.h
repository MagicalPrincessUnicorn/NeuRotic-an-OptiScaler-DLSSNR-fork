#pragma once
#include <cstdint>
#include <type_traits>

namespace Neurotic::Context
{
// A small resolved operation, not a source observation or an eligibility check.
// The API owner selects exposure/hold/fallback and valid representation FIRST.
// Unknown primaries/transfer remain unknown in its existing ColorDescription;
// floating-point storage or this record never invents those facts.
struct ModelColorOperation
{
    std::uint32_t passthrough;
    float whitePoint;
    std::uint32_t signedOriginal;
    std::uint32_t useGameExposure;
    float exposurePreMul;
    std::uint32_t reversibleMode;
};
static_assert(std::is_trivially_copyable_v<ModelColorOperation>);

template<class Constants>
inline void ApplyModelColor(const ModelColorOperation& operation, Constants& constants) noexcept
{
    constants.Passthrough=operation.passthrough;
    constants.WhitePoint=operation.whitePoint;
    constants.SignedPresent=operation.signedOriginal;
    constants.UseGameExposure=operation.useGameExposure;
    constants.ExposurePreMul=operation.exposurePreMul;
    constants.ReversibleMode=operation.reversibleMode;
}
inline ModelColorOperation WithReversibleMode(ModelColorOperation operation, std::uint32_t mode) noexcept
{
    operation.reversibleMode=mode;
    return operation;
}

// API selectors still own valid format/colorspace pairs and profile/revision
// identity. This names their actual conversion, without equating DXGI G22 and
// sRGB sampling or scene exposure and display normalization.
enum class PresentColorConversion : std::uint32_t { Copy, ChannelOrder, Pq2020Linear709 };
struct PresentColorOperation
{
    PresentColorConversion conversion=PresentColorConversion::Copy;
    bool formatSrgb=false;
    bool hdr=false;
    float modelWhitePoint=1.0f;
    bool signedOriginal=false;
    // Recipe normalization is 80 nits per scRGB unit where explicitly selected;
    // model white point 2.5 is not observed display paper-white metadata.
    float workingNitsPerUnit=0.0f;
};
static_assert(std::is_trivially_copyable_v<PresentColorOperation>);
}
