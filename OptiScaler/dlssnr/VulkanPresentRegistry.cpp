#include "VulkanPresentRegistry.h"

#include <algorithm>

namespace DlssNr
{
namespace
{
template<class T> uintptr_t Key(T handle) noexcept { return (uintptr_t) handle; }
}

VkNrAcquireProof::~VkNrAcquireProof(){if(use)VulkanNrRecordings().ReleaseUse(use);}
bool VkNrAcquireProof::Complete() const
{return fenceComplete_.load(std::memory_order_acquire)||(use&&VulkanNrRecordings().GpuComplete(use));}
VulkanPresentRegistry::~VulkanPresentRegistry()=default;
void VulkanPresentRegistry::InstanceObserved(VkInstance instance,std::uint32_t api,std::vector<VkPhysicalDevice> physical)
{std::lock_guard lock(mutex_);if(instance && instances_.size()<32)instances_[instance]={api,std::move(physical)};}
void VulkanPresentRegistry::InstanceDestroyed(VkInstance instance)
{std::lock_guard lock(mutex_);instances_.erase(instance);}
std::uint32_t VulkanPresentRegistry::PhysicalInstanceApi(VkPhysicalDevice physical) const
{
    std::lock_guard lock(mutex_);std::uint32_t api=0;
    for(const auto& [instance,value]:instances_){(void)instance;
        if(std::find(value.physical.begin(),value.physical.end(),physical)!=value.physical.end()){
            if(api)return 0;api=value.api;}}
    return api;
}
bool VulkanPresentRegistry::ImportDeviceAndQueues(const VulkanPresentRegistry& source,VkDevice device)
{
    if(this==&source)return false;
    std::scoped_lock lock(mutex_,source.mutex_);
    const auto d=source.devices_.find(Key(device));if(d==source.devices_.end())return false;
    const auto old=devices_.find(Key(device));
    if(old!=devices_.end()&&old->second.generation!=d->second.generation){
        std::erase_if(swapchains_,[&](const auto& s){return s.second.device==device;});
        acquireFences_.erase(device);
    }
    // Device generations are shared facts; swapchains/acquisitions remain local.
    devices_[Key(device)]=d->second;
    devices_[Key(device)].renderSizes.Reset(); // no replay of another boundary's scalar interval
    std::erase_if(queues_,[&](const auto& q){return q.second.device==device;});
    for(const auto& [key,q]:source.queues_)if(q.device==device)queues_[key]=q;
    return true;
}
void VulkanPresentRegistry::SwapchainImages(VkDevice device,VkSwapchainKHR swap,std::vector<VkImage> images)
{
    std::lock_guard lock(mutex_);auto i=swapchains_.find(Key(swap));
    if(i==swapchains_.end()||i->second.device!=device||i->second.images==images)return;
    auto& s=i->second;s.generation=++nextSwapchainGeneration_;
    s.predecessorGenerations.clear();s.images=std::move(images);
    s.acquired.assign(s.images.size(),false);s.acquisition.assign(s.images.size(),0);
    s.acquireSemaphores.assign(s.images.size(),VK_NULL_HANDLE);s.acquireProofs.assign(s.images.size(),{});
    s.presentSerials.assign(s.images.size(),0);s.presentQueues.assign(s.images.size(),VK_NULL_HANDLE);
}
void VulkanPresentRegistry::ObserveNativeRenderSize(VkDevice device,VkExtent2D output,VkNrRenderSize size)
{std::lock_guard lock(mutex_);auto d=devices_.find(Key(device));if(d!=devices_.end())d->second.renderSizes.Observe(output,size);}
void VulkanPresentRegistry::InvalidateNativeRenderSize(VkDevice device)
{std::lock_guard lock(mutex_);auto d=devices_.find(Key(device));if(d!=devices_.end())d->second.renderSizes.Reject();}
void VulkanPresentRegistry::SwapchainDestroyed(VkSwapchainKHR swap){std::lock_guard lock(mutex_);auto i=swapchains_.find(Key(swap));if(i==swapchains_.end())return;if(auto d=devices_.find(Key(i->second.device));d!=devices_.end())d->second.renderSizes.Reset();swapchains_.erase(i);}
void VulkanPresentRegistry::DeviceDestroyed(VkDevice device){std::lock_guard lock(mutex_);std::erase_if(swapchains_,[&](const auto& s){return s.second.device==device;});std::erase_if(queues_,[&](const auto& q){return q.second.device==device;});acquireFences_.erase(device);devices_.erase(Key(device));}
void VulkanPresentRegistry::DeviceCreated(VkDevice device, VkPhysicalDevice physical,
                                           bool modelExtensionsEnabled, bool routePrepared,
                                           std::vector<uint32_t> createdFamilies,
                                           bool modelExtensionsUnsupported, bool storageWriteWithoutFormatEnabled,
                                           bool storageWriteUnsupported, bool storageWriteUnsafeChain,uint64_t actualDeviceGeneration)
{
    if (device == VK_NULL_HANDLE || physical == VK_NULL_HANDLE) return;
    std::sort(createdFamilies.begin(), createdFamilies.end());
    createdFamilies.erase(std::unique(createdFamilies.begin(), createdFamilies.end()), createdFamilies.end());
    std::lock_guard<std::mutex> lock(mutex_);
    const auto generation=actualDeviceGeneration?actualDeviceGeneration:nextDeviceGeneration_+1;
    nextDeviceGeneration_=std::max(nextDeviceGeneration_,generation);
    devices_[Key(device)] = { physical, generation, modelExtensionsEnabled, routePrepared,
                              std::move(createdFamilies), {}, modelExtensionsUnsupported,
                              storageWriteWithoutFormatEnabled, storageWriteUnsupported, storageWriteUnsafeChain };
}

bool VulkanPresentRegistry::StorageWriteWithoutFormatEnabled(VkDevice device) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto d = devices_.find(Key(device));
    return d != devices_.end() && d->second.storageWriteWithoutFormatEnabled;
}

