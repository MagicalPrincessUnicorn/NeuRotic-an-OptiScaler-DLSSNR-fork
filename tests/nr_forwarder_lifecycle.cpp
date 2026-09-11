// Exercises the production forwarder with fake NGX entry points; no model/GPU or game is loaded.
#include "../OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

namespace {
int initCalls = 0;
int shutdownCalls = 0;
int createCalls = 0;
int initResult = 1;
int shutdownResult = 1;

int __cdecl FakeInit(unsigned long long, const wchar_t*, ID3D12Device*, int, const void*) {
    ++initCalls;
    return initResult;
}
int __cdecl FakeVkInit(unsigned long long, const wchar_t*, void*, void*, void*, const void*, int) {
    ++initCalls;
    return initResult;
}
int __cdecl FakeShutdown() { ++shutdownCalls; return shutdownResult; }
int __cdecl FakeCreate(ID3D12GraphicsCommandList*, int, const void*, void** handle) {
    ++createCalls;
    *handle = reinterpret_cast<void*>(42);
    return 1;
}
int __cdecl FakeVkCreate(void*, int, const void*, void**) { return 1; }
int __cdecl FakeEval(ID3D12GraphicsCommandList*, const void*, const void*, void*) { return 1; }
int __cdecl FakeRelease(void*) { return 1; }
std::map<std::string, unsigned int> uints;
std::map<std::string, float> floats;
void __thiscall SetTestUInt(void*, const char* name, unsigned int value) { uints[name] = value; }
void __thiscall SetTestFloat(void*, const char* name, float value) { floats[name] = value; }
void __thiscall SetTestResource(void*, const char*, unsigned long long) {}

void* vtable[16] {};
void** params = vtable;
auto* deviceA = reinterpret_cast<ID3D12Device*>(1);
auto* deviceB = reinterpret_cast<ID3D12Device*>(2);

void* Create(ID3D12Device* device) {
    return dlssnr_call_create(L"unused", L"unused", device, nullptr, &params,
                              64, 64, 0, 1.f, 0, 1.f, 1.f, -1.f, 1, 1);
}
}

int main() {
    vtable[VT_SET_UINT] = reinterpret_cast<void*>(SetTestUInt);
    vtable[VT_SET_ULL] = reinterpret_cast<void*>(SetTestResource);
    vtable[1] = reinterpret_cast<void*>(SetTestFloat);
    g_snip.module = reinterpret_cast<HMODULE>(1); // suppress library loading
    g_snip.init = FakeInit;
    g_snip.create = FakeCreate;
    g_snip.evaluate = FakeEval;
    g_snip.release = FakeRelease;
    g_snip.shutdown = FakeShutdown;

    const unsigned int origins[] = {13, 7, 2, 5};
    assert(dlssnr_call_evaluate_guided(nullptr, deviceA, &params, nullptr, nullptr, nullptr, nullptr,
        1920, 1080, 1280, 720, 1, 1, 1.f, 0, 1.f, 1.f, -1.f, 1, 960.f, -540.f, .125f, -.25f, origins) == 1);
    assert(uints["DLSSNR.DepthSubrectBaseX"] == 13 && uints["DLSSNR.MVecSubrectBaseY"] == 5);
    assert(uints["DLSSNR.DepthSubrectWidth"] == 1280 && uints["DLSSNR.Width"] == 1920);
    assert(uints["DLSSNR.Reset"] == 1 && uints["DLSSNR.DepthInverted"] == 1);
    assert(floats["DLSSNR.MVecScaleY"] == -540.f && floats["Jitter.Offset.X"] == .125f);
    assert(dlssnr_call_evaluate(nullptr, deviceA, &params, nullptr, nullptr, nullptr, nullptr,
        3840, 2160, 1280, 720, 0, 0, 1.f, 0, 1.f, 1.f, -1.f, 1, 1920.f, -1080.f, .25f, -.5f) == 1);
    assert(uints["DLSSNR.DepthSubrectBaseX"] == 0 && uints["DLSSNR.MVecSubrectBaseY"] == 0);
    assert(floats["Jitter.Offset.X"] == .25f && floats["DLSSNR.MVecScaleY"] == -1080.f);
    std::puts("PASS Enhanced parameter contract: subrect origins, sizes, motion, jitter, reset; legacy Native clears offsets.");

    // Cold/double shutdown must be a no-op; each reopened generation must really reinitialize.
    assert(dlssnr_call_shutdown() == 1 && shutdownCalls == 0);
    for (int cycle = 0; cycle < 100; ++cycle) {
        const int beforeInit = initCalls;
        const int beforeShutdown = shutdownCalls;
        assert(Create(deviceA) != nullptr);
        assert(Create(deviceA) != nullptr && initCalls == beforeInit + 1);
        const int beforeCreate = createCalls;
        assert(Create(deviceB) == nullptr && createCalls == beforeCreate);
        assert(dlssnr_call_shutdown() == 1 && shutdownCalls == beforeShutdown + 1);
        assert(!g_snip.initialised && !g_snip.device && g_floatSlot == 1);
        assert(dlssnr_call_shutdown() == 1 && shutdownCalls == beforeShutdown + 1);
        assert(Create(deviceB) != nullptr && initCalls == beforeInit + 2);
        assert(dlssnr_call_shutdown() == 1);
    }

    // Missing cleanup export must prevent creation; init failure must not reach CreateFeature.
    g_snip.shutdown = nullptr;
    const int beforeCreate = createCalls;
    assert(Create(deviceA) == nullptr && createCalls == beforeCreate);
    g_snip.shutdown = FakeShutdown;
    initResult = -7;
    assert(Create(deviceA) == nullptr && !g_snip.initialised && createCalls == beforeCreate);
    initResult = 1;

    // Do not report a clean generation if native shutdown fails.
    assert(Create(deviceA) != nullptr);
    shutdownResult = -8;
    assert(dlssnr_call_shutdown() == -8 && g_snip.initialised && g_snip.device == deviceA);
    shutdownResult = 1;
    assert(dlssnr_call_shutdown() == 1);

    g_vk.module = reinterpret_cast<HMODULE>(1);
    g_vk.init = FakeVkInit;
    g_vk.create = FakeVkCreate;
    g_vk.shutdown = FakeShutdown;
    const int vkBefore = initCalls;
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceA, 21) == 1);
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceA, 21) == 1);
    assert(initCalls == vkBefore + 1);
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceB, 21) != 1);
    assert(dlssnr_vk_shutdown(1) == 1);
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceB, 21) == 1);
    const int beforeAbandon = shutdownCalls;
    assert(dlssnr_vk_shutdown(0) == 1 && shutdownCalls == beforeAbandon);
    assert(!g_vk.initialised && !g_vk.device);
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceA, 21) != 1);
    assert(dlssnr_vk_shutdown(1) == 1 && shutdownCalls == beforeAbandon);
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceA, 21) != 1);
    // New process fixture: abandonment cannot be repaired by resetting only host handles.
    g_vk.abandoned = false;
    assert(dlssnr_vk_init(L"unused", L"unused", nullptr, nullptr, deviceA, 21) == 1);
    shutdownResult = -9;
    assert(dlssnr_vk_shutdown(1) == -9 && g_vk.initialised);
    shutdownResult = 1;
    assert(dlssnr_vk_shutdown(1) == 1);
    const int afterVkShutdown = shutdownCalls;
    assert(dlssnr_vk_shutdown(1) == 1 && shutdownCalls == afterVkShutdown);
    std::puts("PASS: 100 D3D12 generation cycles, device mismatch, missing exports, init/shutdown failures, Vulkan restart/abandon/idempotence");
}
