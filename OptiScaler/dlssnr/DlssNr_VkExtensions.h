#pragma once

// This is the existing backend compatibility list, derived from model binary names.
// It is not a feature requirements query: in particular the BDA name alone does not
// establish that bufferDeviceAddress must be enabled. Keep feature facts separate.
// Device extensions are immutable after creation; preparation uses this list without
// treating it as proof that every private provider requirement has been discovered.

#include <vulkan/vulkan.h>

#include <string>
#include <vector>
#include <cstring>

namespace DlssNr::VkExt
{

// Instance-level compatibility entry; core promotion and effective instance API
// version still need to be considered before using this with a Vulkan 1.0 host.
inline const char* const kInstance[] = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

// Existing native backend device extension group; not a substitute for a
// version-qualified Feature 18 requirements query or feature manifest.
inline const char* const kDevice[] = {
    "VK_NVX_binary_import",
    "VK_NVX_image_view_handle",
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
};

// Holds the merged list for as long as the create call needs it. VkDeviceCreateInfo keeps a bare
// pointer, so the storage has to outlive the call rather than the statement.
struct Merged
{
    std::vector<const char*> names;
    std::vector<std::string> owned;
};

// Everything the physical device is willing to offer, by name.
inline std::vector<std::string> SupportedDeviceExtensions(PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                                                          VkInstance instance, VkPhysicalDevice physicalDevice,
                                                          bool* querySucceeded = nullptr)
{
    std::vector<std::string> out;
    if (querySucceeded != nullptr) *querySucceeded = false;

    if (getInstanceProcAddr == nullptr || physicalDevice == VK_NULL_HANDLE)
        return out;

    auto enumerate = (PFN_vkEnumerateDeviceExtensionProperties) getInstanceProcAddr(
        instance, "vkEnumerateDeviceExtensionProperties");

    if (enumerate == nullptr)
        return out;

    uint32_t count = 0;

    if (enumerate(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS)
        return out;
    if (count == 0)
    {
        if (querySucceeded != nullptr) *querySucceeded = true;
        return out;
    }

    std::vector<VkExtensionProperties> props(count);

    if (enumerate(physicalDevice, nullptr, &count, props.data()) != VK_SUCCESS)
        return out;
    if (querySucceeded != nullptr) *querySucceeded = true;
    props.resize(count);

    out.reserve(count);

    for (const auto& p : props)
        out.emplace_back(p.extensionName);

    return out;
}

inline bool Contains(const std::vector<std::string>& haystack, const char* needle)
{
    for (const auto& h : haystack)
    {
        if (h == needle)
            return true;
    }

    return false;
}

inline bool ListHas(const char* const* list, uint32_t count, const char* needle)
{
    if (list == nullptr) return false;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (list[i] != nullptr && std::strcmp(list[i], needle) == 0)
            return true;
    }

    return false;
}

} // namespace DlssNr::VkExt
