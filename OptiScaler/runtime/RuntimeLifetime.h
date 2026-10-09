#pragma once
#include <Windows.h>
#include <atomic>

namespace Neurotic::Runtime
{
inline std::atomic<bool> processExiting { false };
inline bool BootstrapUnavailable() noexcept
{
    return processExiting.load(std::memory_order_acquire);
}
inline bool PinRuntime(HMODULE module) noexcept
{
    HMODULE retained = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(module), &retained) && retained == module;
}
inline void RequestProcessExit() noexcept { processExiting.store(true, std::memory_order_release); }
inline bool IsProcessExiting() noexcept { return processExiting.load(std::memory_order_acquire); }
}