VkPhysicalDevice VulkanPresentRegistry::PhysicalDevice(VkDevice device) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto d = devices_.find(Key(device));
    return d == devices_.end() ? VK_NULL_HANDLE : d->second.physical;
}

bool VulkanPresentRegistry::PresentPrepared(VkDevice device) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto d = devices_.find(Key(device));
    return d != devices_.end() && d->second.routePrepared && d->second.modelExtensionsEnabled;
}

void VulkanPresentRegistry::PreparedTransportObserved(VkDevice device,PreparedVulkan::Enabled enabled)
{
    std::lock_guard lock(mutex_);auto d=devices_.find(Key(device));
    if(d!=devices_.end())d->second.preparedTransport=enabled;
}
bool VulkanPresentRegistry::PreparedDevice(PreparedVulkan::DeviceInfo& out) const
{
    std::lock_guard lock(mutex_);const auto d=devices_.find(Key(out.device));
    const auto q=queues_.find(Key(out.queue));
    if(d==devices_.end() || q==queues_.end() || q->second.device!=out.device ||
       q->second.deviceGeneration!=d->second.generation || q->second.family==UINT32_MAX)return false;
    out.physical=d->second.physical;out.generation=d->second.generation;out.family=q->second.family;
    out.queueFlags=q->second.flags;const auto e=d->second.preparedTransport;
    out.externalMemoryWin32=e.externalMemoryWin32;out.externalSemaphoreWin32=e.externalSemaphoreWin32;
    out.timeline=e.timeline;return true;
}
bool VulkanPresentRegistry::FramegenQueue(VkDevice device,VkQueue& queue,std::uint32_t& family)const {
    std::lock_guard lock(mutex_);const auto d=devices_.find(Key(device));if(d==devices_.end())return false;
    queue=VK_NULL_HANDLE;family=UINT32_MAX;
    for(const auto& [key,q]:queues_)if(q.device==device&&q.deviceGeneration==d->second.generation&&
        (q.flags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
        if(q.family==UINT32_MAX||(queue&&family!=q.family)){queue=VK_NULL_HANDLE;family=UINT32_MAX;return false;}
        // Provisional initialization queue only. The FG owner binds the actual
        // observed Present queue before recording or scheduling any frame.
        if(!queue||key<Key(queue))queue=reinterpret_cast<VkQueue>(key);family=q.family;
    }
    return queue!=VK_NULL_HANDLE;
}

void VulkanPresentRegistry::QueueObserved(VkDevice device, VkQueue queue, uint32_t family, VkQueueFlags flags)
{
    if (queue == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto d = devices_.find(Key(device));
    if (d == devices_.end()) return;
    queues_[Key(queue)] = { device, d->second.generation, family, flags };
}

std::vector<uint32_t> VulkanPresentRegistry::QueueFamilies(VkDevice device) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> out;
    const auto d = devices_.find(Key(device));
    if (d == devices_.end()) return out;
    for (const auto& [_, q] : queues_)
        if (q.device == device && q.deviceGeneration == d->second.generation &&
            std::find(out.begin(), out.end(), q.family) == out.end())
            out.push_back(q.family);
    return out;
}
std::vector<uint32_t> VulkanPresentRegistry::CreatedQueueFamilies(VkDevice device) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found=devices_.find(Key(device));
    return found==devices_.end()?std::vector<uint32_t>{}:found->second.createdFamilies;
}

