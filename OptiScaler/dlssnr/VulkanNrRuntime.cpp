#include "VulkanNrRuntime.h"

namespace DlssNr
{

bool VulkanNrRuntime::EnsureDevice(VkDevice device, const std::function<bool()>& initialize)
{
    if (device == VK_NULL_HANDLE) return false;
    if (initialized_ && device_ == device) return true;
    if (device_ != VK_NULL_HANDLE && device_ != device) return false;
    if (!initialize()) return false;
    device_ = device;
    initialized_ = true;
    ++epoch_;
    return true;
}

void VulkanNrRuntime::Activate(VkNrRoute route)
{
    if (!hasActive_ || active_ != route)
    {
        active_ = route;
        hasActive_ = true;
        ++epoch_;
    }
}

} // namespace DlssNr
