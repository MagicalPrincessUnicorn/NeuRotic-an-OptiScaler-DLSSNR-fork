#include "pch.h"
#include "Logger.h"
#include <nvapi/NvApiHooks.h>
#include <misc/IdentifyGpu.h>
#include <NvApiDriverSettings.h>
#include <detours/detours.h>
#include <cstdio>
#include <cstdlib>

// These production dependencies are unsafe once teardown has started. Keeping
// them fatal ensures the actual callback cannot silently reacquire live state.
static void Forbidden(const char* name)
{
    std::fprintf(stderr, "FAIL: terminal callback touched %s\n", name);
    std::exit(81);
}
Config* Config::Instance() { Forbidden("Config"); return nullptr; }
GpuInformation IdentifyGpu::getPrimaryGpu() { Forbidden("GPU discovery"); return {}; }
void ReflexHooks::hookReflex(PFN_NvApi_QueryInterface&) { Forbidden("Reflex installation"); }
void* ReflexHooks::getHookedReflex(unsigned int) { Forbidden("Reflex lookup"); return nullptr; }
void* fakenvapi::queryInterface(NvU32) { Forbidden("fake NvAPI map"); return nullptr; }
void fakenvapi::init(bool) { Forbidden("fake NvAPI initialization"); }
bool fakenvapi::isUsingAsMainNvapi() { Forbidden("fake NvAPI state"); return false; }

static unsigned calls = 0;
static unsigned lastId = 0;
static int token;
static __declspec(noinline) void* __stdcall Original(unsigned id)
{
    ++calls;
    lastId = id;
    return &token;
}
static NvAPI_Status __cdecl Arch(NvPhysicalGpuHandle, NV_GPU_ARCH_INFO* info)
{
    info->architecture_id = NV_GPU_ARCHITECTURE_GP100;
    info->architecture = NV_GPU_ARCHITECTURE_TU100;
    info->implementation = NV_GPU_ARCH_IMPLEMENTATION_TU106 + 1;
    return NVAPI_OK;
}
static NvAPI_Status __cdecl Setting(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING* info)
{
    info->u32CurrentValue = 73;
    return NVAPI_OK;
}
static void Check(bool condition, const char* name)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(82); }
}

static bool runLate = false;
struct LateCallback
{
    ~LateCallback()
    {
        if (!runLate) return;
        // Registered before NvApiTypes::Instance is first constructed in main:
        // this executes AFTER its real unordered_map destructor and spdlog's.
        const auto before = calls;
        Check(NvApiHooks::hkNvAPI_QueryInterface(0xf3148c42) == &token, "post-destruction native result");
        Check(calls == before + 1 && lastId == 0xf3148c42, "post-destruction native ID/count");
        std::puts("PASS: actual QueryInterface after NvApiTypes destruction");
    }
} lateCallback;

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(NvApiTypes::Instance().getId("NvAPI_D3D12_SetFlipConfig") == 0xf3148c42, "real lookup initialized");
    const std::string mode = argc > 1 ? argv[1] : "native";
    CloseLogger();
    NvApiHooks::o_NvAPI_QueryInterface = Original;
    if (mode == "native")
    {
        Check(NvApiHooks::hkNvAPI_QueryInterface(0xf3148c42) == &token, "native result");
        Check(lastId == 0xf3148c42 && calls == 1, "native ID/count");
        runLate = true;
    }
    else if (mode == "null")
    {
        // The successful Unhook path clears this slot; no stale trampoline is
        // available to invoke, and Config/fake fallback must not be entered.
        NvApiHooks::o_NvAPI_QueryInterface = nullptr;
        Check(NvApiHooks::hkNvAPI_QueryInterface(0xf3148c42) == nullptr, "unavailable native result");
    }
    else if (mode == "fake")
    {
        NvApiHooks::o_NvAPI_QueryInterface = reinterpret_cast<PFN_NvApi_QueryInterface>(fakenvapi::queryInterface);
        Check(NvApiHooks::hkNvAPI_QueryInterface(0xf3148c42) == nullptr, "fake lookup refused");
    }
    else if (mode == "unhook")
    {
        Check(DetourTransactionBegin() == NO_ERROR, "begin attach");
        Check(DetourUpdateThread(GetCurrentThread()) == NO_ERROR, "update attach thread");
        Check(DetourAttach(reinterpret_cast<PVOID*>(&NvApiHooks::o_NvAPI_QueryInterface),
                           NvApiHooks::hkNvAPI_QueryInterface) == NO_ERROR, "attach real callback");
        Check(DetourTransactionCommit() == NO_ERROR, "commit attach");
        Check(Original(0xf3148c42) == &token && calls == 1, "terminal detour forwards native trampoline");
        NvApiHooks::Unhook();
        Check(NvApiHooks::o_NvAPI_QueryInterface == nullptr, "successful Unhook clears reclaimed trampoline");
        Check(NvApiHooks::hkNvAPI_QueryInterface(0xf3148c42) == nullptr, "late callback after Unhook");
        Check(Original(0xf3148c42) == &token && calls == 2, "original remains callable after detach");
    }
    else if (mode == "arch")
    {
        NV_GPU_ARCH_INFO info {};
        NvApiHooks::o_NvAPI_GPU_GetArchInfo = Arch;
        Check(NvApiHooks::hkNvAPI_GPU_GetArchInfo(nullptr, &info) == NVAPI_OK, "arch result");
        Check(info.implementation == NV_GPU_ARCH_IMPLEMENTATION_TU106 + 1, "native arch preserved");
        NvApiHooks::o_NvAPI_GPU_GetArchInfo = nullptr;
        Check(NvApiHooks::hkNvAPI_GPU_GetArchInfo(nullptr, nullptr) == NVAPI_ERROR, "missing arch");
    }
    else if (mode == "setting")
    {
        NVDRS_SETTING info {};
        NvApiHooks::o_NvAPI_DRS_GetSetting = Setting;
        Check(NvApiHooks::hkNvAPI_DRS_GetSetting(nullptr, nullptr, NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_ID, &info) == NVAPI_OK,
              "setting result");
        Check(info.u32CurrentValue == 73, "native setting preserved");
        NvApiHooks::o_NvAPI_DRS_GetSetting = nullptr;
        Check(NvApiHooks::hkNvAPI_DRS_GetSetting(nullptr, nullptr, 0, nullptr) == NVAPI_ERROR, "missing setting");
    }
    else return 83;
    std::printf("PASS: %s terminal callback\n", mode.c_str());
}
