#pragma once
#include <cstdint>
#include <optional>

namespace DlssNr::NativeTemporalInputs
{
struct Metadata
{
    unsigned flags = 0;
    unsigned outputWidth = 0, outputHeight = 0;
    unsigned handle = 0;
    uint64_t generation = 0;
};
struct Rect
{
    unsigned x = 0, y = 0, width = 0, height = 0;
};
constexpr unsigned ResolveFlags(std::optional<unsigned> creation, std::optional<unsigned> evaluation)
{
    return creation.value_or(evaluation.value_or(0));
}
constexpr bool Fits(Rect region, uint64_t width, uint64_t height)
{
    return region.width && region.height && region.x <= width && region.y <= height &&
           region.width <= width - region.x && region.height <= height - region.y;
}
constexpr Rect GuideRect(unsigned x, unsigned y, unsigned renderWidth, unsigned renderHeight,
                         unsigned outputWidth, unsigned outputHeight, bool lowResolution)
{
    return {x, y, lowResolution ? renderWidth : outputWidth, lowResolution ? renderHeight : outputHeight};
}
constexpr bool Authoritative(bool superResolution, bool dlssBackend, bool dx12, bool adapter)
{
    return superResolution && dlssBackend && dx12 && !adapter;
}
}
