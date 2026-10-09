/*
 * Validated Warp Blend provider patch.
 * SPDX-License-Identifier: MIT
 *
 * Cross-generation Validated Warp Blend path for the exact 310.9.1 provider.
 * The same qualified Blackwell PTX is rebuilt for native Ada sm_89 or the
 * Ampere/Turing sm_86/sm_75 backends; provider retargeting remains separate.
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "./fatbin.hpp"
#include "./provider.hpp"

namespace mfgunlock::validatedwarp {

enum class Mode : int {
  kRedirectControl = 0,
  kBlackwellBaseline = 1,
  kValidatedWarp = 2,
};

constexpr Mode ConfiguredMode(int value) {
  return value >= static_cast<int>(Mode::kRedirectControl) &&
                 value <= static_cast<int>(Mode::kValidatedWarp)
             ? static_cast<Mode>(value) : Mode::kValidatedWarp;
}

inline const char* ModeName(Mode mode) {
  switch (mode) {
    case Mode::kRedirectControl:
      return "redirect control";
    case Mode::kBlackwellBaseline:
      return "Blackwell baseline";
    case Mode::kValidatedWarp:
      return "Validated Warp";
    default:
      return "unknown";
  }
}

inline std::string ModeLabel(Mode mode, uint32_t target_sm) {
  if (mode == Mode::kRedirectControl) return ModeName(mode);
  return std::string(ModeName(mode)) + " sm_" + std::to_string(target_sm);
}

struct DescriptorPatch {
  uint64_t* slot = nullptr;
  uint64_t original = 0;
  DWORD protection = 0;
};

struct Redirect {
  std::vector<DescriptorPatch> descriptors;
  void* allocation = nullptr;
};

struct Result {
  bool detected = false;
  bool applied = false;
  std::string detail;
};

namespace internal {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAmpereArch = 86;
constexpr uint32_t kAdaArch = 89;
constexpr uint32_t kBlackwellArch = 120;
constexpr uint64_t kUncompressedFlags = 0x41;
constexpr size_t kMaxFatbinSize = 4u * 1024u * 1024u;

using ProviderProfile = profiles::ProviderProfile;
using PtxProfile = profiles::PtxProfile;
inline constexpr auto& kProviderProfiles = profiles::kProfiles;
inline constexpr auto& kPtxProfile = profiles::kWarpPtx;

inline uint16_t ReadU16(const uint8_t* bytes) {
  uint16_t value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

inline uint32_t ReadU32(const uint8_t* bytes) {
  uint32_t value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

inline uint64_t ReadU64(const uint8_t* bytes) {
  uint64_t value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

inline uint64_t Fnv1a64(const uint8_t* bytes, size_t size) {
  uint64_t value = 0xcbf29ce484222325ull;
  for (size_t index = 0; index < size; ++index) {
    value = (value ^ bytes[index]) * 0x100000001b3ull;
  }
  return value;
}

inline bool Lz4BlockDecompress(const uint8_t* source, size_t source_size,
                               uint8_t* destination, size_t destination_size) {
  size_t input = 0;
  size_t output = 0;
  while (input < source_size) {
    const uint8_t token = source[input++];
    size_t literals = token >> 4;
    if (literals == 15) {
      uint8_t extra = 0;
      do {
        if (input >= source_size) return false;
        extra = source[input++];
        literals += extra;
      } while (extra == 0xff);
    }
    if (literals > source_size - input || literals > destination_size - output) return false;
    std::memcpy(destination + output, source + input, literals);
    input += literals;
    output += literals;
    if (input == source_size) break;
    if (source_size - input < 2) return false;
    const size_t distance = source[input] | (static_cast<size_t>(source[input + 1]) << 8u);
    input += 2;
    if (distance == 0 || distance > output) return false;
    size_t match = 4 + (token & 0x0f);
    if ((token & 0x0f) == 15) {
      uint8_t extra = 0;
      do {
        if (input >= source_size) return false;
        extra = source[input++];
        match += extra;
      } while (extra == 0xff);
    }
    if (match > destination_size - output) return false;
    for (size_t index = 0; index < match; ++index) {
      destination[output + index] = destination[output + index - distance];
    }
    output += match;
  }
  return input == source_size && output == destination_size;
}

inline bool ReplaceOnce(std::string& text, const std::string& needle,
                        const std::string& replacement, std::string& why) {
  const size_t first = text.find(needle);
  if (first == std::string::npos) {
    why = "required PTX anchor is missing";
    return false;
  }
  if (text.find(needle, first + needle.size()) != std::string::npos) {
    why = "required PTX anchor is ambiguous";
    return false;
  }
  text.replace(first, needle.size(), replacement);
  return true;
}

inline bool RewriteValidatedWarpBlend(std::string& ptx, std::string& why) {
  if (ptx.find("MFGUNLOCK_VALIDATED_WARP_BLEND_V1") != std::string::npos) {
    why = "validated-warp PTX is already modified";
    return false;
  }
  constexpr char kRegisters[] = ".reg .pred %p<260>;\n";
  if (!ReplaceOnce(ptx, kRegisters,
                   std::string(kRegisters) + ".reg .pred %qv<7>;\n"
                                             ".reg .f32 %qf<12>;\n",
                   why)) {
    return false;
  }

  constexpr char kInsertion[] = "ld.param.u8 %rs8, [%rd6+220];\n";
  constexpr char kProgram[] = R"ptx(// MFGUNLOCK_VALIDATED_WARP_BLEND_V1
cvt.rn.f32.u32 %qf0, %r10;
cvt.rn.f32.u32 %qf1, %r11;
div.approx.ftz.f32 %qf0, 0f3F000000, %qf0;
div.approx.ftz.f32 %qf1, 0f3F000000, %qf1;
sub.ftz.f32 %qf2, 0f3F800000, %qf0;
sub.ftz.f32 %qf3, 0f3F800000, %qf1;
setp.ge.f32 %qv0, %f123, %qf0;
setp.le.f32 %qv2, %f123, %qf2;
and.pred %qv0, %qv0, %qv2;
setp.ge.f32 %qv2, %f124, %qf1;
and.pred %qv0, %qv0, %qv2;
setp.le.f32 %qv2, %f124, %qf3;
and.pred %qv0, %qv0, %qv2;
not.pred %qv2, %p17;
and.pred %qv0, %qv0, %qv2;
setp.ge.f32 %qv1, %f129, %qf0;
setp.le.f32 %qv2, %f129, %qf2;
and.pred %qv1, %qv1, %qv2;
setp.ge.f32 %qv2, %f130, %qf1;
and.pred %qv1, %qv1, %qv2;
setp.le.f32 %qv2, %f130, %qf3;
and.pred %qv1, %qv1, %qv2;
not.pred %qv2, %p16;
and.pred %qv1, %qv1, %qv2;
abs.f32 %qf4, %f125;
abs.f32 %qf5, %f126;
abs.f32 %qf6, %f127;
add.f32 %qf4, %qf4, %qf5;
add.f32 %qf4, %qf4, %qf6;
setp.lt.f32 %qv2, %qf4, 0f7F800000;
and.pred %qv0, %qv0, %qv2;
abs.f32 %qf5, %f131;
abs.f32 %qf6, %f132;
abs.f32 %qf7, %f133;
add.f32 %qf5, %qf5, %qf6;
add.f32 %qf5, %qf5, %qf7;
setp.lt.f32 %qv2, %qf5, 0f7F800000;
and.pred %qv1, %qv1, %qv2;
and.pred %qv3, %qv0, %qv1;
sub.f32 %qf6, %f115, %f119;
sub.f32 %qf7, %f116, %f120;
sub.f32 %qf8, %f117, %f121;
abs.f32 %qf6, %qf6;
abs.f32 %qf7, %qf7;
abs.f32 %qf8, %qf8;
add.f32 %qf6, %qf6, %qf7;
add.f32 %qf6, %qf6, %qf8;
sub.f32 %qf9, %f125, %f131;
sub.f32 %qf10, %f126, %f132;
sub.f32 %qf11, %f127, %f133;
abs.f32 %qf9, %qf9;
abs.f32 %qf10, %qf10;
abs.f32 %qf11, %qf11;
add.f32 %qf9, %qf9, %qf10;
add.f32 %qf9, %qf9, %qf11;
add.f32 %qf10, %qf9, 0f3DA3D70A;
setp.lt.f32 %qv4, %qf10, %qf6;
setp.lt.f32 %qv2, %qf9, 0f3E19999A;
and.pred %qv4, %qv4, %qv2;
and.pred %qv4, %qv4, %qv3;
setp.gt.f32 %qv2, %qf6, 0f3E800000;
setp.ge.f32 %qv5, %f148, 0f3E4CCCCD;
and.pred %qv5, %qv5, %qv2;
or.pred %qv5, %qv5, %qv4;
and.pred %qv0, %qv0, %qv5;
setp.ge.f32 %qv6, %f149, 0f3E4CCCCD;
and.pred %qv6, %qv6, %qv2;
or.pred %qv6, %qv6, %qv4;
and.pred %qv1, %qv1, %qv6;
max.f32 %qf0, %f148, 0f3F59999A;
min.f32 %qf0, %qf0, 0f3F800000;
max.f32 %qf1, %f149, 0f3F59999A;
min.f32 %qf1, %qf1, 0f3F800000;
sub.f32 %qf2, %f125, %f115;
sub.f32 %qf3, %f126, %f116;
sub.f32 %qf4, %f127, %f117;
@%qv0 fma.rn.f32 %f39, %qf0, %qf2, %f115;
@%qv0 fma.rn.f32 %f38, %qf0, %qf3, %f116;
@%qv0 fma.rn.f32 %f37, %qf0, %qf4, %f117;
sub.f32 %qf2, %f131, %f119;
sub.f32 %qf3, %f132, %f120;
sub.f32 %qf4, %f133, %f121;
@%qv1 fma.rn.f32 %f43, %qf1, %qf2, %f119;
@%qv1 fma.rn.f32 %f42, %qf1, %qf3, %f120;
@%qv1 fma.rn.f32 %f41, %qf1, %qf4, %f121;
)ptx";
  return ReplaceOnce(ptx, kInsertion, std::string(kProgram) + kInsertion, why);
}

inline const ProviderProfile* MatchProvider(const provider::internal::ImageIdentity& identity) {
  for (const auto& profile : kProviderProfiles) {
    if (identity.timestamp == profile.timestamp &&
        identity.image_bytes == profile.image_size) {
      return &profile;
    }
  }
  return nullptr;
}

inline bool FindPtxEntry(const uint8_t* fatbin, size_t fatbin_size,
                         const PtxProfile& profile, size_t& entry_offset,
                         std::vector<uint8_t>& ptx, std::string& why) {
  if (fatbin_size < kOuterHeader || ReadU32(fatbin) != kFatbinMagic ||
      ReadU16(fatbin + 6) != kOuterHeader ||
      ReadU64(fatbin + 8) + kOuterHeader != fatbin_size) {
    why = "invalid fatbin header";
    return false;
  }
  size_t cursor = kOuterHeader;
  while (cursor + 64 <= fatbin_size) {
    const uint32_t header = ReadU32(fatbin + cursor + 4);
    const uint64_t payload = ReadU64(fatbin + cursor + 8);
    if (header < 64 || payload == 0 || cursor + header + payload > fatbin_size) {
      why = "invalid fatbin entry";
      return false;
    }
    if (ReadU16(fatbin + cursor) == kPtxKind &&
        ReadU32(fatbin + cursor + 28) == profile.arch) {
      const uint32_t compressed = ReadU32(fatbin + cursor + 16);
      const uint64_t raw = ReadU64(fatbin + cursor + 56);
      if ((profile.declared_raw_size != 0 && raw != profile.declared_raw_size) ||
          compressed == 0 || compressed > payload || raw > (8u << 20)) {
        why = "PTX size/compression does not match the supported profile";
        return false;
      }
      ptx.resize(static_cast<size_t>(raw));
      if (!Lz4BlockDecompress(fatbin + cursor + header, compressed,
                              ptx.data(), ptx.size())) {
        why = "PTX LZ4 decompression failed";
        return false;
      }
      ptx.erase(std::remove(ptx.begin(), ptx.end(), '\r'), ptx.end());
      while (!ptx.empty() && ptx.back() == 0) ptx.pop_back();
      if (ptx.size() != profile.normalized_size ||
          Fnv1a64(ptx.data(), ptx.size()) != profile.raw_fnv1a64) {
        why = "PTX identity hash does not match the supported profile";
        return false;
      }
      const std::string text(reinterpret_cast<const char*>(ptx.data()), ptx.size());
      const std::string entry = std::string(".entry ") + profile.entry_name + "(";
      if (text.find(entry) == std::string::npos ||
          text.find(profile.parameter_signature) == std::string::npos) {
        why = "PTX entry or parameter layout changed";
        return false;
      }
      entry_offset = cursor;
      return true;
    }
    cursor += header + static_cast<size_t>(payload);
  }
  why = "required PTX architecture entry is missing";
  return false;
}

inline bool ValidatePristineLayout(const uint8_t* fatbin, size_t fatbin_size,
                                   size_t target_entry, std::string& why) {
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  if (!fatbin::Parse(std::span<const unsigned char>(fatbin, fatbin_size), entries, end) ||
      end != fatbin_size || entries.size() != 3 ||
      entries[0].offset != target_entry || entries[0].kind != kPtxKind ||
      entries[0].architecture != kBlackwellArch ||
      entries[1].kind != kPtxKind || entries[1].architecture != kAdaArch ||
      entries[2].kind != 2 || entries[2].architecture != kAdaArch) {
    why = "provider fatbin was already retargeted or selectable image layout changed";
    return false;
  }
  return true;
}

using RewritePtxCallback = bool (*)(std::string&, std::string&);

inline bool BuildRetargetedFatbin(const uint8_t* fatbin, size_t fatbin_size,
                                   const PtxProfile& profile, RewritePtxCallback rewrite,
                                   std::vector<uint8_t>& rebuilt,
                                   std::string& why, uint32_t target_sm = kAmpereArch) {
  if (target_sm != 75 && target_sm != kAmpereArch && target_sm != kAdaArch) {
    why = "unsupported quality PTX target";
    return false;
  }
  size_t entry = 0;
  std::vector<uint8_t> source;
  if (!FindPtxEntry(fatbin, fatbin_size, profile, entry, source, why)) return false;
  // Build from the pristine 310.9.1 image set before provider retargeting, so
  // the replacement has one selectable sm_86 program instead of inheriting a
  // second retargeted sm_86 PTX image.
  if (!ValidatePristineLayout(fatbin, fatbin_size, entry, why)) return false;
  std::string ptx(reinterpret_cast<const char*>(source.data()), source.size());
  if (rewrite != nullptr && !rewrite(ptx, why)) return false;
  size_t packed_count = 0;
  if (profile.raw_fnv1a64 == 0x9b47635b91b2436bull && profile.normalized_size == 23116u)
    packed_count = 8;
  if (profile.raw_fnv1a64 == kPtxProfile.raw_fnv1a64 &&
      profile.normalized_size == kPtxProfile.normalized_size) packed_count = 20;
  if (!ptx::LowerPackedHalf(ptx, target_sm, packed_count, why)) return false;
  if (!ReplaceOnce(ptx, ".target sm_120", ".target sm_" + std::to_string(target_sm), why))
    return false;

  const uint32_t header = ReadU32(fatbin + entry + 4);
  const uint64_t original_payload64 = ReadU64(fatbin + entry + 8);
  if (original_payload64 > fatbin_size - entry - header) {
    why = "target PTX entry extends beyond its fatbin";
    return false;
  }
  const size_t original_payload = static_cast<size_t>(original_payload64);
  const size_t padded = (ptx.size() + 7u) & ~size_t{7u};
  const size_t suffix_offset = entry + header + original_payload;
  if (padded > std::numeric_limits<size_t>::max() -
                   (fatbin_size - original_payload)) {
    why = "replacement fatbin size overflow";
    return false;
  }
  const size_t final_size = fatbin_size - original_payload + padded;
  rebuilt.assign(fatbin, fatbin + entry + header);
  rebuilt.resize(final_size, 0);
  std::memcpy(rebuilt.data() + entry + header, ptx.data(), ptx.size());
  // Preserve every entry after the replaced PTX payload. Some fatbins place
  // architecture-specific cubins or metadata after the source entry; dropping
  // that suffix can make a provider fail only when CUDA consumes it at startup.
  std::memcpy(rebuilt.data() + entry + header + padded,
              fatbin + suffix_offset, fatbin_size - suffix_offset);
  const uint64_t payload = padded;
  const uint32_t zero32 = 0;
  const uint64_t zero64 = 0;
  std::memcpy(rebuilt.data() + entry + 8, &payload, sizeof(payload));
  std::memcpy(rebuilt.data() + entry + 16, &zero32, sizeof(zero32));
  std::memcpy(rebuilt.data() + entry + 28, &target_sm, sizeof(target_sm));
  std::memcpy(rebuilt.data() + entry + 40, &kUncompressedFlags,
              sizeof(kUncompressedFlags));
  std::memcpy(rebuilt.data() + entry + 56, &zero64, sizeof(zero64));
  const uint64_t outer = final_size - kOuterHeader;
  std::memcpy(rebuilt.data() + 8, &outer, sizeof(outer));
  return true;
}

struct LocatedFatbin {
  const uint8_t* address = nullptr;
  size_t size = 0;
};

inline bool BuildRedirectControlFatbin(const LocatedFatbin& located,
                                       std::vector<uint8_t>& rebuilt,
                                       std::string& why,
                                       uint32_t target_sm = kAmpereArch) {
  if (located.address == nullptr || located.size < kOuterHeader ||
      ReadU32(located.address) != kFatbinMagic ||
      ReadU16(located.address + 6) != kOuterHeader) {
    why = "prepared control fatbin header is invalid";
    return false;
  }
  const uint64_t declared = ReadU64(located.address + 8);
  if (declared == 0 || declared > located.size - kOuterHeader) {
    why = "prepared control fatbin size is invalid";
    return false;
  }
  const size_t size = static_cast<size_t>(declared) + kOuterHeader;
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  if ((target_sm != 75 && target_sm != kAmpereArch) ||
      !fatbin::Parse(std::span<const unsigned char>(located.address, size), entries, end) ||
      end != size || entries.size() != 2 ||
      entries[0].kind != kPtxKind || entries[0].architecture != kBlackwellArch ||
      entries[1].kind != kPtxKind || entries[1].architecture != target_sm) {
    why = "prepared control fatbin does not match the normal retargeted layout";
    return false;
  }
  // Match the provider's physical backing span as well as its visible header.
  // The normal retargeted path hides the trailing sm_89 cubin by shortening
  // the outer header; keeping those bytes makes pointer relocation the only change.
  rebuilt.assign(located.address, located.address + located.size);
  return true;
}

inline std::string DescribeFatbin(const std::vector<uint8_t>& bytes) {
  if (bytes.size() < kOuterHeader || ReadU32(bytes.data()) != kFatbinMagic)
    return "entries=invalid";
  const uint64_t declared = ReadU64(bytes.data() + 8);
  if (declared > bytes.size() - kOuterHeader) return "entries=invalid";
  const size_t visible = static_cast<size_t>(declared) + kOuterHeader;
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  if (!fatbin::Parse(std::span<const unsigned char>(bytes.data(), visible), entries, end) ||
      end != visible) {
    return "entries=invalid";
  }
  std::ostringstream stream;
  stream << "visible=" << visible << '/' << bytes.size() << "; entries=[";
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index != 0) stream << ',';
    stream << (entries[index].kind == kPtxKind ? "ptx" : "cubin")
           << "/sm_" << entries[index].architecture
           << '/' << entries[index].payload_bytes;
  }
  stream << ']';
  return stream.str();
}

inline bool LocateUniqueFatbin(HMODULE module, const PtxProfile& profile,
                               LocatedFatbin& located, std::string& why) {
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const size_t image_size = nt->OptionalHeader.SizeOfImage;
  size_t matches = 0;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 ||
        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 ||
        section->VirtualAddress >= image_size) {
      continue;
    }
    const size_t section_size = std::min<size_t>(
        section->Misc.VirtualSize, image_size - section->VirtualAddress);
    const uint8_t* bytes = base + section->VirtualAddress;
    for (size_t offset = 0; offset + kOuterHeader <= section_size; ++offset) {
      if (ReadU32(bytes + offset) != kFatbinMagic ||
          ReadU16(bytes + offset + 6) != kOuterHeader) {
        continue;
      }
      const uint64_t declared = ReadU64(bytes + offset + 8);
      if (declared == 0 || declared > kMaxFatbinSize ||
          declared > section_size - offset - kOuterHeader) {
        continue;
      }
      const size_t total = static_cast<size_t>(declared) + kOuterHeader;
      size_t ignored_entry = 0;
      std::vector<uint8_t> ignored_ptx;
      std::string ignored_reason;
      if (!FindPtxEntry(bytes + offset, total, profile, ignored_entry,
                        ignored_ptx, ignored_reason)) {
        continue;
      }
      located = {bytes + offset, total};
      ++matches;
      offset += total - 1;
    }
  }
  if (matches != 1) {
    std::ostringstream stream;
    stream << "found " << matches << " exact PTX fatbins, expected one";
    why = stream.str();
    located = {};
    return false;
  }
  return true;
}

using PrepareProviderCallback = bool (*)(HMODULE);

struct PreparedRedirect {
  HMODULE module = nullptr;
  LocatedFatbin located;
  std::vector<uint64_t*> slots;
  std::vector<uint8_t> rebuilt;
  uint64_t expected = 0;
  uint64_t source_hash = 0;
  size_t source_rva = 0;
};

inline bool PrepareRedirectSource(HMODULE module, const PtxProfile& profile,
                                  PreparedRedirect& prepared, std::string& detail) {
  prepared = {};
  prepared.module = module;
  if (!LocateUniqueFatbin(module, profile, prepared.located, detail)) return false;

  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const size_t image_size = nt->OptionalHeader.SizeOfImage;
  prepared.expected = reinterpret_cast<uint64_t>(prepared.located.address);
  prepared.source_hash = Fnv1a64(prepared.located.address, prepared.located.size);
  prepared.source_rva = static_cast<size_t>(prepared.located.address - base);

  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 ||
        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 ||
        section->VirtualAddress >= image_size) {
      continue;
    }
    const size_t section_size = std::min<size_t>(
        section->Misc.VirtualSize, image_size - section->VirtualAddress);
    uint8_t* bytes = base + section->VirtualAddress;
    for (size_t offset = 0; offset + sizeof(uint64_t) <= section_size;
         offset += alignof(uint64_t)) {
      uint64_t value = 0;
      std::memcpy(&value, bytes + offset, sizeof(value));
      if (value == prepared.expected)
        prepared.slots.push_back(reinterpret_cast<uint64_t*>(bytes + offset));
    }
  }
  if (prepared.slots.size() != profile.descriptor_references) {
    std::ostringstream stream;
    stream << "found " << prepared.slots.size() << " descriptor reference(s), expected "
           << profile.descriptor_references;
    detail = stream.str();
    prepared = {};
    return false;
  }
  return true;
}

inline bool PrepareRetargetedRedirect(HMODULE module, const PtxProfile& profile,
                                      RewritePtxCallback rewrite,
                                      PreparedRedirect& prepared,
                                      std::string& detail, uint32_t target_sm = kAmpereArch) {
  if (!PrepareRedirectSource(module, profile, prepared, detail)) return false;
  if (!BuildRetargetedFatbin(prepared.located.address, prepared.located.size,
                             profile, rewrite, prepared.rebuilt, detail, target_sm)) {
    prepared = {};
    return false;
  }
  return true;
}

// A rollback may restore only our replacement (or an already restored slot).
// Failed ownership checks keep the backing allocation and undo metadata alive.
inline bool RestoreDescriptor(const DescriptorPatch& patch, uint64_t replacement) {
  std::atomic_ref<uint64_t> slot(*patch.slot);
  const auto current = slot.load();
  if (current != replacement && current != patch.original) return false;
  DWORD old_protection = 0;
  if (!VirtualProtect(patch.slot, sizeof(uint64_t), PAGE_READWRITE, &old_protection)) return false;
  auto expected = replacement;
  const bool owned = slot.compare_exchange_strong(expected, patch.original) || expected == patch.original;
  DWORD ignored = 0;
  const DWORD protection = patch.protection != 0 ? patch.protection : old_protection;
  const bool protected_again = VirtualProtect(patch.slot, sizeof(uint64_t), protection, &ignored) != FALSE;
  return owned && protected_again && slot.load() == patch.original;
}

inline bool CommitPreparedRedirect(PreparedRedirect& prepared,
                                   const std::string& label, Redirect& redirect,
                                   std::string& detail) {
  if (prepared.module == nullptr || prepared.located.address == nullptr ||
      prepared.rebuilt.empty() || prepared.slots.empty()) {
    detail = "prepared redirect is incomplete";
    return false;
  }

  redirect.descriptors.reserve(prepared.slots.size());
  void* allocation = VirtualAlloc(nullptr, prepared.rebuilt.size(),
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (allocation == nullptr) {
    detail = "replacement fatbin allocation failed";
    return false;
  }
  std::memcpy(allocation, prepared.rebuilt.data(), prepared.rebuilt.size());

  const uint64_t replacement = reinterpret_cast<uint64_t>(allocation);
  bool committed = true;
  for (uint64_t* slot : prepared.slots) {
    if (*slot != prepared.expected) {
      committed = false;
      break;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(slot, sizeof(uint64_t), PAGE_READWRITE, &old_protection)) {
      committed = false;
      break;
    }
    redirect.descriptors.push_back({slot, prepared.expected, old_protection});
    auto expected = prepared.expected;
    const bool owned = std::atomic_ref<uint64_t>(*slot).compare_exchange_strong(expected, replacement);
    DWORD ignored = 0;
    const bool protected_again = VirtualProtect(slot, sizeof(uint64_t), old_protection, &ignored) != FALSE;
    if (!owned || !protected_again || std::atomic_ref<uint64_t>(*slot).load() != replacement) {
      committed = false;
      break;
    }
  }
  if (!committed || redirect.descriptors.size() != prepared.slots.size()) {
    bool rolled_back = true;
    for (auto patch = redirect.descriptors.rbegin();
         patch != redirect.descriptors.rend(); ++patch) {
      if (!RestoreDescriptor(*patch, replacement)) rolled_back = false;
    }
    if (rolled_back) {
      rolled_back = VirtualFree(allocation, 0, MEM_RELEASE) != FALSE;
      if (rolled_back) redirect.descriptors.clear();
    }
    if (!rolled_back) redirect.allocation = allocation;
    detail = rolled_back
        ? "descriptor redirect transaction failed"
        : "descriptor redirect transaction failed; replacement retained after incomplete rollback";
    return false;
  }

  redirect.allocation = allocation;
  auto* base = reinterpret_cast<uint8_t*>(prepared.module);
  std::ostringstream stream;
  stream << label
         << "; source-rva=0x" << std::hex << prepared.source_rva
         << "; source-hash=0x" << prepared.source_hash
         << "; replacement-hash=0x" << Fnv1a64(prepared.rebuilt.data(), prepared.rebuilt.size())
         << std::dec << "; redirected " << redirect.descriptors.size()
         << " descriptor(s), " << prepared.located.size << " -> " << prepared.rebuilt.size()
         << " bytes; " << DescribeFatbin(prepared.rebuilt) << "; descriptor-rva=[";
  for (size_t index = 0; index < prepared.slots.size(); ++index) {
    if (index != 0) stream << ',';
    stream << "0x" << std::hex
           << static_cast<size_t>(reinterpret_cast<uint8_t*>(prepared.slots[index]) - base);
  }
  stream << ']';
  detail = stream.str();
  return true;
}

inline bool RedirectFatbin(HMODULE module, const PtxProfile& profile, Mode mode,
                           PrepareProviderCallback prepare_provider, Redirect& redirect,
                           std::string& detail, uint32_t target_sm = kAmpereArch) {
  PreparedRedirect prepared;
  if (!PrepareRedirectSource(module, profile, prepared, detail)) return false;

  if (mode == Mode::kBlackwellBaseline || mode == Mode::kValidatedWarp) {
    const RewritePtxCallback rewrite =
        mode == Mode::kValidatedWarp ? RewriteValidatedWarpBlend : nullptr;
    if (!BuildRetargetedFatbin(prepared.located.address, prepared.located.size,
                               profile, rewrite, prepared.rebuilt, detail, target_sm)) {
      return false;
    }
  } else if (mode != Mode::kRedirectControl) {
    detail = "unknown warp diagnostic mode";
    return false;
  }

  // Provider preparation must complete before replacement pointers are
  // published. Redirect-control intentionally copies the resulting normal
  // retargeted fatbin byte-for-byte, isolating the redirect/allocation mechanism.
  if (prepare_provider != nullptr && !prepare_provider(module)) {
    detail = "provider preparation failed before warp redirect";
    return false;
  }
  if (mode == Mode::kRedirectControl &&
      !BuildRedirectControlFatbin(prepared.located, prepared.rebuilt, detail, target_sm)) {
    return false;
  }

  return CommitPreparedRedirect(
      prepared, std::string("mode=") + ModeLabel(mode, target_sm), redirect, detail);
}

}  // namespace internal

inline bool IsSupportedProvider(HMODULE module, std::string& version,
                                std::string& why) {
  provider::internal::ImageIdentity identity;
  provider::internal::Image image;
  if (!provider::internal::ReadIdentity(module, identity) ||
      !provider::internal::InspectImage(module, image)) {
    why = "provider is not a readable x64 image";
    return false;
  }
  const auto* profile = internal::MatchProvider(identity);
  if (profile == nullptr) {
    std::ostringstream stream;
    stream << "unsupported provider metadata (timestamp=0x" << std::hex
           << identity.timestamp << std::dec
           << ", image-size=" << identity.image_bytes << ')';
    why = stream.str();
    return false;
  }
  version = profile->version;
  return true;
}

inline bool Restore(std::vector<Redirect>& redirects) {
  bool restored_all = true;
  for (auto redirect = redirects.rbegin(); redirect != redirects.rend(); ++redirect) {
    bool restored = true;
    for (auto patch = redirect->descriptors.rbegin();
         patch != redirect->descriptors.rend(); ++patch) {
      if (!internal::RestoreDescriptor(*patch, reinterpret_cast<uint64_t>(redirect->allocation)))
        restored = false;
    }
    // Keep rollback metadata and storage until every descriptor is restored.
    if (restored && redirect->allocation != nullptr) {
      restored = VirtualFree(redirect->allocation, 0, MEM_RELEASE) != FALSE;
      if (restored) redirect->allocation = nullptr;
    }
    if (restored) redirect->descriptors.clear();
    restored_all = restored_all && restored;
  }
  std::erase_if(redirects, [](const Redirect& redirect) {
    return redirect.allocation == nullptr && redirect.descriptors.empty();
  });
  return restored_all;
}

inline bool Apply(HMODULE module, std::vector<Redirect>& redirects, Result& result,
                  std::string& provider_version, Mode mode = Mode::kValidatedWarp,
                  internal::PrepareProviderCallback prepare_provider = nullptr,
                  uint32_t target_sm = internal::kAmpereArch) {
  redirects.clear();
  result = {};

  if (!IsSupportedProvider(module, provider_version, result.detail)) return false;

  internal::LocatedFatbin located;
  std::string detection_reason;
  result.detected = internal::LocateUniqueFatbin(
      module, internal::kPtxProfile, located, detection_reason);
  if (!result.detected) {
    result.detail = detection_reason;
    return false;
  }

  Redirect redirect;
  if (!internal::RedirectFatbin(module, internal::kPtxProfile, mode, prepare_provider,
                                redirect, result.detail, target_sm)) {
    if (redirect.allocation != nullptr) redirects.push_back(std::move(redirect));
    return false;
  }
  result.applied = true;
  redirects.push_back(std::move(redirect));
  return true;
}

}  // namespace mfgunlock::validatedwarp
