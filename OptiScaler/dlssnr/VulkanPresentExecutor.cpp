#include "VulkanPresentExecutor.h"
#include "VulkanNrCompletion.h"
#include "VulkanPresentConsumer.h"
#include "DlssNrFeature_Vk.h"
#include "VulkanPresentStatus.h"
#include "VulkanPresentGuidesVk.h"
#include "FinalFallbackControl.h"
#include <Config.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace DlssNr
{
namespace
{
static std::atomic<uint32_t> vkPresentGuardReceipts{0};
static bool VkPresentGuardTraceEnabled() noexcept
{
    return !loggerClosed.load(std::memory_order_relaxed)&&spdlog::should_log(spdlog::level::trace)&&
        vkPresentGuardReceipts.load(std::memory_order_relaxed)<32;
}
static bool VkPresentGuardFailed(bool failed,const char* predicate,VkNrPresentGuardObservation* observation) noexcept
{
    if(failed&&observation&&!observation->predicate)observation->predicate=predicate;
    return failed;
}
static void TraceVkPresentGuard(const VkNrPresentGuardObservation* observation,VkNrUseId use,
    uint32_t originLine,const char* reason,const char* disposition,bool providerKnown,bool providerAttempted,bool providerNoExternal,
    const std::optional<VkNrRecordingOwner::UseRecordingSnapshot>& before) noexcept
{
    if(!VkPresentGuardTraceEnabled())return;
    auto receipt=vkPresentGuardReceipts.load(std::memory_order_relaxed);
    while(receipt<32&&!vkPresentGuardReceipts.compare_exchange_weak(receipt,receipt+1,std::memory_order_relaxed)){}
    if(receipt>=32)return;
    try{
        const auto expected=observation?observation->expectedModel:std::nullopt;
        const auto actual=observation?observation->actualModel:std::nullopt;
        LOG_TRACE("Vulkan Present first-guard origin: receipt={} thread={} sourceLine={} use={} predicate={} getter={} preferenceKnown={} expectedPreference={} actualPreference={} expectedModelKnown={} actualModelKnown={} expectedDevice={} actualDevice={} expectedGeneration={} actualGeneration={} expectedRouteEpoch={} actualRouteEpoch={} expectedResume={} actualResume={} expectedInputEpoch={} actualInputEpoch={} applicationProviderKnown={} applicationProviderAttempted={} applicationProviderNoExternal={} preActionKnown={} preActionIncarnation={} preActionSubmitted={} preActionPending={} disposition={} reason=[{}]",
            receipt+1,GetCurrentThreadId(),originLine,use.value,observation&&observation->predicate?observation->predicate:"not_visited",
            observation?static_cast<unsigned>(observation->read):static_cast<unsigned>(VkNrPresentLifetimeRead::NotVisited),
            observation&&observation->preferenceKnown,observation?observation->expectedPreference:0,observation?observation->actualPreference:0,
            expected.has_value(),actual.has_value(),expected?expected->device:0,actual?actual->device:0,
            expected?expected->deviceGeneration:0,actual?actual->deviceGeneration:0,
            expected?expected->routeEpoch:0,actual?actual->routeEpoch:0,expected?expected->resumeGeneration:0,actual?actual->resumeGeneration:0,
            expected?expected->inputInterruptionEpoch:0,actual?actual->inputInterruptionEpoch:0,
            providerKnown,providerAttempted,providerNoExternal,before.has_value(),before?before->incarnation:0,before?before->submitted:false,before?before->pending:0,
            disposition,reason?reason:"none");
    }catch(...){/* Diagnostic projection never changes refusal/discard behavior. */}
}

constexpr size_t kMaximumSlots = 8;
// Small WSI-only records remain quarantined when the game provides no explicit
// replacement relation. They do not retain images or consume active frame slots.
constexpr size_t kMaximumPresentationRecords = 128;
constexpr uint64_t kMaximumPrivateBytes = 1536ull * 1024ull * 1024ull;
}

VulkanPresentExecutor::Slot* VulkanPresentExecutor::Find(const VkObservedPresent& observed)
{
    for (auto& slot : slots_)
        if (slot->frame && slot->applicationFacing==observed.applicationFacing && slot->generation == observed.request.swapchainGeneration &&
            slot->swapchain == observed.swapchain && slot->imageIndex == observed.request.imageIndex &&
            slot->device == observed.device && slot->deviceGeneration == observed.request.deviceGeneration)
            return slot.get();
    return nullptr;
}

VulkanPresentExecutor::Slot* VulkanPresentExecutor::Create(const VkObservedPresent& observed, VkInstance instance)
{
    // Preflight estimate; the allocation total is checked again against driver-reported sizes.
    const uint64_t bytes = uint64_t(observed.request.extent.width) * observed.request.extent.height * 32;
    const auto modelBytes = PresentGenerationPrivateBytesVk();
    createRefusal_ = "Vulkan Present private resource budget exhausted";
    const auto active = std::count_if(slots_.begin(), slots_.end(), [](const auto& slot) { return bool(slot->frame); });
    if (static_cast<size_t>(active) >= kMaximumSlots)
    {
        createRefusal_ = "Vulkan Present private frame slots await owned GPU completion";
        return nullptr;
    }
    if (retainedBytes_ > kMaximumPrivateBytes ||
        modelBytes > kMaximumPrivateBytes - retainedBytes_ ||
        bytes > kMaximumPrivateBytes - retainedBytes_ - modelBytes)
        return nullptr;
    if (slots_.size() >= kMaximumPresentationRecords)
    {
        createRefusal_ = "Vulkan Present unresolved presentation semaphore capacity exhausted; restart required";
        return nullptr;
    }
    auto slot = std::make_unique<Slot>();
    slot->applicationFacing=observed.applicationFacing;
    slot->ticket = ++nextTicket_;
    slot->generation = observed.request.swapchainGeneration;
    slot->deviceGeneration = observed.request.deviceGeneration;
    slot->swapchain = observed.swapchain;
    slot->imageIndex = observed.request.imageIndex;
    slot->device = observed.device;
    slot->queue = observed.queue;
    slot->estimatedBytes = bytes;
    slot->frame = std::make_unique<VkPresentPrivateFrame>();
    VkPresentImageRequest image {};
    image.image = observed.image;
    image.format = observed.request.format;
    image.colorSpace = observed.request.colorSpace;
    image.extent = observed.request.extent;
    image.generation = observed.request.swapchainGeneration;
    createRefusal_ = "Vulkan Present private frame allocation or format features unavailable";
    if (!slot->frame->Initialize(instance, observed.physicalDevice, observed.device,
                                 observed.queueFamily, image))
        return nullptr;
    slot->estimatedBytes = slot->frame->AllocatedBytes();
    createRefusal_ = "Vulkan Present driver allocation exceeds private resource budget";
    if (slot->estimatedBytes > kMaximumPrivateBytes - retainedBytes_ - modelBytes)
        return nullptr;
    VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    createRefusal_ = "Vulkan Present semaphore or fence allocation failed";
    VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateSemaphore(observed.device, &semaphore, nullptr, &slot->signal) != VK_SUCCESS ||
        vkCreateFence(observed.device, &fence, nullptr, &slot->fence) != VK_SUCCESS)
    {
        Destroy(*slot, true);
        return nullptr;
    }
    auto* result = slot.get();
    retainedBytes_ += slot->estimatedBytes;
    slots_.push_back(std::move(slot));
    return result;
}

