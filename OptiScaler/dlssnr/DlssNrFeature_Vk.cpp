#include "pch.h"
#include <atomic>
#include "FinalFallbackControl.h"
#include "VulkanNrStreamline.h"
#include "VulkanNrPassRecorder.h"
#include "VulkanPresentColor.h"
#include "VulkanNrExposureScan.h"
#include "VulkanNrCaptureVk.h"
#include "VulkanNrPerformanceCapture.h"
#include "VulkanNrFinalColor.h"
#include "VulkanNrFfxFg.h"
#include "VulkanPresentGuidesVk.h"
#include "VulkanPresentRegistry.h"
#include "VulkanNrPreFg.h"
#include "VulkanNrImageSnapshot.h"
#include "VulkanNrImageFacts.h"
#include "VulkanNrInputLayout.h"
#include "VulkanNrNativeOutput.h"
#include <nr/diagnostics/capability/CapabilityOwnerAdapters.h>

#include "DlssNrFeature_Vk.h"
#include "DlssNrFeature_Dx12.h"
#include "VulkanNrTuning.h"
#include "VulkanNrGeneration.h"
#include "VulkanNrMemoryBudget.h"
#include "VulkanNrPreSr.h"
#include "VulkanNrColorInput.h"
#include "VulkanNrFlightRecorder.h"
#include "NrExperimentalPolicy.h"
#include "VulkanNrCompletion.h"
#include "VulkanNrFrameParams.h"
#include "VulkanNrRuntime.h"
#include "VulkanNrReservations.h"
#include "VulkanNrSession.h"
#include "VulkanPresentExecutor.h"
#include "VulkanPresentStatus.h"
#include "VulkanNrSignalReceipts.h"

#include <Config.h>
#include <State.h>
#include <upscalers/IFeature.h>
#include <Util.h>
#include <NVNGX_Parameter.h>
#include <proxies/NVNGX_Proxy.h>
#include <hooks/VulkanwDx12_Hooks.h>

#include <shaders/dlssnr/DlssNr_Vk.h>
#include <shaders/output_scaling/OS_Vk.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <string>

namespace DlssNr
{
static std::atomic<uint32_t> vkGenerationLossReceipts{0};
static bool VkGenerationLossTraceEnabled() noexcept
{
    return !loggerClosed.load(std::memory_order_relaxed)&&spdlog::should_log(spdlog::level::trace)&&
        vkGenerationLossReceipts.load(std::memory_order_relaxed)<32;
}
static std::optional<VkNrGenerationLoss> VkGenerationLossTrace(const char* caller)
{
    if(!VkGenerationLossTraceEnabled())return {};
    VkNrGenerationLoss observation;observation.caller=caller;observation.thread=GetCurrentThreadId();return observation;
}
static void TraceVkGenerationLoss(const VkNrGenerationOwner& owner,const VkNrGenerationLoss& loss) noexcept
{
    if(loss.cause==VkNrGenerationLossCause::None||!VkGenerationLossTraceEnabled())return;
    auto receipt=vkGenerationLossReceipts.load(std::memory_order_relaxed);
    while(receipt<32&&!vkGenerationLossReceipts.compare_exchange_weak(receipt,receipt+1,std::memory_order_relaxed)){}
    if(receipt>=32)return;
    try{
        const auto& post=loss.postPredicate;
        LOG_TRACE("Vulkan Present generation first-loss: receipt={} decisionThread={} caller={} origin={} cause={} route={} pass={} deviceGeneration={} routeEpoch={} resume={} swapchainGeneration={} oldGeneration={} nextUnallocatedCandidate={} allocatedReplacement={} appliedAfterOperation={} keyField={} oldBits={} newBits={} use={} reusableObserved={} reusable={} gpuCompleteObserved={} gpuComplete={} postPredicateKnown={} postIncarnation={} postSubmitted={} postPending={} postRecordingReference={} snapshotUnavailable={}",
            receipt+1,loss.thread,loss.caller?loss.caller:"unspecified",loss.origin?loss.origin:"unspecified",static_cast<unsigned>(loss.cause),
            static_cast<unsigned>(loss.route),loss.pass,loss.deviceGeneration,loss.routeEpoch,loss.resumeGeneration,loss.swapchainGeneration,
            loss.oldGeneration,loss.nextCandidate,loss.allocatedReplacement,owner.AppliedGeneration(),loss.difference.field?loss.difference.field:"none",
            loss.difference.before,loss.difference.after,loss.use.value,loss.reusableObserved,loss.reusable,loss.gpuCompleteObserved,loss.gpuComplete,
            post.has_value(),post?post->incarnation:0,post?post->submitted:false,post?post->pending:0,post?post->recordingReference:false,loss.snapshotUnavailable);
    }catch(...){/* Observation never changes generation/recording ownership. */}
}

static void TraceVkGenerationRefusal(const VkNrGenerationResult& result,const VkNrGenerationKey& key)
{
    if(result.diagnostic.empty())return;
    try{
        LOG_INFO("Vulkan NR owned-completion refusal: route={} placement={} pass={} resume={} output={}x{} work={}x{} firstFailure={} diagnostic={}",
            static_cast<unsigned>(key.frame.route),static_cast<unsigned>(key.frame.placement),key.passIndex,
            key.frame.resumeGeneration,key.frame.output.width,key.frame.output.height,key.frame.work.width,key.frame.work.height,
            result.reason,result.diagnostic);
    }catch(...){/* Diagnostics never grant or withhold execution rights. */}
}
static void ShutdownVkLocked(bool deviceAlive,std::optional<uint64_t> sourceRetirementGeneration=std::nullopt);

namespace
{

// One image this pass owns: the storage, the view, and the NGX wrapper that describes it. Kept
// together because they are created, resized and destroyed as one thing.
struct OwnedImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    NVSDK_NGX_Resource_VK ngx {};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint64_t bytes = 0;

    bool Valid() const { return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE; }
};

struct VkResources final : VkNrGenerationPayload
{
    void* feature = nullptr;
    NVSDK_NGX_Parameter* capabilityParams = nullptr;
    VkTuning::Settings settings;
    OwnedImage output, proxy, keep, proxySmall, outputNative, composed, meter;
    OwnedImage preSr, reJitter, dlaaOutput;
    NVSDK_NGX_Handle* dlaaFeature = nullptr;
    NVSDK_NGX_Parameter* dlaaParams = nullptr;
    VkNrSnapshotImages heldImages;
    VkNrHeldFrame held;
    VkNrUseId creationUse;
    VkNrPreSrSeed creationSeed, nrSeed, dlaaSeed;
    bool dlaaReset = true, srReset = true;
    std::unique_ptr<OS_Vk> superUp, superDown;
    uint32_t width = 0, height = 0, workWidth = 0, workHeight = 0;
    void Abandon() override {
        ReleaseVkNrImages(heldImages,false);
        if(superUp)superUp->AbandonDevice();if(superDown)superDown->AbandonDevice();
        superUp.reset();superDown.reset();
    }
};

struct VkState
{
    VkNrNativeSelectedStatus<NrConfigSnapshot<Config>> nativeStatus;
    VkNrSignalDiagnostics<VkNrRecordingOwner> signalDiagnostics;
    bool failed = false;
    const char* reason = "";
    bool modelRecordedCurrent = false;
    bool outputWriteRecordedCurrent = false;
    bool preparedCurrent = false;
    std::vector<VkNrGenerationKey> preparationPlan;
    std::string generationReason;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkResources* resources = nullptr; // Non-owning alias; generations own every physical resource.
    std::unique_ptr<VkNrGenerationOwner> generations;
    std::unique_lock<std::mutex>* controlLock = nullptr;
    VkCommandBuffer recordingCommand = VK_NULL_HANDLE;
    VkQueue modelQueue = VK_NULL_HANDLE;
    uint32_t modelQueueFamily = UINT32_MAX;
    VkNrWorkloadStatus workload;
    std::optional<VkNrWorkloadAdmission> admittedWorkload;
    std::optional<VkTuning::Settings> requestedTuning;
    VkTuning::Result tuningResult = VkTuning::Result::Applied;
    std::string tuningMessage = "Waiting for NR";

    std::unique_ptr<DlssNr_Vk> pass;
    DlssNr_Vk* borrowedPass=nullptr;
    VkNrChainResult chain;
    std::optional<VkNrRecordedOutput> candidate;
    VkFrameRequest recordingFrame;
    std::optional<VkNrFinalColorSource> finalColorSource;VkFrameRequest finalColorRequest;std::optional<NVSDK_NGX_Resource_VK> finalColorExposure;VkNrFinalColorAttempt finalColorAttempt;
    std::optional<NVSDK_NGX_Resource_VK> performanceSource,performanceOriginalSource;
    std::function<bool()> performanceSourceCurrent,performanceRawSourceCurrent;
    const void* performanceParams=nullptr;bool performanceCaptureOnly=false;
    VkNrUseId performanceUse;uint64_t performanceSerial=0;
    VkNrEvaluationIdentity performanceEvaluation;
    DlssNrConstants diagnosticConstants{};
    VkImageView diagnosticProxy=VK_NULL_HANDLE,diagnosticAnswer=VK_NULL_HANDLE;
    VulkanNrExposureScan scan;
    ExposureGuard::WhitePointHold exposureHold;
    VkNrFrameContract exposureContext{};
    VkNrAutomaticWhitePoint automaticWhitePoint;
    bool reportedAutomaticWhitePoint=false;
    uint64_t automaticMeterFrames=0;
    uint64_t automaticReadbacks=0;
    uint64_t automaticTotalAttempts=0,automaticContextChanges=0,reportedAutomaticReadbacks=0;
    uint32_t sceneMeterLogCount=0;
    uint64_t gameExposureSequence=0;
    VkNrPassHistory passHistory;

    VkNrResetRevision resetState{1};uint64_t diagnosticResetEpoch=1;
    uint64_t resumeGeneration = 0, inputInterruptionEpoch = 0;
    uint64_t reportedEncodingGeneration = 0;
    uint64_t pixelProbeGeneration = 0;
    uint32_t pixelProbeFrames = 0;
    unsigned long long frames = 0;
    PreSrResetPolicyState preSrResetPolicy;
    uint64_t preSrEpoch = 1;
    bool preSrDelivered = false;

    // Timing slots stay attached to actual uses and publish only after owned fence completion.
    // Query availability alone is not completion. Vulkan ticks use the device timestampPeriod.
    VkQueryPool queryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    uint32_t timestampBits=0;uint64_t lastQueryUse=0;
    std::array<VkNrFrameContract,VkNrReservationPool::HardCap> queryFrames{};
    std::array<VkNrSignalContext,VkNrReservationPool::HardCap> queryContexts{};
    std::array<uint64_t,VkNrReservationPool::HardCap> queryTimes{};
    std::array<double,VkNrReservationPool::HardCap> queryClocks{};
    VkNrSignalContext lastGpuContext{};
    uint64_t lastGpuAt=0;VkNrCompletedTimings timing;
    unsigned long long timedFrames = 0;
    VkNrReservationPool querySlots;
    std::array<VkNrUseId,VkNrReservationPool::HardCap> queryUses{};
    std::array<bool,VkNrReservationPool::HardCap> queryPublished{};
    std::optional<double> lastGpuTime;
    std::optional<VkNrFrameContract> lastGpuFrame;

    // Whether the game hands over an exposure texture, and what it said when it did.
    bool exposureOffered = false;

    // The game's own exposure, read off its 1x1 texture, and the scale it multiplied its buffer by.
    //
    // gameExposure holds its last good value rather than resetting when a frame arrives without a
    // texture: GTA V dropped it three times in one session on the D3D12 path, and falling back to a
    // default on those frames is a flicker, not a fallback.
    float gameExposure = 0.0f;
    float gamePreExposure = 1.0f;

    // The exposure courier: a kDlssNrMeterGrid-square R32_FLOAT image and retained
    // host-visible buffers. Game exposure uses texel (0,0); scene calibration uses every tile.
    VkBuffer meterReadback[VkNrReservationPool::HardCap] = {};
    VkDeviceMemory meterReadbackMemory[VkNrReservationPool::HardCap] = {};
    void* meterMapped[VkNrReservationPool::HardCap] = {};
    unsigned long long meterFrames = 0;
    std::unique_ptr<VkNrReservationPool> meterSlots;
    std::array<VkNrUseId,VkNrReservationPool::HardCap> meterUses{};
    std::array<bool,VkNrReservationPool::HardCap> meterPublished{};
    std::array<bool,VkNrReservationPool::HardCap> meterScan{},meterGrid{},meterAutomatic{};
    std::array<VkNrFrameContract,VkNrReservationPool::HardCap> meterContexts{};
};

void ClearFinalSource(VkState& state);

// The grid the meter writes and the byte size of its complete readback.
constexpr uint32_t kMeterSide = kDlssNrMeterGrid;
constexpr VkDeviceSize kMeterBytes = kMeterSide * kMeterSide * sizeof(float);

// Readback/query capacity is bounded; reservations retain slots through recording release.
constexpr uint32_t kMeterSlots = VkNrReservationPool::HardCap;

// One pair per reserved timing use, without cyclic overwrite.
constexpr uint32_t kTimingSlots = VkNrReservationPool::HardCap;

struct VkPerformancePair final:VkNrGenerationPayload {
    OwnedImage before,after;NVSDK_NGX_Handle* feature=nullptr;NVSDK_NGX_Parameter* parameters=nullptr;VkNrUseId creation;VkNrPreSrSeed creationSeed;
};
struct VkFinalColorCarrier final:VkNrGenerationPayload {VkDevice device=VK_NULL_HANDLE;OwnedImage target,depth,motion,linear,modelTarget,encoded;std::unique_ptr<PresentColor_Vk> color;void Abandon() override{if(color)color->AbandonDevice();color.reset();}};
std::unique_ptr<VkNrGenerationOwner> g_finalColorCarriers;
std::unique_ptr<VkNrGenerationOwner> g_performancePairs;
VkState g_nativeVk;
VkState g_presentVk;
std::array<VkState,9> g_nativeChildren,g_presentChildren;
VkState& PassStateVk(VkNrRoute route,uint32_t pass) {
    return pass==0 ? (route==VkNrRoute::Native?g_nativeVk:g_presentVk) :
        (route==VkNrRoute::Native?g_nativeChildren[pass-1]:g_presentChildren[pass-1]);
}
// Provider calls and owned waits release g_vkMutex while this scope is alive.
// Another rendering thread must not redirect this call's state on reacquisition.
thread_local VkState* g_activeVk = &g_nativeVk;
VkState& ActiveVk() { return *g_activeVk; }
DlssNr_Vk* CompositionVk(){return ActiveVk().pass?ActiveVk().pass.get():ActiveVk().borrowedPass;}
struct ScopedVkSession
{
    VkState* prior;
    explicit ScopedVkSession(VkNrRoute route,uint32_t pass=0) : prior(g_activeVk)
    {
        g_activeVk = &PassStateVk(route,pass);
    }
    ~ScopedVkSession() { g_activeVk = prior; }
};
uint64_t GenerationBytesVk() {
    uint64_t bytes=0;
    for(auto route:{VkNrRoute::Native,VkNrRoute::Present})for(uint32_t pass=0;pass<10;++pass)
        if(const auto& state=PassStateVk(route,pass);state.generations)bytes+=state.generations->PrivateBytes();
    if(g_finalColorCarriers)bytes+=g_finalColorCarriers->PrivateBytes();
    if(g_performancePairs)bytes+=g_performancePairs->PrivateBytes();
    return bytes;
}
size_t GenerationCountVk() {
    size_t count=0;
    for(auto route:{VkNrRoute::Native,VkNrRoute::Present})for(uint32_t pass=0;pass<10;++pass)
        if(const auto& state=PassStateVk(route,pass);state.generations)count+=state.generations->Count();
    if(g_finalColorCarriers)count+=g_finalColorCarriers->Count();
    if(g_performancePairs)count+=g_performancePairs->Count();
    return count;
}
std::mutex g_vkMutex;
bool g_vkSessionClosed = false;
bool g_vkShutdownFailed = false;
bool g_vkCleanupActive = false;
VulkanNrRuntime g_vkRuntime;
VulkanNrSession g_nativeSession { VkNrRoute::Native, g_vkRuntime };
VulkanNrSession g_presentSession { VkNrRoute::Present, g_vkRuntime };
std::mutex g_nativeRecordMutex;

void Fail(const char* why)
{
    if (ActiveVk().failed)
        return;

    ActiveVk().failed = true;
    ActiveVk().reason = why;
    LOG_ERROR("DLSS-NR Vulkan unavailable: {}", why);
}

// ---------------------------------------------------------------------------------------------
// Images this pass owns
// ---------------------------------------------------------------------------------------------

void DestroyImage(OwnedImage& img)
{
    if (ActiveVk().device == VK_NULL_HANDLE)
        return;

    if (img.view != VK_NULL_HANDLE)
        vkDestroyImageView(ActiveVk().device, img.view, nullptr);

    if (img.image != VK_NULL_HANDLE)
        vkDestroyImage(ActiveVk().device, img.image, nullptr);

    if (img.memory != VK_NULL_HANDLE)
        vkFreeMemory(ActiveVk().device, img.memory, nullptr);

    img = OwnedImage {};
}

bool ReleaseVkResources(VkNrGenerationPayload& payload)
{
    auto& resources = static_cast<VkResources&>(payload);
    if (resources.dlaaFeature) {
        const auto release = NVNGXProxy::VULKAN_ReleaseFeature();
        auto* lock = ActiveVk().controlLock;
        if (lock) lock->unlock();
        const auto result = release ? VkFlight::Call(VkFlight::CallSite::DlaaRelease,reinterpret_cast<uint64_t>(resources.dlaaFeature),[&]{return release(resources.dlaaFeature);}) : NVSDK_NGX_Result_Fail;
        if (lock) lock->lock();
        if (result != NVSDK_NGX_Result_Success) return false;
        resources.dlaaFeature = nullptr;
    }
    if (resources.dlaaParams) {
        const auto destroy = NVNGXProxy::VULKAN_DestroyParameters();
        if (!destroy || destroy(resources.dlaaParams) != NVSDK_NGX_Result_Success) return false;
        resources.dlaaParams = nullptr;
    }
    if (resources.feature) {
        const auto release = g_vkRuntime.Exports().release;
        auto* lock = ActiveVk().controlLock;
        if (lock) lock->unlock();
        const int result = release ? VkFlight::Call(VkFlight::CallSite::PrivateRelease,reinterpret_cast<uint64_t>(resources.feature),[&]{return release(resources.feature);}) : -1;
        if (lock) lock->lock();
        if (result != 1) {
            LOG_ERROR("VK-NR generation release failed result=0x{:X}; retaining owned resources", (uint32_t) result);
            return false;
        }
        resources.feature = nullptr;
    }
    if (resources.capabilityParams) {
        const auto result = NVSDK_NGX_VULKAN_DestroyParameters(resources.capabilityParams);
        if (result != NVSDK_NGX_Result_Success) return false;
        resources.capabilityParams = nullptr;
    }
    for (auto* image : {&resources.output,&resources.proxy,&resources.keep,&resources.proxySmall,&resources.outputNative,&resources.composed,
                       &resources.preSr,&resources.reJitter,&resources.dlaaOutput,&resources.meter})
        DestroyImage(*image);
    ReleaseVkNrImages(resources.heldImages,true);
    resources.superUp.reset(); resources.superDown.reset();
    return true;
}

// The caller serializes model recording with g_nativeRecordMutex. Provider
// release may temporarily unlock the state mutex, but must retain its owning
// route/pass/device context until every child resource has been released.
static void RetireVkPassGenerationsLocked(VkNrRoute route,uint32_t pass,std::unique_lock<std::mutex>& lock,const char* origin)
{
    ScopedVkSession active(route,pass);
    auto& state=ActiveVk();
    state.signalDiagnostics.Clear();state.timing.Clear();
    if(!state.generations)return;
    struct ControlScope {
        VkState& state;std::unique_lock<std::mutex>* previous;
        ~ControlScope(){state.controlLock=previous;}
    } control{state,state.controlLock};
    state.controlLock=&lock;
    if(state.generations->AppliedGeneration()){state.resetState.Request();}
    state.resources=nullptr;
    auto diagnostic=route==VkNrRoute::Present?VkGenerationLossTrace(origin):std::nullopt;
    state.generations->Disable(origin,diagnostic?&*diagnostic:nullptr);
    state.generations->RetireCompleted(VulkanNrRecordings(),diagnostic?&*diagnostic:nullptr);
    if(diagnostic)TraceVkGenerationLoss(*state.generations,*diagnostic);
}
// End pass generation retirement

// Called under the model-recording mutex and state lock. No wait or inferred
// completion: the selected route starts after every predecessor model is released.
static bool PrepareVkRouteTransitionLocked(VkDevice device,VkNrRoute selected,std::unique_lock<std::mutex>& lock,uint64_t nextEpoch)
{
    VulkanNrFgContributions().RevokeNewContributions(nextEpoch);
    const auto previous=selected==VkNrRoute::Native?VkNrRoute::Present:VkNrRoute::Native;
    bool drained=true;
    for(uint32_t pass=0;pass<10;++pass) {
        auto& state=PassStateVk(previous,pass);
        if(state.device!=device)continue;
        RetireVkPassGenerationsLocked(previous,pass,lock,"opposite_route_transition");
        if(state.generations&&state.generations->Count())drained=false;
    }
    return drained;
}
// End route transition

uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps {};
    vkGetPhysicalDeviceMemoryProperties(ActiveVk().physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return UINT32_MAX;
}

// STORAGE and SAMPLED both, because every one of these is written by one dispatch and read by the
// next; TRANSFER_SRC so a capture can copy it out without a second surface.
// Build the OS_Vk resample descriptor for one of our own images. OS_Vk reads Width/Height/Format from
// this (the NR override makes it size from the images, not the current feature).
static VkImageInfo ImageInfoOf(const OwnedImage& img)
{
    VkImageInfo info {};
    info.ImageView = img.view;
    info.Image = img.image;
    info.SubresourceRange = VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    info.Format = img.format;
    info.Width = img.width;
    info.Height = img.height;
    return info;
}

bool CreateImage(OwnedImage& img, uint32_t width, uint32_t height, VkFormat format, bool readWrite,
                 uint64_t* remainingBudget = nullptr,VkImageUsageFlags explicitUsage=0,VkNrMemoryAdmission* headroom=nullptr)
{
    // Creation is initial-only. Never recycle an image referenced by recorded work.
    if (img.image != VK_NULL_HANDLE || img.view != VK_NULL_HANDLE || img.memory != VK_NULL_HANDLE)
        return false;
    OwnedImage replacement;

    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(explicitUsage)info.usage=explicitUsage;
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(ActiveVk().physicalDevice,format,&properties);
    VkFormatFeatureFlags required=0;
    if(info.usage&VK_IMAGE_USAGE_STORAGE_BIT)required|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if(info.usage&VK_IMAGE_USAGE_SAMPLED_BIT)required|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if(info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)required|=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if(info.usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)required|=VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if((properties.optimalTilingFeatures&required)!=required)return false;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ActiveVk().device, &info, nullptr, &replacement.image) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not create a {}x{} image", width, height);
        return false;
    }

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(ActiveVk().device, replacement.image, &req);

    if (remainingBudget && req.size > *remainingBudget) {
        DestroyImage(replacement); return false;
    }
    VkMemoryAllocateInfo alloc {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryTypeIndex(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if(headroom&&!headroom->ReserveImage(alloc.memoryTypeIndex,req.size)) {
        DestroyImage(replacement);return false;
    }
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(ActiveVk().device, &alloc, nullptr, &replacement.memory) != VK_SUCCESS ||
        vkBindImageMemory(ActiveVk().device, replacement.image, replacement.memory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not back a {}x{} image", width, height);
        DestroyImage(replacement);
        return false;
    }

    VkImageViewCreateInfo view {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = replacement.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(ActiveVk().device, &view, nullptr, &replacement.view) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan: could not view a {}x{} image", width, height);
        DestroyImage(replacement);
        return false;
    }

    // Only a new, never-submitted allocation is discarded on the failure paths above.
    replacement.bytes = req.size;
    if (remainingBudget) *remainingBudget -= req.size;
    img = replacement;
    img.width = width;
    img.height = height;
    img.format = format;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // The NGX wrapper. Filled once, because none of it changes until the image is recreated.
    img.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    img.ngx.Resource.ImageViewInfo.ImageView = img.view;
    img.ngx.Resource.ImageViewInfo.Image = img.image;
    img.ngx.Resource.ImageViewInfo.SubresourceRange = view.subresourceRange;
    img.ngx.Resource.ImageViewInfo.Format = format;
    img.ngx.Resource.ImageViewInfo.Width = width;
    img.ngx.Resource.ImageViewInfo.Height = height;
    img.ngx.ReadWrite = readWrite;

    return true;
}

// The ring of host-visible buffers the meter's grid is copied into, created once and mapped for
// good. HOST_COHERENT so the read needs no invalidate; it is universally available for a buffer this
// small and the alternative is a vkInvalidateMappedMemoryRanges on a path that runs every frame.
bool CreateMeterReadback(uint32_t capacity = VkNrReservationPool::InitialCapacity)
{
    VkBuffer buffers[kMeterSlots] {};
    VkDeviceMemory memory[kMeterSlots] {};
    void* mapped[kMeterSlots] {};
    const auto discard = [&]()
    {
        for (size_t i = 0; i < capacity; ++i)
        {
            if (mapped[i] != nullptr) vkUnmapMemory(ActiveVk().device, memory[i]);
            if (buffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(ActiveVk().device, buffers[i], nullptr);
            if (memory[i] != VK_NULL_HANDLE) vkFreeMemory(ActiveVk().device, memory[i], nullptr);
        }
    };
    for (unsigned long long i = 0; i < capacity; ++i)
    {
        if (ActiveVk().meterReadback[i] != VK_NULL_HANDLE)
            continue;

        VkBufferCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = kMeterBytes;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ActiveVk().device, &info, nullptr, &buffers[i]) != VK_SUCCESS)
        {
            LOG_WARN("DLSS-NR Vulkan: could not create the exposure readback buffer");
            discard();
            return false;
        }

        VkMemoryRequirements req {};
        vkGetBufferMemoryRequirements(ActiveVk().device, buffers[i], &req);

        VkMemoryAllocateInfo alloc {};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = FindMemoryTypeIndex(
            req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (alloc.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(ActiveVk().device, &alloc, nullptr, &memory[i]) != VK_SUCCESS ||
            vkBindBufferMemory(ActiveVk().device, buffers[i], memory[i], 0) != VK_SUCCESS ||
            vkMapMemory(ActiveVk().device, memory[i], 0, kMeterBytes, 0, &mapped[i]) !=
                VK_SUCCESS)
        {
            LOG_WARN("DLSS-NR Vulkan: could not back the exposure readback buffer");
            discard();
            return false;
        }
    }

    for (size_t i = 0; i < capacity; ++i)
    {
        if (buffers[i] == VK_NULL_HANDLE) continue;
        ActiveVk().meterReadback[i] = buffers[i];
        ActiveVk().meterReadbackMemory[i] = memory[i];
        ActiveVk().meterMapped[i] = mapped[i];
    }
    return true;
}

void DestroyMeterReadback()
{
    for (unsigned long long i = 0; i < kMeterSlots; ++i)
    {
        if (ActiveVk().meterReadbackMemory[i] != VK_NULL_HANDLE)
        {
            if (ActiveVk().meterMapped[i] != nullptr)
                vkUnmapMemory(ActiveVk().device, ActiveVk().meterReadbackMemory[i]);

            vkFreeMemory(ActiveVk().device, ActiveVk().meterReadbackMemory[i], nullptr);
        }

        if (ActiveVk().meterReadback[i] != VK_NULL_HANDLE)
            vkDestroyBuffer(ActiveVk().device, ActiveVk().meterReadback[i], nullptr);

        ActiveVk().meterMapped[i] = nullptr;
        ActiveVk().meterReadbackMemory[i] = VK_NULL_HANDLE;
        ActiveVk().meterReadback[i] = VK_NULL_HANDLE;
    }

    ActiveVk().meterFrames = 0;
}

VkNrGenerationCreation CreateVkResources(const VkNrGenerationKey& key,VkNrUseId use,uint64_t remaining)
{
    if(GenerationCountVk()>=80)return {{},false,false,"eighty retained Vulkan pass generations await release"};
    auto admission=VkNrAdmitModelMemory(ActiveVk().instance,ActiveVk().physicalDevice,vkGetInstanceProcAddr,
        key.frame.output.width,key.frame.output.height,key.frame.work.width,key.frame.work.height,
        key.frame.placement==VkNrPlacement::BeforeSR,key.privateDlaa,key.hold);
    auto payload = std::make_unique<VkResources>();
    auto& r = *payload;
    r.settings = key.tuning;
    r.width = key.frame.output.width; r.height = key.frame.output.height;
    r.workWidth = key.frame.work.width; r.workHeight = key.frame.work.height;
    const bool resized = r.width != r.workWidth || r.height != r.workHeight;
    const bool super = r.workWidth > r.width || r.workHeight > r.height;
    const bool before = key.frame.placement == VkNrPlacement::BeforeSR;
    r.creationUse = use;
    const uint64_t budget = remaining;
    const VkFormat working = VK_FORMAT_R16G16B16A16_SFLOAT;
    const bool images = CreateImage(r.output,r.workWidth,r.workHeight,working,true,&remaining,0,&admission) &&
        CreateImage(r.proxy,r.width,r.height,working,true,&remaining,0,&admission) &&
        CreateImage(r.keep,r.width,r.height,working,true,&remaining,0,&admission) &&
        CreateImage(r.composed,r.width,r.height,key.targetFormat,true,&remaining,0,&admission) &&
        (!resized || CreateImage(r.proxySmall,r.workWidth,r.workHeight,working,true,&remaining,0,&admission)) &&
        (!super || CreateImage(r.outputNative,r.width,r.height,working,true,&remaining,0,&admission)) &&
        (!before || CreateImage(r.preSr,r.width,r.height,key.targetFormat,true,&remaining,0,&admission)) &&
        (!key.privateDlaa || (CreateImage(r.reJitter,r.width,r.height,key.targetFormat,true,&remaining,0,&admission) &&
                             CreateImage(r.dlaaOutput,r.width,r.height,key.colorFormat,true,&remaining,0,&admission)));
    // The meter image must follow the same generation lifetime as model images;
    // two retained generations may execute on different same-family queues.
    if(images)CreateImage(r.meter,kMeterSide,kMeterSide,VK_FORMAT_R32_SFLOAT,true,&remaining,0,&admission);
    r.privateBytes = budget - remaining;
    if(admission.known&&!admission.allowed) {
        static uint64_t nextReport=0;const auto now=GetTickCount64();
        if(now>=nextReport){nextReport=now+2000;
            LOG_INFO("Vulkan NR allocation deferred: route={} pass={} budget={} usage={} privateEstimate={} opaqueReserve={}",
                static_cast<unsigned>(key.frame.route),key.passIndex,admission.budget,admission.usage,admission.images,admission.reserve);}
        return {std::move(payload),false,false,"Vulkan VRAM headroom is insufficient for another NR model; reduce layers or resolution"};
    }
    if (!images) return {std::move(payload),false,false,"private images unavailable or actual allocation budget exhausted"};
    if (super) {
        r.superUp = std::make_unique<OS_Vk>("DLSS-NR VK supersample up",ActiveVk().device,ActiveVk().physicalDevice,true,static_cast<Scaler>(key.upFilter));
        r.superDown = std::make_unique<OS_Vk>("DLSS-NR VK supersample down",ActiveVk().device,ActiveVk().physicalDevice,false,static_cast<Scaler>(key.downFilter));
        r.privateBytes += r.superUp->PrivateAllocationBytes() + r.superDown->PrivateAllocationBytes();
        if (!r.superUp->IsInit() || !r.superDown->IsInit() || r.privateBytes > budget)
            return {std::move(payload),false,false,"private resampling pipeline unavailable or allocation budget exhausted"};
    }
    const auto allocated = NVSDK_NGX_VULKAN_AllocateParameters(&r.capabilityParams);
    if (allocated != NVSDK_NGX_Result_Success || !r.capabilityParams)
        return {std::move(payload),false,false,"typed model parameter allocation failed"};
    const char* failedKey = "invalid configuration";
    if (!VkTuning::Prepare(r.capabilityParams,r.settings,r.workWidth,r.workHeight,&failedKey))
        return {std::move(payload),false,false,std::string("typed model creation parameter rejected: ")+failedKey};
    // All creation flags are installed before the vendor creates its history.
    const auto flags = key.creationFlags;
    if (!VkTuning::WriteChecked(r.capabilityParams,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags))
        return {std::move(payload),false,false,"typed model creation flags rejected"};
    if (key.privateDlaa) {
        const auto allocate = NVNGXProxy::VULKAN_AllocateParameters();
        if (!NVNGXProxy::IsVulkanInited() || !allocate || !NVNGXProxy::VULKAN_CreateFeature() ||
            !NVNGXProxy::VULKAN_EvaluateFeature() || !NVNGXProxy::VULKAN_ReleaseFeature() ||
            !NVNGXProxy::VULKAN_DestroyParameters() ||
            allocate(&r.dlaaParams) != NVSDK_NGX_Result_Success || !r.dlaaParams)
            return {std::move(payload),false,false,"private native Vulkan DLAA provider unavailable"};
        const auto write = [&](const char* name,auto value) { return VkTuning::WriteChecked(r.dlaaParams,name,value); };
        if (!write(NVSDK_NGX_Parameter_Width,r.width) || !write(NVSDK_NGX_Parameter_Height,r.height) ||
            !write(NVSDK_NGX_Parameter_OutWidth,r.width) || !write(NVSDK_NGX_Parameter_OutHeight,r.height) ||
            !write(NVSDK_NGX_Parameter_PerfQualityValue,static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA)) ||
            !write(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags) ||
            !write(NVSDK_NGX_Parameter_CreationNodeMask,1u) || !write(NVSDK_NGX_Parameter_VisibilityNodeMask,1u))
            return {std::move(payload),false,false,"private DLAA creation parameters rejected"};
        auto* lock = ActiveVk().controlLock;
        const auto create = NVNGXProxy::VULKAN_CreateFeature();
        lock->unlock();
        const auto created = VkFlight::Call(VkFlight::CallSite::DlaaCreate,reinterpret_cast<uint64_t>(ActiveVk().recordingCommand),[&]{return create(ActiveVk().recordingCommand,NVSDK_NGX_Feature_SuperSampling,r.dlaaParams,&r.dlaaFeature);});
        lock->lock();
        ActiveVk().modelRecordedCurrent = true;
        if (created != NVSDK_NGX_Result_Success || !r.dlaaFeature)
            return {std::move(payload),false,true,"private DLAA creation failed; recorded resources retained"};
    }
    if(key.hold) {
        std::string reason;
        if(!CaptureVkNrImages(ActiveVk().recordingFrame,r.heldImages,budget-r.privateBytes,reason))
            return {std::move(payload),false,false,reason};
        r.privateBytes+=r.heldImages.bytes;
        auto held=ActiveVk().recordingFrame;
        held.color=&r.heldImages.resources[0];held.depth=&r.heldImages.resources[1];held.motion=&r.heldImages.resources[2];held.colorLayout=VK_IMAGE_LAYOUT_GENERAL;
        ClearVkNrColorInput(held);
        r.held.Capture(held,0); // The first encode locks its actual selected white point below.
    }
    auto* lock = ActiveVk().controlLock;
    const auto create = g_vkRuntime.Exports().create;
    const auto command = ActiveVk().recordingCommand;
    lock->unlock();
    const int result = VkFlight::Call(VkFlight::CallSite::PrivateCreate,reinterpret_cast<uint64_t>(command),[&]{return create(command,r.capabilityParams,&r.feature);});
    lock->lock();
    ActiveVk().modelRecordedCurrent = true;
    r.creationSeed.Record(use,result == 1 && r.feature != nullptr);
    return {std::move(payload),result==1 && r.feature!=nullptr,true,
            result==1 ? "" : "Vulkan model creation failed; recorded allocation retained"};
}

