#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
#ifdef NRPG_CPU_MAINLINE_TESTING
#include <dlssnr/PreparedGuideDispatch.h>
#endif

namespace nrpg {
// Serialized caller; packed input is already-completed source readback, in
// color BGRA8 / depth float / motion float2 / distrust float planes (20 B/px).
class CpuMainlineClient {
public:
    struct GpuInputs {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12Resource> color,motion,distrust,depth;
        Microsoft::WRL::ComPtr<ID3D12Fence> producer;
        std::uint64_t value=0;
        std::shared_ptr<void> lease;
    };
    struct Completion { Microsoft::WRL::ComPtr<ID3D12Fence> fence; std::uint64_t value=0; };
    // Completed output in UAV state. Release the lease only after the consumer
    // finishes and restores UAV state; another dispatch is refused while leased.
    struct GpuOutput { Microsoft::WRL::ComPtr<ID3D12Resource> color; Completion completion; std::shared_ptr<void> lease; };
    bool ProcessResident(std::uint64_t,const Neurotic::Feed::Prepared::Descriptor&,
        const GpuInputs&,GpuOutput&,std::string&);
    CpuMainlineClient();
    ~CpuMainlineClient();
    CpuMainlineClient(const CpuMainlineClient&)=delete;
    CpuMainlineClient& operator=(const CpuMainlineClient&)=delete;
    bool Process(std::uint64_t adapterLuid,const Neurotic::Feed::Prepared::Descriptor&,
        std::span<const std::byte> packed,std::vector<std::uint8_t>& bgraOutput,std::string& reason);
    // Completed same-device input textures arrive and leave NON_PIXEL_SHADER_RESOURCE.
    // Only depth is uploaded; motion and distrust stay in their producer's GPU resources.
    // Caller registers the returned reader completion before returning the flow lease.
    bool ProcessGpu(std::uint64_t adapterLuid,const Neurotic::Feed::Prepared::Descriptor&,
        const GpuInputs&,std::span<const float> depth,std::vector<std::uint8_t>& bgraOutput,
        Completion&,std::string& reason);
    bool RequiresRestart() const;
    // True only after the latest Process completed sealed initialization work.
    // Process returns false and no color; this is safe to retry, not delivery.
    bool Preparing() const;
#ifdef NRPG_CPU_MAINLINE_TESTING
    struct TestRecorder {
        DlssNr::PreparedGuides::PrepareFn prepare;
        DlssNr::PreparedGuides::RecordFn record;
        DlssNr::PreparedGuides::SealFn seal;
        DlssNr::PreparedGuides::ReasonFn reason=nullptr;
    };
    explicit CpuMainlineClient(TestRecorder);
#endif
private:
    bool ProcessImpl(std::uint64_t,const Neurotic::Feed::Prepared::Descriptor&,
        std::span<const std::byte>,const GpuInputs*,std::span<const float>,
        std::vector<std::uint8_t>&,Completion*,std::string&,GpuOutput* resident=nullptr);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
