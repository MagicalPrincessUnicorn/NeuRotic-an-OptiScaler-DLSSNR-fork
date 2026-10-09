#pragma once
#include "VulkanNrPreFg.h"
#include "VulkanNrProviderCaps.h"
namespace DlssNr {
struct VkNrFfxObservation {
 VkNrFgConsumer consumer;VkNrUseId use;uint64_t callbackContext=0;uint32_t apiVersion=0;
 bool succeeded=false,releaseObserved=false,releaseContractObserved=false;
};
class VulkanNrFfxFg {
 public:
 VulkanNrFfxFg(VkNrRecordingOwner& r,VulkanNrPreFg& f):recordings_(r),fg_(f){}
 ~VulkanNrFfxFg();bool ObservePrepare(const VkNrFfxObservation&);std::optional<VkNrConsumerId> ObserveDispatch(const VkNrFfxObservation&);
 bool ObserveCompose(const VkNrFfxObservation&);void RetireCompleted();void ReleaseContext(uint64_t);
 private:struct Entry {VkNrFfxObservation prepare;std::optional<VkNrConsumerId> consumer;bool released=false;};
 VkNrRecordingOwner& recordings_;VulkanNrPreFg& fg_;std::mutex mutex_;std::vector<Entry> entries_;
};
struct VkNrFfxRuntimeStatus {uint64_t providerId=0,context=0;std::string providerName,reason="Vulkan FFX provider ABI and consumer release have not been observed";};
void ObserveVkNrFfxVersion(uint64_t context,uint64_t providerId,const char* providerName,bool succeeded);
void ObserveVkNrFfxDispatch(uint64_t context,uint64_t descriptorType,uint64_t frame,VkCommandBuffer,bool succeeded);
VkNrFfxRuntimeStatus VulkanNrFfxStatus();
}