void VulkanPresentRegistry::SwapchainCreated(VkDevice device, VkSwapchainKHR swapchain,
    const VkSwapchainCreateInfoKHR& create, VkImageUsageFlags supportedUsage, bool knownUsageChain,
    std::vector<VkImage> images, std::vector<uint32_t> presentFamilies)
{
    if (swapchain == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto d = devices_.find(Key(device));
    if (d == devices_.end()) return;
    Swapchain s {};
    d->second.renderSizes.Reset();
    s.device = device;
    s.deviceGeneration = d->second.generation;
    s.generation = ++nextSwapchainGeneration_;
    // Only the game's explicit oldSwapchain relation proves succession. Matching
    // raw surface handles alone could join two different surface incarnations.
    if (const auto old = swapchains_.find(Key(create.oldSwapchain));
        old != swapchains_.end() && old->second.device == device &&
        old->second.deviceGeneration == d->second.generation)
    {
        const auto& ancestors = old->second.predecessorGenerations;
        const auto first = ancestors.size() > 127 ? ancestors.size() - 127 : 0;
        s.predecessorGenerations.assign(ancestors.begin() + first, ancestors.end());
        s.predecessorGenerations.push_back(old->second.generation);
    }
    s.format = create.imageFormat;
    s.colorSpace = create.imageColorSpace;
    s.extent = create.imageExtent;
    s.arrayLayers = create.imageArrayLayers;
    s.usage = create.imageUsage;
    s.flags = create.flags;
    s.sharingMode = create.imageSharingMode;
    if (create.imageSharingMode == VK_SHARING_MODE_CONCURRENT &&
        create.pQueueFamilyIndices != nullptr && create.queueFamilyIndexCount != 0)
        s.sharingFamilies.assign(create.pQueueFamilyIndices,
                                 create.pQueueFamilyIndices + create.queueFamilyIndexCount);
    s.supportedUsage = supportedUsage;
    s.knownUsageChain = knownUsageChain;
    s.acquired.resize(images.size(), false);
    s.acquisition.resize(images.size(), 0);
    s.acquireSemaphores.resize(images.size());s.acquireProofs.resize(images.size());
    s.presentSerials.resize(images.size());s.presentQueues.resize(images.size());
    s.images = std::move(images);
    s.presentFamilies = std::move(presentFamilies);
    swapchains_[Key(swapchain)] = std::move(s);
}

void VulkanPresentRegistry::ImageAcquired(VkDevice device, VkSwapchainKHR swapchain, uint32_t imageIndex,VkSemaphore semaphore,VkFence fence)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto s = swapchains_.find(Key(swapchain));
    if (s == swapchains_.end() || s->second.device != device ||
        imageIndex >= s->second.acquired.size()) return;
    s->second.acquired[imageIndex] = true;
    s->second.acquisition[imageIndex] = ++nextAcquire_;
    auto proof=std::make_shared<VkNrAcquireProof>();
    proof->device=device;proof->deviceGeneration=s->second.deviceGeneration;
    proof->swapchain=swapchain;proof->swapchainGeneration=s->second.generation;
    proof->imageIndex=imageIndex;proof->acquireGeneration=nextAcquire_;
    proof->priorPresentSerial=s->second.presentSerials[imageIndex];
    proof->priorPresentQueue=s->second.presentQueues[imageIndex];
    proof->predecessorGenerations=s->second.predecessorGenerations;
    // Reserve before a submit can observe the wait. No shader/recording state
    // determines whether a semaphore-only submission can own this dependency.
    if(const auto use=VulkanNrRecordings().ReserveSubmissionDependency(device,s->second.deviceGeneration,UINT32_MAX))
        proof->use=*use;
    s->second.acquireProofs[imageIndex]=proof;s->second.acquireSemaphores[imageIndex]=semaphore;
    if(fence){auto& fences=acquireFences_[device];std::erase_if(fences,[](const auto& f){return f.second.expired();});fences[fence]=proof;}
}
std::vector<VkNrUseId> VulkanPresentRegistry::PrepareAcquireWait(VkQueue queue,std::span<const VkSemaphore> waits,uint64_t generation){
    std::vector<VkNrUseId> uses;
    std::lock_guard lock(mutex_);auto q=queues_.find(Key(queue));if(q==queues_.end()||!generation)return uses;
    const auto d=devices_.find(Key(q->second.device));if(d==devices_.end()||d->second.generation!=q->second.deviceGeneration)return uses;
    for(auto& [key,s]:swapchains_){(void)key;if(s.device!=q->second.device||s.deviceGeneration!=q->second.deviceGeneration)continue;
        for(size_t i=0;i<s.images.size();++i)if(s.acquired[i]&&s.acquireProofs[i]&&s.acquireSemaphores[i]&&
            std::find(waits.begin(),waits.end(),s.acquireSemaphores[i])!=waits.end()){
            const auto& proof=*s.acquireProofs[i];
            if(proof.use)uses.push_back(proof.use);
        }
    }
    return uses;
}
void VulkanPresentRegistry::AcquireWaitSubmitted(VkQueue queue,std::span<const VkNrUseId> uses,VkResult result)
{
    // Vulkan guarantees these allocation failures leave submission state
    // unaffected. Keep the exact dependency available for the application's retry.
    if(result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY)return;
    std::lock_guard lock(mutex_);const auto q=queues_.find(Key(queue));if(q==queues_.end())return;
    for(auto& [key,s]:swapchains_){(void)key;if(s.device!=q->second.device||s.deviceGeneration!=q->second.deviceGeneration)continue;
        for(size_t i=0;i<s.acquireProofs.size();++i)if(s.acquireProofs[i]&&
            std::find(uses.begin(),uses.end(),s.acquireProofs[i]->use)!=uses.end())s.acquireSemaphores[i]=VK_NULL_HANDLE;
    }
}
std::shared_ptr<const VkNrAcquireProof> VulkanPresentRegistry::AcquireProof(VkSwapchainKHR swap,uint32_t index,uint64_t acquisition) const
{
    std::lock_guard lock(mutex_);const auto s=swapchains_.find(Key(swap));
    if(!acquisition||s==swapchains_.end()||index>=s->second.acquireProofs.size()||
        !s->second.acquired[index]||s->second.acquisition[index]!=acquisition)return {};
    return s->second.acquireProofs[index];
}
std::shared_ptr<const VkNrAcquireProof> VulkanPresentRegistry::LatestAcquireProof(VkSwapchainKHR swap,uint32_t index) const
{std::lock_guard lock(mutex_);auto i=swapchains_.find(Key(swap));return i!=swapchains_.end()&&index<i->second.acquireProofs.size()?i->second.acquireProofs[index]:nullptr;}
std::shared_ptr<const VkNrAcquireProof> VulkanPresentRegistry::AcquireFenceProof(VkDevice device,VkFence fence) const
{
    std::lock_guard lock(mutex_);const auto d=acquireFences_.find(device);if(d==acquireFences_.end())return {};
    const auto f=d->second.find(fence);return f==d->second.end()?nullptr:f->second.lock();
}
void VulkanPresentRegistry::ObserveAcquireFence(const std::shared_ptr<const VkNrAcquireProof>& proof,VkResult result)
{if(proof&&result==VK_SUCCESS)proof->fenceComplete_.store(true,std::memory_order_release);}
void VulkanPresentRegistry::InvalidateAcquireFence(VkDevice device,VkFence fence)
{std::lock_guard lock(mutex_);auto d=acquireFences_.find(device);if(d!=acquireFences_.end())d->second.erase(fence);}
VkNrUseId VulkanPresentRegistry::AcquireUse(VkSwapchainKHR swap,uint32_t index) const{std::lock_guard lock(mutex_);auto s=swapchains_.find(Key(swap));return s!=swapchains_.end()&&index<s->second.acquireProofs.size()&&s->second.acquireProofs[index]?s->second.acquireProofs[index]->use:VkNrUseId{};}
bool VulkanPresentRegistry::AcquireConsumed(VkSwapchainKHR swap,uint32_t index) const
{std::shared_ptr<const VkNrAcquireProof> proof;{std::lock_guard lock(mutex_);auto s=swapchains_.find(Key(swap));if(s!=swapchains_.end()&&index<s->second.acquireProofs.size())proof=s->second.acquireProofs[index];}return proof&&proof->Complete();}
bool VulkanPresentRegistry::AcquireConsumed(VkSwapchainKHR swap,uint32_t index,uint64_t expectedAcquireGeneration) const
{
    if (!expectedAcquireGeneration) return false;
    const auto proof=AcquireProof(swap,index,expectedAcquireGeneration);
    // Do not hold the registry mutex while asking the recording owner. Recheck
    // identity afterward so a concurrent replacement cannot qualify this proof.
    if (!proof || !proof->Complete()) return false;
    std::lock_guard lock(mutex_);
    const auto s = swapchains_.find(Key(swap));
    return s != swapchains_.end() && index < s->second.acquireProofs.size() &&
        s->second.acquired[index] && s->second.acquisition[index] == expectedAcquireGeneration &&
        s->second.acquireProofs[index] == proof;
}
std::optional<VkNrFrameContract> VulkanPresentRegistry::NativeFrameContext(VkDevice device,VkQueue queue,VkExtent2D output) const
{
    std::lock_guard lock(mutex_);const auto q=queues_.find(Key(queue));const auto d=devices_.find(Key(device));
    if(q==queues_.end()||d==devices_.end()||q->second.device!=device||q->second.deviceGeneration!=d->second.generation)return {};
    std::optional<VkNrFrameContract> result;
    for(const auto& [identity,s]:swapchains_) {
        if(s.device!=device||s.deviceGeneration!=d->second.generation||s.extent.width!=output.width||s.extent.height!=output.height)continue;
        for(uint32_t i=0;i<s.acquired.size();++i)if(s.acquired[i]) {
            if(result)return {};
            VkNrFrameContract f;f.deviceGeneration=d->second.generation;f.swapchainGeneration=s.generation;
            f.swapchain=reinterpret_cast<VkSwapchainKHR>(identity);f.swapchainImageIndex=i;f.acquireGeneration=s.acquisition[i];
            f.queue=queue;f.output=output;f.representation.format=s.format;f.representation.colorSpace=s.colorSpace;result=f;
        }
    }
    return result;
}

