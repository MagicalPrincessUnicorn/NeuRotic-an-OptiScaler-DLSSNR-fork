#pragma once
#include "MfgMappedPe.h"
#include "MfgProviderQualification.h"

namespace Neurotic::Mfg {
inline bool ReadMfgModuleVersion(HMODULE module, MfgProviderVersion& version) noexcept {
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    if (!resource) return false;
    const DWORD size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
    if (!bytes || size < sizeof(VS_FIXEDFILEINFO) || size > (1u << 20)) return false;
    for (size_t offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4) {
        VS_FIXEDFILEINFO info {};
        if (!ReadMfgMemory(bytes + offset, &info, sizeof info)) return false;
        if (info.dwSignature != 0xFEEF04BD || info.dwStrucVersion != 0x00010000) continue;
        version = {HIWORD(info.dwFileVersionMS), LOWORD(info.dwFileVersionMS),
            HIWORD(info.dwFileVersionLS), LOWORD(info.dwFileVersionLS)};
        return true;
    }
    return false;
}
}
