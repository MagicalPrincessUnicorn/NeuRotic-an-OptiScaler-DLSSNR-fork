// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>

namespace mfgunlock::profiles {

enum class Qualification : uint32_t { kUnknown, kObserveOnly, kQualified, kPatchable };
inline const char* QualificationName(Qualification state) {
  switch (state) {
    case Qualification::kObserveOnly: return "OBSERVE_ONLY";
    case Qualification::kQualified: return "QUALIFIED";
    case Qualification::kPatchable: return "PATCHABLE";
    default: return "UNKNOWN";
  }
}
struct Region { uint32_t rva; uint32_t bytes; uint64_t hash; };
struct MfgGate { Region code; uint32_t immediate_rva; };
struct VtableBinding { uint32_t rva; uint32_t target_rva; };
struct Selector { uint32_t rva; std::array<unsigned char, 2> before; };
inline uint64_t Fingerprint(std::span<const unsigned char> bytes) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (const auto byte : bytes) hash = (hash ^ byte) * 0x100000001b3ull;
  return hash;
}
inline bool MatchesRegions(std::span<const unsigned char> image, std::span<const Region> regions) {
  if (regions.empty()) return false;
  for (const auto& region : regions) {
    if (region.rva > image.size() || region.bytes > image.size() - region.rva ||
        Fingerprint(image.subspan(region.rva, region.bytes)) != region.hash) return false;
  }
  return true;
}

inline Qualification Classify(bool recognized, bool exact, bool prepared) {
  if (!recognized) return Qualification::kUnknown;
  if (!exact) return Qualification::kObserveOnly;
  return prepared ? Qualification::kPatchable : Qualification::kQualified;
}

inline constexpr uint32_t kTimestamp = 0x6a986031;
inline constexpr uint32_t kImageBytes = 0x737000;

// DL4RT CreateImpl, both PTX constructors, and both PTX loaders. These code
// ranges contain no base relocations in the qualified 310.9.1 image.
inline constexpr std::array<Region, 5> kEndpointCode{{
    {0x4d9f0, 0x381, 0x87f956430958b6adull},
    {0x46410, 0x14e0, 0x1673fa7bd3d78b8eull},
    {0x4b730, 0x1f1f, 0x6110aa99dd5aee70ull},
    {0x5d1e0, 0xc1a, 0xe4486da426b72aafull},
    {0x601a0, 0x11c6, 0x95d22d6fb2bd5dd7ull},
}};

inline constexpr std::array<Selector, 2> kEndpointSelectors{{
    {0x4da50, {0x7e, 0x23}},
    {0x4db91, {0x7e, 0x20}},
}};

inline constexpr std::array<MfgGate, 2> kMfgGates{{
    {{0x16856, 32, 0x2431eb0eabc7beddull}, 0x16864},
    {{0x35e43, 32, 0x3f8842504aa70f09ull}, 0x35e50},
}};

inline constexpr std::array<VtableBinding, 2> kEndpointVtables{{
    {0x64abe8, 0x5d1e0}, {0x64b028, 0x601a0},
}};

