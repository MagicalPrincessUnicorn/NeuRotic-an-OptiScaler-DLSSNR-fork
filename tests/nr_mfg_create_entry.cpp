#include "../OptiScaler/mfg/MfgCreateEntry.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
using namespace Neurotic::Mfg;
static std::atomic<unsigned> observations {0};
static std::atomic<bool> admitted {true};
static bool Allow(HMODULE, ID3D12GraphicsCommandList*) noexcept { return admitted.load(); }
static std::atomic<HMODULE> observedOwner {nullptr};
static std::atomic<void*> observedCommand {nullptr};
static void* foreignTarget = nullptr;
static std::array<uint8_t,5> foreignBranch {};
static void CompetingInstaller(void* address) noexcept
{
    auto* target = static_cast<uint8_t*>(address);
    DWORD old = 0, ignored = 0;
    VirtualProtect(target - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    foreignBranch[0] = 0xe9;
    const auto displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(foreignTarget) - reinterpret_cast<intptr_t>(target));
    std::memcpy(foreignBranch.data() + 1, &displacement, 4);
    std::memcpy(target - 5, foreignBranch.data(), 5);
    _InterlockedCompareExchange16(reinterpret_cast<volatile SHORT*>(target),
        static_cast<SHORT>(0xf9eb), static_cast<SHORT>(0x5340));
    FlushInstructionCache(GetCurrentProcess(), target - 5, 7);
    VirtualProtect(target - 5, 7, old, &ignored);
}
static void Before(HMODULE owner, ID3D12GraphicsCommandList* command) noexcept
{
    ++observations; observedOwner = owner; observedCommand = command;
}
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    const auto owner = LoadLibraryW(argv[1]);
    assert(owner);
    auto cached = reinterpret_cast<MfgCreateFunction>(GetProcAddress(owner, "CreateFixture"));
    auto calls = reinterpret_cast<unsigned(*)()>(GetProcAddress(owner, "Calls"));
    auto valid = reinterpret_cast<bool(*)()>(GetProcAddress(owner, "ValidArguments"));
    assert(cached && calls && valid);
    RetainedCreateEntries entries;
    assert(entries.Install(owner, reinterpret_cast<void*>(cached), Before, Allow));
    assert(entries.Install(owner, reinterpret_cast<void*>(cached), Before, Allow));
    auto* command = reinterpret_cast<ID3D12GraphicsCommandList*>(0x12340000);
    auto* params = reinterpret_cast<NVSDK_NGX_Parameter*>(0x23450000);
    auto** output = reinterpret_cast<NVSDK_NGX_Handle**>(0x34560000);
    assert(cached(command, NVSDK_NGX_Feature_FrameGeneration, params, output) == NVSDK_NGX_Result_Success);
    assert(observations == 1 && observedOwner == owner && observedCommand == command);
    assert(calls() == 1 && valid());
    assert(cached(command, NVSDK_NGX_Feature_SuperSampling, params, output) == NVSDK_NGX_Result_Success);
    assert(observations == 1 && calls() == 2);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&] {
        for (unsigned call = 0; call < 100; ++call)
            assert(cached(command, NVSDK_NGX_Feature_FrameGeneration, params, output) == NVSDK_NGX_Result_Success);
    });
    for (auto& thread : threads) thread.join();
    assert(observations == 801 && calls() == 802 && valid());
    auto race = reinterpret_cast<MfgCreateFunction>(GetProcAddress(owner, "CreateRaceFixture"));
    foreignTarget = reinterpret_cast<void*>(GetProcAddress(owner, "ForeignFixture"));
    assert(race && foreignTarget);
    RetainedCreateEntries::publicationProbe = CompetingInstaller;
    assert(!entries.Install(owner, reinterpret_cast<void*>(race), Before));
    RetainedCreateEntries::publicationProbe = nullptr;
    // Check the competing route before calling it; the old bug could otherwise
    // enter a broken trampoline and crash instead of reporting the race.
    if (std::memcmp(reinterpret_cast<uint8_t*>(race) - 5, foreignBranch.data(), 5)) return 20;
    assert(static_cast<unsigned>(race(command, NVSDK_NGX_Feature_FrameGeneration, params, output)) == 99);
    assert(observations == 801);
    auto lateRace = reinterpret_cast<MfgCreateFunction>(GetProcAddress(owner, "CreateLateRaceFixture"));
    assert(lateRace);
    RetainedCreateEntries::stagedProbe = CompetingInstaller;
    assert(!entries.Install(owner, reinterpret_cast<void*>(lateRace), Before));
    RetainedCreateEntries::stagedProbe = nullptr;
    assert(!std::memcmp(reinterpret_cast<uint8_t*>(lateRace) - 5, foreignBranch.data(), 5));
    assert(static_cast<unsigned>(lateRace(command, NVSDK_NGX_Feature_FrameGeneration, params, output)) == 99);
    assert(observations == 801);
    auto foreign = GetProcAddress(owner, "Calls");
    assert(!entries.Install(owner, reinterpret_cast<void*>(foreign), Before));
    assert(!entries.Install(GetModuleHandleW(nullptr), reinterpret_cast<void*>(cached), Before, Allow));
    FreeLibrary(owner); // Registry retains the image and callable trampoline.
    assert(cached(command, NVSDK_NGX_Feature_FrameGeneration, params, output) == NVSDK_NGX_Result_Success);
    assert(observations == 802 && calls() == 803);
    admitted = false;
    assert(cached(command, NVSDK_NGX_Feature_FrameGeneration, params, output) == NVSDK_NGX_Result_FAIL_FeatureNotSupported);
    assert(observations == 803 && calls() == 803); // No entry into the invalidated backend.
    assert(cached(command, NVSDK_NGX_Feature_SuperSampling, params, output) == NVSDK_NGX_Result_Success);
    assert(observations == 803 && calls() == 804); // SR still forwards unchanged.
    assert(!entries.Install(owner, reinterpret_cast<void*>(cached), Before)); // Cannot discard admission.
    assert(entries.IsCurrent(owner, reinterpret_cast<void*>(cached)));
    // A successful historical install cannot authorize after a competing hook.
    auto* entryBytes = reinterpret_cast<unsigned char*>(cached);
    DWORD protection = 0, ignored = 0;
    assert(VirtualProtect(entryBytes, 2, PAGE_EXECUTE_READWRITE, &protection));
    const SHORT ours = *reinterpret_cast<SHORT*>(entryBytes);
    _InterlockedExchange16(reinterpret_cast<volatile SHORT*>(entryBytes), static_cast<SHORT>(0x9090));
    assert(!entries.IsCurrent(owner, reinterpret_cast<void*>(cached)));
    _InterlockedExchange16(reinterpret_cast<volatile SHORT*>(entryBytes), ours);
    assert(VirtualProtect(entryBytes, 2, protection, &ignored));
    FlushInstructionCache(GetCurrentProcess(), entryBytes, 2);
    assert(entries.IsCurrent(owner, reinterpret_cast<void*>(cached)));
    std::puts("PASS: admission blocks invalidated FG while preserving SR; cached entry pointer observes actual FG creation, preserves ABI and return, filters SR, rejects foreign entries and retains lifetime");
}
