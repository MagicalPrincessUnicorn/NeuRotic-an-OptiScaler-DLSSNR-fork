#include "pch.h"
#include "Dxgi_Hooks.h"

#include "DxgiFactory_Hooks.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <proxies/Streamline_Proxy.h>
#include <wrapped/wrapped_factory.h>

#include <DllNames.h>
#include <misc/IdentifyGpu.h>
#include <with_dx12/with_dx12.h>

#include "Hook_Utils.h"

static std::atomic<DxgiProxy::PFN_CreateDxgiFactory> o_CreateDXGIFactory = nullptr;
static std::atomic<DxgiProxy::PFN_CreateDxgiFactory1> o_CreateDXGIFactory1 = nullptr;
static std::atomic<DxgiProxy::PFN_CreateDxgiFactory2> o_CreateDXGIFactory2 = nullptr;
// Serialize installers only. Hook callers never wait on a loader-facing lock.
static std::mutex hookMutex;
struct FactoryDispatch
{
    DxgiProxy::PFN_CreateDxgiFactory create;
    DxgiProxy::PFN_CreateDxgiFactory1 create1;
    DxgiProxy::PFN_CreateDxgiFactory2 create2;
};
static FactoryDispatch ReadFactoryDispatch()
{
    return {o_CreateDXGIFactory.load(std::memory_order_acquire),
            o_CreateDXGIFactory1.load(std::memory_order_acquire),
            o_CreateDXGIFactory2.load(std::memory_order_acquire)};
}
static bool creatingD3D12DeviceForLuma = false;

#pragma intrinsic(_ReturnAddress)

static void InitD3D12DeviceForLuma(IDXGIFactory* factory)
{
    IDXGIAdapter* hardwareAdapter = nullptr;
    IdentifyGpu::getHardwareAdapter(factory, &hardwareAdapter, D3D_FEATURE_LEVEL_11_0);

    if (hardwareAdapter == nullptr)
        LOG_WARN("Can't get hardwareAdapter, will try nullptr!");

    State::Instance().currentD3D12Device = WithDx12::RequestD3D12Device(D3D_FEATURE_LEVEL_11_0, hardwareAdapter);

    LOG_DEBUG("currentD3D12Device: {:X}", (size_t) State::Instance().currentD3D12Device);
}

static void CheckLumaAndReShade(IDXGIFactory* factory)
{
    if (!Config::Instance()->LoadReShade.value_or_default() ||
        !Config::Instance()->CreateD3D12DeviceForLuma.value_or_default() ||
        State::Instance().currentD3D12Device != nullptr || creatingD3D12DeviceForLuma)
    {
        return;
    }

    auto rsFile = Util::ExePath().parent_path() / L"ReShade64.dll";
    if (reshadeModule == nullptr)
    {
        auto rsFileExist = std::filesystem::exists(rsFile);
        if (!rsFileExist)
        {
            Config::Instance()->LoadReShade.set_volatile_value(false);
            return;
        }
    }

    // For Luma mod + Agility update we are creating D3D12 device early to prevent issues with Luma
    if (Config::Instance()->CreateD3D12DeviceForLuma.value_or_default() &&
        State::Instance().currentD3D12Device == nullptr)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        creatingD3D12DeviceForLuma = true;

        LOG_INFO("Applying Luma DX12 workaround - creating D3D12 device early");
        InitD3D12DeviceForLuma(factory);

        creatingD3D12DeviceForLuma = false;
    }

    // Loading Reshade after Luma's D3D12 device creation to prevent conflicts
    if (reshadeModule == nullptr && Config::Instance()->LoadReShade.value_or_default())
    {
        SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

        if (skModule != nullptr)
            SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

        State::EnableServeOriginal(201);
        reshadeModule = NtdllProxy::LoadLibraryExW_Ldr(rsFile.c_str(), NULL, 0);
        State::DisableServeOriginal(201);

        LOG_INFO("Loading ReShade64.dll, result: {0:X}", (size_t) reshadeModule);
    }
}

