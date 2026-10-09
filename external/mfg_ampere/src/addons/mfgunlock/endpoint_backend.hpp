// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <span>
#include <string_view>

#include "./ptx_retarget.hpp"
#include "./provider_profile.hpp"

namespace mfgunlock::endpoint {

using Region = profiles::Region;
using Selector = profiles::Selector;
inline constexpr auto kTimestamp = profiles::kTimestamp;
inline constexpr auto kImageBytes = profiles::kImageBytes;
inline constexpr auto& kCode = profiles::kEndpointCode;
inline constexpr auto& kSelectors = profiles::kEndpointSelectors;
inline constexpr auto& kPayloads = profiles::kEndpointPayloads;

inline bool Matches(std::span<const unsigned char> bytes, const Region& region) {
  return bytes.size() == region.bytes &&
         ptx::SourceFingerprint({reinterpret_cast<const char*>(bytes.data()), bytes.size()}) ==
             region.hash;
}

inline bool QualifiedCode(std::span<const unsigned char> image, uint32_t timestamp) {
  if (timestamp != kTimestamp || image.size() != kImageBytes) return false;
  for (const auto& region : kCode) {
    if (region.rva > image.size() || region.bytes > image.size() - region.rva ||
        !Matches(image.subspan(region.rva, region.bytes), region)) return false;
  }
  for (const auto& site : kSelectors) {
    if (site.rva > image.size() || site.before.size() > image.size() - site.rva ||
        !std::equal(site.before.begin(), site.before.end(), image.begin() + site.rva)) return false;
  }
  // Constructors publish these vtables; bind their variant-specific loaders.
  const auto base = reinterpret_cast<uintptr_t>(image.data());
  for (const auto& binding : profiles::kEndpointVtables) {
    if (binding.rva > image.size() || sizeof(uint64_t) > image.size() - binding.rva ||
        fatbin::ReadU64(image.data() + binding.rva) != base + binding.target_rva) return false;
  }
  return true;
}

inline bool PreparedPayload(std::span<const unsigned char> before,
                            std::span<const unsigned char> after, const Region& region) {
  if (!Matches(before, region) || after.size() != before.size()) return false;
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  return fatbin::Parse(after, entries, end) && entries.size() == 1 &&
         entries.front().kind == 1 && entries.front().architecture == 75;
}

}  // namespace mfgunlock::endpoint
