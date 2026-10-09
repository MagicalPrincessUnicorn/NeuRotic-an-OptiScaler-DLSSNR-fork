#pragma once
#include <d3d12.h>
#include "IFeature.h"

#include "SysUtils.h"
#include <Util.h>
#include <menu/menu_dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include <shaders/rcas/RCAS_Dx12.h>
#include <shaders/bias/Bias_Dx12.h>
#include <shaders/magnifier/Magnifier_Dx12.h>
#include <gpu_time/GpuTime_Dx12.h>
#include <dlssnr/NrGpuSafety.h>
#include <runtime/OwnedFeatureReleasePolicy.h>
#include <dlssnr/SharedDx12MenuUses.h>

class IFeature_Dx12 : public virtual IFeature
{
  private:
    DlssNr::GpuSafety::CompletionSet _recordedUses;
    bool _unobservedUse = false;
    void TrackUse(ID3D12GraphicsCommandList* list)
    {
        std::erase_if(_recordedUses, [](const auto& ticket) { return ticket && DlssNr::GpuSafety::Reusable(ticket); });
        auto ticket = DlssNr::GpuSafety::Record(list);
        if (!ticket) _unobservedUse = true;
        else if (std::find(_recordedUses.begin(), _recordedUses.end(), ticket) == _recordedUses.end())
            _recordedUses.push_back(std::move(ticket));
    }
    struct ShaderPass
    {
        // Requests the target buffer it needs to write to. Returns the buffer the PREVIOUS stage must write to
        std::function<ID3D12Resource*(ID3D12Resource* nextOutput)> Setup;

        // Runs the shader
        std::function<bool(ID3D12Resource* input, ID3D12Resource* output)> Dispatch;

        // Internal state tracked by the pipeline setup loop
        ID3D12Resource* inputBuffer = nullptr;
        ID3D12Resource* outputBuffer = nullptr;
    };

  protected:
    ID3D12Device* Device = nullptr;
    static inline std::unique_ptr<Menu_Dx12> Imgui = nullptr;
    static inline DlssNr::SharedDx12MenuUses ImguiUses;
    std::unique_ptr<OS_Dx12> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Dx12> RCAS = nullptr;
    std::unique_ptr<Bias_Dx12> Bias = nullptr;
    std::unique_ptr<Magnifier_Dx12> Magnifier = nullptr;

    std::unique_ptr<GpuTime_Dx12> UpscalerTime = nullptr;

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState) const;

    virtual bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;
    virtual bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;

  public:
    // Called by the owning context before destruction, while failure can still
    // preserve the complete object and permit a later release retry.
    virtual NVSDK_NGX_Result ReleaseProvider() { return NVSDK_NGX_Result_Success; }
    virtual bool ProviderReleaseQuarantined() const { return false; }
    static bool TryRetireSharedMenu();
    bool CanRetire() const
    {
        return !ProviderReleaseQuarantined() && !_unobservedUse && std::all_of(_recordedUses.begin(), _recordedUses.end(),
            [](const auto& ticket) { return ticket && DlssNr::GpuSafety::Reusable(ticket); });
    }
    // Waiting is a distinct state from missing observation, device loss or
    // opaque provider failure. This grants retention, never GPU reuse/release.
    bool CanDeferRetirement() const
    {
        if(ProviderReleaseQuarantined()||_unobservedUse||!Device||
           FAILED(Device->GetDeviceRemovedReason())||_recordedUses.empty())return false;
        return std::all_of(_recordedUses.begin(),_recordedUses.end(),[](const auto& ticket){
            const auto observed=DlssNr::GpuSafety::InspectRecording(ticket,nullptr,false);
            return ticket&&observed.valid&&observed.registryHealthy;
        });
    }
    bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters);
    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters);

    API Api() const override { return API::DX12; }
    std::optional<double> ReadUpscalerTime(void* commandQueue) override;
    void ReadDetailedGpuTimes(void* commandQueue, std::vector<DetailedGpuTime>& detailedGpuTimes) override;

    IFeature_Dx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~IFeature_Dx12();
};
