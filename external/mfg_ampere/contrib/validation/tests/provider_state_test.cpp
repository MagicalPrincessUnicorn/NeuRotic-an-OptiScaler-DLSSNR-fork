// SPDX-License-Identifier: MIT
// Runs the production provider registry against simulated Windows memory and
// loader operations. No NVIDIA DLL is loaded and no GPU code is executed.

#include "../../../src/addons/mfgunlock/provider.hpp"
#include "../../../src/addons/mfgunlock/capability_policy.hpp"

#include <iostream>
#include <stdexcept>

namespace provider = mfgunlock::provider;
namespace policy = mfgunlock::policy;
namespace architecture = mfgunlock::architecture;
namespace mock = provider_mock;

using Bytes = std::vector<unsigned char>;

namespace {

unsigned int g_checks = 0;

void Check(bool condition, const char* reason) {
  ++g_checks;
  if (!condition) throw std::runtime_error(reason);
}

template <class T>
void Put(Bytes& bytes, size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

struct FixtureProfile {
  std::array<mfgunlock::profiles::Region, 1> regions{};
  mfgunlock::profiles::ProviderProfile profile = mfgunlock::profiles::kDlssg31091;
};
std::map<HMODULE, FixtureProfile> g_fixture_profiles;

bool PrepareFixture(HMODULE module) {
  std::vector<mfgunlock::profiles::ProviderProfile> entries;
  for (auto& [handle, fixture] : g_fixture_profiles) {
    (void)handle;
    fixture.profile.retarget_payloads = fixture.regions;
    entries.push_back(fixture.profile);
  }
  return provider::PrepareProviderImpl(module, entries);
}

struct Image {
  Bytes bytes = Bytes(0x3000);
  HMODULE module = bytes.data();

  explicit Image(unsigned int id, bool valid_gate = true) {
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    Put(bytes, 0, dos);

    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.TimeDateStamp = id;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(bytes.size());
    nt.OptionalHeader.SizeOfHeaders = 0x400;
    Put(bytes, 0x80, nt);

    IMAGE_SECTION_HEADER text{};
    text.VirtualAddress = 0x1000;
    text.Misc.VirtualSize = 0x1000;
    text.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

    IMAGE_SECTION_HEADER data{};
    data.VirtualAddress = 0x2000;
    data.Misc.VirtualSize = 0x1000;
    data.Characteristics = IMAGE_SCN_MEM_READ;

    Put(bytes, 0x80 + sizeof(nt), text);
    Put(bytes, 0x80 + sizeof(nt) + sizeof(text), data);

    const unsigned char gate[] = {0xb8, 0x90, 1, 0, 0, 0xc3};
    std::memcpy(bytes.data() + 0x1000, gate, sizeof(gate));
    if (!valid_gate) bytes[0x1001] = 0xb0;

    // One minimal compressed PTX container. This is the same validated layout
    // used by ptx_retarget_test.
    std::string ptx =
        ".version 8.7\n"
        ".target sm_89\n"
        ".address_size 64\n"
        ".visible .entry test() { ret; }\n";
    ptx.push_back('\0');

    Bytes payload{0xf0, static_cast<unsigned char>(ptx.size() - 15)};
    payload.insert(payload.end(), ptx.begin(), ptx.end());

    Bytes fatbin(80 + ((payload.size() + 7) & ~size_t{7}));
    Put<uint32_t>(fatbin, 0, mfgunlock::fatbin::kMagic);
    Put<uint16_t>(fatbin, 4, 1);
    Put<uint16_t>(fatbin, 6, 16);
    Put<uint64_t>(fatbin, 8, fatbin.size() - 16);
    Put<uint16_t>(fatbin, 16, 1);
    Put<uint16_t>(fatbin, 18, 0x101);
    Put<uint32_t>(fatbin, 20, 64);
    Put<uint64_t>(fatbin, 24, fatbin.size() - 80);
    Put<uint32_t>(fatbin, 32, static_cast<uint32_t>(payload.size()));
    Put<uint32_t>(fatbin, 44, 89);
    Put<uint64_t>(fatbin, 56, 0x2041);
    Put<uint64_t>(fatbin, 72, ptx.size());
    std::copy(payload.begin(), payload.end(), fatbin.begin() + 80);
    std::copy(fatbin.begin(), fatbin.end(), bytes.begin() + 0x2000);
    auto& fixture = g_fixture_profiles[module];
    fixture.regions[0] = {0x2000, static_cast<uint32_t>(fatbin.size()),
                          mfgunlock::profiles::Fingerprint(fatbin)};
    fixture.profile.id = "synthetic-registry-test";
    fixture.profile.timestamp = id;
    fixture.profile.image_size = static_cast<uint32_t>(bytes.size());


    std::memcpy(bytes.data() + 0x2f00, "dlfg_kernel", 12);
    mock::regions.push_back({module, bytes.size()});
    mock::paths[module] = "X:\\fixture\\provider_" + std::to_string(id) + "\\nvngx_dlssg.dll";
    mock::exports[{module, "NVSDK_NGX_GetGPUArchitecture"}] =
        reinterpret_cast<FARPROC>(bytes.data() + 0x1000);
    mock::exports[{module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl"}] =
        reinterpret_cast<FARPROC>(bytes.data() + 0x1100);
  }

  void Unmap() {
    std::erase_if(mock::regions, [this](const auto& region) { return region.base == module; });
  }
};

void Reset() {
  provider::internal::g_providers.clear();
  provider::internal::g_rejected.clear();
  provider::internal::g_registry_failed = false;
  provider::internal::g_comparison_restore_failed = false;

  mfgunlock::g_enabled = true;
  mfgunlock::architecture::Configure(mfgunlock::Architecture::kAmpere);
  provider::g_create_seen = false;

  g_fixture_profiles.clear();
  mock::regions.clear();
  mock::exports.clear();
  mock::paths.clear();
  mock::retain_ok = true;
  mock::force_lock_busy = false;
  mock::protection_calls = 0;
  mock::fail_protection_call = 0;
  reshade::log::lines.clear();
}

bool Allowed() {
  return policy::CanRelaxRequirements({true, true, provider::PreparedProviderCount(),
                                       policy::kNgxSuccess, policy::kDlssGFeatureId,
                                       policy::kAdapterUnsupported, architecture::kAdaArchitecture},
                                      mfgunlock::architecture::ActiveProfile());
}

}  // namespace

int main() {
  try {
    Reset();
    // Same version and valid-looking architecture exports are insufficient.
    for (const char* name : {"nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssnr.dll",
                             "nvngx_deepdvc.dll", "nvngx_dlssg.dll", "renamed.dll"}) {
      Image sibling(0x6a986031);
      mock::paths[sibling.module] = std::string("X:\\fixture\\") + name;
      std::memset(sibling.bytes.data() + 0x2f00, 0, 12);
      const auto before = sibling.bytes;
      Check(!provider::IsDlssgProvider(sibling.module), "sibling identity rejected regardless of filename");
      Check(!PrepareFixture(sibling.module), "sibling preparation rejected");
      Check(sibling.bytes == before && mock::protection_calls == 0,
            "same-version sibling is never written");
      sibling.Unmap();
    }
    Reset();
    Image renamed(0x6a986031);
    mock::paths[renamed.module] = "X:\\fixture\\renamed-provider.dll";
    Check(provider::IsDlssgProvider(renamed.module), "genuine renamed provider identity accepted");
    Check(!provider::PrepareProvider(renamed.module) && mock::protection_calls == 0,
          "production registry rejects unknown image even with valid PTX and exports");
    Check(PrepareFixture(renamed.module), "explicit exact fixture inventory qualified");
    provider::Restore();
    Reset();
    Image modern(0x6a986031);
    std::memset(modern.bytes.data() + 0x2f00, 0, 128);
    std::strcpy(reinterpret_cast<char*>(modern.bytes.data() + 0x2f00), "Kernel_EstimateIntermMvecsScatter");
    Check(!provider::IsDlssgProvider(modern.module), "one modern symbol is not enough");
    std::strcpy(reinterpret_cast<char*>(modern.bytes.data() + 0x2f40), "Kernel_OutputPull");
    Check(provider::IsDlssgProvider(modern.module), "renamed modern DLSS-G kernel family accepted");
    Check(PrepareFixture(modern.module), "modern identity passes existing provider qualification");
    provider::Restore();
    Reset();
    Image ready(1);
    const auto original = ready.bytes;
    Check(PrepareFixture(ready.module), "initial provider preparation");
    Check(provider::PreparedProviderCount() == 1 && Allowed(),
          "single prepared provider admitted");
    Check(ready.bytes[0x1001] == 0x70, "minimum arch written last");

    auto writes = mock::protection_calls;
    Check(PrepareFixture(ready.module) && mock::protection_calls == writes,
          "preparation idempotent");

    Image rejected(2, false);
    Check(!PrepareFixture(rejected.module), "unrecognized genuine provider rejected");
    Check(provider::GetProviderStatus().ready == 1 && provider::GetProviderStatus().blocked == 1 &&
              !Allowed(),
          "live rejected provider vetoes");
    Check(!provider::internal::g_rejected.front().path.empty() &&
              !provider::internal::g_rejected.front().reason.empty(),
          "rejection identifies module and reason");

    rejected.Unmap();
    Check(provider::GetProviderStatus().ready == 1 && provider::GetProviderStatus().blocked == 0 &&
              Allowed(),
          "expired rejection does not poison ready provider");

    // A historical rejection must not veto a provider that is currently qualified.
    const unsigned int old_count = provider::internal::g_rejected.empty() ? 1u : 0u;
    Check(old_count == 0 && provider::PreparedProviderCount() == 1,
          "historical global veto is distinguishable from the live registry");
    Check(PrepareFixture(ready.module),
          "historical rejection cannot block idempotent reuse");

    auto tagged = reinterpret_cast<HMODULE>(reinterpret_cast<uintptr_t>(ready.module) | 1u);
    writes = mock::protection_calls;
    Check(!PrepareFixture(tagged) && mock::protection_calls == writes && Allowed(),
          "tagged resource ignored without writes");

    Image data(3);
    for (auto& region : mock::regions) {
      if (region.base == data.module) region.type = MEM_MAPPED;
    }
    Check(!PrepareFixture(data.module) && Allowed(),
          "data mapping ignored without global veto");

    Image nonprovider(4);
    mock::exports.erase({nonprovider.module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl"});
    mock::exports.erase({nonprovider.module, "NVSDK_NGX_GetGPUArchitecture"});
    Check(!PrepareFixture(nonprovider.module) && Allowed(),
          "filename-only false positive ignored");

    Image forwarded(5);
    mock::exports[{forwarded.module, "NVSDK_NGX_GetGPUArchitecture"}] =
        reinterpret_cast<FARPROC>(ready.bytes.data() + 0x1000);
    Check(!PrepareFixture(forwarded.module) && !Allowed(),
          "unresolved forwarded ABI blocks, never patched");
    forwarded.Unmap();

    Image incomplete(13);
    mock::exports.erase({incomplete.module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl"});
    Check(!PrepareFixture(incomplete.module) && !Allowed(),
          "partial provider ABI is a real blocker");

    auto* reused_header = reinterpret_cast<IMAGE_NT_HEADERS64*>(incomplete.bytes.data() + 0x80);
    ++reused_header->FileHeader.TimeDateStamp;
    Check(provider::GetProviderStatus().ready == 1 && provider::GetProviderStatus().blocked == 1 &&
              !Allowed(),
          "new image at rejected address cannot authorize the other ready provider");
    incomplete.Unmap();

    Image retry(6);
    mock::retain_ok = false;
    Check(!PrepareFixture(retry.module) && !Allowed(),
          "unretained real provider blocks");
    mock::retain_ok = true;
    Check(PrepareFixture(retry.module), "same provider can be fully requalified");
    Check(provider::GetProviderStatus().blocked == 0 && provider::GetProviderStatus().ready == 2 &&
              !Allowed(),
          "two prepared providers remain ambiguous");
    Check(provider::internal::g_rejected.size() == 3,
          "only successful transaction retires its own rejection");

    provider::Restore();
    Check(ready.bytes == original, "restore original provider bytes");

    Reset();
    Image failing(7);
    mock::fail_protection_call = 2;
    const auto clean = failing.bytes;
    Check(!PrepareFixture(failing.module),
          "post-write protection failure rejects transaction");
    Check(failing.bytes == clean && provider::GetProviderStatus().blocked == 1 && !Allowed(),
          "rollback verified and failed transaction blocks");
    Check(!provider::internal::g_providers.front().failure.empty(),
          "transaction failure has diagnostic");

    Reset();
    Image restore_retry(71);
    Check(PrepareFixture(restore_retry.module), "prepare restore retry fixture");
    mock::fail_protection_call = mock::protection_calls + 1;
    provider::Restore();
    Check(!provider::internal::g_providers.empty() && provider::GetProviderStatus().failed,
          "failed provider restore retains patches and failure state");
    mock::fail_protection_call = 0;
    provider::Restore();
    Check(provider::internal::g_providers.empty() && !provider::GetProviderStatus().failed,
          "successful retry clears provider rollback state");

    Reset();
    Image mismatch(8);
    Check(PrepareFixture(mismatch.module), "prepare for invalidation test");
    mismatch.bytes[0x1001] = 0x90;
    Check(provider::GetProviderStatus().blocked == 1 && !Allowed(),
          "external gate modification cannot pass");

    Reset();
    Image reuse(9, false);
    Check(!PrepareFixture(reuse.module), "identity old generation rejected");
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reuse.bytes.data() + 0x80);
    ++nt->FileHeader.TimeDateStamp;
    g_fixture_profiles[reuse.module].profile.timestamp = nt->FileHeader.TimeDateStamp;
    Check(provider::GetProviderStatus().blocked == 1 && !Allowed(),
          "reused address stays unqualified");
    reuse.bytes[0x1001] = 0x90;
    Check(PrepareFixture(reuse.module) && Allowed(),
          "remapped candidate must pass full preparation");
    Check(provider::internal::g_rejected.empty(),
          "successful new generation clears old rejection");

    mock::force_lock_busy = true;
    Check(provider::GetProviderStatus().busy && !Allowed(), "registry lock contention fails closed");
    mock::force_lock_busy = false;

    Reset();
    Image bad(10);
    bad.bytes[0] = 0;
    Check(!PrepareFixture(bad.module) && provider::GetProviderStatus().failed && !Allowed(),
          "malformed executable never silently ignored");

    Reset();
    Image late(11);
    provider::g_create_seen = true;
    Check(!PrepareFixture(late.module) && !Allowed(),
          "new provider after Create refused");

    Reset();
    Image disabled(12);
    mfgunlock::g_enabled = false;
    Check(!PrepareFixture(disabled.module) && mock::protection_calls == 0,
          "disabled backport path makes no writes");

    for (auto selected : {mfgunlock::Architecture::kAmpere, mfgunlock::Architecture::kTuring}) {
      Reset();
      mfgunlock::architecture::Configure(selected);
      const auto* profile = mfgunlock::architecture::ActiveProfile();
      Image target(100);
      const auto before = target.bytes;
      if (selected == mfgunlock::Architecture::kTuring) {
        Check(!PrepareFixture(target.module) && target.bytes == before &&
                  mock::protection_calls == 0,
              "unknown Turing network layout rejects before any write");
        continue;
      }
      Check(PrepareFixture(target.module), "explicit profile prepares provider");
      Check(target.bytes[0x1001] == static_cast<unsigned char>(profile->native_arch),
            "minimum architecture follows profile");
      Check(mfgunlock::fatbin::ReadU32(target.bytes.data() + 0x2000 + 44) == profile->target_sm,
            "fatbin SM follows profile");
      target.bytes[0x1100] = 0xb0;
      target.bytes[0x1101] = 0xb0;
      unsigned char* sites[] = {target.bytes.data() + 0x1100, target.bytes.data() + 0x1101};
      std::vector<provider::internal::BytePatch> comparison_undo;
      Check(provider::ApplyMfgComparisons(sites, comparison_undo), "profile MFG comparison transaction");
      Check(*sites[0] == static_cast<unsigned char>(profile->native_arch) &&
                *sites[1] == static_cast<unsigned char>(profile->native_arch),
            "MFG comparisons use native target, not spoofed Ada");
      Check(provider::RestoreMfgComparisons(comparison_undo) && comparison_undo.empty() &&
                *sites[0] == 0xb0 && *sites[1] == 0xb0,
            "comparison restore preserves original bytes/protection and drains ownership");
      // Synthetic test image did not originally contain real comparison sites.
      *sites[0] = before[0x1100];
      *sites[1] = before[0x1101];
      provider::Restore();
      Check(target.bytes == before, "profile restore is byte exact");
    }

    Reset();
    mfgunlock::architecture::Configure(mfgunlock::Architecture::kAda);
    Image ada(101);
    const auto ada_before = ada.bytes;
    Check(!PrepareFixture(ada.module) && ada.bytes == ada_before &&
              mock::protection_calls == 0,
          "Ada skips backport preparation");

    Reset();
    mfgunlock::architecture::Configure(mfgunlock::Architecture::kAuto);
    Image automatic(102);
    const auto automatic_before = automatic.bytes;
    Check(!PrepareFixture(automatic.module) && automatic.bytes == automatic_before,
          "pending Auto writes nothing");
    mfgunlock::architecture::ResolveAuto(0x10de, 0x160, 2);
    Check(!PrepareFixture(automatic.module) && automatic.bytes == automatic_before &&
              mock::protection_calls == 0,
          "resolved Turing Auto requires an exact network layout");
    provider::Restore();
    Check(automatic.bytes == automatic_before, "Auto provider restored");

    Reset();
    mfgunlock::architecture::Configure(mfgunlock::Architecture::kAda);
    Image gates(200);
    gates.bytes[0x1100] = 0xb0;
    gates.bytes[0x1101] = 0xb0;
    unsigned char* gate_sites[] = {gates.bytes.data() + 0x1100, gates.bytes.data() + 0x1101};
    std::vector<provider::internal::BytePatch> gate_undo;
    Check(provider::ApplyMfgComparisons(gate_sites, gate_undo), "Ada gates use checked transaction too");
    mock::fail_protection_call = mock::protection_calls + 1;
    Check(!provider::RestoreMfgComparisons(gate_undo) && !gate_undo.empty(),
          "failed comparison restore keeps retryable undo metadata");
    mock::fail_protection_call = 0;
    Check(provider::RestoreMfgComparisons(gate_undo) && gate_undo.empty() &&
              *gate_sites[0] == 0xb0 && *gate_sites[1] == 0xb0,
          "comparison restore retry succeeds");

    Reset();
    Image inspection(201);
    const auto retainedBefore=mock::retains, releasedBefore=mock::module_releases;
    (void)provider::IsDlssgProvider(inspection.module);
    Check(mock::retains==retainedBefore+1 && mock::module_releases==releasedBefore+1,
          "candidate inspection holds and releases a temporary module reference");
    mock::fail_read=true;
    Check(!provider::IsDlssgProvider(inspection.module), "protection/read failure refuses candidate");
    mock::fail_read=false;mock::partial_read=true;
    Check(!provider::IsDlssgProvider(inspection.module), "partial image read refuses candidate");
    mock::partial_read=false;mock::retain_ok=false;
    Check(!provider::IsDlssgProvider(inspection.module), "unretained candidate is not inspected");
    mock::retain_ok=true;
    std::cout << "provider state: " << g_checks
              << " checks PASS (real provider.hpp; simulated Windows memory)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