std::optional<VkNrFrameContract> VulkanPresentRegistry::NativeResolutionContext(VkDevice device,VkQueue queue,VkExtent2D output) const
{
    std::lock_guard lock(mutex_);
    const auto q=queues_.find(Key(queue));const auto d=devices_.find(Key(device));
    if(q==queues_.end()||d==devices_.end()||q->second.device!=device||q->second.deviceGeneration!=d->second.generation)return {};
    std::optional<VkNrFrameContract> result;
    for(const auto& [identity,s]:swapchains_) {
        if(s.device!=device||s.deviceGeneration!=d->second.generation||s.extent.width!=output.width||s.extent.height!=output.height)continue;
        if(result)return {}; // Multiple outputs cannot identify the selected SR destination.
        VkNrFrameContract f;f.deviceGeneration=d->second.generation;f.swapchainGeneration=s.generation;
        f.swapchain=reinterpret_cast<VkSwapchainKHR>(identity);f.queue=queue;f.output=output;
        f.representation.format=s.format;f.representation.colorSpace=s.colorSpace;result=f;
    }
    return result;
}

std::optional<VkNrFrameContract> VulkanPresentRegistry::TaggedImageContext(VkImage image,VkFormat format,VkExtent2D extent) const
{
    if(!image)return {};
    std::lock_guard lock(mutex_);std::optional<VkNrFrameContract> found;
    for(const auto& [key,s]:swapchains_) {
        const auto device=devices_.find(Key(s.device));
        if(device==devices_.end()||device->second.generation!=s.deviceGeneration||(format!=VK_FORMAT_UNDEFINED&&s.format!=format)||
            (extent.width&&s.extent.width!=extent.width)||(extent.height&&s.extent.height!=extent.height)||s.arrayLayers!=1)continue;
        for(uint32_t index=0;index<s.images.size();++index) {
            if(s.images[index]!=image||index>=s.acquired.size()||!s.acquired[index]||
                index>=s.acquisition.size()||!s.acquisition[index])continue;
            if(found)return {}; // Even identical non-dispatchable handles need a unique live owner.
            VkNrFrameContract f;f.deviceGeneration=s.deviceGeneration;f.swapchainGeneration=s.generation;
            f.swapchain=reinterpret_cast<VkSwapchainKHR>(key);f.swapchainImageIndex=index;f.acquireGeneration=s.acquisition[index];
            f.output=s.extent;f.representation.format=s.format;f.representation.colorSpace=s.colorSpace;found=f;
        }
    }
    return found;
}

