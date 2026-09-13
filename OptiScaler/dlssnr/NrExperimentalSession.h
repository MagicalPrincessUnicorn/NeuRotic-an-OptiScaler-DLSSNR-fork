#pragma once

#include "NrExperimentalPolicy.h"

#include <Config.h>
#include <Util.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>

namespace DlssNr::ExperimentalSession
{
namespace Detail
{
constexpr std::uint32_t Magic = 0x4E525845u; // EXRN
struct Marker
{
    std::uint32_t magic = Magic;
    std::uint32_t version = 1;
    std::uint32_t active = 1;
    std::uint32_t processId = 0;
    std::uint64_t processCreated = 0;
};

inline std::once_flag initializeOnce;
inline HANDLE markerHandle = INVALID_HANDLE_VALUE;
inline std::atomic<bool> recoveredUnclean { false };
inline std::atomic<bool> concurrentOwner { false };
inline std::atomic<bool> markerUnavailable { false };
inline std::atomic<bool> noticeConsumed { false };
inline std::atomic<double> initializedAtMs { 0.0 };

inline std::filesystem::path MarkerPath()
{
    return Util::DllPath().parent_path() / L"NeuRotic-experimental-session.state";
}

inline std::uint64_t CreationTime(HANDLE process)
{
    FILETIME created {}, exited {}, kernel {}, user {};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

inline bool ReadMarker(Marker& value)
{
    HANDLE file = CreateFileW(MarkerPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    const bool ok = ReadFile(file, &value, sizeof(value), &read, nullptr) && read == sizeof(value) &&
                    value.magic == Magic && value.version == 1;
    CloseHandle(file);
    return ok;
}

inline bool MarkerOwnerAlive(const Marker& value)
{
    if (!value.active || value.processId == 0) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, value.processId);
    if (process == nullptr) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
                       CreationTime(process) == value.processCreated;
    CloseHandle(process);
    return alive;
}

inline bool WriteMarker(bool active)
{
    if (markerHandle == INVALID_HANDLE_VALUE)
        markerHandle = CreateFileW(MarkerPath().c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (markerHandle == INVALID_HANDLE_VALUE) return false;
    Marker value;
    value.active = active ? 1u : 0u;
    value.processId = GetCurrentProcessId();
    value.processCreated = CreationTime(GetCurrentProcess());
    LARGE_INTEGER start {};
    DWORD written = 0;
    return SetFilePointerEx(markerHandle, start, nullptr, FILE_BEGIN) &&
           WriteFile(markerHandle, &value, sizeof(value), &written, nullptr) && written == sizeof(value) &&
           SetEndOfFile(markerHandle) && FlushFileBuffers(markerHandle);
}
} // namespace Detail

inline void DisableInMemory(Config& config)
{
    config.DlssNrExperimentalMode = false;
    config.DlssNrOverrideMultipassGuardrails = false;
    config.DlssNrOverrideHdrGuardrails = false;
    config.DlssNrOverrideFgGuardrails = false;
    config.DlssNrPreSrSoftReset = false;
    ExperimentalPolicy::Changed();
}

inline void Initialize(Config* config)
{
    if (config == nullptr) return;
    std::call_once(Detail::initializeOnce, [config]() {
        Detail::initializedAtMs.store(Util::MillisecondsNow(), std::memory_order_release);
        Detail::Marker previous;
        const bool hasActiveMarker = Detail::ReadMarker(previous) && previous.active;
        const bool ownerAlive = hasActiveMarker && Detail::MarkerOwnerAlive(previous);
        const bool unclean = hasActiveMarker && !ownerAlive;
        const bool ownedByAnotherProcess = ownerAlive &&
                                           previous.processId != GetCurrentProcessId();
        if (unclean)
        {
            DisableInMemory(*config);
            config->SaveExperimentalSettings(false, false, false, false, false);
            Detail::WriteMarker(false);
            Detail::recoveredUnclean.store(true, std::memory_order_release);
        }
        if (ownedByAnotherProcess)
        {
            DisableInMemory(*config);
            Detail::concurrentOwner.store(true, std::memory_order_release);
        }
        else if (config->DlssNrExperimentalMode.value_or_default() && !Detail::WriteMarker(true))
        {
            DisableInMemory(*config);
            config->SaveExperimentalSettings(false, false, false, false, false);
            Detail::markerUnavailable.store(true, std::memory_order_release);
        }
        ExperimentalPolicy::SessionReady.store(true, std::memory_order_release);
    });
}

inline bool Applied(Config& config)
{
    const bool requested = config.DlssNrExperimentalMode.value_or_default();
    Initialize(&config);
    if (requested && !config.DlssNrExperimentalMode.value_or_default()) return false;
    if (config.DlssNrExperimentalMode.value_or_default())
    {
        if (!Detail::WriteMarker(true))
        {
            DisableInMemory(config);
            config.SaveExperimentalSettings(false, false, false, false, false);
            Detail::markerUnavailable.store(true, std::memory_order_release);
            return false;
        }
    }
    else
        Detail::WriteMarker(false);
    ExperimentalPolicy::Changed();
    return true;
}

inline bool ConsumeRecoveryNotice(double nowMs)
{
    if (!Detail::recoveredUnclean.load(std::memory_order_acquire) ||
        nowMs - Detail::initializedAtMs.load(std::memory_order_acquire) < 10000.0)
        return false;
    bool expected = false;
    return Detail::noticeConsumed.compare_exchange_strong(expected, true);
}

inline bool ConsumeConcurrentOwnerNotice()
{
    return Detail::concurrentOwner.exchange(false, std::memory_order_acq_rel);
}

inline bool ConsumeMarkerUnavailableNotice()
{
    return Detail::markerUnavailable.exchange(false, std::memory_order_acq_rel);
}

// Called during process detach. The handle is opened earlier, so this performs only fixed-size
// kernel I/O and does not allocate or load another module while the loader lock is held.
inline void MarkCleanShutdown() noexcept
{
    if (Detail::markerHandle == INVALID_HANDLE_VALUE) return;
    Detail::WriteMarker(false);
    CloseHandle(Detail::markerHandle);
    Detail::markerHandle = INVALID_HANDLE_VALUE;
}
} // namespace DlssNr::ExperimentalSession
