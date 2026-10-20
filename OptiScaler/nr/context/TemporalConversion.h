#pragma once
#include <cstdint>

namespace Neurotic::Context::Temporal
{
// Effective values at an existing consumer boundary. These are not a claim that
// fallback values were observed, or that different consumers share model units.
// Their source owners retain observation provenance, active rectangles and pairs.
struct Vector { float x; float y; };
struct Values { Vector motionScale; Vector jitter; };
struct Conversion
{
    Values source;
    Vector motionFactor, jitterFactor;
    Values values;
};

// Established D3D12 fallback is per axis. Callers requiring nonzero extents
// (including Vulkan) retain their own validation before using this arithmetic.
inline Vector RasterRatio(std::uint32_t width, std::uint32_t height,
                          std::uint32_t targetWidth, std::uint32_t targetHeight)
{
    return {width ? static_cast<float>(targetWidth) / width : 1.0f,
            height ? static_cast<float>(targetHeight) / height : 1.0f};
}
inline Vector Scale(Vector value, Vector factor)
{
    return {value.x * factor.x, value.y * factor.y};
}
// Motion and jitter have independent bases. Pass the factors belonging to the
// actual consumer; never infer a universal convention from an API or route name.
inline Values Project(const Values& source, Vector motionFactor, Vector jitterFactor)
{
    return {Scale(source.motionScale, motionFactor), Scale(source.jitter, jitterFactor)};
}
inline Conversion Convert(const Values& source, Vector motionFactor, Vector jitterFactor)
{
    return {source, motionFactor, jitterFactor, Project(source, motionFactor, jitterFactor)};
}
}
