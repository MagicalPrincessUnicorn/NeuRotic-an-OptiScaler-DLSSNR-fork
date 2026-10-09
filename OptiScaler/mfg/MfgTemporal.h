// Adapted from MFGAdaUnlock-RenoDx 1.1.5 midpoint.hpp (MIT).
// Copyright (c) 2026 ImDreamt. See THIRD_PARTY_MFGAdaUnlock_LICENSE.
// Temporal midpoint technique credited to dashdogy's RTX40MFG-Unlock.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace Neurotic::Mfg::Temporal {

namespace internal {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAdaArch = 89;
constexpr uint64_t kUncompressedFlags = 0x41;

// Structural expectations. NVIDIA renamed every kernel in 310.9, but the D157
// temporal program itself is otherwise unchanged: the PTX grew by exactly the
// accumulated symbol-name delta. Keep separate, exact profiles so an unrelated
// provider cannot be admitted merely because it happens to contain a similar
// midpoint sequence.
struct TemporalProfile {
  size_t ptx_bytes;
  const char* entry_name;
  const char* descriptor_name;
  ptrdiff_t entry_name_offset;
  ptrdiff_t descriptor_name_offset;
  uint64_t ptx_fingerprint = 0;
  size_t descriptor_count = 8;
};

constexpr TemporalProfile kTemporalProfiles[] = {
    {99362, "main_kernel", "dlfg_kernel", 0x10, 0x28},
    {99626, "Kernel_EstimateIntermMvecsScatter", "EstimateIntermMvecsScatter", 0x10, -0x08},
    // NVIDIA SDK 2.7.32 / DLSS-G 310.2.1. The sibling sm_120 program
    // establishes param_5 as the time fraction and uses 1-t for its first
    // 52 motion multiplies, t for its final 52. sm_89 hardcodes 0.5 there.
    {210951, "main_kernel", "dlfg_kernel", 0x10, -0x08, 0x3a163969edd65291ull, 7},
};

constexpr size_t kExpectedMidpoints = 104;

constexpr char kJoinLabel[] = "$L__BB0_3:";
constexpr char kMidpointBits[] = "0f3F000000";
constexpr char kMulPrefix[] = "mul.ftz.f32 ";
constexpr char kCurrToPrev[] = "%f136";
constexpr char kPrevToCurr[] = "%f134";
inline uint16_t ReadU16(const uint8_t* p) {
  uint16_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}
inline uint32_t ReadU32(const uint8_t* p) {
  uint32_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}
inline uint64_t ReadU64(const uint8_t* p) {
  uint64_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

// Plain LZ4 block format: token high nibble = literal run, low nibble = match
// length - 4, 16-bit little-endian back offset, 0xFF continuation bytes.
inline bool Lz4BlockDecompress(const uint8_t* src, size_t src_size, uint8_t* dst,
                               size_t dst_size) {
  size_t in = 0;
  size_t out = 0;
  while (in < src_size) {
    const uint8_t token = src[in++];
    size_t literals = token >> 4;
    if (literals == 15) {
      uint8_t ext = 0;
      do {
        if (in >= src_size) return false;
        ext = src[in++];
        literals += ext;
      } while (ext == 0xFF);
    }
    if (literals > src_size - in || literals > dst_size - out) return false;
    std::memcpy(dst + out, src + in, literals);
    in += literals;
    out += literals;
    if (in == src_size) break;
    if (src_size - in < 2) return false;
    const size_t back = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
    in += 2;
    if (back == 0 || back > out) return false;
    size_t match = 4 + (token & 0x0F);
    if ((token & 0x0F) == 15) {
      uint8_t ext = 0;
      do {
        if (in >= src_size) return false;
        ext = src[in++];
        match += ext;
      } while (ext == 0xFF);
    }
    if (match > dst_size - out) return false;
    for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
    out += match;
  }
  return in == src_size && out == dst_size;
}

// Locates the sm_89 PTX entry inside a fatbin by walking its entry list rather
// than trusting fixed offsets.
inline bool FindAdaPtxEntry(const uint8_t* fat, size_t fat_size, size_t& entry_offset) {
  if (fat == nullptr || fat_size < kOuterHeader || ReadU32(fat) != kFatbinMagic) return false;
  if (ReadU16(fat + 6) != kOuterHeader) return false;
  const uint64_t declared = ReadU64(fat + 8);
  if (declared != fat_size - kOuterHeader) return false;

  size_t p = kOuterHeader;
  while (p <= fat_size && fat_size - p >= 64) {
    const uint32_t kind = ReadU16(fat + p);
    const uint32_t hdr = ReadU32(fat + p + 4);
    const uint64_t payload = ReadU64(fat + p + 8);
    if (hdr < 64 || payload == 0) return false;
    if (hdr > fat_size - p || payload > fat_size - p - hdr) return false;
    if (kind == kPtxKind && ReadU32(fat + p + 28) == kAdaArch) {
      entry_offset = p;
      return true;
    }
    p += hdr + static_cast<size_t>(payload);
  }
  return false;
}

// Decompress the Ada PTX, rewrite the blend weights, and re-emit a truncated
// fatbin that ends after it.
inline bool BuildTemporalFatbin(const uint8_t* fat, size_t fat_size,
                                const TemporalProfile& profile,
                                std::vector<uint8_t>& out, std::string& why) {
  size_t entry = 0;
  if (!FindAdaPtxEntry(fat, fat_size, entry)) {
    why = "no sm_89 PTX entry";
    return false;
  }

  const uint32_t hdr = ReadU32(fat + entry + 4);
  const uint32_t compressed = ReadU32(fat + entry + 16);
  const uint64_t raw = ReadU64(fat + entry + 56);
  if (compressed == 0 || raw == 0 || raw > (8u << 20)) {
    why = "PTX entry is not compressed as expected";
    return false;
  }
  if (raw != profile.ptx_bytes) {
    std::stringstream s;
    s << "PTX is " << raw << " bytes, expected " << profile.ptx_bytes;
    why = s.str();
    return false;
  }
  if (hdr > fat_size - entry || compressed > fat_size - entry - hdr ||
      compressed > ReadU64(fat + entry + 8)) {
    why = "compressed PTX exceeds its fatbin entry";
    return false;
  }

  std::vector<uint8_t> ptx(static_cast<size_t>(raw));
  if (!Lz4BlockDecompress(fat + entry + hdr, compressed, ptx.data(), ptx.size())) {
    why = "LZ4 decompression failed";
    return false;
  }

  const bool legacy = profile.ptx_fingerprint != 0;
  if (legacy) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : ptx) { hash ^= byte; hash *= 1099511628211ull; }
    if (hash != profile.ptx_fingerprint) { why = "legacy temporal program changed"; return false; }
  }