void Transition(VkCommandBuffer cmd, OwnedImage& img, VkImageLayout to)
{
    // A same-layout barrier still orders earlier shader accesses on the qualified single queue.
    if (img.image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = img.layout;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = img.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    img.layout = to;
}

// A resource the game owns. Its layout is the game's business, so this records the transition and
// puts it back exactly as it was rather than tracking it.
void TransitionForeign(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                       VkImageLayout to)
{
    if (image == VK_NULL_HANDLE || from == to)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------------------------

bool LoadForwarder(std::unique_lock<std::mutex>& lock)
{
    if (g_vkRuntime.Exports().module != nullptr)
        return g_vkRuntime.Exports().init != nullptr && g_vkRuntime.Exports().create != nullptr && g_vkRuntime.Exports().evaluate != nullptr &&
               g_vkRuntime.Exports().release != nullptr && g_vkRuntime.Exports().shutdown != nullptr;

    auto path = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
        path = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
    {
        Fail("nvngx.dll_dlssnr.dll was not found beside OptiScaler or the game");
        return false;
    }

    lock.unlock();
    const auto module = LoadLibraryW(path->wstring().c_str());
    lock.lock();
    g_vkRuntime.Exports().module = module;

    if (g_vkRuntime.Exports().module == nullptr)
    {
        Fail("the forwarder would not load");
        return false;
    }

    g_vkRuntime.Exports().probe = reinterpret_cast<decltype(g_vkRuntime.Exports().probe)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_probe"));
    g_vkRuntime.Exports().init = reinterpret_cast<decltype(g_vkRuntime.Exports().init)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_init"));
    g_vkRuntime.Exports().create = reinterpret_cast<decltype(g_vkRuntime.Exports().create)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_create_v2"));
    g_vkRuntime.Exports().evaluate = reinterpret_cast<decltype(g_vkRuntime.Exports().evaluate)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_evaluate_v2"));
    g_vkRuntime.Exports().release = reinterpret_cast<decltype(g_vkRuntime.Exports().release)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_release_v2"));
    g_vkRuntime.Exports().shutdown = reinterpret_cast<decltype(g_vkRuntime.Exports().shutdown)>(
        GetProcAddress(static_cast<HMODULE>(g_vkRuntime.Exports().module), "dlssnr_vk_shutdown"));

    if (g_vkRuntime.Exports().init == nullptr || g_vkRuntime.Exports().create == nullptr || g_vkRuntime.Exports().evaluate == nullptr ||
        g_vkRuntime.Exports().release == nullptr || g_vkRuntime.Exports().shutdown == nullptr)
    {
        Fail("the forwarder is missing its Vulkan entry points");
        return false;
    }

    return true;
}

// Whether a format can hold linear, open-ended light. A frame the game already tone mapped has white
// at 1 and must not be encoded a second time; an 8-bit or normalised format cannot be scene-referred
// whatever the game says. The D3D12 path asks the same question of DXGI formats.
bool FormatCanHoldLinearHdr(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return true;
    default:
        return false;
    }
}

// The create flags the game gave its own upscaler, which is where HDR and inverted depth are stated.
// Read from the parameter block rather than configured, because they describe the game's buffers and
// getting either wrong is silent: an encoded frame encoded twice, or depth read backwards.
unsigned int GameCreateFlags(NVSDK_NGX_Parameter* params)
{
    unsigned int flags = 0;

    if (params != nullptr)
        params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags);

    return flags;
}

std::optional<std::filesystem::path> FindSnippet()
{
    auto snippet = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx_dlssnr.dll");

    if (!snippet.has_value())
        snippet = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx_dlssnr.dll");

    return snippet;
}

} // namespace

// ---------------------------------------------------------------------------------------------

static VkNrNativeRawStatus g_nativeRawVk;

static bool NativeRunningVkLocked(const NrConfigSnapshot<Config>& cfg)
{
    const auto runtime = cfg.GetDlssNrRuntimeSnapshot();
    const auto key = g_nativeVk.generations ? g_nativeVk.generations->AppliedKey() : std::nullopt;
    const bool requestedBefore = cfg.DlssNrRunBeforeSr.value_or_default();
    const bool placed = key && (key->frame.placement == VkNrPlacement::AfterRR ||
        (requestedBefore ? key->frame.placement == VkNrPlacement::BeforeSR && g_nativeVk.preSrDelivered :
                           key->frame.placement == VkNrPlacement::AfterSR));
    return placed && g_nativeVk.nativeStatus.Matches(cfg,VkNrCompletionDeviceGeneration(g_nativeVk.device),
               g_vkRuntime.Epoch(),g_nativeVk.generations?g_nativeVk.generations->AppliedGeneration():0,
               FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst)) && g_nativeVk.nativeStatus.workload.reason.empty() && runtime.enabled && cfg.DlssNrRoute.value_or_default() == 0 &&
           !g_vkSessionClosed && !g_vkShutdownFailed &&
           g_nativeVk.resources && g_nativeVk.resources->feature != nullptr && !g_nativeVk.failed && g_nativeVk.frames != 0 && !g_nativeVk.resetState.Pending() &&
           runtime.resumeGeneration == g_nativeVk.resumeGeneration&&
           FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst)==g_nativeVk.inputInterruptionEpoch;
}

bool IsRunningVk()
{
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());if(!cfg)return false;
    std::lock_guard lock(g_vkMutex);return NativeRunningVkLocked(*cfg);
}

void ObserveNativeResultVk(const VkNrNativeObservation& observation,const NrConfigSnapshot<Config>& cfg,
                           const VkRecordResult& result,bool before,bool bound,bool succeeded,bool selected)
{
    std::lock_guard lock(g_vkMutex);
    const auto inputEpoch=FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst);
    g_nativeRawVk.Publish(observation,succeeded,inputEpoch);
    auto& state=g_nativeVk;
    // Failed SR still requests authentic history recovery even if its status is stale.
    if(state.nativeStatus.Publish(observation,cfg,result,before,bound,succeeded,selected,inputEpoch)) {
        if(result.nativeRecording)state.signalDiagnostics.Select(result.nativeRecording->use,
            observation.frame.evaluation,state.nativeStatus.accepted,GetTickCount64());
        if(!state.nativeStatus.accepted)state.resetState.Request();
    } else if(!succeeded&&cfg.GetDlssNrRuntimeSnapshot().enabled&&cfg.DlssNrRoute.value_or_default()==0)
        state.resetState.Request();
}

static std::optional<double> NativeGpuTimeVkLocked(const NrConfigSnapshot<Config>&);

static TelemetrySnapshot NativeTelemetryVkLocked(const std::optional<NrConfigSnapshot<Config>>& cfg)
{
    TelemetrySnapshot result;auto& state=g_nativeVk;
    const auto inputEpoch=FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst);
    const bool rawCurrent=g_nativeRawVk.Current(inputEpoch);
    if(rawCurrent) {
        result.nativeInputs=g_nativeRawVk.observation->inputs;
        result.nativeInputs.observations=g_nativeRawVk.observations;
        result.nativeInputGeneration=g_nativeRawVk.generation;
    }
    result.frames=state.frames;
    result.enabled=cfg&&cfg->GetDlssNrRuntimeSnapshot().enabled&&cfg->DlssNrRoute.value_or_default()==0;
    const auto* observation=result.enabled?
        (state.nativeStatus.observation?&*state.nativeStatus.observation:nullptr):
        (rawCurrent?&*g_nativeRawVk.observation:nullptr);
    if(observation&&!observation->Current(inputEpoch))observation=nullptr;
    result.selectedNativeObservations=observation&&result.enabled?state.nativeStatus.observations:0;
    const bool selectedCurrent=result.enabled&&observation&&state.nativeStatus.settings&&
        cfg&&state.nativeStatus.settings->SameConfiguration(*cfg);
    const auto now=GetTickCount64();const auto policy=Connections::SignalPreferences();
    if(selectedCurrent&&state.resources&&state.generations) {
        VkNrSignalContext current{state.device,state.recordingFrame.contract,state.generations->AppliedGeneration(),state.resetState.Capture(),0};
        current.resetEpoch=state.diagnosticResetEpoch;current.policyRevision=policy.revision;
        current.frame.settingsRevision=cfg->ObservationRevision().value_or(0);
        current.frame.deviceGeneration=VkNrCompletionDeviceGeneration(state.device);
        current.frame.routeEpoch=g_vkRuntime.Epoch();current.frame.inputInterruptionEpoch=inputEpoch;
        current.frame.resumeGeneration=cfg->GetDlssNrRuntimeSnapshot().resumeGeneration;
        state.signalDiagnostics.Match(current);state.signalDiagnostics.Poll(VulkanNrRecordings(),now);
        for(size_t i=0;i<result.signalReceipts.size();++i)
            result.signalReceipts[i]=state.signalDiagnostics.Read(static_cast<SignalReceipts::Signal>(i),now);
        const auto& color=result.signalReceipts[size_t(SignalReceipts::Signal::Color)];
        const auto& depth=result.signalReceipts[size_t(SignalReceipts::Signal::Depth)];
        const auto& motion=result.signalReceipts[size_t(SignalReceipts::Signal::Motion)];
        auto& wire=result.inputSignals.status;wire.revision=policy.revision;wire.capture=color.sequence;
        wire.generation=current.applied;
        const auto source=[](const auto& r){return uint32_t(r.fact.admitted?Connections::Source::Native:Connections::Source::None);};
        const auto reason=[](const auto& r){return uint32_t(r.fact.admitted?Connections::Mixed::Reason::Ready:Connections::Mixed::Reason::Unavailable);};
        wire.color=source(color);wire.depth=source(depth);wire.motion=source(motion);
        wire.colorReason=reason(color);wire.depthReason=reason(depth);wire.motionReason=reason(motion);
        result.inputSignals.fresh=color.sequence&&color.age<=2000&&color.state!=SignalReceipts::State::Stale;
        // A used selection is one completed observation, never a union of session totals.
        if(color.sequence&&color.sequence==depth.sequence&&color.sequence==motion.sequence&&
           color.state==SignalReceipts::State::Consumed&&depth.state==SignalReceipts::State::Consumed&&
           motion.state==SignalReceipts::State::Consumed) {
            result.inputSignals.used=wire;result.inputSignals.used.consumed=1;result.inputSignals.usedFresh=true;
        }
    } else {
        state.signalDiagnostics.Clear();
        for(auto& r:result.signalReceipts){r.state=result.enabled?SignalReceipts::State::Pending:SignalReceipts::State::Off;
            r.fact.reason=result.enabled?"Waiting for a current selected Vulkan evaluation":"Native NR is off";}
    }
    result.lifecycleOpen=!g_vkSessionClosed&&!g_vkShutdownFailed;
    result.lifecycleGeneration=g_vkRuntime.Epoch();
    result.nativeRayReconstructionActive=observation&&observation->rayReconstruction;
    result.runBeforeSr=observation&&observation->frame.placement==VkNrPlacement::BeforeSR;
    const auto frame=!observation?VkExtent2D{}:result.runBeforeSr?observation->render:observation->output;
    result.frameWidth=frame.width;result.frameHeight=frame.height;
    result.guideWidth=observation?observation->render.width:0;result.guideHeight=observation?observation->render.height:0;
    result.workWidth=result.enabled&&observation?state.nativeStatus.workload.appliedWork.width:0;
    result.workHeight=result.enabled&&observation?state.nativeStatus.workload.appliedWork.height:0;
    result.modelLoaded=state.resources&&state.resources->feature;
    result.failed=(selectedCurrent&&state.failed)||g_vkShutdownFailed;
    result.resetPending=result.enabled&&state.resetState.Pending();result.historyResetRequested=result.resetPending;
    result.seedEvaluationCompleted=state.resources&&state.resources->nrSeed.Ready();
    result.completedPipelineEvaluations=result.seedEvaluationCompleted?g_nativeSession.CompletedCount():0;
    result.gpuCompletedOutputEvaluations=result.seedEvaluationCompleted&&state.nativeStatus.accepted?
        g_nativeSession.CompletedCount():0;
    result.preSrDisplayReady=state.preSrDelivered&&result.seedEvaluationCompleted;
    result.running=cfg&&NativeRunningVkLocked(*cfg);
    result.transitionPending=result.enabled&&!result.running;result.layerCount=result.enabled?state.nativeStatus.completedPasses:0;
    if(result.running)result.totalGpuMs=NativeGpuTimeVkLocked(*cfg);
    static thread_local std::string reason;
    reason=g_vkShutdownFailed?"Vulkan shutdown could not safely complete; restart the process":
        !result.enabled?"":!selectedCurrent?"Waiting for a selected Vulkan Native evaluation":
        state.failed?state.reason:state.nativeStatus.workload.reason;
    result.failureReason=reason.c_str();result.failureText=reason;return result;
}

TelemetrySnapshot NativeTelemetryVk()
{
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());
    std::lock_guard recordLock(g_nativeRecordMutex);
    std::lock_guard lock(g_vkMutex);
    return NativeTelemetryVkLocked(cfg);
}

