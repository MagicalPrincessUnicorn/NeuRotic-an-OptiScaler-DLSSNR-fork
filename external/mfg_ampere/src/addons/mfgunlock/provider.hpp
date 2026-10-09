/*
 * Native DLSS-G provider preparation for the selected architecture.
 * SPDX-License-Identifier: MIT
 *
 * Retargets compatible sm_89 PTX in the mapped provider, hides the matching
 * sm_89 cubins, and lowers the provider architecture gate to the selected GPU.
 */
#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "./reshade_compat.hpp"

#include "./ptx_retarget.hpp"
#include "./endpoint_backend.hpp"

namespace mfgunlock::provider {

inline std::atomic_bool g_create_seen{false};

inline void Log(const std::string& message, bool warning = false) {
  reshade::log::message(warning ? reshade::log::level::warning : reshade::log::level::info,
                        ("mfgunlock: " + message).c_str());
}

namespace internal {

struct BytePatch {
  unsigned char* address;
  unsigned char before;
  unsigned char after;
  DWORD original_protection = 0;
};

struct ImageIdentity {
  DWORD timestamp = 0;
  DWORD image_bytes = 0;
  bool operator==(const ImageIdentity&) const = default;
};

struct Provider {
  HMODULE module;
  std::vector<BytePatch> patches;
  bool ready = false;
  unsigned fatbins = 0;
  unsigned hidden_cubins = 0;
  ImageIdentity identity;
  std::string path;
  std::string failure;
  Architecture architecture = Architecture::kUnknown;
  profiles::Qualification qualification = profiles::Qualification::kUnknown;
};

struct Rejection {
  HMODULE module;
  ImageIdentity identity;
  std::string path;
  std::string reason;
};
inline SRWLOCK g_lock = SRWLOCK_INIT;
inline std::vector<Provider> g_providers;
inline std::vector<Rejection> g_rejected;
inline std::atomic_bool g_registry_failed{false};
inline std::atomic_bool g_comparison_restore_failed{false};

inline bool IsReadable(const void* pointer, size_t bytes, HMODULE allocation) {
  uintptr_t at = reinterpret_cast<uintptr_t>(pointer);
  if (bytes > std::numeric_limits<uintptr_t>::max() - at) return false;
  const uintptr_t end = at + bytes;
  while (at < end) {
    MEMORY_BASIC_INFORMATION memory = {};
    if (!VirtualQuery(reinterpret_cast<void*>(at), &memory, sizeof(memory)) ||
        memory.AllocationBase != allocation || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
      return false;
    }
    const DWORD protection = memory.Protect & 0xFF;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) {
      return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    if (memory.RegionSize > std::numeric_limits<uintptr_t>::max() - base) return false;
    const auto next = base + memory.RegionSize;
    if (next <= at) return false;
    at = std::min(next, end);
  }
  return true;
}

inline bool EndpointSelectorsApplied(const Provider& provider) {
  if (provider.architecture != Architecture::kTuring) return true;
  const auto* base = reinterpret_cast<const unsigned char*>(provider.module);
  for (const auto& site : endpoint::kSelectors) {
    if (!IsReadable(base + site.rva, 2, provider.module) ||
        base[site.rva] != 0x90 || base[site.rva + 1] != 0x90) return false;
  }
  return true;
}

// A LoadLibrary name match is only a discovery hint. Resource handles may be
// tagged, and failed candidates can disappear after an NGX inspection load.
// Neither is evidence of a second executable provider.
inline bool IsImageMapping(HMODULE module) {
  if (!module || (reinterpret_cast<uintptr_t>(module) & 3u) != 0) return false;
  MEMORY_BASIC_INFORMATION memory = {};
  return VirtualQuery(module, &memory, sizeof(memory)) &&
         memory.AllocationBase == module && memory.State == MEM_COMMIT &&
         memory.Type == MEM_IMAGE && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}
class ModuleReference {
  HMODULE held_ = nullptr;
 public:
  explicit ModuleReference(HMODULE candidate) {
    if (!candidate || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(candidate), &held_) || held_ != candidate) {
      if (held_) FreeLibrary(held_);
      held_ = nullptr;
    }
  }
  ~ModuleReference() { if (held_) FreeLibrary(held_); }
  explicit operator bool() const { return held_ != nullptr; }
  ModuleReference(const ModuleReference&) = delete;
  ModuleReference& operator=(const ModuleReference&) = delete;
};
inline bool ReadImageBytes(const void* source, void* destination, size_t bytes) {
  SIZE_T copied = 0;
  return ReadProcessMemory(GetCurrentProcess(), source, destination, bytes, &copied) && copied == bytes;
}
inline bool ReadIdentity(HMODULE module, ImageIdentity& identity) {
  if (!IsImageMapping(module) || !IsReadable(module, sizeof(IMAGE_DOS_HEADER), module)) {
    return false;
  }
  const auto* base = reinterpret_cast<const unsigned char*>(module);
  IMAGE_DOS_HEADER dos = {};
  if (!ReadImageBytes(base, &dos, sizeof(dos))) return false;
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 || dos.e_lfanew > 1024 * 1024)
    return false;
  const auto* location = base + dos.e_lfanew;
  if (!IsReadable(location, sizeof(IMAGE_NT_HEADERS64), module)) return false;
  IMAGE_NT_HEADERS64 nt = {};
  if (!ReadImageBytes(location, &nt, sizeof(nt))) return false;
  if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
  identity = {nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage};
  return identity.image_bytes != 0;
}
inline bool IsCurrent(HMODULE module, const ImageIdentity& expected) {
  ImageIdentity actual;
  return ReadIdentity(module, actual) && actual == expected;
}
inline std::string ModulePath(HMODULE module) {
  std::string path(32768, '\0');
  const auto length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return "<path unavailable>";
  path.resize(length);
  return path;
}

