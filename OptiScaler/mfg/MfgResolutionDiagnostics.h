#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace Neurotic::Mfg
{
inline unsigned MfgResolutionExport(const char* name) noexcept
{
    // GetProcAddress also accepts MAKEINTRESOURCE ordinals, including 0xF000..0xFFFF.
    if (reinterpret_cast<uintptr_t>(name) <= 0xffff) return 0;
    constexpr const char* names[] = {"NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl",
        "NVSDK_NGX_GetGPUArchitecture", "NVSDK_NGX_D3D12_CreateFeature",
        "NVSDK_NGX_D3D12_EvaluateFeature", "NVSDK_NGX_D3D12_GetFeatureRequirements"};
    for (unsigned i = 0; i < std::size(names); ++i)
        if (std::strcmp(name, names[i]) == 0) return i + 1;
    return 0;
}
struct MfgResolutionObservation
{
    uintptr_t queried = 0, resolved = 0, caller = 0;
    unsigned kind = 0;
    bool operator==(const MfgResolutionObservation&) const = default;
};
// Diagnostic data only. Resolution does not prove invocation or provider ownership.
class MfgResolutionDiagnostics
{
    std::array<MfgResolutionObservation, 64> records {};
    unsigned count = 0;
    bool saturated = false;
public:
    bool Record(MfgResolutionObservation value) noexcept
    {
        if (!value.queried || !value.resolved || !value.caller || !value.kind) return false;
        for (unsigned i = 0; i < count; ++i) if (records[i] == value) return false;
        if (count == records.size()) { saturated = true; return false; }
        records[count++] = value;
        return true;
    }
    unsigned Count() const noexcept { return count; }
    bool Saturated() const noexcept { return saturated; }
};
}
