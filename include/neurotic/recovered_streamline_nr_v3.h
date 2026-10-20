// NeuRotic recovered interface, not an NVIDIA-authored private SDK header.
// ABI evidence: artifacts/streamline_nr_recovery/RECOVERED_CONTRACT.md.
// Layout follows the MIT-licensed openNR recovery (Carlos Lopez, 2026);
// original source and complete notice are preserved in that evidence bundle.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <sl.h>

namespace neurotic::streamline_nr
{
inline constexpr sl::Feature kFeature = 1004u;
inline constexpr sl::BufferType kBufferTypeUpliftInputColor = 70u;
inline constexpr sl::BufferType kBufferTypeUpliftOutputColor = 71u;
inline constexpr sl::BufferType kBufferTypeUpliftControlMask = 72u;
inline constexpr char kSetOptionsFunction[] = "slDLSSNRSetOptions";
inline constexpr char kControlProfileName[] = "OpenNrExplicitV3Control";

enum class Mode : std::uint32_t { Off = 0u, On = 1u };
// Numeric recovered choices; no vendor semantic names are asserted.
enum class PerformanceMode : std::uint32_t { Value1 = 1u, Value2 = 2u, Value3 = 3u, Value6 = 6u };

struct RecoveredOptionsV3 : sl::BaseStructure
{
    inline static constexpr sl::StructType s_structType{
        0x29dfdfe0u, 0x273au, 0x4e72u, {0xb4u, 0x92u, 0x2du, 0xc8u, 0x23u, 0xd5u, 0xb1u, 0xadu}};

    // Values are intentionally supplied by an explicit profile, not vendor defaults.
    RecoveredOptionsV3() : sl::BaseStructure(s_structType, sl::kStructVersion3) {}
    Mode mode;
    float intensity;
    float localToneStrength;
    float localStructureStrength;
    float globalToneStrength;
    std::uint32_t style;
    std::uint32_t preset;
    sl::Boolean useAutoMask;
    float skinStructureStrength;
    PerformanceMode performanceMode;
};

// The x64 layout is the only recovered target supported by this lane.
static_assert(sizeof(void*) == 8, "Recovered NR ABI is supported only on x64");
static_assert(sizeof(sl::BaseStructure) == 0x20, "Streamline BaseStructure ABI mismatch");
static_assert(sizeof(sl::StructType) == 16);
static_assert(sizeof(float) == 4);
static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::underlying_type_t<Mode>) == 4);
static_assert(sizeof(std::underlying_type_t<PerformanceMode>) == 4);
static_assert(sizeof(std::underlying_type_t<sl::Boolean>) == 1);
static_assert(sizeof(sl::Boolean) == 1);
static_assert(sizeof(RecoveredOptionsV3) == 0x48, "Recovered NR options ABI mismatch");
static_assert(alignof(RecoveredOptionsV3) == alignof(sl::BaseStructure));

// offsetof on this inherited SDK structure is a compiler-supported ABI check.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(RecoveredOptionsV3, mode) == 0x20);
static_assert(offsetof(RecoveredOptionsV3, intensity) == 0x24);
static_assert(offsetof(RecoveredOptionsV3, localToneStrength) == 0x28);
static_assert(offsetof(RecoveredOptionsV3, localStructureStrength) == 0x2c);
static_assert(offsetof(RecoveredOptionsV3, globalToneStrength) == 0x30);
static_assert(offsetof(RecoveredOptionsV3, style) == 0x34);
static_assert(offsetof(RecoveredOptionsV3, preset) == 0x38);
static_assert(offsetof(RecoveredOptionsV3, useAutoMask) == 0x3c);
static_assert(offsetof(RecoveredOptionsV3, skinStructureStrength) == 0x40);
static_assert(offsetof(RecoveredOptionsV3, performanceMode) == 0x44);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using PFun_slDLSSNRSetOptions = sl::Result(const sl::ViewportHandle&, const RecoveredOptionsV3&);

inline RecoveredOptionsV3 MakeOpenNrControlProfile()
{
    RecoveredOptionsV3 options;
    options.mode = Mode::On; // Deliberate control-lane enablement; openNR default is Off.
    options.intensity = 1.0f;
    options.localToneStrength = 1.0f;
    options.localStructureStrength = 1.0f;
    options.globalToneStrength = 1.0f;
    options.style = 0u;
    options.preset = 1u;
    options.useAutoMask = sl::Boolean::eFalse;
    options.skinStructureStrength = -1.0f;
    options.performanceMode = PerformanceMode::Value3;
    return options;
}
} // namespace neurotic::streamline_nr