void VulkanPresentExecutor::Destroy(Slot& slot, bool deviceAlive)
{
    slot.consumer.reset();
    slot.acquireProof.reset();
    if (slot.frame) slot.frame->Release(deviceAlive);
    if (deviceAlive && slot.device != VK_NULL_HANDLE)
    {
        if (slot.signal != VK_NULL_HANDLE) vkDestroySemaphore(slot.device, slot.signal, nullptr);
        if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(slot.device, slot.fence, nullptr);
    }
    slot.signal = VK_NULL_HANDLE;
    slot.fence = VK_NULL_HANDLE;
}

void VulkanPresentExecutor::ObserveSlotCompletion(Slot& slot,bool countCompletion)
{
    if(slot.completionObserved)return;
    slot.completionObserved=true;
    if(countCompletion)GetVulkanPresentStatus().GpuCompleted();
    const auto& receipt=slot.sources;
    if(receipt.ticket==slot.ticket&&receipt.acquisition==slot.acquisition&&
       receipt.imageIndex==slot.imageIndex&&receipt.context.device==reinterpret_cast<uintptr_t>(slot.device)&&
       receipt.context.queue==reinterpret_cast<uintptr_t>(slot.queue)&&
       receipt.context.swapchain==reinterpret_cast<uintptr_t>(slot.swapchain)&&
       receipt.context.deviceGeneration==slot.deviceGeneration&&receipt.context.swapchainGeneration==slot.generation)
        GetVulkanPresentStatus().SourceCompleted(receipt,Connections::SignalPreferences().revision);
}
void VulkanPresentExecutor::InvalidateSourceHistory()
{
    sourceContext_.reset();sourcePolicyRevision_=0;
    lastSourceInput_=PresentInputDecision::InputClass::Refused;
    compositionGate_.MarkHistoryDiscontinuous();
    GetVulkanPresentStatus().InvalidateSources();
}

void VulkanPresentExecutor::Maintain(const VkObservedPresent& observed, bool inactive)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || closed_) return;
    if(initialization_.Device()) {
        initialization_.Maintain(PollPresentInitializationVk);
        if(inactive)initialization_.Release(true,PollPresentInitializationVk);
    }
    auto& registry = observed.applicationFacing?GetVulkanApplicationPresentRegistry():GetVulkanPresentRegistry();
    const auto candidate = registry.AcquireProof(observed.swapchain, observed.request.imageIndex,
                                                observed.request.acquireGeneration);
    for (auto i = slots_.begin(); i != slots_.end();)
    {
        auto& slot = **i;
        // Inactive output must drain its own completed work on every queue of
        // this device, including the queue used before FG changed the boundary.
        if (slot.device != observed.device || (!inactive && slot.queue != observed.queue)) { ++i; continue; }
        const bool retireInactive=inactive||slot.applicationFacing!=observed.applicationFacing;
        if (slot.queue == observed.queue && !slot.applicationFacing && !observed.applicationFacing && !slot.acquireProof && slot.state.Presented() && candidate && candidate->device == slot.device &&
            candidate->deviceGeneration == slot.deviceGeneration)
        {
            const bool sameImage = slot.swapchain == candidate->swapchain && slot.generation == candidate->swapchainGeneration &&
                slot.imageIndex == candidate->imageIndex && candidate->acquireGeneration > slot.acquisition;
            // Both serials here belong to the semantic registry, including NR-off
            // Presents. The executor's own submission serial is a different domain.
            const bool successor = slot.registryPresentSerial && candidate->priorPresentSerial > slot.registryPresentSerial &&
                candidate->priorPresentQueue == slot.queue &&
                std::find(candidate->predecessorGenerations.begin(),candidate->predecessorGenerations.end(),slot.generation) != candidate->predecessorGenerations.end();
            if (sameImage || successor) slot.acquireProof = candidate;
        }
        const bool complete = !slot.state.Pending() || vkGetFenceStatus(slot.device, slot.fence) == VK_SUCCESS;
        if (complete && slot.state.Pending())
        {
            compositionGate_.ObserveCompletion(slot.ticket, true);
            ObserveSlotCompletion(slot);
        }
        if (slot.applicationFacing ? (slot.consumer && slot.consumer->Complete()) :
            (slot.acquireProof && slot.acquireProof->Complete())) slot.retirementProven = true;
        if (complete && (!slot.state.Pending() || (slot.state.PresentWaitEnqueued() && slot.retirementProven)) &&
            (retireInactive || !slot.frame))
        {
            Destroy(slot, true); retainedBytes_ -= slot.estimatedBytes; i = slots_.erase(i); continue;
        }
        if (retireInactive && slot.frame && slot.state.CanReleasePrivateFrame(complete))
        {
            slot.frame->Release(true); slot.frame.reset();
            retainedBytes_ -= slot.estimatedBytes; slot.estimatedBytes = 0;
        }
        ++i;
    }
    if (inactive) InvalidateSourceHistory();
}

void VulkanPresentExecutor::ReleaseCompletedSupersededFrames(const VkObservedPresent& observed)
{
    uint64_t released = 0;
    for (auto& slot : slots_)
    {
        if (!slot->frame || slot->applicationFacing!=observed.applicationFacing || slot->device != observed.device || slot->queue != observed.queue ||
            slot->generation >= observed.request.swapchainGeneration) continue;
        const bool complete = !slot->state.Pending() || vkGetFenceStatus(slot->device, slot->fence) == VK_SUCCESS;
        if (!slot->state.CanReleasePrivateFrame(complete)) continue;
        if (slot->state.Pending() && !slot->completionObserved)
        { ObserveSlotCompletion(*slot); }
        // Destroy the owned pool first, ending replay rights and generation
        // references, then private images. WSI sees only slot.signal and the
        // game's swapchain image; both remain untouched here.
        slot->frame->Release(true);
        slot->frame.reset();
        released += slot->estimatedBytes;
        retainedBytes_ -= slot->estimatedBytes;
        slot->estimatedBytes = 0;
    }
    if (released) LOG_INFO("Vulkan Present released {} completed superseded private bytes; retained={} WSI records={}",
                           released, retainedBytes_, slots_.size());
}

void VulkanPresentExecutor::RetirePredecessors(const VkObservedPresent& observed, uint64_t successorPresentSerial)
{
    // Called only after a successful successor Present's image was reacquired
    // AND its acquire wait completed. Khronos's swapchain-recreation sample uses
    // this boundary to retire earlier Present waits in an explicit replacement chain.
    for (auto i = slots_.begin(); i != slots_.end();)
    {
        auto& slot = **i;
        const bool predecessor = std::find(observed.predecessorGenerations.begin(),
            observed.predecessorGenerations.end(), slot.generation) != observed.predecessorGenerations.end();
        // Already-acquired retired images can legally be presented after the
        // successor starts. Only waits preceding THIS proved successor Present
        // can retire here, rather than every wait of an older generation.
        if (slot.applicationFacing || observed.applicationFacing || !predecessor || !slot.presentSerial || slot.presentSerial >= successorPresentSerial ||
            slot.device != observed.device || slot.queue != observed.queue ||
            !slot.state.PresentWaitEnqueued() || vkGetFenceStatus(slot.device, slot.fence) != VK_SUCCESS)
        { ++i; continue; }
        Destroy(slot, true);
        retainedBytes_ -= slot.estimatedBytes;
        i = slots_.erase(i);
    }
}

