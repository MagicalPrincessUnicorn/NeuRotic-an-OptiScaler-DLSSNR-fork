#pragma once
// NR-FEED-001 BEGIN
#include <inputs/universal_feeder/providers/PresentGuideObservationAdapter.h>
// NR-FEED-001 END

#include "NrGpuSafety.h"
#include "NativeIdentity.h"
#include <nr/lifecycle/NativeLeaseBinding.h>
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
    unsigned long long providerFrame = 0;
    unsigned int count = 0;
    int slot = -1;
    std::string captureError;
    DlssNrFrameInfo frame;
    ComPtr<IUnknown> swapchain;
    GpuSafety::Ticket producer;
    std::shared_ptr<const unsigned char> reservation;
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
        std::weak_ptr<const unsigned char> selectionReservation;
        std::shared_ptr<Neurotic::Lifecycle::NativeLeaseRegistration> depthLease, motionLease;
        std::shared_ptr<const Dx11Producer> dx11Producer;
        DlssNrFrameInfo frame;
        UINT64 epoch = 0, generation = 0, bytes = 0;
        UINT64 providerFrame = 0;
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
    struct PendingFrame
    {
        UINT64 token = 0, epoch = 0;
        unsigned int count = 0;
        int candidate = -1;
        std::string error;
        Selection metadata;
    };
    // Provider frames can be recorded ahead of Present. Retaining their metadata
    // grants no GPU access: MatchMetadata/Bind still require the exact token and order.
    std::array<PendingFrame, 16> pendingFrames;
    UINT64 nextEpoch = 2;
    UINT64 activeProviderGeneration = 0, lastPresentedProviderFrame = 0;
    bool havePresentedProviderFrame = false;

    bool SyncProviderGeneration(UINT64 generation)
    {
        if (!generation || generation == activeProviderGeneration) return true;
        if (generation < activeProviderGeneration) return false;
        CloseLeaseAdmissions();
        activeProviderGeneration = generation;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        lastPresentedProviderFrame = 0;
        ++telemetry.generation; epoch = nextEpoch++;
        count = 0; candidate = -1; metadata = {}; epochError.clear();
        return true;
    }
    bool AlreadyPresented(UINT64 token) const
    {
        return token && havePresentedProviderFrame &&
            (token == lastPresentedProviderFrame || EarlierToken(token, lastPresentedProviderFrame));
    }

    PendingFrame* Pending(UINT64 token)
    {
        for (auto& frame : pendingFrames) if (frame.token == token) return &frame;
        for (auto& frame : pendingFrames) if (!frame.token)
        { frame.token = token; frame.epoch = nextEpoch++; return &frame; }
        return nullptr;
    }
    bool PendingSlot(unsigned int index) const
    {
        for (const auto& frame : pendingFrames)
            if (frame.token && frame.candidate == static_cast<int>(index)) return true;
        return false;
    }
    static bool EarlierToken(UINT64 candidate, UINT64 selected)
    {
        const auto distance = static_cast<uint32_t>(selected - 1) - static_cast<uint32_t>(candidate - 1);
        return distance != 0 && distance < (uint32_t{1} << 31);
    }

    void CloseLeaseAdmissions()
    {
        for(auto& slot:slots)
        {
            if(slot.depthLease)slot.depthLease->Close();
            if(slot.motionLease)slot.motionLease->Close();
            slot.selectionReservation.reset();
        }
    }

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
        CloseLeaseAdmissions();
        ++telemetry.generation; epoch = nextEpoch++; count = 0; candidate = -1;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        metadata = {}; epochError.clear();
        telemetry.status = "Native source lifecycle changed; waiting for fresh guides";
    }
    void RejectNative(const std::string& reason, UINT64 providerFrame = 0, UINT64 providerGeneration = 0)
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled) return;
        if (!SyncProviderGeneration(providerGeneration) || AlreadyPresented(providerFrame)) return;
        ++telemetry.captureAttempts;
        if (providerFrame)
        {
            if (auto* frame = Pending(providerFrame))
            { ++frame->count; frame->candidate = -1; frame->error = reason; }
            telemetry.captureError = reason; Reject(reason);
        }
        else
        { ++count; candidate = -1; RejectCapture(reason); }
    }
    void Enable(bool enabled, UINT64 key = 0)
    {
        std::lock_guard lock(mutex);
        if (telemetry.enabled == enabled && configurationKey == key) return;
        CloseLeaseAdmissions(); // administrative closure only; tickets/holds continue to own retirement
        configurationKey = key;
        telemetry.enabled = enabled;
        ++telemetry.generation;
        epoch = nextEpoch++; count = 0; candidate = -1;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        metadata = {};
        epochError.clear(); telemetry.captureError.clear(); telemetry.inputDescription.clear();
        telemetry.status = enabled ? "Waiting for Native guides" : "Control: constant depth / zero motion";
    }
    Selection BeginPresent(UINT64 providerFrame = 0, UINT64 providerGeneration = 0)
    {
        std::lock_guard lock(mutex);
        if (!SyncProviderGeneration(providerGeneration) || AlreadyPresented(providerFrame))
        {
            Selection refused;
            refused.enabled = telemetry.enabled; refused.generation = telemetry.generation;
            refused.captureError = "Native provider frame is stale or already presented";
            return refused;
        }
        Selection selection = metadata;
        selection.enabled = telemetry.enabled; selection.epoch = epoch;
        selection.generation = telemetry.generation; selection.count = count;
        selection.slot = candidate; selection.captureError = epochError;
        count = 0; candidate = -1;
        metadata = {};
        epochError.clear();
        epoch = nextEpoch++;
        if (providerFrame)
        {
            lastPresentedProviderFrame = providerFrame; havePresentedProviderFrame = true;
            selection = {};
            selection.enabled = telemetry.enabled;
            selection.generation = telemetry.generation;
            for (auto& frame : pendingFrames)
            {
                if (frame.token == providerFrame)
                {
                    selection = frame.metadata;
                    selection.enabled = telemetry.enabled;
                    selection.generation = telemetry.generation;
                    selection.epoch = frame.epoch;
                    selection.count = frame.count;
                    selection.slot = frame.candidate;
                    selection.captureError = frame.error;
                    frame = {};
                }
                else if (frame.token && EarlierToken(frame.token, providerFrame)) frame = {};
            }
        }
        // Hold a selected copy between selection and Bind even when its producer
        // has already retired. A simultaneous Native callback must not recycle it.
        if (selection.slot >= 0 && selection.slot < static_cast<int>(slots.size()))
        {
            try
            {
                selection.reservation = std::make_shared<const unsigned char>();
                slots[selection.slot].selectionReservation = selection.reservation;
            }
            catch (...)
            { selection.slot = -1; selection.captureError = "Native guide selection reservation unavailable"; }
        }
        return selection;
    }
    void Capture(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion,
                 const DlssNrFrameInfo& frame, IUnknown* swapchain, UINT backbuffer,
                 UINT width, UINT height, D3D12_RESOURCE_STATES depthState,
                 D3D12_RESOURCE_STATES motionState, bool copyGuides = true,
                 const char* metadataError = nullptr, UINT64 providerFrame = 0,
                 std::shared_ptr<const Dx11Producer> dx11Producer = {}, UINT64 providerGeneration = 0)
    {
        std::lock_guard lock(mutex);
        // NR-FEED-001 BEGIN
        Neurotic::Feed::Callback feedGuides({"PresentGuides", Neurotic::Contracts::GraphicsApi::D3D12,
            "capture", telemetry.generation, Neurotic::Contracts::SourceClass::HostObserved}, this);
        Neurotic::Feed::ObservePresentGuide(feedGuides, frame, depth, motion);
        feedGuides.Value("bridge.epoch", epoch);feedGuides.Value("provider.frame", providerFrame);
        // NR-FEED-001 END
        if (!telemetry.enabled) return;
        if (!SyncProviderGeneration(providerGeneration) || AlreadyPresented(providerFrame))
        { telemetry.captureError = "Native capture belongs to a stale provider frame"; Reject(telemetry.captureError); return; }
        auto* pending = providerFrame ? Pending(providerFrame) : nullptr;
        if (providerFrame && !pending)
        { telemetry.captureError = "Native provider frame metadata capacity busy"; Reject(telemetry.captureError); return; }
        auto& captureCount = pending ? pending->count : this->count;
        auto& captureCandidate = pending ? pending->candidate : this->candidate;
        auto& captureError = pending ? pending->error : this->epochError;
        auto& captureMetadata = pending ? pending->metadata : this->metadata;
        const auto captureEpoch = pending ? pending->epoch : this->epoch;
        auto RejectCapture = [&](const std::string& reason)
        { captureError = reason; telemetry.captureError = reason; Reject(reason); };
        ++telemetry.captureAttempts;
        ++captureCount; captureCandidate = -1;
        const auto depthWidth = frame.DepthSubrectWidth ? frame.DepthSubrectWidth : frame.RenderSubrectWidth;
        const auto depthHeight = frame.DepthSubrectHeight ? frame.DepthSubrectHeight : frame.RenderSubrectHeight;
        const auto motionWidth = frame.MotionSubrectWidth ? frame.MotionSubrectWidth : frame.RenderSubrectWidth;
        const auto motionHeight = frame.MotionSubrectHeight ? frame.MotionSubrectHeight : frame.RenderSubrectHeight;
        telemetry.inputDescription = DescribeInput("Depth", depth) + " | " + DescribeInput("Motion", motion) +
            " | depthRect=" + std::to_string(depthWidth) + "x" + std::to_string(depthHeight) +
            " motionRect=" + std::to_string(motionWidth) + "x" + std::to_string(motionHeight);
        if (captureCount != 1) { RejectCapture("Multiple Native evaluations before Present; no guide pair used"); return; }
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
        captureMetadata.frame = frame; captureMetadata.frame.ExposureTexture = nullptr;
        captureMetadata.providerFrame = providerFrame;
        captureMetadata.swapchain = swapchain; captureMetadata.backbuffer = backbuffer;
        captureMetadata.width = width; captureMetadata.height = height;
        captureMetadata.producer = GpuSafety::Record(list);
        captureMetadata.dx11Producer = dx11Producer;
        if (!captureMetadata.producer) { RejectCapture("Native metadata producer tracking unavailable"); return; }
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
            !NativeIdentity::CompareDevices(device.Get(), depthDevice.Get()).equal ||
            !NativeIdentity::CompareDevices(device.Get(), motionDevice.Get()).equal)
        { RejectCapture("Native guide device mismatch"); return; }
        int freeSlot = -1;
        UINT64 resident = 0;
        for (unsigned int i = 0; i < slots.size(); ++i)
        {
            auto& slot = slots[i];
            if (slot.epoch != captureEpoch && !PendingSlot(i) && slot.selectionReservation.expired() &&
                GpuSafety::Reusable(slot.producer) && GpuSafety::Reusable(slot.consumer) &&
                (!slot.depthLease || slot.depthLease->CanRecycle(slot.depth.Get(),slot.producer,slot.consumer)) &&
                (!slot.motionLease || slot.motionLease->CanRecycle(slot.motion.Get(),slot.producer,slot.consumer)))
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
                    slot.depthLease.reset(); slot.motionLease.reset();
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
        next.epoch = captureEpoch; next.generation = telemetry.generation; next.bytes = bytes;
        next.providerFrame = providerFrame;
        next.frame = frame; next.frame.ExposureTexture = nullptr;
        Copy(list, depth, next.depth.Get(), depthState);
        Copy(list, motion, next.motion.Get(), motionState);
        slots[freeSlot] = std::move(next);
        // NR-FEED-001 BEGIN
        {
            Neurotic::Feed::TranslationScope feedCopy(feedGuides, "PresentGuides.private-copy");
            Neurotic::Feed::Callback copyObservation({"PresentGuides", Neurotic::Contracts::GraphicsApi::D3D12,
                "prepared-copy", telemetry.generation, Neurotic::Contracts::SourceClass::Derived}, this);
            Neurotic::Feed::ObservePresentGuide(copyObservation, frame, slots[freeSlot].depth.Get(), slots[freeSlot].motion.Get());
        }
        // NR-FEED-001 END
        captureCandidate = freeSlot; ++telemetry.captures;
        telemetry.captureError.clear();
        telemetry.status = "Captured Native guides; awaiting matching Present submission";
    }
    bool Bind(const Selection& selection, ID3D12GraphicsCommandList* list,
              ID3D12CommandQueue* queue, IUnknown* swapchain, UINT backbuffer,
              UINT width, UINT height, Inputs& inputs, UINT64 providerFrame = 0,
              std::shared_ptr<Neurotic::Lifecycle::NativeLeaseOwner> leaseOwner = {})
    {
        std::lock_guard lock(mutex);
        (void)backbuffer; // diagnostic only when provider-frame identity is unavailable
        if (!telemetry.enabled || !selection.enabled || selection.generation != telemetry.generation)
        { Reject("Native guide selection belongs to an inactive/stale test generation"); return false; }
        if (!selection.captureError.empty())
        { Reject(selection.captureError); return false; } // retain the producer failure, including its descriptors
        if (selection.count == 0)
        { Reject("Native capture callback not observed in this Present interval"); return false; }
        if (selection.count != 1 || selection.slot < 0 || selection.slot >= static_cast<int>(slots.size()))
        { Reject("No unique completed Native capture for this Present interval"); return false; }
        auto& slot = slots[selection.slot];
        // GetCurrentBackBufferIndex is sampled at Native evaluation and again at Present.
        // Some flip-model games advance that observable index between the two hooks even though
        // the unique capture still belongs to this Present interval. Keep provider-frame identity
        // exact when available; otherwise the interval, swapchain, size and GPU order are the proof.
        if (slot.epoch != selection.epoch || slot.generation != selection.generation ||
            slot.swapchain.Get() != swapchain ||
            ((slot.providerFrame || providerFrame) && slot.providerFrame != providerFrame) ||
            slot.width != width || slot.height != height || slot.consumer)
        { Reject("Native guide provider-frame/swapchain/size mismatch"); return false; }
        if (!(providerFrame ? GpuSafety::OrderBefore(slot.producer, queue) :
                              GpuSafety::OrderedOn(slot.producer, queue)))
        { Reject("Native copy not uniquely submitted on Present queue"); return false; }
        if (slot.dx11Producer && (!slot.dx11Producer->Valid() || slot.dx11Producer->orderedQueue.Get() != queue))
        { Reject("DX11 guide producer fence/queue mismatch"); return false; }
        slot.consumer = GpuSafety::Record(list);
        if (!slot.consumer) { Reject("Present guide consumer tracking unavailable"); return false; }
        slot.selectionReservation.reset(); // the recorded consumer now retains the copy
        if (leaseOwner)
        {
            try
            {
            // Retain both CPU wrappers before any admission. Partial failure pins the
            // existing slot; dropping a wrapper cannot force release of owner resources.
            slot.depthLease=std::make_shared<Neurotic::Lifecycle::NativeLeaseRegistration>(leaseOwner);
            slot.motionLease=std::make_shared<Neurotic::Lifecycle::NativeLeaseRegistration>(std::move(leaseOwner));
            if (!slot.depthLease->Begin(slot.depth.Get(),list,queue,slot.producer,slot.consumer,providerFrame!=0) ||
                !slot.motionLease->Begin(slot.motion.Get(),list,queue,slot.producer,slot.consumer,providerFrame!=0))
            { slot.depthLease->Close();slot.motionLease->Close();Reject("C03 private guide lease refused");return false; }
            }
            catch (...)
            { Reject("C03 private guide metadata unavailable");return false; }
        }
        inputs = {slot.depth, slot.motion, slot.frame};
        ++telemetry.matched;
        telemetry.status = "Matched Native guides bound; model success not yet confirmed";
        return true;
    }
    // Called by the consuming owner immediately before its existing submission while
    // its list/identity scope is stable. This does not submit, seal or release anything.
    bool ValidateLeasedSubmit(const Selection& selection,ID3D12GraphicsCommandList* list,
                              ID3D12CommandQueue* queue)
    {
        std::lock_guard lock(mutex);
        if (!telemetry.enabled || selection.generation!=telemetry.generation || selection.slot<0 ||
            selection.slot>=static_cast<int>(slots.size()))return false;
        auto& slot=slots[selection.slot];
        if(slot.epoch!=selection.epoch || slot.generation!=selection.generation || !slot.depthLease || !slot.motionLease)
            return false;
        return slot.depthLease->ValidateSubmit(slot.depth.Get(),list,queue,slot.producer,slot.consumer,slot.providerFrame!=0) &&
               slot.motionLease->ValidateSubmit(slot.motion.Get(),list,queue,slot.producer,slot.consumer,slot.providerFrame!=0);
    }
    void CloseLeasedAdmission(const Selection& selection)
    {
        std::lock_guard lock(mutex);
        if(selection.slot<0 || selection.slot>=static_cast<int>(slots.size()))return;
        auto& slot=slots[selection.slot];
        if(slot.epoch!=selection.epoch || slot.generation!=selection.generation)return;
        if(slot.depthLease)slot.depthLease->Close();if(slot.motionLease)slot.motionLease->Close();
    }
    // Readiness for arbitration only; never consumes a slot or grants resource access.
    // Rechecked by Bind after BeginPresent, including its owner/lease checks.
    bool CurrentQualified(ID3D12CommandQueue* queue, IUnknown* swapchain,
                          UINT width, UINT height)
    {
        std::lock_guard lock(mutex);
        if (!queue || !swapchain || !telemetry.enabled || !epochError.empty() || count != 1 ||
            candidate < 0 || candidate >= static_cast<int>(slots.size())) return false;
        const auto& slot = slots[candidate];
        if (!slot.depth || !slot.motion || slot.consumer || slot.epoch != epoch ||
            slot.generation != telemetry.generation || slot.swapchain.Get() != swapchain ||
            slot.width != width || slot.height != height || slot.providerFrame) return false;
        if (!GpuSafety::OrderedOn(slot.producer, queue)) return false;
        return !slot.dx11Producer || (slot.dx11Producer->Valid() &&
               slot.dx11Producer->orderedQueue.Get() == queue);
    }
    bool MatchMetadata(const Selection& selection, ID3D12CommandQueue* queue,
                       IUnknown* swapchain, UINT backbuffer, UINT width, UINT height,
                       UINT64 providerFrame = 0)
    {
        std::lock_guard lock(mutex);
        (void)backbuffer; // diagnostic only when provider-frame identity is unavailable
        if (!telemetry.enabled || !selection.enabled || selection.generation != telemetry.generation)
        { Reject("Native metadata belongs to an inactive/stale route or resolution"); return false; }
        if (!selection.captureError.empty()) { Reject(selection.captureError); return false; }
        if (selection.count != 1 || !selection.producer)
        { Reject("No fresh unique Native render metadata in this Present interval"); return false; }
        // The non-provider path is already tied to exactly one Native evaluation since the prior
        // BeginPresent. Its two DXGI index observations are diagnostic, not a stable frame token.
        if (selection.swapchain.Get() != swapchain ||
            ((selection.providerFrame || providerFrame) && selection.providerFrame != providerFrame) ||
            selection.width != width || selection.height != height)
        { Reject("Native metadata provider-frame/swapchain/output-size mismatch"); return false; }
        if (!(providerFrame ? GpuSafety::OrderBefore(selection.producer, queue) :
                              GpuSafety::OrderedOn(selection.producer, queue)))
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