bool VulkanPresentRegistry::OnlySwapchain(VkDevice device,VkSwapchainKHR swapchain) const
{
    std::lock_guard lock(mutex_);bool found=false;
    for(const auto& [key,s]:swapchains_)if(s.device==device){
        if(key!=Key(swapchain))return false;found=true;
    }
    return found;
}
bool VulkanPresentRegistry::IsSwapchainImage(VkDevice device,VkImage image) const
{
    std::lock_guard lock(mutex_);
    for(const auto& [key,s]:swapchains_){(void)key;
        if(s.device==device&&std::find(s.images.begin(),s.images.end(),image)!=s.images.end())return true;
    }
    return false;
}

std::optional<VkObservedPresent> VulkanPresentRegistry::SnapshotForPresent(VkQueue queue, const VkPresentInfoKHR& info,
    bool enabled, uint32_t route, PresentInput::Policy policy, bool fgKnownActive)
{
    if (info.swapchainCount == 0 || info.pSwapchains == nullptr || info.pImageIndices == nullptr)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto s = swapchains_.find(Key(info.pSwapchains[0]));
    if (s == swapchains_.end()) return std::nullopt;
    const auto d = devices_.find(Key(s->second.device));
    if (d == devices_.end()) return std::nullopt;
    const auto q = queues_.find(Key(queue));
    const uint32_t index = info.pImageIndices[0];
    VkObservedPresent out {};
    out.renderSize=d->second.renderSizes.Take(s->second.extent);
    out.device = s->second.device;
    out.physicalDevice = d->second.physical;
    out.queue = queue;
    out.swapchain = info.pSwapchains[0];
    out.predecessorGenerations = s->second.predecessorGenerations;
    if(index<s->second.presentSerials.size())out.priorPresentSerial=s->second.presentSerials[index];
    if (index < s->second.images.size()) out.image = s->second.images[index];
    out.request.enabled = enabled;
    out.request.route = route;
    out.request.policy = policy;
    out.request.fgKnownActive = fgKnownActive;
    out.request.format = s->second.format;
    out.request.colorSpace = s->second.colorSpace;
    out.request.extent = s->second.extent;
    out.request.arrayLayers = s->second.arrayLayers;
    out.request.imageUsage = s->second.usage;
    out.request.flags = s->second.flags;
    out.request.swapchainCount = info.swapchainCount;
    out.request.imageIndex = index;
    out.request.acquiredObserved = index < s->second.acquired.size() && s->second.acquired[index];
    out.request.acquireGeneration = index < s->second.acquisition.size() ? s->second.acquisition[index] : 0;
    out.request.deviceGeneration = s->second.deviceGeneration;
    out.request.swapchainGeneration = s->second.generation;
    auto& c = out.capabilities;
    c.deviceObserved = true;
    c.modelExtensionsEnabled = d->second.modelExtensionsEnabled;
    c.modelExtensionsUnsupported = d->second.modelExtensionsUnsupported;
    c.storageWriteUnsupported = d->second.storageWriteUnsupported;
    c.storageWriteUnsafeChain = d->second.storageWriteUnsafeChain;
    c.routePreparedAtDeviceCreation = d->second.routePrepared;
    c.surfaceUsageKnown = s->second.supportedUsage != 0;
    c.surfaceUsage = s->second.supportedUsage;
    c.swapchainPNextKnown = s->second.knownUsageChain;
    c.swapchainObserved = true;
    c.imagesObserved = !s->second.images.empty();
    c.imageCount = static_cast<uint32_t>(s->second.images.size());
    c.deviceGeneration = d->second.generation;
    c.swapchainGeneration = s->second.generation;
    if (q != queues_.end() && q->second.device == s->second.device &&
        q->second.deviceGeneration == d->second.generation)
    {
        c.queueObserved = true;
        out.queueFamily = q->second.family;
        c.queueFlags = q->second.flags;
        c.queueFamilyMatches = std::find(s->second.presentFamilies.begin(),
            s->second.presentFamilies.end(), q->second.family) != s->second.presentFamilies.end();
        // At a valid vkQueuePresentKHR call the application must already have
        // arranged ownership on this queue family; Present does not transfer it.
        // Our work uses this exact queue and consumes the original waits at
        // ALL_COMMANDS before accessing the image. Other created queue families
        // do not invalidate that contract. This is not barrier/GPU observation.
        c.presentQueueAccessQualified = s->second.sharingMode == VK_SHARING_MODE_CONCURRENT
            ? std::find(s->second.sharingFamilies.begin(), s->second.sharingFamilies.end(),
                        q->second.family) != s->second.sharingFamilies.end()
            : s->second.sharingMode == VK_SHARING_MODE_EXCLUSIVE &&
              c.queueFamilyMatches && out.request.acquiredObserved &&
              std::find(d->second.createdFamilies.begin(), d->second.createdFamilies.end(),
                        q->second.family) != d->second.createdFamilies.end();
    }
    return out;
}