struct Image {
  unsigned char* base = nullptr;
  size_t bytes = 0;
  std::vector<IMAGE_SECTION_HEADER> sections;
};
inline bool InspectImage(HMODULE module, Image& image) {
  auto* base = reinterpret_cast<unsigned char*>(module);
  if (!IsReadable(base, sizeof(IMAGE_DOS_HEADER), module)) return false;
  IMAGE_DOS_HEADER dos;
  if (!ReadImageBytes(base, &dos, sizeof(dos))) return false;
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 || dos.e_lfanew > 1024 * 1024)
    return false;
  auto* location = base + dos.e_lfanew;
  if (!IsReadable(location, sizeof(IMAGE_NT_HEADERS64), module)) return false;
  IMAGE_NT_HEADERS64 nt;
  if (!ReadImageBytes(location, &nt, sizeof(nt))) return false;
  if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
      nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96 ||
      nt.OptionalHeader.SizeOfImage == 0 || nt.OptionalHeader.SizeOfImage > 512 * 1024 * 1024)
    return false;
  const size_t table_offset = static_cast<size_t>(dos.e_lfanew) + sizeof(nt);
  const size_t table_bytes = nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
  if (table_offset > nt.OptionalHeader.SizeOfHeaders ||
      table_bytes > nt.OptionalHeader.SizeOfHeaders - table_offset ||
      !IsReadable(base + table_offset, table_bytes, module)) return false;
  if (nt.OptionalHeader.SizeOfHeaders > nt.OptionalHeader.SizeOfImage) return false;
  image.base = base;
  image.bytes = nt.OptionalHeader.SizeOfImage;
  image.sections.resize(nt.FileHeader.NumberOfSections);
  if (!ReadImageBytes(base + table_offset, image.sections.data(), table_bytes)) return false;
  std::vector<std::pair<size_t, size_t>> ranges;
  for (const auto& section : image.sections) {
    if (section.VirtualAddress > image.bytes ||
        section.Misc.VirtualSize > image.bytes - section.VirtualAddress) {
      return false;
    }
    if (section.Misc.VirtualSize != 0) {
      ranges.emplace_back(section.VirtualAddress,
                          section.VirtualAddress + section.Misc.VirtualSize);
    }
  }
  std::sort(ranges.begin(), ranges.end());
  for (size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i].first < ranges[i - 1].second) return false;
  }
  return true;
}

