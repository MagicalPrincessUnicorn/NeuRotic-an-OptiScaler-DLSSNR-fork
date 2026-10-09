#include "VulkanPresentExecutor.h"
#include "VulkanNrCompletion.h"
#include "VulkanPresentConsumer.h"
#include "DlssNrFeature_Vk.h"
#include "VulkanPresentStatus.h"
#include "VulkanPresentGuidesVk.h"
#include <Config.h>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace DlssNr
{
namespace
{
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
            slot->device == observed.device)
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

void VulkanPresentExecutor::Maintain(const VkObservedPresent& observed, bool inactive)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || closed_) return;
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
            if (!slot.completionObserved) { slot.completionObserved = true; GetVulkanPresentStatus().GpuCompleted(); }
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
    if (inactive) compositionGate_.MarkHistoryDiscontinuous();
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
        { slot->completionObserved = true; GetVulkanPresentStatus().GpuCompleted(); }
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
                                                        const std::function<bool()>& prepareProvider,
                                                        std::shared_ptr<const VkCapturedGuideInput> capturedGuides)
{
    if (!waitBudget) waitBudget = std::make_shared<VkNrWaitBudget>();
    std::lock_guard<std::mutex> lock(mutex_);
    VkPresentExecution result {};
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
    if(capturedGuides&&(!capturedGuides->lease||!capturedGuides->bind||
        !capturedGuides->identity.Matches(reinterpret_cast<uintptr_t>(observed.device),guideFrame.deviceGeneration,
          reinterpret_cast<uintptr_t>(observed.swapchain),guideFrame.swapchainGeneration,guideFrame.acquireGeneration,
          reinterpret_cast<uintptr_t>(observed.image),guideFrame.output.width,guideFrame.output.height))) {
        result.reason="Captured Vulkan guides do not match this completed acquisition";return result;
    }
    const auto guides = capturedGuides ? std::optional<VkNrGuideLease>{} : SelectVulkanPresentGuides(guideFrame,guideSelection.get(),observed.image);
    const auto metadata = guides ? std::optional<VkNrFrameContract>(guides->contract) :
        SelectVulkanResolutionMetadata(guideFrame,guideSelection.get());
    const auto scalarSize=observed.renderSize.value;
    const auto input = PresentInputDecision::Choose(capturedGuides?PresentInput::Policy::RequireGuides:observed.request.policy,guides.has_value()||bool(capturedGuides),scalarSize.has_value()||metadata.has_value(),
        PresentResolution::Selected(*settings));
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
    auto capabilities = observed.capabilities; capabilities.guidesQualified = guides.has_value()||bool(capturedGuides);
    capabilities.applicationBeforeProvider=observed.applicationFacing&&bool(prepareProvider);
    auto admissionRequest=observed.request;
    if(capturedGuides)admissionRequest.policy=PresentInput::Policy::RequireGuides;
    result.admission = DecideVkPresent(admissionRequest, capabilities);
    result.reason = VkPresentRefusalText(result.admission.reason);
    if (!result.admission.allowed) return result;
    if (input.input == PresentInputDecision::InputClass::Refused) {
        result.admission.allowed=false;result.admission.actualInputClass=input.input;result.reason=input.reason;return result;
    }
    result.admission.actualInputClass = input.input;
    if (!IsVkPresentWaitReplacementChainKnown(original.pNext) || original.waitSemaphoreCount > 64 ||
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
                (*previous)->completionObserved = true;
                GetVulkanPresentStatus().GpuCompleted();
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
            if (fence != VK_TIMEOUT && fence != VK_NOT_READY)
                MarkPresentRuntimeUncertainVk("Vulkan Present composition fence failed; restart required");
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
            slot->completionObserved = true;
            GetVulkanPresentStatus().GpuCompleted();
        }
        if (fence == VK_ERROR_DEVICE_LOST) slot->state.Uncertain();
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
        if(slot->applicationFacing){
            // Both obligations for the old cycle are complete. A later policy
            // failure must leave a reusable fresh slot, not a phantom pending wait.
            slot->state={};slot->consumer.reset();slot->acquireProof.reset();
            slot->retirementProven=false;
        }
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
            result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
        }
        return result;
    }
    if(observed.applicationFacing){
        // Reserve consumption tracking before forwarding anything to the provider.
        // The previous cycle has proved both GPU work and semaphore consumption.
        slot->consumer.reset();
        slot->consumer=VulkanPresentConsumers().Register(slot->device,VkNrCompletionDeviceGeneration(slot->device),slot->signal);
        if(!slot->consumer||(!result.record.preparationOnly&&(!prepareProvider||!prepareProvider()))){
            if(image.capturedGuides) {
                if(!slot->frame->DiscardCapturedGuides()) {
                    slot->state.Uncertain();result.handoff.state=VkPresentHandoffState::Uncertain;
                    result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
                }
            } else AbandonPresentRecordingVk(!result.record.preparationOnly);
            slot->consumer.reset();
            result.reason="Vulkan application Present full-frame provider contract unavailable";return result;
        }
    }
    // Only a proved free slot reaches the fence reset. An error here discards model work.
    if ((slot->state.Pending() || slot->fenceNeedsReset) && vkResetFences(slot->device, 1, &slot->fence) != VK_SUCCESS)
    {
        if(!image.capturedGuides)AbandonPresentRecordingVk(!result.record.preparationOnly);
        result.reason = "Vulkan Present fence could not be reset";
        if(image.capturedGuides&&!slot->frame->DiscardCapturedGuides()) {
            slot->state.Uncertain();result.handoff.state=VkPresentHandoffState::Uncertain;
            result.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
        }
        return result;
    }
    slot->fenceNeedsReset=false;
    const auto submitStart = VkNrWaitBudget::Clock::now();
    result.handoff = SubmitVkPresent(result.record, original, observed.queue,
        image.commandBuffer, slot->signal, slot->fence,
        [](VkQueue queue, const VkSubmitInfo& submit, VkFence fence) {
            return vkQueueSubmit(queue, 1, &submit, fence);
        });
    reportSlowStage("submit",submitStart);
    if (result.handoff.state == VkPresentHandoffState::Accepted)
    {
        lastApplicationFacing_=observed.applicationFacing;
        modelQueue_ = observed.queue;
        slot->state.Submitted();
        slot->fenceNeedsReset=true;
        slot->retirementProven = false;
        slot->completionObserved = false;
        slot->acquisition = observed.request.acquireGeneration;
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
            slot->state.Uncertain();MarkPresentRuntimeUncertainVk("Captured-guide consumer completion uncertain; restart required");return false;
        }
        slot->completionObserved=true;
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
                compositionGate_.MarkHistoryDiscontinuous();
            else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                MarkPresentRuntimeUncertainVk("Original Vulkan Present failed; restart required");
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
    if(!compositionGate_.CanRecord() || std::any_of(slots_.begin(), slots_.end(), [](const auto& slot) {
        return slot->frame || !slot->state.CanReleasePrivateFrame(true) ||
            (slot->applicationFacing && (!slot->consumer || !slot->consumer->Complete()));
    })){
        reason="Vulkan Present output or presentation ownership remains pending";return false;
    }
    reason.clear();return true;
}

void VulkanPresentExecutor::NotifyDeviceInit(){std::lock_guard lock(mutex_);if(slots_.empty())closed_=false;}

void VulkanPresentExecutor::Shutdown(bool deviceAlive)
{
    std::lock_guard<std::mutex> lock(mutex_);
    closed_=true;
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
    modelQueue_ = VK_NULL_HANDLE;
    compositionGate_ = {};
}

void VulkanPresentExecutor::DeviceDestroyed(VkDevice device)
{
    std::lock_guard lock(mutex_);
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