void VulkanPresentRegistry::PresentSucceeded(VkSwapchainKHR swapchain, uint32_t imageIndex,VkQueue queue)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto s = swapchains_.find(Key(swapchain));
    if (s == swapchains_.end() || imageIndex >= s->second.acquired.size()) return;
    s->second.acquired[imageIndex] = false;
    s->second.presentSerials[imageIndex]=++nextPresentSerial_;
    s->second.presentQueues[imageIndex]=queue;
}
uint64_t VulkanPresentRegistry::LastPresentSerial(VkSwapchainKHR swapchain,uint32_t imageIndex) const
{std::lock_guard lock(mutex_);const auto s=swapchains_.find(Key(swapchain));return s==swapchains_.end()||imageIndex>=s->second.presentSerials.size()?0:s->second.presentSerials[imageIndex];}

VulkanPresentRegistry& GetVulkanPresentRegistry()
{
    static auto* registry = new VulkanPresentRegistry;
    return *registry;
}

VulkanPresentRegistry& GetVulkanApplicationPresentRegistry()
{static auto* registry=new VulkanPresentRegistry;return *registry;}

VulkanPresentRegistry& GetVulkanLoaderPresentRegistry()
{
    // Kept separate from application/provider-visible image arrays. Only the
    // raw loader boundary contributes lifecycle observations to this registry.
    static auto* registry = new VulkanPresentRegistry;
    return *registry;
}

} // namespace DlssNr
