#pragma once
// NR-FEED-001 BEGIN
#include <inputs/universal_feeder/providers/PresentGuideObservationAdapter.h>
// NR-FEED-001 END

#include "NrGpuSafety.h"
#include "PresentGuideMemory.h"
#include "DlssNr_DepthFormats.h"
#include "NativeIdentity.h"
#include <nr/lifecycle/NativeLeaseBinding.h>
#include "DlssNr_PresentResolution.h"
#include <shaders/dlssnr/DlssNr_Common.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <deque>
#include <limits>
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
struct CaptureProducerIdentity
{
    unsigned slot=0;
    uint64_t producer=0,consumer=0;
};
struct CaptureOccupancy
{
    bool known=false;
    UINT64 epoch=0,generation=0,providerFrame=0,providerGeneration=0;
    UINT64 residentBytes=0,depthBytes=0,motionBytes=0,budgetBytes=0; // residentBytes is committed guide bytes in this domain
    bool budgetKnown=false,budgetFresh=false,budgetQueried=false;
    UINT64 processUsageBytes=0,requestedPairBytes=0,reservedGuideBytes=0;
    LUID budgetAdapter {};UINT budgetNode=0,heapCreationMask=1;HRESULT budgetQueryResult=S_FALSE;
    unsigned slots=0,availableSlots=0,constraint=0;
    int firstAvailableSlot=-1;
    // First failed existing predicate per slot, in its original short-circuit order:
    // same epoch, pending frame, reservation, producer, consumer, depth/motion lease.
    std::array<unsigned,7> firstBlocked{};
    // Refusal-only read-only proof for the exact first-blocked producer slots.
    // Separate observations can change after the admission predicate; never grant reuse.
    bool producerProofKnown=false;
    unsigned producerProofSequence=0,producerProofMask=0;
    // Mask remains the low unsigned-width slots; counts include ALL blocked
    // producers. Identity rows are bounded diagnostics, never an admission cap.
    bool producerProofMaskComplete=false;
    unsigned producerIdentityCount=0,producerIdentityOmitted=0;
    std::array<CaptureProducerIdentity,16> producerIdentities{};
    GpuSafety::SlotSnapshot producerProof;
};
struct Snapshot
{
    bool enabled = false;
    unsigned long long generation = 0, captures = 0, matched = 0, evaluated = 0, rejected = 0;
    unsigned long long captureAttempts = 0;
    std::string captureError;
    std::string inputDescription;
    CaptureOccupancy captureOccupancy;
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
    CaptureOccupancy captureOccupancy;
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

// A memory-budget-bounded bridge. Original resources are never passed to Present.
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
        LUID allocationAdapter {};UINT allocationCreationMask=1;
        UINT64 providerFrame = 0;
        UINT width = 0, height = 0, backbuffer = 0;
        bool firstBlockedProducer=false; // per-Capture scratch, under bridge mutex
    };
    std::mutex mutex;
    PresentGuideMemory::Cache memoryBudgets;
    // Appending preserves existing Slot references, resources and numeric IDs.
    // Eight initial metadata slots retain the ordinary small-working-set path.
    std::deque<Slot> slots = std::deque<Slot>(8);
    Snapshot telemetry;
    UINT64 epoch = 1;
    unsigned int count = 0;
    int candidate = -1;
    std::string epochError;
    CaptureOccupancy epochOccupancy;
    Selection metadata;
    UINT64 configurationKey = 0;
    UINT64 dx11FeatureOwner = 0;
    struct PendingFrame
    {
        UINT64 token = 0, epoch = 0;
        unsigned int count = 0;
        int candidate = -1;
        std::string error;
        CaptureOccupancy occupancy;
        Selection metadata;
    };
    // Provider frames can be recorded ahead of Present. Retaining their metadata
    // grants no GPU access: MatchMetadata/Bind still require the exact token and order.
    std::array<PendingFrame, 16> pendingFrames;
    UINT64 nextEpoch = 2;
    UINT64 activeProviderGeneration = 0, lastPresentedProviderFrame = 0;
    bool havePresentedProviderFrame = false;

    void InvalidateLocked()
    {
        CloseLeaseAdmissions();
        dx11FeatureOwner = 0;
        ++telemetry.generation; epoch = nextEpoch++; count = 0; candidate = -1;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        metadata = {}; epochError.clear();
        epochOccupancy = {}; telemetry.captureOccupancy = {};
        telemetry.status = "Native source lifecycle changed; waiting for fresh guides";
    }

    bool SyncProviderGeneration(UINT64 generation)
    {
        if (!generation || generation == activeProviderGeneration) return true;
        if (generation < activeProviderGeneration) return false;
        CloseLeaseAdmissions();
        dx11FeatureOwner = 0;
        activeProviderGeneration = generation;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        lastPresentedProviderFrame = 0;
        ++telemetry.generation; epoch = nextEpoch++;
        count = 0; candidate = -1; metadata = {}; epochError.clear();epochOccupancy={};telemetry.captureOccupancy={};
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
        return GuideFormats::DepthType(format).view;
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
        // Subresource zero is mip zero, array zero, DEPTH plane zero for D24S8/D32S8.
        // The shared depth-family view reads that plane only. Do not copy or
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
        InvalidateLocked();
    }
    UINT64 ActivateDx11Feature(UINT64 feature)
    {
        std::lock_guard lock(mutex);
        if (!feature) return 0;
        if (dx11FeatureOwner == feature) return telemetry.generation;
        InvalidateLocked();
        dx11FeatureOwner = feature;
        return telemetry.generation;
    }
    void InvalidateDx11Feature(UINT64 feature)
    {
        std::lock_guard lock(mutex);
        // A delayed destructor can revoke only its own current publication.
        // Closing admission never clears retained slots/tickets or proves retirement.
        if (feature && dx11FeatureOwner == feature) InvalidateLocked();
    }
    void RejectNative(const std::string& reason, UINT64 providerFrame = 0, UINT64 providerGeneration = 0,
                      UINT64 expectedDx11Feature = 0, UINT64 expectedDx11Generation = 0)
    {
        std::lock_guard lock(mutex);
        if (expectedDx11Feature && dx11FeatureOwner != expectedDx11Feature) return;
        if (expectedDx11Generation && telemetry.generation != expectedDx11Generation) return;
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
        dx11FeatureOwner = 0;
        configurationKey = key;
        telemetry.enabled = enabled;
        ++telemetry.generation;
        epoch = nextEpoch++; count = 0; candidate = -1;
        pendingFrames = {};
        havePresentedProviderFrame = false;
        metadata = {};
        epochError.clear(); telemetry.captureError.clear(); telemetry.inputDescription.clear();epochOccupancy={};telemetry.captureOccupancy={};
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
        selection.captureOccupancy=epochOccupancy;
        count = 0; candidate = -1;
        metadata = {};
        epochError.clear();epochOccupancy={};
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
                    selection.captureOccupancy=frame.occupancy;
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
                 std::shared_ptr<const Dx11Producer> dx11Producer = {}, UINT64 providerGeneration = 0,
                 UINT64 expectedDx11Generation = 0)
    {
        std::lock_guard lock(mutex);
        if (dx11Producer && expectedDx11Generation &&
            (dx11FeatureOwner != dx11Producer->feature || telemetry.generation != expectedDx11Generation)) return;
        // NR-FEED-001 BEGIN
        Neurotic::Feed::Callback feedGuides({"PresentGuides", Neurotic::Contracts::GraphicsApi::D3D12,
            "capture", telemetry.generation, Neurotic::Contracts::SourceClass::HostObserved}, this);
        Neurotic::Feed::ObservePresentGuide(feedGuides, frame, depth, motion);
        feedGuides.Value("bridge.epoch", epoch);feedGuides.Value("provider.frame", providerFrame);
        // NR-FEED-001 END
        if (!telemetry.enabled) return;
        telemetry.captureOccupancy={};
        if (!SyncProviderGeneration(providerGeneration) || AlreadyPresented(providerFrame))
        { telemetry.captureError = "Native capture belongs to a stale provider frame"; Reject(telemetry.captureError); return; }
        dx11FeatureOwner = dx11Producer ? dx11Producer->feature : 0;
        auto* pending = providerFrame ? Pending(providerFrame) : nullptr;
        if (providerFrame && !pending)
        { telemetry.captureError = "Native provider frame metadata capacity busy"; Reject(telemetry.captureError); return; }
        auto& captureCount = pending ? pending->count : this->count;
        auto& captureCandidate = pending ? pending->candidate : this->candidate;
        auto& captureError = pending ? pending->error : this->epochError;
        auto& captureMetadata = pending ? pending->metadata : this->metadata;
        const auto captureEpoch = pending ? pending->epoch : this->epoch;
        auto& captureOccupancy=pending?pending->occupancy:epochOccupancy;
        captureOccupancy={};
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
        if (frame.MotionVectorsJittered)
        { RejectCapture("Native capture: jittered motion-vector normalization is unavailable"); return; }
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
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        int matchingSlot = -1;
        unsigned producerFirstBlockedMask=0;
        bool producerMaskComplete=true;
        constexpr unsigned proofMaskBits=std::numeric_limits<unsigned>::digits;
        int freeSlot = -1;
        UINT64 resident = 0;
        CaptureOccupancy occupancy;occupancy.known=true;occupancy.epoch=captureEpoch;
        occupancy.generation=telemetry.generation;occupancy.providerFrame=providerFrame;
        occupancy.providerGeneration=providerGeneration;occupancy.slots=static_cast<unsigned>(slots.size());
        for (unsigned int i = 0; i < slots.size(); ++i)
        {
            auto& slot = slots[i];
            slot.firstBlockedProducer=false;
            bool recyclable=false;
            if(slot.epoch==captureEpoch)++occupancy.firstBlocked[0];
            else if(PendingSlot(i))++occupancy.firstBlocked[1];
            else if(!slot.selectionReservation.expired())++occupancy.firstBlocked[2];
            else if(!GpuSafety::Reusable(slot.producer)){
                ++occupancy.firstBlocked[3];slot.firstBlockedProducer=true;
                if(i<proofMaskBits)producerFirstBlockedMask|=1u<<i;
                else producerMaskComplete=false;
            }
            else if(!GpuSafety::Reusable(slot.consumer))++occupancy.firstBlocked[4];
            else if(slot.depthLease&&!slot.depthLease->CanRecycle(slot.depth.Get(),slot.producer,slot.consumer))++occupancy.firstBlocked[5];
            else if(slot.motionLease&&!slot.motionLease->CanRecycle(slot.motion.Get(),slot.producer,slot.consumer))++occupancy.firstBlocked[6];
            else recyclable=true;
            if(recyclable)
            {
                ++occupancy.availableSlots;
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
                    if (matchingSlot < 0) matchingSlot = static_cast<int>(i);
                    slot.producer.reset(); slot.consumer.reset();
                    slot.depthLease.reset(); slot.motionLease.reset();
                    slot.originalDepth.Reset(); slot.originalMotion.Reset(); slot.swapchain.Reset();
                }
            }
            // Resource-attached charges count actual allocation lifetime, even
            // when an external recording still retains a cleared slot's resource.
        }
        // Prefer an existing matching pair even if an earlier metadata slot is empty.
        if (matchingSlot >= 0) freeSlot = matchingSlot;
        const bool reuse = freeSlot >= 0 && slots[freeSlot].depth != nullptr;
        // Query on growth only; ordinary reuse adds no allocation or DXGI query.
        const auto memory = memoryBudgets.Read(device.Get(), heap, !reuse);
        const UINT allocationMask = device->GetNodeCount() > 1 ? memory.visibleMask : 0;
        const auto depthBytes = device->GetResourceAllocationInfo(allocationMask, 1, &dd).SizeInBytes;
        const auto motionBytes = device->GetResourceAllocationInfo(allocationMask, 1, &md).SizeInBytes;
        resident = memory.ledger ? memory.ledger->committed.load(std::memory_order_relaxed) : 0;
        const auto reserved = memory.ledger ? memory.ledger->reserved.load(std::memory_order_relaxed) : 0;
        const auto admission = PresentGuideBudget::Evaluate(memory.observation, resident, depthBytes, motionBytes, reuse, reserved);
        occupancy.firstAvailableSlot=freeSlot;occupancy.residentBytes=resident;
        occupancy.depthBytes=depthBytes;occupancy.motionBytes=motionBytes;
        occupancy.budgetBytes=memory.observation.known?memory.observation.budget:PresentGuideBudget::LegacyFallbackBytes;
        occupancy.budgetKnown=memory.observation.known;occupancy.budgetFresh=memory.observation.fresh;
        occupancy.budgetQueried=memory.queried;occupancy.processUsageBytes=memory.observation.usage;
        occupancy.requestedPairBytes=admission.bytes;occupancy.reservedGuideBytes=reserved;occupancy.budgetQueryResult=memory.queryResult;
        occupancy.budgetAdapter=memory.adapter;occupancy.budgetNode=memory.node;occupancy.heapCreationMask=memory.creationMask;
        const auto busy=[&](unsigned constraint){
            if(occupancy.firstBlocked[3]){
                // Reserve the process-wide finite budget BEFORE extra inspection.
                static std::atomic<unsigned> proofReceipts{0};
                auto used=proofReceipts.load(std::memory_order_relaxed);
                while(used<64&&!proofReceipts.compare_exchange_weak(used,used+1,std::memory_order_relaxed)){}
                if(used<64)try{
                    GpuSafety::SlotSnapshot proof;
                    for(unsigned i=0;i<slots.size();++i)if(slots[i].firstBlockedProducer){
                        const auto observed=GpuSafety::InspectSlots(&slots[i].producer,1);
                        proof.reusable+=observed.reusable;proof.gpuPending+=observed.gpuPending;
                        proof.completedUnsealed+=observed.completedUnsealed;
                        proof.unsubmittedUnsealed+=observed.unsubmittedUnsealed;proof.failed+=observed.failed;
                        proof.registryFailed|=observed.registryFailed;
                        if(occupancy.producerIdentityCount<occupancy.producerIdentities.size()) {
                            occupancy.producerIdentities[occupancy.producerIdentityCount++]={i,
                                GpuSafety::TicketIdentity(slots[i].producer),GpuSafety::TicketIdentity(slots[i].consumer)};
                        } else ++occupancy.producerIdentityOmitted;
                    }
                    occupancy.producerProof=proof;
                    occupancy.producerProofMask=producerFirstBlockedMask;
                    occupancy.producerProofMaskComplete=producerMaskComplete;
                    occupancy.producerProofSequence=used+1;
                    occupancy.producerProofKnown=true;
                }catch(...){
                    occupancy.producerIdentityCount=occupancy.producerIdentityOmitted=0;
                    /* Diagnostic failure cannot change original refusal or GPU ownership. */
                }
            }
            occupancy.constraint=constraint;captureOccupancy=occupancy;telemetry.captureOccupancy=occupancy;
            RejectCapture("Native guide copy slots/budget busy; no stale reuse");
        };
        if (!reuse && !memory.ledger)
        { RejectCapture("Native guide allocation accounting unavailable; retry"); return; }
        if (!admission.allowed)
        { busy(static_cast<unsigned>(admission.refusal)); return; }
        // Capture owns the bridge mutex through both Create calls and publication:
        // the complete pair reservation cannot race another bridge allocation.
        const auto bytes = reuse ? slots[freeSlot].bytes : admission.bytes;
        PresentGuideAllocation::Reservation reservation(memory.ledger, reuse ? 0 : bytes);
        if (freeSlot<0)
        {
            // All old copies remain retained. Allocate metadata BEFORE recording
            // commands; failure cannot leave commands referring to freed images.
            if(slots.size()>=static_cast<size_t>((std::numeric_limits<int>::max)()))
            { busy(1); return; } // Selection's existing numeric index representation
            try { slots.emplace_back(); }
            catch(...) { RejectCapture("Native guide slot metadata allocation failed"); return; }
            freeSlot=static_cast<int>(slots.size()-1);
            occupancy.slots=static_cast<unsigned>(slots.size());
            occupancy.availableSlots=1;occupancy.firstAvailableSlot=freeSlot;
        }
        // If resource creation fails, the new empty metadata slot stays available
        // for a later attempt. Repeated failures cannot append empty slots forever.
        Slot next;
        if (reuse)
        {
            next.depth = slots[freeSlot].depth;
            next.motion = slots[freeSlot].motion;
        }
        else if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &dd,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&next.depth))) ||
            FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &md,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&next.motion))))
        { occupancy.constraint=4;captureOccupancy=occupancy;telemetry.captureOccupancy=occupancy;
          RejectCapture("Native guide copy allocation failed"); return; }
        // Private-data ownership keeps charges alive until real final resource
        // release. Registration failure precedes any recorded copy and unwinds only
        // this unsubmitted pair; old pinned resources are untouched.
        if (!reuse && (!reservation.Attach(next.depth.Get(), depthBytes) ||
                       !reservation.Attach(next.motion.Get(), motionBytes)))
        { RejectCapture("Native guide allocation accounting attachment failed; retry"); return; }
        next.producer = GpuSafety::Record(list);
        next.dx11Producer = std::move(dx11Producer);
        if (!next.producer) { RejectCapture("Native guide producer tracking unavailable"); return; }
        next.originalDepth = depth; next.originalMotion = motion;
        next.swapchain = swapchain; next.backbuffer = backbuffer;
        next.width = width; next.height = height;
        next.epoch = captureEpoch; next.generation = telemetry.generation; next.bytes = bytes;
        next.allocationAdapter=memory.adapter;next.allocationCreationMask=memory.creationMask;
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
        if (!reuse) { captureOccupancy=occupancy;telemetry.captureOccupancy=occupancy; }
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
