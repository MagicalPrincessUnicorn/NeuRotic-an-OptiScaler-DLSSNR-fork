#include "pch.h"
#include "DlssNr_Present.h"
#include "DlssNr_PresentCompatibility.h"
#include "DlssNr_PresentHistory.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNr_PresentGuides.h"

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
};

struct PresentState
{
    std::mutex mutex;
    UINT64 guideGeneration = 0;
    UINT64 resourceRouteKey = 0;
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
    UINT64 timestampFrequency = 0;
    UINT64 timingCallSequence = 0;
    PresentPacing::Window<> pacing;
    PresentHistory::Continuity history;
    bool presentWasRequested = false;
    UINT64 resumeGeneration = 0;
    bool guidesNeedUpload = true;
    // Set when submitted work can no longer be paired with a trustworthy completion value, or when
    // an unsubmitted command list has already been registered with the shared GPU-safety tracker.
    // Retrying or releasing resources in either case would turn an untouched-frame fallback into a
    // use-after-submit risk, so only process teardown may reclaim this generation.
    bool completionUntrackable = false;
};

PresentState g_present;

void SyncHistoryTelemetry()
{
    g_present.telemetry.historyResetPending = g_present.history.ResetForNextEvaluation();
    g_present.telemetry.uninterruptedFrames = g_present.history.CompletedFrames();
    g_present.telemetry.historyResetReason = g_present.history.ResetReason();
    g_present.telemetry.historyInvalidationReason = g_present.history.LastInvalidationReason();
}

void InvalidateHistory(const char* reason)
{
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
}