// The PTX loaders consume these exact 25 DL1 + 14 DL2 containers. Merely
// retargeting the framework fatbins is not sufficient to select these networks.
inline constexpr std::array<Region, 39> kEndpointPayloads{{
    {0x629800, 0x4080, 0x89dff841138dd3ddull},
    {0x62d880, 0x7f0, 0x8af8415b3be2cc40ull},
    {0x62e070, 0x1358, 0xf2fc168928ff7dfeull},
    {0x31cc40, 0x2ba0, 0x711ca0c3059dfaa2ull},
    {0x633fb0, 0x2c98, 0xba6ef97dc502a19bull},
    {0x636c50, 0x39a0, 0x2932ac7c94f98908ull},
    {0x63a5f0, 0x2c40, 0x45729dedf9d45653ull},
    {0x62f3d0, 0x4be0, 0x7b6afc4d91f6879bull},
    {0x63d230, 0x8f8, 0xdf1b73e889c7763aull},
    {0x63db30, 0x868, 0x892bea3bfdadeddcull},
    {0x63e3a0, 0x2c38, 0x3d3618626a6a30c5ull},
    {0x1d04d0, 0x2c40, 0x67df19f6aa49216dull},
    {0x640fe0, 0x8f8, 0xa3e4c48e80b6c528ull},
    {0x6418e0, 0x868, 0xa2d819163bbec287ull},
    {0x642150, 0x1cc8, 0x57bf86232bee81b8ull},
    {0x643e20, 0x20f8, 0x7cce7cd21fa9505full},
    {0x645f20, 0x2130, 0xea336fd5cb215384ull},
    {0x648050, 0x8f8, 0x7716bb3b62a34dfaull},
    {0x648950, 0x868, 0x7737b4f3949e04ull},
    {0x1fb100, 0x17c8, 0x52abdf3a72b5c181ull},
    {0x6491c0, 0x1768, 0xab6ea93220507c00ull},
    {0x4cc1f0, 0x8f8, 0xb06cd5341bae068bull},
    {0x5a2cc0, 0x860, 0x1c7ca69db01861c5ull},
    {0x5b7380, 0x12e8, 0xc8ac8dbec2d8e3e5ull},
    {0x5f5980, 0xf48, 0xcc9ae2145c85206eull},
    {0x1c1c60, 0x390, 0xa2041775a503e275ull},
    {0x1c1ff0, 0x1060, 0x25f6a15340923219ull},
    {0x1c3050, 0x1508, 0x7afa56ef76b1c2b8ull},
    {0x1c4560, 0x1478, 0x6dd8d8de9ca1686aull},
    {0x1c59e0, 0x14e8, 0x418873d01511db28ull},
    {0x1c6ed0, 0x12e8, 0x49c31a6cf881beb1ull},
    {0x18da20, 0x1b40, 0x9a5ccecff5711c58ull},
    {0x1c81c0, 0x840, 0xb29a172c987a1c2eull},
    {0x1c8a00, 0x1678, 0x7fc73cb00f793d90ull},
    {0x1ca080, 0x13e8, 0xf938f6de09283cadull},
    {0x1cb470, 0x1060, 0x1aadc6a153008187ull},
    {0x1cc4d0, 0x1320, 0x5420451670bda034ull},
    {0x1cd7f0, 0x1490, 0x547ac75cc61d056dull},
    {0x1cec80, 0x1850, 0x3cbd50ac2f7ca420ull},
}};


