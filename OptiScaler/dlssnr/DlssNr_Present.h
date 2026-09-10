#pragma once

#include "DlssNr_PresentPacing.h"

#include <dxgi1_4.h>
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
    bool requested = false;
    bool active = false;
    bool failed = false;
    PresentApi api = PresentApi::Unknown;
    unsigned int backbufferWidth = 0;
    unsigned int backbufferHeight = 0;
    DXGI_FORMAT backbufferFormat = DXGI_FORMAT_UNKNOWN;
    unsigned int backbufferSampleCount = 0;
    DXGI_SWAP_EFFECT swapEffect = DXGI_SWAP_EFFECT_DISCARD;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_CUSTOM;
    unsigned int workload = 0;
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
    double originalPresentMs = 0.0;
    double originalPresentMaxMs = 0.0;
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
    PresentPacing::CallToken pacing;
    unsigned long long presentAttempt = 0;
    // Set only after the complete private output path has reached the game backbuffer.  The original
    // Present result decides whether this frame may become temporal history for the next one.
    bool completedOutput = false;
};

struct PresentCallTimingSample
{
    PresentCallIdentity identity;
    double frameIntervalMs = 0.0;
    double adapterCpuMs = 0.0;
    double hookCpuMs = 0.0;
    double originalPresentCpuMs = 0.0;
    HRESULT result = S_OK;
};

// Called immediately before the one original Present/Present1 call. The adapter never presents.
PresentCallIdentity EvaluatePresentImageOnly(IDXGISwapChain* swapChain, IUnknown* presentDevice,
                                             UINT presentFlags,
                                             const DXGI_PRESENT_PARAMETERS* presentParameters);
void ReportPresentUnavailable(PresentApi api, const char* reason);
// One behavior-neutral CPU observation reported after the original game Present call. GPU timing is
// collected separately from non-blocking timestamps and the existing completion fence.
void ReportPresentCallTiming(const PresentCallTimingSample& sample);
PresentTelemetrySnapshot PresentTelemetry();
const char* PresentWorkloadName(unsigned int workload);
float PresentWorkloadScale(unsigned int workload);
unsigned int PresentWorkDimension(unsigned int fullDimension, unsigned int workload);
} // namespace DlssNr
