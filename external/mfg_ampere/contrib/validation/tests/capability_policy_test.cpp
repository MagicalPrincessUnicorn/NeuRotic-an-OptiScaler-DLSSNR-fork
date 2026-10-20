// SPDX-License-Identifier: MIT
#include "../../../src/addons/mfgunlock/capability_policy.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>

using namespace mfgunlock;
using namespace mfgunlock::policy;

namespace {
unsigned int g_checks = 0;

void Check(bool value, const char* name) {
  ++g_checks;
  if (value) return;
  std::fprintf(stderr, "FAIL %s\n", name);
  std::exit(1);
}
}  // namespace

int main() {
  const RequirementsEvidence base{true, true, 1, kNgxSuccess, kDlssGFeatureId,
                                  kAdapterUnsupported, kAdaArchitecture};
  for (const ArchitectureProfile* profile : {&architecture::kAmpere, &architecture::kTuring}) {
    Check(CanRelaxRequirements(base, profile), "qualified architecture override");
    auto evidence = base;
    evidence.minimum_architecture = profile->native_arch;
    Check(CanRelaxRequirements(evidence, profile), "retargeted minimum with unsupported flag");
    evidence.flags = 0;
    Check(!CanRelaxRequirements(evidence, profile), "already supported is unchanged");

    for (uint32_t feature = 0; feature < 40; ++feature) {
      for (uint32_t flags = 0; flags < 64; ++flags) {
        for (uint32_t minimum : {0u, 0x160u, 0x170u, 0x190u, 0x1b0u, 0xffffffffu}) {
          evidence = base;
          evidence.feature = feature;
          evidence.flags = flags;
          evidence.minimum_architecture = minimum;
          const bool expected = feature == kDlssGFeatureId &&
              ((flags == 0 && minimum == profile->exposed_arch) ||
               (flags == kAdapterUnsupported &&
                (minimum == profile->exposed_arch || minimum == profile->native_arch)));
          Check(CanRelaxRequirements(evidence, profile) == expected,
                "feature/flag/minimum matrix");
        }
      }
    }
    for (uint32_t result : {0u, 2u, 0xbad00001u, 0xbad00005u, 0xffffffffu}) {
      evidence = base;
      evidence.call_result = result;
      Check(!CanRelaxRequirements(evidence, profile), "preserve original errors");
    }
    evidence = base;
    evidence.enabled = false;
    Check(!CanRelaxRequirements(evidence, profile), "master switch");
    evidence = base;
    evidence.adapter_bound = false;
    Check(!CanRelaxRequirements(evidence, profile), "bound adapter required for scoped override");
    for (unsigned int providers : {0u, 2u, 3u, 8u}) {
      evidence = base;
      evidence.prepared_providers = providers;
      Check(!CanRelaxRequirements(evidence, profile), "unprepared or ambiguous provider");
    }
    for (int mask = 0; mask < 32; ++mask) {
      Check(CanExposeArchitecture((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                                   (mask & 8) != 0, (mask & 16) ? -1 : 0, profile) == (mask == 15),
            "NVAPI scope matrix");
    }
    const StreamlineSupportEvidence support_base{
        true, true, true, 1, kStreamlineNoSupportedAdapter};
    Check(CanRelaxStreamlineSupport(support_base, profile),
          "qualified Streamline adapter admission override");
    for (int mask = 0; mask < 16; ++mask) {
      auto support = support_base;
      support.enabled = (mask & 1) != 0;
      support.adapter_bound = (mask & 2) != 0;
      support.adapter_matches = (mask & 4) != 0;
      support.prepared_providers = (mask & 8) ? 1u : 0u;
      Check(CanRelaxStreamlineSupport(support, profile) == (mask == 15),
            "Streamline admission evidence matrix");
    }
    for (uint32_t result : {0u, 4u, 7u, 31u, 32u, 0xffffffffu}) {
      auto support = support_base;
      support.result = result;
      Check(!CanRelaxStreamlineSupport(support, profile),
            "preserve non-adapter Streamline failures");
    }
  }
  for (const auto* profile : {&architecture::kAda, static_cast<const ArchitectureProfile*>(nullptr)}) {
    Check(!CanRelaxRequirements(base, profile), "native or unresolved profile leaves NGX unchanged");
    Check(!CanExposeArchitecture(true, true, true, true, 0, profile), "no architecture spoof");
    const StreamlineSupportEvidence support{true, true, true, 1, kStreamlineNoSupportedAdapter};
    Check(!CanRelaxStreamlineSupport(support, profile),
          "native or unresolved profile leaves Streamline support unchanged");
  }

  Check(CapabilityFrameCount(1, 3) == 3, "raise lower capability");
  Check(CapabilityFrameCount(3, 3) == 3, "preserve equal capability");
  Check(CapabilityFrameCount(5, 3) == 5, "never reduce native capability");
  Check(CapabilityFrameCount(-1, 3) == -1, "preserve invalid capability");
  Check(CapabilityFrameCount(1, 0) == 1, "zero limit leaves capability unchanged");
  Check(CapabilityFrameCount(1, static_cast<unsigned int>(INT_MAX)) == INT_MAX,
        "maximum signed limit accepted");
  Check(CapabilityFrameCount(1, static_cast<unsigned int>(INT_MAX) + 1u) == 1,
        "out-of-range limit rejected");
  Check(CapabilityFrameCount(1, 5, 3) == 3,
        "Streamline structural ceiling caps an advertised raise");
  Check(CapabilityFrameCount(5, 5, 3) == 3,
        "Streamline structural ceiling caps a larger native capability");
  Check(CapabilityFrameCount(1, 5, 0) == 5,
        "direct NGX has no artificial Streamline ceiling");

  std::printf("PASS capability policy: %u checks\n", g_checks);
}
