#pragma once
#include "SysUtils.h"
#include <d3d12.h>
#include <nr/d3d12/NativeRecordingState.h>
#include <dlssnr/NativeHostReturnObservation.h>
#include <optional>

struct NativeStateRestorePoint
{
    ID3D12GraphicsCommandList* nativeList = nullptr;
    std::uint64_t incarnation = 0;
    Neurotic::D3D12::NativeRecordingState::Snapshot state;
};

class D3D12Hooks
{
  private:
    inline static std::mutex hookMutex;
    inline static std::mutex agilityMutex;

    static bool RestoreDescriptorHeaps(ID3D12GraphicsCommandList* cmdList);
    static bool RestorePipelineState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreComputeRootState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreGraphicsRootState(ID3D12GraphicsCommandList* cmdList);

  public:
    // Local helper commands must not replace the legacy game-binding cache.
    // Strict native recording observation remains enabled throughout this scope.
    class ScopedLegacyCaptureSuppression {
      public:
        ScopedLegacyCaptureSuppression() noexcept;
        ~ScopedLegacyCaptureSuppression();
        ScopedLegacyCaptureSuppression(const ScopedLegacyCaptureSuppression&)=delete;
        ScopedLegacyCaptureSuppression& operator=(const ScopedLegacyCaptureSuppression&)=delete;
    };
    static bool LegacyCaptureSuppressed() noexcept;
    using NativeRecordingObservation = Neurotic::D3D12::NativeRecordingObservation;
    static NativeRecordingObservation ObserveNativeRecording(ID3D12GraphicsCommandList* commandList);
    static DlssNr::NativeRecordingDiagnosticV1 DiagnoseNativeRecording(ID3D12GraphicsCommandList* commandList);
    static std::optional<NativeStateRestorePoint> CapturePostSrState(
        ID3D12GraphicsCommandList* commandList, Neurotic::D3D12::RestoreMask mask,
        Neurotic::D3D12::NativeStateCaptureDiagnostic* diagnostic = nullptr);
    static bool RestorePostSrState(const NativeStateRestorePoint& snapshot);
    static bool RegisterNativeRootLayout(ID3D12RootSignature* signature,
                                         std::vector<Neurotic::D3D12::RootParameter> layout);
    static std::optional<D3D12_RESOURCE_STATES> KnownHudResourceState(ID3D12GraphicsCommandList*, ID3D12Resource*);
    static bool WatchNativeResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                    std::uint64_t generation, UINT subresource, D3D12_RESOURCE_STATES knownState);
    static void Hook();
    static void HookAgility(HMODULE module);
    static void HookDevice(ID3D12Device* device);
    static void Unhook();
    static void SetRootSignatureTracking(bool enable);
    static bool CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList);
    static void HookToCommandListLate(ID3D12GraphicsCommandList* commandList);
    static void InstallNativeRecordingHooks(ID3D12GraphicsCommandList* commandList);
    static void RestoreRoot(ID3D12GraphicsCommandList* cmdList);
};
