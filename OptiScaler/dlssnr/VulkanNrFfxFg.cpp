#include "VulkanNrFfxFg.h"
namespace DlssNr {
VulkanNrFfxFg::~VulkanNrFfxFg(){for(auto& e:entries_)recordings_.ReleaseUse(e.prepare.use);}
bool VulkanNrFfxFg::ObservePrepare(const VkNrFfxObservation& o){std::lock_guard lock(mutex_);
 if(!o.succeeded||!o.releaseContractObserved||o.apiVersion!=0x010104||!o.callbackContext||!o.use||!o.consumer.providerGeneration||!o.consumer.commandBuffer||
 !recordings_.UseOnRecording(o.use,o.consumer.commandBuffer)||entries_.size()>=64)return false;
 for(const auto& e:entries_)if(e.prepare.callbackContext==o.callbackContext&&e.prepare.consumer.frameToken==o.consumer.frameToken)return false;
 if(!recordings_.RetainUse(o.use))return false;entries_.push_back({o});return true;
}
std::optional<VkNrConsumerId> VulkanNrFfxFg::ObserveDispatch(const VkNrFfxObservation& o){std::lock_guard lock(mutex_);if(!o.succeeded||o.apiVersion!=0x010104)return {};
 for(auto& e:entries_){const auto& p=e.prepare;
  if(p.callbackContext!=o.callbackContext||p.consumer.frameToken!=o.consumer.frameToken||p.consumer.providerGeneration!=o.consumer.providerGeneration||
    p.consumer.viewport!=o.consumer.viewport||p.use!=o.use||p.consumer.commandBuffer!=o.consumer.commandBuffer||p.consumer.queue!=o.consumer.queue||
    p.consumer.input!=o.consumer.input||p.consumer.capturedContentRevision!=o.consumer.capturedContentRevision||e.consumer||e.released)continue;
  e.consumer=fg_.Bind(o.consumer);return e.consumer;
 }return {};
}
bool VulkanNrFfxFg::ObserveCompose(const VkNrFfxObservation& o){std::lock_guard lock(mutex_);if(!o.succeeded||!o.releaseObserved||o.apiVersion!=0x010104)return false;
 for(auto& e:entries_)if(e.prepare.callbackContext==o.callbackContext&&e.prepare.consumer.frameToken==o.consumer.frameToken&&
   e.prepare.consumer.providerGeneration==o.consumer.providerGeneration&&e.prepare.use==o.use&&e.consumer){fg_.ConsumerReleased(*e.consumer);e.released=true;return true;}return false;
}
void VulkanNrFfxFg::RetireCompleted(){std::lock_guard lock(mutex_);fg_.ObserveSubmissions();for(auto i=entries_.begin();i!=entries_.end();){
 if(i->released&&recordings_.Reusable(i->prepare.use)){recordings_.ReleaseUse(i->prepare.use);i=entries_.erase(i);}else ++i;}}
void VulkanNrFfxFg::ReleaseContext(uint64_t context){std::lock_guard lock(mutex_);for(auto& e:entries_)if(e.prepare.callbackContext==context){if(e.consumer)fg_.ConsumerReleased(*e.consumer);e.released=true;}}
namespace {std::mutex runtimeMutex;VkNrFfxRuntimeStatus runtime;}
void ObserveVkNrFfxVersion(uint64_t context,uint64_t id,const char* name,bool ok){std::lock_guard lock(runtimeMutex);runtime.context=context;
 runtime.providerId=ok?id:0;runtime.providerName=ok&&name?std::string(name).substr(0,128):"";
 // A version-name string cannot prove the renderer ABI or an asynchronous consumer's release semaphore.
 runtime.reason=ok&&id?"Vulkan FFX version observed; final-color frame mapping and callback release remain unqualified":"Vulkan FFX provider version query did not succeed";
}
void ObserveVkNrFfxDispatch(uint64_t context,uint64_t,uint64_t,VkCommandBuffer cb,bool ok){std::lock_guard lock(runtimeMutex);runtime.context=context;
 if(!ok)runtime.reason="Vulkan FFX original dispatch failed";else if(!cb)runtime.reason="Vulkan FFX callback has no observed recording command buffer";
}
VkNrFfxRuntimeStatus VulkanNrFfxStatus(){std::lock_guard lock(runtimeMutex);return runtime;}
}
