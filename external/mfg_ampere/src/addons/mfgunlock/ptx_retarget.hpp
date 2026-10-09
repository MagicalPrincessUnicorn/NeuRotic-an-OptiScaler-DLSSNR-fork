/*
 * PTX retargeting for known DLSS-G fatbin layouts.
 * SPDX-License-Identifier: MIT
 *
 * Ordinary target changes edit independent LZ4 literals and verify the decoded
 * bytes. Fingerprinted SM89 compatibility programs use strict SM75 lowerings;
 * rebuilt single-image PTX is recompressed only when it fits its exact container.
 */
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "./architecture.hpp"
#include "./fatbin.hpp"
#include "./ptx_compat.hpp"

namespace mfgunlock::ptx {

enum class Result {
  kUnchanged,
  kRetargeted,
  kRejected,
};

struct Plan {
  std::vector<unsigned char> replacement;
  size_t ptx_offset = 0;
  size_t ptx_bytes = 0;
  size_t hidden_cubins = 0;
};

inline std::string_view Trim(std::string_view value) {
  const size_t first = value.find_first_not_of(" \t\r");
  if (first == value.npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

// Preserve byte positions while blanking comments and strings. This prevents a
// filename, quoted string, or comment containing "sm_89" from being retargeted.
inline bool FindTargetDigits(std::span<const unsigned char> raw, uint32_t target_sm,
                              size_t& digit) {
  const auto nul = std::find(raw.begin(), raw.end(), 0);
  if (nul == raw.end()) return false;

  std::string text(raw.begin(), nul);
  bool block_comment = false;
  bool quoted = false;

  for (size_t i = 0; i < text.size(); ++i) {
    if (block_comment) {
      if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
        text[i] = ' ';
        text[i + 1] = ' ';
        ++i;
        block_comment = false;
      } else if (text[i] != '\n') {
        text[i] = ' ';
      }
      continue;
    }

    if (quoted) {
      const char c = text[i];
      text[i] = ' ';
      if (c == '\\' && i + 1 < text.size()) {
        text[++i] = ' ';
      } else if (c == '"') {
        quoted = false;
      } else if (c == '\n') {
        return false;
      }
      continue;
    }

    if (text[i] == '"') {
      text[i] = ' ';
      quoted = true;
    } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      text[i] = ' ';
      text[i + 1] = ' ';
      ++i;
      block_comment = true;
    } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      for (; i < text.size() && text[i] != '\n'; ++i) text[i] = ' ';
    }
  }

  if (block_comment || quoted) return false;

  size_t targets = 0;
  size_t versions = 0;
  size_t addresses = 0;

  for (size_t offset = 0; offset < text.size();) {
    const size_t newline = text.find('\n', offset);
    const size_t end = newline == text.npos ? text.size() : newline;
    const auto line = Trim(std::string_view(text).substr(offset, end - offset));

    const auto is_directive = [&](std::string_view name) {
      return line.starts_with(name) && line.size() > name.size() &&
             (line[name.size()] == ' ' || line[name.size()] == '\t');
    };

    if (is_directive(".target")) {
      const auto target = Trim(line.substr(7));
      if (target != "sm_89" || ++targets != 1) return false;
      digit = static_cast<size_t>(target.data() - text.data()) + 3;
    } else if (is_directive(".version")) {
      const auto version = Trim(line.substr(8));
      if (version.size() != 3 || version[0] != '8' || version[1] != '.' ||
          version[2] < '0' || version[2] > '7' || ++versions != 1) {
        return false;
      }
    } else if (is_directive(".address_size")) {
      if (Trim(line.substr(13)) != "64" || ++addresses != 1) return false;
    }

    offset = end + 1;
  }

  // These instructions are outside the supported backport targets.
  constexpr std::string_view kUnsupported[] = {
      "wgmma.",
      "tcgen05.",
      "tensormap.",
      "cp.async.bulk",
      ".e4m3",
      ".e5m2",
      ".e2m1",
  };
  for (const auto token : kUnsupported) {
    if (text.find(token) != text.npos) return false;
  }

