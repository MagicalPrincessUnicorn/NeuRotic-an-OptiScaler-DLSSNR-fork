// SPDX-License-Identifier: MIT
#include "../../../src/addons/mfgunlock/architecture.hpp"

#include <cstdio>
#include <cstdlib>

using namespace mfgunlock;
namespace arch = mfgunlock::architecture;

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
  Check(arch::Parse(nullptr) == Architecture::kAuto, "missing config uses Auto");
  for (const auto mode : {Architecture::kAda, Architecture::kAmpere, Architecture::kTuring}) {
    Check(arch::Parse(arch::Name(mode)) == mode, "profile name roundtrip");
    arch::Configure(mode);
    const auto* profile = arch::ActiveProfile();
    Check(profile && profile->architecture == mode, "explicit profile resolved immediately");
    Check(!arch::NeedsDetection(), "explicit mode does not detect hardware");
    Check(!arch::ResolveAuto(0x1002, 0xffffffff, 0), "detection cannot replace explicit selection");
    Check(arch::ActiveProfile() == profile, "explicit profile unchanged");
    Check(profile->exposed_arch == 0x190, "DLSS-G capability architecture");
    Check(arch::NeedsBridge() == (mode != Architecture::kAda), "native Ada skips provider retarget bridge");
  }
  Check(arch::kAda.native_arch == 0x190 && arch::kAda.target_sm == 89 &&
            arch::kAda.provider_backend == ProviderBackend::kNativeSm89 &&
            !arch::kAda.RequiresProviderRetarget(),
        "Ada native provider backend");
  Check(arch::kAmpere.native_arch == 0x170 && arch::kAmpere.target_sm == 86 &&
            arch::kAmpere.provider_backend == ProviderBackend::kRetargetSm86 &&
            arch::kAmpere.RequiresProviderRetarget(),
        "Ampere provider backend");
  Check(arch::kTuring.native_arch == 0x160 && arch::kTuring.target_sm == 75 &&
            arch::kTuring.provider_backend == ProviderBackend::kRetargetSm75 &&
            arch::kTuring.RequiresProviderRetarget(),
        "Turing provider backend");
  Check(arch::Parse("aMpErE") == Architecture::kAmpere, "case-insensitive name");
  Check(arch::Parse(" \tTURING\r\n") == Architecture::kTuring, "trim name");
  Check(arch::Parse("auto") == Architecture::kAuto, "auto name");
  for (const auto* value : {"", " ", "RTX 3090", "AmpereExperimental", "Ampere1", "auto-detect"}) {
    Check(arch::Parse(value) == Architecture::kUnknown, "invalid setting is not a default");
  }

  for (const auto* profile : {&arch::kAda, &arch::kAmpere, &arch::kTuring}) {
    arch::Configure(Architecture::kAuto);
    Check(!arch::ActiveProfile() && arch::NeedsDetection(), "Auto pending");
    Check(arch::ResolveAuto(0x10de, profile->native_arch, 2), "resolve actual architecture");
    Check(arch::ActiveProfile() == profile, "Auto selected expected profile");
    Check(!arch::NeedsDetection(), "Auto resolves once");
    Check(!arch::ResolveAuto(0x10de, 0x190, 2), "later exposed arch cannot replace selection");
    Check(arch::ActiveProfile() == profile, "Auto profile cached");
  }
  Check(arch::Detect(0x10de, 0x170, 0) == Architecture::kUnknown, "GA100 is not sm86");
  for (auto impl : {2u, 4u, 6u, 7u}) {
    Check(arch::Detect(0x10de, 0x170, impl) == Architecture::kAmpere, "GA10x profile");
  }
  for (auto vendor : {0u, 0x1002u, 0x8086u}) {
    Check(arch::Detect(vendor, 0x170, 2) == Architecture::kUnknown, "non-NVIDIA Auto");
  }
  arch::Configure(Architecture::kAuto);
  Check(arch::ResolveAuto(0x10de, 0x1b0, 2), "unknown Auto result resolved");
  Check(!arch::ActiveProfile() && !arch::NeedsDetection() && !arch::NeedsBridge(),
        "unknown Auto does not fall back to Ampere");
  arch::Configure(Architecture::kUnknown);
  Check(!arch::ActiveProfile() && !arch::NeedsBridge(), "invalid config does not patch");
  arch::Configure(arch::Parse(nullptr));
  Check(!arch::ActiveProfile() && arch::NeedsDetection(), "fresh load restores Auto default");
  std::printf("PASS architecture profiles: %u checks\n", g_checks);
}