VkPresentExecution VulkanPresentExecutor::BeforePresent(const VkObservedPresent& observed,
                                                        VkInstance instance, const VkPresentInfoKHR& original,
                                                        std::shared_ptr<const NrConfigSnapshot<Config>> settings,
                                                        std::shared_ptr<const VkNrGuideSelection> guideSelection,
                                                        std::shared_ptr<const VkNrWaitBudget> waitBudget,
                                                        const std::function<VkPresentProviderPreparation(uint64_t,const VkNrPresentModelLifetime&)>& prepareProvider,
                                                        std::shared_ptr<const VkCapturedGuideInput> capturedGuides)
{
    if (!waitBudget) waitBudget = std::make_shared<VkNrWaitBudget>();
    const auto sourcePolicy=Connections::SignalPreferences();
    std::lock_guard<std::mutex> lock(mutex_);
    VkPresentExecution result {};
    if(initialization_.Device())initialization_.Maintain(PollPresentInitializationVk);
    if(closed_){result.reason="Vulkan Present session closed with unresolved presentation ownership; restart required";return result;}
    // Admission and early fence refusals skip a selected real frame. Keep its
    // outstanding ticket/fence owned and request a reset on the next recording.
    // While application frames own history, physical MFG/provider-unknown
    // presentations intentionally bypass NR and must not reset that history.
    struct SkippedPresentHistory
    {
        VkPresentCompositionGate& gate;
        bool selectedBoundary;
        bool submitted = false;
        ~SkippedPresentHistory() { if (selectedBoundary && !submitted) gate.MarkHistoryDiscontinuous(); }
    } history { compositionGate_, observed.applicationFacing || !lastApplicationFacing_.value_or(false) ||
        (observed.request.fgStateKnown && !observed.request.fgKnownActive) };
    const auto reportSlowStage = [&](const char* stage, VkNrWaitBudget::Clock::time_point start) {
        const auto now = VkNrWaitBudget::Clock::now();
        const auto elapsedMs = std::chrono::duration<double,std::milli>(now-start).count();
        if (elapsedMs <= 100.0) return;
        static std::atomic<int64_t> lastReportMs { 0 };
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        auto previous = lastReportMs.load(std::memory_order_relaxed);
        if (nowMs-previous < 1000 || !lastReportMs.compare_exchange_strong(previous,nowMs,std::memory_order_relaxed)) return;
        LOG_INFO("Vulkan Present slow host stage: stage={} elapsedMs={:.3f} generation={} queue={}",
            stage,elapsedMs,observed.request.swapchainGeneration,reinterpret_cast<uintptr_t>(observed.queue));
    };
    if (!settings) {
        const auto captured = TryNrConfigSnapshot(*Config::Instance());
        if (!captured) { result.reason = "Vulkan Present configuration capture unavailable"; return result; }
        settings = std::make_shared<NrConfigSnapshot<Config>>(*captured);
    }
    const auto guideFrame=MakeVulkanGuidePresentContract(observed,guideSelection.get());
    VkPresentSources::Context sourceContext{
        reinterpret_cast<uintptr_t>(observed.device),reinterpret_cast<uintptr_t>(observed.queue),
        reinterpret_cast<uintptr_t>(observed.swapchain),observed.request.deviceGeneration,
        observed.request.swapchainGeneration,0,observed.request.route,observed.applicationFacing};
    sourceContext.modelLifetime=CurrentVulkanPresentModelLifetimeVk();
    if(sourceContext.modelLifetime)sourceContext.routeEpoch=sourceContext.modelLifetime->routeEpoch;
    if(history.selectedBoundary){
        if(!sourceContext_||*sourceContext_!=sourceContext||sourcePolicyRevision_!=sourcePolicy.revision)
            compositionGate_.MarkHistoryDiscontinuous();
        sourceContext_=sourceContext;sourcePolicyRevision_=sourcePolicy.revision;
        if(nextSourceCapture_==UINT64_MAX){
            result.reason="Vulkan Present source receipt timeline exhausted; restart required";return result;
        }
        result.sources.signals.capture=++nextSourceCapture_;
        GetVulkanPresentStatus().ObserveSourcePolicy(sourcePolicy,sourceContext,result.sources.signals.capture);
    }
    result.sources.context=sourceContext;
    if(capturedGuides&&(!capturedGuides->lease||!capturedGuides->bind||
        !capturedGuides->identity.Matches(reinterpret_cast<uintptr_t>(observed.device),guideFrame.deviceGeneration,
          reinterpret_cast<uintptr_t>(observed.swapchain),guideFrame.swapchainGeneration,guideFrame.acquireGeneration,
          reinterpret_cast<uintptr_t>(observed.image),guideFrame.output.width,guideFrame.output.height))) {
        result.reason="Captured Vulkan guides do not match this completed acquisition";return result;
    }
    if(observed.request.policy==PresentInput::Policy::Invalid){
        result.reason="Vulkan Present input policy is invalid. Image unchanged.";return result;
    }
    auto guides = capturedGuides ? std::optional<VkNrGuideLease>{} : SelectVulkanPresentGuides(guideFrame,guideSelection.get(),observed.image);
    const auto metadata = guides ? std::optional<VkNrFrameContract>(guides->contract) :
        SelectVulkanResolutionMetadata(guideFrame,guideSelection.get());
    const auto scalarSize=observed.renderSize.value;
    const auto sourcePlan=VkPresentSources::Plan(sourcePolicy,
        capturedGuides?VkPresentSources::GuideKind::Captured:
            guides?VkPresentSources::GuideKind::Native:VkPresentSources::GuideKind::None,
        capturedGuides?PresentInput::Policy::RequireGuides:observed.request.policy,
        scalarSize.has_value()||metadata.has_value(),PresentResolution::Selected(*settings));
    const auto capture=result.sources.signals.capture;
    result.sources.signals=sourcePlan.sources.receipt;
    result.sources.signals.capture=capture;
    result.sources.signals.generation=observed.request.swapchainGeneration;
    if(!sourcePlan.allowed){
        result.reason=sourcePlan.sources.reason?sourcePlan.sources.reason:
            sourcePlan.input.reason?sourcePlan.input.reason:"Vulkan Present input sources refused";
        return result;
    }
    if(!sourcePlan.useNativeGuides)guides.reset();
    const auto input=sourcePlan.input;
    if(PresentInputDecision::ChangesHistory(lastSourceInput_,input.input))
        compositionGate_.MarkHistoryDiscontinuous();
    const auto requestedResolution=PresentResolution::Selected(*settings);
    const uint32_t nativeW=scalarSize?scalarSize->extent.width:metadata?metadata->temporal->color.width:0;
    const uint32_t nativeH=scalarSize?scalarSize->extent.height:metadata?metadata->temporal->color.height:0;
    const std::array<uint64_t,9> resolutionKey{observed.request.swapchainGeneration,
        requestedResolution.mode,requestedResolution.scale,nativeW,nativeH,
        input.resolution.mode,input.resolution.scale,uint64_t(guides.has_value()||bool(capturedGuides)),uint64_t(input.workloadFallback)};
    static std::optional<std::array<uint64_t,9>> lastResolution;
    static std::string lastSizeReason;
    if(lastResolution!=resolutionKey||lastSizeReason!=observed.renderSize.reason) {
        lastResolution=resolutionKey;
        lastSizeReason=observed.renderSize.reason;
        LOG_INFO("Vulkan Present resolution: requestedMode={} requestedScale={} nativeRender={}x{} effectiveMode={} effectiveScale={} guides={} workloadFallback={} reason=[{}] sizeObservation=[{}]",
            requestedResolution.mode,requestedResolution.scale,nativeW,nativeH,input.resolution.mode,input.resolution.scale,
            guides.has_value()||bool(capturedGuides),input.workloadFallback,input.reason?input.reason:"",observed.renderSize.reason);
    }
    auto capabilities = observed.capabilities; capabilities.guidesQualified = sourcePlan.useNativeGuides||sourcePlan.useCapturedGuides;
    capabilities.applicationBeforeProvider=observed.applicationFacing&&bool(prepareProvider);
    auto admissionRequest=observed.request;
    admissionRequest.policy=sourcePlan.admissionPolicy;
    result.admission = DecideVkPresent(admissionRequest, capabilities);
    result.reason = VkPresentRefusalText(result.admission.reason);
    if (!result.admission.allowed) return result;
    if (input.input == PresentInputDecision::InputClass::Refused) {
        result.admission.allowed=false;result.admission.actualInputClass=input.input;result.reason=input.reason;return result;
    }
    result.admission.actualInputClass = input.input;
    if (!IsVkPresentWaitReplacementChainKnown(original) || original.waitSemaphoreCount > 64 ||
        (original.waitSemaphoreCount > 0 && original.pWaitSemaphores == nullptr))
    {
        result.reason = "Vulkan Present wait or extension chain is unsupported";
        return result;
    }
    if(lastApplicationFacing_&&*lastApplicationFacing_!=observed.applicationFacing)compositionGate_.MarkHistoryDiscontinuous();
    if (!compositionGate_.CanRecord())
    {
        const auto ticket = compositionGate_.Ticket();
        const auto previous = std::find_if(slots_.begin(), slots_.end(),
            [ticket](const auto& s) { return s->ticket == ticket; });
        if (previous == slots_.end())
        {
            result.reason = "Vulkan Present composition completion owner unavailable";
            return result;
        }
        const auto requestedWaitNs = waitBudget->RemainingNs();
        const auto waitStart = VkNrWaitBudget::Clock::now();
        const auto fence = PollOrWaitVkPresentFence((*previous)->device, (*previous)->fence,
                                                   vkGetFenceStatus, vkWaitForFences, requestedWaitNs);
        if (fence == VK_SUCCESS)
        {
            if (!(*previous)->completionObserved)
            {
                ObserveSlotCompletion(**previous);
            }
            compositionGate_.ObserveCompletion(ticket, true);
        }
        else
        {
            if (fence == VK_TIMEOUT || fence == VK_NOT_READY)
            {
                const auto waitEnd = VkNrWaitBudget::Clock::now();
                const auto remainingWaitNs = waitBudget->RemainingNs();
                const auto spentBudgetNs = requestedWaitNs - (std::min)(requestedWaitNs, remainingWaitNs);
                static std::atomic<uint64_t> timeoutCount { 0 };
                const auto count = timeoutCount.fetch_add(1, std::memory_order_relaxed) + 1;
                static std::atomic<int64_t> lastReportMs { 0 };
                const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd.time_since_epoch()).count();
                auto previousReportMs = lastReportMs.load(std::memory_order_relaxed);
                if (nowMs - previousReportMs >= 1000 &&
                    lastReportMs.compare_exchange_strong(previousReportMs, nowMs, std::memory_order_relaxed))
                    LOG_INFO("Vulkan Present composition wait diagnostic: timeouts={} ticket={} "
                        "requestedWaitNs={} spentBudgetNs={} remainingBudgetNs={} hostWaitMs={:.3f} "
                        "previousGeneration={} currentGeneration={} image={} nativeRender={}x{} output={}x{} "
                        "applicationFacing={} previousApplicationFacing={} fgStateKnown={} fgKnownActive={} "
                        "result={} (host wait is not GPU execution time)",
                        count, ticket, requestedWaitNs, spentBudgetNs, remainingWaitNs,
                        std::chrono::duration<double, std::milli>(waitEnd - waitStart).count(),
                        (*previous)->generation, observed.request.swapchainGeneration, observed.request.imageIndex,
                        nativeW, nativeH, observed.request.extent.width, observed.request.extent.height,
                        observed.applicationFacing, (*previous)->applicationFacing,
                        observed.request.fgStateKnown, observed.request.fgKnownActive, static_cast<int>(fence));
            }
            if (fence != VK_TIMEOUT && fence != VK_NOT_READY){
                InvalidateSourceHistory();
                MarkPresentRuntimeUncertainVk("Vulkan Present composition fence failed; restart required");
            }
            result.reason = fence == VK_TIMEOUT || fence == VK_NOT_READY ?
                "Vulkan Present composition wait timed out; image unchanged" :
                "Vulkan Present composition fence failed; restart required";
            return result;
        }
    }
    if (modelQueue_ != VK_NULL_HANDLE && observed.queue != modelQueue_)
    {
        // Native Vulkan FG can move application Present to another queue. A
        // completed model fence permits retiring private recordings/images,
        // independently of WSI/provider consumption of their binary signals.
        // Prove every old recording before releasing any; uncertain submissions
        // remain quarantined and never grant a queue transition.
        for (const auto& previous : slots_)
        {
            if (!previous->frame) continue;
            const bool complete = !previous->state.Pending() ||
                vkGetFenceStatus(previous->device, previous->fence) == VK_SUCCESS;
            if (previous->device != observed.device || !previous->state.CanReleasePrivateFrame(complete))
            {
                result.reason = "Vulkan Present queue transition awaits owned GPU completion";
                return result;
            }
        }
        for (auto previous = slots_.begin(); previous != slots_.end();)
        {
            auto& slot = **previous;
            if (slot.frame)
            {
                slot.frame->Release(true);
                slot.frame.reset();
                retainedBytes_ -= slot.estimatedBytes;
                slot.estimatedBytes = 0;
            }
            // A refused recording never handed its fresh semaphore to anyone.
            // Remove it rather than accumulating one record per drain retry.
            if (!slot.state.Pending())
            {
                Destroy(slot, true);
                previous = slots_.erase(previous);
            }
            else ++previous;
        }
        compositionGate_.MarkHistoryDiscontinuous();
        // Signals and consumer receipts remain with their existing slots. The
        // model's auxiliary owners perform their own drain before recording.
    }
    ReleaseCompletedSupersededFrames(observed);
    auto* slot = Find(observed);
    if (!slot) slot = Create(observed, instance);
    if (!slot)
    {
        result.reason = createRefusal_;
        return result;
    }
    bool fenceReady = false;
    if (slot->state.Pending())
    {
        const auto fence = vkGetFenceStatus(slot->device, slot->fence);
        fenceReady = fence == VK_SUCCESS;
        if (fenceReady && !slot->completionObserved)
        {
            ObserveSlotCompletion(*slot);
        }
        if (fence == VK_ERROR_DEVICE_LOST) {slot->state.Uncertain();InvalidateSourceHistory();}
    }
    bool acquireConsumed=!slot->state.Pending();
    if(slot->state.Pending()&&slot->applicationFacing){
        acquireConsumed=slot->consumer&&slot->consumer->Complete();
    }else if(slot->state.Pending()){
        const auto proof=GetVulkanPresentRegistry().AcquireProof(observed.swapchain,observed.request.imageIndex,
                                                               observed.request.acquireGeneration);
        acquireConsumed=proof&&(proof->Complete() || (proof->use&&WaitVkNrUse(proof->use,waitBudget->RemainingNs())==VK_SUCCESS));
    }
    if (!slot->state.CanRecord(observed.request.acquiredObserved, fenceReady,acquireConsumed))
    {
        result.reason = "Vulkan Present image or prior GPU work is not retired";
        return result;
    }
    if (slot->state.Pending()) {
        slot->retirementProven = true;
        RetirePredecessors(observed, slot->presentSerial);
        // CanRecord proved the old GPU and presentation-consumption obligations
        // for both physical and application-facing slots. Consume that old cycle
        // before resetting its fence; a refused new recording is not pending work.
        slot->state={};slot->consumer.reset();slot->acquireProof.reset();
        slot->retirementProven=false;
    }

    VkPresentImageRequest image {};
    image.waitBudget = waitBudget;
    image.image = observed.image;
    image.format = observed.request.format;
    image.colorSpace = observed.request.colorSpace;
    image.extent = observed.request.extent;
    image.generation = observed.request.swapchainGeneration;image.captureOnly=settings->IsPrivateDiagnostic();
    image.commandBuffer = slot->frame->CommandBuffer();
    image.privateAllocationBudget = kMaximumPrivateBytes - retainedBytes_;
    image.frame = guideFrame; image.guides = guides; image.inputDecision = input; image.settings = settings;
    image.capturedGuides = std::move(capturedGuides);
    if(scalarSize)image.observedRenderSize=scalarSize->extent;
    if (metadata) image.frame.temporal = metadata->temporal;
    const bool sameFrameInit=VkPresentInitializationEligible(settings->DlssNrVulkanPresentSameFrameInit.value_or_default(),
        VkNrRoute::Present,VkNrPlacement::AfterSR,requestedResolution.mode,input.resolution.mode,
        settings->DlssNrHoldFrame.value_or_default(),false,image.captureOnly);
    std::function<bool()> currentInitialization;
    std::optional<VkNrPresentModelLifetime> expectedInitializationModel;
    std::optional<VkNrPresentGuardObservation> initializationObservation;
    VkNrUseId initializationDiagnosticUse;
    if(sameFrameInit) {
        std::string preparationReason;
        auto request=slot->frame->InitializationRequest(image,preparationReason);
        if(!request){result.reason=preparationReason;return result;}
        const auto epoch=FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst);
        expectedInitializationModel=CurrentVulkanPresentModelLifetimeVk();
        auto& registry=observed.applicationFacing?GetVulkanApplicationPresentRegistry():GetVulkanPresentRegistry();
        const auto acquire=registry.AcquireProof(observed.swapchain,observed.request.imageIndex,observed.request.acquireGeneration);
        currentInitialization=[&,epoch,acquire,registryPointer=&registry] {
            initializationObservation.reset();if(VkPresentGuardTraceEnabled())initializationObservation.emplace();
            auto* projection=initializationObservation?&*initializationObservation:nullptr;
            const auto failed=[&](bool value,const char* predicate){return VkPresentGuardFailed(value,predicate,projection);};
            const auto preferenceChanged=[&]{
                const auto revision=Connections::SignalPreferences().revision;
                const bool changed=!VkPresentSources::CanSubmit(sourcePlan,revision);
                if(changed&&projection){projection->preferenceKnown=true;projection->expectedPreference=sourcePlan.sources.receipt.revision;projection->actualPreference=revision;}
                return changed;
            };
            const auto modelChanged=[&]{
                const auto actual=CurrentVulkanPresentModelLifetimeVk(projection?&projection->read:nullptr);
                const bool changed=actual!=expectedInitializationModel;
                if(changed&&projection){projection->expectedModel=expectedInitializationModel;projection->actualModel=actual;}
                return changed;
            };
            const auto live=TryNrConfigSnapshot(*Config::Instance());
            if(failed(!live,"initializer_config_unavailable")||
                failed(live->ObservationRevision()!=settings->ObservationRevision(),"initializer_settings_revision")||
                failed(!live->GetDlssNrRuntimeSnapshot().enabled,"initializer_disabled")||
                failed(FinalFallback::inputInterruptionEpoch.load(std::memory_order_seq_cst)!=epoch,"initializer_input_epoch")||
                failed(FinalFallback::suppressInGame.load(std::memory_order_seq_cst),"initializer_suppressed")||
                failed(FinalFallback::inputSwitchPending.load(std::memory_order_seq_cst),"initializer_switch_pending")||
                failed(preferenceChanged(),"initializer_input_preference")||
                failed(modelChanged(),"initializer_model")||
                failed(VkNrCompletionDeviceGeneration(observed.device)!=observed.request.deviceGeneration,"initializer_device_generation")||
                failed(!VkNrRecordingQueueContext(observed.device,observed.queueFamily,observed.queue),"initializer_queue_context")||
                failed(!acquire,"initializer_acquire_missing")||
                failed(acquire->device!=observed.device,"initializer_acquire_device")||
                failed(acquire->deviceGeneration!=observed.request.deviceGeneration,"initializer_acquire_device_generation")||
                failed(acquire->swapchainGeneration!=observed.request.swapchainGeneration,"initializer_swapchain_generation")||
                failed(registryPointer->AcquireProof(observed.swapchain,observed.request.imageIndex,observed.request.acquireGeneration)!=acquire,"initializer_acquire_changed")||
                failed(!registryPointer->IsSwapchainImage(observed.device,observed.image),"initializer_swapchain_image")){
                if(projection)TraceVkPresentGuard(projection,initializationDiagnosticUse,__LINE__,"initialization currentness refused","initializer_guard_false",false,false,false,{});
                return false;
            }
            if(guides) {
                if(failed(!CurrentVulkanPresentGuideLease(guideFrame,guideSelection.get(),observed.image,*guides),"initializer_guide_lease")){
                    if(projection)TraceVkPresentGuard(projection,initializationDiagnosticUse,__LINE__,"initialization currentness refused","initializer_guard_false",false,false,false,{});
                    return false;
                }
            }
            return true;
        };
        const auto initialized=initialization_.Prepare(*request,currentInitialization,
            [&](const VkPresentInitializationRequest& preparation,VkCommandBuffer command,VkNrUseId use) {
                if(VkPresentGuardTraceEnabled())initializationDiagnosticUse=use;
                const auto step=PreparePresentModelsVk(preparation,command,use);
                if(step.modelLifetime)expectedInitializationModel=step.modelLifetime;
                return step;
            },PollPresentInitializationVk);
        if(initialized.submissions||initialized.status!=VkPresentInitializationStatus::Ready) {
            static std::atomic<uint64_t> reports{0};const auto n=++reports;
            if(n<=12||n%120==0)LOG_INFO("Vulkan Present same-frame initialization test: status={} submissions={} hostCreateMs={:.3f} ownedWaitMs={:.3f} reason=[{}]",
                static_cast<unsigned>(initialized.status),initialized.submissions,initialized.creationMs,initialized.waitMs,initialized.reason);
        }
        if(initialized.status!=VkPresentInitializationStatus::Ready||!currentInitialization()||!waitBudget->RemainingNs()) {
            result.reason=initialized.reason.empty()?"Vulkan same-frame initialization frame expired":initialized.reason;
            if(initialized.status==VkPresentInitializationStatus::Uncertain) {
                closed_=true;slot->state.Uncertain();result.handoff.state=VkPresentHandoffState::Uncertain;
                result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;InvalidateSourceHistory();
                MarkPresentRuntimeUncertainVk(result.reason.c_str());
            }
            return result;
        }
        image.initializationCompleted=true;
    } else if(initialization_.Device())initialization_.Release(true,PollPresentInitializationVk);
    // Slot recycling keeps history. A new swapchain or a skipped selected frame
    // resets it; the session also owns route/enable resets.
    const auto recordStart = VkNrWaitBudget::Clock::now();
    result.record = RecordVkPresentImage(image, *slot->frame,
        compositionGate_.NeedsHistoryReset(observed.request.swapchainGeneration));
    reportSlowStage("record",recordStart);
    if (result.record.status != VkRecordStatus::Complete)
    {
        result.reason = result.record.reason;
        if(image.capturedGuides&&!slot->frame->DiscardCapturedGuides()) {
            slot->state.Uncertain();result.handoff.state=VkPresentHandoffState::Uncertain;
            InvalidateSourceHistory();
            result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
        }
        return result;
    }
    // Only model-owner output authenticates lifetime. The guide skeleton does not.
    if(result.record.modelLifetime&&result.record.modelLifetime->Qualified()&&
       result.record.modelLifetime->device==sourceContext.device&&
       result.record.modelLifetime->deviceGeneration==sourceContext.deviceGeneration){
        sourceContext.modelLifetime=result.record.modelLifetime;
        sourceContext.routeEpoch=result.record.modelLifetime->routeEpoch;
        result.sources.context=sourceContext;
        if(history.selectedBoundary){
            if(!sourceContext_||*sourceContext_!=sourceContext)compositionGate_.MarkHistoryDiscontinuous();
            sourceContext_=sourceContext;
            GetVulkanPresentStatus().ObserveSourcePolicy(sourcePolicy,sourceContext,result.sources.signals.capture);
        }
    }
    bool providerAttempted=false;
    const auto retainUncertain=[&]{
        slot->state.Uncertain();slot->retirementProven=false;slot->completionObserved=true;
        closed_=true;result.handoff.state=VkPresentHandoffState::Uncertain;
        result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
    };
    // Capture the reason at the failing guard. A later clock/config read must
    // not relabel that failure, including the final submission callback guard.
    const char* currentRefusalReason=nullptr;
    std::optional<VkNrPresentGuardObservation> guardObservation;
    const auto current=[&]{
        bool retriedOwner=false;
        for(;;){
            // Re-observation shares this frame's original deadline. The getter
            // releases both try-locks on return; no model lock crosses this wait.
            if(retriedOwner&&!waitBudget->RemainingNs()){
                currentRefusalReason="Vulkan Present model owner remained busy through the frame deadline.";
                if(guardObservation)guardObservation->predicate="current_model_busy_deadline";
                return false;
            }
            guardObservation.reset();if(VkPresentGuardTraceEnabled())guardObservation.emplace();
            auto* projection=guardObservation?&*guardObservation:nullptr;
            currentRefusalReason="Vulkan Present recorded model lifetime is unavailable or inconsistent.";
            if(!result.record.modelLifetime||result.record.modelLifetime!=sourceContext.modelLifetime){
                if(projection){projection->predicate="recorded_lifetime";projection->expectedModel=sourceContext.modelLifetime;projection->actualModel=result.record.modelLifetime;}
                return false;
            }
            const auto preferenceRevision=Connections::SignalPreferences().revision;
            currentRefusalReason="Vulkan Present input preference changed before submission.";
            if(!VkPresentSources::CanSubmit(sourcePlan,preferenceRevision)){
                if(projection){projection->predicate="input_preference";projection->preferenceKnown=true;projection->expectedPreference=sourcePlan.sources.receipt.revision;projection->actualPreference=preferenceRevision;}
                return false;
            }
            // A busy query is not a changed tuple. Always request the genuine
            // read classification, independently of diagnostic trace settings.
            VkNrPresentLifetimeRead read=VkNrPresentLifetimeRead::NotVisited;
            const auto observedModel=CurrentVulkanPresentModelLifetimeVk(&read);
            if(projection)projection->read=read;
            if(!VkPresentSources::CurrentModelMatches(sourceContext,observedModel)){
                if(projection){projection->predicate="current_model";projection->expectedModel=sourceContext.modelLifetime;projection->actualModel=observedModel;}
                const bool busy=!observedModel&&(read==VkNrPresentLifetimeRead::BusyRecording||read==VkNrPresentLifetimeRead::BusyState);
                if(busy){
                    const auto remaining=waitBudget->RemainingNs();
                    if(remaining){
                        // Yield actual CPU time to the owner, without a busy spin
                        // or another provider invocation. Recheck EVERY guard.
                        std::this_thread::sleep_for(std::chrono::nanoseconds((std::min)(remaining,uint64_t{1000000})));
                        retriedOwner=true;
                        continue;
                    }
                    currentRefusalReason="Vulkan Present model owner remained busy through the frame deadline.";
                    if(projection)projection->predicate="current_model_busy_deadline";
                }else currentRefusalReason=observedModel?
                    "Vulkan Present model lifetime changed before submission.":
                    "Vulkan Present current model owner is unavailable before submission.";
                return false;
            }
            // A query may itself consume the last allowance. A late matching
            // tuple cannot extend the frame's deadline after contention.
            if(retriedOwner&&!waitBudget->RemainingNs()){
                currentRefusalReason="Vulkan Present frame deadline expired while observing the model owner.";
                if(projection)projection->predicate="current_model_observation_deadline";
                return false;
            }
            if(currentInitialization){
                currentRefusalReason="Vulkan Present initialization frame changed before submission.";
                if(!currentInitialization()){
                    if(projection){if(initializationObservation) *projection=*initializationObservation;else projection->predicate="initialization_unobserved";}
                    return false;
                }
                currentRefusalReason="Vulkan Present deadline exhausted before submission.";
                if(!waitBudget->RemainingNs()){
                    if(projection)projection->predicate="initialization_deadline";
                    return false;
                }
            }
            currentRefusalReason=nullptr;return true;
        }
    };
    const auto refuse=[&](const char* reason,uint32_t originLine){
        const bool trace=VkPresentGuardTraceEnabled();
        std::optional<VkNrRecordingOwner::UseRecordingSnapshot> before;
        if(trace&&result.record.use){try{before=VulkanNrRecordings().InspectUseRecording(result.record.use);}catch(...) {}}
        bool discardKnown=false,discardSucceeded=false;
        const bool externalUnresolved=providerAttempted&&!result.providerPreparation.NoExternalEntry();
        if(externalUnresolved){
            // The callback may already have forwarded provider work. Preserve its
            // consumer, recording/private owners and semaphore: never pretend it
            // did not run or use the prior cycle's signaled fence as new proof.
            retainUncertain();
            MarkPresentRuntimeUncertainVk("Vulkan Present refused after provider entry; ownership unresolved; restart required");
        }else{
            // No provider invocation/submission has occurred. Reset the actual
            // unsubmitted recording so its replay right cannot strand the model.
            bool discarded=false;
            try {discarded=slot->frame->DiscardCapturedGuides();}
            catch(...) {retainUncertain();
                if(trace)TraceVkPresentGuard(guardObservation?&*guardObservation:nullptr,result.record.use,originLine,reason,"discard_exception",true,providerAttempted,!externalUnresolved,before);
                throw;} // preserve cleanup exception and every remaining owner
            if(trace){discardKnown=true;discardSucceeded=discarded;}
            if(!discarded){
                retainUncertain();
                MarkPresentRuntimeUncertainVk("Vulkan Present exact unsubmitted recording discard failed; ownership unresolved; restart required");
                reason="Vulkan Present exact unsubmitted recording discard failed; ownership unresolved; restart required";
            }else if(observed.applicationFacing)slot->consumer.reset();
        }
        InvalidateSourceHistory();result.reason=reason;
        if(trace)TraceVkPresentGuard(guardObservation?&*guardObservation:nullptr,result.record.use,originLine,reason,
            externalUnresolved?"retained_external_uncertain":discardKnown?(discardSucceeded?"exact_discard_succeeded":"exact_discard_failed"):"discard_not_observed",
            true,providerAttempted,!externalUnresolved,before);
    };
    // Recording has not consumed the game's waits or transferred its image.
    if(!current()){refuse(currentRefusalReason,__LINE__);return result;}
    if(observed.applicationFacing){
        slot->consumer.reset();
        slot->consumer=VulkanPresentConsumers().Register(slot->device,VkNrCompletionDeviceGeneration(slot->device),slot->signal);
        if(!slot->consumer){refuse("Vulkan application Present consumption tracking unavailable",__LINE__);return result;}
        if(!result.record.preparationOnly){
            if(!prepareProvider){refuse("Vulkan application Present full-frame provider contract unavailable",__LINE__);return result;}
            if(!current()){refuse(currentRefusalReason,__LINE__);return result;}
            providerAttempted=true;
            try {result.providerPreparation=prepareProvider(sourcePlan.sources.receipt.revision,*result.record.modelLifetime);}
            catch(...) {retainUncertain();throw;} // no structured return authenticates absence of external effects
            if(!result.providerPreparation.prepared){
                const auto& p=result.providerPreparation;
                static std::atomic<uint64_t> reports{0};const auto n=++reports;
                if(n<=16||n%120==0)LOG_INFO("Vulkan Present provider preparation refused: provider={} observedProvider={} frame={} viewport={} sourceRevision={} observedRevision={} modelUse={} stage={} failure={} effects={} tokenEntered={} tokenReturned={} tokenResult={} tokenVersion={} tokenFrame={} tagsEntered={} tagsReturned={} tagsResult={} ownerObserved={} ownerDevice={} ownerGeneration={} ownerRoute={} ownerResume={} ownerInput={} expectedDevice={} expectedGeneration={} expectedRoute={} expectedResume={} expectedInput={} providerObservedAt={} ownerObservedAt={} adapter=[{}]",
                    p.provider,p.observedProvider,p.frame,p.viewport,p.sourceRevision,p.observedRevision,result.record.use.value,
                    static_cast<unsigned>(p.stage),static_cast<unsigned>(p.failure),static_cast<unsigned>(p.effects),
                    p.token.entered,p.token.returned,p.token.result,p.tokenVersion,p.tokenFrame,
                    p.tags.entered,p.tags.returned,p.tags.result,p.observedLifetime.has_value(),
                    p.observedLifetime?p.observedLifetime->device:0,p.observedLifetime?p.observedLifetime->deviceGeneration:0,
                    p.observedLifetime?p.observedLifetime->routeEpoch:0,p.observedLifetime?p.observedLifetime->resumeGeneration:0,
                    p.observedLifetime?p.observedLifetime->inputInterruptionEpoch:0,p.expectedLifetime.device,
                    p.expectedLifetime.deviceGeneration,p.expectedLifetime.routeEpoch,p.expectedLifetime.resumeGeneration,
                    p.expectedLifetime.inputInterruptionEpoch,static_cast<unsigned>(p.providerObservedAt),
                    static_cast<unsigned>(p.ownerObservedAt),p.adapterReason);
                refuse(p.NoExternalEntry()?"Vulkan application Present provider preparation refused before external entry; NR not delivered":
                    "Vulkan application Present provider preparation failed after external entry or with unknown effects; ownership unresolved; restart required",__LINE__);return result;
            }
        }
    }
    if(!current()){refuse(currentRefusalReason,__LINE__);return result;}
    if ((slot->state.Pending() || slot->fenceNeedsReset) && vkResetFences(slot->device, 1, &slot->fence) != VK_SUCCESS)
    {
        refuse("Vulkan Present fence could not be reset",__LINE__);return result;
    }
    // A successful reset is not submission or completion. Check again because the
    // Vulkan reset call itself is an external boundary; retain needs-reset on refusal.
    if(!current()){refuse(currentRefusalReason,__LINE__);return result;}
    slot->fenceNeedsReset=false;
    bool submitGuardRefused=false;
    const auto submitStart = VkNrWaitBudget::Clock::now();
    result.handoff = SubmitVkPresent(result.record, original, observed.queue,
        image.commandBuffer, slot->signal, slot->fence,
        [&](VkQueue queue, const VkSubmitInfo& submit, VkFence fence) {
            if(!current()){submitGuardRefused=true;return VK_ERROR_INITIALIZATION_FAILED;}
            return vkQueueSubmit(queue, 1, &submit, fence);
        });
    reportSlowStage("submit",submitStart);
    if(submitGuardRefused){
        // No Vulkan submission was attempted. An entered provider still owns
        // unresolved work: use the same retaining/closing refusal as above.
        result.handoff={};
        refuse(currentRefusalReason,__LINE__);
        return result;
    }
    if (result.handoff.state == VkPresentHandoffState::Accepted)
    {
        lastApplicationFacing_=observed.applicationFacing;
        modelQueue_ = observed.queue;
        slot->state.Submitted();
        slot->fenceNeedsReset=true;
        slot->retirementProven = false;
        slot->completionObserved = false;
        slot->acquisition = observed.request.acquireGeneration;
        result.sources.ticket=slot->ticket;result.sources.acquisition=slot->acquisition;
        result.sources.image=reinterpret_cast<uintptr_t>(observed.image);
        result.sources.imageIndex=observed.request.imageIndex;
        result.sources.completeTarget=result.record.modelRecorded&&result.record.targetWriteRecorded&&
            !result.record.preparationOnly&&result.record.chainDeliverable&&result.record.requestedPasses&&
            result.record.completedPasses==result.record.requestedPasses&&result.record.outputVersion;
        slot->sources=result.sources;
        lastSourceInput_=input.input;
        slot->acquireProof.reset();
        result.ticket = slot->ticket;
        compositionGate_.Submitted(slot->ticket, observed.request.swapchainGeneration);
        history.submitted = true;
        result.reason = result.record.preparationOnly?result.record.reason:std::string{};
    }
    else
    {
        slot->state.Uncertain();
        slot->retirementProven = false;
        InvalidateSourceHistory();
        MarkPresentRuntimeUncertainVk("Present queue submission uncertain; restart required");
        result.reason = "Vulkan Present adapter submission failed or is uncertain";
    }
    return result;
}

