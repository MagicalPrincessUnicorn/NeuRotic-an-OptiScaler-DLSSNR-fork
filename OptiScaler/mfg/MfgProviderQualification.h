#pragma once

#include <cstddef>
#include <cstdint>

namespace Neurotic::Mfg
{
struct MfgProviderVersion
{
    uint16_t major = 0, minor = 0, patch = 0, build = 0;
};

constexpr uint32_t MfgProviderTemporalProfile(MfgProviderVersion v) noexcept
{
    if (v.major != 310) return 0;
    if (v.minor == 8 && v.patch == 0) return 1;
    if (v.minor == 9) return 2;
    if (v.minor == 2 && v.patch == 1 && v.build == 0) return 3;
    return 0;
}
constexpr bool IsMfgWrapperVersionSupported(MfgProviderVersion v) noexcept
{
    return v.major == 2 && (v.minor == 13 || v.minor == 14 ||
        (v.minor == 7 && v.patch == 32 && v.build == 0));
}
inline uint32_t QualifiedMfgWrapperCeiling(const uint8_t* bytes, size_t size,
    MfgProviderVersion version, size_t* gateOffset = nullptr) noexcept
{
    if (!bytes || size > (128u << 20) || !IsMfgWrapperVersionSupported(version)) return 0;
    size_t sites = 0;
    uint32_t ceiling = 0;
    for (size_t i = 0; i + 10 <= size; ++i) {
        if (bytes[i] == 0xba && (bytes[i + 1] == 3 || bytes[i + 1] == 5) &&
            !bytes[i + 2] && !bytes[i + 3] && !bytes[i + 4] &&
            bytes[i + 5] == 0x3b && bytes[i + 6] == 0xca && bytes[i + 7] == 0x0f &&
            bytes[i + 8] == 0x42 && bytes[i + 9] == 0xd1) {
            ++sites; ceiling = bytes[i + 1]; if (gateOffset) *gateOffset = i;
        }
    }
    return sites == 1 && (version.minor != 7 || ceiling == 3) ? ceiling : 0;
}

struct MfgModuleView
{
    // A validated, readable executable PE section from the selected module.
    // Hashing the entire relocated image would be unstable across loads.
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    uint64_t imageIdentity = 0;
    uint64_t generation = 0;
    bool activeOwner = false;
    uint32_t candidateCount = 0;
    MfgProviderVersion version;
    // Zero means no independently validated temporal descriptor was found.
    uint32_t temporalProfile = 0;
    uint32_t abiVersion = 5;
    // Snapshot of this selected image's executable bytes, used for currentness.
    // This is not a fixed hash allowlist.
    uint64_t snapshotFingerprint = 0;
};

struct MfgProviderImages
{
    MfgModuleView provider, wrapper;
    uint64_t boundProviderIdentity = 0;
    uint64_t boundWrapperIdentity = 0;
};
struct MfgAdapterIdentity { bool rtx40 = false, d3d12 = false; };
enum class MfgQualificationStatus : uint8_t
{
    Unavailable, Refused, Indeterminate, Stale, ReadyForExperiment
};

struct MfgQualification
{
    MfgQualificationStatus status = MfgQualificationStatus::Unavailable;
    uint64_t providerIdentity = 0;
    uint64_t wrapperIdentity = 0;
    uint64_t providerGeneration = 0;
    uint64_t providerFingerprint = 0;
    uint64_t wrapperFingerprint = 0;
    size_t architectureSites = 0;
    size_t wrapperSites = 0;
    uint32_t maxGenerated = 0;
};

constexpr uint64_t FingerprintMfgImage(const uint8_t* bytes, size_t size) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    if (!bytes) return 0;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// The scanner is deliberately bounded to the selected image views supplied by
// the module owner. A caller must validate readable PE sections before passing
// mapped bytes; this pure reducer never probes arbitrary process memory.
inline MfgQualification QualifyMfgProvider(const MfgProviderImages& images,
    const MfgAdapterIdentity& adapter, uint64_t generation) noexcept
{
    MfgQualification result;
    const auto& p = images.provider;
    const auto& w = images.wrapper;
    if (!p.bytes || !w.bytes || !p.size || !w.size || !p.activeOwner || !w.activeOwner)
        return result;
    if (p.candidateCount != 1 || w.candidateCount != 1)
    {
        result.status = MfgQualificationStatus::Indeterminate;
        return result;
    }
    if (!generation || p.generation != generation || w.generation != generation)
    {
        result.status = MfgQualificationStatus::Stale;
        return result;
    }
    result.status = MfgQualificationStatus::Refused;
    if (!adapter.rtx40 || !adapter.d3d12 || p.size > (128u << 20) ||
        w.size > (128u << 20) || !p.imageIdentity || !w.imageIdentity ||
        p.imageIdentity == w.imageIdentity ||
        images.boundProviderIdentity != p.imageIdentity ||
        images.boundWrapperIdentity != w.imageIdentity ||
        p.abiVersion < 1 || p.abiVersion > 5 ||
        w.abiVersion < 1 || w.abiVersion > 5 || !p.temporalProfile ||
        !p.snapshotFingerprint || !w.snapshotFingerprint) return result;
    const bool canonical = p.version.major == 310 && p.version.minor == 8 &&
        p.version.patch == 0 && w.version.major == 2 && w.version.minor == 13 &&
        w.version.patch == 0 && p.temporalProfile == 1;
    const bool current = p.version.major == 310 && p.version.minor == 9 &&
        w.version.major == 2 && w.version.minor == 14 && p.temporalProfile == 2;
    const bool legacyWrapper = w.version.major == 2 && w.version.minor == 7 &&
        w.version.patch == 32 && w.version.build == 0 &&
        MfgProviderTemporalProfile(p.version) == p.temporalProfile;
    if (!canonical && !current && !legacyWrapper) return result;

    // Source lineage: MFGAdaUnlock-RenoDx 1.1.5, MIT, ImDreamt and contributors.
    // The provider architecture compares are 3D/81 FD with 0x1b0 immediate.
    // The wrapper ceiling is mov edx,3/5; cmp ecx,edx; cmovb edx,ecx.
    constexpr uint8_t archA[] = {0x3D, 0xB0, 0x01, 0x00, 0x00};
    constexpr uint8_t archB[] = {0x81, 0xFD, 0xB0, 0x01, 0x00, 0x00};
    constexpr uint8_t ceiling3[] = {0xBA, 0x03, 0, 0, 0, 0x3B, 0xCA, 0x0F, 0x42, 0xD1};
    constexpr uint8_t ceiling5[] = {0xBA, 0x05, 0, 0, 0, 0x3B, 0xCA, 0x0F, 0x42, 0xD1};
    const auto count = [](const uint8_t* bytes, size_t size, const uint8_t* pattern, size_t n) noexcept
    {
        size_t found = 0;
        if (size < n) return found;
        for (size_t i = 0; i <= size - n; ++i)
        {
            size_t j = 0;
            while (j < n && bytes[i + j] == pattern[j]) ++j;
            found += j == n;
        }
        return found;
    };
    result.providerFingerprint = FingerprintMfgImage(p.bytes, p.size);
    result.wrapperFingerprint = FingerprintMfgImage(w.bytes, w.size);
    if (result.providerFingerprint != p.snapshotFingerprint ||
        result.wrapperFingerprint != w.snapshotFingerprint) return result;
    const auto firstArchitectureSites = count(p.bytes, p.size, archA, sizeof archA);
    const auto secondArchitectureSites = count(p.bytes, p.size, archB, sizeof archB);
    result.architectureSites = firstArchitectureSites + secondArchitectureSites;
    const auto fiveFrameSites = count(w.bytes, w.size, ceiling5, sizeof ceiling5);
    if (legacyWrapper && fiveFrameSites) return result;
    result.wrapperSites = count(w.bytes, w.size, ceiling3, sizeof ceiling3) + fiveFrameSites;
    const size_t expectedSecond = p.temporalProfile == 3 ? 0 : 1;
    if (firstArchitectureSites != 1 || secondArchitectureSites != expectedSecond ||
        result.wrapperSites != 1) return result;
    result.providerIdentity = p.imageIdentity;
    result.wrapperIdentity = w.imageIdentity;
    result.providerGeneration = generation;
    result.maxGenerated = fiveFrameSites ? 5 : 3;
    result.status = MfgQualificationStatus::ReadyForExperiment;
    return result;
}
}