Capability::NativeObservation CopyCapabilityObservationVk(const Capability::WriterPort& port,
                                                        const Capability::WriterPort* lifecycle) noexcept
{
    Capability::NativeObservation o;
    try {
        const auto cfg=TryNrConfigSnapshot(*Config::Instance());
        std::lock_guard recordLock(g_nativeRecordMutex);
        std::lock_guard lock(g_vkMutex);
        o.sequence=Capability::ReserveSample(port).sequence;
        if(lifecycle)o.lifecycleSequence=Capability::ReserveSample(*lifecycle).sequence;
        const auto t=NativeTelemetryVkLocked(cfg);
        o.lifecycleOpen=t.lifecycleOpen;o.lifecycleGeneration=t.lifecycleGeneration;
        if(cfg)o.configRevision=cfg->ObservationRevision();
        o.states={t.running,t.transitionPending,t.outputQuarantined,t.failed,t.modelLoaded,
                  t.preSrDisplayReady,t.resetPending,t.nativeRayReconstructionActive};
        o.observedStates=0xfb; // Vulkan has no independent output quarantine observation.
        o.counters={t.frames,t.gameResets,t.featureBuilds,t.featureRebuilds,t.evaluateFailures,
                    t.successfulEvaluations,t.gpuCompletedOutputEvaluations};
        o.observedCounters=0x41; // Only delivered frames and completed pipelines have Vulkan owner counters.
        const bool reason=o.failure.AssignC(t.failureReason);
        const bool layer=o.layer2Failure.AssignC(t.layer2FailureReason);o.reasonsComplete=reason&&layer;
    } catch(...) { return {}; /* Failed capture cannot publish default values as observations. */ }
    return o;
}

const char* FailureReasonVk()
{
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());
    std::lock_guard<std::mutex> lock(g_vkMutex);
    static thread_local std::string reason;
    // The diagnostics fallback getter must not revive an old Native failure on another route.
    if(!cfg||!cfg->GetDlssNrRuntimeSnapshot().enabled||cfg->DlssNrRoute.value_or_default()!=0) {
        reason.clear();return reason.c_str();
    }
    const bool current=g_nativeVk.nativeStatus.observation&&g_nativeVk.nativeStatus.settings&&
        g_nativeVk.nativeStatus.settings->SameConfiguration(*cfg)&&
        g_nativeVk.nativeStatus.observation->Current(FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst));
    reason=g_vkShutdownFailed ? "Vulkan shutdown could not safely complete; restart the process" :
        !current?"Waiting for a selected Vulkan Native evaluation":
        g_nativeVk.failed?g_nativeVk.reason:g_nativeVk.nativeStatus.workload.reason.empty()?g_nativeVk.reason:g_nativeVk.nativeStatus.workload.reason;
    return reason.c_str();
}

void RequestHistoryResetVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    g_nativeVk.resetState.Request();
}

unsigned long long FramesVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    return g_nativeVk.frames;
}

unsigned long long CompletedFramesVk()
{
    std::lock_guard<std::mutex> lock(g_nativeRecordMutex);
    return g_nativeSession.CompletedCount();
}

bool ExposureOfferedVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    return g_nativeVk.exposureOffered;
}

static std::optional<double> GpuTimeVkLocked(VkNrRoute route,uint32_t productRoute,
    const std::array<VkNrGpuStageSample,10>& stages,const NrConfigSnapshot<Config>* cfg=nullptr)
{
    const auto& root=PassStateVk(route,0);const auto passes=root.chain.requested;
    if(!passes||passes>10)return {};
    std::array<VkNrTimingView,10> views{};const auto now=GetTickCount64();
    for(uint32_t pass=0;pass<passes;++pass) {
        const auto& state=PassStateVk(route,pass);const auto& sample=stages[pass];
        auto& view=views[pass];view.ready=state.resources&&state.generations&&state.generations->AppliedKey()&&
            state.lastGpuTime&&state.lastGpuFrame&&sample.pass==pass;
        view.use=sample.use.value;view.lastQueryUse=state.lastQueryUse;view.at=sample.at;
        view.productRoute=sample.productRoute;view.ms=sample.milliseconds;
        view.sample={sample.device,sample.frame,sample.applied,sample.reset,sample.productRoute};
        view.current={state.device,state.recordingFrame.contract,
            state.generations?state.generations->AppliedGeneration():0,state.resetState.Capture(),productRoute};
        view.sample.resetEpoch=sample.resetEpoch;view.current.resetEpoch=state.diagnosticResetEpoch;
        view.current.frame.deviceGeneration=VkNrCompletionDeviceGeneration(state.device);
        view.current.frame.routeEpoch=g_vkRuntime.Epoch();
        view.current.frame.inputInterruptionEpoch=FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst);
        if(cfg){view.current.frame.settingsRevision=cfg->ObservationRevision().value_or(0);
            view.current.frame.resumeGeneration=cfg->GetDlssNrRuntimeSnapshot().resumeGeneration;}
        if(!SameVkNrSignalContext(state.lastGpuContext,view.sample)||state.lastGpuAt!=sample.at)return {};
    }
    const auto aggregate=AggregateVkNrTiming(std::span(views.data(),passes),productRoute,now);
    if(!aggregate)return {};
    return aggregate->milliseconds;
}
// Query completion already holds g_vkMutex. No status/model callback is made
// here: all current pass identities are copied directly from the owned states.
static void PublishCompletedGpuTimeVkLocked(const VkNrRecordingOwner& owner,
    const VkNrGpuStageSample& sample,const NrConfigSnapshot<Config>& cfg)
{
    const auto route=sample.frame.route;const auto selected=uint32_t(cfg.DlssNrRoute.value_or_default());
    if(!cfg.GetDlssNrRuntimeSnapshot().enabled||g_vkSessionClosed||g_vkShutdownFailed||
       !g_vkRuntime.IsActive(route)||sample.productRoute!=selected||
       (route==VkNrRoute::Native?selected!=0:(selected!=1&&selected!=2))||
       !owner.GpuComplete(sample.use)||!owner.RecordingReleased(sample.use))return;
    auto& root=PassStateVk(route,0);const auto requested=root.chain.requested;
    if(!requested||requested>10||sample.pass>=requested)return;
    std::array<VkNrSignalContext,10> current{};
    for(uint32_t pass=0;pass<requested;++pass){
        const auto& state=PassStateVk(route,pass);
        if(state.failed||!state.resources||!state.generations||!state.generations->AppliedKey())return;
        current[pass]={state.device,state.recordingFrame.contract,state.generations->AppliedGeneration(),
            state.resetState.Capture(),selected};
        current[pass].resetEpoch=state.diagnosticResetEpoch;
        current[pass].frame.deviceGeneration=VkNrCompletionDeviceGeneration(state.device);
        current[pass].frame.routeEpoch=g_vkRuntime.Epoch();
        current[pass].frame.settingsRevision=cfg.ObservationRevision().value_or(0);
        current[pass].frame.resumeGeneration=cfg.GetDlssNrRuntimeSnapshot().resumeGeneration;
        current[pass].frame.inputInterruptionEpoch=FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst);
    }
    VkNrTimingView completed;completed.ready=true;completed.use=sample.use.value;
    // This is this immutable completed query slot's use, independent of the
    // latest per-pass display slot, which may already contain a newer use.
    completed.lastQueryUse=sample.use.value;completed.at=sample.at;completed.productRoute=sample.productRoute;
    completed.sample={sample.device,sample.frame,sample.applied,sample.reset,sample.productRoute};
    completed.sample.resetEpoch=sample.resetEpoch;completed.ms=sample.milliseconds;
    try {root.timing.Record(sample.pass,requested,selected,completed,std::span(current.data(),requested),GetTickCount64());}
    catch(...) { /* Optional scalar diagnostics never interrupt rendering. */ }
}
static std::optional<double> NativeGpuTimeVkLocked(const NrConfigSnapshot<Config>& cfg)
{
    return GpuTimeVkLocked(VkNrRoute::Native,0,GetVulkanPresentStatus().NativeGpuStages(),&cfg);
}

VkPresentStatusSnapshot PresentTelemetryVk(const NrConfigSnapshot<Config>& cfg)
{
    std::lock_guard recording(g_nativeRecordMutex);std::lock_guard lock(g_vkMutex);
    auto result=GetVulkanPresentStatus().TimingSnapshot();
    const auto route=uint32_t(cfg.DlssNrRoute.value_or_default());
    const auto& runtime=cfg.GetDlssNrRuntimeSnapshot();auto& state=g_presentVk;
    const auto policy=Connections::SignalPreferences();result.requestedSources=policy;
    // This copy has no owner callbacks. Keep source receipts conservative until
    // their independent source owner validates its current policy and lifetime.
    result.sources.fresh=false;result.sources.usedFresh=false;
    const auto& frame=state.recordingFrame.contract;
    const bool current=runtime.enabled&&(route==1||route==2)&&result.route==route&&
        g_vkRuntime.IsActive(VkNrRoute::Present)&&!state.failed&&
        result.timingDevice==state.device&&result.timingQueue==frame.queue&&
        result.timingSwapchain==frame.swapchain&&result.generation==frame.swapchainGeneration&&
        result.timingDeviceGeneration==frame.deviceGeneration;
    if(current)if(const auto time=GpuTimeVkLocked(VkNrRoute::Present,route,result.gpuStages,&cfg)){
        result.gpuTimeValid=true;result.gpuTimeMs=*time;result.gpuTimeSamples=state.timing.Samples();
    }
    if(!current){result.active=false;result.gpuTimeValid=false;}
    return result;
}
std::optional<double> LastGpuTimeVk()
{
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());if(!cfg)return {};
    std::lock_guard lock(g_vkMutex);
    return NativeRunningVkLocked(*cfg)?NativeGpuTimeVkLocked(*cfg):std::nullopt;
}

std::string TuningStatusVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    return g_nativeVk.tuningMessage + " (" + std::to_string(g_nativeVk.generations ? g_nativeVk.generations->Count() : 0) + "/8 retained generations)";
}
std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk(VkNrPresentLifetimeRead* projection)
{
    const auto unavailable=[&](VkNrPresentLifetimeRead reason)->std::optional<VkNrPresentModelLifetime>{
        if(projection)*projection=reason;return {};
    };
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());
    if(!cfg)return unavailable(VkNrPresentLifetimeRead::ConfigUnavailable);
    if(!cfg->GetDlssNrRuntimeSnapshot().enabled)return unavailable(VkNrPresentLifetimeRead::Disabled);
    const auto route=cfg->DlssNrRoute.value_or_default();if(route!=1&&route!=2)return unavailable(VkNrPresentLifetimeRead::WrongRoute);
    std::unique_lock recording(g_nativeRecordMutex,std::try_to_lock);
    if(!recording.owns_lock())return unavailable(VkNrPresentLifetimeRead::BusyRecording);
    std::unique_lock lock(g_vkMutex,std::try_to_lock);
    if(!lock.owns_lock())return unavailable(VkNrPresentLifetimeRead::BusyState);
    if(!g_vkRuntime.Initialized())return unavailable(VkNrPresentLifetimeRead::Uninitialized);
    if(!g_vkRuntime.IsActive(VkNrRoute::Present))return unavailable(VkNrPresentLifetimeRead::Inactive);
    const auto device=g_vkRuntime.Device();
    if(!device)return unavailable(VkNrPresentLifetimeRead::NoDevice);
    // Live global interruption identity must reject a tail admitted before a switch.
    if(projection)*projection=VkNrPresentLifetimeRead::ReturnedTuple;
    return VkNrPresentModelLifetime{reinterpret_cast<uintptr_t>(device),VkNrCompletionDeviceGeneration(device),
        g_vkRuntime.Epoch(),cfg->GetDlssNrRuntimeSnapshot().resumeGeneration,
        FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst)};
}
std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk()
{return CurrentVulkanPresentModelLifetimeVk(nullptr);}

uint64_t PresentGenerationPrivateBytesVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    return GenerationBytesVk();
}

VkNrCapabilitySnapshot CurrentVulkanNrCapabilities(){
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());if(!cfg){auto result=BuildVkNrCapabilities({});result.capture={false,true,"Vulkan configuration snapshot unavailable"};return result;}
    VkNrCapabilityFacts f;f.enabled=cfg->GetDlssNrRuntimeSnapshot().enabled;f.present=cfg->DlssNrRoute.value_or_default()!=0;
    f.beforeSr=cfg->DlssNrRunBeforeSr.value_or_default();f.apply=cfg->DlssNrApplyModel.value_or_default();f.debug=cfg->DlssNrDebugView.value_or_default()!=0;f.compare=cfg->DlssNrCompare.value_or_default()!=0;
    {std::lock_guard lock(g_vkMutex);f.rayReconstruction=(g_nativeVk.admittedWorkload?g_nativeVk.admittedWorkload->frame.placement:VkNrPlacement::AfterSR)==VkNrPlacement::AfterRR;}
    if(auto* selected=State::Instance().currentFeature;selected&&selected->GetUpscalerType()==Upscaler::DLSSD)f.rayReconstruction=true;
    auto result=BuildVkNrCapabilities(f);
    if(f.beforeSr&&!f.rayReconstruction&&!f.present){auto* selected=State::Instance().currentFeature;
        if(selected&&(selected->IsWithDx12()||selected->Api()!=API::Vulkan||selected->GetUpscalerType()!=Upscaler::DLSS)){
            result.performanceCapture={false,false,"Performance paired capture requires the selected native Vulkan DLSS backend"};
            result.capture=result.performanceCapture;
        }
    }
    uint32_t viewport=0;bool authenticSource=false;uint64_t sourceProvider=0;
    {std::lock_guard lock(g_vkMutex);const auto& root=f.present?g_presentVk:g_nativeVk;
        f.guides=root.admittedWorkload&&root.admittedWorkload->frame.temporal.has_value();
        const auto* output=root.candidate?&*root.candidate:root.finalColorSource?&root.finalColorSource->observation:nullptr;
        authenticSource=output&&output->frame.temporal&&output->frame.temporal->providerFrameKnown;
        if(authenticSource){viewport=output->frame.temporal->viewport;sourceProvider=output->frame.temporal->providerGeneration;}}
    result.enhanced.pending=!f.guides;result.enhanced.reason=f.guides?"":"Enhanced awaits fresh matching temporal guides";
    auto& streamline=VulkanNrStreamlineAdapter();const auto slReason=streamline.Reason(viewport);
    authenticSource=authenticSource&&sourceProvider&&sourceProvider==streamline.Generation();
    result.streamlineFg={streamline.Generation()&&slReason.empty()&&authenticSource,!authenticSource,slReason.empty()&&!authenticSource?"Vulkan FG awaits the selected successful public real-frame source":slReason};
    result.ffxFg.reason=VulkanNrFfxStatus().reason;
    return result;
}
bool NativeRayReconstructionVk(){std::lock_guard lock(g_vkMutex);return g_nativeVk.admittedWorkload && g_nativeVk.admittedWorkload->frame.placement==VkNrPlacement::AfterRR;}
VkNrChainResult PassChainStatusVk(VkNrRoute route){std::lock_guard lock(g_vkMutex);return PassStateVk(route,0).chain;}
float BestExposureScanVk(int* index,float* low,float* high){
 const auto cfg=TryNrConfigSnapshot(*Config::Instance());if(!cfg)return 0;
 std::lock_guard lock(g_vkMutex);const auto& scan=PassStateVk(cfg->DlssNrRoute.value_or_default()?VkNrRoute::Present:VkNrRoute::Native,0).scan;const float value=scan.Value().value_or(0);
 if(index)*index=value>0?0:-1;if(low)*low=scan.Low();if(high)*high=scan.High();return value;
}
std::optional<VkNrRecordedOutput> RecordedVkNrOutput(VkNrRoute route,VkCommandBuffer cmd)
{
    const auto cfg=TryNrConfigSnapshot(*Config::Instance());
    if(!cfg||!cfg->GetDlssNrRuntimeSnapshot().enabled||!cfg->DlssNrApplyModel.value_or_default()||cfg->DlssNrDebugView.value_or_default()!=0||cfg->DlssNrCompare.value_or_default()!=0||((cfg->DlssNrRoute.value_or_default()==0)!=(route==VkNrRoute::Native)))return {};
    std::lock_guard lock(g_vkMutex);auto& root=PassStateVk(route,0);
    if(!root.candidate||!VulkanNrRecordings().UseOnRecording(root.candidate->use,cmd)||
       root.candidate->commandSerial!=VulkanNrRecordings().CommandSerial(cmd))return {};
    return root.candidate;
}
VkNrWorkloadStatus WorkloadStatusVk(VkNrRoute route)
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    if(route!=VkNrRoute::Native)return g_presentVk.workload;
    if(g_nativeVk.nativeStatus.observation&&g_nativeVk.nativeStatus.observation->Current(FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst)))
        return g_nativeVk.nativeStatus.workload;
    VkNrWorkloadStatus result;result.pending=true;result.reason="Waiting for a selected Vulkan Native evaluation";
    return result;
}

static bool EnsureVkProviderDeviceLocked(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                         std::unique_lock<std::mutex>& lock)
{
    if (!LoadForwarder(lock))
        return false;

    // Initialise NGX on this device, once. The snippet path is the model itself; the forwarder loads
    // it so the caller gate sees a module named nvngx.dll.
    return g_vkRuntime.EnsureDevice(device, [&] {
        auto snippet = FindSnippet();

        if (!snippet.has_value())
        {
            Fail("nvngx_dlssnr.dll was not found beside OptiScaler or the game");
            return false;
        }

        lock.unlock();
        const int probe = g_vkRuntime.Exports().probe != nullptr ? g_vkRuntime.Exports().probe(snippet->wstring().c_str()) : 0;
        lock.lock();

        // Four bits, one per entry point. Anything short of fifteen means the model's Vulkan surface
        // is not entirely reachable and there is no point going further.
        if (probe != 15)
        {
            LOG_ERROR("DLSS-NR Vulkan: the model's Vulkan surface is incomplete (probe {})", probe);
            Fail("the model does not expose a complete Vulkan surface");
            return false;
        }

        const auto applicationData = State::Instance().NVNGX_ApplicationDataPath;
        lock.unlock();
        const int result =
            g_vkRuntime.Exports().init(snippet->wstring().c_str(), applicationData.c_str(),
                      (void*) instance, (void*) physicalDevice, (void*) device, 0x0000015);
        lock.lock();

        if (result != 1)
        {
            LOG_ERROR("DLSS-NR Vulkan: NVSDK_NGX_VULKAN_Init_Ext returned {}", result);
            Fail("the model would not initialise on this Vulkan device");
            return false;
        }

        LOG_INFO("DLSS-NR Vulkan: the model initialised on this device");
        return true;
    });
}

