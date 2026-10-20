// SPDX-License-Identifier: MIT
// Optional DLL argument is read as bytes only. No vendor code is executed.
#include "../../../src/addons/mfgunlock/provider.hpp"
#include "../../../src/addons/mfgunlock/blackwell_temporal.hpp"
#include "../../../src/addons/mfgunlock/validated_warp.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace endpoint = mfgunlock::endpoint;
namespace provider = mfgunlock::provider;
namespace architecture = mfgunlock::architecture;
namespace fatbin = mfgunlock::fatbin;
namespace ptx = mfgunlock::ptx;
namespace mock = provider_mock;
using Bytes = std::vector<unsigned char>;

namespace {
unsigned int g_checks = 0;

void Check(bool condition, const char* reason) {
  ++g_checks;
  if (!condition) throw std::runtime_error(reason);
}

uint64_t Hash(const Bytes& bytes) {
  return ptx::SourceFingerprint({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

void Reset() {
  provider::internal::g_providers.clear();
  provider::internal::g_rejected.clear();
  provider::internal::g_registry_failed = false;
  provider::internal::g_comparison_restore_failed = false;
  provider::g_create_seen = false;
  mfgunlock::g_enabled = true;
  mock::protection_calls = 0;
  mock::fail_protection_call = 0;
  mock::fail_protection_calls.clear();
}

// Map sections and DIR64 relocations for the production Windows-memory mock.
// This is not LoadLibrary; the buffer is never executable.
Bytes MapFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  Check(input.good(), "cannot open provider file");
  Bytes file{std::istreambuf_iterator<char>(input), {}};
  Check(file.size() > 0x100 && file.size() < 64 * 1024 * 1024, "file size");
  const auto nt = fatbin::ReadU32(file.data() + 0x3c);
  Check(nt <= file.size() - 264 && fatbin::ReadU32(file.data() + nt) == 0x4550,
        "PE signature/bounds");
  const auto* optional = file.data() + nt + 24;
  Check(fatbin::ReadU16(optional) == 0x20b, "PE64 required");
  const auto image_bytes = fatbin::ReadU32(optional + 56);
  const auto headers = fatbin::ReadU32(optional + 60);
  Check(image_bytes == endpoint::kImageBytes && headers <= file.size() &&
            headers <= image_bytes, "qualified image/header size");
  Bytes image(image_bytes);
  std::copy_n(file.begin(), headers, image.begin());
  const auto count = fatbin::ReadU16(file.data() + nt + 6);
  const size_t table = nt + 24 + fatbin::ReadU16(file.data() + nt + 20);
  Check(count <= 96 && table <= file.size() && count * 40u <= file.size() - table,
        "section table bounds");
  for (size_t i = 0; i < count; ++i) {
    const auto* section = file.data() + table + 40 * i;
    const auto rva = fatbin::ReadU32(section + 12);
    const auto bytes = fatbin::ReadU32(section + 16);
    const auto raw = fatbin::ReadU32(section + 20);
    Check(rva <= image.size() && bytes <= image.size() - rva &&
              raw <= file.size() && bytes <= file.size() - raw, "section bounds");
    std::copy_n(file.begin() + raw, bytes, image.begin() + rva);
  }
  const auto preferred_base = fatbin::ReadU64(optional + 24);
  const auto delta = reinterpret_cast<uintptr_t>(image.data()) - preferred_base;
  size_t at = fatbin::ReadU32(optional + 112 + 5 * 8);
  size_t remaining = fatbin::ReadU32(optional + 112 + 5 * 8 + 4);
  Check(at <= image.size() && remaining <= image.size() - at, "relocation bounds");
  while (remaining != 0) {
    Check(remaining >= 8, "relocation header");
    const auto page = fatbin::ReadU32(image.data() + at);
    const auto bytes = fatbin::ReadU32(image.data() + at + 4);
    Check(bytes >= 8 && bytes <= remaining && bytes % 2 == 0, "relocation block");
    for (size_t i = 8; i < bytes; i += 2) {
      const auto entry = fatbin::ReadU16(image.data() + at + i);
      if (entry >> 12 == 0) continue;
      Check(entry >> 12 == 10, "DIR64 relocation required");
      const size_t rva = page + (entry & 0xfff);
      Check(rva <= image.size() - 8, "relocation target");
      const uint64_t value = fatbin::ReadU64(image.data() + rva) + delta;
      std::memcpy(image.data() + rva, &value, sizeof(value));
    }
    at += bytes;
    remaining -= bytes;
  }
  return image;
}

void Register(Bytes& image) {
  auto* base = image.data();
  mock::regions = {{base, image.size()}};
  mock::paths[base] = "X:\\read_only_input\\nvngx_dlssg.dll";
  const auto nt = fatbin::ReadU32(base + 0x3c);
  const auto exports = fatbin::ReadU32(base + nt + 24 + 112);
  Check(exports <= image.size() - 40, "export bounds");
  const auto count = fatbin::ReadU32(base + exports + 24);
  const auto funcs = fatbin::ReadU32(base + exports + 28);
  const auto names = fatbin::ReadU32(base + exports + 32);
  const auto ordinals = fatbin::ReadU32(base + exports + 36);
  Check(count < 4096 && names <= image.size() && 4ull * count <= image.size() - names &&
            ordinals <= image.size() && 2ull * count <= image.size() - ordinals,
        "export tables");
  for (size_t i = 0; i < count; ++i) {
    const auto name = fatbin::ReadU32(base + names + i * 4);
    const auto ordinal = fatbin::ReadU16(base + ordinals + i * 2);
    const size_t slot = funcs + 4ull * ordinal;
    Check(name < image.size() && slot <= image.size() - 4, "export name/function");
    const auto end = std::find(image.begin() + name, image.end(), '\0');
    Check(end != image.end(), "unterminated export");
    const auto rva = fatbin::ReadU32(base + slot);
    Check(rva < image.size(), "export rva");
    const std::string label(image.begin() + name, end);
    mock::exports[{base, label}] = reinterpret_cast<FARPROC>(base + rva);
  }
}

void UnitChecks() {
  const Bytes bytes{1, 2, 3, 4};
  const endpoint::Region own{0, 4, Hash(bytes)};
  Check(endpoint::Matches(bytes, own), "fingerprint matches");
  auto changed = bytes;
  changed[2] ^= 1;
  Check(!endpoint::Matches(changed, own), "changed bytes reject");
  Check(!endpoint::Matches({}, own), "truncated bytes reject");
  Check(!endpoint::QualifiedCode({}, endpoint::kTimestamp), "empty image reject");
  Bytes image(endpoint::kImageBytes);
  Check(!endpoint::QualifiedCode(image, endpoint::kTimestamp), "same size not a fingerprint");
  Check(!endpoint::QualifiedCode(image, endpoint::kTimestamp + 1), "unknown version reject");
  Check(endpoint::kPayloads.size() == 39 && endpoint::kSelectors.size() == 2,
        "both networks and all payloads are required");
  for (size_t i = 0; i < endpoint::kPayloads.size(); ++i) {
    const auto& region = endpoint::kPayloads[i];
    Check(region.rva < endpoint::kImageBytes && region.bytes <= endpoint::kImageBytes - region.rva,
          "payload bounds");
    for (size_t j = 0; j < i; ++j) {
      const auto& previous = endpoint::kPayloads[j];
      Check(region.rva + region.bytes <= previous.rva ||
                previous.rva + previous.bytes <= region.rva, "payload uniqueness/nonoverlap");
    }
  }
}

void FileChecks(const char* path) {
  auto image = MapFile(path);
  Register(image);
  const auto before = image;
  Check(endpoint::QualifiedCode(image, endpoint::kTimestamp), "real provider code qualification");
  for (const auto& region : endpoint::kCode) {
    image[region.rva] ^= 1;
    Check(!endpoint::QualifiedCode(image, endpoint::kTimestamp), "one-bit code changes reject");
    image[region.rva] ^= 1;
  }
  const auto* turing = architecture::GetProfile(mfgunlock::Architecture::kTuring);
  for (const auto& region : endpoint::kPayloads) {
    const std::span<const unsigned char> source(image.data() + region.rva, region.bytes);
    ptx::Plan plan;
    std::string reason;
    Check(ptx::Retarget(source, plan, reason, *turing) == ptx::Result::kRetargeted,
          "every endpoint payload retargets");
    Check(endpoint::PreparedPayload(source, plan.replacement, region), "prepared SM75 payload");
    Check(!endpoint::PreparedPayload(source, source, region), "original SM89 payload not ready");
  }

  Reset();
  architecture::Configure(mfgunlock::Architecture::kTuring);
  Check(provider::PrepareProvider(image.data()), "real Turing provider transaction");
  Check(provider::PreparedProviderCount() == 1, "Turing provider ready");
  const auto writes = mock::protection_calls;
  for (const auto& site : endpoint::kSelectors)
    Check(image[site.rva] == 0x90 && image[site.rva + 1] == 0x90, "PTX branch selected");
  for (const auto& region : endpoint::kPayloads)
    Check(endpoint::PreparedPayload({before.data() + region.rva, region.bytes},
                                   {image.data() + region.rva, region.bytes}, region),
          "published payload is SM75");
  const auto& patches = provider::internal::g_providers.front().patches;
  Check(patches.back().after == 0x60, "Turing admission gate is final write");
  Check(provider::PrepareProvider(image.data()) && mock::protection_calls == writes,
        "idempotent prepare performs no writes");
  image[endpoint::kSelectors.front().rva] = 0x7e;
  Check(provider::PreparedProviderCount() == 0 && !provider::PrepareProvider(image.data()),
        "altered endpoint selector invalidates readiness and idempotent reuse");
  image[endpoint::kSelectors.front().rva] = 0x90;
  provider::Restore();
  Check(image == before, "Turing payloads, selectors and gate restore byte-exactly");

  Reset();
  mock::fail_protection_call = writes - 7;
  Check(!provider::PrepareProvider(image.data()), "mid-selector write failure rejects");
  Check(image == before && provider::PreparedProviderCount() == 0,
        "mid-selector rollback restores full image and never reports ready");

  Reset();
  image[endpoint::kCode.front().rva] ^= 1;
  const auto bad_code = image;
  Check(!provider::PrepareProvider(image.data()) && mock::protection_calls == 0 && image == bad_code,
        "unknown code makes zero writes");
  std::copy(before.begin(), before.end(), image.begin());

  Reset();
  image[endpoint::kPayloads.front().rva + 44] = 75;
  const auto incomplete = image;
  Check(!provider::PrepareProvider(image.data()) && mock::protection_calls == 0 && image == incomplete,
        "missing/unqualified endpoint payload prevents all writes");
  std::copy(before.begin(), before.end(), image.begin());

  Reset();
  image[0x64abe8] ^= 1;
  const auto wrong_loader = image;
  Check(!provider::PrepareProvider(image.data()) && mock::protection_calls == 0 && image == wrong_loader,
        "wrong network vtable loader prevents all writes");
  std::copy(before.begin(), before.end(), image.begin());

  // Every allowed container is a guard, including the 31 non-endpoint entries.
  for (const auto mode : {mfgunlock::Architecture::kTuring, mfgunlock::Architecture::kAmpere}) {
    architecture::Configure(mode);
    for (const auto& region : mfgunlock::profiles::kRetargetPayloads) {
      Reset();
      const size_t at = region.rva + region.bytes - 1;
      image[at] ^= 1;
      const auto rejected_image = image;
      Check(!provider::PrepareProvider(image.data()) && mock::protection_calls == 0 &&
                image == rejected_image,
            "one unqualified container prevents any write on both retarget backends");
      image[at] ^= 1;
    }
  }

  Reset();
  architecture::Configure(mfgunlock::Architecture::kAmpere);
  Check(provider::PrepareProvider(image.data()), "Ampere preparation regression");
  for (const auto& region : endpoint::kCode)
    Check(std::equal(image.begin() + region.rva, image.begin() + region.rva + region.bytes,
                     before.begin() + region.rva), "Ampere code is untouched");
  provider::Restore();
  Check(image == before, "Ampere restore byte-exact");

  Reset();
  architecture::Configure(mfgunlock::Architecture::kAda);
  Check(provider::QualifiedNativeInventory(image.data()), "Ada exact original inventory qualifies without retarget");
  std::vector<unsigned char*> comparison_sites;
  Check(provider::FindQualifiedMfgGates(image.data(), comparison_sites) && comparison_sites.size() == 2,
        "both exact MFG gate contexts qualify");
  image[mfgunlock::profiles::kMfgGates[0].code.rva] ^= 1;
  Check(!provider::FindQualifiedMfgGates(image.data(), comparison_sites), "mutated MFG context rejects");
  image[mfgunlock::profiles::kMfgGates[0].code.rva] ^= 1;
  image[mfgunlock::profiles::kRetargetPayloads[0].rva] ^= 1;
  Check(!provider::QualifiedNativeInventory(image.data()), "Ada partial inventory rejects without writes");
  image[mfgunlock::profiles::kRetargetPayloads[0].rva] ^= 1;
  Check(!provider::PrepareProvider(image.data()) && image == before && mock::protection_calls == 0,
        "Ada remains native/unmodified");

  namespace temporal = mfgunlock::blackwelltemporal;
  const std::array quality_cases{
      std::pair{temporal::BoundaryArtifactMode::kOff, false},
      std::pair{temporal::BoundaryArtifactMode::kOff, true},
      std::pair{temporal::BoundaryArtifactMode::kBalanced, true},
      std::pair{temporal::BoundaryArtifactMode::kAggressive, true}};
  for (const auto& [mode, scatter] : quality_cases) {
    Reset();
    temporal::Plan plan;
    temporal::Result result;
    std::vector<mfgunlock::validatedwarp::Redirect> redirects;
    std::string version;
    Check(temporal::Prepare(image.data(), scatter, mode, plan, result, version, 89),
          "real provider prepares native SM89 temporal plan");
    Check(plan.ready && plan.target_sm == 89 && plan.intermediate_scatter == scatter,
          "native SM89 temporal plan preserves requested quality mode");
    Check(temporal::Commit(plan, redirects, result),
          "real provider commits native SM89 temporal plan");
    Check(result.applied && result.motion_vector && result.inpaint && result.inpaint_decision &&
              result.intermediate_scatter == scatter && result.boundary_mode == mode,
          "native SM89 temporal result reports exact applied roles/mode");
    Check(redirects.size() == 3, "native SM89 temporal redirects all three roles");
    Check(temporal::Restore(redirects) && image == before,
          "native SM89 temporal redirect restores provider byte-exactly");
  }

  Reset();
  std::vector<mfgunlock::validatedwarp::Redirect> warp_redirects;
  mfgunlock::validatedwarp::Result warp_result;
  std::string warp_version;
  Check(mfgunlock::validatedwarp::Apply(
            image.data(), warp_redirects, warp_result, warp_version,
            mfgunlock::validatedwarp::Mode::kValidatedWarp, nullptr, 89),
        "real provider applies native SM89 Validated Warp");
  Check(warp_result.applied && warp_redirects.size() == 1,
        "native SM89 Validated Warp publishes one qualified redirect");
  Check(mfgunlock::validatedwarp::Restore(warp_redirects) && image == before,
        "native SM89 Validated Warp restores provider byte-exactly");

  for (const auto mode : {mfgunlock::Architecture::kTuring, mfgunlock::Architecture::kAmpere,
                          mfgunlock::Architecture::kAda}) {
    Reset();
    architecture::Configure(mode);
    Check(mode == mfgunlock::Architecture::kAda ? provider::QualifiedNativeInventory(image.data())
                                              : provider::PrepareProvider(image.data()),
          "comparison transaction starts from correct native/retargeted provider");
    const auto prepared = image;
    std::vector<unsigned char*> sites;
    std::vector<provider::internal::BytePatch> undo;
    Check(provider::FindQualifiedMfgGates(image.data(), sites) && sites.size() == 2,
          "exact comparison contexts still qualify after provider preparation");
    Check(provider::ApplyMfgComparisons(sites, undo) && undo.size() == 2,
          "all architectures publish both comparison sites with undo ownership");
    Check(provider::RestoreMfgComparisons(undo) && undo.empty() && image == prepared,
          "comparison restore preserves prepared provider bytes");
    provider::Restore();
    Check(image == before, "combined comparison and provider restore is byte-exact");
  }

  Reset();
  std::cout << "Exact provider: endpoint SM75 + native SM89 temporal/ISR/Boundary/Warp PASS\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    UnitChecks();
    if (argc == 2) FileChecks(argv[1]);
    else Check(argc == 1, "usage: endpoint_backend_test [nvngx_dlssg.dll]");
    std::cout << "endpoint backend: " << g_checks << " checks PASS (no GPU/vendor execution)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    for (const auto& line : reshade::log::lines) std::cerr << line << '\n';
    return 1;
  }
}
