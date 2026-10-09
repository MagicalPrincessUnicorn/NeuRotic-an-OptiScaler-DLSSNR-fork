/*
 * Architecture selection shared by the provider and capability hooks.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace mfgunlock {

// The existing multi-frame override switch also controls provider preparation.
inline std::atomic_bool g_enabled{false};

enum class Architecture {
  kAuto,
  kAda,
  kAmpere,
  kTuring,
  kUnknown,
};

enum class ProviderBackend {
  kUnknown,
  kNativeSm89,
  kRetargetSm86,
  kRetargetSm75,
};

struct ArchitectureProfile {
  Architecture architecture;
  ProviderBackend provider_backend;
  uint32_t native_arch;
  uint32_t exposed_arch;
  uint32_t target_sm;

  constexpr bool RequiresProviderRetarget() const {
    return provider_backend == ProviderBackend::kRetargetSm86 ||
           provider_backend == ProviderBackend::kRetargetSm75;
  }
};

namespace architecture {

inline constexpr uint32_t kNvidiaVendorId = 0x10de;
inline constexpr uint32_t kTuringArchitecture = 0x160;
inline constexpr uint32_t kAmpereArchitecture = 0x170;
inline constexpr uint32_t kAdaArchitecture = 0x190;
inline constexpr Architecture kDefault = Architecture::kAuto;

inline constexpr ArchitectureProfile kAda{Architecture::kAda, ProviderBackend::kNativeSm89,
                                           kAdaArchitecture, kAdaArchitecture, 89};
inline constexpr ArchitectureProfile kAmpere{Architecture::kAmpere, ProviderBackend::kRetargetSm86,
                                              kAmpereArchitecture, kAdaArchitecture, 86};
inline constexpr ArchitectureProfile kTuring{Architecture::kTuring, ProviderBackend::kRetargetSm75,
                                              kTuringArchitecture, kAdaArchitecture, 75};

inline constexpr const ArchitectureProfile* GetProfile(Architecture value) {
  switch (value) {
    case Architecture::kAda: return &kAda;
    case Architecture::kAmpere: return &kAmpere;
    case Architecture::kTuring: return &kTuring;
    default: return nullptr;
  }
}

inline constexpr const char* Name(Architecture value) {
  switch (value) {
    case Architecture::kAuto: return "Auto";
    case Architecture::kAda: return "Ada";
    case Architecture::kAmpere: return "Ampere";
    case Architecture::kTuring: return "Turing";
    default: return "Unknown";
  }
}

inline bool EqualsInsensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

// A missing key uses the build default; a misspelled value does not.
inline Architecture Parse(const char* value) {
  if (!value) return kDefault;
  std::string_view text(value);
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == text.npos) return Architecture::kUnknown;
  text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
  for (const auto candidate : {Architecture::kAuto, Architecture::kAda,
                               Architecture::kAmpere, Architecture::kTuring}) {
    if (EqualsInsensitive(text, Name(candidate))) return candidate;
  }
  return Architecture::kUnknown;
}

inline Architecture Detect(uint32_t vendor, uint32_t native_arch, uint32_t implementation) {
  if (vendor != kNvidiaVendorId) return Architecture::kUnknown;
  switch (native_arch) {
    case kAdaArchitecture: return Architecture::kAda;
    case kTuringArchitecture: return Architecture::kTuring;
    case kAmpereArchitecture:
      // GA100 is sm_80, not the sm_86 target used by this profile.
      return implementation == 0 ? Architecture::kUnknown : Architecture::kAmpere;
    default: return Architecture::kUnknown;
  }
}

inline std::atomic<Architecture> g_configured{kDefault};
inline std::atomic<Architecture> g_active{kDefault};

// Configuration is read at addon load. Profile changes require a game restart:
// an already prepared provider must not be retargeted a second time.
inline void Configure(Architecture value) {
  g_configured.store(value, std::memory_order_relaxed);
  g_active.store(value, std::memory_order_release);
}

inline const ArchitectureProfile* ActiveProfile() {
  return GetProfile(g_active.load(std::memory_order_acquire));
}

inline bool NeedsDetection() {
  return g_active.load(std::memory_order_acquire) == Architecture::kAuto;
}

inline bool ResolveAuto(uint32_t vendor, uint32_t native_arch, uint32_t implementation) {
  if (g_configured.load(std::memory_order_relaxed) != Architecture::kAuto) return false;
  auto pending = Architecture::kAuto;
  return g_active.compare_exchange_strong(pending, Detect(vendor, native_arch, implementation),
                                          std::memory_order_acq_rel);
}

inline bool NeedsBridge() {
  const auto* profile = ActiveProfile();
  return profile ? profile->RequiresProviderRetarget() : NeedsDetection();
}

}  // namespace architecture
}  // namespace mfgunlock
