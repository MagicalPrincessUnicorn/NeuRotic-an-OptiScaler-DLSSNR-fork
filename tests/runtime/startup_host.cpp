#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <dxgi.h>

static LONG WINAPI Fault(EXCEPTION_POINTERS* fault)
{
    std::fprintf(stderr, "FAULT code=%08lx address=%p\n", fault->ExceptionRecord->ExceptionCode,
                 fault->ExceptionRecord->ExceptionAddress);
    // A null indirect call leaves its caller at the exception stack pointer.
    // Record the owning module/RVA without loading a symbol provider in a hook.
    auto caller = *reinterpret_cast<void* const*>(fault->ContextRecord->Rsp);
    MEMORY_BASIC_INFORMATION owner{};
    if (VirtualQuery(caller, &owner, sizeof(owner)) && owner.AllocationBase)
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(static_cast<HMODULE>(owner.AllocationBase), path, MAX_PATH);
        std::fprintf(stderr, "FAULT caller=%p module=%ls rva=%llx\n", caller, path,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(caller) -
                                            reinterpret_cast<uintptr_t>(owner.AllocationBase)));
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(Fault);
    if (argc != 2 && argc != 3) return 2;
    std::printf("BEFORE LoadLibrary\n"); std::fflush(stdout);
    auto module = LoadLibraryW(argv[1]);
    std::printf("AFTER LoadLibrary module=%p error=%lu\n", module, module ? 0 : GetLastError());
    std::fflush(stdout);
    if (!module) return 3;
    if (argc == 3)
    {
        std::printf("BEFORE restricted DXGI load\n"); std::fflush(stdout);
        auto dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        std::printf("AFTER restricted DXGI load module=%p error=%lu\n", dxgi, dxgi ? 0 : GetLastError());
        std::fflush(stdout);
        return dxgi ? 0 : 5;
    }
    auto leaf = std::wcsrchr(argv[1], L'\\'); leaf = leaf ? leaf + 1 : argv[1];
    const char* entry = "timeGetTime";
    enum class Kind { Timer, Version, DebugHelp, Internet, Dxgi };
    auto kind = Kind::Timer;
    if (!_wcsicmp(leaf, L"version.dll")) { entry = "GetFileVersionInfoSizeW"; kind = Kind::Version; }
    else if (!_wcsicmp(leaf, L"dbghelp.dll")) { entry = "SymGetOptions"; kind = Kind::DebugHelp; }
    else if (!_wcsicmp(leaf, L"wininet.dll")) { entry = "InternetGetConnectedState"; kind = Kind::Internet; }
    else if (!_wcsicmp(leaf, L"dxgi.dll")) { entry = "CreateDXGIFactory1"; kind = Kind::Dxgi; }
    std::printf("BEFORE GetProcAddress %s\n", entry); std::fflush(stdout);
    auto address = GetProcAddress(module, entry);
    std::printf("AFTER GetProcAddress address=%p\n", address); std::fflush(stdout);
    if (address)
    {
        auto bytes = reinterpret_cast<const unsigned char*>(address);
        if (bytes[0] == 0x48) ++bytes;
        if (bytes[0] == 0xff && bytes[1] == 0x25)
        {
            LONG displacement = 0; std::memcpy(&displacement, bytes + 2, sizeof(displacement));
            auto destination = *reinterpret_cast<void* const*>(bytes + 6 + displacement);
            MEMORY_BASIC_INFORMATION memory{}; VirtualQuery(destination, &memory, sizeof(memory));
            std::printf("FORWARD destination=%p owner=%p self=%d\n", destination, memory.AllocationBase,
                        memory.AllocationBase == module); std::fflush(stdout);
            if (memory.AllocationBase == module) return 4;
        }
    }
    if (!address) return 6;
    if (kind == Kind::Timer || kind == Kind::DebugHelp)
    {
        auto call = reinterpret_cast<DWORD(WINAPI*)()>(address);
        std::printf("FORWARD %s=%lu\n", entry, call());
    }
    else if (kind == Kind::Version)
    {
        DWORD unused = 0;
        auto call = reinterpret_cast<DWORD(WINAPI*)(LPCWSTR, LPDWORD)>(address);
        std::printf("FORWARD version bytes=%lu\n", call(argv[0], &unused));
    }
    else if (kind == Kind::Internet)
    {
        DWORD flags = 0;
        auto call = reinterpret_cast<BOOL(WINAPI*)(LPDWORD, DWORD)>(address);
        std::printf("FORWARD cached connectivity=%d\n", call(&flags, 0));
    }
    else
    {
        IDXGIFactory1* factory = nullptr;
        auto call = reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(address);
        auto result = call(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
        std::printf("FORWARD factory=%08lx\n", result); std::fflush(stdout);
        if (FAILED(result) || !factory) return 7;
        factory->Release();
    }
    std::fflush(stdout);
    std::printf("EXIT\n"); std::fflush(stdout);
    // Test process completion too; the runtime is intentionally process-pinned.
    return 0;
}
