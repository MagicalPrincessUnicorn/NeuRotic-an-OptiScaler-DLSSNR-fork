#include "dlssnr/VulkanDevicePreparation.h"
#include "NrConfigState.h"
#include <cstdlib>
#include <cstdio>

static void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL %s\n", what); std::exit(1); }
}

int main()
{
    using namespace DlssNr;
    const std::vector<std::string> supported(std::begin(VkExt::kDevice), std::end(VkExt::kDevice));
    const char* gameNames[] = { "VK_KHR_swapchain", VkExt::kDevice[0] };
    float priority = 0.75f;
    VkDeviceQueueCreateInfo queue { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queue.queueFamilyIndex = 7; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures features {}; features.shaderInt64 = VK_TRUE;
    VkBaseInStructure unknown { static_cast<VkStructureType>(1900000000), nullptr };
    VkDeviceCreateInfo host { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    host.pNext = &unknown; host.pEnabledFeatures = &features;
    host.queueCreateInfoCount = 1; host.pQueueCreateInfos = &queue;
    host.enabledExtensionCount = 2; host.ppEnabledExtensionNames = gameNames;
    for (bool enabled : { false, true })
        for (uint32_t route : { 0u, 1u, 2u })
        {
            const auto plan = PrepareVkNrDevice(host, supported,
                ShouldPrepareVulkanModelExtensions(enabled, route));
            const auto submitted = plan.Apply(host);
            const auto cert = ObserveVkNrDeviceCreation(submitted, VK_SUCCESS);
            Check(cert.ModelExtensionsEnabled(), "disabled/enabled Native/Present startup prepares same requirements");
            Check(submitted.enabledExtensionCount == 5, "existing requirement is never duplicated");
            Check(submitted.pNext == host.pNext && submitted.pEnabledFeatures == &features &&
                submitted.pQueueCreateInfos == &queue && queue.pQueuePriorities == &priority,
                "unknown pNext, caller features, queue family and priorities preserved");
            Check(host.enabledExtensionCount == 2 && host.ppEnabledExtensionNames == gameNames &&
                unknown.pNext == nullptr, "caller storage unchanged");
            Check(!cert.bufferDeviceAddressObserved && !cert.bufferDeviceAddressEnabled,
                "BDA extension name never invents feature enablement");
            Check(!ObserveVkNrDeviceCreation(submitted, VK_ERROR_FEATURE_NOT_PRESENT).ModelExtensionsEnabled(),
                "failed create never certifies requested extensions");
            const auto again = PrepareVkNrDevice(submitted, supported, true);
            Check(again.added == 0 && again.Apply(submitted).enabledExtensionCount == 5,
                "repeated preparation is idempotent");
            Check(ObserveVkNrDeviceCreation(submitted, VK_SUCCESS).ModelExtensionsEnabled(),
                "later Native to Present selection does not revoke immutable certificate");
        }
    const auto optOut = PrepareVkNrDevice(host, supported, ShouldPrepareVulkanModelExtensions(true, 2, false));
    Check(optOut.added == 0 && optOut.Apply(host).ppEnabledExtensionNames == gameNames,
        "explicit opt-out preserves original extension array");
    const auto unsupported = PrepareVkNrDevice(host, { VkExt::kDevice[0], VkExt::kDevice[1] }, true);
    Check(unsupported.unavailable != 0 && unsupported.added == 0 &&
        !ObserveVkNrDeviceCreation(unsupported.Apply(host), VK_SUCCESS).ModelExtensionsEnabled(),
        "unsupported backend requirements never cause partial additions or readiness");
    const auto unqueried = PrepareVkNrDevice(host, {}, true);
    Check(unqueried.added == 0, "failed capability query does not enable unknown extensions");
    const char* legacyNames[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};
    auto legacy = host; legacy.ppEnabledExtensionNames = legacyNames; legacy.enabledExtensionCount = 2;
    const auto conflicting = PrepareVkNrDevice(legacy, supported, true);
    Check(conflicting.added == 0 && conflicting.unavailable != 0 &&
        conflicting.Apply(legacy).ppEnabledExtensionNames == legacyNames,
        "caller-requested EXT BDA cannot be combined with KHR BDA preparation");
    const auto ownedLegacy = PrepareVkNrDevice(legacy, supported, true, &host);
    const auto replaced = ownedLegacy.Apply(legacy);
    Check(!VkExt::ListHas(replaced.ppEnabledExtensionNames,replaced.enabledExtensionCount,VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
        ObserveVkNrDeviceCreation(replaced,VK_SUCCESS).ModelExtensionsEnabled(),
        "only hook-added legacy BDA is replaced by the supported KHR preparation contract");
    Check(PrepareVkNrDevice(legacy,supported,false,&host).Apply(legacy).ppEnabledExtensionNames==legacyNames,
        "opt-out leaves hook-owned legacy preparation intact");

    host.ppEnabledExtensionNames = VkExt::kDevice;
    host.enabledExtensionCount = static_cast<uint32_t>(std::size(VkExt::kDevice));
    std::vector<const char*> supplemented(std::begin(VkExt::kDevice),std::end(VkExt::kDevice));
    supplemented.push_back(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    auto allSpoofed=host;allSpoofed.ppEnabledExtensionNames=supplemented.data();
    allSpoofed.enabledExtensionCount=static_cast<uint32_t>(supplemented.size());
    const auto allPlan=PrepareVkNrDevice(allSpoofed,supported,true,&host);
    Check(allPlan.added==0 && allPlan.removedLegacyBda &&
        allPlan.Apply(allSpoofed).enabledExtensionCount==host.enabledExtensionCount,
        "hook-added EXT is removed even when all model extensions were already requested");
    Check(PrepareVkNrDevice(allSpoofed,supported,false,&host).Apply(allSpoofed).enabledExtensionCount==host.enabledExtensionCount,
        "preparation opt-out cannot retain our illegal supplemental EXT beside caller KHR");
    Check(ObserveVkNrDeviceCreation(PrepareVkNrDevice(host, {}, false).Apply(host), VK_SUCCESS)
        .ModelExtensionsEnabled(), "opt-out does not reject requirements already enabled by the game");
    VkPhysicalDeviceBufferDeviceAddressFeatures bda {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    bda.bufferDeviceAddress = VK_TRUE;
    bda.pNext = &unknown; host.pNext = &bda;
    auto cert = ObserveVkNrDeviceCreation(host, VK_SUCCESS);
    Check(cert.bufferDeviceAddressObserved && cert.bufferDeviceAddressEnabled && cert.featureChainComplete,
        "extension feature node is observed through an unknown chain without mutation");
    VkPhysicalDeviceVulkan12Features v12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    host.pNext = &v12;
    cert = ObserveVkNrDeviceCreation(host, VK_SUCCESS);
    Check(cert.bufferDeviceAddressObserved && !cert.bufferDeviceAddressEnabled,
        "core feature false remains false despite extension enablement");
    v12.pNext = &bda;
    Check(!ObserveVkNrDeviceCreation(host, VK_SUCCESS).featureChainComplete,
        "duplicate feature representations are not certified as a complete observation");
    host.pNext = &unknown; unknown.pNext = &unknown;
    Check(!ObserveVkNrDeviceCreation(host, VK_SUCCESS).featureChainComplete,
        "cyclic unknown chain observation terminates");
    unknown.pNext = nullptr;

    host.pNext = &unknown;
    host.pEnabledFeatures = &features;
    const auto storagePlan = PrepareVkNrStorageWrite(host, true, true);
    const auto withStorage = storagePlan.Apply(host);
    Check(storagePlan.amended && withStorage.pNext == &unknown &&
        withStorage.pEnabledFeatures != &features && features.shaderStorageImageWriteWithoutFormat == VK_FALSE &&
        withStorage.pEnabledFeatures->shaderInt64 == VK_TRUE &&
        ObserveVkNrDeviceCreation(withStorage, VK_SUCCESS).CompositionEnabled(),
        "storage write feature copied and enabled without changing caller features or unknown chain");
    Check(!PrepareVkNrStorageWrite(host, true, false).amended &&
          !PrepareVkNrStorageWrite(host, false, true).amended,
        "storage feature respects opt-out and physical unsupported result");
    Check(!ObserveVkNrDeviceCreation(host, VK_SUCCESS).CompositionEnabled(),
        "physical support and extension strings never replace actual feature enablement");
    VkPhysicalDeviceFeatures2 featureRoot { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    featureRoot.features.shaderInt64 = VK_TRUE; featureRoot.pNext = &unknown;
    host.pEnabledFeatures = nullptr; host.pNext = &featureRoot;
    const auto rootPlan = PrepareVkNrStorageWrite(host, true, true);
    const auto rootInfo = rootPlan.Apply(host);
    Check(rootPlan.amended && rootInfo.pNext == &rootPlan.features2 &&
        rootPlan.features2.pNext == &unknown && rootPlan.features2.features.shaderInt64 == VK_TRUE &&
        featureRoot.features.shaderStorageImageWriteWithoutFormat == VK_FALSE &&
        ObserveVkNrDeviceCreation(rootInfo,VK_SUCCESS).CompositionEnabled(),
        "Features2 root copied with unknown tail preserved and no duplicate representation");
    unknown.pNext = reinterpret_cast<const VkBaseInStructure*>(&featureRoot); featureRoot.pNext = nullptr;
    host.pNext = &unknown;
    const auto deepRoot = PrepareVkNrStorageWrite(host, true, true);
    Check(!deepRoot.amended && deepRoot.unsafeChain,
        "unknown predecessor to disabled Features2 refuses mutation");
    featureRoot.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    Check(!PrepareVkNrStorageWrite(host, true, true).amended &&
        ObserveVkNrDeviceCreation(host,VK_SUCCESS).CompositionEnabled(),
        "already enabled feature through unknown predecessor remains usable");
    unknown.pNext = nullptr; host.pNext = &unknown;

    NrConfigState state;
    NrOptional<bool> savedEnabled { false }, savedPrepare { true };
    state.LoadEnabled(savedEnabled, std::nullopt);
    Check(!state.Snapshot().enabled && savedPrepare.value_or_default(),
        "Enabled=auto stays off while preparation defaults on");
    state.SetEnabled(savedEnabled, true);
    (void) ObserveVkNrDeviceCreation(host, VK_ERROR_INITIALIZATION_FAILED);
    const auto persistedEnabled = savedEnabled.value_for_config();
    Check(persistedEnabled == true && state.Snapshot().enabled,
        "runtime create failure never changes saved enable intent");
    savedPrepare.set_from_config(false);
    const auto persistedPrepare = savedPrepare.value_for_config();
    NrConfigState reloaded;
    NrOptional<bool> loadedEnabled { false }, loadedPrepare { true };
    reloaded.LoadEnabled(loadedEnabled, persistedEnabled);
    loadedPrepare.set_from_config(persistedPrepare);
    Check(reloaded.Snapshot().enabled && !loadedPrepare.value_or_default(),
        "explicit enabled intent and preparation opt-out survive config option roundtrip");
    std::puts("PASS Vulkan preparation: disabled/Native/Present, opt-out, unsupported, no duplicates, "
              "successful-create certificate, caller preservation, feature observation, intent roundtrip");
}
