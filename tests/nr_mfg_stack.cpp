#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include <windows.h>
#include <malloc.h>
#include <cstdio>

static DWORD WINAPI ObserverOnSmallStack(void*)
{
    __try
    {
        // GetProcAddress hooks see unrelated exports too. A no-op observer
        // must not exhaust the caller's 128 KiB worker/fiber stack.
        SetLastError(12345);
        Neurotic::Mfg::ObserveAdaExportResolution(reinterpret_cast<HMODULE>(1),
            "UnrelatedExport", reinterpret_cast<void*>(1), reinterpret_cast<void*>(1));
        Neurotic::Mfg::ObserveAdaExportResolution(nullptr, nullptr, nullptr, nullptr);
        const auto owner = GetModuleHandleW(nullptr);
        SetLastError(12345);
        Neurotic::Mfg::ObserveAdaExportResolution(owner,
            "NVSDK_NGX_D3D12_CreateFeature", reinterpret_cast<void*>(&ObserverOnSmallStack),
            reinterpret_cast<void*>(&ObserverOnSmallStack));
        if (GetLastError() != 12345) return 2;
        // Exercise the path-name helpers reached by FreeLibrary interception.
        if (Neurotic::Mfg::IsAdaMfgModule(owner)) return 5;
        SetLastError(12345);
        Neurotic::Mfg::ObserveAdaExportResolution(owner, reinterpret_cast<const char*>(0xffff),
            reinterpret_cast<void*>(1), reinterpret_cast<void*>(1));
        return GetLastError() == 12345 ? 0 : 2;
    }
    __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        _resetstkoflw();
        return EXCEPTION_STACK_OVERFLOW;
    }
}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto thread = CreateThread(nullptr, 128 * 1024, ObserverOnSmallStack, nullptr,
        STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) return 3;
    const auto wait = WaitForSingleObject(thread, 10000);
    DWORD result = 4;
    if (wait == WAIT_OBJECT_0) GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    if (result)
    {
        std::fprintf(stderr, "FAIL: no-op MFG export observer on 128 KiB stack: 0x%08lx\n", result);
        return 1;
    }
    std::puts("PASS: MFG export observer returns safely on 128 KiB stack and preserves LastError");
}