  if (target_sm == 75) {
    // PTX features introduced with Ampere cannot be enabled by changing .target.
    constexpr std::string_view kSm80Instructions[] = {
        "cp.async", "mbarrier.", "redux.sync", "mma.sp.", ".bf16", ".tf32",
        ".f16x2.f32", "max.f16", "min.f16",
        "mma.sync.aligned.m16n8k16", "mma.sync.aligned.m16n8k32",
        "mma.sync.aligned.m16n8k64", "mma.sync.aligned.m8n8k128",
        "mma.sync.aligned.m16n8k128", "mma.sync.aligned.m16n8k256",
        "mma.sync.aligned.m8n8k4.row.col.f64",
    };
    for (const auto token : kSm80Instructions) {
      if (text.find(token) != text.npos) return false;
    }
  }
  return targets == 1 && versions == 1 && addresses == 1 &&
         text.find(".entry") != text.npos;
}


struct PackedHalfProfile {
  size_t bytes;
  uint64_t fingerprint;
  size_t conversions;
};

// The only inspected SM89 packed-half programs in DLSS-G 310.9.1.
inline constexpr PackedHalfProfile kPackedHalfProfiles[] = {
    {42029, 0x9b5219cdd66e0969ull, 20},  // BlendCandidatesFused
    {10036, 0x87e83571b0c08cbeull, 1},   // DL1Net_Input
    {30175, 0x6578876be2b5fbe2ull, 8},   // OutputPull
    {11165, 0x1d6ba2d7fd03152eull, 8},   // OutputPullMiddle
    {52834, 0x62a7b5ee15c6f3e1ull, 2},   // OutputPush
};

inline size_t PackedHalfCount(std::string_view source) {
  const auto hash = SourceFingerprint(source);
  for (const auto& profile : kPackedHalfProfiles) {
    if (source.size() == profile.bytes && hash == profile.fingerprint)
      return profile.conversions;
  }
  return 0;
}


struct TuringMmaProfile {
  size_t bytes;
  uint64_t fingerprint;
  size_t instructions;
  HalfMinMaxCounts half_minmax;
};

// Exact single-image SM89 programs in DLSS-G 310.9.1 that use the Ampere
// m16n8k16 f16 MMA shape. Unknown programs remain rejected.
inline constexpr TuringMmaProfile kTuringMmaProfiles[] = {
    {20437, 0xce2c5c3f5d55cd81ull, 18, {4, 4, 0, 0}},
    {11638, 0x75fe3a8642b3e477ull, 9, {0, 0, 4, 2}},
    {16101, 0xd932cb22dd83419eull, 18, {0, 0, 4, 2}},
    {15662, 0x6702b594af03caecull, 18, {0, 0, 4, 2}},
    {16341, 0x8eb26063a973182cull, 12, {0, 0, 4, 2}},
    {13231, 0xbc841338134753c6ull, 12, {0, 0, 4, 2}},
    {14603, 0xdde1c125651eb889ull, 9, {0, 0, 4, 2}},
    {11750, 0xfdfe9b7351889bfdull, 9, {0, 0, 4, 2}},
    {14684, 0xc83bdce19a1a249full, 18, {0, 0, 4, 2}},
    {14693, 0x400b19b43610cce1ull, 18, {0, 0, 4, 2}},
    {17609, 0xe0ba6d48fb005cc1ull, 9, {4, 4, 0, 0}},
    {33114, 0xa582db83fc911fe0ull, 32, {8, 0, 0, 0}},
    {16453, 0xfb821a89f2997eceull, 16, {16, 0, 0, 0}},
    {32740, 0x802242e77fae47cbull, 36, {16, 0, 0, 0}},
    {12511, 0xe52f68bf382c1392ull, 8, {16, 0, 0, 0}},
    {9881, 0xdba2cda2f6af70a5ull, 4, {0, 0, 0, 0}},
    {56261, 0x80fccb8da5f37636ull, 108, {48, 0, 0, 0}},
    {12754, 0xe1fd0775658c8dd3ull, 8, {16, 0, 0, 0}},
    {59794, 0x58f45c0876b5b7c5ull, 64, {8, 0, 0, 0}},
    {33295, 0x3e454577f62ecf8full, 36, {16, 0, 0, 0}},
    {43831, 0x6e407251363cf2a5ull, 48, {8, 0, 0, 0}},
    {33114, 0x625ec319e7d0a5c9ull, 32, {8, 0, 0, 0}},
    {33114, 0xe5fa510399d45224ull, 32, {8, 0, 0, 0}},
    {20153, 0xdef932bb960c28d0ull, 16, {8, 0, 0, 0}},
    {24116, 0x5702cf69c73d6af4ull, 32, {16, 0, 0, 0}},
    {24206, 0xdc473e258540ef53ull, 32, {16, 0, 0, 0}},
    {16214, 0x92f1a598e2b2254cull, 16, {16, 0, 0, 0}},
};

