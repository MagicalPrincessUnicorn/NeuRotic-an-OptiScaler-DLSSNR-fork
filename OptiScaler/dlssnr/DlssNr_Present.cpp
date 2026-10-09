#include "pch.h"
#include "../nr/diagnostics/capability/CapabilityOwnerAdapters.h"
#include "FrameTrace.h"
#include "DredDiagnostics.h"
#include "DlssNr_Present.h"
#include "PreparedGuideRoute.h"
#include "DlssNr_PresentCompatibility.h"
#include "DlssNr_PresentHistory.h"
#include "DlssNr_PresentEffects.h"
#include "DlssNr_Multipass.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNr_PresentGuides.h"
#include "HdrObservation.h"
#include "NrExperimentalPolicy.h"
#include "NativeIdentity.h"
#include "VulkanPresentStatus.h"
#include "PresentCopybackRecording.h"
#include "PresentDx11CopyCompletion.h"

#include <shaders/format_transfer/FT_Dx12.h>
#include <with_dx12/dx11_with_dx12.h>

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <memory>
#include <wrl/client.h>

namespace DlssNr
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr unsigned int kSlotCount = 8;
constexpr std::array<unsigned int, 6> kWorkPercent = { 100, 77, 67, 58, 50, 33 };
constexpr std::array<const char*, 6> kWorkNames = {
    "Full / Native (100%)", "Ultra Quality (77%)", "Quality (67%)",
    "Balanced (58%)", "Performance (50%)", "Ultra Performance (33%)"
};

struct PresentSlot
{
    ComPtr<ID3D12CommandAllocator> modelAllocator;
    ComPtr<ID3D12CommandAllocator> compositeAllocator;
    UINT64 completion = 0;
    PresentPacing::CallToken pacing;
    UINT64 presentAttempt = 0;
    double firstSubmissionMs = 0.0;
    bool completionObserved = true;
    bool timingStarted = false;
    bool timingResolved = false;
    bool pacingExpected = false;
    UINT64 historyGeneration = 0;
};

struct PresentState
{
    std::mutex mutex;
    UINT64 guideGeneration = 0;
    UINT64 historyGeneration = 0;
    UINT64 resourceGeneration = 0;
    UINT64 resourceRouteKey = 0;
    UINT64 resourceGuideGeneration = 0, resourceResumeGeneration = 0;
    ComPtr<IUnknown> resourceSwapchainIdentity; // cookie only; never holds a backbuffer

    UINT64 readinessConfiguration = 0;
    std::optional<NrConfigSnapshot<Config>> readinessSettings;
    UINT nativeWidth = 0, nativeHeight = 0;
    DlssNrFrameInfo nativeFrame;
    PresentTelemetrySnapshot telemetry;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12QueryHeap> pacingQueryHeap;
    ComPtr<ID3D12Resource> pacingReadback;
    std::array<PresentSlot, kSlotCount> slots;
    ComPtr<ID3D12Resource> frame;
    ComPtr<ID3D12Resource> conversionSource;
    ComPtr<ID3D12Resource> conversionOutput;
    std::unique_ptr<FT_Dx12> inputTransfer;
    std::unique_ptr<FT_Dx12> outputTransfer;
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Input;
    Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C dx11Output;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12Resource> depthUpload;
    ComPtr<ID3D12Resource> motionUpload;
    UINT64 nextFence = 1;
    unsigned int nextSlot = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int workWidth = 0;
    unsigned int workHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_CUSTOM;
    PresentColor::Decision colorDecision;
    UINT64 colorIdentity = 0;
    UINT64 timestampFrequency = 0;
    UINT64 timingCallSequence = 0;
    PresentPacing::Window<> pacing;
    PresentHistory::Continuity history;
    PresentHistory::HostReturnGate hostReturn;
    bool presentWasRequested = false;
    PresentInputDecision::InputClass lastCommittedInputClass = PresentInputDecision::InputClass::Refused;
    unsigned int experimentalFlags = 0;
    UINT64 resumeGeneration = 0, inputInterruptionEpoch = 0;
    UINT64 lastHdrObservationSequence = 0;
    bool guidesNeedUpload = true;
    // Set when submitted work can no longer be paired with a trustworthy completion value, or when
    // an unsubmitted command list has already been registered with the shared GPU-safety tracker.
    // Retrying or releasing resources in either case would turn an untouched-frame fallback into a
    // use-after-submit risk, so only process teardown may reclaim this generation.
    bool completionUntrackable = false;
    // Independent proof for the D3D11 target write after the bridge's D3D12 work.
    PresentDx11CopyCompletion dx11OutputCompletion;
};

PresentState g_present;
int lastObservedGuideClass = -1; // protected by the existing Present owner lock

void SyncHistoryTelemetry()
{
    g_present.telemetry.historyResetPending = g_present.history.ResetForNextEvaluation();
    g_present.telemetry.uninterruptedFrames = g_present.history.CompletedFrames();
    g_present.telemetry.historyResetReason = g_present.history.ResetReason();
    g_present.telemetry.historyInvalidationReason = g_present.history.LastInvalidationReason();
}

void InvalidateHistory(const char* reason)
{
    ++g_present.historyGeneration;
    g_present.telemetry.presentGpuValid = false;
    g_present.history.Invalidate(reason != nullptr ? reason : "unknown continuity interruption");
    SyncHistoryTelemetry();
}

void EmitPacingSummary(const std::optional<PresentPacing::WindowSummary>& completed)
{
    if (!completed.has_value() || !completed->valid)
        return;

    const auto& summary = completed.value();
    const char* route = summary.route == PresentPacing::Route::PresentEnhanced ? "Present Enhanced" :
        summary.route == PresentPacing::Route::PresentImageOnly
                            ? "Present Image-Only" : "Native Temporal";
    g_present.telemetry.pacingSummary = summary;
    g_present.telemetry.hasPacingSummary = true;

    LOG_INFO("DLSS-NR Present pacing: window {} {} calls {}-{} | samples {} warm-up {} failed Presents {} | "
             "frame ms avg {:.2f} median {:.2f} p95 {:.2f} max {:.2f} | adapter CPU p95 {:.3f} max {:.3f} | "
             "hook CPU p95 {:.3f} max {:.3f} | original Present CPU p95 {:.3f} max {:.3f} | "
             "Present GPU {}/{} median {:.2f} p95 {:.2f} max {:.2f} ms | completion-observed upper bound "
             "p95 {:.2f} max {:.2f} ms | fence age p95 {:.0f} max {:.0f} attempts | pending high-water {} | "
             "missing GPU samples {}",
             summary.serial, route, summary.firstCall, summary.lastCall, summary.frameInterval.samples,
             summary.warmupDiscarded, summary.failedPresents, summary.frameInterval.average,
             summary.frameInterval.median, summary.frameInterval.p95, summary.frameInterval.maximum,
             summary.adapterCpu.p95, summary.adapterCpu.maximum, summary.hookCpu.p95,
             summary.hookCpu.maximum, summary.originalPresentCpu.p95,
             summary.originalPresentCpu.maximum, summary.presentGpu.samples,
             summary.expectedGpuSamples, summary.presentGpu.median, summary.presentGpu.p95,
             summary.presentGpu.maximum, summary.completionObservation.p95,
             summary.completionObservation.maximum, summary.fenceAge.p95, summary.fenceAge.maximum,
             summary.pendingSlotsHighWater, summary.missingGpuSamples);
}

void ReadCompletedPacingSample(PresentSlot& slot, unsigned int slotIndex, UINT64 currentAttempt)
{
    if (slot.completionObserved || slot.completion == 0)
        return;

    slot.completionObserved = true;
    if (!slot.pacingExpected || !slot.timingResolved || g_present.pacingReadback == nullptr ||
        g_present.timestampFrequency == 0)
        return;

    const UINT64 offset = static_cast<UINT64>(slotIndex) * 2ull * sizeof(UINT64);
    D3D12_RANGE readRange {static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + 2ull * sizeof(UINT64))};
    void* mapped = nullptr;
    if (FAILED(g_present.pacingReadback->Map(0, &readRange, &mapped)) || mapped == nullptr)
        return;

    const auto* timestamps = reinterpret_cast<const UINT64*>(
        static_cast<const unsigned char*>(mapped) + offset);
    const UINT64 start = timestamps[0];
    const UINT64 end = timestamps[1];
    const D3D12_RANGE noWrite {0, 0};
    g_present.pacingReadback->Unmap(0, &noWrite);
    if (end < start)
        return;

    const double gpuMs = static_cast<double>(end - start) /
                         static_cast<double>(g_present.timestampFrequency) * 1000.0;
    const double completionMs = std::max(0.0, Util::MillisecondsNow() - slot.firstSubmissionMs);
    const UINT64 fenceAge = currentAttempt >= slot.presentAttempt ? currentAttempt - slot.presentAttempt : 0;
    if (!g_present.pacing.recordGpu(slot.pacing, gpuMs, completionMs, fenceAge))
        ++g_present.telemetry.unmatchedGpuTimingSamples;
    else if (slot.historyGeneration == g_present.historyGeneration)
    {
        g_present.telemetry.presentGpuMs = gpuMs;
        g_present.telemetry.presentGpuValid = true;
        g_present.telemetry.presentGpuRoute = slot.pacing.route;
        ++g_present.telemetry.presentGpuSamples;
    }
}

void RefreshCompletionTelemetry()
{
    if (g_present.fence == nullptr)
    {
        g_present.telemetry.pendingSlots = 0;
        return;
    }

    const UINT64 completed = g_present.fence->GetCompletedValue();
    NR_FRAME_TRACE("nr-completion-poll", "fence={:p} completed={} submitted={}",
        static_cast<void*>(g_present.fence.Get()), completed, g_present.telemetry.lastSubmittedFence);
    if (completed == UINT64_MAX)
        return;

    g_present.telemetry.lastCompletedFence = completed;
    unsigned int pending = 0;
    for (unsigned int i = 0; i < kSlotCount; ++i)
    {
        auto& slot = g_present.slots[i];
        if (slot.completion != 0 && completed < slot.completion)
            ++pending;
        else if (slot.completion != 0)
            ReadCompletedPacingSample(slot, i, g_present.telemetry.presentAttempts);
    }
    g_present.telemetry.pendingSlots = pending;
}

void RecordSubmission(UINT64 signal)
{
    NR_FRAME_TRACE("nr-fence-signaled", "fence={:p} value={} attempt={}",
        static_cast<void*>(g_present.fence.Get()), signal, g_present.telemetry.presentAttempts);
    g_present.telemetry.lastSubmittedFence = signal;
    RefreshCompletionTelemetry();
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after) return;
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

void UavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