static void EvaluateAfterUpscaleVkInternal(const VkFrameRequest& request, NVSDK_NGX_Parameter* params,
                                         const NrConfigSnapshot<Config>& cfg,bool preparationOnly=false)
{
    const auto cmdBuffer = request.commandBuffer;
    const auto instance = request.instance;
    const auto physicalDevice = request.physicalDevice;
    const auto device = request.device;
    const bool beforeSr = request.contract.placement == VkNrPlacement::BeforeSR;
    const auto selectedRoute = cfg.DlssNrRoute.value_or_default();
    const bool routeMatches = request.route == VkNrRoute::Native ? selectedRoute == 0 :
        (selectedRoute == 1 || selectedRoute == 2);
    if (!cfg.GetDlssNrRuntimeSnapshot().enabled || !routeMatches)
        return;

    if (cmdBuffer == VK_NULL_HANDLE || (request.route == VkNrRoute::Native && params == nullptr && !request.privateChain) || device == VK_NULL_HANDLE ||
        physicalDevice == VK_NULL_HANDLE)
        return;

    std::unique_lock<std::mutex> lock(g_vkMutex);
    ScopedVkSession session(request.route,request.passIndex);
    struct Context {
        VkState& state;
        ~Context() {
            if (!state.workload.pending) state.workload.reason.clear();
            else if (state.workload.reason.empty()) state.workload.reason = state.reason[0] ? state.reason : state.tuningMessage;
            if (&state == &g_presentVk) GetVulkanPresentStatus().ObserveWorkload(state.workload);
            state.recordingFrame.color=nullptr;state.recordingFrame.targetColor=nullptr;
            state.recordingFrame.depth=nullptr;state.recordingFrame.motion=nullptr;state.recordingFrame.exposure=nullptr;
            ClearVkNrColorInput(state.recordingFrame);
            state.controlLock = nullptr; state.recordingCommand = VK_NULL_HANDLE;
        }
    } context {ActiveVk()};
    ActiveVk().controlLock = &lock; ActiveVk().recordingCommand = cmdBuffer;
    ActiveVk().modelRecordedCurrent = false;
    ActiveVk().outputWriteRecordedCurrent = false;
    ActiveVk().preparedCurrent = false;
    if (!ActiveVk().failed) ActiveVk().reason = "";

    if (g_vkSessionClosed || g_vkShutdownFailed)
        return;

    // Invalidate the old generation BEFORE reading mapped exposure memory or other old handles.
    // Vulkan has no retained COM device identity; preserve the existing dead-device abandonment
    // policy when shutdown was not observed, without calling the old driver.
    const auto routeDeviceUse = ClassifyVkRouteDevice(ActiveVk().device, g_vkRuntime.Device(), device);
    if (routeDeviceUse == VkRouteDeviceUse::Conflict)
    {
        // A new route with no owned handles is not evidence that the old device died.
        // A real mismatch still cannot be reclaimed through an unknown driver lifetime.
        if (ActiveVk().device != VK_NULL_HANDLE) ShutdownVkLocked(false);
        g_vkShutdownFailed = true;
        Fail("device changed without NGX shutdown; restart the process");
        return;
    }
    if (routeDeviceUse == VkRouteDeviceUse::FirstUse)
        ActiveVk().device = device;
    ActiveVk().instance = instance;
    ActiveVk().physicalDevice = physicalDevice;

    const auto queue = VkNrRecordingQueueContext(device,VulkanNrRecordings().UseFamily(request.use),request.contract.queue);
    if (VulkanNrRecordings().UseDevice(request.use)!=device || !queue ||
        (request.route!=VkNrRoute::Native&&!request.finalColorOnly&&!queue->queue) ||
        (ActiveVk().modelQueue&&queue->queue&&ActiveVk().modelQueue!=queue->queue) ||
        (ActiveVk().modelQueueFamily!=UINT32_MAX&&ActiveVk().modelQueueFamily!=queue->family)) {
        ActiveVk().reason = "Vulkan model recording family or explicit Present queue is unproved";
        return;
    }
    ActiveVk().modelQueue = queue->queue;
    ActiveVk().modelQueueFamily = queue->family;

    if (ActiveVk().failed)
        return;

    const auto runtime = cfg.GetDlssNrRuntimeSnapshot();
    if (!runtime.enabled)
        return;

    if (runtime.resumeGeneration != ActiveVk().resumeGeneration ||
        FinalFallback::CurrentInputEpoch()!=ActiveVk().inputInterruptionEpoch)
    {
        ActiveVk().resumeGeneration = runtime.resumeGeneration;
        ActiveVk().inputInterruptionEpoch = FinalFallback::CurrentInputEpoch();
        ++ActiveVk().diagnosticResetEpoch;ActiveVk().resetState.MarkPending();
        LOG_INFO("DLSS-NR Vulkan: enable transition {} applied at the render boundary; temporal reset requested",
                 runtime.resumeGeneration);
    }

    // The game's own resources, already wrapped: NGX hands Vulkan resources over as
    // NVSDK_NGX_Resource_VK, so only this pass's own images need building.
    auto selectedInput=request;
    NVSDK_NGX_Resource_VK* colour = selectedInput.color;
    NVSDK_NGX_Resource_VK* targetColour = request.targetColor;
    NVSDK_NGX_Resource_VK* depth = request.depth;
    NVSDK_NGX_Resource_VK* motion = request.motion;
    if (params != nullptr)
    {
        if(beforeSr) {
            std::string reason;
            if(!SelectVkNrPreSrColor(selectedInput,params,VulkanNrImageFacts(),VulkanNrRecordings(),
                VkNrCompletionDeviceGeneration(device),true,reason)) {ActiveVk().tuningMessage=reason;ActiveVk().reason=ActiveVk().tuningMessage.c_str();return;}
            colour=selectedInput.color;
        } else params->Get(NVSDK_NGX_Parameter_Output, (void**) &colour);
        params->Get(NVSDK_NGX_Parameter_Depth, (void**) &depth);
        params->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &motion);
        targetColour = beforeSr ? nullptr : colour;
    }
    if(const auto reason=VkNrAdaptedDlaaRefusal(beforeSr,selectedInput.qualifiedColor&&selectedInput.qualifiedColor->Adapted(),cfg.DlssNrPreDlaa.value_or_default())) {
        ActiveVk().reason=reason;return;
    }
    if (beforeSr) {
        const auto& temporal = request.contract.temporal;
        if (!temporal || !temporal->featureGeneration || (!temporal->frameToken&&!temporal->providerFrameKnown) || !ValidVkNrTemporal(*temporal)) {
            ActiveVk().reason = "Native Performance awaits authenticated SR frame metadata"; return;
        }
        const auto reset = AdvancePreSrResetPolicy(ActiveVk().preSrResetPolicy,temporal->reset,
            ExperimentalPolicy::Capture(cfg).active && cfg.DlssNrPreSrSoftReset.value_or_default());
        if (reset.conservativeTransition) {
            ++ActiveVk().preSrEpoch; ActiveVk().preSrDelivered = false;
            ActiveVk().resetState.Request();
        }
        if (temporal->reset && !reset.softResetForBurst) {
            ActiveVk().reason = "Native Performance waits for the game's conservative reset transition"; return;
        }
    }

    NVSDK_NGX_Resource_VK* exposure = request.exposure;
    float preExposure = request.preExposure;
    bool havePre = std::isfinite(preExposure)&&preExposure>0;
    if(params) { havePre=params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&preExposure)==NVSDK_NGX_Result_Success;
        params->Get(NVSDK_NGX_Parameter_ExposureTexture,reinterpret_cast<void**>(&exposure)); }
    VkImageLayout exposureLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    if(exposure&&exposure->Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW&&exposure->Resource.ImageViewInfo.Width==1&&exposure->Resource.ImageViewInfo.Height==1&&VulkanNrImageFacts().Rights(device,*exposure,VK_IMAGE_USAGE_SAMPLED_BIT,false)) {
        vk_state::CommandBufferState observed;
        if(Vulkan_wDx12::cmdBufferStateTracker.CaptureNrState(cmdBuffer,observed)) {
#ifndef LOW_PRECISION_TRACKING
            auto layout=observed.ImageLayouts.find(exposure->Resource.ImageViewInfo.Image);
            auto range=observed.ImageLayoutRanges.find(exposure->Resource.ImageViewInfo.Image);
            if(layout!=observed.ImageLayouts.end()&&range!=observed.ImageLayoutRanges.end()&&
               (range->second.aspectMask&VK_IMAGE_ASPECT_COLOR_BIT)&&range->second.baseMipLevel==0&&range->second.baseArrayLayer==0&&range->second.levelCount&&range->second.layerCount)
                exposureLayout=layout->second;
#endif
        }
    }
    const bool exposureQualified=exposureLayout==VK_IMAGE_LAYOUT_GENERAL||exposureLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ActiveVk().exposureOffered = exposure != nullptr;

    if (havePre && std::isfinite(preExposure) && preExposure > 0.0f)
        ActiveVk().gamePreExposure = preExposure;

    // Publish only complete owned uses; mapped readbacks here are explicitly HOST_COHERENT.
    auto& recordingOwner = VulkanNrRecordings();
    for (uint32_t slot = 0; slot < VkNrReservationPool::HardCap; ++slot) {
        if (!ActiveVk().meterPublished[slot] && ActiveVk().meterMapped[slot] &&
            VkNrReadbackReady(recordingOwner,ActiveVk().meterUses[slot],true,true,false)&&recordingOwner.RecordingReleased(ActiveVk().meterUses[slot])) {
            float measured = 0.0f; std::memcpy(&measured,ActiveVk().meterMapped[slot],sizeof(float));
            if(ActiveVk().meterGrid[slot])measured=VkNrCalibrationPercentile(std::span(static_cast<const float*>(ActiveVk().meterMapped[slot]),kMeterSide*kMeterSide));
            if(ActiveVk().meterAutomatic[slot]) {
                auto& state=ActiveVk();
                state.automaticWhitePoint.ObserveCompleted(state.meterContexts[slot],state.meterUses[slot].value,
                    std::span(static_cast<const float*>(state.meterMapped[slot]),kMeterSide*kMeterSide));
                const auto n=++state.automaticReadbacks;
                if((n<=8||n%120==0)&&state.sceneMeterLogCount<128) {
                    ++state.sceneMeterLogCount;
                    const auto& result=state.automaticWhitePoint.LastObservation();
                    LOG_INFO("DLSS-NR Vulkan scene meter: completed={} use={} outcome={} tiles={} positive={} lit={} peak={} p90={} stableSamples={} windowLow={} windowHigh={} derived={}",
                        n,state.meterUses[slot].value,result.outcome,result.tiles,result.positive,result.lit,result.peak,result.percentile,
                        result.stableSamples,result.windowLow,result.windowHigh,state.automaticWhitePoint.Value().value_or(0));
                }
            }
            if(ActiveVk().meterScan[slot])ActiveVk().scan.Readback(ActiveVk().meterUses[slot],measured,true);
            else if(!ActiveVk().meterAutomatic[slot]&&!ActiveVk().meterGrid[slot]&&std::isfinite(measured)&&measured>0&&ActiveVk().meterUses[slot].value>ActiveVk().gameExposureSequence) {
                ActiveVk().gameExposureSequence=ActiveVk().meterUses[slot].value;ActiveVk().gameExposure=measured;
            }
            ActiveVk().meterPublished[slot] = true;
        }
        if (ActiveVk().queryPool && !ActiveVk().queryPublished[slot] &&
            recordingOwner.GpuComplete(ActiveVk().queryUses[slot])&&recordingOwner.RecordingReleased(ActiveVk().queryUses[slot])) {
            // No WAIT flag: owned completion and each timestamp's availability are independent checks.
            uint64_t values[5][2]{};
            const bool available=vkGetQueryPoolResults(device,ActiveVk().queryPool,slot*5,5,sizeof(values),values,
                sizeof(values[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)==VK_SUCCESS&&
                std::all_of(std::begin(values),std::end(values),[](const auto& v){return v[1]!=0;});
            if(VkNrReadbackReady(recordingOwner,ActiveVk().queryUses[slot],available,true,false)) {
                uint64_t ticks[5]{};for(size_t i=0;i<5;++i)ticks[i]=values[i][0];
                const auto stages=VkNrTimestampStages(ticks,ActiveVk().timestampBits,ActiveVk().timestampPeriod,
                    VkNrDiagnosticMilliseconds()-ActiveVk().queryClocks[slot]);
                const auto& captured=ActiveVk().queryContexts[slot];
                VkNrSignalContext current{device,ActiveVk().recordingFrame.contract,
                    ActiveVk().generations?ActiveVk().generations->AppliedGeneration():0,
                    ActiveVk().resetState.Capture(),uint32_t(cfg.DlssNrRoute.value_or_default())};
                current.resetEpoch=ActiveVk().diagnosticResetEpoch;
                if(stages&&SameVkNrSignalContext(captured,current)) {
                    VkNrGpuStageSample sample;sample.use=ActiveVk().queryUses[slot];sample.frame=captured.frame;
                    sample.pass=request.passIndex;sample.device=captured.device;sample.applied=captured.applied;
                    sample.reset=captured.reset;sample.resetEpoch=captured.resetEpoch;sample.productRoute=captured.productRoute;sample.at=ActiveVk().queryTimes[slot];
                    sample.milliseconds=*stages;double total=0;for(auto ms:*stages)total+=ms;
                    if(sample.use.value>ActiveVk().lastQueryUse){
                        ActiveVk().lastQueryUse=sample.use.value;ActiveVk().lastGpuTime=total;ActiveVk().lastGpuFrame=sample.frame;
                        ActiveVk().lastGpuContext=captured;ActiveVk().lastGpuAt=sample.at;
                        GetVulkanPresentStatus().ObserveGpuStages(recordingOwner,sample);
                    }
                    PublishCompletedGpuTimeVkLocked(recordingOwner,sample,cfg);
                }
                ActiveVk().queryPublished[slot]=true;
            }
        }
    }
    ActiveVk().querySlots.ReleaseCompleted(recordingOwner);
    if (ActiveVk().meterSlots) ActiveVk().meterSlots->ReleaseCompleted(recordingOwner);

    // Said when it moves by more than a fiftieth, not every frame. Enough to see in a log that the
    // number is the game's and that it tracks the scene, without a line per frame.
    static float loggedExposure = -1.0f;

    if (ActiveVk().gameExposure > 1e-6f &&
        std::abs(loggedExposure - ActiveVk().gameExposure) > std::max(0.02f * ActiveVk().gameExposure, 1e-5f))
    {
        loggedExposure = ActiveVk().gameExposure;
        LOG_INFO("DLSS-NR Vulkan: the game's exposure is {}, pre-exposure {}, so white point {}",
                 ActiveVk().gameExposure, ActiveVk().gamePreExposure, ActiveVk().gamePreExposure / ActiveVk().gameExposure);
    }

    VkFrame::Failure inputFailure;
    if (!VkFrame::ValidateImage("RR.Output", colour, false, inputFailure) ||
        (!beforeSr && !VkFrame::ValidateImage("RR.Target", targetColour, true, inputFailure)) ||
        !VkFrame::ValidateImage("RR.Depth", depth, false, inputFailure) ||
        !VkFrame::ValidateImage("RR.MotionVectors", motion, false, inputFailure))
    {
        LOG_ERROR("VK-NR frame input rejected key={} reason={}", inputFailure.key, inputFailure.reason);
        Fail("invalid Vulkan input resource; see frame input log");
        return;
    }

    auto sourceRect=VkFrame::SourceRect(colour,request.contract.temporal,beforeSr,inputFailure,request.postSrOutput);
    if(!sourceRect){ActiveVk().reason="Native active color rectangle is outside its allocation";return;}
    const uint32_t width = sourceRect->width;
    const uint32_t height = sourceRect->height;
    const uint32_t guideWidth = depth->Resource.ImageViewInfo.Width;
    const uint32_t guideHeight = depth->Resource.ImageViewInfo.Height;
    if (!beforeSr && (targetColour->Resource.ImageViewInfo.Width < width ||
        targetColour->Resource.ImageViewInfo.Height < height ||
        (request.route == VkNrRoute::Present &&
         (targetColour->Resource.ImageViewInfo.Image == colour->Resource.ImageViewInfo.Image ||
          targetColour->Resource.ImageViewInfo.Format != VK_FORMAT_R16G16B16A16_SFLOAT))))
    {
        Fail("Present target is not a separate full-size private RGBA16F image");
        return;
    }

    // Preserve the existing guide subrect, but prove that it fits motion as well as depth.
    const auto motionRect = request.contract.temporal ? request.contract.temporal->motion : VkNrRect{0,0,guideWidth,guideHeight};
    if (!VkFrame::ValidateRect("RR.MotionVectors", motion, motionRect.x, motionRect.y,
        motionRect.width, motionRect.height, false, inputFailure))
    {
        LOG_ERROR("VK-NR frame input rejected key={} reason={}", inputFailure.key, inputFailure.reason);
        Fail("invalid Vulkan guide subrect; see frame input log");
        return;
    }

    if (width == 0 || height == 0)
        return;

    if (!EnsureVkProviderDeviceLocked(instance,physicalDevice,device,lock)) return;

    VkNrFrameContract workloadFrame = request.contract;
    workloadFrame.route = request.route;
    workloadFrame.deviceGeneration = VkNrCompletionDeviceGeneration(device);
    workloadFrame.routeEpoch = g_vkRuntime.IsActive(request.route) ? g_vkRuntime.Epoch() : g_vkRuntime.Epoch()+1;
    workloadFrame.queue = ActiveVk().modelQueue;
    workloadFrame.queueFamily = ActiveVk().modelQueueFamily;
    workloadFrame.resumeGeneration = runtime.resumeGeneration;
    workloadFrame.inputInterruptionEpoch = FinalFallback::CurrentInputEpoch();
    if (workloadFrame.temporal) workloadFrame.guideGeneration = workloadFrame.temporal->featureGeneration;
    workloadFrame.output = {width,height};
    if (workloadFrame.representation.format == VK_FORMAT_UNDEFINED)
        workloadFrame.representation.format = colour->Resource.ImageViewInfo.Format;
    const auto effective = request.effectiveResolutionMode ? std::optional<PresentResolution::Policy>(
        PresentResolution::Policy{*request.effectiveResolutionMode,request.effectiveResolutionScale}) : std::nullopt;
    const auto metadata = request.resolutionMetadata ? request.resolutionMetadata : request.contract.temporal;
    const auto work = request.workOverride ? PresentResolution::Size{request.workOverride->width,request.workOverride->height} : request.presentInput ? ResolveVkNrPresentWorkload(*request.presentInput,workloadFrame,
        metadata,ActiveVk().admittedWorkload,request.observedRenderSize) : ResolveVkNrWorkload(cfg,workloadFrame,
        metadata,ActiveVk().admittedWorkload,effective);
    ActiveVk().workload.requestedOutput = {width,height};
    ActiveVk().workload.requestedWork = {work.width,work.height};
    ActiveVk().workload.policy = request.presentInput ? VkNrResolutionPolicy(cfg,workloadFrame) :
        effective.value_or(VkNrResolutionPolicy(cfg,workloadFrame));
    ActiveVk().workload.pending = true;
    ActiveVk().workload.reason.clear();
    if (work.reason || !work.width || !work.height) {
        ActiveVk().tuningMessage = work.reason ? work.reason : "Vulkan NR working raster unavailable";
        ActiveVk().workload.reason = ActiveVk().tuningMessage;
        ActiveVk().reason = "Vulkan NR resolution policy is unavailable; see requested workload";
        return;
    }
    const uint32_t workWidth = work.width, workHeight = work.height;
    workloadFrame.work = {workWidth,workHeight};
    const bool reduced = workWidth != width || workHeight != height;
    const bool supersampled = workWidth > width || workHeight > height;

    if (ActiveVk().queryPool == VK_NULL_HANDLE)
    {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);

        // Timestamp support belongs to the actual queue family, not just the device period.
        uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&families,queues.data());
        const auto family=VulkanNrRecordings().UseFamily(request.use);
        ActiveVk().timestampBits=family<families?queues[family].timestampValidBits:0;
        ActiveVk().timestampPeriod = props.limits.timestampPeriod;

        if (ActiveVk().timestampPeriod > 0.0f&&ActiveVk().timestampBits>0)
        {
            VkQueryPoolCreateInfo info {};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kTimingSlots * 5;

            if (vkCreateQueryPool(device, &info, nullptr, &ActiveVk().queryPool) != VK_SUCCESS)
            {
                ActiveVk().queryPool = VK_NULL_HANDLE;
                LOG_INFO("DLSS-NR Vulkan: no timestamp pool, the pass will not report its cost");
            }
        }
    }

    if (CompositionVk() == nullptr)
    {
        ActiveVk().pass = std::make_unique<DlssNr_Vk>("Neural Rendering", device, physicalDevice,
            GetVulkanPresentRegistry().StorageWriteWithoutFormatEnabled(device));

        if (!ActiveVk().pass->IsInit())
        {
            ActiveVk().pass.reset();
            Fail("the composition pass could not be created");
            return;
        }
    }

    const auto composition = request.composition ? request.composition : CompositionVk()->Reserve(request.use,8);
    if (!composition) { ActiveVk().reason = "Vulkan composition slots await completion or recording release"; return; }
    uint32_t compositionIndex = 0;

    const unsigned int observedFlags = request.contract.temporal ? request.contract.temporal->featureFlags :
        params != nullptr ? GameCreateFlags(params) : 0;
    const unsigned int createFlags = request.route==VkNrRoute::Present ?
        (observedFlags & ~NVSDK_NGX_DLSS_Feature_Flags_IsHDR) |
            (request.gameHdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0u) : observedFlags;
    const bool gameSaysHdr = params != nullptr ? (createFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0 : request.gameHdr;
    const bool depthInverted = params != nullptr ? (createFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0 : request.depthInverted;
    const VkTuning::Settings requested { cfg.DlssNrPreset.value_or_default(), cfg.DlssNrStyle.value_or_default(),
        cfg.DlssNrIntensity.value_or_default(), cfg.DlssNrLocalStructure.value_or_default(),
        cfg.DlssNrLocalTone.value_or_default(), cfg.DlssNrSkinStructure.value_or_default(),
        cfg.DlssNrAutoMask.value_or_default() };
    if(beforeSr&&(cfg.DlssNrHoldFrame.value_or_default()||cfg.DlssNrDebugView.value_or_default())) {
        ActiveVk().reason="Native Performance analysis requires a completed post-SR frame";return;
    }
    VkNrGenerationKey key;
    key.hold=cfg.DlssNrHoldFrame.value_or_default()&&request.passIndex==0;
    key.frame = workloadFrame;
    key.frame.route = request.route;
    key.frame.deviceGeneration = VkNrCompletionDeviceGeneration(device);
    key.frame.routeEpoch = g_vkRuntime.IsActive(request.route) ? g_vkRuntime.Epoch() : g_vkRuntime.Epoch()+1;
    key.frame.output = {width,height}; key.frame.work = {workWidth,workHeight};
    key.colorFormat = colour->Resource.ImageViewInfo.Format;
    key.depthFormat = depth->Resource.ImageViewInfo.Format; key.motionFormat = motion->Resource.ImageViewInfo.Format;
    key.targetFormat = beforeSr ? colour->Resource.ImageViewInfo.Format : targetColour->Resource.ImageViewInfo.Format;
    key.gameHdr = gameSaysHdr;
    if (beforeSr) {
        NVSDK_NGX_Resource_VK* srOutput = nullptr;
        const auto quality=VkFrame::ObserveSrQuality(params,request.selectedSrQuality);
        if (params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&srOutput)) != NVSDK_NGX_Result_Success ||
            !VkFrame::ValidateImage("SR.Output",srOutput,true,inputFailure) ||
            !quality) {
            ActiveVk().reason = "Native Performance requires observed output and SR quality"; return;
        }
        key.srQuality=*quality;
        key.srOutput = {srOutput->Resource.ImageViewInfo.Width,srOutput->Resource.ImageViewInfo.Height};
        key.privateDlaa = cfg.DlssNrPreDlaa.value_or_default(); key.preSrEpoch = ActiveVk().preSrEpoch;
    }
    key.creationFlags = createFlags;
    key.passIndex = request.passIndex;
    key.tuning = requested;
    key.upFilter = key.downFilter = static_cast<uint32_t>(cfg.DlssNrScalingDownscaler.value_or_default());
    if(request.route==VkNrRoute::Present)
        CompleteVkPresentGenerationKey(key,colour->Resource.ImageViewInfo.Format,targetColour->Resource.ImageViewInfo.Format,
            depth->Resource.ImageViewInfo.Format,motion->Resource.ImageViewInfo.Format,gameSaysHdr);
    auto& state = ActiveVk();
    if (!state.generations) state.generations = std::make_unique<VkNrGenerationOwner>(VulkanNrRecordings(),
        [](const auto& desired,VkNrUseId use,uint64_t budget) { return CreateVkResources(desired,use,budget); },
        ReleaseVkResources);
    const auto others=GenerationBytesVk()-state.generations->PrivateBytes();
    if(others>request.privateAllocationBudget) {state.reason="Vulkan multipass private allocation budget exhausted";return;}
    state.generations->SetPrivateBudget(request.privateAllocationBudget-others);
    // An alias can become retired while preparing; publish it again only under the control lock.
    state.resources = nullptr;
    // Composition-only settings keep their model generation but invalidate prior measurements.
    if(state.lastGpuFrame&&!SameVkNrDiagnosticContext(*state.lastGpuFrame,key.frame)) {
        state.lastGpuTime.reset();state.lastGpuFrame.reset();
    }
    state.recordingFrame=selectedInput;state.recordingFrame.contract=key.frame;
    state.recordingFrame.color=colour;state.recordingFrame.depth=depth;state.recordingFrame.motion=motion;
    if(!SameVkNrDiagnosticContext(state.exposureContext,key.frame)){
        const auto n=++state.automaticContextChanges;
        if(request.route==VkNrRoute::Native&&request.passIndex==0&&(n<=8||n%120==0)&&state.sceneMeterLogCount<128) {
            ++state.sceneMeterLogCount;
            LOG_INFO("DLSS-NR Vulkan scene meter context: change={} settings={}->{} routeEpoch={}->{} resume={}->{} swapchain={}->{} guides={}->{} queue={}->{} output={}x{} work={}x{}",
                n,state.exposureContext.settingsRevision,key.frame.settingsRevision,state.exposureContext.routeEpoch,key.frame.routeEpoch,
                state.exposureContext.resumeGeneration,key.frame.resumeGeneration,state.exposureContext.swapchainGeneration,key.frame.swapchainGeneration,
                state.exposureContext.guideGeneration,key.frame.guideGeneration,reinterpret_cast<uintptr_t>(state.exposureContext.queue),reinterpret_cast<uintptr_t>(key.frame.queue),
                key.frame.output.width,key.frame.output.height,key.frame.work.width,key.frame.work.height);
        }
        state.exposureHold.Reset();state.gameExposure=0;state.gameExposureSequence=0;state.exposureContext=key.frame;
        state.reportedAutomaticWhitePoint=false;state.automaticMeterFrames=0;
    }
    state.automaticWhitePoint.Configure(key.frame);
    state.scan.Configure(key.frame,cfg.DlssNrScanAnchors.value_or_default(),cfg.DlssNrScanInverted.value_or_default(),cfg.DlssNrScanTrim.value_or_default());
    const auto scannedWhite=state.scan.CompletedWhitePoint(recordingOwner);
    auto diagnostic=request.route==VkNrRoute::Present?VkGenerationLossTrace("present_chain_prepare"):std::nullopt;
    const auto generation = state.generations->Prepare(key,request.use,[&](VkNrUseId old) {
        const auto remaining=request.waitBudget?request.waitBudget->RemainingNs():0;if(!remaining)return;
        lock.unlock();WaitVkNrUse(old,remaining);lock.lock();
    },preparationOnly,!preparationOnly,diagnostic?&*diagnostic:nullptr);
    if(diagnostic)TraceVkGenerationLoss(*state.generations,*diagnostic);
    TraceVkGenerationRefusal(generation,key);
    state.workload.requestedGeneration = generation.generation;
    if (!state.generations->AppliedKey()) {
        state.workload.appliedGeneration = 0;
        state.workload.appliedOutput = {}; state.workload.appliedWork = {};
    }
    const auto previous = state.generations->AppliedGeneration();
    if (generation.ready && state.generations->Commit(generation.generation)) {
        if (previous != generation.generation) {
            state.resetState.Request(); state.lastGpuTime.reset(); state.lastGpuFrame.reset();
            LOG_INFO("Vulkan NR generation applied: route={} pass={} generation={} previous={} resume={} "
                     "work={}x{} retainedGenerations={} trackedPrivateBytes={} providerPrivateBytes=unknown",
                     static_cast<unsigned>(request.route),request.passIndex,generation.generation,previous,
                     runtime.resumeGeneration,workWidth,workHeight,GenerationCountVk(),GenerationBytesVk());
        }
    } else if (preparationOnly || !state.generations->CanApply(key) ||
               !state.generations->ReadyForUse(previous,request.use) ||
               !state.generations->Retain(previous,request.use)) {
        state.tuningMessage = generation.reason;
        state.generationReason = generation.reason;
        state.reason = state.generationReason.c_str();
        state.workload.reason = generation.reason;
        state.tuningResult = generation.pending ? VkTuning::Result::Full : VkTuning::Result::Failed;
        return;
    }
    state.resources = static_cast<VkResources*>(state.generations->Payload(state.generations->AppliedGeneration()));
    if (!state.resources) return;
    state.preparedCurrent=true;
    if(preparationOnly)return;
    if(!key.frame.queue&&key.frame.route==VkNrRoute::Native) {
        // Earlier uses have both completed through owned fences and lost their
        // replayable recording references. Make those available device writes
        // visible before accessing history on the next same-family queue.
        VkMemoryBarrier visibility{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        visibility.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
        visibility.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmdBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,1,&visibility,0,nullptr,0,nullptr);
    }
    const auto& activeModel = *state.resources;
    if(request.privateChain) {
        Transition(cmdBuffer,state.resources->composed,VK_IMAGE_LAYOUT_GENERAL);
        targetColour=&state.resources->composed.ngx;
    }
    if (beforeSr) {
        auto& r = *state.resources;
        if(!request.privateChain)targetColour = &r.preSr.ngx;
        if (r.creationUse == request.use || !r.creationSeed.Ready()) {
            state.preSrDelivered = false;
            state.reason = "Native Performance model preparation awaits GPU completion"; return;
        }
        if (key.privateDlaa) {
            const auto& t = *request.contract.temporal;
            const auto write = [&](const char* name,auto value) { return VkTuning::WriteChecked(r.dlaaParams,name,value); };
            Transition(cmdBuffer,r.dlaaOutput,VK_IMAGE_LAYOUT_GENERAL);
            if (!write(NVSDK_NGX_Parameter_Color,static_cast<void*>(colour)) ||
                !write(NVSDK_NGX_Parameter_Output,static_cast<void*>(&r.dlaaOutput.ngx)) ||
                !write(NVSDK_NGX_Parameter_Depth,static_cast<void*>(depth)) ||
                !write(NVSDK_NGX_Parameter_MotionVectors,static_cast<void*>(motion)) ||
                !write(NVSDK_NGX_Parameter_Jitter_Offset_X,t.jitterX) || !write(NVSDK_NGX_Parameter_Jitter_Offset_Y,t.jitterY) ||
                !write(NVSDK_NGX_Parameter_MV_Scale_X,t.motionScaleX) || !write(NVSDK_NGX_Parameter_MV_Scale_Y,t.motionScaleY) ||
                !write(NVSDK_NGX_Parameter_Reset,static_cast<unsigned>(r.dlaaReset || t.reset)) ||
                !write(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,t.color.width) ||
                !write(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,t.color.height) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,t.color.x) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,t.color.y) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,t.depth.x) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,t.depth.y) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,t.motion.x) ||
                !write(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,t.motion.y) ||
                !write(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,state.gamePreExposure)) {
                state.reason = "private DLAA frame parameter verification failed"; return;
            }
            const bool seedReady = r.dlaaSeed.Ready();
            const auto evaluate = NVNGXProxy::VULKAN_EvaluateFeature();
            lock.unlock();
            const auto evaluated = evaluate(cmdBuffer,r.dlaaFeature,r.dlaaParams,nullptr);
            lock.lock();
            state.modelRecordedCurrent = true;
            if (evaluated != NVSDK_NGX_Result_Success) {
                r.dlaaReset = true; r.dlaaSeed.Reset(); state.preSrDelivered = false;
                state.reason = "private DLAA evaluation failed; game inputs preserved"; return;
            }
            r.dlaaReset = false;
            r.dlaaSeed.Record(request.use,true);
            Transition(cmdBuffer,r.dlaaOutput,VK_IMAGE_LAYOUT_GENERAL);
            if (!seedReady) { state.reason = "private DLAA seed awaits GPU completion"; return; }
            colour = &r.dlaaOutput.ngx;
        }
        Transition(cmdBuffer,r.preSr,VK_IMAGE_LAYOUT_GENERAL);
    }
    state.requestedTuning = requested;
    state.tuningResult = generation.ready ? VkTuning::Result::Applied : VkTuning::Result::Failed;
    state.tuningMessage = generation.ready ? "Applied" : generation.reason + "; keeping compatible applied settings";
    const auto appliedKey = state.generations->AppliedKey();
    state.workload.appliedOutput = appliedKey->frame.output; state.workload.appliedWork = appliedKey->frame.work;
    state.workload.appliedGeneration = state.generations->AppliedGeneration();
    state.workload.pending = !generation.ready;
    if (!generation.ready) state.workload.reason = state.tuningMessage;
    if (generation.ready) state.admittedWorkload = VkNrWorkloadAdmission{key.frame,state.workload.policy,work};
    const bool meterReady = state.resources->meter.Valid() && CreateMeterReadback();
    if (!meterReady) LOG_WARN("DLSS-NR Vulkan: no exposure meter; the white point stays on the slider");

    // -----------------------------------------------------------------------------------------
    // Encode: the frame the upscaler wrote -> a display-referred proxy, plus an untouched copy
    // -----------------------------------------------------------------------------------------

    // The game asking the upscaler to forget its history -- a cut, a teleport, a load. Same omission
    // as the D3D12 path had: the model's history was only ever reset by things that happened to us,
    // never by anything that happened in the game.
    {
        unsigned int gameReset = 0;

        if ((params != nullptr && params->Get(NVSDK_NGX_Parameter_Reset, &gameReset) == NVSDK_NGX_Result_Success &&
            gameReset != 0) || request.gameReset)
        {
            ++ActiveVk().diagnosticResetEpoch;ActiveVk().resetState.MarkPending();

            static unsigned long long resets = 0;
            ++resets;

            if (resets <= 3 || resets % 100 == 0)
                LOG_INFO("DLSS-NR Vulkan: the game asked for a history reset ({} so far)", resets);
        }
    }

    // Both have to agree. A game can set the HDR flag on a buffer that cannot hold open-ended light,
    // and encoding an already tone-mapped frame a second time looks washed out and banded.
    const bool linearHdr = gameSaysHdr && FormatCanHoldLinearHdr(colour->Resource.ImageViewInfo.Format);

    // The same rule as the D3D12 path, deliberately spelled the same way: the game divides its frame
    // by preExposure and multiplies by exposure, so undoing that is the divisor this pass wants, and
    // the slider becomes a trim on top rather than the answer.
    //
    // The trim is bounded here, at the point of use, rather than at the slider. Someone who found 64
    // by hand on the manual path and then switches the exposure source on keeps that 64 in their ini;
    // bounding it in the menu would leave the picture wrong for a reason the menu no longer showed.
    // Their value stays in the config untouched, so switching back to manual restores it.
    const auto presentRecipe=SelectVkPresentColorRecipe(request.contract.representation.format,request.contract.representation.colorSpace);
    const auto selectedPresentColor=presentRecipe.Operation();
    float whitePoint = request.route==VkNrRoute::Present ? selectedPresentColor.modelWhitePoint : cfg.DlssNrWhitePointScale.value_or_default();

    const auto automaticWhitePoint=PassStateVk(request.route,0).automaticWhitePoint.Value();
    if(request.route!=VkNrRoute::Present)whitePoint=ResolveVkNrWhitePoint(cfg.DlssNrWhitePointSource.value_or_default(),whitePoint,
        state.gameExposure,state.gamePreExposure,cfg.DlssNrWhitePointTrim.value_or_default(),scannedWhite,state.exposureHold,
            linearHdr&&request.route==VkNrRoute::Native?automaticWhitePoint:std::nullopt);
    if(state.reportedAutomaticReadbacks!=state.automaticReadbacks) {
        state.reportedAutomaticReadbacks=state.automaticReadbacks;
        const auto n=state.automaticReadbacks;
        if((n<=8||n%120==0)&&state.sceneMeterLogCount<128) {
            ++state.sceneMeterLogCount;
            LOG_INFO("DLSS-NR Vulkan scene meter resolution: completed={} available={} rawDerived={} manual={} trim={} resolved={} heldGameExposure={}",
                n,automaticWhitePoint.has_value(),automaticWhitePoint.value_or(0),cfg.DlssNrWhitePointScale.value_or_default(),
                cfg.DlssNrWhitePointTrim.value_or_default(),whitePoint,state.exposureHold.HasValue());
        }
    }
    const bool derivedWhitePoint=!key.hold&&whitePoint>cfg.DlssNrWhitePointScale.value_or_default()&&linearHdr&&request.route==VkNrRoute::Native&&
        cfg.DlssNrWhitePointSource.value_or_default()==1&&!state.exposureHold.HasValue()&&automaticWhitePoint.has_value();
    if(derivedWhitePoint&&!state.reportedAutomaticWhitePoint) {
        state.reportedAutomaticWhitePoint=true;
        state.resetState.Request();
        LOG_INFO("DLSS-NR Vulkan normalization: origin=completed-scene-grid whitePoint={} gameExposureAvailable=false; derived normalization is not game exposure",whitePoint);
    }
    auto temporal=request.contract.temporal;
    if(key.hold) {
        const auto held=state.resources->held.Select(key.frame);
        if(!held){state.reason="Hold generation no longer matches the route/raster/recipe";return;}
        colour=held->color;depth=held->depth;motion=held->motion;temporal=held->contract.temporal;
        sourceRect=VkFrame::SourceRect(colour,temporal,beforeSr,inputFailure,held->postSrOutput);
        if(!sourceRect||sourceRect->width!=width||sourceRect->height!=height){
            state.reason="Held source rectangle no longer matches its private raster";return;
        }
        if(state.resources->held.WhitePoint()>0)whitePoint=state.resources->held.WhitePoint();
        else state.resources->held.LockWhitePoint(whitePoint);
    }
    VkNrSignalPayload signals;
    signals.context={device,key.frame,state.generations->AppliedGeneration(),state.resetState.Capture(),0};
    signals.context.resetEpoch=state.diagnosticResetEpoch;
    signals.context.policyRevision=Connections::SignalPreferences().revision;
    signals.use=request.use;signals.sourceEvaluation=request.contract.evaluation;
    signals.time=GetTickCount64();signals.held=key.hold;
    const auto signalStage=beforeSr?SignalReceipts::Stage::PreSr:
        request.contract.placement==VkNrPlacement::AfterRR?SignalReceipts::Stage::PostRr:SignalReceipts::Stage::PostSr;
    const auto signalDomain=linearHdr?SignalReceipts::Domain::LinearHdr:SignalReceipts::Domain::EncodedDisplay;
    const auto fact=[&](NVSDK_NGX_Resource_VK* resource,VkNrRect rect,const char* reason){
        SignalReceipts::Fact f;f.present=resource!=nullptr;f.admitted=f.present;
        f.width=rect.width;f.height=rect.height;f.originX=rect.x;f.originY=rect.y;
        f.format=resource?uint32_t(resource->Resource.ImageViewInfo.Format):0;
        f.stage=signalStage;f.domain=signalDomain;f.reason=reason;return f;};
    const auto publishSignals=[&] {
        if(request.route!=VkNrRoute::Native||request.passIndex)return;
        // Diagnostics never change a successful rendering result on allocation failure.
        try {state.signalDiagnostics.Store(signals,VulkanNrRecordings());} catch(...) {}
    };
    const auto encodingGeneration=state.generations->AppliedGeneration();
    if (state.reportedEncodingGeneration!=encodingGeneration)
    {
        state.reportedEncodingGeneration=encodingGeneration;
        LOG_INFO("DLSS-NR Vulkan: route={} pass={} generation={} buffer={} (flag {}, format {}), depth {} "
                 "applyModel={} style={} intensity={} whitePoint={}",
                 static_cast<unsigned>(request.route),request.passIndex,encodingGeneration,
                 linearHdr ? "linear HDR" : "already tone-mapped", gameSaysHdr ? "set" : "clear",
                 (int) colour->Resource.ImageViewInfo.Format, depthInverted ? "inverted" : "normal",
                 cfg.DlssNrApplyModel.value_or_default(),activeModel.settings.style,activeModel.settings.intensity,whitePoint);
        LOG_INFO("DLSS-NR Vulkan exposure: route={} pass={} generation={} source={} textureOffered={} "
                 "textureQualified={} preExposure={} completedExposure={} validatedWhitePoint={} resolvedWhitePoint={}",
                 static_cast<unsigned>(request.route),request.passIndex,encodingGeneration,
                 cfg.DlssNrWhitePointSource.value_or_default(),exposure!=nullptr,exposureQualified,
                 havePre?std::to_string(preExposure):std::string("not supplied"),state.gameExposure,
                 state.exposureHold.HasValue()?std::to_string(state.exposureHold.Value()):std::string("unavailable"),whitePoint);
    }

    const Neurotic::Context::ModelColorOperation modelColorOperation {
        linearHdr ? 0u : 1u, whitePoint,
        request.route==VkNrRoute::Present&&selectedPresentColor.signedOriginal ? 1u : 0u,
        0u, 0.0f, cfg.DlssNrReversibleMode.value_or_default()};
    DlssNrConstants encode {};
    encode.Mode = DlssNrMode_Encode;
    Neurotic::Context::ApplyModelColor(modelColorOperation, encode);
    encode.Width = width;
    encode.Height = height;
    if(!key.privateDlaa){encode.SourceX=sourceRect->x;encode.SourceY=sourceRect->y;}
    encode.ApplyModel = cfg.DlssNrApplyModel.value_or_default() ? 1u : 0u;
    encode.TransferStrength = cfg.DlssNrTransferStrength.value_or_default();
    encode.ColourStrength = cfg.DlssNrColourStrength.value_or_default();
    encode.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();
    encode.Transfer = cfg.DlssNrTransfer.value_or_default();
    encode.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;

    const VkImageSubresourceRange colourRange = colour->Resource.ImageViewInfo.SubresourceRange;

    std::optional<VkNrReservation> timing;
    if (ActiveVk().queryPool) timing = ActiveVk().querySlots.Reserve(request.use,1);
    const uint32_t timingSlot = timing ? timing->slots.front() : 0;
    if (timing) {
        ActiveVk().queryFrames[timingSlot]=key.frame;
        ActiveVk().queryContexts[timingSlot]={device,key.frame,state.generations->AppliedGeneration(),
            state.resetState.Capture(),uint32_t(cfg.DlssNrRoute.value_or_default())};
        ActiveVk().queryContexts[timingSlot].resetEpoch=state.diagnosticResetEpoch;
        ActiveVk().queryTimes[timingSlot]=GetTickCount64();
        ActiveVk().queryClocks[timingSlot]=VkNrDiagnosticMilliseconds();
        ActiveVk().queryUses[timingSlot] = request.use; ActiveVk().queryPublished[timingSlot] = false;
        vkCmdResetQueryPool(cmdBuffer,ActiveVk().queryPool,timingSlot*5,5);
        vkCmdWriteTimestamp(cmdBuffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,ActiveVk().queryPool,timingSlot*5);
    }

    // Post-SR reads the upscaler's storage output in GENERAL. BeforeSR samples the
    // original NGX input in its qualified read layout and never changes that image.
    Transition(cmdBuffer, ActiveVk().resources->proxy, VK_IMAGE_LAYOUT_GENERAL);
    Transition(cmdBuffer, ActiveVk().resources->keep, VK_IMAGE_LAYOUT_GENERAL);

    // Read in GENERAL, which is the layout it is actually in.
    //
    // This slot used to take the default and declare SHADER_READ_ONLY_OPTIMAL, which disagreed with
    // the comment four lines up and with the resolve below -- the resolve writes this same image as a
    // storage image, which is only legal in GENERAL, and nothing transitions it in between. It is the
    // upscaler's output, a storage image the upscaler has just written, so GENERAL is what it is.
    // Inert on the only hardware this model runs on, wrong everywhere it is read.
    if (!CompositionVk()->Dispatch(cmdBuffer, request.use, composition->slots[compositionIndex++], encode, width, height, colour->Resource.ImageViewInfo.ImageView,
                             VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, ActiveVk().resources->proxy.view, ActiveVk().resources->keep.view,
                             beforeSr && key.privateDlaa ? VK_IMAGE_LAYOUT_GENERAL : request.colorLayout))
    {
        Fail("the encode dispatch failed");
        return;
    }

    signals.facts[size_t(SignalReceipts::Signal::Color)]=fact(colour,{encode.SourceX,encode.SourceY,width,height},key.hold?
        "Held color actually read by encode":"Color actually read by encode");
    signals.recorded|=SignalReceipts::Bit(SignalReceipts::Signal::Color);publishSignals();
    if(timing)vkCmdWriteTimestamp(cmdBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,ActiveVk().queryPool,timingSlot*5+1);

    // The model's input: the full proxy, or a downsampled copy of it when the working scale is below
    // the frame. Mirrors the D3D12 path -- the encode always writes a full proxy, and a separate
    // downsample makes the small one the model actually reads.
    OwnedImage* modelInput = &ActiveVk().resources->proxy;

    if (reduced && ActiveVk().resources->proxySmall.Valid())
    {
        bool built = false;

        if (supersampled)
        {
            // Supersample: upscale the proxy to the super-native working size with the chosen filter so
            // the model sees a clean input. Rebuild both scalers when the NR downscaler changed (baked
            // at construction). proxy -> SHADER_READ_ONLY (sampled), proxySmall -> GENERAL (storage).
            Transition(cmdBuffer, ActiveVk().resources->proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, ActiveVk().resources->proxySmall, VK_IMAGE_LAYOUT_GENERAL);

            VkImageInfo upin = ImageInfoOf(ActiveVk().resources->proxy);
            VkImageInfo upout = ImageInfoOf(ActiveVk().resources->proxySmall);

            if (ActiveVk().resources->superUp && ActiveVk().resources->superUp->IsInit() && ActiveVk().resources->superUp->Dispatch(cmdBuffer, upin, upout))
                built = true;
            else
            {
                static bool warnedVkSuper = false;
                if (!warnedVkSuper)
                {
                    warnedVkSuper = true;
                    LOG_WARN("DLSS-NR Vulkan supersample: upscaler unavailable, falling back to box enlarge.");
                }
            }
        }

        if (!built)
        {
            DlssNrConstants down = encode;
            down.Mode = DlssNrMode_Downsample;
            down.Width = workWidth;
            down.Height = workHeight;

            Transition(cmdBuffer, ActiveVk().resources->proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, ActiveVk().resources->proxySmall, VK_IMAGE_LAYOUT_GENERAL);

            if (!CompositionVk()->Dispatch(cmdBuffer, request.use, composition->slots[compositionIndex++], down, workWidth, workHeight, ActiveVk().resources->proxy.view, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, VK_NULL_HANDLE, ActiveVk().resources->proxySmall.view, VK_NULL_HANDLE,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
            {
                Fail("the downsample dispatch failed");
                return;
            }
        }

        modelInput = &ActiveVk().resources->proxySmall;
    }

    // -----------------------------------------------------------------------------------------
    // The meter: the game's 1x1 exposure -> texel (0,0) of the grid -> a buffer the CPU can read
    // -----------------------------------------------------------------------------------------

    const bool scanRequested=cfg.DlssNrWhitePointSource.value_or_default()==2||cfg.DlssNrScanExposure.value_or_default()||cfg.DlssNrScanMeter.value_or_default();
    const bool automaticMeter=request.route==VkNrRoute::Native&&request.passIndex==0&&linearHdr&&
        cfg.DlssNrWhitePointSource.value_or_default()==1&&!exposureQualified&&!state.exposureHold.HasValue();
    // Collect the initial stable sample window promptly, then meter sparsely.
    const auto automaticAttempt=automaticMeter?++state.automaticMeterFrames:0;
    const auto totalAutomaticAttempt=automaticMeter?++state.automaticTotalAttempts:0;
    const bool reportAutomaticAttempt=automaticMeter&&(totalAutomaticAttempt<=8||totalAutomaticAttempt%960==0);
    const bool automaticGrid=automaticMeter&&(automaticAttempt<=8||automaticAttempt%8==0);
    const bool gridRequested=cfg.DlssNrScanMeter.value_or_default()||automaticGrid;
    if(!key.hold&&ActiveVk().resources->meter.Valid()&&((automaticGrid||scanRequested&&gridRequested)||exposureQualified&&
        (scanRequested||cfg.DlssNrWhitePointSource.value_or_default()==1)))
    {
        if (!ActiveVk().meterSlots)
            ActiveVk().meterSlots = std::make_unique<VkNrReservationPool>(recordingOwner,[](uint32_t capacity){return CreateMeterReadback(capacity);});
        const auto meterReservation = ActiveVk().meterSlots->Reserve(request.use,1);
        const uint32_t slot = meterReservation ? meterReservation->slots.front() : 0;

        if (meterReservation && ActiveVk().meterReadback[slot] != VK_NULL_HANDLE)
        {
            Transition(cmdBuffer,ActiveVk().resources->keep,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            DlssNrConstants meter = encode;
            meter.Mode = gridRequested?DlssNrMode_Calibrate:DlssNrMode_Meter;
            meter.Width = kMeterSide;
            meter.Height = kMeterSide;

            Transition(cmdBuffer, ActiveVk().resources->meter, VK_IMAGE_LAYOUT_GENERAL);

            if (CompositionVk()->Dispatch(cmdBuffer, request.use, composition->slots[compositionIndex++], meter, kMeterSide, kMeterSide, ActiveVk().resources->keep.view, VK_NULL_HANDLE,
                                    VK_NULL_HANDLE, gridRequested?VK_NULL_HANDLE:exposure->Resource.ImageViewInfo.ImageView, ActiveVk().resources->meter.view,
                                    VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    gridRequested?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:exposureLayout))
            {
                if(!gridRequested) {
                    signals.facts[size_t(SignalReceipts::Signal::Exposure)]=fact(exposure,{0,0,1,1},"Exposure texture read by metering dispatch; not a model binding");
                    signals.facts[size_t(SignalReceipts::Signal::Exposure)].domain=SignalReceipts::Domain::Unknown;
                    signals.recorded|=SignalReceipts::Bit(SignalReceipts::Signal::Exposure);publishSignals();
                }
                Transition(cmdBuffer, ActiveVk().resources->meter, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                VkBufferImageCopy region {};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageOffset = { 0, 0, 0 };
                region.imageExtent = { kMeterSide, kMeterSide, 1 };

                vkCmdCopyImageToBuffer(cmdBuffer, ActiveVk().resources->meter.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       ActiveVk().meterReadback[slot], 1, &region);

                // The copy has to be visible to a host read, and only the host will read it.
                VkBufferMemoryBarrier toHost {};
                toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toHost.buffer = ActiveVk().meterReadback[slot];
                toHost.offset = 0;
                toHost.size = kMeterBytes;

                vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                     nullptr, 1, &toHost, 0, nullptr);

                ActiveVk().meterUses[slot] = request.use; ActiveVk().meterPublished[slot] = false;
                ActiveVk().meterScan[slot]=scanRequested;ActiveVk().meterGrid[slot]=gridRequested;
                ActiveVk().meterAutomatic[slot]=automaticGrid;ActiveVk().meterContexts[slot]=key.frame;
                if(scanRequested){auto scanFrame=request;scanFrame.contract=key.frame;ActiveVk().scan.Record(scanFrame,request.use);}
                ActiveVk().meterFrames++;
                if(automaticGrid&&reportAutomaticAttempt&&state.sceneMeterLogCount<128) {
                    ++state.sceneMeterLogCount;
                    LOG_INFO("DLSS-NR Vulkan scene meter recorded: attempt={} use={} slot={} completed={} grid={}x{} source={}x{} settings={} routeEpoch={} resume={}",
                        automaticAttempt,request.use.value,slot,state.automaticReadbacks,kMeterSide,kMeterSide,width,height,
                        key.frame.settingsRevision,key.frame.routeEpoch,key.frame.resumeGeneration);
                }
            }
            else if(automaticGrid&&reportAutomaticAttempt&&state.sceneMeterLogCount<128) {
                ++state.sceneMeterLogCount;
                LOG_INFO("DLSS-NR Vulkan scene meter refused: attempt={} reason=dispatch-failed use={}",automaticAttempt,request.use.value);
            }
        }
        else if(automaticGrid&&reportAutomaticAttempt&&state.sceneMeterLogCount<128) {
            ++state.sceneMeterLogCount;
            LOG_INFO("DLSS-NR Vulkan scene meter refused: attempt={} reason=readback-reservation use={} completed={}",automaticAttempt,request.use.value,state.automaticReadbacks);
        }
    }

    // -----------------------------------------------------------------------------------------
    // The model
    // -----------------------------------------------------------------------------------------

    Transition(cmdBuffer, ActiveVk().resources->output, VK_IMAGE_LAYOUT_GENERAL);

    // Each model owns this block. Pointer/scalar readbacks gate the vendor call.
    auto* modelParams = ActiveVk().resources->capabilityParams;
    const VkFrame::Inputs frame { &modelInput->ngx, depth, motion, &ActiveVk().resources->output.ngx,
        workWidth, workHeight, guideWidth, guideHeight, depthInverted, ActiveVk().resetState.Pending(),
        temporal ? std::optional<VkNrTemporalMetadata>(
            ScaleVkNrTemporal(*temporal, {width,height}, {workWidth,workHeight})) : std::nullopt };
    int evaluated = 0;
    const auto resetRevision = ActiveVk().resetState.Capture();
    const auto evaluationDevice = ActiveVk().device;
    const auto evaluationFeature = ActiveVk().resources->feature;
    const auto evaluate = g_vkRuntime.Exports().evaluate;
    VkFrame::Failure failure;
    if(timing)vkCmdWriteTimestamp(cmdBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,ActiveVk().queryPool,timingSlot*5+2);
    const bool prepared = VkFrame::PrepareAndEvaluate(modelParams, frame, activeModel.settings, failure, [&] {
        lock.unlock();
        VkFlight::Current().Write(VkFlight::Kind::ModelEnter,request.passIndex,request.contract.evaluation.invocation,
            reinterpret_cast<uintptr_t>(cmdBuffer),request.use.value,0,resetRevision);
        evaluated = evaluate((void*)cmdBuffer,evaluationFeature,modelParams);
        VkFlight::Current().Write(VkFlight::Kind::ModelReturn,request.passIndex,request.contract.evaluation.invocation,
            reinterpret_cast<uintptr_t>(cmdBuffer),request.use.value,0,resetRevision,evaluated,evaluated!=1);
        lock.lock();
    });
    if (!prepared)
    {
        LOG_ERROR("VK-NR frame parameter rejected key={} reason={} readbackAttempted={} getResult=0x{:X}; model not called",
                  failure.key, failure.reason, failure.readbackAttempted, failure.result);
        Fail("model frame parameter verification failed");
        return;
    }

    if (evaluated != 1)
    {
        LOG_ERROR("DLSS-NR Vulkan: evaluate returned {}", evaluated);
        Fail("the model refused to evaluate");
        return;
    }
    if (ActiveVk().device != evaluationDevice || ActiveVk().resources->feature != evaluationFeature) {
        Fail("model generation changed during evaluation"); return;
    }

    if(timing)vkCmdWriteTimestamp(cmdBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,ActiveVk().queryPool,timingSlot*5+3);
    // These are the validated bindings after held substitution and model-space scaling.
    const auto bindings=frame.Bindings();
    const SignalReceipts::Signal names[]={SignalReceipts::Signal::Color,SignalReceipts::Signal::Depth,
        SignalReceipts::Signal::Motion,SignalReceipts::Signal::Output};
    for(size_t i=0;i<bindings.size();++i){const auto& b=bindings[i];
        signals.facts[size_t(names[i])]=fact(b.resource,{b.x,b.y,b.width,b.height},key.hold?
            "Actual model binding from held inputs":"Actual verified model binding");
        signals.facts[size_t(names[i])].domain=(i==0||i==3)?
            SignalReceipts::Domain::EncodedDisplay:SignalReceipts::Domain::Unknown;
        signals.recorded|=SignalReceipts::Bit(names[i]);}
    auto& mv=signals.facts[size_t(SignalReceipts::Signal::Motion)];mv.valuesKnown=true;
    mv.x=frame.temporal?frame.temporal->motionScaleX:1.f;mv.y=frame.temporal?frame.temporal->motionScaleY:1.f;
    auto& jitter=signals.facts[size_t(SignalReceipts::Signal::Jitter)];
    jitter=fact(nullptr,{},"No temporal jitter binding supplied");jitter.domain=SignalReceipts::Domain::Unknown;
    if(frame.temporal){jitter.present=jitter.admitted=jitter.valuesKnown=true;
        jitter.x=frame.temporal->jitterX;jitter.y=frame.temporal->jitterY;
        jitter.reason=key.hold?"Actual scaled held jitter":"Actual scaled model jitter";
        signals.recorded|=SignalReceipts::Bit(SignalReceipts::Signal::Jitter);}
    auto& evaluation=signals.facts[size_t(SignalReceipts::Signal::Evaluation)];
    evaluation=fact(frame.output,{0,0,frame.width,frame.height},"Verified model evaluation recorded; GPU completion still required");
    evaluation.domain=SignalReceipts::Domain::EncodedDisplay;
    signals.recorded|=SignalReceipts::Bit(SignalReceipts::Signal::Evaluation);
    signals.facts[size_t(SignalReceipts::Signal::Delivery)]=fact(frame.output,{0,0,width,height},"Final selected delivery has not completed");
    signals.facts[size_t(SignalReceipts::Signal::PreExposure)].reason="Not a model binding; normalization use is not separately instrumented";
    signals.facts[size_t(SignalReceipts::Signal::History)].reason="Model internal history is not externally observable";
    publishSignals();
    ActiveVk().modelRecordedCurrent = true;
    ActiveVk().resetState.Acknowledge(resetRevision);

    // -----------------------------------------------------------------------------------------
    // Resolve: proxy + the model's answer + the untouched copy -> the frame
    // -----------------------------------------------------------------------------------------

    DlssNrConstants resolve = encode;
    resolve.Mode = DlssNrMode_Resolve;
    resolve.FinalLuminanceGuard = beforeSr ? 0u : 1u;
    // The exact image view supplied to the resolve below determines storage
    // range. This does not admit formats or infer range from a saved route.
    resolve.OutputIsUnorm = 0;
    switch (targetColour->Resource.ImageViewInfo.Format)
    {
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_R16G16B16_UNORM:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        resolve.OutputIsUnorm = 1;
        break;
    default:
        break; // Do not conflate float, SNORM, sRGB or integer image views.
    }

    // Supersampling down-leg (Vulkan). Average the Nx model answer back to native with the chosen
    // filter so the resolve composites a native answer against the native proxy 1:1 -- not the single
    // bilinear tap the Nx answer would otherwise get, which aliases the model's detail into noise. On
    // failure it falls back to the Nx pair (modelInput + output), the old behaviour.
    OwnedImage* resolveProxy = modelInput;
    OwnedImage* resolveAnswer = &ActiveVk().resources->output;

    if (supersampled && ActiveVk().resources->superDown && ActiveVk().resources->superDown->IsInit() && ActiveVk().resources->outputNative.Valid())
    {
        Transition(cmdBuffer, ActiveVk().resources->output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmdBuffer, ActiveVk().resources->outputNative, VK_IMAGE_LAYOUT_GENERAL);

        VkImageInfo dsin = ImageInfoOf(ActiveVk().resources->output);
        VkImageInfo dsout = ImageInfoOf(ActiveVk().resources->outputNative);

        if (ActiveVk().resources->superDown->Dispatch(cmdBuffer, dsin, dsout))
        {
            resolveProxy = &ActiveVk().resources->proxy;
            resolveAnswer = &ActiveVk().resources->outputNative;
        }
    }

    Transition(cmdBuffer, *resolveProxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, *resolveAnswer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, ActiveVk().resources->keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!CompositionVk()->Dispatch(cmdBuffer, request.use, composition->slots[compositionIndex++], resolve, width, height, resolveProxy->view, resolveAnswer->view,
                             ActiveVk().resources->keep.view, VK_NULL_HANDLE, targetColour->Resource.ImageViewInfo.ImageView,
                             VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        Fail("the resolve dispatch failed");
        return;
    }

    ActiveVk().diagnosticConstants=resolve;ActiveVk().diagnosticProxy=resolveProxy->view;ActiveVk().diagnosticAnswer=resolveAnswer->view;
    ActiveVk().outputWriteRecordedCurrent = true;
    if (beforeSr && !request.privateChain) {
        auto& r = *ActiveVk().resources;
        r.nrSeed.Record(request.use,true);
        if (key.privateDlaa) {
            Transition(cmdBuffer,r.preSr,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer,r.reJitter,VK_IMAGE_LAYOUT_GENERAL);
            DlssNrConstants jitter = resolve; jitter.Mode = 5;
            jitter.MvScaleX = request.contract.temporal->jitterX;
            jitter.MvScaleY = request.contract.temporal->jitterY;
            if (!CompositionVk()->Dispatch(cmdBuffer,request.use,composition->slots[compositionIndex++],jitter,width,height,
                r.preSr.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,r.reJitter.view,VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
                ActiveVk().outputWriteRecordedCurrent = false; r.nrSeed.Reset();
                ActiveVk().reason = "private DLAA re-jitter dispatch failed"; return;
            }
            Transition(cmdBuffer,r.reJitter,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else Transition(cmdBuffer,r.preSr,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else if(!request.privateChain) { ActiveVk().preSrDelivered = false; ++ActiveVk().frames; }
    if(request.privateChain) {
        Transition(cmdBuffer,ActiveVk().resources->composed,VK_IMAGE_LAYOUT_GENERAL);
        ActiveVk().resources->nrSeed.Record(request.use,true);
    }

    if (timing) {
        vkCmdWriteTimestamp(cmdBuffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,ActiveVk().queryPool,timingSlot*5+4);
        ++ActiveVk().timedFrames;
    }

    static bool reported = false;

    if (!reported && ActiveVk().frames > 2)
    {
        reported = true;
        LOG_INFO("DLSS-NR Vulkan: running natively at {}x{}, guides {}x{}", width, height, guideWidth, guideHeight);
    }
}

static bool PrepareVkPassDeviceLocked(VkDevice device)
{
    if(g_vkSessionClosed||g_vkShutdownFailed||device==VK_NULL_HANDLE)return false;
    const auto ownership=ClassifyVkRouteDevice(ActiveVk().device,g_vkRuntime.Device(),device);
    if(ownership==VkRouteDeviceUse::Conflict){g_vkShutdownFailed=true;return false;}
    if(ownership==VkRouteDeviceUse::FirstUse)ActiveVk().device=device;
    return true;
}

// A real-frame recording can move when the game enables FG/async compute. Model/image
// generations already include queue/family identity, but the shared composer,
// readback buffers and timestamp interpretation must also leave the old family.
static bool PrepareVkQueueTransitionLocked(VkNrRoute route,const VkNrQueueContext& next)
{
    auto& root=PassStateVk(route,0);
    const bool changed=(root.modelQueueFamily!=UINT32_MAX&&root.modelQueueFamily!=next.family)||
        (root.modelQueue&&next.queue&&root.modelQueue!=next.queue);
    if(!changed)return true;
    for(uint32_t pass=0;pass<10;++pass) {
        auto& state=PassStateVk(route,pass);
        state.signalDiagnostics.Clear();state.timing.Clear();
        state.resources=nullptr;ClearFinalSource(state);state.candidate.reset();state.performanceSource.reset();
        state.performanceUse={};state.performanceEvaluation={};state.performanceSerial=0;
        state.preSrDelivered=false;state.nativeStatus.accepted=false;
    }
    PollVkNrCompletions();
    bool drained=true;
    for(uint32_t pass=0;pass<10;++pass) {
        auto& state=PassStateVk(route,pass);
        // Evaluate every owner, even if a prior one is pending. These calls retire
        // only reusable uses; no device idle, guessed completion or driver release.
        if(state.pass&&!state.pass->DrainReleased())drained=false;
        if(!state.querySlots.DrainReleased())drained=false;
        if(state.meterSlots&&!state.meterSlots->DrainReleased())drained=false;
    }
    if(!drained)return false;
    for(uint32_t pass=0;pass<10;++pass) {
        ScopedVkSession active(route,pass);auto& state=ActiveVk();
        ClearFinalSource(state);state.candidate.reset();state.performanceSource.reset();
        state.performanceUse={};state.performanceEvaluation={};state.performanceSerial=0;
        state.scan.Reset();state.exposureHold.Reset();state.automaticWhitePoint.Reset();
        state.exposureContext={};state.gameExposure=0;state.gameExposureSequence=0;state.gamePreExposure=1;
        state.reportedAutomaticWhitePoint=false;state.automaticMeterFrames=0;
        state.meterSlots.reset();DestroyMeterReadback();state.meterUses.fill({});state.meterPublished.fill(true);
        state.pass.reset();state.borrowedPass=nullptr;
        if(state.queryPool)vkDestroyQueryPool(state.device,state.queryPool,nullptr);
        state.queryPool=VK_NULL_HANDLE;state.timestampBits=0;state.timestampPeriod=0;state.lastQueryUse=0;
        state.queryUses.fill({});state.queryPublished.fill(true);state.queryFrames.fill({});
        state.lastGpuTime.reset();state.lastGpuFrame.reset();state.timedFrames=0;
        state.modelQueue=next.queue;state.modelQueueFamily=next.family;
        state.admittedWorkload.reset();state.preSrDelivered=false;state.nativeStatus.accepted=false;
        state.chain={};state.resetState.Request();
        // Keep all model/image generations. Prepare selects a distinct queue/family
        // key and retires old payloads through its existing vendor/lifetime owner.
    }
    static uint32_t reports=0;
    if(reports<64){++reports;LOG_INFO("Vulkan model queue transition: route={} auxiliary owners drained; family={} queueKnown={}; retained model generations remain owned",static_cast<unsigned>(route),next.family,next.queue!=VK_NULL_HANDLE);}
    return true;
}

static VkRecordResult EvaluateVkChain(const VkFrameRequest& request,NVSDK_NGX_Parameter* params,
                                      const NrConfigSnapshot<Config>& cfg)
{
    PollVulkanNrCaptures();VulkanNrFgContributions().ObserveSubmissions();
    GetVulkanPresentStatus().ObserveFgOwnership(VulkanNrFgContributions().AcceptedContributions(),VulkanNrFgContributions().ConsumerCount());
    VkFrameRequest base=request;
    base.captureOnly|=cfg.IsPrivateDiagnostic();
    if(base.contract.temporal)base.gameReset=base.contract.temporal->reset;
    const bool before=base.contract.placement==VkNrPlacement::BeforeSR;
    if(params) {
        if(before) {
            std::string reason;
            if(!SelectVkNrPreSrColor(base,params,VulkanNrImageFacts(),VulkanNrRecordings(),
                VkNrCompletionDeviceGeneration(base.device),true,reason))return {VkRecordStatus::NoWork,reason};
        } else params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&base.color));
        params->Get(NVSDK_NGX_Parameter_Depth,reinterpret_cast<void**>(&base.depth));
        params->Get(NVSDK_NGX_Parameter_MotionVectors,reinterpret_cast<void**>(&base.motion));
        params->Get(NVSDK_NGX_Parameter_ExposureTexture,reinterpret_cast<void**>(&base.exposure));
        params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&base.preExposure);
        base.targetColor=before?nullptr:base.color;
        const auto flags=GameCreateFlags(params);
        base.gameHdr=(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)!=0;
        base.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;
    }
    if(const auto reason=VkNrAdaptedDlaaRefusal(before,base.qualifiedColor&&base.qualifiedColor->Adapted(),cfg.DlssNrPreDlaa.value_or_default()))
        return {VkRecordStatus::NoWork,reason};
    VkFrame::Failure failure;
    if(!VkFrame::ValidateImage("Chain.Color",base.color,false,failure)||!base.depth||!base.motion)
        return {VkRecordStatus::NoWork,"Vulkan chain input resources unavailable"};
    const auto& color=base.color->Resource.ImageViewInfo;
    const auto activeRect=VkFrame::SourceRect(base.color,base.contract.temporal,before,failure,base.postSrOutput);
    if(!activeRect)return {VkRecordStatus::NoWork,failure.reason};
    base.contract.output={activeRect->width,activeRect->height};base.width=activeRect->width;base.height=activeRect->height;
    base.contract.route=base.route;
    std::optional<VkNrReservation> reservation;
    VkNrPassPlan plan;
    {
        std::unique_lock lock(g_vkMutex);ScopedVkSession active(base.route);
        auto& root=ActiveVk();root.candidate.reset();
        // Own even a partially initialized pass, and refuse allocation after closure.
        if(!PrepareVkPassDeviceLocked(base.device))
            return {VkRecordStatus::NoWork,"Vulkan chain session closed or device changed; restart required"};
        base.contract.deviceGeneration=VkNrCompletionDeviceGeneration(base.device);
        base.contract.routeEpoch=g_vkRuntime.IsActive(base.route)?g_vkRuntime.Epoch():g_vkRuntime.Epoch()+1;
        base.contract.resumeGeneration=cfg.GetDlssNrRuntimeSnapshot().resumeGeneration;
        base.contract.inputInterruptionEpoch=FinalFallback::CurrentInputEpoch();
        base.contract.settingsRevision=cfg.ObservationRevision().value_or(0);
        if(!PrepareVkRouteTransitionLocked(base.device,base.route,lock,base.contract.routeEpoch))
            return {VkRecordStatus::NoWork,"Vulkan route transition awaits prior model GPU/recording/consumer release"};
        const auto queue=VkNrRecordingQueueContext(base.device,VulkanNrRecordings().UseFamily(base.use),base.contract.queue);
        if(VulkanNrRecordings().UseDevice(base.use)!=base.device||!queue||(base.route!=VkNrRoute::Native&&!base.finalColorOnly&&!queue->queue)) {
            static std::atomic<uint64_t> refusals{0};const auto n=++refusals;
            const auto family=VulkanNrRecordings().UseFamily(base.use);
            if(n<=8||n%600==0)LOG_INFO("Vulkan Native queue refusal: device={} family={} eligibleQueues={} count={}",
                reinterpret_cast<uintptr_t>(base.device),family,VkNrCompletionQueueCount(base.device,family),n);
            return {VkRecordStatus::NoWork,"Vulkan chain queue ownership is unproved"};
        }
        if(!PrepareVkQueueTransitionLocked(base.route,*queue))
            return {VkRecordStatus::NoWork,"Vulkan model queue transition awaits owned completion and recording/consumer release"};
        base.contract.queue=queue->queue;
        base.contract.queueFamily=queue->family;
        if(base.contract.representation.format==VK_FORMAT_UNDEFINED)base.contract.representation.format=color.Format;
        if(base.contract.temporal)base.contract.guideGeneration=base.contract.temporal->featureGeneration;
        const auto metadata=base.resolutionMetadata?base.resolutionMetadata:base.contract.temporal;
        const auto work=base.presentInput?ResolveVkNrPresentWorkload(*base.presentInput,base.contract,metadata,root.admittedWorkload,base.observedRenderSize):
            ResolveVkNrWorkload(cfg,base.contract,metadata,root.admittedWorkload);
        if(work.reason||!work.width||!work.height)return {VkRecordStatus::NoWork,work.reason?work.reason:"Vulkan chain work size unavailable"};
        base.contract.work={work.width,work.height};plan=BuildVkNrPassPlan(cfg,base.contract);
        if(plan.passes.empty())return {VkRecordStatus::NoWork,plan.reason.empty()?"No effective Vulkan model passes":plan.reason};
        // Structural equality excludes frame tokens and UI observation revisions.
        // Changing count/resolution/model settings is an explicit admission retry.
        if(root.preparationPlan!=plan.passes) {
            root.preparationPlan=plan.passes;
            for(uint32_t pass=0;pass<10;++pass) {
                auto& state=PassStateVk(base.route,pass);
                if(state.generations)state.generations->RetryPreparation();
            }
        }
        for(uint32_t pass=static_cast<uint32_t>(plan.passes.size());pass<10;++pass)
            RetireVkPassGenerationsLocked(base.route,pass,lock,"unused_pass");
        if(!root.pass)root.pass=std::make_unique<DlssNr_Vk>("Neural Rendering",base.device,base.physicalDevice,
            GetVulkanPresentRegistry().StorageWriteWithoutFormatEnabled(base.device));
        if(!root.pass->IsInit())return {VkRecordStatus::NoWork,"Vulkan chain composition unavailable"};
        reservation=root.pass->Reserve(base.use,VkNrPassDemand(static_cast<uint32_t>(plan.passes.size())));
        if(!reservation)return {VkRecordStatus::NoWork,"complete Vulkan chain slots await recording/completion release"};
        for(uint32_t pass=1;pass<plan.passes.size();++pass)PassStateVk(base.route,pass).borrowedPass=root.pass.get();
    }
    VkNrPassCallbacks callbacks;
    auto visit=[&](uint32_t pass,const VkFrameRequest& input,const VkNrGenerationKey& key,
                         const VkNrReservation& slots,uint64_t version,bool preparationOnly) {
        auto frame=input;frame.passIndex=pass;frame.privateChain=true;frame.composition=slots;frame.workOverride=key.frame.work;
        auto settings=VkNrPassSettingsFor(cfg,pass);
        settings.DlssNrCompare=0u;settings.DlssNrDebugView=0u;
        if(pass)settings.DlssNrHoldFrame=false;
        if(pass) {
            frame.contract.placement=VkNrPlacement::AfterSR;frame.colorLayout=VK_IMAGE_LAYOUT_GENERAL;
            frame.postSrOutput.reset(); // The preceding composed image is a local carrier.
            ClearVkNrColorInput(frame);
            frame.targetColor=base.targetColor?base.targetColor:base.color;
            frame.presentInput.reset();frame.effectiveResolutionMode.reset();
            std::lock_guard lock(g_vkMutex);
            const auto& root=PassStateVk(base.route,0);
            if(root.resources&&root.resources->held.Active()){
                auto held=root.resources->held.Select(root.generations->AppliedKey()->frame);
                if(held){frame.depth=held->depth;frame.motion=held->motion;frame.contract.temporal=held->contract.temporal;}}
            std::vector<VkNrHistoryIdentity> prefix;
            for(uint32_t previous=0;previous<pass;++previous) {
                const auto& source=PassStateVk(base.route,previous);
                prefix.push_back({source.generations?source.generations->AppliedGeneration():0,source.resetState.Capture()});
            }
            if(PassStateVk(base.route,0).passHistory.Observe(pass,prefix)) {
                auto& child=PassStateVk(base.route,pass);child.resetState.Request();
            }
        }
        // Only the selected first SR invocation can ask for private DLAA.
        EvaluateAfterUpscaleVkInternal(frame,pass==0?params:nullptr,settings,preparationOnly);
        std::lock_guard lock(g_vkMutex);auto& state=PassStateVk(base.route,pass);
        VkNrPassStep step;
        if(preparationOnly) {
            step.recording={state.preparedCurrent?VkRecordStatus::Complete:VkRecordStatus::NoWork,
                state.reason,state.modelRecordedCurrent,false};
            if(state.preparedCurrent&&state.resources)step.preparedColor=state.resources->composed.ngx;
            return step;
        }
        step.recording={state.outputWriteRecordedCurrent?VkRecordStatus::Complete:
            state.modelRecordedCurrent?VkRecordStatus::Partial:VkRecordStatus::NoWork,
            state.reason,state.modelRecordedCurrent,state.outputWriteRecordedCurrent};
        if(state.outputWriteRecordedCurrent&&state.resources){step.output=&state.resources->composed.ngx;step.outputVersion=version+1;}
        return step;
    };
    callbacks.prepare=[&](uint32_t pass,const VkFrameRequest& input,const VkNrGenerationKey& key,
                         const VkNrReservation& slots,uint64_t version){return visit(pass,input,key,slots,version,true);};
    callbacks.record=[&](uint32_t pass,const VkFrameRequest& input,const VkNrGenerationKey& key,
                         const VkNrReservation& slots,uint64_t version){return visit(pass,input,key,slots,version,false);};
    callbacks.deliver=[&](const VkNrPassStep& final,uint32_t slot) {
        std::lock_guard lock(g_vkMutex);ScopedVkSession active(base.route);auto& root=ActiveVk();
        if(!root.resources||!final.output||final.passIndex>=plan.passes.size())return false;
        auto* target=base.targetColor;
        if(before) {Transition(base.commandBuffer,root.resources->preSr,VK_IMAGE_LAYOUT_GENERAL);target=&root.resources->preSr.ngx;}
        if(!target||target->Resource.ImageViewInfo.Image==final.output->Resource.ImageViewInfo.Image)return false;
        DlssNrConstants display{};display.Mode=DlssNrMode_Present;display.Width=base.width;display.Height=base.height;
        // Comparison is a final display operation, once, outside every neural history.
        PopulateVkNrComparisonConstants(before?0u:cfg.DlssNrCompare.value_or_default(),cfg.DlssNrCompareSplit.value_or_default(),
            cfg.DlssNrCompareZoom.value_or_default(),cfg.DlssNrCompareSwap.value_or_default(),display);
        display.WhitePoint=cfg.DlssNrWhitePointScale.value_or_default();
        auto& last=PassStateVk(base.route,final.passIndex);
        PopulateVkNrDiagnosticConstants(cfg,base.contract,display);
        const bool diagnostic=display.DebugView!=0;
        if(diagnostic){auto debugView=display.DebugView;auto debugScale=display.DebugScale;display=last.diagnosticConstants;
            display.DebugView=debugView;display.DebugScale=debugScale;display.CompareMode=0;}
        // Apply after diagnostic constants replacement; all sampled private images remain local.
        display.TargetX=before?0u:base.postSrOutput.value_or(VkNrRect{}).x;
        display.TargetY=before?0u:base.postSrOutput.value_or(VkNrRect{}).y;
        Transition(base.commandBuffer,root.resources->keep,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const bool deliverySkipped=((base.captureOnly&&!base.ownedPresentInputs)||base.finalColorOnly)&&!before;
        bool delivered=deliverySkipped||root.pass->Dispatch(base.commandBuffer,base.use,slot,display,base.width,base.height,
            diagnostic?last.diagnosticProxy:final.output->Resource.ImageViewInfo.ImageView,diagnostic?last.diagnosticAnswer:VK_NULL_HANDLE,root.resources->keep.view,VK_NULL_HANDLE,
            target->Resource.ImageViewInfo.ImageView,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_GENERAL);
        if(delivered&&!deliverySkipped&&!before&&base.route==VkNrRoute::Native)
            PublishVkNrNativeOutput(base.commandBuffer,target->Resource.ImageViewInfo.Image,
                target->Resource.ImageViewInfo.SubresourceRange);
        if(delivered&&before&&root.generations->AppliedKey()->privateDlaa) {
            Transition(base.commandBuffer,root.resources->preSr,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(base.commandBuffer,root.resources->reJitter,VK_IMAGE_LAYOUT_GENERAL);
            auto jitter=display;jitter.Mode=5;
            jitter.MvScaleX=base.contract.temporal->jitterX;jitter.MvScaleY=base.contract.temporal->jitterY;
            delivered=root.pass->Dispatch(base.commandBuffer,base.use,reservation->slots.back(),jitter,base.width,base.height,
                root.resources->preSr.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,root.resources->reJitter.view,VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(base.commandBuffer,root.resources->reJitter,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else if(delivered&&before)Transition(base.commandBuffer,root.resources->preSr,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        // Bounded private-image samples go to the ordinary log after real completion.
        // They establish local pixel deltas without requiring screenshot exports or GPU waits.
        if(delivered&&!base.captureOnly&&last.resources&&final.completedPasses==final.requestedPasses&&
           cfg.DlssNrApplyModel.value_or_default()&&!cfg.DlssNrDebugView.value_or_default()) {
            const auto generation=root.generations->AppliedGeneration();
            if(root.pixelProbeGeneration!=generation){root.pixelProbeGeneration=generation;root.pixelProbeFrames=0;}
            if(root.pixelProbeFrames<30) {
                ++root.pixelProbeFrames;
                if(root.pixelProbeFrames==3||root.pixelProbeFrames==30) {
                  try {
                    VkNrCaptureRequest probe;probe.frame=base.contract;probe.use=base.use;
                    probe.requestedPasses=final.requestedPasses;probe.completedPasses=final.completedPasses;
                    const auto& constants=root.diagnosticConstants;
                    probe.settings=std::format("generation={} sampleFrame={} passthrough={} reversible={} whitePoint={} apply={} transferStrength={} colourStrength={} compare={} target={} targetFormat={}",
                        generation,root.pixelProbeFrames,constants.Passthrough,constants.ReversibleMode,constants.WhitePoint,
                        constants.ApplyModel,constants.TransferStrength,constants.ColourStrength,cfg.DlssNrCompare.value_or_default(),
                        reinterpret_cast<uintptr_t>(target->Resource.ImageViewInfo.Image),static_cast<unsigned>(target->Resource.ImageViewInfo.Format));
                    auto sample=[](const char* name,const OwnedImage& image){return VkNrCaptureImage{name,image.image,image.format,image.layout,{image.width,image.height},0};};
                    probe.images={sample("original",root.resources->keep),sample("composed-private",last.resources->composed),
                        sample("proxy",root.resources->proxy),sample("model",last.resources->output)};
                    RecordVulkanNrPixelProbe(probe,base.commandBuffer,base.physicalDevice,base.device);
                  } catch(...) { /* Optional diagnostics must not interrupt delivery. */ }
                }
            }
        }
        if(delivered&&!before&&last.resources&&final.completedPasses==final.requestedPasses&&WantsVulkanNrCapture()&&cfg.DlssNrApplyModel.value_or_default()&&!cfg.DlssNrDebugView.value_or_default()&&!cfg.DlssNrCompare.value_or_default()) {
            VkNrCaptureRequest capture;capture.frame=base.contract;capture.use=base.use;
            capture.requestedPasses=final.requestedPasses;capture.completedPasses=final.completedPasses;
            capture.settings="route="+std::to_string(static_cast<unsigned>(base.route))+";work="+std::to_string(base.contract.work.width)+"x"+std::to_string(base.contract.work.height)+";placement="+std::to_string(static_cast<unsigned>(base.contract.placement))+";settingsRevision="+std::to_string(base.contract.settingsRevision);
            for(const auto& pass:plan.passes)capture.settings+=";pass="+std::to_string(pass.passIndex)+",work="+std::to_string(pass.frame.work.width)+"x"+std::to_string(pass.frame.work.height)+",filters="+std::to_string(pass.upFilter)+"/"+std::to_string(pass.downFilter);
            const float previewWhite=root.diagnosticConstants.Passthrough?0.f:root.diagnosticConstants.WhitePoint;
            auto image=[&](const char* name,const OwnedImage& owned,float white){return VkNrCaptureImage{name,owned.image,owned.format,owned.layout,{owned.width,owned.height},white};};
            capture.images.push_back(image("original",root.resources->keep,previewWhite));
            capture.images.push_back(image("final",last.resources->composed,previewWhite));
            capture.images.push_back(image("proxy",root.resources->proxy,0));
            capture.images.push_back(image("model",last.resources->output,0));
            RecordVulkanNrCapture(capture,base.commandBuffer,base.physicalDevice,base.device);
        }
        if(base.route==VkNrRoute::Native) {
            SignalReceipts::Fact actual{true,true,base.width,base.height,uint32_t(target->Resource.ImageViewInfo.Format),
                before?SignalReceipts::Stage::PreSr:base.contract.placement==VkNrPlacement::AfterRR?
                    SignalReceipts::Stage::PostRr:SignalReceipts::Stage::PostSr,
                diagnostic?SignalReceipts::Domain::Unknown:root.diagnosticConstants.Passthrough?
                    SignalReceipts::Domain::EncodedDisplay:SignalReceipts::Domain::LinearHdr,
                "Final copy recorded; selected SR result and GPU completion still required"};
            actual.originX=display.TargetX;actual.originY=display.TargetY;
            root.signalDiagnostics.Delivery(base.use,base.contract.evaluation,
                delivered&&!deliverySkipped&&!base.captureOnly&&!base.finalColorOnly,actual);
        }
        if(delivered&&!before&&!base.captureOnly){++root.frames;root.preSrDelivered=false;}
        return delivered;
    };
    auto chain=RecordVkNrPassChain(base,plan,*reservation,callbacks);
    auto result=chain.recording;
    if(base.route==VkNrRoute::Present&&result.modelRecorded)
        result.modelLifetime=VkNrPresentModelLifetime{reinterpret_cast<uintptr_t>(base.device),
            base.contract.deviceGeneration,base.contract.routeEpoch,base.contract.resumeGeneration,
            base.contract.inputInterruptionEpoch};
    result.requestedPasses=chain.requested;result.completedPasses=chain.completed;
    result.outputVersion=chain.outputVersion;result.chainDeliverable=chain.deliverable;
    if(before) {
        result.preSrSourceCurrent=[base,params]() mutable {
            std::string reason;
            return SelectVkNrPreSrColor(base,params,VulkanNrImageFacts(),VulkanNrRecordings(),
                VkNrCompletionDeviceGeneration(base.device),true,reason);
        };
        if(!result.preSrSourceCurrent()) {
            // Commands already recorded remain owned; only delivery is withdrawn.
            chain.deliverable=false;result.chainDeliverable=false;result.outputWriteRecorded=false;
            result.status=result.modelRecorded?VkRecordStatus::Partial:VkRecordStatus::NoWork;
            result.reason="Native original source changed before carrier publication";
        }
        std::lock_guard lock(g_vkMutex);auto& root=g_nativeVk;
        root.performanceSource.reset();root.performanceOriginalSource.reset();root.performanceSourceCurrent={};
        root.performanceRawSourceCurrent={};root.performanceParams=nullptr;root.performanceCaptureOnly=false;
        root.performanceUse={};root.performanceEvaluation={};
        if(chain.deliverable&&chain.completed==chain.requested) {
            root.performanceSource=*base.color;
            root.performanceOriginalSource=base.colorIngress->Original();
            root.performanceRawSourceCurrent=result.preSrSourceCurrent;
            root.performanceParams=params;root.performanceCaptureOnly=base.captureOnly;
            root.performanceSourceCurrent=[color=base.qualifiedColor] {return color->StorageCurrent(VulkanNrImageFacts());};
            root.performanceUse=base.use;root.performanceEvaluation=base.contract.evaluation;
            root.performanceSerial=VulkanNrRecordings().CommandSerial(base.commandBuffer);
        }
    }

    {std::unique_lock lock(g_vkMutex);for(uint32_t pass=static_cast<uint32_t>(plan.passes.size());pass<10;++pass)
        RetireVkPassGenerationsLocked(base.route,pass,lock,"unused_pass");}
    {
        std::lock_guard lock(g_vkMutex);ScopedVkSession active(base.route);auto& root=ActiveVk();root.chain=chain;
        root.modelRecordedCurrent=result.modelRecorded;root.outputWriteRecordedCurrent=result.outputWriteRecorded;
        if(base.route==VkNrRoute::Native&&root.generations)
            result.nativeRecording=VkNrNativeRecordingReceipt{base.contract,base.use,root.generations->AppliedGeneration()};
        if(!base.captureOnly&&!before&&chain.deliverable&&root.resources&&(base.contract.temporal||base.providerTemporal)&&cfg.DlssNrApplyModel.value_or_default()&&
           !cfg.DlssNrHoldFrame.value_or_default()&&!cfg.DlssNrDebugView.value_or_default()&&!cfg.DlssNrCompare.value_or_default()) {
            auto& last=PassStateVk(base.route,chain.completed-1);
            VkNrRecordedOutput output;output.frame=base.contract;if(base.providerTemporal)output.frame.temporal=base.providerTemporal;output.use=base.use;output.resource=last.resources->composed.ngx;
            output.memory=last.resources->composed.memory;output.gameInput=base.targetColor?base.targetColor->Resource.ImageViewInfo.Image:VK_NULL_HANDLE;
            output.contentRevision=base.use.value;output.commandSerial=VulkanNrRecordings().CommandSerial(base.commandBuffer);
            output.requestedPasses=chain.requested;output.completedPasses=chain.completed;
            result.finalOutput=output;root.candidate=output;
            VkNrStreamlineSourceScope::Candidate(output.use);
        }
        if(before&&chain.deliverable&&root.resources&&root.generations) {
            result.preSrColor=root.generations->AppliedKey()->privateDlaa?&root.resources->reJitter.ngx:&root.resources->preSr.ngx;result.preSrContract=root.generations->AppliedKey()->frame;result.preSrContract.evaluation=base.contract.evaluation;
            result.preSrGeneration=root.generations->AppliedGeneration();result.forceSrReset=root.resources->srReset;
            bool seeded=true;
            for(uint32_t pass=0;pass<chain.requested;++pass)
                seeded&=PassStateVk(base.route,pass).resources&&PassStateVk(base.route,pass).resources->nrSeed.Ready();
            result.preSrReady=seeded&&cfg.DlssNrApplyModel.value_or_default();
            if(!seeded)result.reason="Native Performance complete chain seed awaits GPU completion";
        }
    }
    return result;
}

#include "VulkanNrPerformanceCapture.inl"
#include "VulkanNrFinalColor.inl"

namespace {
struct SelectedNativeAttempt {
    bool complete=false;
    ~SelectedNativeAttempt(){if(!complete)RequestHistoryResetVk();}
};
}

VkRecordResult EvaluateAfterUpscaleVk(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* params, VkInstance instance,
                                      VkPhysicalDevice physicalDevice, VkDevice device,
                                      const NrConfigSnapshot<Config>* settings,
                                      const VkNrTemporalMetadata* temporal, bool rayReconstruction,const VkNrEvaluationIdentity* evaluation,
                                      const VkFrame::PostSrOutputObservation* output)
{
    FinalFallback::RenderScope renderAdmission;
    if (!renderAdmission.Admitted()) return {VkRecordStatus::NoWork,"Output handoff is active"};
    const auto captured = settings ? std::optional<NrConfigSnapshot<Config>>{} :
        TryNrConfigSnapshot(*Config::Instance());
    if (!settings && !captured) return {VkRecordStatus::NoWork, "configuration capture unavailable"};
    const auto& cfg = settings ? *settings : *captured;
    if (!cfg.GetDlssNrRuntimeSnapshot().enabled || cfg.DlssNrRoute.value_or_default() != 0)
        return {VkRecordStatus::NoWork,"Native Temporal is not selected"};
    SelectedNativeAttempt attempt;
    if(!output)return {VkRecordStatus::NoWork,"Selected Native output geometry unavailable"};
    NVSDK_NGX_Resource_VK* restoredOutput=nullptr;VkFrame::Failure outputFailure;
    if(!params||params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&restoredOutput))!=NVSDK_NGX_Result_Success||
       !VkFrame::RestoredPostSrOutput(*output,restoredOutput,outputFailure))
        return {VkRecordStatus::NoWork,"Selected Native output view or rectangle was not restored after SR"};
    auto& tracker=Vulkan_wDx12::cmdBufferStateTracker;vk_state::CommandBufferState saved;
    if(!tracker.CaptureNrState(cmdBuffer,saved)||!saved.Recording||saved.InRenderPass)return {VkRecordStatus::NoWork,"Native Temporal command bindings are unproved"};
    struct RestoreNrBindings {vk_state::CommandBufferStateTracker& tracker;VkCommandBuffer cb;vk_state::CommandBufferState& state;
        ~RestoreNrBindings() noexcept {vk_state::ReplayParams replay;replay.RequiredGraphicsSetMask=UINT32_MAX;replay.ReplayComputeToo=true;replay.ReplayVertexIndex=true;
            try{if(!tracker.ReplaySaved(cb,state,replay))LOG_ERROR("VK-NR Native binding restoration failed");}catch(...){LOG_ERROR("VK-NR Native binding restoration exception");}}
    } restore{tracker,cmdBuffer,saved};
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    {
        std::lock_guard<std::mutex> stateLock(g_vkMutex);
        g_nativeVk.modelRecordedCurrent = false;
        g_nativeVk.outputWriteRecordedCurrent = false;
    }
    VkFrameRequest request {};
    request.commandBuffer = cmdBuffer;
    request.instance = instance;
    request.physicalDevice = physicalDevice;
    request.device = device;
    request.contract.placement = rayReconstruction ? VkNrPlacement::AfterRR : VkNrPlacement::AfterSR;
    if(evaluation)request.contract.evaluation=*evaluation;
    if (temporal) request.contract.temporal = *temporal;
    request.postSrOutput=output->rect;
    auto result=g_nativeSession.Record(request, [&](const VkFrameRequest& frame, bool reset) {
        if (reset) RequestHistoryResetVk();
        return EvaluateVkChain(frame,params,cfg);
    });
    attempt.complete=result.status==VkRecordStatus::Complete;
    return result;
}

VkRecordResult EvaluateBeforeUpscaleVk(const VkFrameRequest& request,NVSDK_NGX_Parameter* params,
                                     const NrConfigSnapshot<Config>& cfg)
{
    FinalFallback::RenderScope renderAdmission;
    if (!renderAdmission.Admitted()) return {VkRecordStatus::NoWork,"Output handoff is active"};
    if (!cfg.GetDlssNrRuntimeSnapshot().enabled || cfg.DlssNrRoute.value_or_default() != 0 ||
        !cfg.DlssNrRunBeforeSr.value_or_default() || request.contract.placement != VkNrPlacement::BeforeSR || !params)
        return {VkRecordStatus::NoWork,"Native Performance is not selected"};
    SelectedNativeAttempt attempt;
    auto& tracker = Vulkan_wDx12::cmdBufferStateTracker;
    vk_state::CommandBufferState saved;
    NVSDK_NGX_Resource_VK* input = nullptr;
    VkFrameRequest qualified = request;
    VkFrame::Failure failure;
    if (!tracker.CaptureNrState(request.commandBuffer,saved) || !saved.Recording ||
        params->Get(NVSDK_NGX_Parameter_Color,reinterpret_cast<void**>(&input)) != NVSDK_NGX_Result_Success ||
        !VkFrame::ValidateImage("SR.Color",input,false,failure))
        return {VkRecordStatus::NoWork,"Native Performance recording state or color input is unproved"};
#ifndef LOW_PRECISION_TRACKING
    const auto layout = saved.ImageLayouts.find(input->Resource.ImageViewInfo.Image);
    const auto range = saved.ImageLayoutRanges.find(input->Resource.ImageViewInfo.Image);
    static std::atomic<unsigned> preColorRefusalReceipts{0};
    std::string colorReason,colorDiagnostic;
    auto* diagnostic=preColorRefusalReceipts.load(std::memory_order_relaxed)<64?&colorDiagnostic:nullptr;
    qualified.qualifiedColor=VkNrQualifiedColor::Qualify(VulkanNrImageFacts(),qualified.colorIngress,colorReason,diagnostic);
    if(!qualified.qualifiedColor){
        if(!colorDiagnostic.empty()){
            auto used=preColorRefusalReceipts.load(std::memory_order_relaxed);
            while(used<64&&!preColorRefusalReceipts.compare_exchange_weak(used,used+1,std::memory_order_relaxed)){}
            if(used<64)try{
                LOG_INFO("Vulkan Native Pre-SR image refusal: role=SceneColor firstFailure={} observed={}",colorReason,colorDiagnostic);
            }catch(...){/* Diagnostics never change image admission. */}
        }
        return {VkRecordStatus::NoWork,colorReason};
    }
    if(!SelectVkNrPreSrColor(qualified,params,VulkanNrImageFacts(),VulkanNrRecordings(),
        VkNrCompletionDeviceGeneration(request.device),false,colorReason))return {VkRecordStatus::NoWork,colorReason};
    const auto rights=std::optional{qualified.qualifiedColor->Rights()};
    if(saved.InRenderPass)return {VkRecordStatus::NoWork,"Native Performance requires recording outside a render pass"};
    const auto inputLayout = ResolveVkNrInputLayout(request.ngxSrInputContract,
        rights->view.range,
        layout==saved.ImageLayouts.end()?std::nullopt:std::optional{layout->second},
        range==saved.ImageLayoutRanges.end()?std::nullopt:std::optional{range->second});
    if(!inputLayout.accepted)return {VkRecordStatus::NoWork,inputLayout.reason};
    qualified.colorLayout = inputLayout.layout;
#else
    return {VkRecordStatus::NoWork,"Native Performance requires full recording layout observations"};
#endif
    if(const auto reason=VkNrAdaptedDlaaRefusal(true,qualified.qualifiedColor->Adapted(),cfg.DlssNrPreDlaa.value_or_default()))
        return {VkRecordStatus::NoWork,reason};
    struct RestoreBindings {
        vk_state::CommandBufferStateTracker& tracker;VkCommandBuffer command;vk_state::CommandBufferState& saved;
        ~RestoreBindings() noexcept {
            vk_state::ReplayParams replay; replay.RequiredGraphicsSetMask = UINT32_MAX;
            replay.ReplayComputeToo = true; replay.ReplayVertexIndex = true;
            try { if (!tracker.ReplaySaved(command,saved,replay)) LOG_ERROR("VK-NR failed to restore saved command state"); }
            catch (...) { LOG_ERROR("VK-NR exception restoring saved command state"); }
        }
    } restore {tracker,request.commandBuffer,saved};
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    auto result = g_nativeSession.Record(qualified,[&](const VkFrameRequest& frame,bool reset) -> VkRecordResult {
        auto selected=frame;std::string reason;
        if(!SelectVkNrPreSrColor(selected,params,VulkanNrImageFacts(),VulkanNrRecordings(),
            VkNrCompletionDeviceGeneration(frame.device),true,reason))return {VkRecordStatus::NoWork,reason};
        if(const auto refusal=VkNrAdaptedDlaaRefusal(true,selected.qualifiedColor->Adapted(),cfg.DlssNrPreDlaa.value_or_default()))
            return {VkRecordStatus::NoWork,refusal};
        if (reset) RequestHistoryResetVk();
    // Extend the producer's availability to our compute read without transitioning
    // the borrowed game input. Original NGX queue ownership/semaphore waits apply.
    VkMemoryBarrier inputRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    inputRead.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    inputRead.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(request.commandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&inputRead,0,nullptr,0,nullptr);

        return EvaluateVkChain(selected,params,cfg);
    });
#ifndef LOW_PRECISION_TRACKING
    result.inputLayoutBasis=inputLayout.basis;
#endif
    attempt.complete=result.status==VkRecordStatus::Complete;
    return result;
}

void CompletePreSrVk(const VkRecordResult& recorded,bool bound,bool succeeded)
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    auto& state = g_nativeVk;
    if (!bound) { state.preSrDelivered = false; state.resetState.Request(); return; }
    if (!succeeded) {
        state.preSrDelivered = false; state.resetState.Request();
        auto* failed = state.generations ? static_cast<VkResources*>(state.generations->Payload(recorded.preSrGeneration)) : nullptr;
        if (failed) { failed->nrSeed.Reset(); failed->srReset = true; }
        state.reason = "Native Performance SR call failed; private history must be seeded again";
        return;
    }
    if (state.generations && state.generations->AppliedGeneration() == recorded.preSrGeneration && state.generations->AppliedKey() &&
        SameVkNrPreSrRaster(state.generations->AppliedKey()->frame,recorded.preSrContract) && state.resources) {
        state.resources->srReset = false; state.preSrDelivered = true; ++state.frames;
    }
}

void PollPresentInitializationVk()
{
    std::lock_guard recording(g_nativeRecordMutex);
    std::unique_lock lock(g_vkMutex);
    PollVkNrCompletions();
    for(uint32_t pass=0;pass<10;++pass) {
        ScopedVkSession active(VkNrRoute::Present,pass);
        auto& state=ActiveVk();
        struct Control { VkState& state;std::unique_lock<std::mutex>* prior;
            ~Control(){state.controlLock=prior;} } control{state,state.controlLock};
        state.controlLock=&lock;
        if(state.generations) {
            state.resources=nullptr;
            auto diagnostic=VkGenerationLossTrace("poll_present_initialization");
            state.generations->RetireCompleted(VulkanNrRecordings(),diagnostic?&*diagnostic:nullptr);
            if(diagnostic)TraceVkGenerationLoss(*state.generations,*diagnostic);
            state.resources=static_cast<VkResources*>(state.generations->Payload(state.generations->AppliedGeneration()));
        }
    }
}

VkPresentInitializationStep PreparePresentModelsVk(const VkPresentInitializationRequest& request,
    VkCommandBuffer command,VkNrUseId use)
{
    VkPresentInitializationStep result;
    const auto refuse=[&](const char* reason) {result.reason=reason;return result;};
    FinalFallback::RenderScope admission;
    if(!admission.Admitted()||!request.settings||!request.waitBudget)
        return refuse("Vulkan initialization admission unavailable");
    const auto& cfg=*request.settings;
    const auto runtime=cfg.GetDlssNrRuntimeSnapshot();
    const auto route=cfg.DlssNrRoute.value_or_default();
    if(!runtime.enabled||(route!=1&&route!=2)||!VkPresentInitializationEligible(
        cfg.DlssNrVulkanPresentSameFrameInit.value_or_default(),request.frame.route,request.frame.placement,
        PresentResolution::Selected(cfg).mode,request.input.resolution.mode,cfg.DlssNrHoldFrame.value_or_default(),
        false,cfg.IsPrivateDiagnostic()))return refuse("Vulkan same-frame initialization is not eligible");
    if(!command||!use||!request.device||!request.physicalDevice||!request.instance||
        request.colorFormat!=VK_FORMAT_R16G16B16A16_SFLOAT||request.targetFormat!=VK_FORMAT_R16G16B16A16_SFLOAT||
        request.depthFormat==VK_FORMAT_UNDEFINED||request.motionFormat==VK_FORMAT_UNDEFINED)
        return refuse("Vulkan initialization metadata is incomplete");
    std::lock_guard recording(g_nativeRecordMutex);
    std::unique_lock lock(g_vkMutex);
    ScopedVkSession rootScope(VkNrRoute::Present);
    if(!PrepareVkPassDeviceLocked(request.device))return refuse("Vulkan initialization device is unavailable");
    const auto queue=VkNrRecordingQueueContext(request.device,VulkanNrRecordings().UseFamily(use),request.frame.queue);
    if(!queue||queue->queue!=request.frame.queue||queue->family!=request.frame.queueFamily||
        VulkanNrRecordings().UseDevice(use)!=request.device||!VulkanNrRecordings().UseOnRecording(use,command)||
        request.frame.deviceGeneration!=VkNrCompletionDeviceGeneration(request.device))
        return refuse("Vulkan initialization queue/use identity is unproved");
    auto& root=ActiveVk();root.instance=request.instance;root.physicalDevice=request.physicalDevice;
    if(root.failed)return refuse(root.reason);
    if(!EnsureVkProviderDeviceLocked(request.instance,request.physicalDevice,request.device,lock))
        return refuse("Vulkan initialization provider unavailable");
    auto frame=request.frame;
    frame.routeEpoch=g_vkRuntime.IsActive(VkNrRoute::Present)?g_vkRuntime.Epoch():g_vkRuntime.Epoch()+1;
    frame.resumeGeneration=runtime.resumeGeneration;
    frame.inputInterruptionEpoch=FinalFallback::CurrentInputEpoch();
    frame.settingsRevision=cfg.ObservationRevision().value_or(0);
    if(frame.temporal)frame.guideGeneration=frame.temporal->featureGeneration;
    if(!PrepareVkRouteTransitionLocked(request.device,VkNrRoute::Present,lock,frame.routeEpoch)||
        !PrepareVkQueueTransitionLocked(VkNrRoute::Present,*queue)) {
        result.status=VkPresentInitializationStatus::Pending;
        return refuse("Vulkan initialization awaits previous route/queue owners");
    }
    const auto work=ResolveVkNrPresentWorkload(request.input,frame,request.resolutionMetadata?request.resolutionMetadata:frame.temporal,
        root.admittedWorkload,request.observedRenderSize);
    if(work.reason||!work.width||!work.height)return refuse(work.reason?work.reason:"Vulkan initialization work size unavailable");
    frame.work={work.width,work.height};
    if(!ValidateVkNrFrameContract(frame))return refuse("Vulkan initialization frame contract invalid");
    const auto plan=BuildVkNrPassPlan(cfg,frame);
    if(plan.passes.empty()||plan.passes.size()>10)return refuse("Vulkan initialization pass plan unavailable");
    result.requestedPasses=static_cast<uint32_t>(plan.passes.size());
    if(root.preparationPlan!=plan.passes) {
        root.preparationPlan=plan.passes;
        for(uint32_t pass=0;pass<10;++pass)if(auto& owner=PassStateVk(VkNrRoute::Present,pass).generations;owner)
            owner->RetryPreparation();
    }
    for(uint32_t pass=result.requestedPasses;pass<10;++pass)RetireVkPassGenerationsLocked(VkNrRoute::Present,pass,lock,"initializer_unused_pass");
    const auto lifetime=[&] {
        if(g_vkRuntime.IsActive(VkNrRoute::Present))result.modelLifetime=VkNrPresentModelLifetime{
            reinterpret_cast<uintptr_t>(request.device),frame.deviceGeneration,g_vkRuntime.Epoch(),
            frame.resumeGeneration,frame.inputInterruptionEpoch};
    };
    for(uint32_t pass=0;pass<result.requestedPasses;++pass) {
        if(!request.waitBudget->RemainingNs()) {
            result.status=VkPresentInitializationStatus::Pending;
            lifetime();return refuse("Vulkan initialization deadline exhausted before creation");
        }
        ScopedVkSession active(VkNrRoute::Present,pass);auto& state=ActiveVk();
        if(!PrepareVkPassDeviceLocked(request.device))return refuse("Vulkan initialization pass device unavailable");
        state.instance=request.instance;state.physicalDevice=request.physicalDevice;
        state.modelQueue=queue->queue;state.modelQueueFamily=queue->family;
        if(state.failed)return refuse(state.reason);
        struct Control {VkState& state;std::unique_lock<std::mutex>* prior;VkCommandBuffer command;
            ~Control(){state.controlLock=prior;state.recordingCommand=command;}}
            control{state,state.controlLock,state.recordingCommand};
        state.controlLock=&lock;state.recordingCommand=command;state.modelRecordedCurrent=false;
        auto key=plan.passes[pass];
        CompleteVkPresentGenerationKey(key,request.colorFormat,request.targetFormat,request.depthFormat,request.motionFormat,request.gameHdr);
        if(!state.generations)state.generations=std::make_unique<VkNrGenerationOwner>(VulkanNrRecordings(),
            [](const auto& desired,VkNrUseId id,uint64_t budget){return CreateVkResources(desired,id,budget);},ReleaseVkResources);
        const auto others=GenerationBytesVk()-state.generations->PrivateBytes();
        if(others>request.privateAllocationBudget)return refuse("Vulkan initialization private allocation budget exhausted");
        state.generations->SetPrivateBudget(request.privateAllocationBudget-others);
        const auto previous=state.generations->AppliedGeneration();
        state.resources=nullptr;
        auto diagnostic=VkGenerationLossTrace("prepare_present_models");
        const auto generation=state.generations->Prepare(key,use,[&](VkNrUseId old) {
            const auto remaining=request.waitBudget->RemainingNs();if(!remaining)return;
            lock.unlock();WaitVkNrUse(old,remaining);lock.lock();
        },true,false,diagnostic?&*diagnostic:nullptr);
        if(diagnostic)TraceVkGenerationLoss(*state.generations,*diagnostic);
        TraceVkGenerationRefusal(generation,key);
        result.recorded=state.modelRecordedCurrent;
        if(result.recorded) {g_vkRuntime.Activate(VkNrRoute::Present);g_presentSession.RequestReset();}
        lifetime();
        if(!generation.ready||!state.generations->Commit(generation.generation)) {
            state.generationReason=generation.reason;state.reason=state.generationReason.c_str();
            result.status=generation.pending?VkPresentInitializationStatus::Pending:VkPresentInitializationStatus::Refused;
            result.reason=generation.reason;return result;
        }
        state.resources=static_cast<VkResources*>(state.generations->Payload(generation.generation));
        if(previous!=generation.generation) {
            state.resetState.Request();state.lastGpuTime.reset();state.lastGpuFrame.reset();
        }
        state.resumeGeneration=frame.resumeGeneration;state.inputInterruptionEpoch=frame.inputInterruptionEpoch;
        state.workload.requestedGeneration=state.workload.appliedGeneration=generation.generation;
        state.workload.requestedOutput=state.workload.appliedOutput=key.frame.output;
        state.workload.requestedWork=state.workload.appliedWork=key.frame.work;
        state.workload.pending=false;
        if(result.recorded) {result.status=VkPresentInitializationStatus::Recorded;return result;}
    }
    lifetime();
    result.status=VkPresentInitializationStatus::Ready;
    return result;
}

VkRecordResult EvaluatePresentImageVk(const VkFrameRequest& request)
{
    FinalFallback::RenderScope renderAdmission;
    if (!renderAdmission.Admitted()) return {VkRecordStatus::NoWork,"Output handoff is active"};
    const auto captured = request.settings ? std::optional<NrConfigSnapshot<Config>>{} : TryNrConfigSnapshot(*Config::Instance());
    const auto* settings = request.settings ? request.settings.get() : captured ? &*captured : nullptr;
    if (!settings) return {VkRecordStatus::NoWork, "configuration capture unavailable"};
    if (request.route != VkNrRoute::Present || request.color == nullptr || request.targetColor == nullptr ||
        request.depth == nullptr || request.motion == nullptr)
        return { VkRecordStatus::NoWork, "Present private images or neutral guides unavailable" };
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    {
        std::lock_guard<std::mutex> stateLock(g_vkMutex);
        g_presentVk.modelRecordedCurrent = false;
        g_presentVk.outputWriteRecordedCurrent = false;
    }
    return g_presentSession.Record(request, [&](const VkFrameRequest& frame, bool reset) {
        if (reset)
        {
            std::lock_guard<std::mutex> lock(g_vkMutex);
            g_presentVk.resetState.Request();
        }
        return EvaluateVkChain(frame,nullptr,*settings);
    });
}

void AbandonPresentRecordingVk(bool currentCounted)
{
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    g_presentSession.AbandonLatestRecording(currentCounted);
    g_presentVk.resetState.Request();
    g_presentVk.reason = "Present recording discarded; waiting for a safe model generation";
}

bool BeginAbandonPresentRecordingVk(VkNrUseId use,const VkNrRecordingOwner::UseRecordingSnapshot& beforeReset)
{
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    return g_presentSession.BeginAbandonRecording(use,beforeReset);
}

bool AbandonPresentRecordingVk(VkNrUseId use,const VkNrRecordingOwner::UseRecordingSnapshot& beforeReset)
{
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    if(!g_presentSession.AbandonRecording(use,beforeReset))return false;
    g_presentVk.resetState.Request();
    g_presentVk.reason="Present recording discarded; waiting for a safe model generation";
    return true;
}

void MarkPresentRuntimeUncertainVk(const char* reason)
{
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    g_presentSession.RequestReset();
    ++g_presentVk.diagnosticResetEpoch;g_presentVk.resetState.MarkPending();
    g_presentVk.failed = true;
    g_presentVk.reason = reason != nullptr ? reason : "Present GPU or presentation state uncertain";
}

static void ShutdownVkLocked(bool deviceAlive,std::optional<uint64_t> sourceRetirementGeneration)
{
    ActiveVk().signalDiagnostics.Clear();ActiveVk().timing.Clear();
    if(ActiveVk().device==VK_NULL_HANDLE)return;
    // Device destruction may finish after a newer source publishes. This
    // cutoff fences CPU diagnostics only; resource teardown always continues.
    if(!sourceRetirementGeneration||!ActiveVk().nativeStatus.observation||
       (ActiveVk().nativeStatus.observation->device==ActiveVk().device&&
        ActiveVk().nativeStatus.observation->frame.evaluation.featureGeneration<=*sourceRetirementGeneration))
        ActiveVk().nativeStatus={};
    ActiveVk().lastGpuFrame.reset();
    ClearFinalSource(ActiveVk());
    VulkanNrFgContributions().RevokeNewContributions(g_vkRuntime.Epoch()+1);
    ActiveVk().candidate.reset();
    RevokeVulkanNrCaptures(g_vkRuntime.Epoch()+1);
    if(!deviceAlive)AbandonVulkanNrCaptures();
    if(!deviceAlive)VulkanNrFgContributions().AbandonDevice();
    if (!deviceAlive)
    {
        if(&ActiveVk()==&g_nativeVk&&g_finalColorCarriers){g_finalColorCarriers->AbandonDevice();g_finalColorCarriers.reset();}
        if(&ActiveVk()==&g_nativeVk&&g_performancePairs){g_performancePairs->AbandonDevice();g_performancePairs.reset();}
        if (&ActiveVk() == &g_nativeVk && g_vkRuntime.Initialized())
            g_vkShutdownFailed = true;
        // No driver/provider releases on a dead device. Clear handle ownership first,
        // then free CPU wrappers, reservation metadata and observation holds normally.
        if(ActiveVk().pass)ActiveVk().pass->AbandonDevice();
        ActiveVk().pass.reset();
        if (ActiveVk().generations) ActiveVk().generations->AbandonDevice();
        ActiveVk().generations.reset(); ActiveVk().resources = nullptr; ActiveVk().reportedEncodingGeneration = 0;
        ActiveVk().pixelProbeGeneration = 0; ActiveVk().pixelProbeFrames = 0;
        ActiveVk().modelQueue = VK_NULL_HANDLE;
        ActiveVk().modelQueueFamily = UINT32_MAX;
        ActiveVk().admittedWorkload.reset(); ActiveVk().workload = {};
        ActiveVk().requestedTuning.reset();
        ActiveVk().tuningMessage = "Device abandoned; restart required";
        if (&ActiveVk() == &g_nativeVk && g_vkRuntime.Exports().shutdown != nullptr)
            g_vkRuntime.Exports().shutdown(0);
        ActiveVk().queryPool = VK_NULL_HANDLE;
        ActiveVk().querySlots.DrainReleased();ActiveVk().meterSlots.reset();
        ActiveVk().scan.Reset();ActiveVk().exposureHold.Reset();ActiveVk().automaticWhitePoint.Reset();ActiveVk().reportedAutomaticWhitePoint=false;

        for (uint32_t i = 0; i < kMeterSlots; ++i)
        {
            ActiveVk().meterReadback[i] = VK_NULL_HANDLE;
            ActiveVk().meterReadbackMemory[i] = VK_NULL_HANDLE;
            ActiveVk().meterMapped[i] = nullptr;
        }

        ActiveVk().device = VK_NULL_HANDLE;
        ActiveVk().timedFrames = 0;
        ActiveVk().meterFrames = 0;
        ActiveVk().lastGpuTime.reset();
        if (&ActiveVk() == &g_nativeVk)
            g_vkRuntime.ResetDevice();
        ++ActiveVk().diagnosticResetEpoch;ActiveVk().resetState.MarkPending();
        ActiveVk().failed = false;
        ActiveVk().reason = "";
        return;
    }

    // Full shutdown may wait for device idle; ordinary inactive maintenance
    // only retires resources whose recording/completion owners already allow it.
    if (ActiveVk().device != VK_NULL_HANDLE && VkFlight::Call(VkFlight::CallSite::DeviceIdle,reinterpret_cast<uint64_t>(ActiveVk().device),[&]{return vkDeviceWaitIdle(ActiveVk().device);}) != VK_SUCCESS)
    {
        g_vkShutdownFailed = true;
        LOG_ERROR("VK-NR shutdown drain failed; retained resources not reported as cleaned up");
        return;
    }
    // Idle is not recording release. Refresh actual owned-fence observations and
    // protect partial recordings that reserved descriptors before model creation.
    PollVkNrCompletions();
    if((ActiveVk().pass&&!ActiveVk().pass->DrainReleased())||
       !ActiveVk().querySlots.DrainReleased()||
       (ActiveVk().meterSlots&&!ActiveVk().meterSlots->DrainReleased())) {
        g_vkShutdownFailed=true;
        LOG_WARN("VK-NR shutdown retains shader/query/meter recording reservations");
        return;
    }
    if(&ActiveVk()==&g_nativeVk&&g_finalColorCarriers){if(!g_finalColorCarriers->DrainReleased()){g_vkShutdownFailed=true;return;}g_finalColorCarriers.reset();}
    if(&ActiveVk()==&g_nativeVk&&g_performancePairs){if(!g_performancePairs->DrainReleased()){g_vkShutdownFailed=true;return;}g_performancePairs.reset();}
    auto diagnostic=VkGenerationLossTrace("shutdown_drain");
    const bool generationsDrained=!ActiveVk().generations||ActiveVk().generations->DrainReleased(diagnostic?&*diagnostic:nullptr);
    if(diagnostic&&ActiveVk().generations)TraceVkGenerationLoss(*ActiveVk().generations,*diagnostic);
    if (!generationsDrained) {
        g_vkShutdownFailed = true;
        LOG_WARN("VK-NR shutdown retains {} generations ({} tracked private bytes); recording references or vendor release unresolved; opaque vendor bytes unknown",
                 ActiveVk().generations->Count(),ActiveVk().generations->PrivateBytes());
        return;
    }
    ActiveVk().generations.reset(); ActiveVk().resources = nullptr; ActiveVk().reportedEncodingGeneration = 0;
    ActiveVk().pixelProbeGeneration = 0; ActiveVk().pixelProbeFrames = 0;
    ActiveVk().modelQueue = VK_NULL_HANDLE;
    ActiveVk().modelQueueFamily = UINT32_MAX;
    ActiveVk().admittedWorkload.reset(); ActiveVk().workload = {};

    if (&ActiveVk() == &g_nativeVk && g_vkRuntime.Exports().shutdown != nullptr)
    {
        const int result = g_vkRuntime.Exports().shutdown(1);
        if (result != 1)
        {
            g_vkShutdownFailed = true;
            LOG_ERROR("DLSS-NR Vulkan: model shutdown returned 0x{:X}; further NR initialization blocked",
                      (unsigned int) result);
            return;
        }
    }

    RevokeVulkanNrCaptures(g_vkRuntime.Epoch()+1);
    if(!deviceAlive)AbandonVulkanNrCaptures();
    ActiveVk().scan.Reset();ActiveVk().exposureHold.Reset();ActiveVk().automaticWhitePoint.Reset();ActiveVk().reportedAutomaticWhitePoint=false;
    DestroyMeterReadback();
    ActiveVk().pass.reset();
    ActiveVk().requestedTuning.reset();
    ActiveVk().tuningMessage = "Waiting for NR";

    if (ActiveVk().queryPool != VK_NULL_HANDLE && ActiveVk().device != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(ActiveVk().device, ActiveVk().queryPool, nullptr);
        ActiveVk().queryPool = VK_NULL_HANDLE;
    }

    ActiveVk().timedFrames = 0;
    ActiveVk().lastGpuTime.reset();

    ActiveVk().device = VK_NULL_HANDLE;
    ActiveVk().instance = VK_NULL_HANDLE;
    ActiveVk().physicalDevice = VK_NULL_HANDLE;
    if (&ActiveVk() == &g_nativeVk)
        g_vkRuntime.ResetDevice();
    ++ActiveVk().diagnosticResetEpoch;ActiveVk().resetState.MarkPending();
    ActiveVk().failed = false;
    ActiveVk().reason = "";
    ActiveVk().frames = 0;
}

static void ShutdownVkSessionsLocked(bool deviceAlive,VkDevice device,std::unique_lock<std::mutex>& lock,std::optional<uint64_t> sourceRetirementGeneration=std::nullopt)
{
    g_vkCleanupActive=true;
    // Present can initialize the shared provider before Native has ever rendered.
    if(!g_nativeVk.device&&g_vkRuntime.Initialized())g_nativeVk.device=g_vkRuntime.Device();
    // A route root owns the composition pass borrowed by every child. The Native
    // root also owns process-wide provider shutdown, so Present must drain first.
    for(auto route:{VkNrRoute::Present,VkNrRoute::Native})for(uint32_t pass=9;pass>0;--pass) {
        if(device&&PassStateVk(route,pass).device!=device)continue;
        ScopedVkSession child(route,pass);ActiveVk().controlLock=&lock;
        ShutdownVkLocked(deviceAlive,sourceRetirementGeneration);ActiveVk().controlLock=nullptr;ActiveVk().borrowedPass=nullptr;
    }
    for(auto route:{VkNrRoute::Present,VkNrRoute::Native}) {
        if(device&&PassStateVk(route,0).device!=device)continue;
        bool dependent=false;
        if(deviceAlive) {
            for(uint32_t pass=1;pass<10;++pass)dependent|=PassStateVk(route,pass).device!=VK_NULL_HANDLE;
            if(route==VkNrRoute::Native)
                for(uint32_t pass=0;pass<10;++pass)dependent|=PassStateVk(VkNrRoute::Present,pass).device!=VK_NULL_HANDLE;
        }
        if(dependent){g_vkShutdownFailed=true;continue;}
        ScopedVkSession session(route);ActiveVk().controlLock=&lock;
        ShutdownVkLocked(deviceAlive,sourceRetirementGeneration);ActiveVk().controlLock=nullptr;
    }
    bool retained=false;
    for(auto route:{VkNrRoute::Present,VkNrRoute::Native})for(uint32_t pass=0;pass<10;++pass)
        retained|=PassStateVk(route,pass).device!=VK_NULL_HANDLE;
    if(!retained&&deviceAlive)g_vkShutdownFailed=false;
    g_vkCleanupActive=false;
    LOG_INFO("VK-NR shutdown bookkeeping: retainedStates={} generations={} trackedPrivateBytes={}; opaque vendor allocation bytes unknown",
             retained,GenerationCountVk(),GenerationBytesVk());
}

void ShutdownVk(bool deviceAlive)
{
    GetVulkanPresentExecutor().Shutdown(deviceAlive);
    std::lock_guard<std::mutex> recordLock(g_nativeRecordMutex);
    std::unique_lock<std::mutex> lock(g_vkMutex);
    g_vkSessionClosed=true;
    ShutdownVkSessionsLocked(deviceAlive,VK_NULL_HANDLE,lock);
}

bool CanYieldVulkanOutput(std::string& reason)
{
    // Do not invert recording/executor -> model lock order or run maintenance
    // from a proof query. The caller has already gated new model admission.
    if (!GetVulkanPresentExecutor().CanYieldOutput(reason) ||
        !VulkanNrRecordings().CanYieldOutput(reason)) return false;
    std::unique_lock recordLock(g_nativeRecordMutex, std::try_to_lock);
    std::unique_lock stateLock(g_vkMutex, std::try_to_lock);
    if (!recordLock.owns_lock() || !stateLock.owns_lock())
    { reason = "Vulkan model owner is busy"; return false; }
    if (g_vkShutdownFailed || g_vkCleanupActive)
    { reason = "Vulkan model retirement is incomplete or quarantined"; return false; }
    for (auto route : {VkNrRoute::Native, VkNrRoute::Present})
        for (uint32_t pass = 0; pass < 10; ++pass)
        {
            const auto& state = PassStateVk(route, pass);
            if (state.failed || state.controlLock)
            { reason = "Vulkan model pass completion is unverified"; return false; }
        }
    if (GenerationCountVk() != 0)
    { reason = "Vulkan model generations remain retained"; return false; }
    reason.clear();
    return true;
}

void RetryShutdownVk(VkDevice device)
{
    // Pool hooks also run inside private-frame and model cleanup. Never wait or
    // enter the Present executor from this callback: its mutex may already be held.
    std::unique_lock recordLock(g_nativeRecordMutex,std::try_to_lock);
    if(!recordLock.owns_lock())return;
    std::unique_lock lock(g_vkMutex,std::try_to_lock);
    if(!lock.owns_lock()||!g_vkSessionClosed||g_vkCleanupActive)return;
    ShutdownVkSessionsLocked(true,device,lock);
}

void RetireInactiveVk(VkDevice device,bool enabled,uint32_t selectedRoute)
{
    // Best effort at a render boundary: never wait for another recording or GPU
    // completion. Existing owners retain pending, replayable and consumer uses.
    std::unique_lock recordLock(g_nativeRecordMutex,std::try_to_lock);
    if(!recordLock.owns_lock())return;
    std::unique_lock lock(g_vkMutex,std::try_to_lock);
    if(!lock.owns_lock()||g_vkSessionClosed||g_vkCleanupActive||!device)return;
    g_nativeSession.PollCompletions();g_presentSession.PollCompletions();
    for(auto route:{VkNrRoute::Native,VkNrRoute::Present}) {
        const bool selected=route==VkNrRoute::Native?selectedRoute==0:(selectedRoute==1||selectedRoute==2);
        if(enabled&&selected)continue;
        auto& root=PassStateVk(route,0);
        if(root.device!=device)continue;
        ClearFinalSource(root);root.candidate.reset();root.performanceSource.reset();root.performanceUse={};
        root.nativeStatus.accepted=false;root.preSrDelivered=false;
        for(uint32_t pass=0;pass<10;++pass) {
            auto& state=PassStateVk(route,pass);
            if(state.device!=device)continue;
            RetireVkPassGenerationsLocked(route,pass,lock,"inactive_or_disabled_route");
            state.workload.appliedGeneration=0;state.workload.appliedOutput={};state.workload.appliedWork={};
            state.lastGpuTime.reset();state.lastGpuFrame.reset();
        }
    }
    // These diagnostic carrier owners share Native's release context. Final
    // color can belong to either route, so only disable it when all NR is off.
    if(g_nativeVk.device==device) {
        ScopedVkSession active(VkNrRoute::Native);
        struct ControlScope {
            VkState& state;std::unique_lock<std::mutex>* previous;
            ~ControlScope(){state.controlLock=previous;}
        } control{g_nativeVk,g_nativeVk.controlLock};
        g_nativeVk.controlLock=&lock;
        if((!enabled||selectedRoute!=0)&&g_performancePairs)g_performancePairs->DrainReleased();
    }
    if(!enabled&&g_vkRuntime.Device()==device&&g_finalColorCarriers)g_finalColorCarriers->DrainReleased();
}

void DeviceDestroyedVk(VkDevice device,uint64_t sourceRetirementGeneration)
{
    GetVulkanPresentExecutor().DeviceDestroyed(device);
    std::lock_guard recordLock(g_nativeRecordMutex);
    std::unique_lock lock(g_vkMutex);
    // The source registry was revoked before actual destruction. Preserve a
    // replacement registered at the same address while that call returned.
    if(g_nativeRawVk.observation&&
       g_nativeRawVk.observation->frame.evaluation.featureGeneration<=sourceRetirementGeneration)
        g_nativeRawVk.InvalidateDevice(device);
    if(g_nativeVk.nativeStatus.observation&&g_nativeVk.nativeStatus.observation->device==device&&
       g_nativeVk.nativeStatus.observation->frame.evaluation.featureGeneration<=sourceRetirementGeneration)
        g_nativeVk.nativeStatus={};
    bool matches=false;
    for(auto route:{VkNrRoute::Present,VkNrRoute::Native})for(uint32_t pass=0;pass<10;++pass)
        matches|=PassStateVk(route,pass).device==device;
    if(!matches)return;
    g_vkSessionClosed=true;
    ShutdownVkSessionsLocked(false,device,lock,sourceRetirementGeneration);
}

void NotifyDeviceInitVk()
{
    GetVulkanPresentExecutor().NotifyDeviceInit();
    std::lock_guard<std::mutex> lock(g_vkMutex);
    if (!g_vkShutdownFailed&&!g_vkCleanupActive)
        g_vkSessionClosed = false;
}

} // namespace DlssNr
