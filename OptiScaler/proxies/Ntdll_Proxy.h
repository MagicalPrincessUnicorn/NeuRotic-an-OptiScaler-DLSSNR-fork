#pragma once

#include "SysUtils.h"

#include <detours/detours.h>

#include <winternl.h>
#include "KernelBase_Proxy.h"

class NtdllProxy
{
    // Win32's original loader still enters our NTDLL detour. Internal loads
    // must retain the native search policy without recursively re-interposing.
    inline static thread_local unsigned internalLoadDepth = 0;
    struct InternalLoadScope
    {
        InternalLoadScope() { ++internalLoadDepth; }
        ~InternalLoadScope() { --internalLoadDepth; }
        InternalLoadScope(const InternalLoadScope&) = delete;
        InternalLoadScope& operator=(const InternalLoadScope&) = delete;
    };

  public:
    static bool IsInternalLoad() noexcept { return internalLoadDepth != 0; }
    typedef NTSTATUS(NTAPI* PFN_LdrLoadDll)(PWSTR PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                            PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_NtLoadDll)(PUNICODE_STRING PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                           PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_LdrUnloadDll)(PVOID ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_RtlGetVersion)(PRTL_OSVERSIONINFOW lpVersionInformation);

    // It's a partial implementation
    static HMODULE LoadLibraryExW_Ldr(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        // Win32 search flags are not LdrLoadDll flags. Preserve their dependency
        // search semantics through the original Win32 trampoline.
        constexpr DWORD searchFlags = LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
            LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
        if (dwFlags & searchFlags)
        {
            KernelBaseProxy::Init();
            auto load = KernelBaseProxy::LoadLibraryExW_();
            if (!load) { SetLastError(ERROR_PROC_NOT_FOUND); return nullptr; }
            std::wstring qualifiedName;
            auto requestedName = lpLibFileName;
            if ((dwFlags & searchFlags) == LOAD_LIBRARY_SEARCH_SYSTEM32 && lpLibFileName &&
                *lpLibFileName && !std::filesystem::path(lpLibFileName).has_parent_path())
            {
                // Search flags do not override Windows' loaded-module lookup.
                // A bare proxy name (winmm/dxgi/etc.) otherwise resolves to us.
                wchar_t systemDirectory[MAX_PATH]{};
                auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
                if (!length || length >= MAX_PATH)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return nullptr;
                }
                qualifiedName = (std::filesystem::path(systemDirectory) / lpLibFileName).wstring();
                requestedName = qualifiedName.c_str();
            }
            InternalLoadScope internalLoad;
            return load(requestedName, hFile, dwFlags);
        }
        // LdrLoadDll wants a ULONG*, remove unsupported flags
        ULONG ldrFlags = dwFlags & ~(LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);

        // This will receive the module handle:
        HANDLE hModule = nullptr;

        NTSTATUS status;
        if (dwFlags & LOAD_LIBRARY_SEARCH_SYSTEM32)
        {
            static std::filesystem::path sysDir = []
            {
                wchar_t buffer[MAX_PATH];
                GetSystemDirectoryW(buffer, MAX_PATH);
                return std::filesystem::path(buffer);
            }();

            std::filesystem::path sysPath = sysDir / lpLibFileName;

            UNICODE_STRING uName;
            o_RtlInitUnicodeString(&uName, sysPath.c_str());

            status = o_LdrLoadDll(nullptr,                        // null is default search order
                                  ldrFlags ? &ldrFlags : nullptr, // optional flags
                                  &uName,                         // the name of the DLL
                                  &hModule                        // out: module handle
            );
        }
        else
        {
            UNICODE_STRING uName;
            o_RtlInitUnicodeString(&uName, lpLibFileName);

            status = o_LdrLoadDll(nullptr,                        // null is default search order
                                  ldrFlags ? &ldrFlags : nullptr, // optional flags
                                  &uName,                         // the name of the DLL
                                  &hModule                        // out: module handle
            );
        }

        if (NT_SUCCESS(status))
        {
            return static_cast<HMODULE>(hModule);
        }
        else
        {
            // translate NTSTATUS to a Win32 error code:
            SetLastError(o_RtlNtStatusToDosError(status));
            return nullptr;
        }
    }

    static NTSTATUS FreeLibrary_Ldr(PVOID handle) { return o_LdrUnloadDll(handle); }

    static void Init()
    {
        if (o_RtlInitUnicodeString != nullptr)
            return;

        _dll = GetModuleHandleW(L"ntdll.dll");

        if (_dll == nullptr)
            return;

        o_RtlInitUnicodeString = (PFN_RtlInitUnicodeString) GetProcAddress(_dll, "RtlInitUnicodeString");
        o_RtlNtStatusToDosError = (PFN_RtlNtStatusToDosError) GetProcAddress(_dll, "RtlNtStatusToDosError");
        o_RtlGetVersion = (PFN_RtlGetVersion) GetProcAddress(_dll, "RtlGetVersion");
        o_LdrLoadDll = (PFN_LdrLoadDll) GetProcAddress(_dll, "LdrLoadDll");
        o_LdrUnloadDll = (PFN_LdrUnloadDll) GetProcAddress(_dll, "LdrUnloadDll");
        o_NtLoadDll = (PFN_NtLoadDll) GetProcAddress(_dll, "NtLoadDll");
    }

    static HMODULE Module() { return _dll; }

    static PFN_RtlGetVersion Hook_RtlGetVersion(PVOID method)
    {
        auto addr = o_RtlGetVersion;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        DetourTransactionCommit();

        o_RtlGetVersion = addr;
        return addr;
    }

    static PFN_LdrLoadDll Hook_LdrLoadDll(PVOID method)
    {
        auto addr = o_LdrLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_LdrLoadDll = addr;
        return addr;
    }

    static PFN_LdrUnloadDll Hook_LdrUnloadDll(PVOID method)
    {
        auto addr = o_LdrUnloadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_LdrUnloadDll = addr;
        return addr;
    }

    static PFN_NtLoadDll Hook_NtLoadDll(PVOID method)
    {
        auto addr = o_NtLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_NtLoadDll = addr;
        return addr;
    }

  private:
    typedef decltype(&RtlInitUnicodeString) PFN_RtlInitUnicodeString;
    typedef decltype(&RtlNtStatusToDosError) PFN_RtlNtStatusToDosError;

    inline static HMODULE _dll = nullptr;

    inline static PFN_LdrLoadDll o_LdrLoadDll = nullptr;
    inline static PFN_LdrUnloadDll o_LdrUnloadDll = nullptr;
    inline static PFN_NtLoadDll o_NtLoadDll = nullptr;
    inline static PFN_RtlInitUnicodeString o_RtlInitUnicodeString = nullptr;
    inline static PFN_RtlNtStatusToDosError o_RtlNtStatusToDosError = nullptr;
    inline static PFN_RtlGetVersion o_RtlGetVersion = nullptr;
};
