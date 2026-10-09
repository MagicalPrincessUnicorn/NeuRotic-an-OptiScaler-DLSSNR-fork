#pragma once
#include "CharacterPipeline.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
namespace Neurotic::Semantic::Character {
class CharacterCaptureDx11 {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 CharacterCaptureDx11();~CharacterCaptureDx11();
 std::uint64_t AdmissionToken() const noexcept;
 void InvalidateAdmission();
 bool TrySubmit(ID3D11Device*,ID3D11Texture2D*,CpuFrame);
 bool TrySubmitSwapchain(ID3D11Device*,IDXGISwapChain*,CpuFrame);
 // Control worker: device/private resource creation only, never immediate context.
 void PrepareRequested();
 // Worker: CPU move only. No immediate-context access.
 bool TryTakeNewest(CpuFrame&);
 // Serialized render/resize boundary ONLY. Never call these from the worker.
 // Boundary device must match the active capture; foreign contexts are never polled.
 void PollRetirement(ID3D11Device* boundary);
 bool RetireSources(ID3D11Device* boundary,unsigned timeoutMs);
 // Exact renderer/device release boundary: drop CPU ownership without querying
 // or flushing an immediate context. D3D11 retains queued GPU resource lifetimes.
 void ReleaseDevice(ID3D11Device* boundary);
};
}
