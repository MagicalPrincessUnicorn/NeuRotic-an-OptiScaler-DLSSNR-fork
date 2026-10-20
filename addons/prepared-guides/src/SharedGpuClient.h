#pragma once
#include <dlssnr/PreparedVkDx12Transport.h>
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include <reshade_api.hpp>
#include <array>
#include <memory>
#include <string>
namespace nrpg {
// A single queue-owned slot. Call on the ReShade queue's serialized owner thread.
// Source color is render_target, guides shader_resource, all full-sized SDR images.
// The caller keeps every source alive until Process completes, or retains its
// source owner after RequiresRestart. Return true proves copyback GPU completion,
// never presentation or observed display. No device extensions are enabled here.
bool ValidateSharedInput(const Neurotic::Feed::Prepared::Descriptor&,
                         const std::array<reshade::api::resource_desc,4>&,std::string&);
class SharedGpuClient {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SharedGpuClient();~SharedGpuClient();
    SharedGpuClient(const SharedGpuClient&)=delete;SharedGpuClient& operator=(const SharedGpuClient&)=delete;
    // transportOnly makes no model call. It reads actual completed D3D12 source
    // bytes and then recaptures the actual Vulkan copyback target, comparing
    // bounded stage hashes. It is an optional roundtrip diagnostic, not a render.
    bool Process(reshade::api::command_queue*,const Neurotic::Feed::Prepared::Descriptor&,
                 const std::array<reshade::api::resource,4>&,std::string& reason,bool transportOnly=false);
    bool RequiresRestart()const;
    bool Preparing()const;
    std::uint64_t CompletedCapture()const;
    std::uint64_t CompletionValue()const;
    std::uint64_t InputDigest()const;
    std::uint64_t ProcessedDigest()const;
    std::uint64_t CopybackDigest()const;
    // Exact canonical completion, provider release and recording retirement
    // evidence only. A completed frame by itself does not supply all four facts.
    bool Retire(const DlssNr::PreparedTransport::DrainEvidence&,std::string& reason);
};
}