VALIDATE_HOOK(hkCreateDXGIFactory, DxgiProxy::PFN_CreateDxgiFactory)
inline static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    const auto original = ReadFactoryDispatch();
    if (!original.create) return E_FAIL;

    // Driver teardown can create a factory after CloseLogger(). Preserve the
    // native call without touching logging, config, wrappers or plugin loaders.
    if (State::Instance().isShuttingDown)
        return original.create(riid, ppFactory);

    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return original.create(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return original.create(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = original.create(riid, ppFactory);
#else
    result = original.create2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif

    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    {
        IDXGIFactory* real = nullptr;

        if (State::Instance().activeFgOutput != FGOutput::DLSSG &&
            Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        {
            *ppFactory = real;
        }

        if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
            *ppFactory = (IDXGIFactory*) (new WrappedIDXGIFactory7(*ppFactory));
        else
            DxgiFactoryHooks::HookToFactory(*ppFactory);
    }

    CheckLumaAndReShade(*ppFactory);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory1, DxgiProxy::PFN_CreateDxgiFactory1)
inline static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    const auto original = ReadFactoryDispatch();
    if (!original.create1) return E_FAIL;

    if (State::Instance().isShuttingDown)
        return original.create1(riid, ppFactory);

    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return original.create1(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return original.create1(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = original.create1(riid, ppFactory);
#else
    result = original.create2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    {
        IDXGIFactory1* real = nullptr;

        if (State::Instance().activeFgOutput != FGOutput::DLSSG &&
            Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        {
            *ppFactory = real;
        }

        if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
            *ppFactory = (IDXGIFactory1*) (new WrappedIDXGIFactory7(*ppFactory));
        else
            DxgiFactoryHooks::HookToFactory(*ppFactory);
    }

    CheckLumaAndReShade(*ppFactory);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory2, DxgiProxy::PFN_CreateDxgiFactory2)
inline static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    const auto original = ReadFactoryDispatch();
    if (!original.create2) return E_FAIL;

    if (State::Instance().isShuttingDown)
        return original.create2(Flags, riid, ppFactory);

    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return original.create2(Flags, riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return original.create2(Flags, riid, ppFactory);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = original.create2(Flags, riid, ppFactory);
#else
    result = original.create2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    {
        IDXGIFactory2* real = nullptr;

        if (State::Instance().activeFgOutput != FGOutput::DLSSG &&
            Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        {
            *ppFactory = real;
        }

        if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
            *ppFactory = (IDXGIFactory2*) (new WrappedIDXGIFactory7(*ppFactory));
        else
            DxgiFactoryHooks::HookToFactory(*ppFactory);
    }

    CheckLumaAndReShade(*ppFactory);

    return result;
}

void DxgiHooks::Hook()
{
    std::lock_guard<std::mutex> lock(hookMutex);

    // If not spoofing and
    // using no frame generation (or Nukem's) and
    // not using DXGI spoofing we don't need DXGI hooks
    // Probably I forgot something but we can add it later
    if (!Config::Instance()->OverlayMenu.value_or_default() &&
        (Config::Instance()->FGInput.value_or_default() == FGInput::NoFG ||
         Config::Instance()->FGInput.value_or_default() == FGInput::NvngxFG) &&
        !Config::Instance()->DxgiSpoofing.value_or_default())
    {
        return;
    }

    if (DxgiProxy::Module() == nullptr)
        return;

    LOG_DEBUG("");

    if (o_CreateDXGIFactory.load(std::memory_order_acquire))
        return;

    // Publish all prepared originals before any factory hook becomes callable.
    // Publishing after Commit allows the startup GPU worker into a null call.
    auto module = State::Instance().workingMode == WorkingMode::Dxgi ? dllModule : DxgiProxy::Module();
    PVOID targets[] = {
        reinterpret_cast<PVOID>(KernelBaseProxy::GetProcAddress_()(module, "CreateDXGIFactory")),
        reinterpret_cast<PVOID>(KernelBaseProxy::GetProcAddress_()(module, "CreateDXGIFactory1")),
        reinterpret_cast<PVOID>(KernelBaseProxy::GetProcAddress_()(module, "CreateDXGIFactory2"))};
    PVOID hooks[] = {reinterpret_cast<PVOID>(hkCreateDXGIFactory),
                     reinterpret_cast<PVOID>(hkCreateDXGIFactory1),
                     reinterpret_cast<PVOID>(hkCreateDXGIFactory2)};
    PDETOUR_TRAMPOLINE prepared[3]{};
    auto error = DetourTransactionBegin();
    if (error != NO_ERROR) return;
    error = DetourUpdateThread(GetCurrentThread());
    for (unsigned i = 0; error == NO_ERROR && i < 3; ++i)
        error = targets[i] ? DetourAttachEx(&targets[i], hooks[i], &prepared[i], nullptr, nullptr)
                           : ERROR_PROC_NOT_FOUND;
    if (error != NO_ERROR)
    {
        DetourTransactionAbort();
        LOG_ERROR("Failed to prepare DXGI factory hooks: {:X}", error);
        return;
    }
    o_CreateDXGIFactory.store(reinterpret_cast<DxgiProxy::PFN_CreateDxgiFactory>(prepared[0]), std::memory_order_release);
    o_CreateDXGIFactory1.store(reinterpret_cast<DxgiProxy::PFN_CreateDxgiFactory1>(prepared[1]), std::memory_order_release);
    o_CreateDXGIFactory2.store(reinterpret_cast<DxgiProxy::PFN_CreateDxgiFactory2>(prepared[2]), std::memory_order_release);
    error = DetourTransactionCommit();
    if (error != NO_ERROR)
    {
        o_CreateDXGIFactory.store(nullptr, std::memory_order_release);
        o_CreateDXGIFactory1.store(nullptr, std::memory_order_release);
        o_CreateDXGIFactory2.store(nullptr, std::memory_order_release);
        LOG_ERROR("Failed to commit DXGI factory hooks: {:X}", error);
    }
}
