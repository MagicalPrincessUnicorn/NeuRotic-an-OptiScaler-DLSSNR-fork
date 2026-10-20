#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include "../OptiScaler/mfg/MfgMappedPe.h"
#include "../OptiScaler/mfg/MfgProviderQualification.h"
#include "../OptiScaler/mfg/MfgCreateEntry.h"

#include <cstdio>
#include <string>

using namespace Neurotic::Mfg;
static void InertBeforeCreate(HMODULE, ID3D12GraphicsCommandList*) noexcept {}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 4) return 2;
    const std::wstring folder = argv[1];
    // Inert mapped images: imports and vendor entry points are never executed.
    const auto wrapper = LoadLibraryExW((folder + L"\\sl.dlss_g.dll").c_str(),
        nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!wrapper) return 3;
    const LUID adapter {89951, 0};
    if (TryPublishAdaMfg(wrapper, 7, 5, adapter)) return 4; // device not observed
    ObserveAdaD3D12Adapter(adapter);
    SetLastError(12345);
    ObserveAdaExportResolution(wrapper, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl",
        reinterpret_cast<const void*>(wrapper), reinterpret_cast<const void*>(0x1234));
    if (GetLastError() != 12345) return 17;
    if (TryPublishAdaMfg(wrapper, 7, 5, adapter) ||
        AdaMfgSnapshot().reason != MfgRuntimeReason::ProviderMissing) return 12;
    const auto providerPath = argc >= 3 ? std::wstring(argv[2]) : folder + L"\\nvngx_dlssg.dll";
    const auto provider = LoadLibraryExW(providerPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!provider) return 3;
    RetainedCreateEntries observers;
    if (!observers.Install(provider, reinterpret_cast<void*>(GetProcAddress(provider,
        "NVSDK_NGX_D3D12_CreateFeature")), InertBeforeCreate)) return 22;
    if (argc == 4)
    {
        const auto duplicate = LoadLibraryExW((folder + L"\\nvngx_dlssg.dll").c_str(),
            nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!duplicate || duplicate == provider) return 13;
        if (!observers.Install(duplicate, reinterpret_cast<void*>(GetProcAddress(duplicate,
            "NVSDK_NGX_D3D12_CreateFeature")), InertBeforeCreate)) return 22;
        if (TryPublishAdaMfg(wrapper, 7, 5, adapter) ||
            AdaMfgSnapshot().reason != MfgRuntimeReason::ProviderAmbiguous) return 14;
        const auto scans = AdaMfgSnapshot().discoveryScans;
        for (unsigned repeat = 0; repeat < 3; ++repeat)
            if (TryPublishAdaMfg(wrapper, 7, 5, adapter) ||
                AdaMfgSnapshot().reason != MfgRuntimeReason::ProviderAmbiguous ||
                AdaMfgSnapshot().discoveryScans != scans) return 16;
        // Concrete invocation ownership resolves duplicate discovery. Calling
        // this boundary directly keeps vendor machine code inert in the test.
        const bool selectOther = std::wstring(argv[3]) == L"other";
        const auto selected = selectOther ? duplicate : provider;
        const auto inactive = selectOther ? provider : duplicate;
        const auto text = ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(inactive)).ExecutableText();
        const auto fingerprint = FingerprintMfgImage(text.bytes, text.size);
        if (!TryPublishAdaMfg(wrapper, 7, 5, adapter, selected, &adapter) || !HasOwnedAdaMfg(wrapper, 7) ||
            FingerprintMfgImage(text.bytes, text.size) != fingerprint) return 18;
        // Selecting either identity leaves the entire other code image alone.
        if (std::wstring(argv[3]) == L"adapter-conflict" || std::wstring(argv[3]) == L"adapter-provider-conflict")
        {
            const LUID wrong {89952, 0};
            const auto attempted = std::wstring(argv[3]) == L"adapter-conflict" ? selected : inactive;
            TryPublishAdaMfg(wrapper, 7, 5, adapter, attempted, &wrong);
            if (HasOwnedAdaMfg(wrapper, 7) || AdaMfgSnapshot().status != MfgRuntimeStatus::Stale) return 20;
            ObserveAdaD3D12Adapter(adapter);
            if (TryPublishAdaMfg(wrapper, 7, 5, adapter, selected, &adapter)) return 21;
            std::puts("PASS: conflicting actual creation adapter disables publication until restart");
            return 0;
        }
        if (argc == 4 && std::wstring(argv[3]) == L"conflict")
        {
            if (TryPublishAdaMfg(wrapper, 7, 5, adapter, duplicate, &adapter) || HasOwnedAdaMfg(wrapper, 7) ||
                TryPublishAdaMfg(wrapper, 7, 5, adapter, provider, &adapter)) return 19;
            std::puts("PASS: a conflicting entered provider disables publication until restart");
            return 0;
        }
        FreeLibrary(inactive);
    }
    // The selected wrapper calls the NGX runtime; the runtime resolves its
    // snippet exports. There is no direct wrapper -> snippet GetProcAddress.
    if (!TryPublishAdaMfg(wrapper, 7, 5, adapter))
    {
        std::fprintf(stderr, "FAIL: loaded structural pair cannot publish before capability query\n");
        return 5;
    }
    if (!HasOwnedAdaMfg(wrapper, 7)) return 6;
    const LUID otherAdapter {89952, 0};
    if (TryPublishAdaMfg(wrapper, 7, 99, adapter) ||
        TryPublishAdaMfg(wrapper, 7, 5, otherAdapter) || !HasOwnedAdaMfg(wrapper, 7)) return 15;
    // Capability-time publication has no game options ABI yet. A later game
    // SetOptions with a supported older layout must reuse the owned patches.
    if (!TryPublishAdaMfg(wrapper, 7, 1, adapter)) return 7;
    if (HasOwnedAdaMfg(wrapper, 8)) return 8;
    InvalidateAdaMfg(8);
    if (!TryPublishAdaMfg(wrapper, 8, 2, adapter)) return 9;
    if (!HasOwnedAdaMfg(wrapper, 8)) return 10;
    const auto snapshot = AdaMfgSnapshot();
    if (snapshot.status != MfgRuntimeStatus::Published || snapshot.patchSites != 11 ||
        snapshot.temporalProfile != 1 || (snapshot.maxGenerated != 3 && snapshot.maxGenerated != 5)) return 11;
    std::puts("PASS: real runtime publishes an inert mapped pair before capabilities; ABI reuse and generation checks pass; no vendor code executed.");
    return 0;
}
