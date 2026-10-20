#include "dlssnr/VulkanPresentConsumer.h"
// Execute the real Present executor against a typed driver/model boundary.
// No game, Vulkan driver, or neural provider is loaded by this CPU regression.
#include <pch.h>
#include <dlssnr/VulkanPresentExecutor.h>
#include <dlssnr/DlssNrFeature_Vk.h>
#include <dlssnr/VulkanPresentGuidesVk.h>
#include <dlssnr/VulkanPresentStatus.h>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace {
void Check(bool value,const char* why) { if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
template<class T> T H(uintptr_t id){return reinterpret_cast<T>(id);}
uintptr_t nextHandle=1000;
VkResult fenceStatus=VK_SUCCESS;
VkResult idleStatus=VK_SUCCESS;
bool forbidDriverCalls=false;
std::unordered_map<VkSemaphore,bool> signals;
std::unordered_map<VkFence,DlssNr::VkNrSubmissionId> submissions;
std::vector<DlssNr::VkNrUseId> modelUses;
int releasedFrames=0;
std::optional<VkExtent2D> expectedRenderSize;
std::vector<bool> historyResets;
bool recordNoWork=false;
bool expectCapturedGuides=false;
bool budgetPhase=false,forceCompositionWait=false;
DlssNr::VkNrWaitBudget::Clock::time_point budgetNow{};
std::shared_ptr<const DlssNr::VkNrWaitBudget> expectedBudget;
std::vector<uint64_t> compositionAllowances,acquireAllowances;
DlssNr::VkNrSubmissionId budgetAcquireReceipt;
}
// Preserve actual immutable configuration and registry/recording owners. Only
// their application's startup and external GPU/model boundaries are replaced.
Config::Config() {}
Config* Config::Instance(){static Config c;return &c;}
Config::DlssNrRuntimeSnapshot Config::GetDlssNrRuntimeSnapshot() const noexcept {return {true,1};}
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out){*out=H<VkSemaphore>(++nextHandle);signals[*out]=true;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){*out=H<VkFence>(++nextHandle);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice,VkSemaphore s,const VkAllocationCallbacks*){Check(!forbidDriverCalls,"no semaphore destroy after device destruction");signals.erase(s);}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){Check(!forbidDriverCalls,"no fence destroy after device destruction");}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice,VkFence fence){if(forceCompositionWait)return VK_NOT_READY;if(auto s=submissions.find(fence);s!=submissions.end())DlssNr::VulkanNrRecordings().ObserveFence(s->second,fenceStatus);return fenceStatus;}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t timeout){
 if(budgetPhase){compositionAllowances.push_back(timeout);budgetNow+=std::chrono::milliseconds(60);forceCompositionWait=false;}
 return fenceStatus==VK_NOT_READY?VK_TIMEOUT:fenceStatus;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice,uint32_t,const VkFence*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t count,const VkSubmitInfo* info,VkFence fence){
 Check(count==1&&info->commandBufferCount==1,"real executor submits its one private recording");
 auto& owner=DlssNr::VulkanNrRecordings();auto use=owner.SnapshotUses(info->pCommandBuffers[0]);Check(bool(use),"owned private command recording");
 auto receipt=owner.PrepareSubmission({q,1,*use});Check(bool(receipt),"owned private submission");
 owner.SubmissionReturned(*receipt,VK_SUCCESS);owner.CheckpointReturned(*receipt,VK_SUCCESS);submissions[fence]=*receipt;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice){Check(!forbidDriverCalls,"no wait on dead device");return idleStatus;}
}
namespace DlssNr {
uint64_t VkNrCompletionDeviceGeneration(VkDevice){return 77;}
uint64_t PresentGenerationPrivateBytesVk(){return 100ull*1024*1024;}
void AbandonPresentRecordingVk(bool){}
void MarkPresentRuntimeUncertainVk(const char*){}
static std::optional<VkNrPresentModelLifetime> fixtureModelLifetime;
std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk(){return fixtureModelLifetime;}
VkResult WaitVkNrUse(VkNrUseId use,uint64_t timeout){
 if(budgetPhase){acquireAllowances.push_back(timeout);Check(timeout==40000000,"acquire wait receives only 40ms after 60ms composition wait");budgetNow+=std::chrono::milliseconds(30);VulkanNrRecordings().ObserveFence(budgetAcquireReceipt,VK_SUCCESS);}
 return VulkanNrRecordings().GpuComplete(use)?VK_SUCCESS:VK_NOT_READY;
}
uint64_t VulkanGuideContextGeneration(const VkNrFrameContract&,const VkNrGuideSelection*){return 1;}
std::optional<VkNrFrameContract> SelectVulkanResolutionMetadata(const VkNrFrameContract&,const VkNrGuideSelection*){return {};}
std::optional<VkNrGuideLease> SelectVulkanPresentGuides(const VkNrFrameContract&,const VkNrGuideSelection*,VkImage){return {};}
PresentColor_Vk::~PresentColor_Vk(){}
VkPresentPrivateFrame::~VkPresentPrivateFrame(){Release(true);}
bool VkPresentPrivateFrame::Initialize(VkInstance,VkPhysicalDevice,VkDevice d,uint32_t family,const VkPresentImageRequest& r){device_=d;request_=r;commandPool_=H<VkCommandPool>(++nextHandle);commandBuffer_=H<VkCommandBuffer>(++nextHandle);VulkanNrRecordings().OnAllocate(d,commandPool_,commandBuffer_,family);allocatedBytes_=uint64_t(r.extent.width)*r.extent.height*32;return true;}
void VkPresentPrivateFrame::Release(bool){if(commandBuffer_){++releasedFrames;VulkanNrRecordings().OnDestroyPool(device_,commandPool_);}commandPool_={};commandBuffer_={};allocatedBytes_=0;device_={};}
#include "dlssnr/VulkanPresentCheck.inl"
bool VkPresentPrivateFrame::Begin(){return true;}
void VkPresentPrivateFrame::SourceToTransfer(){}
void VkPresentPrivateFrame::Capture(){}
void VkPresentPrivateFrame::SourceToPresent(){}
bool VkPresentPrivateFrame::ConvertToModel(){return true;}
bool VkPresentPrivateFrame::Evaluate(bool){if(expectCapturedGuides)::Check(bool(request_.capturedGuides),"reused private frame receives completed captured guides");return true;}
bool VkPresentPrivateFrame::ConvertFromModel(){return true;}
void VkPresentPrivateFrame::TargetToTransfer(){}
void VkPresentPrivateFrame::CopyBack(){}
void VkPresentPrivateFrame::TargetToPresent(){}
bool VkPresentPrivateFrame::End(){return true;}
void VkPresentPrivateFrame::Discard(){}
bool VkPresentPrivateFrame::DiscardCapturedGuides(){ReleaseCapturedGuides();return true;}
VkPresentRecordResult RecordVkPresentImage(const VkPresentImageRequest& r,IVkPresentCommands& commands,bool reset){
 if(expectCapturedGuides){Check(r.capturedGuides&&r.inputDecision.input==PresentInputDecision::InputClass::Guided,"captured route cannot select image-only policy");Check(commands.Check(r)&&commands.Evaluate(false),"captured private frame binds its actual input");}
 historyResets.push_back(reset);
 if(budgetPhase)Check(r.waitBudget==expectedBudget&&r.waitBudget->RemainingNs()==10000000,"model recording inherits exact shared budget with only 10ms remaining");
 if(recordNoWork)return {VkRecordStatus::NoWork,"injected model refusal"};
 if(expectedRenderSize) {
  Check(r.observedRenderSize&&r.observedRenderSize->width==expectedRenderSize->width&&r.observedRenderSize->height==expectedRenderSize->height,
      "real executor passes scalar render size to model recording");
  Check(!r.guides&&!r.frame.temporal,"scalar size grants no temporal or image guide rights");
  Check(!r.inputDecision.workloadFallback,"scalar Match Native avoids full-output fallback");
 }
 auto& owner=VulkanNrRecordings();owner.OnReset(r.commandBuffer,VK_SUCCESS);owner.OnBegin(r.commandBuffer,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 auto use=owner.Reserve(r.commandBuffer,1);Check(bool(use)&&owner.RetainUse(*use),"private model recording owns actual retained use");modelUses.push_back(*use);
 owner.OnEnd(r.commandBuffer,VK_SUCCESS);VkPresentRecordResult result{VkRecordStatus::Complete,"",true,true};result.use=*use;
 fixtureModelLifetime=VkNrPresentModelLifetime{reinterpret_cast<uintptr_t>(owner.UseDevice(*use)),r.frame.deviceGeneration,99,1,0};
 result.modelLifetime=fixtureModelLifetime;return result;
}
}
int main(){
 using namespace DlssNr;
 auto& cfg=*Config::Instance();cfg.DlssNrRoute=1;cfg.DlssNrPresentResolution=1;
 auto settings=std::make_shared<NrConfigSnapshot<Config>>(cfg);
 const auto device=H<VkDevice>(1);
 const auto queue=H<VkQueue>(3);
 auto& registry=GetVulkanPresentRegistry();registry.DeviceCreated(device,H<VkPhysicalDevice>(4),true,true,{0});registry.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
 {
  VulkanPresentExecutor generated;
  auto swap=H<VkSwapchainKHR>(850);uint32_t index=0;
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,{H<VkImage>(851),H<VkImage>(852),H<VkImage>(853)},{0});
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
  auto real=[&] {
   registry.ImageAcquired(device,swap,index,H<VkSemaphore>(854));
   auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,true);
   observed.applicationFacing=true;
   auto result=generated.BeforePresent(observed,H<VkInstance>(5),pi,settings,{},{},[](uint64_t,const VkNrPresentModelLifetime&){VkPresentProviderPreparation p;p.prepared=true;p.effects=VkProviderPrepareEffects::ExternalEntered;return p;});
   Check(result.handoff.state==VkPresentHandoffState::Accepted,"application frame before MFG is admitted");
   generated.OriginalPresentReturned(result.ticket,VK_SUCCESS);registry.PresentSucceeded(swap,index,queue);
  };
  real();Check(historyResets.back(),"first application frame seeds NR history");
  for(auto known:{true,false}) {
   auto physical=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,true);
   physical.request.fgStateKnown=known;
   const auto before=historyResets.size();
   Check(!generated.BeforePresent(physical,H<VkInstance>(5),pi,settings).admission.allowed,
       "physical generated/provider-unknown frame bypasses NR");
   Check(historyResets.size()==before,"generated frame did not record NR");
   ++index;real();Check(!historyResets.back(),"physical MFG bypass preserves real-frame NR history");
  }
  registry.ImageAcquired(device,swap,index,H<VkSemaphore>(854));
  auto physical=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  auto returned=generated.BeforePresent(physical,H<VkInstance>(5),pi,settings);
  Check(returned.handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),"returning to physical real frames resets history");
  generated.OriginalPresentReturned(returned.ticket,VK_SUCCESS);registry.PresentSucceeded(swap,index,queue);
  index=1;registry.ImageAcquired(device,swap,index,H<VkSemaphore>(854));
  physical=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  auto unknown=physical;unknown.request.fgStateKnown=false;
  Check(!generated.BeforePresent(unknown,H<VkInstance>(5),pi,settings).admission.allowed,"unknown physical real frame refuses NR");
  auto recovered=generated.BeforePresent(physical,H<VkInstance>(5),pi,settings);
  Check(recovered.handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),
      "unknown provider on the physical real-frame route still resets skipped history");
  generated.OriginalPresentReturned(recovered.ticket,VK_SUCCESS);
  generated.Shutdown(false);registry.SwapchainDestroyed(swap);signals.clear();
  for(auto use:modelUses)VulkanNrRecordings().ReleaseUse(use);
  modelUses.clear();historyResets.clear();releasedFrames=0;
 }
 {
  VulkanPresentExecutor recovery;
  auto swap=H<VkSwapchainKHR>(900);
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,{H<VkImage>(910),H<VkImage>(911),H<VkImage>(912)},{0});
  uint32_t index=0;auto acquire=H<VkSemaphore>(920);
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
  auto attempt=[&]{
   auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
   auto result=recovery.BeforePresent(observed,H<VkInstance>(5),pi,settings);
   if(result.ticket)recovery.OriginalPresentReturned(result.ticket,VK_SUCCESS);
   registry.PresentSucceeded(swap,index);
   return result;
  };
  registry.ImageAcquired(device,swap,index,acquire);
  Check(attempt().handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),"first Present seeds history");
  {
   VulkanPresentExecutor inactive;
   registry.ImageAcquired(device,swap,index,acquire);
   const auto active=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
   auto initialized=inactive.BeforePresent(active,H<VkInstance>(5),pi,settings);
   Check(initialized.handoff.state==VkPresentHandoffState::Accepted,"inactive fixture records actual private work");
   inactive.OriginalPresentReturned(initialized.ticket,VK_SUCCESS);
   registry.PresentSucceeded(swap,index,queue);
   const auto before=releasedFrames;
   const auto signalCount=signals.size();
   auto off=*registry.SnapshotForPresent(queue,pi,false,1,PresentInput::Policy::ImageOnly,false);
   fenceStatus=VK_NOT_READY;inactive.Maintain(off,true);
   Check(releasedFrames==before,"inactive maintenance retains pending private work");
   fenceStatus=VK_SUCCESS;inactive.Maintain(off,true);
   Check(releasedFrames==before+1,"inactive maintenance releases completed private images without device idle");
   Check(signals.size()==signalCount,"private release preserves unresolved WSI semaphore");
   inactive.Maintain(off,true);
   Check(releasedFrames==before+1,"inactive maintenance does not double-release private frames");
   registry.ImageAcquired(device,swap,index,acquire);
   auto dependencies=registry.PrepareAcquireWait(queue,{&acquire,1},1);
   auto& uses=VulkanNrRecordings();auto proofReceipt=*uses.PrepareSubmission({queue,1,dependencies});
   uses.SubmissionReturned(proofReceipt,VK_SUCCESS);uses.CheckpointReturned(proofReceipt,VK_SUCCESS);
   registry.AcquireWaitSubmitted(queue,dependencies,VK_SUCCESS);
   off=*registry.SnapshotForPresent(queue,pi,false,1,PresentInput::Policy::ImageOnly,false);
   inactive.Maintain(off,true);Check(signals.size()==signalCount,"pending reacquire proof cannot retire presentation wait");
   registry.PresentSucceeded(swap,index,queue);registry.ImageAcquired(device,swap,index,acquire);
   uses.ObserveFence(proofReceipt,VK_SUCCESS);
   off=*registry.SnapshotForPresent(queue,pi,false,1,PresentInput::Policy::ImageOnly,false);
   inactive.Maintain(off,false);
   Check(signals.size()==signalCount-1,"delayed exact proof survives registry overwrite and retires released slot while active");
   auto activeAgain=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
   auto again=inactive.BeforePresent(activeAgain,H<VkInstance>(5),pi,settings);
   Check(again.handoff.state==VkPresentHandoffState::Accepted,"fresh resume after private frame retirement");
   registry.PresentSucceeded(swap,index,queue);
   inactive.OriginalPresentReturned(again.ticket,VK_SUCCESS,registry.LastPresentSerial(swap,index));
   const auto priorSignals=signals.size();
   VkSwapchainKHR successor=H<VkSwapchainKHR>(995);auto replacement=ci;replacement.oldSwapchain=swap;
   registry.SwapchainCreated(device,successor,replacement,ci.imageUsage,true,{H<VkImage>(996)},{0});
   registry.ImageAcquired(device,successor,0,acquire);registry.PresentSucceeded(successor,0,queue);
   auto successorFence=H<VkFence>(997);registry.ImageAcquired(device,successor,0,VK_NULL_HANDLE,successorFence);
   VulkanPresentRegistry::ObserveAcquireFence(registry.AcquireFenceProof(device,successorFence),VK_SUCCESS);
   auto successorPi=pi;uint32_t successorIndex=0;successorPi.pSwapchains=&successor;successorPi.pImageIndices=&successorIndex;
   auto successorObserved=*registry.SnapshotForPresent(queue,successorPi,false,1,PresentInput::Policy::ImageOnly,false);
   inactive.Maintain(successorObserved,true);
   Check(signals.size()==priorSignals-1,"NR-off completed explicit successor retires predecessor WSI slot");
   registry.SwapchainDestroyed(successor);
  }
  index=1;registry.ImageAcquired(device,swap,index,acquire);
  Check(attempt().handoff.state==VkPresentHandoffState::Accepted&&!historyResets.back(),"continuous Present preserves history across image slots");
  const auto recordsBeforeTimeout=historyResets.size(),signalsBeforeTimeout=signals.size();
  index=2;registry.ImageAcquired(device,swap,index,acquire);fenceStatus=VK_NOT_READY;
  Check(attempt().handoff.state==VkPresentHandoffState::NoSubmission,"pending composition bypasses model recording");
  Check(historyResets.size()==recordsBeforeTimeout&&signals.size()==signalsBeforeTimeout,"timeout retains pending semaphore and does not record");
  registry.ImageAcquired(device,swap,index,acquire);
  Check(attempt().handoff.state==VkPresentHandoffState::NoSubmission,"repeated timeout retains predecessor completion gate");
  fenceStatus=VK_SUCCESS;registry.ImageAcquired(device,swap,index,acquire);
  Check(attempt().handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),"first actual recording after composition timeout resets history");
  // Reuse the first slot only after its exact new acquire wait is observed complete.
  index=0;registry.ImageAcquired(device,swap,index,acquire);
  auto& owner=VulkanNrRecordings();auto cb=H<VkCommandBuffer>(930);
  owner.OnAllocate(device,H<VkCommandPool>(931),cb,0);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
  registry.PrepareAcquireWait(queue,{&acquire,1},1);
  auto receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
  owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
  recordNoWork=true;
  Check(attempt().handoff.state==VkPresentHandoffState::NoSubmission&&!historyResets.back(),"successful recovery clears reset before later model refusal");
  recordNoWork=false;
  registry.ImageAcquired(device,swap,index,acquire);
  owner.OnReset(cb,VK_SUCCESS);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
  registry.PrepareAcquireWait(queue,{&acquire,1},1);
  receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
  owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
  auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  auto resumed=recovery.BeforePresent(observed,H<VkInstance>(5),pi,settings);
  Check(resumed.handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),"model refusal also preserves reset until successful submission");
  recovery.OriginalPresentReturned(resumed.ticket,VK_SUCCESS);
  registry.PresentSucceeded(swap,index);registry.ImageAcquired(device,swap,index,acquire);
  owner.OnReset(cb,VK_SUCCESS);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
  registry.PrepareAcquireWait(queue,{&acquire,1},1);
  receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
  owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
  observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  budgetNow={};expectedBudget=std::make_shared<VkNrWaitBudget>(budgetNow,[]{return budgetNow;});
  // Recreate a still-pending acquisition checkpoint: completion is published
  // by the simulated wait, matching the real adapter instead of pre-completing it.
  registry.ImageAcquired(device,swap,index,acquire);
  auto budgetDependencies=registry.PrepareAcquireWait(queue,{&acquire,1},1);
  budgetAcquireReceipt=*owner.PrepareSubmission({queue,1,budgetDependencies});
  owner.SubmissionReturned(budgetAcquireReceipt,VK_SUCCESS);owner.CheckpointReturned(budgetAcquireReceipt,VK_SUCCESS);
  registry.AcquireWaitSubmitted(queue,budgetDependencies,VK_SUCCESS);
  observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  budgetPhase=true;forceCompositionWait=true;
  auto bounded=recovery.BeforePresent(observed,H<VkInstance>(5),pi,settings,{},expectedBudget);
  Check(bounded.handoff.state==VkPresentHandoffState::Accepted,"shared wait budget still permits proved slot reuse");
  Check(compositionAllowances==std::vector<uint64_t>{100000000}&&acquireAllowances==std::vector<uint64_t>{40000000},"one Present attempt cannot renew its fence allowance");
  recovery.OriginalPresentReturned(bounded.ticket,VK_SUCCESS);budgetPhase=false;expectedBudget.reset();
  registry.PresentSucceeded(swap,index);registry.ImageAcquired(device,swap,index,acquire);
  owner.OnReset(cb,VK_SUCCESS);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
  registry.PrepareAcquireWait(queue,{&acquire,1},1);
  receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
  owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
  observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  auto refused=observed;refused.capabilities.routePreparedAtDeviceCreation=false;
  const auto beforeAdmissionRefusal=historyResets.size();
  Check(recovery.BeforePresent(refused,H<VkInstance>(5),pi,settings).handoff.state==VkPresentHandoffState::NoSubmission,
      "admission refusal skips model recording");
  Check(historyResets.size()==beforeAdmissionRefusal,"admission refusal did not evaluate model");
  auto readmitted=recovery.BeforePresent(observed,H<VkInstance>(5),pi,settings);
  Check(readmitted.handoff.state==VkPresentHandoffState::Accepted&&historyResets.back(),"admission recovery resets skipped-frame history");
  recovery.OriginalPresentReturned(readmitted.ticket,VK_SUCCESS);
  registry.PresentSucceeded(swap,index,queue);
  const auto acquireFence=H<VkFence>(990);
  registry.ImageAcquired(device,swap,index,VK_NULL_HANDLE,acquireFence);
  auto fenceProof=registry.AcquireFenceProof(device,acquireFence);
  VulkanPresentRegistry::ObserveAcquireFence(fenceProof,VK_SUCCESS);
  auto fenceObserved=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  auto fenceResume=recovery.BeforePresent(fenceObserved,H<VkInstance>(5),pi,settings);
  Check(fenceResume.handoff.state==VkPresentHandoffState::Accepted,"fence-only acquisition completion admits Present slot reuse");
  recovery.OriginalPresentReturned(fenceResume.ticket,VK_SUCCESS);
  owner.OnFree(cb);recovery.Shutdown(false);registry.SwapchainDestroyed(swap);signals.clear();
  for(auto use:modelUses)owner.ReleaseUse(use);modelUses.clear();historyResets.clear();releasedFrames=0;
 }
 VulkanPresentExecutor executor;
 // Execute the released private-frame/WSI split through the actual executor.
 // Fence completion permits OUT_OF_DATE private release; it never retires the
 // binary Present signal. A genuine unknown result retains both owners.
 for (const auto presentResult : {VK_ERROR_OUT_OF_DATE_KHR, VK_ERROR_DEVICE_LOST}) {
  VulkanPresentExecutor outcome;
  auto swap=H<VkSwapchainKHR>(9300+unsigned(-presentResult));
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=H<VkSurfaceKHR>(10);
  ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,{H<VkImage>(9400)},{0});
  uint32_t index=0;registry.ImageAcquired(device,swap,index,H<VkSemaphore>(9401));
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
  auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
  observed.applicationFacing=false;
  auto accepted=outcome.BeforePresent(observed,H<VkInstance>(5),pi,settings);
  Check(accepted.handoff.state==VkPresentHandoffState::Accepted,"outcome fixture requires authentic accepted private submission");
  const auto signal=accepted.handoff.signal;
  const auto releaseBefore=releasedFrames;
  const auto use=modelUses.back();
  outcome.OriginalPresentReturned(accepted.ticket,presentResult);
  auto inactive=observed;
  fenceStatus=VK_NOT_READY;outcome.Maintain(inactive,true);
  Check(releasedFrames==releaseBefore&&signals.contains(signal),"incomplete private fence retains private frame and Present signal for any outcome");
  fenceStatus=VK_SUCCESS;outcome.Maintain(inactive,true);
  if(presentResult==VK_ERROR_OUT_OF_DATE_KHR) {
   Check(releasedFrames==releaseBefore+1,"OUT_OF_DATE completed private frame releases independently of WSI retirement");
   Check(VulkanNrRecordings().Reusable(use),"OUT_OF_DATE private release ends actual recording replay references");
  } else Check(releasedFrames==releaseBefore,"genuine Present uncertainty retains private frame even after fence success");
  Check(signals.contains(signal),"private fence completion never retires unresolved Present signal");
  outcome.Shutdown(true);
  Check(signals.contains(signal),"device idle alone never retires unresolved Present signal");
  Check(releasedFrames==releaseBefore+(presentResult==VK_ERROR_OUT_OF_DATE_KHR?1:0),"shutdown preserves uncertain private ownership");
  outcome.DeviceDestroyed(device);registry.SwapchainDestroyed(swap);
  signals.erase(signal); // Fake driver device teardown, after actual executor device-death cleanup.
 }
 for(uint32_t generation=1;generation<=16;++generation){
  auto swap=H<VkSwapchainKHR>(20+generation);
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=H<VkSurfaceKHR>(10);
  ci.imageFormat=generation==1?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_A2B10G10R10_UNORM_PACK32;ci.imageColorSpace=generation==1?VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:VK_COLOR_SPACE_HDR10_ST2084_EXT;
  ci.imageExtent=generation==1?VkExtent2D{2542,1345}:VkExtent2D{3840,2160};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,{H<VkImage>(100),H<VkImage>(101),H<VkImage>(102)},{0});
  for(uint32_t index=0;index<3;++index){
   cfg.DlssNrPresentResolution=index==0?PresentResolution::FollowNative:index==1?PresentResolution::FullOutput:PresentResolution::Manual;
   cfg.DlssNrPresentCustomScale=50;settings=std::make_shared<NrConfigSnapshot<Config>>(cfg);
   expectedRenderSize=VkExtent2D{ci.imageExtent.width/2,ci.imageExtent.height/2};
   registry.ObserveNativeRenderSize(device,ci.imageExtent,{*expectedRenderSize,generation});
   registry.ImageAcquired(device,swap,index,H<VkSemaphore>(200));
   VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
   auto observed=registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);Check(bool(observed),"actual swapchain snapshot");
   auto result=executor.BeforePresent(*observed,H<VkInstance>(5),pi,settings);
   if(result.handoff.state!=VkPresentHandoffState::Accepted)std::fprintf(stderr,"generation=%u image=%u reason=%s\n",generation,index,result.reason.c_str());
   Check(result.handoff.state==VkPresentHandoffState::Accepted,"Quality/Performance/format recreation keeps Present recording within its original budget");
   executor.OriginalPresentReturned(result.ticket,VK_SUCCESS);registry.PresentSucceeded(swap,index);
  }
  // Destruction observation cannot by itself prove presentation-wait retirement.
  registry.SwapchainDestroyed(swap);
 }
 expectedRenderSize.reset();cfg.DlssNrPresentResolution=1;settings=std::make_shared<NrConfigSnapshot<Config>>(cfg);
 Check(releasedFrames>=45,"completed superseded private images are released");
 for(size_t i=0;i<45;++i)Check(VulkanNrRecordings().Reusable(modelUses[i]),"destroyed completed private pool ends real model recording references");
 Check(signals.size()>=3,"unproved presentation semaphores stay owned");
 executor.Shutdown(false);
 signals.clear(); // Device teardown implicitly releases the fake driver's WSI objects.

 // Explicit oldSwapchain succession is different from an unrelated new surface.
 // Reacquisition alone must keep the predecessor semaphore, and a consumed
 // successor acquire must release it. This executes the actual registry/owner.
 VulkanPresentExecutor chained;
 auto old=H<VkSwapchainKHR>(300), next=H<VkSwapchainKHR>(301);
 VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=H<VkSurfaceKHR>(10);
 ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
 ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 uint32_t index=0;auto acquire=H<VkSemaphore>(200);
 VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pImageIndices=&index;pi.pSwapchains=&old;
 registry.SwapchainCreated(device,old,ci,ci.imageUsage,true,{H<VkImage>(100),H<VkImage>(102)},{0});registry.ImageAcquired(device,old,index,acquire);
 auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
 auto r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);Check(r.handoff.state==VkPresentHandoffState::Accepted,"predecessor Present");
 chained.OriginalPresentReturned(r.ticket,VK_SUCCESS);registry.PresentSucceeded(old,index);
 registry.ImageAcquired(device,old,1,acquire); // Legal outstanding acquisition before retirement.
 ci.oldSwapchain=old;registry.SwapchainCreated(device,next,ci,ci.imageUsage,true,{H<VkImage>(101)},{0});
 pi.pSwapchains=&next;registry.ImageAcquired(device,next,index,acquire);
 observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);Check(r.handoff.state==VkPresentHandoffState::Accepted,"successor Present");
 chained.OriginalPresentReturned(r.ticket,VK_SUCCESS);registry.PresentSucceeded(next,index);
 Check(signals.size()==2,"new Present and completed private work do not prove predecessor WSI retirement");
 // An already-acquired retired image may be presented AFTER the first successor
 // Present. That later old WSI wait cannot retire with the earlier successor.
 uint32_t lateIndex=1;VkPresentInfoKHR late=pi;late.pSwapchains=&old;late.pImageIndices=&lateIndex;
 auto oldObserved=*registry.SnapshotForPresent(queue,late,true,1,PresentInput::Policy::ImageOnly,false);
 auto oldResult=chained.BeforePresent(oldObserved,H<VkInstance>(5),late,settings);
 Check(oldResult.handoff.state==VkPresentHandoffState::Accepted,"late already-acquired retired image Present");
 chained.OriginalPresentReturned(oldResult.ticket,VK_SUCCESS);registry.PresentSucceeded(old,lateIndex);registry.SwapchainDestroyed(old);
 Check(signals.size()==3,"late old Present has a distinct owned wait");
 registry.ImageAcquired(device,next,index,acquire);
 observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);
 Check(r.handoff.state==VkPresentHandoffState::NoSubmission&&signals.size()==3,"acquire return alone retains all WSI waits");
 auto& owner=VulkanNrRecordings();auto cb=H<VkCommandBuffer>(400);
 owner.OnAllocate(device,H<VkCommandPool>(401),cb,0);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
 registry.PrepareAcquireWait(queue,{&acquire,1},1);
 auto use=registry.AcquireUse(next,index);Check(bool(use),"successor acquire uses actual observed wait");
 auto receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
 owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);
 Check(r.handoff.state==VkPresentHandoffState::Accepted&&signals.size()==2,"successor completion retires only earlier predecessor Present, not the later old Present");
 chained.OriginalPresentReturned(r.ticket,VK_SUCCESS);registry.PresentSucceeded(next,index);
 registry.ImageAcquired(device,next,index,acquire);owner.OnReset(cb,VK_SUCCESS);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
 registry.PrepareAcquireWait(queue,{&acquire,1},1);receipt=*owner.PrepareSubmission({queue,1,std::vector<VkNrUseId>{registry.AcquireUse(pi.pSwapchains[0],index)}});
 owner.SubmissionReturned(receipt,VK_SUCCESS);owner.CheckpointReturned(receipt,VK_SUCCESS);owner.ObserveFence(receipt,VK_SUCCESS);
 observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);
 Check(r.handoff.state==VkPresentHandoffState::Accepted&&signals.size()==1,"later successor completion retires the delayed predecessor wait");
 chained.OriginalPresentReturned(r.ticket,VK_SUCCESS);registry.PresentSucceeded(next,index);
 owner.OnFree(cb);registry.SwapchainDestroyed(next);
 idleStatus=VK_ERROR_DEVICE_LOST;auto retained=signals.size();auto frames=releasedFrames;
 chained.Shutdown(true);Check(signals.size()==retained&&releasedFrames==frames,"idle failure retains resources rather than inventing destruction");
 idleStatus=VK_SUCCESS;chained.Shutdown(true);
 Check(!signals.empty(),"device idle cannot release unresolved WSI semaphore");
 forbidDriverCalls=true;
 chained.DeviceDestroyed(H<VkDevice>(999)); // foreign device must leave quarantine
 chained.NotifyDeviceInit();
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);
 Check(r.handoff.state!=VkPresentHandoffState::Accepted,"foreign device cleanup leaves session closed");
 chained.DeviceDestroyed(device);chained.DeviceDestroyed(device);
 chained.NotifyDeviceInit();
 // Admission can reopen only after the matching records have actually been removed.
 // Registry has retired this swapchain; this checks the executor's closed reason.
 observed.request.swapchainGeneration=0;
 r=chained.BeforePresent(observed,H<VkInstance>(5),pi,settings);
 Check(std::string(r.reason).find("session closed")==std::string::npos,"matching device cleanup removes CPU quarantine");
 chained.Shutdown(false);forbidDriverCalls=false;
 {
  auto& application=GetVulkanApplicationPresentRegistry();
  Check(application.ImportDeviceAndQueues(registry,device),"app registry shares device facts");
  const auto appSwap=H<VkSwapchainKHR>(50000);uint32_t appIndex=0;
  VkSwapchainCreateInfoKHR appCreate{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  appCreate.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;appCreate.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  appCreate.imageExtent={64,64};appCreate.imageArrayLayers=1;appCreate.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  application.SwapchainCreated(device,appSwap,appCreate,appCreate.imageUsage,true,{H<VkImage>(50001)},{0});
  application.ImageAcquired(device,appSwap,0);
  VkPresentInfoKHR appInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};appInfo.swapchainCount=1;appInfo.pSwapchains=&appSwap;appInfo.pImageIndices=&appIndex;
  auto snapshot=[&]{auto value=*application.SnapshotForPresent(queue,appInfo,true,1,PresentInput::Policy::ImageOnly,true);value.applicationFacing=true;return value;};
  VulkanPresentExecutor app;int policyCalls=0;
  VulkanPresentExecutor deniedApp;auto denied=deniedApp.BeforePresent(snapshot(),H<VkInstance>(5),appInfo,settings,{}, {},[&](uint64_t,const VkNrPresentModelLifetime&){++policyCalls;return VkPresentProviderPreparation{};});
  Check(denied.handoff.state==VkPresentHandoffState::Uncertain&&policyCalls==1,"entered provider failure preserves uncertain ownership and original waits");deniedApp.Shutdown(false);
  auto appFirst=app.BeforePresent(snapshot(),H<VkInstance>(5),appInfo,settings,{}, {},[&](uint64_t,const VkNrPresentModelLifetime&){++policyCalls;VkPresentProviderPreparation p;p.prepared=true;p.effects=VkProviderPrepareEffects::ExternalEntered;return p;});
  Check(appFirst.handoff.state==VkPresentHandoffState::Accepted,"app final image records and submits before provider");
  application.PresentSucceeded(appSwap,0,queue);app.OriginalPresentReturned(appFirst.ticket,VK_SUCCESS,application.LastPresentSerial(appSwap,0));
  application.ImageAcquired(device,appSwap,0);
  auto pending=app.BeforePresent(snapshot(),H<VkInstance>(5),appInfo,settings,{}, {},[](uint64_t,const VkNrPresentModelLifetime&){VkPresentProviderPreparation p;p.prepared=true;p.effects=VkProviderPrepareEffects::ExternalEntered;return p;});
  Check(pending.handoff.state==VkPresentHandoffState::NoSubmission,"proxy reacquire and completed NR fence do not recycle unconsumed provider wait");
  auto consume=[&](VkSemaphore signal){
   auto consumerUses=VulkanPresentConsumers().Prepare(device,77,0,{&signal,1});Check(consumerUses.size()==1,"actual owned signal is registered for provider wait");
   auto& consumerOwner=VulkanNrRecordings();auto consumerReceipt=*consumerOwner.PrepareSubmission({queue,77,consumerUses});consumerOwner.SubmissionReturned(consumerReceipt,VK_SUCCESS);consumerOwner.CheckpointReturned(consumerReceipt,VK_SUCCESS);VulkanPresentConsumers().Returned(consumerUses,VK_SUCCESS);consumerOwner.ObserveFence(consumerReceipt,VK_SUCCESS);
  };
  consume(appFirst.handoff.signal);
  auto retryPolicy=app.BeforePresent(snapshot(),H<VkInstance>(5),appInfo,settings,{}, {},{});
  Check(retryPolicy.handoff.state==VkPresentHandoffState::NoSubmission,"recycled slot can refuse a missing callback before any provider entry without becoming pending");
  auto appSecond=app.BeforePresent(snapshot(),H<VkInstance>(5),appInfo,settings,{}, {},[](uint64_t,const VkNrPresentModelLifetime&){VkPresentProviderPreparation p;p.prepared=true;p.effects=VkProviderPrepareEffects::ExternalEntered;return p;});
  Check(appSecond.handoff.state==VkPresentHandoffState::Accepted&&appSecond.handoff.signal==appFirst.handoff.signal,"only completed provider consumption allows binary signal reuse");
  application.PresentSucceeded(appSwap,0,queue);app.OriginalPresentReturned(appSecond.ticket,VK_SUCCESS,application.LastPresentSerial(appSwap,0));
  const auto signalCountApp=signals.size();consume(appSecond.handoff.signal);
  auto physical=snapshot();physical.applicationFacing=false;
  app.Maintain(physical,false);
  Check(signals.size()==signalCountApp-1,"FG-off physical boundary retires completed application slots");
  app.Shutdown(false);application.SwapchainDestroyed(appSwap);
 }
 {
  auto swap=H<VkSwapchainKHR>(65000);uint32_t capturedIndex=0;
  VkSwapchainCreateInfoKHR capturedCreate{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};capturedCreate.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;capturedCreate.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  capturedCreate.imageExtent={64,64};capturedCreate.imageArrayLayers=1;capturedCreate.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,capturedCreate,capturedCreate.imageUsage,true,{H<VkImage>(65001)},{0});registry.ImageAcquired(device,swap,0);
  VkPresentInfoKHR capturedInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};capturedInfo.swapchainCount=1;capturedInfo.pSwapchains=&swap;capturedInfo.pImageIndices=&capturedIndex;
  const auto capturedObserved=*registry.SnapshotForPresent(queue,capturedInfo,true,1,PresentInput::Policy::ImageOnly,false);
  auto guides=std::make_shared<VkCapturedGuideInput>();guides->identity={reinterpret_cast<uintptr_t>(device),capturedObserved.request.deviceGeneration,reinterpret_cast<uintptr_t>(swap),capturedObserved.request.swapchainGeneration,capturedObserved.request.acquireGeneration,reinterpret_cast<uintptr_t>(capturedObserved.image),12,1,0,64,64,true};
  guides->lease=std::make_shared<int>(1);guides->bind=[](VkNrUseId){return true;};
  VulkanPresentExecutor captured;expectCapturedGuides=true;
  auto wrong=std::make_shared<VkCapturedGuideInput>(*guides);wrong->identity.acquisition++;
  Check(captured.BeforePresent(capturedObserved,H<VkInstance>(5),capturedInfo,settings,{},{},{},wrong).handoff.state==VkPresentHandoffState::NoSubmission,"wrong captured acquisition rejected before model submission");
  wrong->identity=guides->identity;wrong->identity.completed=false;
  Check(captured.BeforePresent(capturedObserved,H<VkInstance>(5),capturedInfo,settings,{},{},{},wrong).handoff.state==VkPresentHandoffState::NoSubmission,"unfinished captured producer rejected before model submission");
  recordNoWork=true;
  Check(captured.BeforePresent(capturedObserved,H<VkInstance>(5),capturedInfo,settings,{},{},{},guides).handoff.state==VkPresentHandoffState::NoSubmission,
    "ordinary captured model refusal remains a non-submission");
  Check(guides.use_count()==1,"refused recording releases captured lease before native resize");recordNoWork=false;
  auto output=captured.BeforePresent(capturedObserved,H<VkInstance>(5),capturedInfo,settings,{},{},{},guides);
  Check(output.admission.actualInputClass==PresentInputDecision::InputClass::Guided,"captured admission reports its actual guided workload");
  Check(output.handoff.state==VkPresentHandoffState::Accepted&&captured.CompleteCapturedGuides(output.ticket),"exact completed capture reaches model and joins its actual consumer fence");
  captured.OriginalPresentReturned(output.ticket,VK_SUCCESS);captured.Shutdown(false);expectCapturedGuides=false;registry.SwapchainDestroyed(swap);
 }
 for(auto modelUse:modelUses)VulkanNrRecordings().ReleaseUse(modelUse);
 std::puts("PASS real Present executor survives repeated window/4K/packed-format recreation");
}
