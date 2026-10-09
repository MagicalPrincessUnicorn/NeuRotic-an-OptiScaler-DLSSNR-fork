#pragma once

#include <vulkan/vulkan.h>

#include <shaders/dlssnr/DlssNr_Common.h>

#include <optional>
#include <string>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include "VulkanNrSession.h"
#include "VulkanNrStreamline.h"
#include "VulkanNrCapabilities.h"
#include "VulkanNrResolution.h"
#include "VulkanNrPassPlan.h"

// Native Windows Vulkan NR. Model generations own images, parameters and temporal history.
// Composition descriptors and constants remain immutable until their recording uses end.
// Native Performance uses private carriers and restores the game's parameters and observed
// command state around the selected SR call. RR remains after reconstruction. Present uses
// a separate session with caller-owned private images and actual submission completion.

class Config;
template<class Source> struct NrConfigSnapshot;

namespace DlssNr
{
struct TelemetrySnapshot;
namespace Capability { struct NativeObservation; class WriterPort; }
TelemetrySnapshot NativeTelemetryVk();
// Requires new NR admission gated and render callbacks joined. Read-only;
// inactive maintenance must independently retire any retained owners.
bool CanYieldVulkanOutput(std::string& reason);
Capability::NativeObservation CopyCapabilityObservationVk(const Capability::WriterPort&,
    const Capability::WriterPort* lifecycle) noexcept;
void ObserveNativeVk(NVSDK_NGX_Parameter*, const VkNrTemporalMetadata*, const NrConfigSnapshot<Config>&, bool rayReconstruction);
void ObserveNativeResultVk(const VkRecordResult&, bool beforeSr, bool bound, bool srSucceeded, bool selected=true);

// Runs the model over what the upscaler just wrote, on the same command buffer.
//
// Everything Vulkan needs that D3D12 does not is passed rather than looked up: the device handles
// belong to the game's instance and there is no ambient place to find them from here.
//
// Safe to call every frame. It builds what it needs on first use and disables itself for the session
// rather than retrying into a crash.
VkRecordResult EvaluateAfterUpscaleVk(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* params, VkInstance instance,
                                      VkPhysicalDevice physicalDevice, VkDevice device,
                                      const NrConfigSnapshot<Config>* settings = nullptr,
                                      const VkNrTemporalMetadata* temporal = nullptr, bool rayReconstruction = false,
                                      const VkNrEvaluationIdentity* evaluation = nullptr);
// Records image-only Present using private caller-owned color and neutral guide images.
// targetColor must be private storage; it must never be the game's swapchain image.
VkRecordResult EvaluatePresentImageVk(const VkFrameRequest& request);
VkRecordResult EvaluateBeforeUpscaleVk(const VkFrameRequest&, NVSDK_NGX_Parameter*, const NrConfigSnapshot<Config>&);
VkNrCapabilitySnapshot CurrentVulkanNrCapabilities();
void ClearSelectedVkNrFinalColor(VkCommandBuffer);
void ObserveSelectedVkNrFinalColor(VkCommandBuffer,NVSDK_NGX_Parameter*,VkInstance,VkPhysicalDevice,VkDevice,const VkNrTemporalMetadata&,const NrConfigSnapshot<Config>&,const VkNrEvaluationIdentity&);
std::optional<VkNrRecordedOutput> PrepareVkNrFinalColor(VkCommandBuffer,uint64_t provider,uint64_t frame,uint32_t viewport,VkNrTaggedColor);
std::optional<VkNrRecordedOutput> SelectedVkNrOutput(VkCommandBuffer);
void RecordPerformanceCaptureVk(VkCommandBuffer,NVSDK_NGX_Parameter*,const NVSDK_NGX_Handle* live);
void CompletePreSrVk(const VkRecordResult&, bool bound, bool srSucceeded);
// A discarded Present command buffer may contain model creation or evaluation commands.
// Prevent reuse of that model state after the buffer is discarded or submission is rejected.
void AbandonPresentRecordingVk(bool currentCounted = true);
void MarkPresentRuntimeUncertainVk(const char* reason);

// Whether the native Vulkan path is up, and why not if it is not.
bool IsRunningVk();
const char* FailureReasonVk();
void RequestHistoryResetVk();

// How many frames it has actually composed. The menu needs this to tell "up but nothing has come
// through yet" apart from "running", and the D3D12 counters say nothing about this path.
unsigned long long FramesVk();
unsigned long long CompletedFramesVk();

// What the pass last cost on the GPU, in milliseconds, or nothing if it has not been measured yet.
// A timestamp pair either side of the whole pass, read three frames later so the query is retired.
std::optional<double> LastGpuTimeVk();
std::string TuningStatusVk();
uint64_t PresentGenerationPrivateBytesVk();
VkNrWorkloadStatus WorkloadStatusVk(VkNrRoute);
VkNrChainResult PassChainStatusVk(VkNrRoute);
bool NativeRayReconstructionVk();
float BestExposureScanVk(int* index=nullptr,float* low=nullptr,float* high=nullptr);
std::optional<VkNrRecordedOutput> RecordedVkNrOutput(VkNrRoute,VkCommandBuffer);

// Whether the game offers an exposure texture on this path. Presence alone does not
// prove qualified image rights/layout, completed readback, or a usable white point.
bool ExposureOfferedVk();

void ShutdownVk(bool deviceAlive = true);
void RetryShutdownVk(VkDevice device);
// Render-boundary maintenance; uses try-locks and actual completion/recording
// ownership, never a device-idle or fence wait. Does not shut down the provider.
void RetireInactiveVk(VkDevice device,bool enabled,uint32_t selectedRoute);
void DeviceDestroyedVk(VkDevice device);
void NotifyDeviceInitVk();

} // namespace DlssNr