bool VulkanPresentExecutor::CompleteCapturedGuides(uint64_t ticket)
{
    std::lock_guard lock(mutex_);
    for(auto& slot:slots_)if(slot->ticket==ticket&&slot->state.Pending()) {
        if(vkWaitForFences(slot->device,1,&slot->fence,VK_TRUE,2'000'000'000ull)!=VK_SUCCESS) {
            slot->state.Uncertain();InvalidateSourceHistory();
            MarkPresentRuntimeUncertainVk("Captured-guide consumer completion uncertain; restart required");return false;
        }
        ObserveSlotCompletion(*slot,false);
        compositionGate_.ObserveCompletion(ticket,true);slot->frame->ReleaseCapturedGuides();return true;
    }
    return false;
}
void VulkanPresentExecutor::OriginalPresentReturned(uint64_t ticket, VkResult result, uint64_t registryPresentSerial)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : slots_)
        if (slot->ticket == ticket)
        {
            slot->state.OriginalPresentReturned(result);
            slot->registryPresentSerial = registryPresentSerial;
            // Hook calls are queue-serialized. Saturation preserves ownership:
            // equal serials will never qualify a predecessor for destruction.
            if (nextPresentSerial_ != UINT64_MAX) ++nextPresentSerial_;
            slot->presentSerial = nextPresentSerial_;
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
                InvalidateSourceHistory();
            else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
                InvalidateSourceHistory();
                MarkPresentRuntimeUncertainVk("Original Vulkan Present failed; restart required");
            }
            return;
        }
}

