#pragma once
#include "VulkanNrPassRecorder.h"
#include <mutex>
namespace DlssNr {
enum class VkNrFgActivity { Unknown,Off,On };
inline bool VkNrFgKnownOff(VkNrFgActivity v){return v==VkNrFgActivity::Off;}
struct VkNrContribution {
 VkNrFrameContract frame;VkNrUseId producer;uint64_t providerGeneration=0,viewport=0,contentRevision=0;
 VkImage image=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
 VkImageSubresourceRange subresource{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;VkNrChainResult chain;
};
struct VkNrFgConsumer {
 uint64_t providerGeneration=0,frameToken=0,viewport=0;VkQueue queue=VK_NULL_HANDLE;VkCommandBuffer commandBuffer=VK_NULL_HANDLE;
 VkImage input=VK_NULL_HANDLE;uint64_t capturedContentRevision=0;VkImageSubresourceRange subresource{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
 uint32_t maxGenerated=1;
};
struct VkNrConsumerBinding { VkNrConsumerId id;VkNrUseId use;VkNrContribution contribution; };
class VulkanNrPreFg {
 public:
  explicit VulkanNrPreFg(VkNrRecordingOwner& owner):owner_(owner){}
  ~VulkanNrPreFg();
  bool Publish(const VkNrContribution&);
  std::optional<VkNrConsumerId> Bind(const VkNrFgConsumer&);
  std::optional<VkNrConsumerId> ContinueAccepted(VkNrConsumerId,const VkNrFgConsumer&);
  std::optional<VkNrConsumerBinding> Binding(VkNrConsumerId) const;
  void ConsumerSubmitted(VkNrConsumerId,VkNrSubmissionId);
  void ConsumerReleased(VkNrConsumerId);
  void ConsumerAccepted(VkNrConsumerId);uint64_t AcceptedContributions() const;
  bool Generated(VkNrConsumerId,uint32_t count,uint32_t index);
  void RevokeNewContributions(uint64_t epoch);
  void RevokeProvider(uint64_t provider);
  void ProviderReleased(uint64_t provider);
  void ObserveSubmissions();void RetireCompleted();void AbandonDevice();
  size_t ConsumerCount() const;uint64_t PrimaryContributions() const;
 private:
  struct Publication {VkNrContribution value;bool active=true,held=true;};
  struct Consumer {VkNrConsumerBinding binding;VkNrFgConsumer facts;VkNrSubmissionId submission;bool released=false,accepted=false;uint64_t generated=0;};
  void Revoke(Publication&);void RetireLocked();
  std::optional<VkNrConsumerId> BindValue(const VkNrContribution&,const VkNrFgConsumer&);
  VkNrRecordingOwner& owner_;mutable std::mutex mutex_;std::vector<Publication> publications_;std::vector<Consumer> consumers_;
  uint64_t epoch_=0,next_=1,primary_=0,accepted_=0;
};
VulkanNrPreFg& VulkanNrFgContributions();
}