inline const TuringMmaProfile* FindTuringMmaProfile(std::string_view source) {
  const auto hash = SourceFingerprint(source);
  for (const auto& profile : kTuringMmaProfiles) {
    if (source.size() == profile.bytes && hash == profile.fingerprint) return &profile;
  }
  return nullptr;
}

inline size_t TuringMmaCount(std::string_view source) {
  const auto* profile = FindTuringMmaProfile(source);
  return profile == nullptr ? 0 : profile->instructions;
}

// The lowered program fits inside the original complete container, but not
// necessarily its compressed PTX slot. Publish one uncompressed SM75 image;
// the existing provider byte transaction owns every write and its rollback.
inline bool RebuildLoweredProvider(std::span<const unsigned char> bytes,
                                   const fatbin::Entry& source,
                                   const std::string& text,
                                   std::vector<unsigned char>& replacement) {
  if (text.size() > fatbin::kMaxPtxBytes || source.header_bytes < 64 ||
      source.header_bytes % 8 != 0) return false;
  const size_t payload = (text.size() + 1 + 7) & ~size_t{7};
  if (source.offset > bytes.size() || source.header_bytes > bytes.size() - source.offset ||
      bytes.size() < fatbin::kHeaderBytes + source.header_bytes ||
      payload > bytes.size() - fatbin::kHeaderBytes - source.header_bytes) return false;
  replacement.assign(bytes.begin(), bytes.end());
  auto* entry = replacement.data() + fatbin::kHeaderBytes;
  std::memcpy(entry, bytes.data() + source.offset, source.header_bytes);
  const uint64_t outer = source.header_bytes + payload;
  const uint64_t stored = payload;
  const uint32_t target = 75;
  const uint32_t zero32 = 0;
  const uint64_t zero64 = 0;
  const uint64_t flags = 0x41;
  std::memcpy(replacement.data() + 8, &outer, sizeof(outer));
  std::memcpy(entry + 8, &stored, sizeof(stored));
  std::memcpy(entry + 16, &zero32, sizeof(zero32));
  std::memcpy(entry + 28, &target, sizeof(target));
  std::memcpy(entry + 40, &flags, sizeof(flags));
  std::memcpy(entry + 56, &zero64, sizeof(zero64));
  std::memset(entry + source.header_bytes, 0, payload);
  std::memcpy(entry + source.header_bytes, text.data(), text.size());
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  return fatbin::Parse(replacement, entries, end) && entries.size() == 1 &&
      entries[0].architecture == target && entries[0].flags == flags &&
      end == fatbin::kHeaderBytes + outer;
}


