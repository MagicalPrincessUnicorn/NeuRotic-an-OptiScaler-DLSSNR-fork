#include "VulkanNrPreFg.h"
namespace DlssNr {
namespace {
bool SameRange(const VkImageSubresourceRange& a,const VkImageSubresourceRange& b){return a.aspectMask==b.aspectMask&&a.baseMipLevel==b.baseMipLevel&&a.levelCount==b.levelCount&&a.baseArrayLayer==b.baseArrayLayer&&a.layerCount==b.layerCount;}
}
VulkanNrPreFg::~VulkanNrPreFg(){AbandonDevice();}
void VulkanNrPreFg::Revoke(Publication& p){p.active=false;if(p.held){owner_.RemoveConsumerHold(p.value.producer);p.held=false;}}
bool VulkanNrPreFg::Publish(const VkNrContribution& c){
 std::lock_guard lock(mutex_);RetireLocked();
 if(!c.producer||!c.providerGeneration||!c.contentRevision||!c.image||!c.view||!c.frame.deviceGeneration||
   !c.frame.temporal||(!c.frame.temporal->frameToken&&!c.frame.temporal->providerFrameKnown)||c.frame.placement==VkNrPlacement::BeforeSR||!c.chain.requested||c.chain.completed!=c.chain.requested||!c.chain.deliverable||!c.chain.outputVersion||
   !c.chain.recording.outputWriteRecorded||c.subresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||c.subresource.baseMipLevel||c.subresource.baseArrayLayer||
   c.subresource.levelCount!=1||c.subresource.layerCount!=1||c.layout!=VK_IMAGE_LAYOUT_GENERAL||publications_.size()>=128||
   (epoch_&&epoch_!=c.frame.routeEpoch)||owner_.RecordingReleased(c.producer))return false;
 // Before submission, only the authenticated selected evaluation on this exact
 // recording can publish without a queue. Submission later supplies the queue.
 if(!c.frame.queue&&(!c.frame.evaluation||!c.frame.temporal->providerFrameKnown||
    c.frame.temporal->providerGeneration!=c.providerGeneration||c.frame.temporal->viewport!=c.viewport||
    c.frame.evaluation.featureGeneration!=c.frame.temporal->featureGeneration||
    c.frame.evaluation.incarnation!=owner_.Incarnation(c.frame.evaluation.commandBuffer)||
    !owner_.UseOnRecording(c.producer,c.frame.evaluation.commandBuffer)||
    owner_.ModelRecordingReason(c.frame.evaluation.commandBuffer)||c.frame.queueFamily!=owner_.UseFamily(c.producer)))return false;
 for(const auto& p:publications_)if(p.value.producer==c.producer&&p.value.providerGeneration==c.providerGeneration&&p.value.viewport==c.viewport)return false;
 if(!owner_.RetainUse(c.producer))return false;
 if(!owner_.AddConsumerHold(c.producer)){owner_.ReleaseUse(c.producer);return false;}
 for(auto& p:publications_)if(p.value.providerGeneration==c.providerGeneration&&p.value.viewport==c.viewport)Revoke(p);
 publications_.push_back({c});++primary_;return true;
}
std::optional<VkNrConsumerId> VulkanNrPreFg::Bind(const VkNrFgConsumer& c){
 std::lock_guard lock(mutex_);RetireLocked();
 if(!c.commandBuffer||!c.input||!c.maxGenerated||c.maxGenerated>63||consumers_.size()>=128)return {};
 for(auto& p:publications_){const auto& v=p.value;
  if(!p.active||v.providerGeneration!=c.providerGeneration||v.viewport!=c.viewport||v.frame.temporal->frameToken!=c.frameToken||
    v.image!=c.input||v.contentRevision!=c.capturedContentRevision||!SameRange(v.subresource,c.subresource)||v.layout!=c.layout||v.frame.queue!=c.queue)continue;
  return BindValue(v,c);
 }return {};
}
std::optional<VkNrConsumerId> VulkanNrPreFg::ContinueAccepted(VkNrConsumerId id,const VkNrFgConsumer& c){
 std::lock_guard lock(mutex_);RetireLocked();
 for(const auto& accepted:consumers_)if(accepted.binding.id==id&&accepted.accepted&&!accepted.released){
  const auto value=accepted.binding.contribution;
  if(epoch_&&epoch_!=value.frame.routeEpoch)return {};
  return BindValue(value,c);
 }return {};
}
std::optional<VkNrConsumerId> VulkanNrPreFg::BindValue(const VkNrContribution& v,const VkNrFgConsumer& c){
 if(!c.commandBuffer||!c.input||!c.maxGenerated||c.maxGenerated>63||consumers_.size()>=128||
 v.providerGeneration!=c.providerGeneration||v.viewport!=c.viewport||v.frame.temporal->frameToken!=c.frameToken||
 v.image!=c.input||v.contentRevision!=c.capturedContentRevision||!SameRange(v.subresource,c.subresource)||v.layout!=c.layout||v.frame.queue!=c.queue)return {};
  const auto proof=owner_.ProducerProof(v.producer);
  const bool sameRecording=owner_.UseOnRecording(v.producer,c.commandBuffer);
  if(!sameRecording&&(!c.queue||!proof||proof->queue!=c.queue||proof->deviceGeneration!=v.frame.deviceGeneration))return {};
  const auto use=owner_.Reserve(c.commandBuffer,v.frame.deviceGeneration);
  if(!use||owner_.UseDevice(*use)!=owner_.UseDevice(v.producer)||owner_.UseFamily(*use)!=owner_.UseFamily(v.producer)){
   if(use)owner_.CancelBeforeRecording(*use);return {};
  }
  if(!owner_.RetainUse(*use)){owner_.CancelBeforeRecording(*use);return {};}
  if(!owner_.AddConsumerHold(v.producer)){owner_.ReleaseUse(*use);owner_.CancelBeforeRecording(*use);return {};}
  VkNrConsumerId id{next_++};consumers_.push_back({{id,*use,v},c});return id;
}
std::optional<VkNrConsumerBinding> VulkanNrPreFg::Binding(VkNrConsumerId id) const{
 std::lock_guard lock(mutex_);for(const auto& c:consumers_)if(c.binding.id==id)return c.binding;return {};
}
void VulkanNrPreFg::ConsumerSubmitted(VkNrConsumerId id,VkNrSubmissionId submission){
 std::lock_guard lock(mutex_);for(auto& c:consumers_)if(c.binding.id==id){auto proof=owner_.ProducerProof(c.binding.use);
  if(proof&&proof->submission==submission&&(!c.facts.queue||proof->queue==c.facts.queue)&&proof->deviceGeneration==c.binding.contribution.frame.deviceGeneration){c.facts.queue=proof->queue;c.submission=submission;}}
}
void VulkanNrPreFg::ObserveSubmissions(){std::lock_guard lock(mutex_);for(auto& c:consumers_){auto proof=owner_.ProducerProof(c.binding.use);
 if(proof&&(!c.facts.queue||proof->queue==c.facts.queue)&&proof->deviceGeneration==c.binding.contribution.frame.deviceGeneration){c.facts.queue=proof->queue;c.submission=proof->submission;}}RetireLocked();}
void VulkanNrPreFg::ConsumerReleased(VkNrConsumerId id){std::lock_guard lock(mutex_);for(auto& c:consumers_)if(c.binding.id==id){c.released=true;for(auto& p:publications_)if(p.value.producer==c.binding.contribution.producer)Revoke(p);}}
void VulkanNrPreFg::ConsumerAccepted(VkNrConsumerId id){std::lock_guard lock(mutex_);for(auto& c:consumers_)if(c.binding.id==id&&!c.accepted){c.accepted=true;++accepted_;}}
uint64_t VulkanNrPreFg::AcceptedContributions() const{std::lock_guard lock(mutex_);return accepted_;}
bool VulkanNrPreFg::Generated(VkNrConsumerId id,uint32_t count,uint32_t index){std::lock_guard lock(mutex_);for(auto& c:consumers_)if(c.binding.id==id){
 if(!count||count>c.facts.maxGenerated||index>=count)return false;c.generated|=uint64_t(1)<<index;return true;}return false;}
void VulkanNrPreFg::RevokeNewContributions(uint64_t epoch){std::lock_guard lock(mutex_);epoch_=epoch;for(auto& p:publications_)Revoke(p);RetireLocked();}
void VulkanNrPreFg::RevokeProvider(uint64_t provider){std::lock_guard lock(mutex_);
 for(auto& p:publications_)if(p.value.providerGeneration==provider)Revoke(p);
 // Existing asynchronous consumers keep their independent release obligation.
 RetireLocked();}
void VulkanNrPreFg::ProviderReleased(uint64_t provider){std::lock_guard lock(mutex_);for(auto& p:publications_)if(p.value.providerGeneration==provider)Revoke(p);
 for(auto& c:consumers_)if(c.facts.providerGeneration==provider)c.released=true;RetireLocked();}
void VulkanNrPreFg::RetireLocked(){
 for(auto it=consumers_.begin();it!=consumers_.end();){
  const bool submitted=bool(it->submission);
  if(it->released&&owner_.Reusable(it->binding.use)&&(!submitted||owner_.GpuComplete(it->binding.use))){
   owner_.RemoveConsumerHold(it->binding.contribution.producer);owner_.ReleaseUse(it->binding.use);it=consumers_.erase(it);
  }else ++it;
 }
 for(auto it=publications_.begin();it!=publications_.end();){if(!it->active&&owner_.Reusable(it->value.producer)){owner_.ReleaseUse(it->value.producer);it=publications_.erase(it);}else ++it;}
}
void VulkanNrPreFg::RetireCompleted(){std::lock_guard lock(mutex_);RetireLocked();}
void VulkanNrPreFg::AbandonDevice(){std::lock_guard lock(mutex_);for(auto& c:consumers_){owner_.RemoveConsumerHold(c.binding.contribution.producer);owner_.ReleaseUse(c.binding.use);}
 for(auto& p:publications_){Revoke(p);owner_.ReleaseUse(p.value.producer);}consumers_.clear();publications_.clear();epoch_=0;}
size_t VulkanNrPreFg::ConsumerCount() const{std::lock_guard lock(mutex_);return consumers_.size();}
uint64_t VulkanNrPreFg::PrimaryContributions() const{std::lock_guard lock(mutex_);return primary_;}
VulkanNrPreFg& VulkanNrFgContributions(){static VulkanNrPreFg owner(VulkanNrRecordings());return owner;}
}
