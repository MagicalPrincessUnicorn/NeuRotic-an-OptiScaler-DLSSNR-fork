#pragma once

#include "DlssNr_PresentPacing.h"
#include "PreFg.h"
#include "NrAdvisorSampling.h"

#include <dxgi1_6.h>
#include <string>

namespace DlssNr
{
enum class PresentApi : unsigned int
{
    Unknown,
    D3D11,
    D3D12,
    Vulkan
};

struct PresentTelemetrySnapshot
{
    AdvisorSampling::Cadence cadence;
    bool requested = false;
    bool active = false;
    bool failed = false;
    bool policyBlocked = false;
    std::string policyGuardrail;
    PresentApi api = PresentApi::Unknown;
    unsigned int backbufferWidth = 0;
    unsigned int backbufferHeight = 0;
    DXGI_FORMAT backbufferFormat = DXGI_FORMAT_UNKNOWN;
    unsigned int backbufferSampleCount = 0;
    DXGI_SWAP_EFFECT swapEffect = DXGI_SWAP_EFFECT_DISCARD;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_CUSTOM;
    bool colorSpaceObserved = false;
    bool hdrDescriptorTransitioning = false;
    unsigned long long hdrObservationSequence = 0;
    unsigned long long hdrDescriptorGeneration = 0;
    unsigned long long hdrResizeGeneration = 0;
    HRESULT lastColorSpaceResult = S_FALSE;
    DXGI_HDR_METADATA_TYPE hdrMetadataType = DXGI_HDR_METADATA_TYPE_NONE;
    unsigned int hdrMetadataSize = 0;
    unsigned long long hdrMetadataHash = 0;
    HRESULT lastHdrMetadataResult = S_FALSE;
    unsigned int workload = 0;
    unsigned int resolution = 1;
    unsigned int workWidth = 0;
    unsigned int workHeight = 0;
    unsigned long long modelEvaluations = 0;
    unsigned long long compositeEvaluations = 0;
    unsigned long long skippedFrames = 0;
    unsigned long long modelSubmissions = 0;
    unsigned long long compositeSubmissions = 0;
    unsigned long long presentAttempts = 0;
    unsigned long long consecutiveFallbacks = 0;
    unsigned long long lastFallbackAttempt = 0;
    bool historyResetPending = true;
    unsigned long long uninterruptedFrames = 0;
    std::string historyResetReason = "initial Present frame";
    std::string historyInvalidationReason;
    unsigned long long lastSubmittedFence = 0;
    unsigned long long lastCompletedFence = 0;
    unsigned long long adapterCpuSlowCalls = 0;
    unsigned long long originalPresentSlowCalls = 0;
    unsigned int pendingSlots = 0;
    double adapterCpuMs = 0.0;
    double adapterCpuMaxMs = 0.0;
    double frameIntervalMs = 0.0;
    double originalPresentMs = 0.0;
    double originalPresentMaxMs = 0.0;
    // Latest successfully matched GPU timestamp for the route that submitted it. The monotonically
    // increasing sample count lets UI observers consume each completion once without affecting the
    // pacing window or the render path.
    double presentGpuMs = 0.0;
    bool presentGpuValid = false;
    unsigned long long presentGpuSamples = 0;
    unsigned long long resourceGeneration = 0;
    PresentPacing::Route presentGpuRoute = PresentPacing::Route::NativeTemporal;
    bool hasPacingSummary = false;
    unsigned long long unmatchedGpuTimingSamples = 0;
    PresentPacing::WindowSummary pacingSummary;
    std::string requestedPlacement = "Native Temporal";
    std::string actualPlacement = "Native Temporal";
    std::string compatibilityPath;
    std::string fallbackReason;
    std::string failure;
};

struct PresentCallIdentity
{
    uint64_t advisorConfigurationGeneration = 0;
    PresentPacing::CallToken pacing;
    unsigned long long presentAttempt = 0;
    // Set only after the complete private output path has reached the game backbuffer.  The original
    // Present result decides whether this frame may become temporal history for the next one.
    bool completedOutput = false;
    // Successful model recording/submission with a tracked completion signal;
    // warmup can prepare work without publishing it or clearing the game's FG tags.
    bool modelPrepared = false;
    bool probeSubmitted = false;
    // A submitted copyback must not be canceled as if the backbuffer were unchanged,
    // even when its signal fails. Own the fence across the Present lock boundary.
    bool copybackSubmitted = false;
    Microsoft::WRL::ComPtr<ID3D12Fence> completionFence;
    unsigned long long completionValue = 0;
    ID3D12Resource* outputResource = nullptr;
};

struct PresentCallTimingSample
{
    PresentCallIdentity identity;
    double frameIntervalMs = 0.0;
    double adapterCpuMs = 0.0;
    double hookCpuMs = 0.0;
    double originalPresentCpuMs = 0.0;
    HRESULT result = S_OK;
    bool verifiedNative = false;
    uint64_t providerGeneration = 0;
};

// Called immediately before the one original Present/Present1 call. The adapter never presents.
PresentCallIdentity EvaluatePresentImageOnly(IDXGISwapChain* swapChain, IUnknown* presentDevice,
                                             UINT presentFlags,
                                             const DXGI_PRESENT_PARAMETERS* presentParameters,
                                             PreFg::Frame* preFgFrame = nullptr);
void ReportPresentUnavailable(PresentApi api, const char* reason);
// One behavior-neutral CPU observation reported after the original game Present call. GPU timing is
// collected separately from non-blocking timestamps and the existing completion fence.
void ReportPresentCallTiming(const PresentCallTimingSample& sample);
PresentTelemetrySnapshot PresentTelemetry();
const char* PresentWorkloadName(unsigned int workload);
float PresentWorkloadScale(unsigned int workload);
unsigned int PresentWorkDimension(unsigned int fullDimension, unsigned int workload);
} // namespace DlssNr
