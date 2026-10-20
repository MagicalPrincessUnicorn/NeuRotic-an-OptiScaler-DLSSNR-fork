#include "dlssnr/VulkanPresentConsumer.h"
#include "../OptiScaler/dlssnr/VulkanNrCompletion.h"
#include "../OptiScaler/dlssnr/VulkanNrRuntime.h"
#include "../OptiScaler/dlssnr/VulkanPresentRegistry.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
using namespace DlssNr;
static void Check(bool condition, const char* name) { if (!condition) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } }
static std::unordered_map<VkFence,VkResult> fenceStatus;
static uintptr_t nextFence = 100;
static int checkpoints = 0, destroyedFences = 0;
static bool failFenceCreate = false;
static VkResult VKAPI_CALL CreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* fence)
{ if (failFenceCreate) return VK_ERROR_OUT_OF_HOST_MEMORY; *fence = reinterpret_cast<VkFence>(nextFence++); fenceStatus[*fence] = VK_NOT_READY; return VK_SUCCESS; }
static void VKAPI_CALL DestroyFence(VkDevice,VkFence fence,const VkAllocationCallbacks*) { ++destroyedFences; fenceStatus.erase(fence); }
static VkResult VKAPI_CALL FenceStatus(VkDevice,VkFence fence) { return fenceStatus.at(fence); }
static VkResult VKAPI_CALL Checkpoint(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence fence)
{ Check(count == 1 && info->commandBufferCount == 0 && info->waitSemaphoreCount == 0 && info->signalSemaphoreCount == 0 && fenceStatus.contains(fence),"checkpoint owns fence with no game wait/signal changes"); ++checkpoints; return VK_SUCCESS; }
int main()
{
    VkNrRecordingOwner owner;
    auto device = reinterpret_cast<VkDevice>(1); auto pool = reinterpret_cast<VkCommandPool>(2);
    auto buffer = reinterpret_cast<VkCommandBuffer>(3); auto queue = reinterpret_cast<VkQueue>(4);
    owner.OnAllocate(device,pool,buffer,0); owner.OnBegin(buffer,VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,VK_SUCCESS);
    const auto use = owner.Reserve(buffer,1); Check(use.has_value(),"recording reserves use"); owner.OnEnd(buffer,VK_SUCCESS);
    VkNrSubmission submission{queue,1,{*use}};
    const auto a = owner.PrepareSubmission(submission), b = owner.PrepareSubmission(submission);
    Check(a && b && *a != *b,"each simultaneous submit has unique receipt");
    for (auto receipt : {*a,*b}) { owner.SubmissionReturned(receipt,VK_SUCCESS); owner.CheckpointReturned(receipt,VK_SUCCESS); }
    Check(!owner.ProducerProof(*use),"repeated actual submissions cannot prove one guide producer");
    owner.ObserveFence(*a,VK_SUCCESS); Check(!owner.GpuComplete(*use),"second actual submission retains pending use");
    owner.ObserveFence(*b,VK_TIMEOUT); Check(!owner.GpuComplete(*use),"timeout is not completion");
    owner.ObserveFence(*b,VK_SUCCESS); Check(owner.GpuComplete(*use) && !owner.Reusable(*use),"GPU completion does not release replay references");
    Check(owner.RetainUse(*use),"completion evidence can be leased by a session or readback owner");
    const auto replay = owner.PrepareSubmission(submission); Check(replay.has_value(),"executable recording may replay");
    owner.SubmissionReturned(*replay,VK_SUCCESS); owner.CheckpointReturned(*replay,VK_ERROR_OUT_OF_HOST_MEMORY);
    owner.ObserveFence(*replay,VK_SUCCESS); owner.OnReset(buffer,VK_SUCCESS);
    Check(owner.RecordingReleased(*use) && !owner.Reusable(*use),"failed checkpoint retains unresolved GPU hold after reset");
    owner.OnDeviceDestroyed(device); Check(owner.Reusable(*use),"actual device destruction ends unresolved use");
    owner.ReleaseUse(*use);
    owner.OnAllocate(device,pool,buffer,0); owner.OnBegin(buffer,0,VK_SUCCESS); const auto failed = owner.Reserve(buffer,2); owner.OnEnd(buffer,VK_SUCCESS);
    Check(failed.has_value(),"second recording reserves use");
    const auto failedSubmit = owner.PrepareSubmission({queue,2,{*failed}}); Check(failedSubmit.has_value(),"failed submit receipt reserved");
    owner.SubmissionReturned(*failedSubmit,VK_ERROR_OUT_OF_HOST_MEMORY); owner.OnReset(buffer,VK_SUCCESS);
    Check(owner.Reusable(*failed),"unsuccessful submit adds no accepted GPU use");
    VkNrRecordingOwner unknownOwner;
    unknownOwner.OnAllocate(device,pool,buffer,0); unknownOwner.OnBegin(buffer,0,VK_SUCCESS);
    auto unknownUse = unknownOwner.Reserve(buffer,2); Check(unknownUse.has_value(),"unknown-error use reserved"); unknownOwner.OnEnd(buffer,VK_SUCCESS);
    auto unknownReceipt = unknownOwner.PrepareSubmission({queue,2,{*unknownUse}}); Check(unknownReceipt.has_value(),"unknown-error receipt reserved");
    unknownOwner.SubmissionReturned(*unknownReceipt,VK_ERROR_UNKNOWN); unknownOwner.OnReset(buffer,VK_SUCCESS);
    Check(!unknownOwner.Reusable(*unknownUse),"unknown submit error cannot prove resources unaffected");
    owner.OnBegin(buffer,0,VK_SUCCESS); const auto third = owner.Reserve(buffer,3); owner.OnEnd(buffer,VK_SUCCESS);
    Check(third.has_value(),"third recording reserves use");
    const VkSemaphore wait = reinterpret_cast<VkSemaphore>(5), signal = reinterpret_cast<VkSemaphore>(6);
    const VkFence fence = reinterpret_cast<VkFence>(7); const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkSubmitInfo app{VK_STRUCTURE_TYPE_SUBMIT_INFO,nullptr,1,&wait,&stage,1,&buffer,1,&signal};
    int originalCalls = 0, checkpointCalls = 0;
    const auto result = ObserveVkNrSubmission(owner,{queue,3,{*third}},[&] {
        ++originalCalls; Check(app.pWaitSemaphores == &wait && app.pSignalSemaphores == &signal && fence == reinterpret_cast<VkFence>(7),"application arguments unchanged"); return VK_SUCCESS;
    },[&](VkNrSubmissionId) { ++checkpointCalls; return VK_ERROR_DEVICE_LOST; });
    Check(result == VK_SUCCESS && originalCalls == 1 && checkpointCalls == 1 && !owner.GpuComplete(*third),"checkpoint failure preserves original application result");
    int aliasReceipts = 0;
    { VkNrObservationScope core(VkNrObservation::Submit2,4); { VkNrObservationScope khr(VkNrObservation::Submit2,4); if(khr.Observe())++aliasReceipts; } if(core.Observe())++aliasReceipts; }
    Check(aliasReceipts == 1,"core and KHR Submit2 aliases make single receipt");
    owner.OnReset(buffer,VK_SUCCESS); Check(!owner.Reusable(*third),"device loss from owned checkpoint is held until destruction");
    owner.OnDeviceDestroyed(device); Check(owner.Reusable(*third),"destroyed device releases lost checkpoint");
    auto& actual = VulkanNrRecordings();
    RegisterVkNrCompletionDevice(device,{CreateFence,DestroyFence,FenceStatus,Checkpoint});
    RegisterVkNrCompletionQueue(device,queue,0);
    Check(UniqueVkNrCompletionQueue(device,0)==queue,"single real queue qualifies private model ordering");
    Check(!UniqueVkNrCompletionQueue(device,9),"unknown family cannot prove ownership");
    const auto secondQueue=reinterpret_cast<VkQueue>(401),thirdQueue=reinterpret_cast<VkQueue>(402);
    RegisterVkNrCompletionQueue(device,secondQueue,2);RegisterVkNrCompletionQueue(device,thirdQueue,2);
    auto familyContext=VkNrRecordingQueueContext(device,2);
    Check(familyContext&&familyContext->family==2&&!familyContext->queue,"ambiguous recording family does not invent a queue");
    Check(VkNrRecordingQueueContext(device,2,secondQueue)->queue==secondQueue,"actual explicit queue resolves multi-queue family");
    Check(!VkNrRecordingQueueContext(device,0,secondQueue),"explicit queue from other family refused");
    Check(!VkNrRecordingQueueContext(reinterpret_cast<VkDevice>(999),2),"other device refused");
    Check(!VkNrRecordingQueueContext(device,UINT32_MAX),"unknown recording family refused");
    RegisterVkNrCompletionQueue(device,reinterpret_cast<VkQueue>(403),3,true);
    Check(!VkNrRecordingQueueContext(device,3),"protected-only family refused");
    actual.OnAllocate(device,pool,buffer,0); actual.OnBegin(buffer,0,VK_SUCCESS);
    const auto actualUse = actual.Reserve(buffer,4); Check(actualUse && ReserveVkNrCompletion(device,*actualUse),"reserve actual owned fence before NR recording");
    const auto opaqueChild=reinterpret_cast<VkCommandBuffer>(808);
    actual.OnExecute(buffer,{&opaqueChild,1});
    actual.OnEnd(buffer,VK_SUCCESS);
    Check(ObserveVkNrQueueSubmit(queue,{&buffer,1},[]{return VK_SUCCESS;}) == VK_SUCCESS && checkpoints == 1,"actual device dispatch issues owned checkpoint");
    PollVkNrCompletions(); Check(!actual.GpuComplete(*actualUse),"actual fence NOT_READY retains GPU use");
    Check(actual.RetainUse(*actualUse),"readback retains completed-use identity until publication");
    actual.OnReset(buffer,VK_SUCCESS); Check(!actual.Reusable(*actualUse),"reset does not release submitted GPU resources");
    for (auto& [ownedFence,status] : fenceStatus) { (void)ownedFence; status = VK_SUCCESS; }
    PollVkNrCompletions(); Check(actual.GpuComplete(*actualUse) && actual.Reusable(*actualUse) && destroyedFences == 1,"actual completion plus reset permits reclamation");
    actual.OnBegin(buffer,0,VK_SUCCESS); const auto unobserved = actual.Reserve(buffer,4); Check(unobserved.has_value(),"unknown queue fixture owns recorded work"); actual.OnEnd(buffer,VK_SUCCESS);
    Check(actual.GpuComplete(*actualUse),"new recording does not erase leased completion evidence"); actual.ReleaseUse(*actualUse);
    ObserveVkNrQueueSubmit(reinterpret_cast<VkQueue>(50),{&buffer,1},[]{return VK_SUCCESS;}); actual.OnReset(buffer,VK_SUCCESS);
    Check(!actual.Reusable(*unobserved),"unobserved actual queue cannot turn successful work into safe reclamation");
    actual.OnBegin(buffer,0,VK_SUCCESS);
    VulkanNrRuntime runtime; runtime.EnsureDevice(device,[]{return true;}); runtime.Activate(VkNrRoute::Native);
    VulkanNrSession session(VkNrRoute::Native,runtime); VkFrameRequest request; request.device = device; request.commandBuffer = buffer;
    failFenceCreate = true; int modelCalls = 0;
    auto blocked = session.Record(request,[&](const VkFrameRequest&,bool){ ++modelCalls; return VkRecordResult{VkRecordStatus::Complete,"",true,true}; });
    Check(blocked.status == VkRecordStatus::NoWork && modelCalls == 0,"failed owned fence creation refuses NR before vendor recording");
    failFenceCreate = false;
    runtime.Activate(VkNrRoute::Present);runtime.Activate(VkNrRoute::Native);
    session.Record(request,[&](const VkFrameRequest& frame,bool){
        Check(frame.contract.deviceGeneration==VkNrCompletionDeviceGeneration(device),
            "route changes never replace actual device identity with runtime epoch");
        return VkRecordResult{VkRecordStatus::NoWork,"identity observation"};
    });
    const auto unused = actual.Reserve(buffer,6); Check(unused && ReserveVkNrCompletion(device,*unused),"teardown owns an unused fence");
    RegisterVkNrCompletionQueue(device,reinterpret_cast<VkQueue>(51),0);
    Check(!UniqueVkNrCompletionQueue(device,0),"two queues require an explicit cross-queue dependency contract");
    {
        auto& loader=GetVulkanLoaderPresentRegistry();
        loader.DeviceCreated(device,reinterpret_cast<VkPhysicalDevice>(71),true,true,{0});
        loader.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
        auto swap=reinterpret_cast<VkSwapchainKHR>(72);auto acquire=reinterpret_cast<VkSemaphore>(73);
        VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;create.imageExtent={64,64};create.imageArrayLayers=1;
        create.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        loader.SwapchainCreated(device,swap,create,create.imageUsage,true,{reinterpret_cast<VkImage>(74)},{0});
        loader.ImageAcquired(device,swap,0,acquire);
        auto acquiredCommand=reinterpret_cast<VkCommandBuffer>(75);
        actual.OnAllocate(device,pool,acquiredCommand,0);
        actual.OnBegin(acquiredCommand,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);actual.OnEnd(acquiredCommand,VK_SUCCESS);
        Check(ObserveVkNrQueueSubmit(queue,{&acquiredCommand,1},[]{return VK_SUCCESS;},{&acquire,1})==VK_SUCCESS,
            "loader acquisition wait passes through the actual submission observer");
        Check(bool(loader.AcquireUse(swap,0))&&!loader.AcquireConsumed(swap,0),
            "loader acquisition owns pending completion without claiming queue acceptance is completion");
        Check(!GetVulkanPresentRegistry().AcquireUse(swap,0),"loader acquisition never contaminates semantic registry");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(loader.AcquireConsumed(swap,0),"actual checkpoint completes loader acquisition proof");
        loader.ImageAcquired(device,swap,0,acquire);
        const auto beforeEmpty = checkpoints;
        Check(ObserveVkNrQueueSubmit(queue,{},[]{return VK_SUCCESS;},{&acquire,1})==VK_SUCCESS,
            "semaphore-only acquisition submission preserves application result");
        Check(checkpoints==beforeEmpty+1 && bool(loader.AcquireUse(swap,0)),
            "semaphore-only acquisition wait receives its own completion checkpoint");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(loader.AcquireConsumed(swap,0),"empty submission acquisition completes");
        loader.ImageAcquired(device,swap,0,acquire);
        const auto opaqueCommand=reinterpret_cast<VkCommandBuffer>(9991);
        const auto beforeOpaque=checkpoints;
        ObserveVkNrQueueSubmit(queue,{&opaqueCommand,1},[]{return VK_SUCCESS;},{&acquire,1});
        Check(checkpoints==beforeOpaque+1,"opaque first command buffer does not block acquisition proof");
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};uint32_t imageIndex=0;
        info.swapchainCount=1;info.pSwapchains=&swap;info.pImageIndices=&imageIndex;
        const auto currentProof=[&]{const auto observed=loader.SnapshotForPresent(queue,info,false,0,PresentInput::Policy::ImageOnly,false);
            return loader.AcquireProof(swap,0,observed->request.acquireGeneration);};
        auto leased=currentProof();Check(leased&&!leased->Complete(),"actual delayed acquisition proof leased before GPU completion");
        loader.PresentSucceeded(swap,0,queue);const auto serial=loader.LastPresentSerial(swap,0);
        loader.ImageAcquired(device,swap,0,acquire);
        auto next=currentProof();Check(next&&next!=leased&&next->priorPresentSerial==serial&&next->priorPresentQueue==queue,
            "new acquisition retains exact previous successful Present identity");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(leased->Complete()&&!next->Complete(),"leased proof completes after registry advances acquisition");
        Check(!loader.AcquireProof(swap,0,leased->acquireGeneration),"current registry never lends old acquisition as current");
        const auto beforeRetry=checkpoints;
        for(auto error:{VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY}){
            Check(ObserveVkNrQueueSubmit(queue,{},[&]{return error;},{&acquire,1})==error,"failed application submission preserves result");
            Check(checkpoints==beforeRetry&&!next->Complete(),"failed application allocation creates no completion claim");
        }
        ObserveVkNrQueueSubmit(queue,{},[]{return VK_SUCCESS;},{&acquire,1});
        Check(checkpoints==beforeRetry+1&&currentProof()==next,"retry submits same leased acquisition dependency");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(next->Complete(),"retried exact dependency reaches actual completion");
        const auto consumedCheckpoints=checkpoints;
        ObserveVkNrQueueSubmit(queue,{},[]{return VK_SUCCESS;},{&acquire,1});
        Check(checkpoints==consumedCheckpoints,"already consumed acquire semaphore cannot resubmit acquisition proof");
        loader.ImageAcquired(device,swap,0,acquire);
        actual.OnBegin(acquiredCommand,0,VK_SUCCESS);actual.OnUnsupportedBindings(acquiredCommand);
        actual.OnExecute(acquiredCommand,{&opaqueCommand,1});actual.OnEnd(acquiredCommand,VK_SUCCESS);
        const auto unsupported=currentProof();
        ObserveVkNrQueueSubmit(queue,{&acquiredCommand,1},[]{return VK_SUCCESS;},{&acquire,1});
        Check(checkpoints==consumedCheckpoints+1,"unsupported bindings and opaque child do not refuse actual queue wait proof");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(unsupported->Complete(),"opaque recording acquisition proof completes");
        std::shared_ptr<const VkNrAcquireProof> delayed;
        for(unsigned frame=0;frame<1300;++frame){
            loader.PresentSucceeded(swap,0,queue);loader.ImageAcquired(device,swap,0,acquire);
            const auto current=currentProof();
            Check(current&&current->use&&!current->Complete(),"long off-state run reserves a fresh pending exact proof");
            ObserveVkNrQueueSubmit(queue,{},[]{return VK_SUCCESS;},{&acquire,1});
            if(delayed)Check(delayed->Complete(),"next actual submit observes previous frame's delayed checkpoint");
            for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
            Check(!current->Complete(),"GPU signal alone does not silently publish cached completion");
            delayed=current;
        }
        PollVkNrCompletions();Check(delayed&&delayed->Complete(),"last delayed proof completes through nonblocking maintenance");
        loader.SwapchainDestroyed(swap);Check(unsupported->Complete(),"leased completed proof survives registry swapchain destruction");
        loader.DeviceDestroyed(device);actual.OnReset(acquiredCommand,VK_SUCCESS);
    }
    {
        const auto semaphore=reinterpret_cast<VkSemaphore>(9876);
        auto consumer=VulkanPresentConsumers().Register(device,VkNrCompletionDeviceGeneration(device),semaphore);
        Check(bool(consumer),"application Present registers downstream wait before forwarding");
        const auto previous=checkpoints;
        Check(ObserveVkNrQueueSubmit(secondQueue,{},[]{return VK_SUCCESS;},{&semaphore,1})==VK_SUCCESS,
              "asynchronous provider wait on different actual queue preserves result");
        Check(checkpoints==previous+1&&!consumer->Complete(),"provider wait has actual owned checkpoint, not CPU completion");
        for(auto& [ownedFence,status]:fenceStatus){(void)ownedFence;status=VK_SUCCESS;}
        PollVkNrCompletions();Check(consumer->Complete(),"real observed consumer checkpoint permits binary semaphore reuse");
        ObserveVkNrQueueSubmit(secondQueue,{},[]{return VK_SUCCESS;},{&semaphore,1});
        Check(checkpoints==previous+1,"one semaphore cycle has only one consumer receipt");
    }
    {
        VulkanPresentRegistry physical;
        physical.DeviceCreated(device,reinterpret_cast<VkPhysicalDevice>(9890),true,true,{0});
        physical.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
        const auto physicalSwap=reinterpret_cast<VkSwapchainKHR>(9891);
        VkSwapchainCreateInfoKHR physicalCreate{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        physicalCreate.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;physicalCreate.imageExtent={64,64};physicalCreate.imageArrayLayers=1;
        physicalCreate.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        physical.SwapchainCreated(device,physicalSwap,physicalCreate,physicalCreate.imageUsage,true,{reinterpret_cast<VkImage>(9892)},{0});
        uint32_t physicalIndex=0;VkPresentInfoKHR physicalInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        physicalInfo.swapchainCount=1;physicalInfo.pSwapchains=&physicalSwap;physicalInfo.pImageIndices=&physicalIndex;
        physical.ImageAcquired(device,physicalSwap,0);
        auto physicalImage=*physical.SnapshotForPresent(queue,physicalInfo,false,0,PresentInput::Policy::ImageOnly,false);
        physical.PresentSucceeded(physicalSwap,0,queue);
        const auto physicalSerial=physical.LastPresentSerial(physicalSwap,0);
        const auto forwardedSignal=reinterpret_cast<VkSemaphore>(9893);
        auto forwarded=VulkanPresentConsumers().Register(device,VkNrCompletionDeviceGeneration(device),forwardedSignal);
        VulkanPresentConsumers().PhysicalPresent(device,VkNrCompletionDeviceGeneration(device),{&forwardedSignal,1},VK_SUCCESS,
            MakeVkPhysicalPresentWait(physicalImage,physicalSerial,physical));
        Check(!forwarded->Complete(),"physical Present return alone cannot complete provider signal");
        physical.ImageAcquired(device,physicalSwap,0);
        VulkanPresentConsumers().RefreshPhysical(device); // next physical Present entry
        const auto acquiredProof=physical.LatestAcquireProof(physicalSwap,0);
        physical.PresentSucceeded(physicalSwap,0,queue);physical.ImageAcquired(device,physicalSwap,0);
        Check(!forwarded->Complete(),"multiple physical Presents before application maintenance retain the exact earlier acquire");
        auto acquiredReceipt=*actual.PrepareSubmission({queue,1,{acquiredProof->use}});
        actual.SubmissionReturned(acquiredReceipt,VK_SUCCESS);actual.CheckpointReturned(acquiredReceipt,VK_SUCCESS);
        Check(!forwarded->Complete(),"pending acquire synchronization keeps signal owned");
        actual.ObserveFence(acquiredReceipt,VK_SUCCESS);Check(forwarded->Complete(),"exact physical acquire checkpoint retires forwarded signal");
        physical.SwapchainDestroyed(physicalSwap);Check(forwarded->Complete(),"retained physical proof survives registry retirement");
    }
    int deviceDestroyCalls = 0;
    Check(ObserveVkNrDeviceDestroy(device,[&]{ ++deviceDestroyCalls; Check(fenceStatus.empty(),"owned fence explicitly destroyed before original device destruction"); }) && deviceDestroyCalls == 1,"device destruction forwarded exactly once after safe cleanup");
    std::puts("actual submissions, application arguments, checkpoints, failures and replay lifetimes verified");
}
