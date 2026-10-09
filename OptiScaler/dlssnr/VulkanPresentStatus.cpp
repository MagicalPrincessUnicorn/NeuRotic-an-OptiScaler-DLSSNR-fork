#include "VulkanPresentStatus.h"
#include <chrono>

namespace DlssNr
{
void VulkanPresentStatus::ObserveFgOwnership(uint64_t primary,uint64_t consumers)
{ std::lock_guard lock(mutex_);state_.realFrameContributions=primary;state_.retainedFgConsumers=consumers; }
PresentPacing::CallToken VulkanPresentStatus::BeginCall(uint32_t route,double startMs)
{
    std::lock_guard lock(mutex_);PresentPacing::CallToken token;
    pacing_.beginCall(static_cast<PresentPacing::Route>(std::min(route,2u)),++calls_,token);
    if(callIntervals_.size()<4096)callIntervals_[token.call]=priorMs_>0?std::max(startMs-priorMs_,0.):0.;priorMs_=std::max(priorMs_,startMs);
    return token;
}
void VulkanPresentStatus::FinishCall(const PresentPacing::CallToken& token,double adapterMs,double hookMs,double originalMs,bool failed)
{
    std::lock_guard lock(mutex_);const auto interval=callIntervals_.find(token.call);
    if(interval!=callIntervals_.end()){pacing_.recordCall(token,{interval->second,adapterMs,hookMs,originalMs,failed});callIntervals_.erase(interval);}state_.pacing=pacing_.summary();
}
void VulkanPresentStatus::ExpectGpu(const PresentPacing::CallToken& token,VkNrUseId use,uint32_t passes,double atMs)
{
    std::lock_guard lock(mutex_);
    std::erase_if(pendingGpu_,[&](const auto& p){return p.token.serial!=token.serial;});
    if(use&&passes&&passes<=10&&pendingGpu_.size()<256&&pacing_.expectGpu(token))pendingGpu_.push_back({token,use,passes,atMs,{},{}});
}
bool VulkanPresentStatus::ObserveGpuStages(const VkNrRecordingOwner& owner,const VkNrGpuStageSample& sample)
{
    if(sample.pass>=10||!sample.use||!owner.GpuComplete(sample.use)||
       std::any_of(sample.milliseconds.begin(),sample.milliseconds.end(),[](double ms){return !std::isfinite(ms)||ms<0||ms>1000;}))return false;
    std::lock_guard lock(mutex_);auto& stages=sample.frame.route==VkNrRoute::Present?state_.gpuStages:state_.nativeGpuStages;
    if(sample.frame.route==VkNrRoute::Present&&sample.frame.swapchainGeneration!=state_.generation)return false;
    if(stages[sample.pass].use.value>=sample.use.value)return false;
    stages[sample.pass]=sample;
    for(auto it=pendingGpu_.begin();it!=pendingGpu_.end();++it)if(it->use==sample.use&&sample.pass<it->passes){
        it->ready[sample.pass]=true;it->values[sample.pass]=0;for(auto ms:sample.milliseconds)it->values[sample.pass]+=ms;
        if(std::all_of(it->ready.begin(),it->ready.begin()+it->passes,[](bool ready){return ready;})){
            double sum=0;for(uint32_t p=0;p<it->passes;++p)sum+=it->values[p];
            const double now=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
            pacing_.recordGpu(it->token,sum,std::max(now-it->atMs,0.),calls_-it->token.call);pendingGpu_.erase(it);state_.pacing=pacing_.summary();
        }break;
    }
    return true;
}

void VulkanPresentStatus::Observe(const VkObservedPresent* observed, const VkPresentExecution* execution,
                                  bool originalCalled, VkResult originalResult, const char* reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& s = state_;
    ++s.attempts;
    s.requested = true;
    s.active = false;
    s.failed = false;
    s.possibleTargetWrite = false;
    s.needsRecreate = false;
    s.reason = reason != nullptr ? reason : "";
    if (observed)
    {
        s.route = observed->request.route;
        s.requestedPolicy = observed->request.policy;
        s.format = observed->request.format;
        s.colorSpace = observed->request.colorSpace;
        s.extent = observed->request.extent;
        s.generation = observed->request.swapchainGeneration;
    }
    if (!execution)
    {
        s.actualInputClass = PresentInputDecision::InputClass::Refused;
        s.failed = true;
        ++s.consecutiveFallbacks;
        return;
    }
    s.actualInputClass = execution->admission.actualInputClass;
    s.requestedPasses=execution->record.requestedPasses;
    s.completedPasses=execution->record.completedPasses;
    s.outputVersion=execution->record.outputVersion;
    s.needsRecreate = execution->admission.needsRecreate;
    if (execution->admission.reason == VkPresentRefusal::AutoImageOnly && s.reason.empty())
        s.reason = VkPresentRefusalText(execution->admission.reason);
    if (execution->record.modelRecorded) ++s.recorded;
    if (execution->record.targetWriteRecorded) ++s.composed;
    const bool accepted = execution->handoff.state == VkPresentHandoffState::Accepted;
    if (accepted) ++s.submitted;
    if (accepted && execution->capturedCompletionObserved) ++s.completed;
    s.possibleTargetWrite = execution->record.targetWriteRecorded &&
        (accepted || execution->handoff.state == VkPresentHandoffState::Uncertain);
    const bool originalAccepted = originalCalled &&
        (originalResult == VK_SUCCESS || originalResult == VK_SUBOPTIMAL_KHR);
    if (accepted && originalAccepted) ++s.originalAccepted;
    s.active = accepted && originalAccepted && !execution->record.preparationOnly;
    s.failed = !s.active && (execution->handoff.state == VkPresentHandoffState::Uncertain ||
        !originalAccepted || !execution->admission.allowed || execution->record.status == VkRecordStatus::Partial);
    if (s.active)
        s.consecutiveFallbacks = 0;
    else
    {
        ++s.consecutiveFallbacks;
        if (s.possibleTargetWrite) ++s.uncertain;
        if (s.reason.empty())
            s.reason = !execution->reason.empty() ?
                execution->reason : "Vulkan Present did not submit a complete image";
    }
}

void VulkanPresentStatus::GpuCompleted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.completed < state_.submitted) ++state_.completed;
}
void VulkanPresentStatus::ObserveWorkload(const VkNrWorkloadStatus& workload)
{
    std::lock_guard<std::mutex> lock(mutex_);state_.workload=workload;
}

VkPresentStatusSnapshot VulkanPresentStatus::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

VulkanPresentStatus& GetVulkanPresentStatus()
{
    static VulkanPresentStatus status;
    return status;
}

} // namespace DlssNr
