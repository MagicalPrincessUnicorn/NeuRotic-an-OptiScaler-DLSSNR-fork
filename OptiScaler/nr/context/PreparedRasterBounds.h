#pragma once
#include <cstdint>

namespace Neurotic::PreparedRaster {
// Common upper contract for the prepared-guide D3D12 transport. Actual API,
// format/vendor capabilities and charged resource creation still decide support.
inline constexpr std::uint64_t MaxAxis = 16384;
inline constexpr std::uint64_t MaxPackedBytes = 160ull * 1024 * 1024;
inline constexpr std::uint64_t PackedBytesPerPixel = 20;
inline constexpr std::uint64_t MaxPixels = MaxPackedBytes / PackedBytesPerPixel;
// Typed shared-image transport has its own 256 MiB actual-format ledger. It
// uses the API axis bound here, without inheriting the packed 20-byte ceiling.
constexpr bool AdmitAxes(std::uint64_t width, std::uint64_t height) {
    return width && height && width <= MaxAxis && height <= MaxAxis;
}
constexpr bool Admit(std::uint64_t width, std::uint64_t height, bool derive = false) {
    return AdmitAxes(width, height) &&
           width <= MaxPixels / height && (!derive || (width >= 256 && height >= 64));
}
}