  const std::string entry_signature = std::string(".entry ") + profile.entry_name + "(";
  const std::string parameter_name = std::string(profile.entry_name) + "_param_0";
  const std::string parameter_signature =
      legacy ? ".param .f32 main_kernel_param_5," :
      std::string(".param .align 8 .b8 ") + parameter_name + "[144]";
  const char* registers = legacy ? ".reg .f32 %f<4010>;" : ".reg .f32 %f<1362>;";
  const std::string ptx_text(reinterpret_cast<const char*>(ptx.data()), ptx.size());
  if (ptx_text.find(entry_signature) == std::string::npos ||
      ptx_text.find(parameter_signature) == std::string::npos ||
      ptx_text.find(registers) == std::string::npos) {
    why = "temporal kernel signature changed";
    return false;
  }

  // The join label must be unique, or we are not looking at the kernel we think.
  const char* begin = reinterpret_cast<const char*>(ptx.data());
  const size_t n = ptx.size();
  const size_t label_len = sizeof(kJoinLabel) - 1;
  size_t label = SIZE_MAX;
  for (size_t i = 0; i + label_len <= n; ++i) {
    if (std::memcmp(begin + i, kJoinLabel, label_len) != 0) continue;
    if (label != SIZE_MAX) {
      why = "join label is not unique";
      return false;
    }
    label = i;
  }
  if (label == SIZE_MAX) {
    why = "join label not found";
    return false;
  }
  size_t insertion = label + label_len;
  while (insertion < n && begin[insertion] != '\n') ++insertion;
  if (insertion >= n) {
    why = "join label has no line end";
    return false;
  }
  ++insertion;

  // Every midpoint constant that terminates a mul.ftz.f32 line.
  const size_t mid_len = sizeof(kMidpointBits) - 1;
  const size_t mul_len = sizeof(kMulPrefix) - 1;
  std::vector<size_t> marks;
  marks.reserve(kExpectedMidpoints);
  for (size_t i = 0; i + mid_len < n; ++i) {
    if (std::memcmp(begin + i, kMidpointBits, mid_len) != 0) continue;
    if (begin[i + mid_len] != ';') continue;
    size_t line = i;
    while (line > 0 && begin[line - 1] != '\n') --line;
    if (i - line < mul_len) continue;
    if (std::memcmp(begin + line, kMulPrefix, mul_len) != 0) continue;
    marks.push_back(i);
  }
  if (marks.size() != kExpectedMidpoints) {
    std::stringstream s;
    s << "found " << marks.size() << " midpoint multiplies, expected " << kExpectedMidpoints;
    why = s.str();
    return false;
  }
  if (marks.front() <= insertion) {
    why = "first midpoint precedes the injection point";
    return false;
  }