void SetFallback(PresentApi api, const char* reason, bool failed = false, const char* policyGuardrail = nullptr,
                 bool possibleTargetWrite = false)
{
    NR_FRAME_TRACE("nr-fallback", "attempt={} api={} failed={} reason={}", g_present.telemetry.presentAttempts,
        static_cast<unsigned int>(api), failed, reason ? reason : "unknown");
    const bool changedReason = g_present.telemetry.fallbackReason != (reason ? reason : "unknown fallback");
    // A refusal breaks continuity even when copyback was already submitted and the game target
    // might have changed. Its successor must never inherit an uncertain model result.
    InvalidateHistory(reason);
    g_present.telemetry.requested = true;
    g_present.telemetry.active = false;
    g_present.telemetry.actualInputClass = PresentInputDecision::InputClass::Refused;
    g_present.telemetry.api = api;
    g_present.telemetry.requestedPlacement = "Present";
    g_present.telemetry.actualPlacement = std::string(PresentEffects::FallbackPlacement(possibleTargetWrite));
    g_present.telemetry.possibleTargetWrite = possibleTargetWrite;
    g_present.telemetry.fallbackReason = reason != nullptr ? reason : "unknown fallback";
    g_present.telemetry.failure = failed ? g_present.telemetry.fallbackReason : "";
    g_present.telemetry.failed = failed;
    g_present.telemetry.policyBlocked = policyGuardrail != nullptr;
    g_present.telemetry.policyGuardrail = policyGuardrail != nullptr ? policyGuardrail : "";
    ++g_present.telemetry.skippedFrames;
    ++g_present.telemetry.consecutiveFallbacks;
    g_present.telemetry.lastFallbackAttempt = g_present.telemetry.presentAttempts;
    if (changedReason || g_present.telemetry.consecutiveFallbacks == 1 ||
        g_present.telemetry.consecutiveFallbacks % 300 == 0)
    LOG_WARN("DLSS-NR Present diagnostic: fallback #{} (streak {}, attempt {}): {} | target {}x{} format {} samples {} swap effect {} color space {} | completed fence {} | submitted fence {} | pending slots {}",
             g_present.telemetry.skippedFrames, g_present.telemetry.consecutiveFallbacks,
             g_present.telemetry.presentAttempts, g_present.telemetry.fallbackReason,
             g_present.telemetry.backbufferWidth, g_present.telemetry.backbufferHeight,
             static_cast<unsigned int>(g_present.telemetry.backbufferFormat),
             g_present.telemetry.backbufferSampleCount,
             static_cast<unsigned int>(g_present.telemetry.swapEffect),
             static_cast<unsigned int>(g_present.telemetry.colorSpace),
             g_present.telemetry.lastCompletedFence, g_present.telemetry.lastSubmittedFence,
             g_present.telemetry.pendingSlots);
}

bool AllComplete()
{
    if (!g_present.dx11OutputCompletion.CanYield()) return false;
    if (g_present.fence == nullptr) return true;
    const UINT64 done = g_present.fence->GetCompletedValue();
    if (done == UINT64_MAX) return false;
    for (const auto& slot : g_present.slots)
        if (slot.completion != 0 && done < slot.completion) return false;
    return true;
}

bool ReleaseResources()
{
    // A rebuild/partial failure never erases a pending or unprovable final copy.
    if (!g_present.dx11OutputCompletion.ResetCompleted()) return false;
    g_present.resourceSwapchainIdentity.Reset();
    g_present.resourceGuideGeneration = g_present.resourceResumeGeneration = 0;
    g_present.list.Reset();
    g_present.fence.Reset();
    g_present.pacingQueryHeap.Reset();
    g_present.pacingReadback.Reset();
    for (auto& slot : g_present.slots)
    {
        slot.modelAllocator.Reset();
        slot.compositeAllocator.Reset();
        slot.completion = 0;
        slot.pacing = {};
        slot.presentAttempt = 0;
        slot.firstSubmissionMs = 0.0;
        slot.completionObserved = true;
        slot.timingStarted = false;
        slot.timingResolved = false;
        slot.pacingExpected = false;
    }
    g_present.frame.Reset();
    g_present.conversionSource.Reset();
    g_present.conversionOutput.Reset();
    g_present.inputTransfer.reset();
    g_present.outputTransfer.reset();
    Dx11WithDx12::ReleaseSharedResource(&g_present.dx11Input);
    Dx11WithDx12::ReleaseSharedResource(&g_present.dx11Output);
    g_present.depth.Reset();
    g_present.motion.Reset();
    g_present.depthUpload.Reset();
    g_present.motionUpload.Reset();
    g_present.queue.Reset();
    g_present.device.Reset();
    g_present.nextFence = 1;
    g_present.nextSlot = 0;
    g_present.timestampFrequency = 0;
    g_present.guidesNeedUpload = true;
    g_present.completionUntrackable = false;
    return true;
}

bool CreateTexture(ID3D12Device* device, DXGI_FORMAT format, unsigned int width, unsigned int height,
                   D3D12_RESOURCE_STATES initialState, ComPtr<ID3D12Resource>& output,
                   D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
{
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return SUCCEEDED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void**>(output.ReleaseAndGetAddressOf())));
}

bool CreateUpload(ID3D12Device* device, ID3D12Resource* texture, bool depth,
                  ComPtr<ID3D12Resource>& upload)
{
    const D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 bytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowBytes, &bytes);
    if (bytes == 0 || rows == 0) return false;

    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()))))
        return false;

    unsigned char* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || mapped == nullptr)
        return false;
    std::memset(mapped, 0, static_cast<size_t>(bytes));
    if (depth)
    {
        for (UINT y = 0; y < rows; ++y)
        {
            float* row = reinterpret_cast<float*>(mapped + footprint.Offset + y * footprint.Footprint.RowPitch);
            for (unsigned int x = 0; x < textureDesc.Width; ++x) row[x] = 1.0f;
        }
    }
    upload->Unmap(0, nullptr);
    return true;
}

void RecordUpload(ID3D12GraphicsCommandList* list, ID3D12Device* device,
                  ID3D12Resource* texture, ID3D12Resource* upload)
{
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

bool BuildResources(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned int width,
                    unsigned int height, unsigned int workWidth, unsigned int workHeight,
                    DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace, const PresentColor::Decision& color)
{
    if (!ReleaseResources()) return false;
    g_present.device = device;
    g_present.queue = queue;
    g_present.width = width;
    g_present.height = height;
    g_present.workWidth = workWidth;
    g_present.workHeight = workHeight;
    g_present.format = format;
    g_present.colorSpace = colorSpace;
    g_present.colorDecision = color;

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(g_present.fence.GetAddressOf()))) ||
        !CreateTexture(device, color.workingFormat, width, height,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, g_present.frame) ||
        !CreateTexture(device, DXGI_FORMAT_R32_FLOAT, workWidth, workHeight,
                       D3D12_RESOURCE_STATE_COPY_DEST, g_present.depth) ||
        !CreateTexture(device, DXGI_FORMAT_R16G16_FLOAT, workWidth, workHeight,
                       D3D12_RESOURCE_STATE_COPY_DEST, g_present.motion) ||
        !CreateUpload(device, g_present.depth.Get(), true, g_present.depthUpload) ||
        !CreateUpload(device, g_present.motion.Get(), false, g_present.motionUpload))
    {
        ReleaseResources();
        return false;
    }

    if (format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM;
        const auto flags = bgra ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (!CreateTexture(device, format, width, height, D3D12_RESOURCE_STATE_COPY_DEST,
                           g_present.conversionSource, flags) ||
            !CreateTexture(device, format, width, height,
                           bgra ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           g_present.conversionOutput, flags))
        {
            ReleaseResources();
            return false;
        }
        const bool pq = color.profile == PresentColor::Profile::Hdr10Pq2020;
        g_present.inputTransfer = std::make_unique<FT_Dx12>(bgra ? "Present BGRA8 to RGBA8" :
            pq ? "Present PQ2020 to scRGB" : "Present R10 to RGBA8", device,
            color.workingFormat, bgra ? FT_Dx12::Transfer::Bgra8ToRgba8 :
            pq ? FT_Dx12::Transfer::Pq2020ToScRgb : FT_Dx12::Transfer::Copy);
        g_present.outputTransfer = std::make_unique<FT_Dx12>(bgra ? "Present RGBA8 to packed BGRA8" :
            pq ? "Present scRGB edit to PQ2020" : "Present RGBA8 to R10", device,
            format, bgra ? FT_Dx12::Transfer::Rgba8ToBgra8 :
            pq ? FT_Dx12::Transfer::ScRgbToPq2020 : FT_Dx12::Transfer::Copy);
        // These texture pairs remain fixed until the existing generation-drain gate permits
        // recreation. Never rewrite descriptors referenced by an in-flight conversion.
        if (!g_present.inputTransfer->Ready() || !g_present.outputTransfer->Ready() ||
            !g_present.inputTransfer->BindImmutableDescriptors(g_present.conversionSource.Get(), g_present.frame.Get()) ||
            !g_present.outputTransfer->BindImmutableDescriptors(g_present.frame.Get(), g_present.conversionOutput.Get(),
                pq ? g_present.conversionSource.Get() : nullptr))
        {
            ReleaseResources();
            return false;
        }
    }

    // Timing is best-effort observability. If any timestamp resource is unavailable, keep the
    // unchanged Present route running and report missing GPU samples instead of changing fallback.
    D3D12_QUERY_HEAP_DESC queryDesc {};
    queryDesc.Count = kSlotCount * 2;
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    D3D12_HEAP_PROPERTIES readbackHeap {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = kSlotCount * 2ull * sizeof(UINT64);
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(g_present.pacingQueryHeap.GetAddressOf()))) ||
        FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(g_present.pacingReadback.GetAddressOf()))) ||
        FAILED(queue->GetTimestampFrequency(&g_present.timestampFrequency)) ||
        g_present.timestampFrequency == 0)
    {
        g_present.pacingQueryHeap.Reset();
        g_present.pacingReadback.Reset();
        g_present.timestampFrequency = 0;
        LOG_WARN("DLSS-NR Present pacing: GPU timestamps unavailable; rendering continues unchanged");
    }

    for (auto& slot : g_present.slots)
    {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(slot.modelAllocator.GetAddressOf()))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(slot.compositeAllocator.GetAddressOf()))))
        {
            ReleaseResources();
            return false;
        }
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         g_present.slots[0].modelAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(g_present.list.GetAddressOf()))) ||
        FAILED(g_present.list->Close()))
    {
        ReleaseResources();
        return false;
    }
    DredDiagnostics::Name(g_present.frame.Get(), L"NR Present model frame");
    DredDiagnostics::Name(g_present.depth.Get(), L"NR Present depth");
    DredDiagnostics::Name(g_present.motion.Get(), L"NR Present motion");
    DredDiagnostics::Name(g_present.depthUpload.Get(), L"NR Present depth upload");
    DredDiagnostics::Name(g_present.motionUpload.Get(), L"NR Present motion upload");
    DredDiagnostics::Name(g_present.conversionSource.Get(), L"NR Present conversion input");
    DredDiagnostics::Name(g_present.conversionOutput.Get(), L"NR Present conversion output");
    DredDiagnostics::Name(g_present.fence.Get(), L"NR Present completion fence");
    DredDiagnostics::Name(g_present.list.Get(), L"NR Present model and copyback list");
    DredDiagnostics::Name(g_present.pacingReadback.Get(), L"NR Present timing readback");
    DredDiagnostics::Name(g_present.pacingQueryHeap.Get(), L"NR Present timing queries");
    for (auto& slot : g_present.slots)
    {
        DredDiagnostics::Name(slot.modelAllocator.Get(), L"NR Present model allocator");
        DredDiagnostics::Name(slot.compositeAllocator.Get(), L"NR Present copyback allocator");
    }
    return true;
}