// Exact pristine SM89 containers which the retarget transaction is allowed to edit.
inline constexpr std::array<Region, 70> kRetargetPayloads{{
    {0x18da20, 0x1b40, 0x9a5ccecff5711c58ull},
    {0x1c1c60, 0x390, 0xa2041775a503e275ull},
    {0x1c1ff0, 0x1060, 0x25f6a15340923219ull},
    {0x1c3050, 0x1508, 0x7afa56ef76b1c2b8ull},
    {0x1c4560, 0x1478, 0x6dd8d8de9ca1686aull},
    {0x1c59e0, 0x14e8, 0x418873d01511db28ull},
    {0x1c6ed0, 0x12e8, 0x49c31a6cf881beb1ull},
    {0x1c81c0, 0x840, 0xb29a172c987a1c2eull},
    {0x1c8a00, 0x1678, 0x7fc73cb00f793d90ull},
    {0x1ca080, 0x13e8, 0xf938f6de09283cadull},
    {0x1cb470, 0x1060, 0x1aadc6a153008187ull},
    {0x1cc4d0, 0x1320, 0x5420451670bda034ull},
    {0x1cd7f0, 0x1490, 0x547ac75cc61d056dull},
    {0x1cec80, 0x1850, 0x3cbd50ac2f7ca420ull},
    {0x1d04d0, 0x2c40, 0x67df19f6aa49216dull},
    {0x1fb100, 0x17c8, 0x52abdf3a72b5c181ull},
    {0x31cc40, 0x2ba0, 0x711ca0c3059dfaa2ull},
    {0x4cc1f0, 0x8f8, 0xb06cd5341bae068bull},
    {0x5a2cc0, 0x860, 0x1c7ca69db01861c5ull},
    {0x5b7380, 0x12e8, 0xc8ac8dbec2d8e3e5ull},
    {0x5f5980, 0xf48, 0xcc9ae2145c85206eull},
    {0x629800, 0x4080, 0x89dff841138dd3ddull},
    {0x62d880, 0x7f0, 0x8af8415b3be2cc40ull},
    {0x62e070, 0x1358, 0xf2fc168928ff7dfeull},
    {0x62f3d0, 0x4be0, 0x7b6afc4d91f6879bull},
    {0x633fb0, 0x2c98, 0xba6ef97dc502a19bull},
    {0x636c50, 0x39a0, 0x2932ac7c94f98908ull},
    {0x63a5f0, 0x2c40, 0x45729dedf9d45653ull},
    {0x63d230, 0x8f8, 0xdf1b73e889c7763aull},
    {0x63db30, 0x868, 0x892bea3bfdadeddcull},
    {0x63e3a0, 0x2c38, 0x3d3618626a6a30c5ull},
    {0x640fe0, 0x8f8, 0xa3e4c48e80b6c528ull},
    {0x6418e0, 0x868, 0xa2d819163bbec287ull},
    {0x642150, 0x1cc8, 0x57bf86232bee81b8ull},
    {0x643e20, 0x20f8, 0x7cce7cd21fa9505full},
    {0x645f20, 0x2130, 0xea336fd5cb215384ull},
    {0x648050, 0x8f8, 0x7716bb3b62a34dfaull},
    {0x648950, 0x868, 0x007737b4f3949e04ull},
    {0x6491c0, 0x1768, 0xab6ea93220507c00ull},
    {0x675730, 0xac08, 0x501273ace681e6d2ull},
    {0x680340, 0x11b0, 0x18f4763d82179366ull},
    {0x681500, 0x2a50, 0xf3411d6daf617358ull},
    {0x683f60, 0x33e8, 0x04f2e97b5e1e1ee7ull},
    {0x687350, 0x1840, 0x039519ea4990e44eull},
    {0x688ba0, 0x1800, 0x7270b921236a0399ull},
    {0x68a3b0, 0xd3e0, 0x5befd5ae54f8cafdull},
    {0x6977a0, 0x18190, 0xe2eb54ee22f59ba3ull},
    {0x6af940, 0x33c8, 0x78d8b4bf73f3170dull},
    {0x6b2d10, 0x2de0, 0xb2b2efe90d6756e9ull},
    {0x6b5b00, 0x1288, 0x649703527caff4bbull},
    {0x6b6d90, 0x31f8, 0x532d9eeba8f77b7dull},
    {0x6b9f90, 0x19f0, 0xfa3a939e8c79ef31ull},
    {0x6bb990, 0x3900, 0x9c65bc7fd50ee90cull},
    {0x6bf2a0, 0x23f8, 0xbc0544183ccc4fabull},
    {0x6c16a0, 0x1e18, 0x24e723b48910d992ull},
    {0x6c34c0, 0xfc0, 0xc57c34553572354eull},
    {0x6c4490, 0x96e0, 0xa8f4b2257ea835cbull},
    {0x6cdb80, 0x3bb0, 0x3c76029f9b227b91ull},
    {0x6d1740, 0xdd30, 0x1d646eae171cc82aull},
    {0x6df480, 0x65b8, 0x5cc81aa36be3aecaull},
    {0x6e5a40, 0x8518, 0x5b0d1edf93fa5ee5ull},
    {0x6edf60, 0x3bb0, 0x487a639b74bd0fb1ull},
    {0x6f1b20, 0xe250, 0xfa77cfacbfc78cbdull},
    {0x6ffd80, 0x6638, 0xf4d6c6c18d7ddfc1ull},
    {0x7063c0, 0x1a60, 0xf4a914ade37fa944ull},
    {0x707e30, 0x1a50, 0xe6ab677464438120ull},
    {0x709890, 0x1e78, 0x75e2c0f97e7d9816ull},
    {0x70b710, 0x2718, 0xcbf0796eece7a43eull},
    {0x70de30, 0x1d10, 0x71b18e81fcab7610ull},
    {0x70fb50, 0x11d8, 0x08fc6e9cff9f3befull},
}};