  const std::string temporal_input = legacy ?
      "ld.param.f32 %nrTime, [main_kernel_param_5];\r\n"
      "mov.f32 %nrOneMinusTime, 0f3F800000;\r\n"
      "sub.ftz.f32 %nrOneMinusTime, %nrOneMinusTime, %nrTime;\r\n" :
      "ld.param.f32 %f134, [" + parameter_name + "+32];\r\n"
      "mov.f32 %f135, 0f3F800000;\r\n"
      "sub.ftz.f32 %f136, %f135, %f134;\r\n";

  std::vector<uint8_t> patched;
  patched.reserve(n + temporal_input.size());
  auto append = [&patched](const void* p, size_t bytes) {
    const auto* b = static_cast<const uint8_t*>(p);
    patched.insert(patched.end(), b, b + bytes);
  };
  if (legacy) {
    // Separate names avoid aliasing the legacy program's live %f registers.
    const size_t declarations = ptx_text.find(registers) + std::strlen(registers);
    if (declarations >= insertion) { why = "legacy declarations follow time injection"; return false; }
    append(ptx.data(), declarations);
    const char extra[] = "\r\n.reg .f32 %nrTime, %nrOneMinusTime;\r\n";
    append(extra, sizeof(extra) - 1);
    append(ptx.data() + declarations, insertion - declarations);
  } else append(ptx.data(), insertion);
  append(temporal_input.data(), temporal_input.size());
  size_t src = insertion;
  const size_t half = kExpectedMidpoints / 2;
  for (size_t i = 0; i < marks.size(); ++i) {
    append(ptx.data() + src, marks[i] - src);
    const char* scale = legacy ? (i < half ? "%nrOneMinusTime" : "%nrTime") :
        (i < half ? kCurrToPrev : kPrevToCurr);
    append(scale, std::strlen(scale));
    src = marks[i] + mid_len;
  }
  append(ptx.data() + src, n - src);

  const size_t padded = (patched.size() + 7) & ~size_t{7};
  const size_t final_size = entry + hdr + padded;

  out.assign(fat, fat + entry + hdr);
  out.resize(final_size, 0);
  std::memcpy(out.data() + entry + hdr, patched.data(), patched.size());

  const uint64_t payload64 = padded;
  const uint32_t zero32 = 0;
  const uint64_t zero64 = 0;
  std::memcpy(out.data() + entry + 8, &payload64, sizeof(payload64));
  std::memcpy(out.data() + entry + 16, &zero32, sizeof(zero32));
  std::memcpy(out.data() + entry + 40, &kUncompressedFlags, sizeof(kUncompressedFlags));
  std::memcpy(out.data() + entry + 56, &zero64, sizeof(zero64));
  const uint64_t outer = final_size - kOuterHeader;
  std::memcpy(out.data() + 8, &outer, sizeof(outer));
  return true;
}

// Kernel names confirm that a slot belongs to the expected descriptor family,
// but do not uniquely identify its temporal program. Identify that program by
// the exact sm_89 PTX size as well, then validate its internal signatures before
// rebuilding it.
inline const TemporalProfile* FindTemporalProfile(const uint8_t* fat, size_t fat_size) {
  size_t entry = 0;
  if (!FindAdaPtxEntry(fat, fat_size, entry)) return nullptr;
  const uint64_t raw = ReadU64(fat + entry + 56);
  for (const auto& profile : kTemporalProfiles) {
    if (raw == profile.ptx_bytes) return &profile;
  }
  return nullptr;
}

inline bool PointsToCString(const uint8_t* base, size_t image_size, uint64_t value,
                            const char* expected) {
  const auto start = reinterpret_cast<uintptr_t>(base);
  if (value < start || value >= start + image_size) return false;
  const char* s = reinterpret_cast<const char*>(value);
  const size_t len = std::strlen(expected);
  if (value + len + 1 > start + image_size) return false;
  return std::memcmp(s, expected, len + 1) == 0;
}

inline bool ReadRelativePointer(const uint8_t* base, size_t image_size, const uint8_t* slot,
                                ptrdiff_t displacement, uint64_t& value) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(base);
  const uintptr_t end = start + image_size;
  const uintptr_t address = reinterpret_cast<uintptr_t>(slot);
  uintptr_t field = address;
  if (displacement >= 0) {
    const auto distance = static_cast<uintptr_t>(displacement);
    if (distance > UINTPTR_MAX - address) return false;
    field = address + distance;
  } else {
    const auto distance = static_cast<uintptr_t>(-(displacement + 1)) + 1;
    if (distance > address) return false;
    field = address - distance;
  }
  if (field < start || field > end || sizeof(value) > end - field) return false;
  std::memcpy(&value, reinterpret_cast<const void*>(field), sizeof(value));
  return true;
}

}  // namespace internal

}
