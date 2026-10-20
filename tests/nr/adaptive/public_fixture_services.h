#pragma once
#include "public_fixture_lifetime.h"
#if defined(NR_ADAPTIVE_PUBLIC_PROVIDER_PORT_AVAILABLE)
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace DlssNr::AdaptivePublicFixture
{
// Fixture startup calls Bind once, before opening the real renderer. Pin the
// module for process lifetime: this service never claims provider/GPU drain.
// These are loader dependencies only, not rendering/admission capabilities.
class Services final
{
    inline static std::mutex mutex_;
    inline static HMODULE module_=nullptr;
    inline static std::filesystem::path path_;
public:
    static bool Bind(HMODULE module)
    {
        if(!module)return false;
        using ReadIdentity=const char* (*)();
        const auto readIdentity=reinterpret_cast<ReadIdentity>(
            GetProcAddress(module,"nr_adaptive_public_fixture_identity_v1"));
        if(!readIdentity)return false;
        const char* identity=readIdentity();
        if(!identity||!ResolveLifetime(identity))return false;
        wchar_t path[32768]{};
        const auto length=GetModuleFileNameW(module,path,32768);
        if(!length||length>=32768)return false;
        std::filesystem::path candidate(std::wstring(path,length));
        std::lock_guard lock(mutex_);
        if(module_)return module_==module&&path_==candidate;
        // No implicit private model/core fallback is possible in this service.
        HMODULE retained=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(readIdentity),&retained))return false;
        if(retained!=module){FreeLibrary(retained);return false;}
        path_=std::move(candidate);module_=retained;
        return true;
    }
    static HMODULE Forwarder()
    {std::lock_guard lock(mutex_);return module_;}
    static std::optional<ProviderLifetimeContract> Lifetime()
    {
        // Entry receives the contract of the authenticated, retained public
        // module. A bare constant or caller-supplied identity grants nothing.
        const auto module=Forwarder();if(!module)return {};
        using ReadIdentity=const char* (*)();
        const auto readIdentity=reinterpret_cast<ReadIdentity>(
            GetProcAddress(module,"nr_adaptive_public_fixture_identity_v1"));
        if(!readIdentity)return {};
        const auto* identity=readIdentity();
        return identity?ResolveLifetime(identity):std::nullopt;
    }
    static std::optional<std::filesystem::path> ModelPath()
    {std::lock_guard lock(mutex_);if(!module_)return {};return path_;}
};
}
#endif
