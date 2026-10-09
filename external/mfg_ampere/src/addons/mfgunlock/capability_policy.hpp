/*
 * Architecture capability policy for native DLSS-G.
 * SPDX-License-Identifier: MIT
 *
 * Side-effect-free rules shared by the NGX/NVAPI hooks and portable tests.
 */
#pragma once

#include <climits>
#include <cstdint>

#include "./architecture.hpp"

namespace mfgunlock::policy {

using architecture::kNvidiaVendorId;
using architecture::kTuringArchitecture;
using architecture::kAmpereArchitecture;
using architecture::kAdaArchitecture;
inline constexpr uint32_t kDlssGFeatureId = 11;
inline constexpr uint32_t kNgxSuccess = 1;
inline constexpr uint32_t kAdapterUnsupported = 4;
inline constexpr uint32_t kStreamlineNoSupportedAdapter = 6;

struct RequirementsEvidence {
  bool enabled;
  bool adapter_bound;
  unsigned int prepared_providers;
  uint32_t call_result;
  uint32_t feature;
  uint32_t flags;
  uint32_t minimum_architecture;
};

inline bool CanRelaxRequirements(const RequirementsEvidence& evidence,
                                 const ArchitectureProfile* profile) {
  // AdapterUnsupported also covers non-architecture failures. Preserve all
  // other requirement flags and only override for the prepared DLSS-G provider.
  return profile && profile->RequiresProviderRetarget() && evidence.enabled && evidence.adapter_bound &&
         evidence.prepared_providers == 1 && evidence.call_result == kNgxSuccess &&
         evidence.feature == kDlssGFeatureId &&
         (evidence.flags == 0 || evidence.flags == kAdapterUnsupported) &&
         (evidence.minimum_architecture == profile->exposed_arch ||
          (evidence.minimum_architecture == profile->native_arch &&
           evidence.flags == kAdapterUnsupported));
}

inline bool CanExposeArchitecture(bool enabled, bool fg_requirements_scope, bool same_physical_gpu,
                                   bool provider_ready, int nvapi_result,
                                   const ArchitectureProfile* profile) {
  return profile && profile->RequiresProviderRetarget() && enabled && fg_requirements_scope &&
         same_physical_gpu && provider_ready && nvapi_result == 0;
}

struct StreamlineSupportEvidence {
  bool enabled;
  bool adapter_bound;
  bool adapter_matches;
  unsigned int prepared_providers;
  uint32_t result;
};

inline bool CanRelaxStreamlineSupport(const StreamlineSupportEvidence& evidence,
                                      const ArchitectureProfile* profile) {
  // A prepared native provider plus an exact renderer-adapter LUID match is
  // required before relaxing Streamline's cached adapter-admission verdict.
  // Preserve all other SL errors so OS, driver, plugin and integration failures
  // continue to fail closed.
  return profile && profile->RequiresProviderRetarget() && evidence.enabled && evidence.adapter_bound &&
         evidence.adapter_matches && evidence.prepared_providers == 1 &&
         evidence.result == kStreamlineNoSupportedAdapter;
}

// Counts are generated frames, not presentation multipliers. A nonzero
// structural ceiling is authoritative for Streamline; direct NGX leaves it 0.
inline int CapabilityFrameCount(int reported, unsigned int limit,
                                unsigned int structural_ceiling = 0) {
  if (reported < 0 || limit == 0 || limit > static_cast<unsigned int>(INT_MAX)) return reported;
  int wanted = reported < static_cast<int>(limit) ? static_cast<int>(limit) : reported;
  if (structural_ceiling == 0 || structural_ceiling > static_cast<unsigned int>(INT_MAX))
    return wanted;
  return wanted > static_cast<int>(structural_ceiling)
      ? static_cast<int>(structural_ceiling)
      : wanted;
}


}  // namespace mfgunlock::policy
