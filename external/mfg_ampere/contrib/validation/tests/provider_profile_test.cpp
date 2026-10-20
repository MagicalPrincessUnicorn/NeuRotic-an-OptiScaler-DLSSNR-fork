// SPDX-License-Identifier: MIT
#include "../../../src/addons/mfgunlock/provider_profile.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace pf = mfgunlock::profiles;
unsigned int g_checks = 0;
void Check(bool value, const char* why) {
  ++g_checks;
  if (!value) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
  const auto& p = pf::kDlssg31091;
  Check(pf::Find(p.timestamp, p.image_size) == &pf::kProfiles[0], "known metadata discovers profile");
  Check(!pf::Find(p.timestamp + 1, p.image_size), "timestamp mismatch");
  Check(!pf::Find(p.timestamp, p.image_size + 1), "image mismatch");
  for (auto sm : {75u, 86u, 89u}) Check(pf::AllowsTarget(p, sm), "qualified target");
  Check(!pf::AllowsTarget(p, 120), "Blackwell is not an addon retarget target");
  Check(p.retarget_payloads.size() == 70 && p.endpoint_payloads.size() == 39, "exact inventory sizes");
  Check(p.selectors.size() == 2 && p.endpoint_code.size() == 5 && p.quality_kernels.size() == 4, "layout sizes");
  for (const auto& r : p.endpoint_payloads) {
    Check(std::count_if(p.retarget_payloads.begin(), p.retarget_payloads.end(), [&](const auto& x) {
      return r.rva == x.rva && r.bytes == x.bytes && r.hash == x.hash;
    }) == 1, "every endpoint payload belongs to exact retarget inventory once");
  }
  Check(pf::Classify(false, false, false) == pf::Qualification::kUnknown, "unknown");
  Check(pf::Classify(true, false, true) == pf::Qualification::kObserveOnly, "preparation cannot bypass identity");
  Check(pf::Classify(true, true, false) == pf::Qualification::kQualified, "qualified is not patchable");
  Check(pf::Classify(true, true, true) == pf::Qualification::kPatchable, "commit readiness");
  std::vector<unsigned char> bytes{1, 2, 3, 4, 5, 6};
  const std::array<pf::Region, 1> region{{{1, 3, pf::Fingerprint(std::span(bytes).subspan(1, 3))}}};
  Check(pf::MatchesRegions(bytes, region), "exact region");
  Check(!pf::MatchesRegions(bytes, {}), "empty qualification is not proof");
  Check(!pf::MatchesRegions(std::span(bytes).first(2), region), "partial input");
  for (size_t i = 1; i < 4; ++i) {
    bytes[i] ^= 1;
    Check(!pf::MatchesRegions(bytes, region), "one changed byte rejects");
    bytes[i] ^= 1;
  }
  const std::array<pf::Region, 1> overflow{{{UINT32_MAX, UINT32_MAX, 0}}};
  Check(!pf::MatchesRegions(bytes, overflow), "overflow bounds");
  std::cout << "provider_profile_test: " << g_checks << " checks PASS\n";
}