bool SameDevice(ID3D12Device* a, ID3D12Device* b)
{
    return a != nullptr && b != nullptr && a->GetAdapterLuid().HighPart == b->GetAdapterLuid().HighPart &&
           a->GetAdapterLuid().LowPart == b->GetAdapterLuid().LowPart;
}

bool SameComObject(IUnknown* a, IUnknown* b)
{
    if (a == nullptr || b == nullptr) return false;
    ComPtr<IUnknown> identityA;
    ComPtr<IUnknown> identityB;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(identityA.GetAddressOf()))) &&
           SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(identityB.GetAddressOf()))) &&
           identityA.Get() == identityB.Get();
}

bool FormatCapabilities(ID3D12Device* device, DXGI_FORMAT format, bool& texture,
                        bool& shaderLoad, bool& typedStore)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support {format};
    if (device == nullptr || FAILED(device->CheckFeatureSupport(
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
        return false;
    texture = (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
    shaderLoad = (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
    typedStore = (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    return true;
}
} // namespace

const char* PresentWorkloadName(unsigned int workload)
{
    return kWorkNames[std::min(workload, 5u)];
}

float PresentWorkloadScale(unsigned int workload)
{
    return static_cast<float>(kWorkPercent[std::min(workload, 5u)]) / 100.0f;
}

unsigned int PresentWorkDimension(unsigned int fullDimension, unsigned int workload)
{
    if (fullDimension < 8) return 0;
    const unsigned int percent = kWorkPercent[std::min(workload, 5u)];
    const unsigned long long scaled =
        (static_cast<unsigned long long>(fullDimension) * percent + 50ull) / 100ull;
    unsigned int aligned = static_cast<unsigned int>((scaled + 4ull) & ~7ull);
    aligned = std::max(8u, aligned);
    return std::min(aligned, fullDimension & ~7u);
}

PresentTelemetrySnapshot PresentTelemetry()
{
    return PresentTelemetryForApi(State::Instance().swapchainApi == Vulkan);
}

PresentTelemetrySnapshot PresentTelemetryForApi(bool vulkan)
{
    if (vulkan &&
        Config::Instance()->DlssNrRoute.value_or_default() != 0)
    {
        const auto vk = GetVulkanPresentStatus().Snapshot();
        const auto runtime = Config::Instance()->GetDlssNrRuntimeSnapshot();
        PresentTelemetrySnapshot out {};
        out.api = PresentApi::Vulkan;
        out.requested = runtime.enabled && vk.requested;
        out.active = runtime.enabled && vk.active;
        out.failed = vk.failed;
        out.possibleTargetWrite = vk.possibleTargetWrite;
        out.requestedPlacement = "Present";
        out.actualPlacement = vk.actualInputClass == PresentInputDecision::InputClass::ImageOnly ?
            "Present Image Only" : vk.actualInputClass == PresentInputDecision::InputClass::Guided ? "Present Guided" : "Refused";
        out.requestedInputPolicy = vk.requestedPolicy;
        out.actualInputClass = vk.actualInputClass;
        out.backbufferWidth = vk.extent.width;
        out.backbufferHeight = vk.extent.height;
        out.workWidth = vk.workload.appliedWork.width;
        out.workHeight = vk.workload.appliedWork.height;
        out.workload = vk.workload.policy.scale;
        out.backbufferSampleCount = vk.format != VK_FORMAT_UNDEFINED ? 1 : 0;
        out.vkBackbufferFormat = vk.format;
        out.vkColorSpace = vk.colorSpace;
        out.vkFormatObserved = vk.format != VK_FORMAT_UNDEFINED;
        out.presentAttempts = vk.attempts;
        out.modelEvaluations = vk.recorded;
        out.compositeEvaluations = vk.composed;
        out.acceptedOutputPresents = (std::min)(vk.completed, vk.originalAccepted);
        out.modelSubmissions = vk.submitted;
        out.compositeSubmissions = vk.submitted;
        out.lastSubmittedFence = vk.submitted;
        out.lastCompletedFence = vk.completed;
        out.pendingSlots = static_cast<unsigned int>(std::min<unsigned long long>(
            vk.submitted - vk.completed, UINT32_MAX));
        out.vkOriginalPresents = vk.originalAccepted;
        out.vkUncertain = vk.uncertain;
        out.consecutiveFallbacks = vk.consecutiveFallbacks;
        out.fallbackReason = vk.active ? "" : vk.reason;
        out.failure = vk.failed ? vk.reason : "";
        out.signalFallbackReason = vk.active ? vk.reason : "";
        out.policyBlocked = vk.needsRecreate;
        out.policyGuardrail = vk.needsRecreate ? vk.reason : "";
        return out;
    }
    std::lock_guard<std::mutex> lock(g_present.mutex);
    return g_present.telemetry;
}

Capability::PresentObservation CopyPresentCapabilityObservation(const Capability::WriterPort& port) noexcept
{
    if (State::Instance().api == API::Vulkan)
    {
        try {
            const auto t=GetVulkanPresentStatus().Snapshot();
            Capability::PresentObservation o; o.sequence=Capability::ReserveSample(port).sequence;
            o.counters={t.recorded,t.composed,0,t.submitted,t.submitted,t.attempts,
                        t.consecutiveFallbacks,t.submitted-t.completed};
            o.observedCounters=0xfb; // Vulkan does not collect the skippedFrames total.
            return o;
        } catch(...) { return {}; }
    }
    std::lock_guard<std::mutex> lock(g_present.mutex);
    const auto& t=g_present.telemetry;
    Capability::PresentObservation o; o.sequence=Capability::ReserveSample(port).sequence;
    o.counters={t.modelEvaluations,t.compositeEvaluations,t.skippedFrames,t.modelSubmissions,
                t.compositeSubmissions,t.presentAttempts,t.consecutiveFallbacks,t.pendingSlots};
    return o;
}
bool CanYieldPresentOutput(std::string& reason)
{
    std::unique_lock lock(g_present.mutex,std::try_to_lock);
    if(!lock.owns_lock()){reason="Present owner is busy";return false;}
    if (g_present.completionUntrackable)
    {
        reason = "Present copyback completion is quarantined";
        return false;
    }
    if (!g_present.dx11OutputCompletion.CanYield())
    {
        reason = "Final D3D11 output copy completion is not independently verified";
        return false;
    }
    if (!AllComplete())
    {
        reason = "Present GPU completion is pending or unknown";
        return false;
    }
    if (!GpuSafety::CanYieldOutput())
    {
        reason = "NR recording completion or terminal ownership remains unresolved";
        return false;
    }
    reason.clear();
    return true;
}

void ReportPresentCallTiming(const PresentCallTimingSample& sample)
{
    std::lock_guard<std::mutex> lock(g_present.mutex);
    if (sample.identity.pacing.call != 0 &&
        !g_present.hostReturn.Accept(sample.identity.pacing.call))
        return;
    if (sample.identity.completedOutput && sample.result == S_OK)
        ++g_present.telemetry.acceptedOutputPresents;
    g_present.telemetry.frameIntervalMs = sample.frameIntervalMs;
    auto& cadence = g_present.telemetry.cadence;
    ++cadence.sequence;
    cadence.route = static_cast<unsigned int>(sample.identity.pacing.route);
    cadence.providerGeneration = sample.providerGeneration;
    cadence.resourceGeneration = g_present.telemetry.resourceGeneration;
    cadence.intervalMs = sample.frameIntervalMs;
    const auto& host = State::Instance();
    const bool fgMayBeActive = host.dlssgLastSetMode.load() != sl::DLSSGMode::eOff ||
        host.activeFgInput != FGInput::NoFG || host.activeFgOutput != FGOutput::NoFG;
    cadence.native = SUCCEEDED(sample.result) && (sample.verifiedNative || !fgMayBeActive);
    cadence.configurationGeneration = sample.identity.advisorConfigurationGeneration;
    cadence.source = !cadence.native ? AdvisorSampling::CadenceSource::Unknown : sample.verifiedNative
        ? AdvisorSampling::CadenceSource::VerifiedPreFgPresent : AdvisorSampling::CadenceSource::ApplicationPresentFgOff;
    if (sample.identity.pacing.route != PresentPacing::Route::NativeTemporal &&
        sample.identity.completedOutput)
    {
        if (SUCCEEDED(sample.result))
        {
            g_present.history.CompleteOutputPresent();
            SyncHistoryTelemetry();
        }
        else
        {
            InvalidateHistory("original Present failed");
        }
    }
    if (sample.identity.pacing.route != PresentPacing::Route::NativeTemporal)
    {
        g_present.telemetry.adapterCpuMs = sample.adapterCpuMs;
        g_present.telemetry.adapterCpuMaxMs =
            std::max(g_present.telemetry.adapterCpuMaxMs, sample.adapterCpuMs);
        if (sample.adapterCpuMs >= 4.0)
            ++g_present.telemetry.adapterCpuSlowCalls;
        g_present.telemetry.originalPresentMs = sample.originalPresentCpuMs;
        g_present.telemetry.originalPresentMaxMs =
            std::max(g_present.telemetry.originalPresentMaxMs, sample.originalPresentCpuMs);
    }
    if (sample.identity.pacing.route != PresentPacing::Route::NativeTemporal &&
        sample.originalPresentCpuMs >= 33.3)
        ++g_present.telemetry.originalPresentSlowCalls;
    if (sample.identity.pacing.route != PresentPacing::Route::NativeTemporal &&
        FAILED(sample.result))
        LOG_WARN("DLSS-NR Present diagnostic: original Present returned {:X} after {:.3f} ms",
                  static_cast<unsigned int>(sample.result), sample.originalPresentCpuMs);

    g_present.pacing.recordCall(sample.identity.pacing,
        {sample.frameIntervalMs, sample.adapterCpuMs, sample.hookCpuMs,
         sample.originalPresentCpuMs, FAILED(sample.result)});
}

void ReportPresentUnavailable(PresentApi api, const char* reason)
{
    std::lock_guard<std::mutex> lock(g_present.mutex);
    const auto* config = Config::Instance();
    NR_FRAME_TRACE("nr-present-unavailable", "api={} reason={}", static_cast<unsigned int>(api),
        reason ? reason : "unknown");
    PresentGuides::Instance().BeginPresent(); // discard candidates even on unavailable Presents
    if (!config->GetDlssNrRuntimeSnapshot().enabled || config->DlssNrRoute.value_or_default() == 0)
        return;
    ++g_present.telemetry.presentAttempts;
    RefreshCompletionTelemetry();
    SetFallback(api, reason);
}

PresentCallIdentity EvaluatePresentImageOnly(IDXGISwapChain* swapChain, IUnknown* presentDevice,
                                             UINT presentFlags,
                                             const DXGI_PRESENT_PARAMETERS* presentParameters,
                                             PreFg::Frame* preFgFrame)
{
    std::lock_guard<std::mutex> lock(g_present.mutex);
    Capability::PresentObservationScope capabilityChain(g_present.telemetry);
    // NR-DIAG-001 BEGIN: values already supplied to the Present owner; no resource discovery.
    const auto m0Publisher = FrameTrace::WithM0Publisher([&]() noexcept {
        using SourceSnapshot = Neurotic::Diagnostics::M0::SourceSnapshot;
        using OwnerDomain = Neurotic::Contracts::OwnerDomain;
        auto source = SourceSnapshot::OwnerPublication(OwnerDomain::Presentation,
            "Alpha.Present.EvaluateImageOnly", "DlssNr_Present", 1, "EvaluatePresentImageOnly");
        source.Add("alpha.presentFlags", static_cast<std::uint64_t>(presentFlags));
        source.Add("alpha.presentParametersProvided", presentParameters != nullptr);
        source.Add("alpha.preFgFrameProvided", preFgFrame != nullptr);
        return source;
    });
    (void) m0Publisher;
    // NR-DIAG-001 END
    const auto tracePresent = FrameTrace::Event("nr-present-enter", "swapchain={:p} presentDevice={:p} flags={}",
        static_cast<void*>(swapChain), static_cast<void*>(presentDevice), presentFlags);
    FrameTrace::Context traceContext(FrameTrace::presentObservation, tracePresent);
    const auto* config = Config::Instance();
    const auto advisorEpoch = AdvisorSampling::ConfigurationGeneration.load();
    const auto capturedSettings = TryNrConfigSnapshot(*config);
    if (!capturedSettings)
    {
        if (preFgFrame && preFgFrame->readiness) preFgFrame->readiness->Reset();
        PresentGuides::Instance().BeginPresent();
        const char* reason = "NR settings snapshot unavailable";
        SetFallback(PresentApi::Unknown, reason);
        return {};
    }
    const auto& settings = *capturedSettings;
    capabilityChain.values.configRevision=settings.ObservationRevision();
    if (!g_present.readinessSettings || !settings.SamePresentReadinessConfiguration(*g_present.readinessSettings))
    {
        g_present.readinessSettings = settings;
        ++g_present.readinessConfiguration;
        if (preFgFrame && preFgFrame->readiness) preFgFrame->readiness->Reset();
    }
    if (preFgFrame && preFgFrame->readiness)
        preFgFrame->readiness->CheckEpoch(PreFg::State().readinessEpoch.load());
    const auto runtime = settings.GetDlssNrRuntimeSnapshot();
    const auto resolution = PresentResolution::Selected(settings);
    const auto inputPolicy = PresentInput::Selected(settings);
    const bool wantsGuides = inputPolicy == PresentInput::Policy::RequireGuides ||
        inputPolicy == PresentInput::Policy::AutoGuides;
    const bool observeNative = wantsGuides || resolution.mode == PresentResolution::FollowNative;
    const auto routeKey = PresentResolution::CaptureKey(settings);
    const bool preparedRoute = PreparedGuides::OwnsPresentOutput();
    PresentGuides::Instance().Enable(runtime.enabled && observeNative, routeKey);
    const auto guideSelection = PresentGuides::Instance().BeginPresent(preFgFrame ? preFgFrame->key : 0,
        preFgFrame ? preFgFrame->providerGeneration : PreFg::Provider().generation);
    NR_FRAME_TRACE("guide-selection",
        "epoch={} generation={} count={} producer={:p} identity={:p} backbuffer={} width={} height={} route={}",
        guideSelection.epoch, guideSelection.generation, guideSelection.count,
        static_cast<void*>(guideSelection.producer.get()), static_cast<void*>(guideSelection.swapchain.Get()),
        guideSelection.backbuffer, guideSelection.width, guideSelection.height,
        settings.DlssNrRoute.value_or_default());
    if (g_present.guideGeneration != guideSelection.generation)
    {
        InvalidateHistory("Present route changed");
        g_present.guideGeneration = guideSelection.generation;
    }
    const bool enabled = runtime.enabled;
    const unsigned int route = std::min(settings.DlssNrRoute.value_or_default(), 2u);
    const bool presentRequested = enabled && route != 0 && !preparedRoute;
    g_present.telemetry.requested = presentRequested;
    g_present.telemetry.active = false;
    g_present.telemetry.policyBlocked = false;
    g_present.telemetry.policyGuardrail.clear();
    g_present.telemetry.compatibilityPath.clear();
    g_present.telemetry.workWidth = g_present.telemetry.workHeight = 0;
    g_present.telemetry.requestedPlacement = preparedRoute ? "Prepared guides" : route != 0 ? "Present" : "Native Temporal";
    g_present.telemetry.requestedInputPolicy = inputPolicy;
    g_present.telemetry.actualInputClass = PresentInputDecision::InputClass::Refused;
    g_present.telemetry.signalFallbackReason.clear();
    g_present.telemetry.workloadFallback = false;
    g_present.telemetry.possibleTargetWrite = false;
    if (presentRequested)
        ++g_present.telemetry.presentAttempts;
    RefreshCompletionTelemetry();

    PresentCallIdentity identity {};
    identity.advisorConfigurationGeneration = advisorEpoch;
    identity.presentAttempt = presentRequested ? g_present.telemetry.presentAttempts : 0;
    const auto pacingRoute = !presentRequested ? PresentPacing::Route::NativeTemporal :
        wantsGuides ? PresentPacing::Route::PresentEnhanced : PresentPacing::Route::PresentImageOnly;
    EmitPacingSummary(g_present.pacing.beginCall(pacingRoute, ++g_present.timingCallSequence,
                                                 identity.pacing));
    g_present.pacing.observePending(identity.pacing, g_present.telemetry.pendingSlots);

    if (!presentRequested)
    {
        if (g_present.presentWasRequested)
            InvalidateHistory("Present route or enable state changed");
        g_present.presentWasRequested = false;
        g_present.telemetry.actualPlacement = preparedRoute ? "Prepared guides (addon owned)" : "Native Temporal";
        g_present.telemetry.fallbackReason.clear();
        g_present.telemetry.failure.clear();
        g_present.telemetry.failed = false;
        return identity;
    }

    if (!g_present.presentWasRequested)
        InvalidateHistory("Present route or enable state changed");
    else if (runtime.resumeGeneration != g_present.resumeGeneration ||
             FinalFallback::CurrentInputEpoch() != g_present.inputInterruptionEpoch)
        InvalidateHistory("NR resume generation changed");
    g_present.presentWasRequested = true;
    g_present.resumeGeneration = runtime.resumeGeneration;
    g_present.inputInterruptionEpoch = FinalFallback::CurrentInputEpoch();

    if (preFgFrame && !preFgFrame->valid)
    {
        SetFallback(PresentApi::D3D12, preFgFrame->refusal);
        return identity;
    }
    // Frame-token refusal above is local. All subsequent structural/model/submit
    // failures revoke readiness unless this call is explicitly waiting for proof.
    struct ReadinessAttempt
    {
        PreFg::Frame* frame;
        PresentCallIdentity& identity;
        bool waiting = false;
        ~ReadinessAttempt()
        {
            if (frame && frame->readiness && !waiting && !identity.completedOutput && !identity.probeSubmitted)
                frame->readiness->Reset();
        }
    } readinessAttempt {preFgFrame, identity};
    if (BasicMultipass::Active(settings) && BasicMultipass::Count(settings.DlssNrBasicMultipass.value_or_default()) == 0)
    {
        InvalidateHistory("Basic Multipass totals are zero");
        SetFallback(PresentApi::Unknown, "Basic Multipass effect bypassed; loaded resources retained");
        return identity;
    }

    const auto experimental = ExperimentalPolicy::Capture(settings);
    const bool frameGeneration = config->FGEnabled.value_or_default() ||
        State::Instance().dlssgLastSetMode != sl::DLSSGMode::eOff || State::Instance().fsrfgInputActive;
    // Combination changes invalidate history; guide/resource admission below still applies.
    const unsigned int experimentalFlags = wantsGuides ?
        (settings.DlssNrMultipassEnabled.value_or_default() ? 1u : 0u) |
        (frameGeneration ? 2u : 0u) |
        (Telemetry().nativeRayReconstructionActive ? 4u : 0u) : 0u;
    if (experimentalFlags != g_present.experimentalFlags)
    {
        InvalidateHistory("Present compatibility settings changed");
        g_present.experimentalFlags = experimentalFlags;
        LOG_INFO("DLSS-NR Present Enhanced: compatibility combination changed: Multipass={} FG={} RR={}. "
                 "Processing is allowed; fresh guide matching and resource checks still apply. "
                 "Runtime validation remains pending for future releases.",
                 (experimentalFlags & 1u) != 0, (experimentalFlags & 2u) != 0, (experimentalFlags & 4u) != 0);
    }

    if (swapChain == nullptr || presentDevice == nullptr)
    {
        SetFallback(PresentApi::Unknown, "swapchain or Present device unavailable");
        return identity;
    }
    if (g_present.completionUntrackable)
    {
        SetFallback(PresentApi::Unknown,
                    "private Present completion became untrackable; restart is required", true);
        return identity;
    }
    if ((presentFlags & (DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_SEQUENCE | DXGI_PRESENT_RESTART)) != 0)
    {
        SetFallback(PresentApi::Unknown, "Present flags are not safe for full-frame processing");
        return identity;
    }
    if (presentParameters != nullptr && (presentParameters->DirtyRectsCount != 0 ||
        presentParameters->pScrollRect != nullptr || presentParameters->pScrollOffset != nullptr))
    {
        SetFallback(PresentApi::Unknown, "Present1 dirty-rectangle or scroll update is unsupported");
        return identity;
    }

    ComPtr<IDXGISwapChain3> swapChain3;
    DXGI_SWAP_CHAIN_DESC1 swapDesc {};
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_CUSTOM;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(swapChain3.GetAddressOf()))) ||
        FAILED(swapChain3->GetDesc1(&swapDesc)))
    {
        SetFallback(PresentApi::Unknown, "IDXGISwapChain3 or swapchain description unavailable");
        return identity;
    }
    // DXGI exposes SetColorSpace1 but no getter. Resolve the same swapchain identity
    // as the observation producer. Missing observations stay unknown, including in telemetry.
    const auto hdrObservation = HdrObservation::Registry::Instance().Read(swapChain);
    capabilityChain.values.chainIdentity=hdrObservation.identityGeneration;
    capabilityChain.values.hdrIdentity=hdrObservation.identityGeneration;
    colorSpace = hdrObservation.colorSpace;
    g_present.telemetry.backbufferWidth = swapDesc.Width;
    g_present.telemetry.backbufferHeight = swapDesc.Height;
    g_present.telemetry.backbufferFormat = swapDesc.Format;
    g_present.telemetry.backbufferSampleCount = swapDesc.SampleDesc.Count;
    g_present.telemetry.swapEffect = swapDesc.SwapEffect;
    g_present.telemetry.colorSpace = colorSpace;
    g_present.telemetry.colorSpaceObserved = hdrObservation.colorSpaceObserved;
    g_present.telemetry.hdrDescriptorTransitioning = hdrObservation.transitioning;
    g_present.telemetry.hdrDescriptorRegistered = hdrObservation.registered;
    g_present.telemetry.hdrDescriptorFormat = hdrObservation.format;
    g_present.telemetry.hdrObservationSequence = hdrObservation.observationSequence;
    g_present.telemetry.hdrDescriptorGeneration = hdrObservation.generation;
    g_present.telemetry.hdrResizeGeneration = hdrObservation.resizeGeneration;
    g_present.telemetry.lastColorSpaceResult = hdrObservation.colorSpaceResult;
    g_present.telemetry.hdrMetadataType = hdrObservation.metadataType;
    g_present.telemetry.hdrMetadataSize = hdrObservation.metadataSize;
    g_present.telemetry.hdrMetadataHash = hdrObservation.metadataHash;
    g_present.telemetry.lastHdrMetadataResult = hdrObservation.metadataResult;
    if (hdrObservation.registered &&
        hdrObservation.observationSequence != g_present.lastHdrObservationSequence)
    {
        g_present.lastHdrObservationSequence = hdrObservation.observationSequence;
        LOG_INFO("DLSS-NR HDR diagnostic: Present swapchain {:p}, observation {}, generation {}, resize generation {}, "
                 "format {} (current {}), color space {}, class {}, source {}, transitioning {}, color result {:X}, "
                 "metadata type {}, size {}, result {:X}, bounded hash {:X}",
                 static_cast<void*>(swapChain), hdrObservation.observationSequence,
                 hdrObservation.generation, hdrObservation.resizeGeneration,
                 (UINT) hdrObservation.format, (UINT) swapDesc.Format, (UINT) colorSpace,
                 HdrObservation::ColorClassName(HdrObservation::Classify(colorSpace)),
                 hdrObservation.colorSpaceObserved ? "successful SetColorSpace1" : "DXGI format default",
                 hdrObservation.transitioning, (UINT) hdrObservation.colorSpaceResult,
                 (UINT) hdrObservation.metadataType, hdrObservation.metadataSize,
                 (UINT) hdrObservation.metadataResult, hdrObservation.metadataHash);
    }
    if (hdrObservation.transitioning)
    {
        SetFallback(PresentApi::Unknown, "swapchain HDR descriptor is transitioning after resize");
        return identity;
    }
    const bool flipModel = swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
                           swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    const bool blitModel = swapDesc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD ||
                           swapDesc.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL;
    if (swapDesc.SampleDesc.Count != 1 || (!flipModel && !blitModel))
    {
        SetFallback(PresentApi::Unknown, "target is not single-sample flip-model");
        return identity;
    }
    const auto colorDecision = PresentColor::Select(hdrObservation, swapDesc.Format);
    if (!colorDecision.Supported())
    {
        SetFallback(PresentApi::Unknown, colorDecision.reason);
        return identity;
    }

    // Blit swap chains expose buffer zero as the writable presentation target.
    // Their other buffers must never be selected for copyback.
    const UINT bufferIndex = flipModel ? swapChain3->GetCurrentBackBufferIndex() : 0;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> backbuffer12;
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D11Texture2D> backbuffer11;
    PresentApi api = PresentApi::Unknown;
    PresentCompatibility::Api compatibilityApi = PresentCompatibility::Api::D3D12;
    D3D12_RESOURCE_DESC backDesc {};

    if (SUCCEEDED(presentDevice->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()))))
    {
        api = PresentApi::D3D12;
        compatibilityApi = PresentCompatibility::Api::D3D12;
        if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(queue->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))) ||
            FAILED(device->GetDeviceRemovedReason()))
        {
            SetFallback(api, "a direct D3D12 queue is unavailable", true);
            if (DredDiagnostics::Enabled() && device)
                DredDiagnostics::Collect(device.Get(), device->GetDeviceRemovedReason());
            return identity;
        }
        if (FAILED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(backbuffer12.GetAddressOf()))))
        {
            SetFallback(api, "current D3D12 backbuffer is unavailable");
            return identity;
        }
        ComPtr<ID3D12Device> backbufferDevice;
        const HRESULT backbufferDeviceResult = backbuffer12->GetDevice(IID_PPV_ARGS(backbufferDevice.GetAddressOf()));
        const auto devices = NativeIdentity::CompareDevices(device.Get(), backbufferDevice.Get());
        NR_FRAME_TRACE("nr-device-identity", "queueDevice={:p} backbufferDevice={:p} nativeQueueDevice={:p} "
            "nativeBackbufferDevice={:p} queueLayers={} backbufferLayers={} queueResolve={} "
            "backbufferResolve={} backbufferGetDevice={} equal={}",
            static_cast<void*>(device.Get()), static_cast<void*>(backbufferDevice.Get()),
            static_cast<void*>(devices.left.object.Get()), static_cast<void*>(devices.right.object.Get()),
            devices.left.layers, devices.right.layers, static_cast<unsigned int>(devices.left.result),
            static_cast<unsigned int>(devices.right.result), static_cast<unsigned int>(backbufferDeviceResult), devices.equal);
        if (FAILED(backbufferDeviceResult) || !devices.equal)
        {
            SetFallback(api, "Present target and NR queue use different devices");
            return identity;
        }
        // Ownership equality does not authorize replacing ReShade's execution
        // interface. Retain the pre-existing Streamline-only rendering path.
        const auto executionDevice = NativeIdentity::Resolve<ID3D12Device>(device.Get());
        if (FAILED(executionDevice.result) || !executionDevice.object)
        {
            SetFallback(api, "Present rendering device could not be resolved");
            return identity;
        }
        device = executionDevice.object;
        if (devices.left.reshadeLayers || devices.right.reshadeLayers)
        {
            static unsigned int contractLogs = 0; // Present owner lock is held.
            if (contractLogs < 4)
            {
                ++contractLogs;
                LOG_INFO("NR Present device contract: execution={:p} owner={:p} ReShadeLayers={}; "
                         "retaining rendering interface for resource and command-list creation",
                         static_cast<void*>(device.Get()), static_cast<void*>(devices.left.object.Get()),
                         devices.left.reshadeLayers);
            }
        }
        backDesc = backbuffer12->GetDesc();
        NR_FRAME_TRACE("nr-backbuffer", "swapchain={:p} index={} resource={:p} device={:p} queue={:p} "
            "width={} height={} format={}", static_cast<void*>(swapChain), bufferIndex,
            static_cast<void*>(backbuffer12.Get()), static_cast<void*>(device.Get()), static_cast<void*>(queue.Get()),
            backDesc.Width, backDesc.Height, static_cast<unsigned int>(backDesc.Format));
    }
    else
    {
        ComPtr<ID3D11Device> baseDevice11;
        if (FAILED(presentDevice->QueryInterface(IID_PPV_ARGS(baseDevice11.GetAddressOf()))) ||
            FAILED(baseDevice11.As(&device11)))
        {
            SetFallback(PresentApi::Unknown, "Present graphics API is unsupported");
            return identity;
        }
        api = PresentApi::D3D11;
        compatibilityApi = PresentCompatibility::Api::D3D11;
        ComPtr<ID3D11DeviceContext> baseContext11;
        baseDevice11->GetImmediateContext(baseContext11.GetAddressOf());
        if (baseContext11 == nullptr || FAILED(baseContext11.As(&context11)) ||
            FAILED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(backbuffer11.GetAddressOf()))))
        {
            SetFallback(api, "D3D11 backbuffer or synchronization context is unavailable");
            return identity;
        }
        ComPtr<ID3D11Device> backbufferDevice11;
        backbuffer11->GetDevice(backbufferDevice11.GetAddressOf());
        if (!SameComObject(baseDevice11.Get(), backbufferDevice11.Get()))
        {
            SetFallback(api, "Present target and NR queue use different devices");
            return identity;
        }
        Dx11WithDx12::Init(device11.Get(), context11.Get());
        device = Dx11WithDx12::GetD3D12Device();
        queue = Dx11WithDx12::GetD3D12CommandQueue();
        if (device == nullptr || queue == nullptr ||
            queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(device->GetDeviceRemovedReason()))
        {
            SetFallback(api, "D3D11 shared-resource synchronization is unavailable", true);
            return identity;
        }
        D3D11_TEXTURE2D_DESC desc11 {};
        backbuffer11->GetDesc(&desc11);
        backDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        backDesc.Width = desc11.Width;
        backDesc.Height = desc11.Height;
        backDesc.DepthOrArraySize = static_cast<UINT16>(desc11.ArraySize);
        backDesc.MipLevels = static_cast<UINT16>(desc11.MipLevels);
        backDesc.Format = desc11.Format;
        backDesc.SampleDesc = desc11.SampleDesc;
    }
    g_present.telemetry.api = api;

    // Record the actual Present target before any compatibility guard. This is the evidence needed
    // to identify Wilds/GTA descriptors while every unsupported frame still falls back untouched.
    g_present.telemetry.backbufferWidth = static_cast<unsigned int>(backDesc.Width);
    g_present.telemetry.backbufferHeight = backDesc.Height;
    g_present.telemetry.backbufferFormat = backDesc.Format;
    g_present.telemetry.backbufferSampleCount = backDesc.SampleDesc.Count;
    bool rgba8Texture = false;
    bool rgba8ShaderLoad = false;
    bool rgba8TypedStore = false;
    bool targetTexture = false;
    bool targetShaderLoad = false;
    bool targetTypedStore = false;
    FormatCapabilities(device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
                       rgba8Texture, rgba8ShaderLoad, rgba8TypedStore);
    FormatCapabilities(device.Get(), backDesc.Format,
                       targetTexture, targetShaderLoad, targetTypedStore);
    bool fp16Texture = false, fp16Load = false, fp16Store = false;
    FormatCapabilities(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, fp16Texture, fp16Load, fp16Store);
    const PresentCompatibility::Capabilities capabilities {
        compatibilityApi,
        queue != nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT,
        backDesc.SampleDesc.Count == 1,
        swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
            swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        colorDecision.Supported() && backDesc.Format == swapDesc.Format,
        backDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        backDesc.Width != 0 && backDesc.Height != 0,
        true,
        backDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM,
        backDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM,
        rgba8ShaderLoad,
        rgba8TypedStore,
        targetTexture && targetShaderLoad,
        targetTexture && targetTypedStore,
        api == PresentApi::D3D12 || (device11 != nullptr && context11 != nullptr),
        api == PresentApi::D3D12 || (Dx11WithDx12::GetD3D12Device() == device.Get() &&
                                     Dx11WithDx12::GetD3D12CommandQueue() == queue.Get()),
        backDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT, colorDecision.hdr,
        fp16Texture && fp16Load, fp16Texture && fp16Store,
        backDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM,
        blitModel
    };
    const auto admission = PresentCompatibility::Admit(capabilities);
    if (!admission.supported)
    {
        SetFallback(api, admission.reason);
        return identity;
    }
    g_present.telemetry.compatibilityPath = std::string(api == PresentApi::D3D11 ? "D3D11 shared " : "D3D12 ") + colorDecision.reason;

    const unsigned int width = static_cast<unsigned int>(backDesc.Width);
    const unsigned int height = backDesc.Height;
    const auto swapchainIdentity = PresentGuides::Identity(swapChain3.Get());
    bool metadataQualified = false;
    std::string guideRefusal;
    if (observeNative)
    {
        auto swapchainIdentity = PresentGuides::Identity(swapChain3.Get());
        metadataQualified = PresentGuides::Instance().MatchMetadata(guideSelection,
                queue.Get(), swapchainIdentity.Get(), bufferIndex, width, height,
                preFgFrame ? preFgFrame->key : 0);
        if (!metadataQualified)
        {
            const auto status = PresentGuides::Instance().Inspect();
            guideRefusal = status.status;
        }
        else if (!preFgFrame && guideSelection.backbuffer != bufferIndex)
        {
            static UINT64 acceptedRotations = 0;
            const auto accepted = ++acceptedRotations;
            NR_FRAME_TRACE("guide-backbuffer-rotation",
                "captured={} present={} accepted={} association=unique-present-interval",
                guideSelection.backbuffer, bufferIndex, accepted);
            if (accepted <= 3 || accepted % 300 == 0)
                LOG_INFO("DLSS-NR Present: accepted rotated DXGI backbuffer index {} -> {} "
                         "using unique Present-interval association ({} so far)",
                         guideSelection.backbuffer, bufferIndex, accepted);
        }
        if (metadataQualified && (g_present.nativeWidth != guideSelection.frame.RenderSubrectWidth ||
            g_present.nativeHeight != guideSelection.frame.RenderSubrectHeight)
           )
            InvalidateHistory("Native render subrect changed");
        const auto& next = guideSelection.frame;
        const auto& old = g_present.nativeFrame;
        if (metadataQualified && wantsGuides && (next.DepthSubrectX != old.DepthSubrectX || next.DepthSubrectY != old.DepthSubrectY ||
            next.MotionSubrectX != old.MotionSubrectX || next.MotionSubrectY != old.MotionSubrectY ||
            next.DepthSubrectWidth != old.DepthSubrectWidth || next.DepthSubrectHeight != old.DepthSubrectHeight ||
            next.MotionSubrectWidth != old.MotionSubrectWidth || next.MotionSubrectHeight != old.MotionSubrectHeight ||
            next.DepthInverted != old.DepthInverted || next.MvScaleX != old.MvScaleX || next.MvScaleY != old.MvScaleY))
            InvalidateHistory("Native guide convention or subrect origin changed");
        if (metadataQualified && wantsGuides && next.Reset) InvalidateHistory("Native reset requested");
        if (metadataQualified)
        {
            g_present.nativeFrame = next;
            g_present.nativeWidth = next.RenderSubrectWidth;
            g_present.nativeHeight = next.RenderSubrectHeight;
        }
    }
    const auto decision = PresentInputDecision::Choose(inputPolicy, metadataQualified,
        metadataQualified && guideSelection.frame.RenderSubrectWidth != 0 &&
            guideSelection.frame.RenderSubrectHeight != 0, resolution);
    if (decision.input == PresentInputDecision::InputClass::Refused)
    {
        SetFallback(api, guideRefusal.empty() ? decision.reason : guideRefusal.c_str());
        return identity;
    }
    const bool guided = decision.input == PresentInputDecision::InputClass::Guided;
    identity.inputClass = decision.input;
    identity.workloadFallback = decision.workloadFallback;
    g_present.telemetry.actualInputClass = decision.input;
    g_present.telemetry.workloadFallback = decision.workloadFallback;
    g_present.telemetry.signalFallbackReason = !guideRefusal.empty() ? guideRefusal :
        decision.reason ? decision.reason : "";
    if (observeNative && lastObservedGuideClass != static_cast<int>(decision.input))
    {
        lastObservedGuideClass = static_cast<int>(decision.input);
        LOG_INFO("NR Present guide transition: input={} requestedFrame={} capturedFrame={} captures={} "
                 "slot={} captureError={} reason={}", guided ? "native" : "image-only",
                 preFgFrame ? preFgFrame->key : 0, guideSelection.providerFrame,
                 guideSelection.count, guideSelection.slot, guideSelection.captureError,
                 g_present.telemetry.signalFallbackReason);
    }
    if (PresentInputDecision::ChangesHistory(g_present.lastCommittedInputClass, decision.input))
        InvalidateHistory("Present input class changed");
    const bool sameResourceContext = g_present.frame && swapchainIdentity &&
        g_present.resourceSwapchainIdentity.Get() == swapchainIdentity.Get() &&
        NativeIdentity::CompareDevices(g_present.device.Get(), device.Get()).equal &&
        g_present.queue.Get() == queue.Get() && g_present.width == width && g_present.height == height &&
        g_present.format == backDesc.Format && g_present.colorSpace == colorSpace &&
        g_present.colorIdentity == hdrObservation.identityGeneration &&
        g_present.resourceRouteKey == routeKey &&
        g_present.resourceGuideGeneration == guideSelection.generation &&
        g_present.resourceResumeGeneration == runtime.resumeGeneration;
    const auto workload = PresentInputDecision::ResolveWorkload(decision, width, height,
        metadataQualified ? guideSelection.frame.RenderSubrectWidth : 0,
        metadataQualified ? guideSelection.frame.RenderSubrectHeight : 0,
        sameResourceContext ? PresentResolution::Size{g_present.workWidth, g_present.workHeight}
                            : PresentResolution::Size{});
    const auto size = workload.size;
    if (workload.retained)
    {
        g_present.telemetry.signalFallbackReason = "Native metadata unavailable; image-only at retained workload";
        if (!guideRefusal.empty()) g_present.telemetry.signalFallbackReason += ": " + guideRefusal;
        static unsigned int retainedLogs = 0; // Present owner lock is held.
        if (retainedLogs < 4)
        {
            ++retainedLogs;
            LOG_INFO("NR Present workload fallback: retained {}x{}; input=image-only; reason={}",
                     size.width, size.height, guideRefusal);
        }
    }
    const unsigned int workWidth = size.width, workHeight = size.height;
    g_present.telemetry.backbufferWidth = width;
    g_present.telemetry.backbufferHeight = height;
    g_present.telemetry.backbufferFormat = backDesc.Format;
    g_present.telemetry.resolution = resolution.mode;
    g_present.telemetry.workload = resolution.scale;
    g_present.telemetry.workWidth = workWidth;
    g_present.telemetry.workHeight = workHeight;
    if (workWidth == 0 || workHeight == 0)
    {
        SetFallback(api, size.reason);
        return identity;
    }

    const bool signatureChanged = g_present.device == nullptr || !SameDevice(g_present.device.Get(), device.Get()) ||
        g_present.queue.Get() != queue.Get() || g_present.width != width || g_present.height != height ||
        g_present.workWidth != workWidth || g_present.workHeight != workHeight ||
        g_present.format != backDesc.Format || g_present.colorSpace != colorSpace ||
        g_present.colorIdentity != hdrObservation.identityGeneration ||
        g_present.colorDecision.recipe != colorDecision.recipe || g_present.colorDecision.profile != colorDecision.profile ||
        g_present.resourceRouteKey != routeKey ||
        g_present.resourceSwapchainIdentity.Get() != swapchainIdentity.Get();
    if (signatureChanged)
    {
        if (preFgFrame && preFgFrame->readiness) preFgFrame->readiness->Reset();
        InvalidateHistory("Present target signature changed");
        if (!AllComplete())
        {
            SetFallback(api, "waiting for prior Present resources after a device/resize/workload change");
            return identity;
        }
        if (!BuildResources(device.Get(), queue.Get(), width, height, workWidth, workHeight,
                            backDesc.Format, colorSpace, colorDecision))
        {
            SetFallback(api, "private Present resources could not be created", true);
            return identity;
        }
        g_present.colorIdentity = hdrObservation.identityGeneration;
        LOG_INFO("NR Present color: profile={} recipe={} carrier={} reference={} scRGB units; scene exposure disabled",
            (unsigned) colorDecision.profile, colorDecision.recipe, (unsigned) colorDecision.workingFormat, colorDecision.whitePoint);
        g_present.resourceRouteKey = routeKey;
        ++g_present.resourceGeneration;
        g_present.telemetry.resourceGeneration = g_present.resourceGeneration;
    }

    // Stamp only an admitted allocation. A source reset/off-on/resize/configuration
    // change cannot inherit the reduced workload from a previous context.
    g_present.resourceSwapchainIdentity = swapchainIdentity;
    g_present.resourceGuideGeneration = guideSelection.generation;
    g_present.resourceResumeGeneration = runtime.resumeGeneration;

    const char* modelUnavailable = nullptr;
    if (!DirectD3D12Available(device.Get(), &modelUnavailable))
    {
        SetFallback(api, modelUnavailable && modelUnavailable[0] ? modelUnavailable : "NR model capability check failed", true);
        return identity;
    }

    if (preFgFrame && preFgFrame->readiness)
    {
        auto& key = preFgFrame->readinessIdentity;
        const auto nativeChain = NativeIdentity::Resolve<IDXGISwapChain>(swapChain);
        const auto nativeDevice = NativeIdentity::Resolve<ID3D12Device>(device.Get());
        key.swapchain = reinterpret_cast<uintptr_t>(nativeChain.object.Get());
        key.device = reinterpret_cast<uintptr_t>(nativeDevice.object.Get());
        key.queue = reinterpret_cast<uintptr_t>(queue.Get());
        key.provider = preFgFrame->providerGeneration;
        key.nativeGeneration = preFgFrame->nativeFgGeneration;
        key.nativeInstance = preFgFrame->nativeFgInstance;
        key.configuration = g_present.readinessConfiguration;
        key.resume = runtime.resumeGeneration;
        key.resources = g_present.resourceGeneration;
        const auto model = Telemetry();
        key.model = model.featureBuilds;
        key.modelLifecycle = model.lifecycleGeneration;
        key.additionalModels = model.layer2FeatureBuilds;
        key.additionalRetirements = model.layer2FeatureRetires;
        key.requestedPasses = Multipass::RequestedCount(settings);
        if (!model.lifecycleOpen || !model.modelLoaded) preFgFrame->readiness->Reset();
        key.invalidation = PreFg::State().readinessEpoch.load();
        key.route = route; key.width = width; key.height = height;
        key.format = backDesc.Format; key.samples = backDesc.SampleDesc.Count;
        key.quality = backDesc.SampleDesc.Quality;
        key.workWidth = workWidth; key.workHeight = workHeight; key.colorSpace = colorSpace;
        key.hdrIdentity = hdrObservation.identityGeneration;
        key.colorRecipe = colorDecision.recipe; key.colorProfile = (unsigned) colorDecision.profile;
        if (!key.swapchain || !key.device || device->GetDeviceRemovedReason() != S_OK)
        {
            SetFallback(api, "swapchain or Present device unavailable", true);
            return identity;
        }
        preFgFrame->allowOutput = preFgFrame->readiness->Poll(key, device.Get(), preFgFrame->sequence);
        if (preFgFrame->readiness->Pending())
        {
            readinessAttempt.waiting = true;
            NR_FRAME_TRACE("nr-readiness-wait", "token={} sequence={} reason={} epoch={}",
                preFgFrame->key, preFgFrame->sequence, preFgFrame->readiness->Reason(), key.invalidation);
            SetFallback(api, "Waiting for the selected route. Image unchanged.");
            return identity;
        }
        const auto nativeOutput = NativeIdentity::Resolve<ID3D12Resource>(backbuffer12.Get());
        if (nativeOutput.object.Get() != preFgFrame->outputResource ||
            !(preFgFrame->completionReservation = PreFg::ReserveCompletion(preFgFrame->outputResource, *preFgFrame)))
        {
            SetFallback(api, "Could not reserve the native FG completion handoff for this Present", true);
            return identity;
        }
    }

    ID3D12Resource* presentInput = backbuffer12.Get();
    ID3D12Resource* presentOutput = backbuffer12.Get();
    D3D12_RESOURCE_STATES presentRestingState = D3D12_RESOURCE_STATE_PRESENT;
    if (api == PresentApi::D3D11)
    {
        const UINT64 frameId = identity.presentAttempt;
        const bool inputReady = Dx11WithDx12::PrepareTextureFrom11To12(
            "Present input", device.Get(), backbuffer11.Get(), &g_present.dx11Input,
            true, false, false, frameId);
        const bool outputReady = Dx11WithDx12::PrepareTextureFrom11To12(
            "Present output", device.Get(), backbuffer11.Get(), &g_present.dx11Output,
            false, false, false, frameId);
        const bool resourcesArePrivate =
            g_present.dx11Input.SharedTexture != backbuffer11.Get() &&
            g_present.dx11Output.SharedTexture != backbuffer11.Get();
        if (!inputReady || !outputReady || !resourcesArePrivate ||
            g_present.dx11Input.Dx12Resource == nullptr ||
            g_present.dx11Output.Dx12Resource == nullptr)
        {
            SetFallback(api, "D3D11 compatible private shared images could not be created", true);
            return identity;
        }
        if (!Dx11WithDx12::SyncDx11ToDx12())
        {
            SetFallback(api, "D3D11-to-D3D12 input synchronization failed", true);
            return identity;
        }
        presentInput = g_present.dx11Input.Dx12Resource;
        presentOutput = g_present.dx11Output.Dx12Resource;
        presentRestingState = D3D12_RESOURCE_STATE_COMMON;
    }

    const unsigned int slotIndex = g_present.nextSlot++ % kSlotCount;
    PresentSlot& slot = g_present.slots[slotIndex];
    if (slot.completion != 0 && g_present.fence->GetCompletedValue() < slot.completion)
    {
        SetFallback(api, "private Present command slots are still in flight");
        return identity;
    }
    if (FAILED(slot.modelAllocator->Reset()) ||
        FAILED(g_present.list->Reset(slot.modelAllocator.Get(), nullptr)))
    {
        SetFallback(api, "private model command list could not be reset", true);
        return identity;
    }

    PresentGuides::Inputs nativeGuides;
    if (guided)
    {
        if (!PresentGuides::Instance().Bind(guideSelection,
            g_present.list.Get(), queue.Get(), swapchainIdentity.Get(), bufferIndex,
            static_cast<UINT>(backDesc.Width), backDesc.Height, nativeGuides,
            preFgFrame ? preFgFrame->key : 0))
        {
            g_present.list->Close();
            const auto guideStatus = PresentGuides::Instance().Inspect();
            SetFallback(api, guideStatus.status.c_str());
            return identity;
        }
    }

    slot.pacing = identity.pacing;
    slot.historyGeneration = g_present.historyGeneration;
    slot.presentAttempt = identity.presentAttempt;
    slot.firstSubmissionMs = 0.0;
    slot.completionObserved = true;
    slot.timingStarted = false;
    slot.timingResolved = false;
    slot.pacingExpected = false;
    if (g_present.pacingQueryHeap != nullptr && g_present.pacingReadback != nullptr &&
        g_present.timestampFrequency != 0)
    {
        g_present.list->EndQuery(g_present.pacingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                 slotIndex * 2);
        slot.timingStarted = true;
    }

    const bool uploadingGuides = g_present.guidesNeedUpload;
    if (uploadingGuides)
    {
        RecordUpload(g_present.list.Get(), device.Get(), g_present.depth.Get(), g_present.depthUpload.Get());
        RecordUpload(g_present.list.Get(), device.Get(), g_present.motion.Get(), g_present.motionUpload.Get());
    }
    Transition(g_present.list.Get(), presentInput, presentRestingState,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    bool inputPrepared = true;
    const bool converting = admission.path == PresentCompatibility::PixelPath::Rgb10Conversion ||
                            admission.path == PresentCompatibility::PixelPath::Bgra8Conversion;
    if (!converting)
    {
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        g_present.list->CopyResource(g_present.frame.Get(), presentInput);
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
    {
        g_present.list->CopyResource(g_present.conversionSource.Get(), presentInput);
        Transition(g_present.list.Get(), g_present.conversionSource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        inputPrepared = g_present.inputTransfer->Dispatch(g_present.list.Get(),
            g_present.conversionSource.Get(), g_present.frame.Get());
        UavBarrier(g_present.list.Get(), g_present.frame.Get());
        Transition(g_present.list.Get(), g_present.conversionSource.Get(),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    Transition(g_present.list.Get(), presentInput, D3D12_RESOURCE_STATE_COPY_SOURCE,
               presentRestingState);

    if (!inputPrepared)
    {
        g_present.list->Close();
        SetFallback(api, "Present input conversion could not be recorded", true);
        return identity;
    }

    const auto modelColor = colorDecision.Model();
    const bool modelSucceeded = EvaluateImageOnlyCommandList(g_present.list.Get(), queue.Get(),
        g_present.frame.Get(), guided ? nativeGuides.depth.Get() : g_present.depth.Get(),
        guided ? nativeGuides.motion.Get() : g_present.motion.Get(), workWidth, workHeight,
        g_present.history.ResetForNextEvaluation(), guided ? &nativeGuides.frame : nullptr, &settings, &modelColor,
        preFgFrame != nullptr);
    if (FAILED(g_present.list->Close()))
    {
        g_present.completionUntrackable = true;
        SetFallback(api, "private model command list could not close", true);
        return identity;
    }
    ID3D12CommandList* modelLists[] = { g_present.list.Get() };
    slot.firstSubmissionMs = Util::MillisecondsNow();
    queue->ExecuteCommandLists(1, modelLists);
    identity.modelSubmitted = true;
    const bool modelRecordingSealed = GpuSafety::SealOwnedRecording(g_present.list.Get());
    NR_FRAME_TRACE("nr-model-submitted",
        "attempt={} list={:p} queue={:p} output={:p} width={} height={} enhanced={} modelRecorded={}",
        identity.presentAttempt, static_cast<void*>(g_present.list.Get()), static_cast<void*>(queue.Get()),
        static_cast<void*>(presentOutput), workWidth, workHeight, guided, modelSucceeded);
    ++g_present.telemetry.modelSubmissions;
    if (uploadingGuides)
        g_present.guidesNeedUpload = false;

    if (!modelSucceeded || !modelRecordingSealed)
    {
        const UINT64 signal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), signal)))
        {
            slot.completion = signal;
            slot.completionObserved = false;
            RecordSubmission(signal);
        }
        else
            g_present.completionUntrackable = true;
        const char* reason = FailureReason();
        if (!modelRecordingSealed)
            SetFallback(api, "private model command-list recording could not be sealed", true);
        else
            SetFallback(api,
                        reason != nullptr && reason[0] != 0 ? reason : "model creation/evaluation not yet successful",
                        reason != nullptr && reason[0] != 0);
        return identity;
    }

    if (preFgFrame && preFgFrame->readiness)
    {
        // Model creation/replacement can occur inside evaluation. Such work is a
        // new probe, even if the old generation was ready at entry.
        const auto model = Telemetry();
        if (model.featureBuilds != preFgFrame->readinessIdentity.model ||
            model.lifecycleGeneration != preFgFrame->readinessIdentity.modelLifecycle ||
            model.layer2FeatureBuilds != preFgFrame->readinessIdentity.additionalModels ||
            model.layer2FeatureRetires != preFgFrame->readinessIdentity.additionalRetirements)
        {
            preFgFrame->readiness->Reset();
            preFgFrame->allowOutput = false;
            preFgFrame->readinessIdentity.model = model.featureBuilds;
            preFgFrame->readinessIdentity.modelLifecycle = model.lifecycleGeneration;
            preFgFrame->readinessIdentity.additionalModels = model.layer2FeatureBuilds;
            preFgFrame->readinessIdentity.additionalRetirements = model.layer2FeatureRetires;
        }
        const auto current = TryNrConfigSnapshot(*config);
        const auto currentHdr = HdrObservation::Registry::Instance().Read(swapChain);
        if (!current || !settings.SamePresentReadinessConfiguration(*current) || !model.lifecycleOpen || !model.modelLoaded ||
            currentHdr.transitioning || currentHdr.identityGeneration != preFgFrame->readinessIdentity.hdrIdentity ||
            preFgFrame->readinessIdentity.invalidation != PreFg::State().readinessEpoch.load() ||
            device->GetDeviceRemovedReason() != S_OK)
        {
            preFgFrame->readiness->Reset();
            preFgFrame->allowOutput = false;
            // Track the already submitted work, but do not certify this changed attempt.
            readinessAttempt.waiting = false;
            preFgFrame->readinessIdentity.invalidation = 0;
        }
    }
    if (preFgFrame && !preFgFrame->allowOutput)
    {
        const UINT64 signal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), signal)))
        {
            slot.completion = signal;
            slot.completionObserved = false;
            RecordSubmission(signal);
            identity.modelPrepared = true;
            identity.probeSubmitted = preFgFrame->readinessIdentity.invalidation != 0;
            identity.completionFence = g_present.fence.Get();
            identity.completionValue = signal;
            identity.outputResource = presentOutput;
        }
        else
            g_present.completionUntrackable = true;
        // No full-frame tag change, copyback, or output-history advancement.
        // Reuse stays gated by the normal completion fence and recording lifetime.
        NR_FRAME_TRACE("nr-startup-private-model", "token={} prepared={} fence={}",
            preFgFrame->key, identity.modelPrepared, signal);
        SetFallback(api, "Waiting for the selected route. Image unchanged.");
        return identity;
    }
    if (guided) PresentGuides::Instance().Evaluated();

    if (FAILED(slot.compositeAllocator->Reset()) ||
        FAILED(g_present.list->Reset(slot.compositeAllocator.Get(), nullptr)))
    {
        const UINT64 signal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), signal)))
        {
            slot.completion = signal;
            slot.completionObserved = false;
            RecordSubmission(signal);
        }
        else
            g_present.completionUntrackable = true;
        SetFallback(api, "copyback command list could not be reset", true);
        return identity;
    }
    PresentCopybackRecording copybackRecording(g_present.list.Get(), slot.compositeAllocator.Get(),
                                               g_present.completionUntrackable);
    if (!copybackRecording)
    {
        g_present.list->Close();
        g_present.completionUntrackable = true;
        SetFallback(api, "copyback recording ownership unavailable", true);
        return identity;
    }
    struct ScreenshotPublication
    {
        bool recorded = false;
        bool succeeded = false;
        bool sealed = true;
        ~ScreenshotPublication() { if (recorded) CompletePresentComparison(succeeded && sealed); }
    } screenshotPublication;
    const auto captureFinalPair = [&](ID3D12Resource* finalOutput, D3D12_RESOURCE_STATES finalState)
    {
        screenshotPublication.recorded = RecordPresentComparison(g_present.list.Get(), device.Get(),
            presentInput, presentRestingState, finalOutput, finalState, settings, identity.presentAttempt,
            preFgFrame ? preFgFrame->key : 0, preFgFrame ? preFgFrame->providerGeneration : 0,
            g_present.resourceGeneration, bufferIndex, hdrObservation.colorSpace);
    };
    bool outputPrepared = true;
    const bool applyOutput = Multipass::AppliesModel(settings);
    if (!applyOutput)
    {
        // Model work and fence tracking continue, but no conversion or copy touches the game image.
    }
    else if (!converting)
    {
        captureFinalPair(g_present.frame.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(g_present.list.Get(), presentOutput, presentRestingState,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        g_present.list->CopyResource(presentOutput, g_present.frame.Get());
        Transition(g_present.list.Get(), presentOutput, D3D12_RESOURCE_STATE_COPY_DEST,
                   presentRestingState);
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
    {
        const auto conversionState = admission.path == PresentCompatibility::PixelPath::Bgra8Conversion
            ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (colorDecision.hdr)
            Transition(g_present.list.Get(), g_present.conversionSource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        outputPrepared = g_present.outputTransfer->Dispatch(g_present.list.Get(),
            g_present.frame.Get(), g_present.conversionOutput.Get());
        if (conversionState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            UavBarrier(g_present.list.Get(), g_present.conversionOutput.Get());
        if (colorDecision.hdr)
            Transition(g_present.list.Get(), g_present.conversionSource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST);
        if (outputPrepared)
            captureFinalPair(g_present.conversionOutput.Get(), conversionState);
        Transition(g_present.list.Get(), g_present.frame.Get(),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(g_present.list.Get(), g_present.conversionOutput.Get(),
                   conversionState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(g_present.list.Get(), presentOutput, presentRestingState,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        g_present.list->CopyResource(presentOutput, g_present.conversionOutput.Get());
        Transition(g_present.list.Get(), presentOutput, D3D12_RESOURCE_STATE_COPY_DEST,
                   presentRestingState);
        Transition(g_present.list.Get(), g_present.conversionOutput.Get(),
                   D3D12_RESOURCE_STATE_COPY_SOURCE, conversionState);
    }
    if (!outputPrepared)
    {
        g_present.list->Close();
        const UINT64 phaseOneSignal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), phaseOneSignal)))
        {
            slot.completion = phaseOneSignal;
            slot.completionObserved = false;
            RecordSubmission(phaseOneSignal);
        }
        else
            g_present.completionUntrackable = true;
        SetFallback(api, "Present output conversion could not be recorded", true);
        return identity;
    }
    if (slot.timingStarted && !screenshotPublication.recorded)
    {
        g_present.list->EndQuery(g_present.pacingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                 slotIndex * 2 + 1);
        g_present.list->ResolveQueryData(g_present.pacingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                         slotIndex * 2, 2, g_present.pacingReadback.Get(),
                                         static_cast<UINT64>(slotIndex) * 2ull * sizeof(UINT64));
        slot.timingResolved = true;
    }
    if (FAILED(g_present.list->Close()))
    {
        const UINT64 phaseOneSignal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), phaseOneSignal)))
        {
            slot.completion = phaseOneSignal;
            slot.completionObserved = false;
            RecordSubmission(phaseOneSignal);
        }
        g_present.completionUntrackable = true;
        SetFallback(api, "copyback command list could not close", true);
        return identity;
    }
    // Do not alter the game's FG tags until copyback recording and Close succeed.
    // Earlier failures must leave both its image and its original HUD policy intact.
    if (preFgFrame && preFgFrame->prepareInputs && !preFgFrame->prepareInputs(*preFgFrame))
    {
        const UINT64 modelSignal = g_present.nextFence++;
        if (SUCCEEDED(queue->Signal(g_present.fence.Get(), modelSignal)))
        {
            slot.completion = modelSignal;
            slot.completionObserved = false;
            RecordSubmission(modelSignal);
        }
        else g_present.completionUntrackable = true;
        SetFallback(api, "Could not establish full-frame FG input policy for this token", true);
        return identity;
    }
    ID3D12CommandList* compositeLists[] = { g_present.list.Get() };
    queue->ExecuteCommandLists(1, compositeLists);
    screenshotPublication.sealed = copybackRecording.Submitted(queue.Get());
    identity.copybackSubmitted = true;
    NR_FRAME_TRACE("nr-copyback-submitted", "attempt={} list={:p} queue={:p} output={:p} "
        "generation={} claimGeneration={} claimInstance={} providerGeneration={}",
        identity.presentAttempt, static_cast<void*>(g_present.list.Get()), static_cast<void*>(queue.Get()),
        static_cast<void*>(presentOutput), FgLifecycle::Read().generation,
        preFgFrame ? preFgFrame->diagnosticClaim.generation : 0, preFgFrame ? preFgFrame->diagnosticClaim.instance : 0,
        preFgFrame ? preFgFrame->providerGeneration : 0);
    ++g_present.telemetry.compositeSubmissions;
    const UINT64 signal = g_present.nextFence++;
    if (FAILED(queue->Signal(g_present.fence.Get(), signal)))
    {
        g_present.completionUntrackable = true;
        SetFallback(api, "copyback completion signal failed", true, nullptr, true);
        return identity;
    }
    slot.completion = signal;
    slot.completionObserved = false;
    slot.pacingExpected = g_present.pacing.expectGpu(identity.pacing);
    RecordSubmission(signal);

    if (!screenshotPublication.sealed)
    {
        SetFallback(api, "copyback recording completion or ownership unavailable", true, nullptr, true);
        return identity;
    }

    if (api == PresentApi::D3D11 && applyOutput)
    {
        if (!Dx11WithDx12::SyncDx12ToDx11())
        {
            SetFallback(api, "D3D12-to-D3D11 output synchronization failed", true);
            return identity;
        }
        if (!g_present.dx11OutputCompletion.Copy(context11.Get(), backbuffer11.Get(),
                                                 g_present.dx11Output.SharedTexture))
        {
            const bool written = g_present.dx11OutputCompletion.Quarantined();
            g_present.completionUntrackable = g_present.completionUntrackable || written;
            SetFallback(api, written ? "final D3D11 copy completion signal failed; restart required" :
                        "final D3D11 copy proof or source ownership unavailable", true, nullptr, written);
            return identity;
        }
    }

    ++g_present.telemetry.modelEvaluations;
    ++g_present.telemetry.compositeEvaluations;
    identity.completedOutput = true;
    g_present.lastCommittedInputClass = decision.input;
    screenshotPublication.succeeded = true;
    identity.modelPrepared = true;
    identity.completionFence = g_present.fence.Get();
    identity.completionValue = signal;
    identity.outputResource = presentOutput;
    g_present.telemetry.active = true;
    g_present.telemetry.failed = false;
    if (g_present.telemetry.consecutiveFallbacks != 0)
        LOG_INFO("DLSS-NR Present diagnostic: processing recovered on attempt {} after {} consecutive fallback(s)",
                 g_present.telemetry.presentAttempts, g_present.telemetry.consecutiveFallbacks);
    g_present.telemetry.consecutiveFallbacks = 0;
    g_present.telemetry.actualPlacement = guided ? "Present guided (game HUD included)" :
        "Present image-only";
    g_present.telemetry.fallbackReason.clear();
    g_present.telemetry.failure.clear();
    return identity;
}
} // namespace DlssNr