struct PtxProfile {
  uint32_t arch;
  size_t declared_raw_size;  // zero when normalized identity is the authoritative guard
  size_t normalized_size;
  uint64_t raw_fnv1a64;
  size_t descriptor_references;
  const char* entry_name;
  const char* parameter_signature;
};

constexpr PtxProfile kWarpPtx = {
    120u, 39639u, 39638u, 0x7a6f5f41105c6d85ull, 8u,
    "Kernel_BlendCandidatesFused",
    ".param .align 8 .b8 Kernel_BlendCandidatesFused_param_0[240]"};

constexpr PtxProfile kMotionVectorPtx = {
    120u,
    0u,
    90731u,
    0xb1a2811b29625d41ull,
    8u,
    "Kernel_EstimateIntermMvecsScatter",
    "ld.param.f32 %f2, [Kernel_EstimateIntermMvecsScatter_param_0+120];"};

constexpr PtxProfile kInpaintPtx = {
    120u,
    0u,
    26439u,
    0x546151924160b69bull,
    8u,
    "Kernel_Prev2CurrUnpackPull",
    ".entry Kernel_Prev2CurrUnpackPull("};

constexpr PtxProfile kInpaintDecisionPtx = {
    120u,
    0u,
    23116u,
    0x9b47635b91b2436bull,
    8u,
    "Kernel_OutputPull",
    ".entry Kernel_OutputPull("};

struct KernelProfile {
  const char* role;
  const PtxProfile* ptx;
  uint32_t ada_cubin_bytes;
  uint64_t ada_cubin_hash;
};
inline constexpr std::array<KernelProfile, 4> kQualityKernels{{
    {"motion-vector", &kMotionVectorPtx, 39968u, 0x9642092def23b3dfull},
    {"inpaint", &kInpaintPtx, 17568u, 0x1ba6454ab039f9ddull},
    {"inpaint-decision", &kInpaintDecisionPtx, 15136u, 0xc4a5eb4a8694f835ull},
    {"warp", &kWarpPtx, 0u, 0u},
}};

struct ProviderProfile {
  const char* id;
  const char* version;
  const char* file_sha256;
  uint32_t timestamp;
  uint32_t image_size;
  std::span<const Region> retarget_payloads;
  std::span<const Region> endpoint_code;
  std::span<const Region> endpoint_payloads;
  std::span<const Selector> selectors;
  std::span<const VtableBinding> endpoint_vtables;
  std::span<const MfgGate> mfg_gates;
  std::span<const KernelProfile> quality_kernels;
  std::array<uint32_t, 3> backend_sms;
  std::array<uint32_t, 3> dynamic_streamline_version;
  uint32_t options_version;
  uint32_t state_version;
  uint32_t validated_generated_ceiling;
};

inline constexpr ProviderProfile kDlssg31091{
    "dlssg-310.9.1-ff6e90eb", "310.9.1",
    "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82",
    kTimestamp, kImageBytes, kRetargetPayloads, kEndpointCode, kEndpointPayloads,
    kEndpointSelectors, kEndpointVtables, kMfgGates, kQualityKernels, {89, 86, 75}, {2, 14, 1}, 5, 4, 5};
inline constexpr std::array<ProviderProfile, 1> kProfiles{{kDlssg31091}};

// This is discovery only. Callers still must match the exact relevant bytes,
// descriptors and successful target preparation before publishing any patch.
inline const ProviderProfile* Find(uint32_t timestamp, uint32_t image_size,
                                  std::span<const ProviderProfile> profiles = kProfiles) {
  for (const auto& profile : profiles)
    if (profile.timestamp == timestamp && profile.image_size == image_size) return &profile;
  return nullptr;
}
inline bool AllowsTarget(const ProviderProfile& profile, uint32_t sm) {
  return std::find(profile.backend_sms.begin(), profile.backend_sms.end(), sm) != profile.backend_sms.end();
}
}  // namespace mfgunlock::profiles