inline bool OwnCodeExport(const Image& image, HMODULE module, const char* name) {
  const auto* address = reinterpret_cast<const unsigned char*>(GetProcAddress(module, name));
  if (!address || !IsReadable(address, 1, module)) return false;
  const auto value = reinterpret_cast<uintptr_t>(address);
  for (const auto& section : image.sections) {
    const auto begin = reinterpret_cast<uintptr_t>(image.base + section.VirtualAddress);
    if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && value >= begin &&
        value - begin < section.Misc.VirtualSize) {
      return true;
    }
  }
  return false;
}

inline bool IsDlssgImage(const Image& image, HMODULE module) {
  if (!OwnCodeExport(image, module, "NVSDK_NGX_GetGPUArchitecture") ||
      (!OwnCodeExport(image, module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") &&
       !OwnCodeExport(image, module, "NVSDK_NGX_VULKAN_PopulateDeviceParameters_Impl"))) {
    return false;
  }
  constexpr const char* kMarkers[] = {
      "dlfg_kernel", "Kernel_EstimateIntermMvecsScatter", "Kernel_OutputPull"};
  bool seen[3]{};
  for (const auto& section : image.sections) {
    if (!(section.Characteristics & IMAGE_SCN_MEM_READ)) continue;
    const auto* begin = image.base + section.VirtualAddress;
    const size_t bytes = section.Misc.VirtualSize;
    if (!IsReadable(begin, bytes, module)) return false;
    // Read bounded chunks with overlap; a protection change after VirtualQuery
    // produces a failed scan instead of an access violation.
    std::vector<unsigned char> copy(65536 + 128);
    for (size_t offset = 0; offset < bytes; offset += 65536) {
      const auto count = std::min(copy.size(), bytes - offset);
      if (!ReadImageBytes(begin + offset, copy.data(), count)) return false;
      for (size_t index = 0; index < 3; ++index) {
        const auto length = std::strlen(kMarkers[index]);
        if (count >= length && std::search(copy.data(), copy.data() + count,
              kMarkers[index], kMarkers[index] + length) != copy.data() + count)
          seen[index] = true;
      }
    }
  }
  // The 310.9 family uses renamed kernel symbols. Neither a filename nor one
  // generic interpolation symbol alone identifies a modern DLSS-G provider.
  return seen[0] || (seen[1] && seen[2]);
}

// Writes never span protection regions: only the changed bytes are stored.
// Both protection restoration and read-back are checked. Undo is registered
// before calling this, including the case where the write succeeds but restore fails.
inline bool WriteByte(BytePatch& patch, bool restore) {
  const auto expected = restore ? patch.after : patch.before;
  const auto desired = restore ? patch.before : patch.after;
  if (*patch.address != expected && !(restore && *patch.address == desired)) return false;

  DWORD old_protection = 0;
  DWORD ignored = 0;
  if (!VirtualProtect(patch.address, 1, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
  if (patch.original_protection == 0) patch.original_protection = old_protection;

  // Ownership can change while VirtualProtect is running. Never overwrite
  // a competing patch, including during rollback.
  auto comparand = expected;
  std::atomic_ref<unsigned char> slot(*patch.address);
  const bool owned = slot.compare_exchange_strong(comparand, desired) ||
      (restore && comparand == desired);
  const bool protected_again =
      VirtualProtect(patch.address, 1, patch.original_protection, &ignored) != FALSE;
  const bool flushed = FlushInstructionCache(GetCurrentProcess(), patch.address, 1) != FALSE;
  return owned && protected_again && flushed && slot.load() == desired;
}
inline bool Undo(Provider& provider) {
  bool ok = true;
  for (auto it = provider.patches.rbegin(); it != provider.patches.rend(); ++it) {
    if (!IsReadable(it->address, 1, provider.module)) {
      ok = false;
      continue;
    }
    if (*it->address == it->before && it->original_protection == 0) continue;
    if (!WriteByte(*it, true)) ok = false;
  }
  provider.ready = false;
  return ok;
}
}  // namespace internal

inline bool IsDlssgProvider(HMODULE module) {
  internal::ModuleReference held(module);
  if (!held) return false;
  internal::Image image;
  return internal::IsImageMapping(module) && internal::InspectImage(module, image) &&
         internal::IsDlssgImage(image, module);
}

// Native Ada is not retargeted, but unknown images must not receive addon patches.
// Descriptor redirection keeps these original containers intact, so this remains
// valid after a native quality plan has been published.
inline bool QualifiedNativeInventory(HMODULE module) {
  internal::ModuleReference held(module);
  if (!held) return false;
  internal::Image image;
  internal::ImageIdentity identity;
  if (!internal::ReadIdentity(module, identity) || !internal::InspectImage(module, image) ||
      !internal::IsDlssgImage(image, module)) return false;
  const auto* profile = profiles::Find(identity.timestamp, identity.image_bytes);
  if (!profile) return false;
  for (const auto& region : profile->retarget_payloads) {
    if (region.rva > image.bytes || region.bytes > image.bytes - region.rva ||
        !internal::IsReadable(image.base + region.rva, region.bytes, module)) return false;
  }
  return profiles::MatchesRegions({image.base, image.bytes}, profile->retarget_payloads);
}

inline bool FindQualifiedMfgGates(HMODULE module, std::vector<unsigned char*>& output) {
  output.clear();
  internal::ImageIdentity identity;
  if (!internal::ReadIdentity(module, identity)) return false;
  const auto* profile = profiles::Find(identity.timestamp, identity.image_bytes);
  if (!profile) return false;
  auto* base = reinterpret_cast<unsigned char*>(module);
  for (const auto& gate : profile->mfg_gates) {
    const auto& region = gate.code;
    if (region.rva > identity.image_bytes || region.bytes > identity.image_bytes - region.rva ||
        gate.immediate_rva < region.rva || gate.immediate_rva - region.rva >= region.bytes ||
        !internal::IsReadable(base + region.rva, region.bytes, module) ||
        profiles::Fingerprint({base + region.rva, region.bytes}) != region.hash ||
        base[gate.immediate_rva] != 0xb0) return false;
  }
  for (const auto& gate : profile->mfg_gates) output.push_back(base + gate.immediate_rva);
  return !output.empty();
}

struct ProviderStatus {
  unsigned ready = 0;
  unsigned blocked = 0;
  bool busy = false;
  bool failed = false;
  unsigned int QualifiedCount() const {
    return busy || failed || blocked ? 0 : ready;
  }
};
inline ProviderStatus GetProviderStatus() {
  ProviderStatus result;
  result.failed = internal::g_registry_failed.load();
  if (!TryAcquireSRWLockShared(&internal::g_lock)) {
    result.busy = true;
    return result;
  }
  for (const auto& provider : internal::g_providers) {
    // Prepared images are retained until process exit. Losing one, its gate or
    // an unfinished transaction is a real failure, not an expired rejection.
    const auto* profile = architecture::ActiveProfile();
    bool valid = profile && provider.architecture == profile->architecture &&
                 provider.ready && provider.qualification == profiles::Qualification::kPatchable &&
                 !provider.patches.empty() &&
                 internal::IsCurrent(provider.module, provider.identity);
    if (valid) {
      const auto& gate = provider.patches.back();
      valid = internal::IsReadable(gate.address, 1, provider.module) && *gate.address == gate.after &&
              internal::EndpointSelectorsApplied(provider);
    }
    if (valid) {
      ++result.ready;
    } else {
      ++result.blocked;
    }
  }
  for (const auto& rejected : internal::g_rejected) {
    // An image reused at the same address is unqualified, not automatically
    // harmless. Only absence of an executable image expires this veto.
    if (internal::IsImageMapping(rejected.module)) {
      ++result.blocked;
    }
  }
  ReleaseSRWLockShared(&internal::g_lock);
  return result;
}
inline unsigned int PreparedProviderCount() {
  return GetProviderStatus().QualifiedCount();
}

// Called through the project's existing provider discovery/maintenance path.
// No CUDA, NVAPI, module enumeration, downloads or compilation here.
inline bool PrepareProviderImpl(HMODULE module,
    std::span<const profiles::ProviderProfile> known_profiles = profiles::kProfiles) {
  const auto* profile = architecture::ActiveProfile();
  if (!g_enabled.load() || !profile || !profile->RequiresProviderRetarget() ||
      internal::g_registry_failed.load() || module == nullptr) return false;
  if (!internal::IsImageMapping(module)) return false;
  if (!TryAcquireSRWLockExclusive(&internal::g_lock)) return false;
  struct Unlock {
    ~Unlock() { ReleaseSRWLockExclusive(&internal::g_lock); }
  } unlock;
  for (const auto& provider : internal::g_providers) {
    if (provider.module != module) continue;
    return provider.architecture == profile->architecture &&
           provider.ready && provider.qualification == profiles::Qualification::kPatchable &&
                 !provider.patches.empty() &&
           internal::IsCurrent(module, provider.identity) &&
           internal::IsReadable(provider.patches.back().address, 1, module) &&
           *provider.patches.back().address == provider.patches.back().after &&
           internal::EndpointSelectorsApplied(provider);
  }

  internal::Image image;
  internal::ImageIdentity identity;
  if (!internal::InspectImage(module, image) || !internal::ReadIdentity(module, identity)) {
    internal::g_registry_failed.store(true);
    Log("invalid provider image: " + internal::ModulePath(module), true);
    return false;
  }
  const auto path = internal::ModulePath(module);
  auto reject = [&](const std::string& reason) {
    auto found = std::find_if(internal::g_rejected.begin(), internal::g_rejected.end(),
        [module](const auto& entry) { return entry.module == module; });
    const bool changed = found == internal::g_rejected.end() ||
        found->identity != identity || found->reason != reason;
    internal::Rejection entry{module, identity, path, reason};
    if (found != internal::g_rejected.end()) {
      *found = std::move(entry);
    } else if (internal::g_rejected.size() < 32) {
      internal::g_rejected.push_back(std::move(entry));
    } else {
      internal::g_registry_failed.store(true);
    }
    if (changed) {
      const bool ready = std::any_of(internal::g_providers.begin(), internal::g_providers.end(),
          [](const auto& provider) { return provider.ready; });
      if (!ready) Log("provider rejected: " + reason + " (" + path + ")", true);
    }
    return false;
  };
  const auto arch_export = GetProcAddress(module, "NVSDK_NGX_GetGPUArchitecture");
  const auto parameters_export = GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl");
  if (!arch_export && !parameters_export)
    return false;  // A matching filename alone is not an NGX provider.
  if (!internal::OwnCodeExport(image, module, "NVSDK_NGX_GetGPUArchitecture") ||
      !internal::OwnCodeExport(image, module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl"))
    return reject("incomplete or forwarded provider ABI");

  if (!internal::IsDlssgImage(image, module)) return reject("not a DLSS-G provider");

  // Pre-write failures can be retried at the next normal maintenance boundary.
  // They are cleared ONLY after this same image passes the complete transaction.
  if (g_create_seen.load()) return reject("provider loaded after FG Create");
  if (internal::g_providers.size() >= 8) return reject("provider limit reached");
  auto* gate = reinterpret_cast<unsigned char*>(GetProcAddress(module, "NVSDK_NGX_GetGPUArchitecture"));
  const unsigned char expected_gate[] = {0xB8, 0x90, 0x01, 0x00, 0x00, 0xC3};
  if (!gate || !internal::IsReadable(gate, sizeof(expected_gate), module) ||
      std::memcmp(gate, expected_gate, sizeof(expected_gate)) != 0) {
    return reject("unrecognized minimum-architecture export");
  }
  bool executable_gate = false;
  for (const auto& section : image.sections) {
    const auto begin = reinterpret_cast<uintptr_t>(image.base + section.VirtualAddress);
    const auto address = reinterpret_cast<uintptr_t>(gate);
    if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && address >= begin &&
        address - begin <= section.Misc.VirtualSize &&
        sizeof(expected_gate) <= section.Misc.VirtualSize - (address - begin)) {
      executable_gate = true;
    }
  }
  if (!executable_gate) return reject("architecture export is not in code");

  const auto* qualified = profiles::Find(identity.timestamp, identity.image_bytes, known_profiles);
  if (!qualified || !profiles::AllowsTarget(*qualified, profile->target_sm))
    return reject("unknown provider profile; observation only");
  for (const auto& region : qualified->retarget_payloads) {
    if (region.rva > image.bytes || region.bytes > image.bytes - region.rva ||
        !internal::IsReadable(image.base + region.rva, region.bytes, module))
      return reject("unreadable provider profile payload");
  }
  if (!profiles::MatchesRegions({image.base, image.bytes}, qualified->retarget_payloads))
    return reject("provider inventory fingerprint mismatch; no writes");

  const bool turing_endpoint = profile->architecture == Architecture::kTuring;
  if (turing_endpoint) {
    for (const auto& region : endpoint::kCode) {
      if (region.rva > image.bytes || region.bytes > image.bytes - region.rva ||
          !internal::IsReadable(image.base + region.rva, region.bytes, module))
        return reject("unreadable Turing endpoint code");
    }
    if (image.bytes != endpoint::kImageBytes ||
        !internal::IsReadable(image.base + 0x64abe8, 8, module) ||
        !internal::IsReadable(image.base + 0x64b028, 8, module) ||
        !endpoint::QualifiedCode({image.base, image.bytes}, identity.timestamp))
      return reject("unknown Turing endpoint layout; PTX network selection not qualified");
  }

  struct Block {
    unsigned char* address;
    std::vector<unsigned char> before;
    std::vector<unsigned char> after;
  };
  std::vector<Block> blocks;
  size_t total_bytes = 0;
  size_t hidden_cubins = 0;
  for (const auto& section : image.sections) {
    if (!(section.Characteristics & IMAGE_SCN_MEM_READ) ||
        (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
      continue;
    }
    auto* start = image.base + section.VirtualAddress;
    const size_t size = section.Misc.VirtualSize;
    if (!internal::IsReadable(start, size, module)) return reject("unreadable data section");
    for (size_t at = 0; at + fatbin::kHeaderBytes <= size;) {
      if (fatbin::ReadU32(start + at) != fatbin::kMagic) {
        ++at;
        continue;
      }
      const uint64_t payload = fatbin::ReadU64(start + at + 8);
      if (payload > size - at - fatbin::kHeaderBytes ||
          payload > fatbin::kMaxFatbinBytes - fatbin::kHeaderBytes)
        return reject("fatbin outside section");
      const size_t bytes = static_cast<size_t>(payload) + fatbin::kHeaderBytes;
      ptx::Plan plan;
      std::string reason;
      const auto result = ptx::Retarget({start + at, bytes}, plan, reason, *profile);
      if (result == ptx::Result::kRejected) return reject(reason);
      if (result == ptx::Result::kRetargeted) {
        if (blocks.size() == 512 || bytes > 64 * 1024 * 1024 - total_bytes) {
          return reject("provider exceeds preparation budget");
        }
        blocks.push_back({start + at, {start + at, start + at + bytes}, std::move(plan.replacement)});
        total_bytes += bytes;
        hidden_cubins += plan.hidden_cubins;
      }
      at += bytes;
    }
  }
  if (blocks.empty()) return reject("no supported sm_89 fatbins");
  if (blocks.size() != qualified->retarget_payloads.size())
    return reject("provider inventory count mismatch; no writes");
  for (const auto& block : blocks) {
    const auto rva = static_cast<uint32_t>(block.address - image.base);
    const auto found = std::find_if(qualified->retarget_payloads.begin(),
        qualified->retarget_payloads.end(), [rva](const auto& entry) { return entry.rva == rva; });
    if (found == qualified->retarget_payloads.end() || found->bytes != block.before.size())
      return reject("unqualified provider payload; no writes");
  }
  if (turing_endpoint) {
    for (const auto& payload : endpoint::kPayloads) {
      const auto found = std::find_if(blocks.begin(), blocks.end(), [&](const Block& block) {
        return block.address == image.base + payload.rva;
      });
      if (found == blocks.end() || !endpoint::PreparedPayload(found->before, found->after, payload))
        return reject("Turing endpoint requires all 39 exact SM75 network payloads");
    }
  }
  internal::Provider candidate{module, {}, false, 0, 0, identity, path, {}, profile->architecture};
  for (const auto& block : blocks) {
    if (std::memcmp(block.address, block.before.data(), block.before.size()) != 0) {
      return reject("image changed during preparation");
    }
    // Payload/header updates precede the minimum-architecture export below.
    for (size_t i = block.before.size(); i-- > 0;) {
      if (block.before[i] != block.after[i]) {
        candidate.patches.push_back(
            {block.address + i, block.before[i], block.after[i]});
      }
    }
  }
  if (turing_endpoint) {
    // Select existing *_89_PTX classes, not the SM86-only cubins chosen by
    // the native SM<89 branch. Publish only with the qualified SM75 payloads.
    for (const auto& site : endpoint::kSelectors) {
      for (size_t i = 0; i < site.before.size(); ++i)
        candidate.patches.push_back({image.base + site.rva + i, site.before[i], 0x90});
    }
  }
  candidate.patches.push_back({gate + 1, 0x90, static_cast<unsigned char>(profile->native_arch)});
  // Reserve all rollback storage before the first write. Keep the mapped image
  // alive while rollback pointers into it are stored. This process-lifetime
  // provider reference does not retain the addon and keeps unload restoration
  // deterministic without racing a provider unmap.
  internal::g_providers.reserve(internal::g_providers.size() + 1);
  HMODULE held = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(module), &held) || held != module) return reject("cannot retain provider");
  internal::g_providers.push_back(std::move(candidate));
  auto& saved = internal::g_providers.back();
  bool ok = true;
  for (size_t i = 0; i + 1 < saved.patches.size(); ++i) {
    if (!internal::WriteByte(saved.patches[i], false)) {
      ok = false;
      break;
    }
  }
  for (const auto& block : blocks) {
    if (std::memcmp(block.address, block.after.data(), block.after.size()) != 0) ok = false;
  }
  if (ok) ok = internal::EndpointSelectorsApplied(saved);
  if (ok) ok = internal::WriteByte(saved.patches.back(), false);
  if (!ok) {
    const bool restored = internal::Undo(saved);
    saved.failure = restored ? "provider preparation rolled back" : "provider rollback failed";
    if (!restored) internal::g_registry_failed.store(true);
    Log(restored ? "provider preparation rolled back" : "provider rollback failed", true);
    return false;
  }
  saved.fatbins = static_cast<unsigned>(blocks.size());
  saved.hidden_cubins = static_cast<unsigned>(hidden_cubins);
  saved.qualification = profiles::Classify(true, true, true);
  saved.ready = true;
  std::erase_if(internal::g_rejected, [module](const auto& entry) { return entry.module == module; });
  std::stringstream message;
  message << "provider prepared: sm_89 -> sm_" << profile->target_sm << ", "
          << blocks.size() << " fatbins, "
          << hidden_cubins << " cubins hidden";
  Log(message.str());
  Log(std::string("ProviderProfile ") + qualified->id + " " + profiles::QualificationName(saved.qualification) + " (mapped exact inventory; target preparation committed)");
  if (turing_endpoint)
    Log("TuringEndpointFixV4: DL1/DL2 PTX backend selected; 39 SM75 network payloads qualified");
  return true;
}

inline bool PrepareProvider(HMODULE module) {
  try {
    return PrepareProviderImpl(module);
  } catch (...) {
    internal::g_registry_failed.store(true);
    reshade::log::message(reshade::log::level::error,
        "mfgunlock: provider preparation failed");
    return false;
  }
}

// Exact-qualified comparison sites share one checked transaction on all targets.
// Undo ownership is published before success; failed restore remains retryable.
inline bool ApplyMfgComparisons(std::span<unsigned char* const> sites,
                                std::vector<internal::BytePatch>& ownership) {
  const auto* profile = architecture::ActiveProfile();
  if (!g_enabled.load() || !profile) return false;
  std::vector<internal::BytePatch> changes;
  changes.reserve(sites.size());
  for (auto* site : sites) {
    if (*site != 0xB0) return false;
    changes.push_back({site, 0xB0, static_cast<unsigned char>(profile->native_arch)});
  }
  ownership.reserve(ownership.size() + changes.size());
  size_t attempted = 0;
  for (auto& patch : changes) {
    ++attempted;
    if (internal::WriteByte(patch, false)) continue;
    bool restored = true;
    while (attempted != 0) {
      auto& undo = changes[--attempted];
      if (undo.original_protection != 0 && !internal::WriteByte(undo, true)) restored = false;
    }
    if (!restored) {
      for (const auto& change : changes)
        if (change.original_protection != 0) ownership.push_back(change);
      internal::g_comparison_restore_failed.store(true, std::memory_order_release);
      internal::g_registry_failed.store(true, std::memory_order_release);
    }
    Log(restored ? "MFG gate update rolled back" : "MFG gate rollback failed; undo metadata retained", true);
    return false;
  }
  ownership.insert(ownership.end(), changes.begin(), changes.end());
  return true;
}

inline bool RestoreMfgComparisons(std::vector<internal::BytePatch>& ownership) {
  bool complete = true;
  for (auto it = ownership.rbegin(); it != ownership.rend(); ++it) {
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(it->address, &memory, sizeof(memory)) &&
        internal::IsReadable(it->address, 1, static_cast<HMODULE>(memory.AllocationBase)) &&
        internal::WriteByte(*it, true)) {
      it->address = nullptr;
    } else {
      complete = false;
    }
  }
  std::erase_if(ownership, [](const auto& patch) { return patch.address == nullptr; });
  internal::g_comparison_restore_failed.store(!complete, std::memory_order_release);
  if (!complete) internal::g_registry_failed.store(true, std::memory_order_release);
  return complete;
}

inline void Restore() {
  AcquireSRWLockExclusive(&internal::g_lock);
  bool complete = true;
  for (auto it = internal::g_providers.rbegin(); it != internal::g_providers.rend(); ++it) {
    if (internal::Undo(*it)) {
      it->patches.clear();
    } else {
      complete = false;
      it->failure = "provider restore failed; rollback metadata retained";
      Log(it->failure, true);
    }
  }
  std::erase_if(internal::g_providers, [](const auto& entry) { return entry.patches.empty(); });
  if (complete) internal::g_rejected.clear();
  internal::g_registry_failed.store(!complete || internal::g_comparison_restore_failed.load());
  ReleaseSRWLockExclusive(&internal::g_lock);
}
}  // namespace mfgunlock::provider