void RefreshCompletionTelemetry()
{
    if (g_present.fence == nullptr)
    {
        g_present.telemetry.pendingSlots = 0;
        return;
    }

    const UINT64 completed = g_present.fence->GetCompletedValue();
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

void SetFallback(PresentApi api, const char* reason, bool failed = false)
{
    const bool changedReason = g_present.telemetry.fallbackReason != (reason ? reason : "unknown fallback");
    // A fallback leaves the game image alone, so its successor must never inherit the last model
    // result.  This covers admission, bridge, conversion, queue, copy-back, and model failures.
    InvalidateHistory(reason);
    g_present.telemetry.requested = true;
    g_present.telemetry.active = false;
    g_present.telemetry.api = api;
    g_present.telemetry.requestedPlacement = Config::Instance()->DlssNrRoute.value_or_default() == 2
        ? "Present Enhanced" : "Present Image-Only";
    g_present.telemetry.actualPlacement = "Original Present fallback";
    g_present.telemetry.fallbackReason = reason != nullptr ? reason : "unknown fallback";
    g_present.telemetry.failure = failed ? g_present.telemetry.fallbackReason : "";
    g_present.telemetry.failed = failed;
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
    if (g_present.fence == nullptr) return true;
    const UINT64 done = g_present.fence->GetCompletedValue();
    if (done == UINT64_MAX) return false;
    for (const auto& slot : g_present.slots)
        if (slot.completion != 0 && done < slot.completion) return false;
    return true;
}

void ReleaseResources()
{
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
}

bool CreateTexture(ID3D12Device* device, DXGI_FORMAT format, unsigned int width, unsigned int height,
                   D3D12_RESOURCE_STATES initialState, ComPtr<ID3D12Resource>& output)
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
                    DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace)
{
    ReleaseResources();
    g_present.device = device;
    g_present.queue = queue;
    g_present.width = width;
    g_present.height = height;
    g_present.workWidth = workWidth;
    g_present.workHeight = workHeight;
    g_present.format = format;
    g_present.colorSpace = colorSpace;

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(g_present.fence.GetAddressOf()))) ||
        !CreateTexture(device, DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
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

    if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
    {
        if (!CreateTexture(device, format, width, height, D3D12_RESOURCE_STATE_COPY_DEST,
                           g_present.conversionSource) ||
            !CreateTexture(device, format, width, height, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           g_present.conversionOutput))
        {
            ReleaseResources();
            return false;
        }
        g_present.inputTransfer = std::make_unique<FT_Dx12>("Present R10 to RGBA8", device,
                                                            DXGI_FORMAT_R8G8B8A8_UNORM);
        g_present.outputTransfer = std::make_unique<FT_Dx12>("Present RGBA8 to R10", device, format);
        // These texture pairs remain fixed until the existing generation-drain gate permits
        // recreation. Never rewrite descriptors referenced by an in-flight conversion.
        if (!g_present.inputTransfer->Ready() || !g_present.outputTransfer->Ready() ||
            !g_present.inputTransfer->BindImmutableDescriptors(g_present.conversionSource.Get(), g_present.frame.Get()) ||
            !g_present.outputTransfer->BindImmutableDescriptors(g_present.frame.Get(), g_present.conversionOutput.Get()))
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
    std::lock_guard<std::mutex> lock(g_present.mutex);
    return g_present.telemetry;
}

void ReportPresentCallTiming(const PresentCallTimingSample& sample)
{
    std::lock_guard<std::mutex> lock(g_present.mutex);
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
    PresentGuides::Instance().BeginPresent(); // discard candidates even on unavailable Presents
    if (!config->GetDlssNrRuntimeSnapshot().enabled || config->DlssNrRoute.value_or_default() == 0)
        return;
    ++g_present.telemetry.presentAttempts;
    RefreshCompletionTelemetry();
    SetFallback(api, reason);
}

PresentCallIdentity EvaluatePresentImageOnly(IDXGISwapChain* swapChain, IUnknown* presentDevice,
                                             UINT presentFlags,
                                             const DXGI_PRESENT_PARAMETERS* presentParameters)
{
    std::lock_guard<std::mutex> lock(g_present.mutex);
    const auto* config = Config::Instance();
    const auto runtime = config->GetDlssNrRuntimeSnapshot();
    const auto capturedSettings = TryNrConfigSnapshot(*config);
    if (!capturedSettings)
    {
        PresentGuides::Instance().BeginPresent();
        const char* reason = "NR settings snapshot unavailable";
        SetFallback(PresentApi::Unknown, reason);
        return {};
    }
    const auto& settings = *capturedSettings;
    const auto resolution = PresentResolution::Selected(settings);
    const bool enhanced = settings.DlssNrRoute.value_or_default() == 2;
    const bool observeNative = enhanced || (settings.DlssNrRoute.value_or_default() == 1 &&
        resolution.mode == PresentResolution::FollowNative);
    const auto routeKey = PresentResolution::CaptureKey(settings);
    PresentGuides::Instance().Enable(runtime.enabled && observeNative, routeKey);
    const auto guideSelection = PresentGuides::Instance().BeginPresent();
    if (g_present.guideGeneration != guideSelection.generation)
    {
        InvalidateHistory("Present route changed");
        g_present.guideGeneration = guideSelection.generation;
    }
    const bool enabled = runtime.enabled;
    const unsigned int route = std::min(settings.DlssNrRoute.value_or_default(), 2u);
    const bool presentRequested = enabled && route != 0;
    g_present.telemetry.requested = presentRequested;
    g_present.telemetry.active = false;
    g_present.telemetry.compatibilityPath.clear();
    g_present.telemetry.workWidth = g_present.telemetry.workHeight = 0;
    g_present.telemetry.requestedPlacement = route == 2 ? "Present Enhanced" :
        route == 1 ? "Present Image-Only" : "Native Temporal";
    if (presentRequested)
        ++g_present.telemetry.presentAttempts;
    RefreshCompletionTelemetry();

    PresentCallIdentity identity {};
    identity.presentAttempt = presentRequested ? g_present.telemetry.presentAttempts : 0;
    const auto pacingRoute = !presentRequested ? PresentPacing::Route::NativeTemporal :
        enhanced ? PresentPacing::Route::PresentEnhanced : PresentPacing::Route::PresentImageOnly;
    EmitPacingSummary(g_present.pacing.beginCall(pacingRoute, ++g_present.timingCallSequence,
                                                 identity.pacing));
    g_present.pacing.observePending(identity.pacing, g_present.telemetry.pendingSlots);

    if (!presentRequested)
    {
        if (g_present.presentWasRequested)
            InvalidateHistory("Present route or enable state changed");
        g_present.presentWasRequested = false;
        g_present.telemetry.actualPlacement = "Native Temporal";
        g_present.telemetry.fallbackReason.clear();
        g_present.telemetry.failure.clear();
        g_present.telemetry.failed = false;
        return identity;
    }

    if (!g_present.presentWasRequested)
        InvalidateHistory("Present route or enable state changed");
    else if (runtime.resumeGeneration != g_present.resumeGeneration)
        InvalidateHistory("NR resume generation changed");
    g_present.presentWasRequested = true;
    g_present.resumeGeneration = runtime.resumeGeneration;

    if (enhanced && (config->DlssNrMultipassEnabled.value_or_default() || config->FGEnabled.value_or_default() ||
        State::Instance().dlssgLastSetMode != sl::DLSSGMode::eOff || State::Instance().fsrfgInputActive))
    {
        SetFallback(PresentApi::Unknown, "Present Enhanced requires NR Multipass off and frame generation off");
        return identity;
    }
    if (enhanced && Telemetry().nativeRayReconstructionActive)
    {
        SetFallback(PresentApi::Unknown, "Present Enhanced does not support Ray Reconstruction");
        return identity;
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
    // DXGI exposes SetColorSpace1 but no getter. The wrapper records every color-space change in this
    // process; combine that state with the strict 8-bit format gate below and fail closed for HDR.
    colorSpace = State::Instance().isHdrActive ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                                : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    g_present.telemetry.backbufferWidth = swapDesc.Width;
    g_present.telemetry.backbufferHeight = swapDesc.Height;
    g_present.telemetry.backbufferFormat = swapDesc.Format;
    g_present.telemetry.backbufferSampleCount = swapDesc.SampleDesc.Count;
    g_present.telemetry.swapEffect = swapDesc.SwapEffect;
    g_present.telemetry.colorSpace = colorSpace;
    if (swapDesc.SampleDesc.Count != 1 ||
        (swapDesc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD &&
         swapDesc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL))
    {
        SetFallback(PresentApi::Unknown, "target is not single-sample flip-model");
        return identity;
    }
    if (colorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
    {
        SetFallback(PresentApi::Unknown, "HDR or non-SDR color space is unsupported");
        return identity;
    }

    const UINT bufferIndex = swapChain3->GetCurrentBackBufferIndex();
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
            return identity;
        }
        if (FAILED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(backbuffer12.GetAddressOf()))))
        {
            SetFallback(api, "current D3D12 backbuffer is unavailable");
            return identity;
        }
        ComPtr<ID3D12Device> backbufferDevice;
        if (FAILED(backbuffer12->GetDevice(IID_PPV_ARGS(backbufferDevice.GetAddressOf()))) ||
            !SameComObject(device.Get(), backbufferDevice.Get()))
        {
            SetFallback(api, "Present target and NR queue use different devices");
            return identity;
        }
        backDesc = backbuffer12->GetDesc();
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
    const PresentCompatibility::Capabilities capabilities {
        compatibilityApi,
        queue != nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT,
        backDesc.SampleDesc.Count == 1,
        swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
            swapDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
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
                                     Dx11WithDx12::GetD3D12CommandQueue() == queue.Get())
    };
    const auto admission = PresentCompatibility::Admit(capabilities);
    if (!admission.supported)
    {
        SetFallback(api, admission.reason);
        return identity;
    }
    g_present.telemetry.compatibilityPath = api == PresentApi::D3D11
        ? (admission.path == PresentCompatibility::PixelPath::Rgb10Conversion
            ? "D3D11 shared R10 SDR conversion" : "D3D11 shared RGBA8 direct")
        : (admission.path == PresentCompatibility::PixelPath::Rgb10Conversion
            ? "D3D12 R10 SDR conversion" : "D3D12 RGBA8 direct");

    const unsigned int width = static_cast<unsigned int>(backDesc.Width);
    const unsigned int height = backDesc.Height;
    if (observeNative)
    {
        auto swapchainIdentity = PresentGuides::Identity(swapChain3.Get());
        if (api != PresentApi::D3D12 || !PresentGuides::Instance().MatchMetadata(guideSelection,
                queue.Get(), swapchainIdentity.Get(), bufferIndex, width, height))
        {
            const auto status = PresentGuides::Instance().Inspect();
            SetFallback(api, api != PresentApi::D3D12 ? "Captured Native metadata requires DX12" : status.status.c_str());
            return identity;
        }
        if (g_present.nativeWidth != guideSelection.frame.RenderSubrectWidth ||
            g_present.nativeHeight != guideSelection.frame.RenderSubrectHeight)
            InvalidateHistory("Native render subrect changed");
        const auto& next = guideSelection.frame;
        const auto& old = g_present.nativeFrame;
        if (enhanced && (next.DepthSubrectX != old.DepthSubrectX || next.DepthSubrectY != old.DepthSubrectY ||
            next.MotionSubrectX != old.MotionSubrectX || next.MotionSubrectY != old.MotionSubrectY ||
            next.DepthInverted != old.DepthInverted || next.MvScaleX != old.MvScaleX || next.MvScaleY != old.MvScaleY))
            InvalidateHistory("Native guide convention or subrect origin changed");
        if (enhanced && next.Reset) InvalidateHistory("Native reset requested");
        g_present.nativeFrame = next;
        g_present.nativeWidth = guideSelection.frame.RenderSubrectWidth;
        g_present.nativeHeight = guideSelection.frame.RenderSubrectHeight;
    }
    const auto size = PresentResolution::Resolve(resolution, width, height,
        observeNative ? guideSelection.frame.RenderSubrectWidth : 0,
        observeNative ? guideSelection.frame.RenderSubrectHeight : 0);
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
        g_present.resourceRouteKey != routeKey;
    if (signatureChanged)
    {
        InvalidateHistory("Present target signature changed");
        if (!AllComplete())
        {
            SetFallback(api, "waiting for prior Present resources after a device/resize/workload change");
            return identity;
        }
        if (!BuildResources(device.Get(), queue.Get(), width, height, workWidth, workHeight,
                            backDesc.Format, colorSpace))
        {
            SetFallback(api, "private Present resources could not be created", true);
            return identity;
        }
        g_present.resourceRouteKey = routeKey;
    }

    if (!DirectD3D12Available(device.Get()))
    {
        SetFallback(api, "direct Feature 18 entry-point/capability probe failed", true);
        return identity;
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
    if (enhanced)
    {
        auto swapchainIdentity = PresentGuides::Identity(swapChain3.Get());
        if (api != PresentApi::D3D12 || !PresentGuides::Instance().Bind(guideSelection,
            g_present.list.Get(), queue.Get(), swapchainIdentity.Get(), bufferIndex,
            static_cast<UINT>(backDesc.Width), backDesc.Height, nativeGuides))
        {
            g_present.list->Close();
            const auto guideStatus = PresentGuides::Instance().Inspect();
            SetFallback(api, api != PresentApi::D3D12 ? "Captured Native metadata requires DX12" :
                guideStatus.status.c_str());
            return identity;
        }
    }

    slot.pacing = identity.pacing;
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
    if (admission.path == PresentCompatibility::PixelPath::Rgba8Direct)
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
        SetFallback(api, "10-bit input conversion could not be recorded", true);
        return identity;
    }

    const bool modelSucceeded = EvaluateImageOnlyCommandList(g_present.list.Get(), queue.Get(),
        g_present.frame.Get(), enhanced ? nativeGuides.depth.Get() : g_present.depth.Get(),
        enhanced ? nativeGuides.motion.Get() : g_present.motion.Get(), workWidth, workHeight,
        g_present.history.ResetForNextEvaluation(), enhanced ? &nativeGuides.frame : nullptr);
    if (FAILED(g_present.list->Close()))
    {
        g_present.completionUntrackable = true;
        SetFallback(api, "private model command list could not close", true);
        return identity;
    }
    ID3D12CommandList* modelLists[] = { g_present.list.Get() };
    slot.firstSubmissionMs = Util::MillisecondsNow();
    queue->ExecuteCommandLists(1, modelLists);
    ++g_present.telemetry.modelSubmissions;
    if (modelSucceeded && enhanced) PresentGuides::Instance().Evaluated();
    if (uploadingGuides)
        g_present.guidesNeedUpload = false;

    if (!modelSucceeded)
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
        SetFallback(api,
                    reason != nullptr && reason[0] != 0 ? reason : "model creation/evaluation not yet successful",
                    reason != nullptr && reason[0] != 0);
        return identity;
    }

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
    bool outputPrepared = true;
    if (admission.path == PresentCompatibility::PixelPath::Rgba8Direct)
    {
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
        Transition(g_present.list.Get(), g_present.frame.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        outputPrepared = g_present.outputTransfer->Dispatch(g_present.list.Get(),
            g_present.frame.Get(), g_present.conversionOutput.Get());
        UavBarrier(g_present.list.Get(), g_present.conversionOutput.Get());
        Transition(g_present.list.Get(), g_present.frame.Get(),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(g_present.list.Get(), g_present.conversionOutput.Get(),
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(g_present.list.Get(), presentOutput, presentRestingState,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        g_present.list->CopyResource(presentOutput, g_present.conversionOutput.Get());
        Transition(g_present.list.Get(), presentOutput, D3D12_RESOURCE_STATE_COPY_DEST,
                   presentRestingState);
        Transition(g_present.list.Get(), g_present.conversionOutput.Get(),
                   D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
        SetFallback(api, "10-bit output conversion could not be recorded", true);
        return identity;
    }
    if (slot.timingStarted)
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
    ID3D12CommandList* compositeLists[] = { g_present.list.Get() };
    queue->ExecuteCommandLists(1, compositeLists);
    ++g_present.telemetry.compositeSubmissions;
    const UINT64 signal = g_present.nextFence++;
    if (FAILED(queue->Signal(g_present.fence.Get(), signal)))
    {
        g_present.completionUntrackable = true;
        SetFallback(api, "copyback completion signal failed", true);
        return identity;
    }
    slot.completion = signal;
    slot.completionObserved = false;
    slot.pacingExpected = g_present.pacing.expectGpu(identity.pacing);
    RecordSubmission(signal);

    if (api == PresentApi::D3D11)
    {
        if (!Dx11WithDx12::SyncDx12ToDx11())
        {
            SetFallback(api, "D3D12-to-D3D11 output synchronization failed", true);
            return identity;
        }
        context11->CopyResource(backbuffer11.Get(), g_present.dx11Output.SharedTexture);
        context11->Flush();
    }

    ++g_present.telemetry.modelEvaluations;
    ++g_present.telemetry.compositeEvaluations;
    identity.completedOutput = true;
    g_present.telemetry.active = true;
    g_present.telemetry.failed = false;
    if (g_present.telemetry.consecutiveFallbacks != 0)
        LOG_INFO("DLSS-NR Present diagnostic: processing recovered on attempt {} after {} consecutive fallback(s)",
                 g_present.telemetry.presentAttempts, g_present.telemetry.consecutiveFallbacks);
    g_present.telemetry.consecutiveFallbacks = 0;
    g_present.telemetry.actualPlacement = enhanced ?
        "Present Enhanced (game HUD included)" : "Present Image-Only";
    g_present.telemetry.fallbackReason.clear();
    g_present.telemetry.failure.clear();
    return identity;
}
} // namespace DlssNr