void VulkanPresentExecutor::MarkUncertain(uint64_t ticket)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : slots_)
        if (slot->ticket == ticket)
        {
            slot->state.Uncertain();
            slot->retirementProven = false;
            InvalidateSourceHistory();
            MarkPresentRuntimeUncertainVk("Vulkan overlay submission uncertain; restart required");
            return;
        }
}

bool VulkanPresentExecutor::CanYieldOutput(std::string& reason)
{
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){reason="Vulkan Present ownership is busy";return false;}
    // WSI-only physical records remain owned here, but cannot access NR private
    // resources after Maintain releases their completed frame. They need not
    // block the separate Anything output. Provider consumer ownership still
    // needs its own proof; this query never releases or reuses any semaphore.
    if(initialization_.Busy() || !compositionGate_.CanRecord() || std::any_of(slots_.begin(), slots_.end(), [](const auto& slot) {
        return slot->frame || !slot->state.CanReleasePrivateFrame(true) ||
            (slot->applicationFacing && (!slot->consumer || !slot->consumer->Complete()));
    })){
        reason="Vulkan Present output or presentation ownership remains pending";return false;
    }
    reason.clear();return true;
}

void VulkanPresentExecutor::NotifyDeviceInit(){std::lock_guard lock(mutex_);if(slots_.empty()&&!initialization_.Device())closed_=false;}