inline bool RebuildCompressedProvider(std::span<const unsigned char> bytes,
                                      const fatbin::Entry& source,
                                      const std::string& text,
                                      std::vector<unsigned char>& replacement) {
  if (source.flags != 0x2041 || source.compressed_bytes == 0 ||
      text.size() + 1 > fatbin::kMaxPtxBytes || source.PayloadOffset() > bytes.size() ||
      source.payload_bytes > bytes.size() - source.PayloadOffset()) return false;

  std::string raw = text;
  raw.push_back('\0');
  std::vector<unsigned char> compressed;
  if (!fatbin::Lz4BlockCompress(raw, source.payload_bytes, compressed) &&
      !fatbin::Lz4BlockCompressHigh(raw, source.payload_bytes, compressed)) return false;
  if (compressed.empty() || compressed.size() > source.payload_bytes) return false;

  replacement.assign(bytes.begin(), bytes.end());
  auto* entry = replacement.data() + source.offset;
  const uint32_t compressed_bytes = static_cast<uint32_t>(compressed.size());
  const uint32_t target = 75;
  const uint64_t unpacked_bytes = raw.size();
  std::memcpy(entry + 16, &compressed_bytes, sizeof(compressed_bytes));
  std::memcpy(entry + 28, &target, sizeof(target));
  std::memcpy(entry + 56, &unpacked_bytes, sizeof(unpacked_bytes));
  std::memset(replacement.data() + source.PayloadOffset(), 0, source.payload_bytes);
  std::memcpy(replacement.data() + source.PayloadOffset(), compressed.data(), compressed.size());

  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  if (!fatbin::Parse(replacement, entries, end) || entries.size() != 1 ||
      entries[0].architecture != target || entries[0].compressed_bytes != compressed.size() ||
      entries[0].unpacked_bytes != raw.size() || end > replacement.size()) return false;
  std::vector<unsigned char> decoded(raw.size());
  return fatbin::Lz4BlockDecompress(
             replacement.data() + entries[0].PayloadOffset(), entries[0].compressed_bytes,
             decoded.data(), decoded.size()) &&
         std::equal(decoded.begin(), decoded.end(),
                    reinterpret_cast<const unsigned char*>(raw.data()));
}


