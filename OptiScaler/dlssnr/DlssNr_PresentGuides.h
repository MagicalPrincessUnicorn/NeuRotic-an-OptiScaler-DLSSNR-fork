#pragma once

#include "NrGpuSafety.h"
#include "DlssNr_PresentResolution.h"
#include <shaders/dlssnr/DlssNr_Common.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>

namespace DlssNr::PresentGuides
{
using Microsoft::WRL::ComPtr;
// Private data is forwarded by OptiScaler's swapchain wrapper, so both Native (wrapper)
// and Present (real object) see the SAME identity without retaining a backbuffer/resize blocker.
inline ComPtr<IUnknown> Identity(IDXGISwapChain* swapchain)
{
    class Cookie final : public IUnknown
    {
        std::atomic<ULONG> refs {1};
      public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
        {
            if (!out) return E_POINTER;
            *out = nullptr;
            if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
            *out = this; AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override
        { const ULONG left = --refs; if (!left) delete this; return left; }
    };
    constexpr GUID key = {0x4d5b97ac, 0x5cfa, 0x44df, {0xb4,0xf3,0x31,0x1a,0x86,0xd3,0x9e,0xf0}};
    static std::mutex identityMutex;
    std::lock_guard lock(identityMutex);
    ComPtr<IUnknown> identity;
    if (!swapchain) return identity;
    UINT size = sizeof(IUnknown*);
    if (SUCCEEDED(swapchain->GetPrivateData(key, &size, identity.GetAddressOf()))) return identity;
    identity.Attach(new Cookie);
    if (FAILED(swapchain->SetPrivateDataInterface(key, identity.Get()))) identity.Reset();
    return identity;
}
struct Snapshot
{
    bool enabled = false;
    unsigned long long generation = 0, captures = 0, matched = 0, evaluated = 0, rejected = 0;
    unsigned long long captureAttempts = 0;
    std::string captureError;
    std::string inputDescription;
    std::string status = "Control: constant depth / zero motion";
};
// Native DX11 produces shared carriers before the private DX12 capture submission.
// Keep the actual GPU fence/value and the queue which accepted its Wait, in addition
// to the ordinary command-list ticket. A scalar frame counter is not producer proof.
struct Dx11Producer
{
    ComPtr<ID3D12Fence> ready;
    ComPtr<ID3D12CommandQueue> orderedQueue;
    ComPtr<IUnknown> sourceDevice;
    UINT64 value = 0, feature = 0, evaluation = 0;
    bool Valid() const
    {
        return ready && orderedQueue && sourceDevice && value && feature && evaluation &&
            ready->GetCompletedValue() != UINT64_MAX;
    }
};
struct Selection
{
    bool enabled = false;
    unsigned long long epoch = 0, generation = 0;
    unsigned int count = 0;
    int slot = -1;
    std::string captureError;
    DlssNrFrameInfo frame;
    ComPtr<IUnknown> swapchain;
    GpuSafety::Ticket producer;
    std::shared_ptr<const Dx11Producer> dx11Producer;
    UINT backbuffer = 0, width = 0, height = 0;
};
struct Inputs
{
    ComPtr<ID3D12Resource> depth, motion;
    DlssNrFrameInfo frame;
};

// An intentionally bounded test bridge. Original resources are never passed to Present.
// Both writer and reader recordings retain each private copy until completion AND sealing.
class Bridge
{
    struct Slot
    {
        ComPtr<ID3D12Resource> depth, motion, originalDepth, originalMotion;
        ComPtr<IUnknown> swapchain;
        GpuSafety::Ticket producer, consumer;
        std::shared_ptr<const Dx11Producer> dx11Producer;
        DlssNrFrameInfo frame;
        UINT64 epoch = 0, generation = 0, bytes = 0;
        UINT width = 0, height = 0, backbuffer = 0;
    };
    std::mutex mutex;
    std::array<Slot, 8> slots;
    Snapshot telemetry;
    UINT64 epoch = 1;
    unsigned int count = 0;
    int candidate = -1;
    std::string epochError;
    Selection metadata;
    UINT64 configurationKey = 0;

    void Reject(const std::string& reason) { telemetry.status = reason; ++telemetry.rejected; }
    void RejectCapture(const std::string& reason)
    {
        epochError = reason;
        telemetry.captureError = reason;
        Reject(reason);
    }
    static std::string DescribeInput(const char* name, ID3D12Resource* resource)
    {
        if (!resource) return std::string(name) + " missing";
        const auto d = resource->GetDesc();
        return std::string(name) + " " + std::to_string(d.Width) + "x" + std::to_string(d.Height) +
            " format=" + std::to_string(d.Format) + " mips=" + std::to_string(d.MipLevels) +
            " layers=" + std::to_string(d.DepthOrArraySize) + " samples=" + std::to_string(d.SampleDesc.Count);
    }
    static DXGI_FORMAT Typed(DXGI_FORMAT format, bool motion)
    {
        if (motion)
        {
            switch (format)
            {
            case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
            case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_UNORM:
            case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: return format;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }
        switch (format)
        {
        case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: return format;
        default: return DXGI_FORMAT_UNKNOWN; // Other depth/stencil families remain unverified.
        }
    }
    static bool Describe(ID3D12Resource* source, bool motion, D3D12_RESOURCE_DESC& desc)
    {
        if (!source) return false;
        desc = source->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
            desc.DepthOrArraySize != 1 || desc.MipLevels == 0 || !desc.Width || !desc.Height ||
            desc.Width > 8192 || desc.Height > 8192) return false;
        desc.Format = Typed(desc.Format, motion);
        desc.MipLevels = 1; // NR reads mip zero, not the game's complete depth pyramid.
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Alignment = 0;
        return desc.Format != DXGI_FORMAT_UNKNOWN;
    }
    static void Copy(ID3D12GraphicsCommandList* list, ID3D12Resource* source,
                     ID3D12Resource* destination, D3D12_RESOURCE_STATES state)
    {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        // Subresource zero is mip zero, array zero, DEPTH plane zero for D32S8.
        // The typed R32_FLOAT_X8X24 view reads that plane only. Do not copy or
        // transition the game's stencil plane (which can have an independent state).
        barrier.Transition = {destination, 0,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST};
        list->ResourceBarrier(1, &barrier);
        barrier.Transition = {source, 0,
                              state, D3D12_RESOURCE_STATE_COPY_SOURCE};
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from {}, to {};
        from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource = destination; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        if (state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
        }
        barrier.Transition = {destination, 0,
                              D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        list->ResourceBarrier(1, &barrier);
    }
  public:
    Snapshot Inspect() { std::lock_guard lock(mutex); return telemetry; }
    void Invalidate()
    {
        std::lock_guard lock(mutex);
        ++telemetry.generation; ++epoch; count = 0; candidate = -1;
        metadata = {}; epochError.clear();
        telemetry.status = "Native source lifecycle changed; waiting for fresh guides";
    }
    void RejectNative(const std::string& reason)
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled) return;
        ++telemetry.captureAttempts; ++count; candidate = -1;
        RejectCapture(reason);
    }
    void Enable(bool enabled, UINT64 key = 0)
    {
        std::lock_guard lock(mutex);
        if (telemetry.enabled == enabled && configurationKey == key) return;
        configurationKey = key;
        telemetry.enabled = enabled;
        ++telemetry.generation;
        ++epoch; count = 0; candidate = -1;
        metadata = {};
        epochError.clear(); telemetry.captureError.clear(); telemetry.inputDescription.clear();
        telemetry.status = enabled ? "Waiting for Native guides" : "Control: constant depth / zero motion";
    }
    Selection BeginPresent()
    {
        std::lock_guard lock(mutex);
        Selection selection = metadata;
        selection.enabled = telemetry.enabled; selection.epoch = epoch++;
        selection.generation = telemetry.generation; selection.count = count;
        selection.slot = candidate; selection.captureError = epochError;
        count = 0; candidate = -1;
        metadata = {};
        epochError.clear();
        return selection;
    }
    void Capture(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion,
                 const DlssNrFrameInfo& frame, IUnknown* swapchain, UINT backbuffer,
                 UINT width, UINT height, D3D12_RESOURCE_STATES depthState,
                 D3D12_RESOURCE_STATES motionState, bool copyGuides = true,
                 const char* metadataError = nullptr,
                 std::shared_ptr<const Dx11Producer> dx11Producer = {})
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled) return;
        ++telemetry.captureAttempts;
        ++count; candidate = -1;
        const auto depthWidth = frame.DepthSubrectWidth ? frame.DepthSubrectWidth : frame.RenderSubrectWidth;
        const auto depthHeight = frame.DepthSubrectHeight ? frame.DepthSubrectHeight : frame.RenderSubrectHeight;
        const auto motionWidth = frame.MotionSubrectWidth ? frame.MotionSubrectWidth : frame.RenderSubrectWidth;
        const auto motionHeight = frame.MotionSubrectHeight ? frame.MotionSubrectHeight : frame.RenderSubrectHeight;
        telemetry.inputDescription = DescribeInput("Depth", depth) + " | " + DescribeInput("Motion", motion) +
            " | depthRect=" + std::to_string(depthWidth) + "x" + std::to_string(depthHeight) +
            " motionRect=" + std::to_string(motionWidth) + "x" + std::to_string(motionHeight);
        if (count != 1) { RejectCapture("Multiple Native evaluations before Present; no guide pair used"); return; }
        D3D12_RESOURCE_DESC dd {}, md {};
        if (!list) { RejectCapture("Native capture: command list missing"); return; }
        if (!swapchain) { RejectCapture("Native capture: current swapchain/identity unavailable"); return; }
        if (!width || !height) { RejectCapture("Native capture: output missing or empty"); return; }
        if (metadataError) { RejectCapture(metadataError); return; }
        if (dx11Producer && !dx11Producer->Valid())
        { RejectCapture("DX11 producer fence/order proof unavailable"); return; }
        if (!frame.RenderSubrectWidth || !frame.RenderSubrectHeight || !depthWidth || !depthHeight ||
            !motionWidth || !motionHeight ||
            frame.RenderSubrectWidth > width || frame.RenderSubrectHeight > height)
        { RejectCapture("Native capture: missing or invalid render-subrect dimensions"); return; }
        metadata.frame = frame; metadata.frame.ExposureTexture = nullptr;
        metadata.swapchain = swapchain; metadata.backbuffer = backbuffer;
        metadata.width = width; metadata.height = height;
        metadata.producer = GpuSafety::Record(list);
        metadata.dx11Producer = dx11Producer;
        if (!metadata.producer) { RejectCapture("Native metadata producer tracking unavailable"); return; }
        if (!copyGuides) return;
        if (!Describe(depth, false, dd)) { RejectCapture("Native capture: unsupported depth: " + telemetry.inputDescription); return; }
        if (!Describe(motion, true, md)) { RejectCapture("Native capture: unsupported motion: " + telemetry.inputDescription); return; }
        if (!std::isfinite(frame.MvScaleX) || !std::isfinite(frame.MvScaleY) ||
            !std::isfinite(frame.JitterX) || !std::isfinite(frame.JitterY))
        { RejectCapture("Native capture: non-finite motion scale/jitter"); return; }
        if (frame.DepthSubrectX > dd.Width || frame.DepthSubrectY > dd.Height ||
            frame.MotionSubrectX > md.Width || frame.MotionSubrectY > md.Height ||
            depthWidth > dd.Width - frame.DepthSubrectX ||
            depthHeight > dd.Height - frame.DepthSubrectY ||
            motionWidth > md.Width - frame.MotionSubrectX ||
            motionHeight > md.Height - frame.MotionSubrectY)
        { RejectCapture("Native capture: active subrect exceeds guide dimensions: " + telemetry.inputDescription); return; }
        ComPtr<ID3D12Device> device, depthDevice, motionDevice;
        if (FAILED(list->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(depth->GetDevice(IID_PPV_ARGS(&depthDevice))) ||
            FAILED(motion->GetDevice(IID_PPV_ARGS(&motionDevice))) ||
            device != depthDevice || device != motionDevice)
        { RejectCapture("Native guide device mismatch"); return; }
        int freeSlot = -1;
        UINT64 resident = 0;
        for (unsigned int i = 0; i < slots.size(); ++i)
        {
            auto& slot = slots[i];
            if (slot.epoch != epoch && GpuSafety::Reusable(slot.producer) && GpuSafety::Reusable(slot.consumer))
            {
                if (freeSlot < 0) freeSlot = static_cast<int>(i);
                // Keep reusable allocations of this shape/device, not the original game inputs.
                ComPtr<ID3D12Device> oldDevice;
                if (slot.depth) slot.depth->GetDevice(IID_PPV_ARGS(&oldDevice));
                auto matches = [](ID3D12Resource* resource, const D3D12_RESOURCE_DESC& desc)
                {
                    if (!resource) return false;
                    const auto old = resource->GetDesc();
                    return old.Width == desc.Width && old.Height == desc.Height && old.Format == desc.Format;
                };
                if (oldDevice != device || !matches(slot.depth.Get(), dd) || !matches(slot.motion.Get(), md))
                    slot = {};
                else
                {
                    slot.producer.reset(); slot.consumer.reset();
                    slot.originalDepth.Reset(); slot.originalMotion.Reset(); slot.swapchain.Reset();
                }
            }
            resident += slot.bytes;
        }
        const auto depthBytes = device->GetResourceAllocationInfo(0, 1, &dd).SizeInBytes;
        const auto motionBytes = device->GetResourceAllocationInfo(0, 1, &md).SizeInBytes;
        constexpr UINT64 budget = 512ull * 1024 * 1024;
        if (freeSlot < 0 || depthBytes > budget || motionBytes > budget ||
            depthBytes + motionBytes > budget)
        { RejectCapture("Native guide copy slots/budget busy; no stale reuse"); return; }
        const auto bytes = depthBytes + motionBytes;
        const bool reuse = slots[freeSlot].depth != nullptr;
        if (!reuse && resident > budget - bytes)
        { RejectCapture("Native guide copy slots/budget busy; no stale reuse"); return; }
        Slot next;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (reuse)
        {
            next.depth = slots[freeSlot].depth;
            next.motion = slots[freeSlot].motion;
        }
        else if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &dd,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&next.depth))) ||
            FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &md,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&next.motion))))
        { RejectCapture("Native guide copy allocation failed"); return; }
        next.producer = GpuSafety::Record(list);
        next.dx11Producer = std::move(dx11Producer);
        if (!next.producer) { RejectCapture("Native guide producer tracking unavailable"); return; }
        next.originalDepth = depth; next.originalMotion = motion;
        next.swapchain = swapchain; next.backbuffer = backbuffer;
        next.width = width; next.height = height;
        next.epoch = epoch; next.generation = telemetry.generation; next.bytes = bytes;
        next.frame = frame; next.frame.ExposureTexture = nullptr;
        Copy(list, depth, next.depth.Get(), depthState);
        Copy(list, motion, next.motion.Get(), motionState);
        slots[freeSlot] = std::move(next);
        candidate = freeSlot; ++telemetry.captures;
        telemetry.captureError.clear();
        telemetry.status = "Captured Native guides; awaiting matching Present submission";
    }
    bool Bind(const Selection& selection, ID3D12GraphicsCommandList* list,
              ID3D12CommandQueue* queue, IUnknown* swapchain, UINT backbuffer,
              UINT width, UINT height, Inputs& inputs)
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled || !selection.enabled || selection.generation != telemetry.generation)
        { Reject("Native guide selection belongs to an inactive/stale test generation"); return false; }
        if (!selection.captureError.empty())
        { Reject(selection.captureError); return false; } // retain the producer failure, including its descriptors
        if (selection.count == 0)
        { Reject("Native capture callback not observed in this Present interval"); return false; }
        if (selection.count != 1 || selection.slot < 0 || selection.slot >= static_cast<int>(slots.size()))
        { Reject("No unique completed Native capture for this Present interval"); return false; }
        auto& slot = slots[selection.slot];
        if (slot.epoch != selection.epoch || slot.generation != selection.generation ||
            slot.swapchain.Get() != swapchain || slot.backbuffer != backbuffer ||
            slot.width != width || slot.height != height || slot.consumer)
        { Reject("Native guide frame/swapchain/size mismatch"); return false; }
        if (!GpuSafety::OrderedOn(slot.producer, queue))
        { Reject("Native copy not uniquely submitted on Present queue"); return false; }
        if (slot.dx11Producer && (!slot.dx11Producer->Valid() || slot.dx11Producer->orderedQueue.Get() != queue))
        { Reject("DX11 guide producer fence/queue mismatch"); return false; }
        slot.consumer = GpuSafety::Record(list);
        if (!slot.consumer) { Reject("Present guide consumer tracking unavailable"); return false; }
        inputs = {slot.depth, slot.motion, slot.frame};
        ++telemetry.matched;
        telemetry.status = "Matched Native guides bound; model success not yet confirmed";
        return true;
    }
    bool MatchMetadata(const Selection& selection, ID3D12CommandQueue* queue,
                       IUnknown* swapchain, UINT backbuffer, UINT width, UINT height)
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled || !selection.enabled || selection.generation != telemetry.generation)
        { Reject("Native metadata belongs to an inactive/stale route or resolution"); return false; }
        if (!selection.captureError.empty()) { Reject(selection.captureError); return false; }
        if (selection.count != 1 || !selection.producer)
        { Reject("No fresh unique Native render metadata in this Present interval"); return false; }
        if (selection.swapchain.Get() != swapchain || selection.backbuffer != backbuffer ||
            selection.width != width || selection.height != height)
        { Reject("Native metadata frame/swapchain/output-size mismatch"); return false; }
        if (!GpuSafety::OrderedOn(selection.producer, queue))
        { Reject("Native metadata not uniquely submitted on Present queue"); return false; }
        if (selection.dx11Producer && (!selection.dx11Producer->Valid() ||
            selection.dx11Producer->orderedQueue.Get() != queue))
        { Reject("DX11 metadata producer fence/queue mismatch"); return false; }
        return true;
    }
    void Evaluated()
    {
        std::lock_guard lock(mutex);
        ++telemetry.evaluated;
        telemetry.status = "Model evaluated with matched Native guides (game HUD included)";
    }
};
// Hooks can outlive an NGX session; pending resources must not be freed at DLL static teardown.
inline Bridge& Instance() { static auto* bridge = new Bridge; return *bridge; }
}