void VulkanPresentExecutor::Shutdown(bool deviceAlive)
{
    std::lock_guard<std::mutex> lock(mutex_);
    closed_=true;
    InvalidateSourceHistory();
    if (deviceAlive)
        for (auto& slot : slots_)
            if (slot->device != VK_NULL_HANDLE && vkDeviceWaitIdle(slot->device) != VK_SUCCESS)
            {
                LOG_WARN("Vulkan Present shutdown idle failed; retaining {} slots ({} private bytes)",slots_.size(),retainedBytes_);
                return;
            }
    for(auto i=slots_.begin();i!=slots_.end();){auto& slot=**i;
        if(deviceAlive&&slot.state.Pending()&&!slot.retirementProven){
            // Device idle retires command/image work, not the WSI wait. Avoid
            // retaining hundreds of MB solely for a small unresolved semaphore.
            if(slot.frame&&slot.state.CanReleasePrivateFrame(vkGetFenceStatus(slot.device,slot.fence)==VK_SUCCESS)){
                slot.frame->Release(true);slot.frame.reset();retainedBytes_-=slot.estimatedBytes;slot.estimatedBytes=0;
            }
            ++i;continue;
        }
        Destroy(slot,deviceAlive);retainedBytes_-=slot.estimatedBytes;i=slots_.erase(i);
    }
    if(!slots_.empty())LOG_WARN("Vulkan Present shutdown retains {} unresolved presentation slots ({} private bytes)",slots_.size(),retainedBytes_);
    if(!initialization_.Release(deviceAlive,PollPresentInitializationVk))
        LOG_WARN("Vulkan Present shutdown retains unresolved private initialization");
    modelQueue_ = VK_NULL_HANDLE;
    compositionGate_ = {};
}

void VulkanPresentExecutor::DeviceDestroyed(VkDevice device)
{
    std::lock_guard lock(mutex_);
    initialization_.DeviceDestroyed(device);
    if(sourceContext_&&sourceContext_->device==reinterpret_cast<uintptr_t>(device))InvalidateSourceHistory();
    size_t removed=0;
    for(auto i=slots_.begin();i!=slots_.end();) {
        if((*i)->device!=device){++i;continue;}
        Destroy(**i,false);retainedBytes_-=(*i)->estimatedBytes;
        i=slots_.erase(i);++removed;
    }
    LOG_INFO("Vulkan Present device destruction cleared {} records; remaining {} records ({} private bytes)",removed,slots_.size(),retainedBytes_);
    if(slots_.empty()){modelQueue_=VK_NULL_HANDLE;compositionGate_={};}
}

VulkanPresentExecutor& GetVulkanPresentExecutor()
{
    static VulkanPresentExecutor executor;
    return executor;
}

} // namespace DlssNr
