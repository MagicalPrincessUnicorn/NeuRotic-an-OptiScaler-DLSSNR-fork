#include <windows.h>
#include <chrono>
#include <cstdio>
#include <dlssnr/NrProviderProbeCache.h>

// Local export fixture only. No provider Init/Create/Evaluate, GPU or game.
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto module = LoadLibraryW(argv[1]);
    if (!module) return 3;
    const auto probe = reinterpret_cast<int(__cdecl*)(const wchar_t*)>(
        GetProcAddress(module, "dlssnr_call_probe_d3d12"));
    if (!probe || probe(argv[2]) != 31) return 4;
    constexpr int calls = 128;
    const std::filesystem::path path(argv[2]);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) if (probe(argv[2]) != 31) return 5;
    const auto middle = std::chrono::steady_clock::now();
    DlssNr::NrProviderProbeCache cache;
    int probes = 0;
    const int inertDeviceIdentity = 0;
    for (int i = 0; i < calls; ++i)
        if (cache.Resolve(1, 1, module, &inertDeviceIdentity, path,
            [&] { ++probes; return probe(argv[2]); }) != 31) return 6;
    const auto end = std::chrono::steady_clock::now();
    if (probes != 1) return 7;
    std::printf("PASS real forwarder capability discovery, local export fixture: calls=%d uncachedProbes=%d cachedProbes=%d uncachedMs=%.3f cachedMs=%.3f\n",
        calls, calls, probes, std::chrono::duration<double,std::milli>(middle-start).count(),
        std::chrono::duration<double,std::milli>(end-middle).count());
    std::puts("Scope: capability discovery CPU only; no rendering or in-game FPS measurement.");
}