inline Result Retarget(std::span<const unsigned char> bytes, Plan& plan, std::string& reason,
                        const ArchitectureProfile& profile = architecture::kAmpere) {
  reason.clear();

  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  const auto reject = [&](const char* message) {
    plan = {};
    reason = message;
    return Result::kRejected;
  };

  if (!profile.RequiresProviderRetarget()) {
    plan = {};
    return Result::kUnchanged;
  }
  if (profile.target_sm != 86 && profile.target_sm != 75)
    return reject("unsupported PTX target");

  if (!fatbin::Parse(bytes, entries, end)) return reject("invalid fatbin bounds/layout");

  const auto sm89 = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
    return entry.architecture == 89;
  });
  if (sm89 == entries.end()) {
    plan = {};
    return Result::kUnchanged;
  }

  // Only these two entry orders have been inspected and regression-tested. A
  // new provider layout must be reviewed as a complete selectable image set.
  const bool single = entries.size() == 1 && entries[0].kind == 1 &&
                      entries[0].architecture == 89;
  const bool mixed = entries.size() == 3 && entries[0].kind == 1 &&
                     entries[0].architecture == 120 && entries[1].kind == 1 &&
                     entries[1].architecture == 89 && entries[2].kind == 2 &&
                     entries[2].architecture == 89;
  if (!single && !mixed) return reject("unreviewed fatbin image ordering");

  const auto& entry = entries[mixed ? 1 : 0];
  if (entry.flags != 0x2041 || entry.compressed_bytes == 0 || entry.unpacked_bytes == 0) {
    return reject("unreviewed PTX compression flags");
  }

  std::vector<unsigned char> raw(entry.unpacked_bytes);
  std::vector<unsigned char> check(entry.unpacked_bytes);
  const auto* compressed = bytes.data() + entry.PayloadOffset();
  if (!fatbin::Lz4BlockDecompress(compressed, entry.compressed_bytes, raw.data(), raw.size())) {
    return reject("invalid LZ4 block");
  }

  if (profile.target_sm == 75) {
    const auto nul = std::find(raw.begin(), raw.end(), 0);
    if (nul == raw.end()) return reject("PTX terminator missing");
    std::string text(raw.begin(), nul);
    std::erase(text, '\r');
    size_t original_digit = 0;
    if (HasPackedHalfConversion(text) && !FindTargetDigits(raw, 75, original_digit)) {
      const size_t count = PackedHalfCount(text);
      if (!mixed || count == 0) return reject("unqualified SM75 packed-half source");
      if (!LowerPackedHalf(text, 75, count, reason)) { plan = {}; return Result::kRejected; }
      std::vector<unsigned char> lowered(text.begin(), text.end());
      lowered.push_back(0);
      size_t digit = 0;
      if (!FindTargetDigits(lowered, 75, digit))
        return reject("lowered PTX has unsupported header/instructions");
      text.replace(digit, 2, "75");
      if (!RebuildLoweredProvider(bytes.first(end), entry, text, plan.replacement))
        return reject("lowered SM75 program exceeds original provider container");
      plan.ptx_offset = fatbin::kHeaderBytes + entry.header_bytes;
      plan.ptx_bytes = text.size() + 1;
      plan.hidden_cubins = 1;
      return Result::kRetargeted;
    }

    if (HasTuringMma16816(text)) {
      const auto* turing = FindTuringMmaProfile(text);
      if (!single || turing == nullptr) return reject("unqualified SM75 m16n8k16 source");
      if (!LowerTuringMma16816(text, 75, turing->instructions, reason) ||
          !LowerTuringHalfMinMax(text, 75, turing->half_minmax, reason)) {
        plan = {};
        return Result::kRejected;
      }
      if (!CompactProviderPtx(text, reason)) {
        plan = {};
        return Result::kRejected;
      }
      std::vector<unsigned char> lowered(text.begin(), text.end());
      lowered.push_back(0);
      size_t digit = 0;
      if (!FindTargetDigits(lowered, 75, digit))
        return reject("lowered MMA PTX has unsupported header/instructions");
      text.replace(digit, 2, "75");
      if (!RebuildCompressedProvider(bytes.first(end), entry, text, plan.replacement))
        return reject("lowered SM75 MMA program does not fit its compressed provider container");
      plan.ptx_offset = entry.PayloadOffset();
      plan.ptx_bytes = text.size() + 1;
      plan.hidden_cubins = 0;
      return Result::kRetargeted;
    }
  }

  size_t target_digit = 0;
  if (!FindTargetDigits(raw, profile.target_sm, target_digit))
    return reject("unreviewed PTX header/instructions");

  std::vector<unsigned char> replacement(bytes.begin(), bytes.begin() + end);
  const unsigned char digits[] = {
      static_cast<unsigned char>('0' + profile.target_sm / 10),
      static_cast<unsigned char>('0' + profile.target_sm % 10),
  };
  for (size_t i = 0; i < 2; ++i) {
    if (raw[target_digit + i] == digits[i]) continue;
    size_t literal = 0;
    if (!fatbin::Lz4BlockDecompress(compressed, entry.compressed_bytes, check.data(), check.size(),
                                    target_digit + i, &literal) ||
        literal >= entry.compressed_bytes || compressed[literal] != raw[target_digit + i]) {
      return reject("target is not an independent LZ4 literal");
    }
    replacement[entry.PayloadOffset() + literal] = digits[i];
  }
  if (!fatbin::Lz4BlockDecompress(replacement.data() + entry.PayloadOffset(),
                                  entry.compressed_bytes, check.data(), check.size())) {
    return reject("edited LZ4 block invalid");
  }

  raw[target_digit] = digits[0];
  raw[target_digit + 1] = digits[1];
  if (raw != check) return reject("target literal is shared with another output byte");

  const uint64_t visible_bytes = entry.End() - fatbin::kHeaderBytes;
  std::memcpy(replacement.data() + entry.offset + 28, &profile.target_sm,
              sizeof(profile.target_sm));
  std::memcpy(replacement.data() + 8, &visible_bytes, sizeof(visible_bytes));

  plan.replacement = std::move(replacement);
  plan.ptx_offset = entry.PayloadOffset();
  plan.ptx_bytes = entry.unpacked_bytes;
  plan.hidden_cubins = mixed ? 1 : 0;
  return Result::kRetargeted;
}

}  // namespace mfgunlock::ptx
