#pragma once
#include "DlssNr_VkExtensions.h"
#include <array>

namespace DlssNr
{
// Preparation never requests execution. An explicit false leaves the application's
// create arguments intact, including any capabilities it already enabled.
inline bool ShouldPrepareVulkanModelExtensions(bool enabled, uint32_t route,
                                               bool preparationAllowed = true) noexcept
{
    (void) enabled;
    (void) route;
    return preparationAllowed;
}

inline uint32_t VkNrModelExtensionMask(const char* const* names, uint32_t count)
{
    uint32_t mask = 0;
    if (names != nullptr)
        for (uint32_t i = 0; i < std::size(VkExt::kDevice); ++i)
            if (VkExt::ListHas(names, count, VkExt::kDevice[i])) mask |= 1u << i;
    return mask;
}
inline constexpr uint32_t VkNrAllModelExtensions = (1u << std::size(VkExt::kDevice)) - 1u;

struct VkNrDevicePreparation
{
    std::vector<const char*> names;
    uint32_t advertised = 0;
    uint32_t callerEnabled = 0;
    uint32_t added = 0;
    uint32_t unavailable = 0;
    bool removedLegacyBda = false;

    // The returned view borrows names until the real create call returns. All
    // queue/feature/unknown-chain pointers are preserved; caller memory is never patched.
    VkDeviceCreateInfo Apply(const VkDeviceCreateInfo& caller) const noexcept
    {
        auto out = caller;
        if (added != 0 || removedLegacyBda)
        {
            out.enabledExtensionCount = static_cast<uint32_t>(names.size());
            out.ppEnabledExtensionNames = names.data();
        }
        return out;
    }
};

inline VkNrDevicePreparation PrepareVkNrDevice(const VkDeviceCreateInfo& caller,
    const std::vector<std::string>& supported, bool preparationAllowed,
    const VkDeviceCreateInfo* original = nullptr)
{
    VkNrDevicePreparation out;
    out.callerEnabled = VkNrModelExtensionMask(caller.ppEnabledExtensionNames, caller.enabledExtensionCount);
    for (uint32_t i = 0; i < std::size(VkExt::kDevice); ++i)
        if (VkExt::Contains(supported, VkExt::kDevice[i])) out.advertised |= 1u << i;
    out.unavailable = VkNrAllModelExtensions & ~(out.callerEnabled | out.advertised);
    const bool legacyBda = VkExt::ListHas(caller.ppEnabledExtensionNames, caller.enabledExtensionCount,
                                         VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    // EXT and KHR BDA are mutually exclusive (03328). Only our own optional
    // spoofing addition may be replaced; never remove the application's contract.
    const bool ownedLegacy = legacyBda && original &&
        !VkExt::ListHas(original->ppEnabledExtensionNames, original->enabledExtensionCount,
                       VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    if (legacyBda && !ownedLegacy)
    {
        for (uint32_t i=0;i<std::size(VkExt::kDevice);++i)
            if (std::strcmp(VkExt::kDevice[i], VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)==0)
                out.unavailable |= 1u<<i;
    }
    if (ownedLegacy && VkExt::ListHas(caller.ppEnabledExtensionNames, caller.enabledExtensionCount,
                                      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
    {
        // Repair our illegal supplemental addition even with NR preparation off:
        // this preserves the original KHR contract and adds no capability.
        out.names.assign(caller.ppEnabledExtensionNames, caller.ppEnabledExtensionNames+caller.enabledExtensionCount);
        std::erase_if(out.names, [](const char* name) { return name &&
            std::strcmp(name, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)==0; });
        out.removedLegacyBda=true;
    }
    // Treat the existing backend list as one group: partial additions cannot make
    // that backend available and needlessly change unsupported devices.
    if (!preparationAllowed || out.unavailable != 0 ||
        (caller.enabledExtensionCount != 0 && caller.ppEnabledExtensionNames == nullptr)) return out;
    if (caller.enabledExtensionCount != 0)
        out.names.assign(caller.ppEnabledExtensionNames,
                         caller.ppEnabledExtensionNames + caller.enabledExtensionCount);
    if (ownedLegacy)
    {
        std::erase_if(out.names, [](const char* name) { return name &&
            std::strcmp(name, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)==0; });
        out.removedLegacyBda = true;
    }
    for (uint32_t i = 0; i < std::size(VkExt::kDevice); ++i)
        if ((out.callerEnabled & (1u << i)) == 0)
        {
            out.names.push_back(VkExt::kDevice[i]);
            out.added |= 1u << i;
        }
    return out;
}

struct VkNrDeviceCapabilityCertificate
{
    bool createSucceeded = false;
    uint32_t enabledModelExtensions = 0;
    // Observation only: extension enablement does not prove a feature bit. The
    // binary-name list does not establish that Feature 18 requires this bit.
    bool bufferDeviceAddressObserved = false;
    bool bufferDeviceAddressEnabled = false;
    bool featureChainComplete = true;
    bool storageWriteWithoutFormatEnabled = false;

    bool ModelExtensionsEnabled() const noexcept
    { return createSucceeded && enabledModelExtensions == VkNrAllModelExtensions; }
    bool CompositionEnabled() const noexcept
    { return createSucceeded && featureChainComplete && storageWriteWithoutFormatEnabled; }
};

inline VkNrDeviceCapabilityCertificate ObserveVkNrDeviceCreation(
    const VkDeviceCreateInfo& submitted, VkResult result) noexcept
{
    VkNrDeviceCapabilityCertificate out;
    out.createSucceeded = result == VK_SUCCESS;
    if (!out.createSucceeded) return out;
    out.enabledModelExtensions = VkNrModelExtensionMask(submitted.ppEnabledExtensionNames,
                                                       submitted.enabledExtensionCount);
    bool coreFeaturesSeen = submitted.pEnabledFeatures != nullptr;
    if (submitted.pEnabledFeatures != nullptr)
        out.storageWriteWithoutFormatEnabled = submitted.pEnabledFeatures->shaderStorageImageWriteWithoutFormat == VK_TRUE;
    std::array<const VkBaseInStructure*, 64> visited {};
    size_t count = 0;
    auto node = static_cast<const VkBaseInStructure*>(submitted.pNext);
    while (node != nullptr)
    {
        if (count == visited.size()) { out.featureChainComplete = false; break; }
        for (size_t i = 0; i < count; ++i)
            if (visited[i] == node) { out.featureChainComplete = false; return out; }
        visited[count++] = node;
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
        {
            if (coreFeaturesSeen) { out.featureChainComplete = false; return out; }
            coreFeaturesSeen = true;
            out.storageWriteWithoutFormatEnabled =
                reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features.shaderStorageImageWriteWithoutFormat == VK_TRUE;
        }
        const VkBool32* enabled = nullptr;
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
            enabled = &reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(node)->bufferDeviceAddress;
        else if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
            enabled = &reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->bufferDeviceAddress;
        if (enabled != nullptr)
        {
            if (out.bufferDeviceAddressObserved) { out.featureChainComplete = false; return out; }
            out.bufferDeviceAddressObserved = true;
            out.bufferDeviceAddressEnabled = *enabled == VK_TRUE;
        }
        node = node->pNext;
    }
    return out;
}

struct VkNrStorageWritePreparation
{
    VkPhysicalDeviceFeatures features {};
    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    bool amended = false;
    bool useFeatures2 = false;
    bool unsafeChain = false;
    VkDeviceCreateInfo Apply(const VkDeviceCreateInfo& caller) const noexcept
    {
        auto out = caller;
        if (amended)
        {
            if (useFeatures2) out.pNext = &features2;
            else out.pEnabledFeatures = &features;
        }
        return out;
    }
};

// Composition writes RGBA8, RGBA16F and R32 images with the same shader. Unknown
// storage format is legal only with this explicitly enabled core feature. Never
// insert a conflicting Features2 root or rewrite an unknown predecessor node.
inline VkNrStorageWritePreparation PrepareVkNrStorageWrite(const VkDeviceCreateInfo& caller,
    bool physicallySupported, bool preparationAllowed) noexcept
{
    VkNrStorageWritePreparation out;
    const auto observed = ObserveVkNrDeviceCreation(caller, VK_SUCCESS);
    if (!observed.featureChainComplete) { out.unsafeChain = true; return out; }
    if (!preparationAllowed || !physicallySupported || observed.storageWriteWithoutFormatEnabled) return out;
    auto node = static_cast<const VkBaseInStructure*>(caller.pNext);
    for (size_t i = 0; node != nullptr && i < 64; ++i, node = node->pNext)
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
        {
            if (node != caller.pNext) { out.unsafeChain = true; return out; }
            out.features2 = *reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node);
            out.features2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            out.useFeatures2 = out.amended = true;
            return out;
        }
    if (caller.pEnabledFeatures != nullptr) out.features = *caller.pEnabledFeatures;
    out.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    out.amended = true;
    return out;
}
} // namespace DlssNr
