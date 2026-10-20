#include "../OptiScaler/dlssnr/VulkanPresentAdmission.h"
#include "../OptiScaler/dlssnr/VulkanPresentRegistry.h"

#include <cstdio>
#include <cstdlib>

using namespace DlssNr;

static void Check(bool condition, const char* name)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

template<class Registry>
static std::optional<VkNrFrameContract> TaggedContext(Registry& registry,VkImage image,VkFormat format,VkExtent2D extent)
{
    if constexpr(requires { registry.TaggedImageContext(image,format,extent); })
        return registry.TaggedImageContext(image,format,extent);
    else return {};
}
static int legacyQueries = 0, extendedQueries = 0;
static const void* expectedSurfaceChain = nullptr;
static VkResult extendedQueryResult = VK_SUCCESS;
static VKAPI_ATTR VkResult VKAPI_CALL SurfaceCapabilities(VkPhysicalDevice, VkSurfaceKHR,
                                                         VkSurfaceCapabilitiesKHR* out)
{
    ++legacyQueries;
    out->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL SurfaceCapabilities2(VkPhysicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR* input, VkSurfaceCapabilities2KHR* out)
{
    ++extendedQueries;
    Check(input->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR &&
              input->pNext == expectedSurfaceChain && input->surface == (VkSurfaceKHR) (uintptr_t) 0x401 &&
              out->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR && out->pNext == nullptr,
          "extended capability query carries original fullscreen chain and surface");
    out->surfaceCapabilities.supportedUsageFlags = 0x1F;
    return extendedQueryResult;
}

static VkPresentRequest EligibleRequest()
{
    VkPresentRequest r {};
    r.enabled = true;
    r.route = 1;
    r.policy = PresentInput::Policy::ImageOnly;
    r.format = VK_FORMAT_B8G8R8A8_UNORM;
    r.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    r.extent = { 1920, 1080 };
    r.arrayLayers = 1;
    r.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    r.swapchainCount = 1;
    r.imageIndex = 2;
    r.acquiredObserved = true;
    r.deviceGeneration = 5;
    r.swapchainGeneration = 9;
    return r;
}

static VkPresentCapabilities EligibleCapabilities()
{
    VkPresentCapabilities c {};
    c.deviceObserved = true;
    c.modelExtensionsEnabled = true;
    c.routePreparedAtDeviceCreation = true;
    c.surfaceUsageKnown = true;
    c.surfaceUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    c.swapchainPNextKnown = true;
    c.swapchainObserved = true;
    c.imagesObserved = true;
    c.imageCount = 3;
    c.queueObserved = true;
    c.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    c.queueFamilyMatches = true;
    c.presentQueueAccessQualified = true;
    c.deviceGeneration = 5;
    c.swapchainGeneration = 9;
    return c;
}

int main()
{
    VkBaseInStructure monitor { VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT, nullptr };
    VkBaseInStructure fullscreen { VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT, &monitor };
    auto doom = EligibleRequest();
    doom.format = VK_FORMAT_B8G8R8A8_UNORM;
    doom.extent = { 2542, 1345 };
    doom.imageUsage = 0x1F;
    auto doomCaps = EligibleCapabilities();
    doomCaps.swapchainPNextKnown = IsVkSwapchainUsageChainKnown(&fullscreen);
    const auto doomUsage = PrepareVkSwapchainUsage(doom.imageUsage, 0x1F,
                                                  doomCaps.swapchainPNextKnown, true);
    Check(doomUsage.reason == VkPresentRefusal::None && !doomUsage.amended && doomUsage.usage == 0x1F &&
              DecideVkPresent(doom, doomCaps).allowed,
          "observed DOOM fullscreen chain admits its existing transfer usage without amendment");
    Check(fullscreen.pNext == &monitor && monitor.pNext == nullptr,
          "fullscreen admission retains original extension links");
    Check(IsVkSwapchainUsageChainKnown(nullptr), "empty usage chain remains supported");
    VkBaseInStructure unsupported { VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR, nullptr };
    monitor.pNext = &unsupported;
    Check(!IsVkSwapchainUsageChainKnown(&fullscreen), "fullscreen plus unsupported structure still refuses");
    monitor.pNext = &fullscreen;
    Check(!IsVkSwapchainUsageChainKnown(&fullscreen), "cyclic fullscreen chain refuses");
    monitor.pNext = nullptr;
    VkBaseInStructure duplicate { VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT, &fullscreen };
    Check(!IsVkSwapchainUsageChainKnown(&duplicate), "duplicate fullscreen structure refuses");
    const auto fullscreenAmend = PrepareVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0x1F,
                                                        IsVkSwapchainUsageChainKnown(&fullscreen), true);
    Check(fullscreenAmend.amended && fullscreenAmend.usage == 0x13,
          "known fullscreen chain permits supported transfer flags when missing");
    VkSwapchainCreateInfoKHR surfaceCreate {};
    surfaceCreate.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    surfaceCreate.surface = (VkSurfaceKHR) (uintptr_t) 0x401;
    surfaceCreate.pNext = &fullscreen;
    expectedSurfaceChain = &fullscreen;
    Check(QueryVkSwapchainSurfaceUsage(VK_NULL_HANDLE, surfaceCreate, SurfaceCapabilities,
                                      SurfaceCapabilities2) == 0x1F &&
              extendedQueries == 1 && legacyQueries == 0,
          "fullscreen uses matching extended capabilities rather than default fullscreen capabilities");
    Check(QueryVkSwapchainSurfaceUsage(VK_NULL_HANDLE, surfaceCreate, SurfaceCapabilities, nullptr) == 0 &&
              legacyQueries == 0,
          "missing extended capability query refuses fullscreen usage without a default fallback");
    extendedQueryResult = VK_ERROR_SURFACE_LOST_KHR;
    Check(QueryVkSwapchainSurfaceUsage(VK_NULL_HANDLE, surfaceCreate, SurfaceCapabilities,
                                      SurfaceCapabilities2) == 0 && legacyQueries == 0,
          "failed fullscreen capability query does not reuse default surface usage");
    extendedQueryResult = VK_SUCCESS;
    surfaceCreate.pNext = nullptr;
    Check(QueryVkSwapchainSurfaceUsage(VK_NULL_HANDLE, surfaceCreate, SurfaceCapabilities,
                                      SurfaceCapabilities2) == 0x10 && legacyQueries == 1,
          "empty chain retains ordinary capability query");
    VkBaseInStructure unknown { static_cast<VkStructureType>(1900000000), nullptr };
    VkBaseInStructure first { VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR, &unknown };
    const auto described = DescribeVkSwapchainChain(&first);
    Check(described.count == 2 && described.types[0] == first.sType && described.types[1] == unknown.sType,
          "swapchain diagnostic retains known and unknown structure types in original order");
    Check(first.pNext == &unknown && unknown.pNext == nullptr,
          "swapchain diagnostic never changes game extension links");
    Check(DescribeVkSwapchainChain(nullptr).count == 0, "empty chain diagnostic");
    unknown.pNext = &first;
    const auto cycle = DescribeVkSwapchainChain(&first);
    Check(cycle.count == 2 && cycle.cycle && !cycle.truncated, "cyclic chain diagnostic terminates");
    unknown.pNext = nullptr;
    std::array<VkBaseInStructure, 17> longChain {};
    for (size_t i = 0; i < longChain.size(); ++i)
        longChain[i] = { static_cast<VkStructureType>(1900000000 + i),
                         i + 1 < longChain.size() ? &longChain[i + 1] : nullptr };
    const auto bounded = DescribeVkSwapchainChain(longChain.data());
    Check(bounded.count == 16 && bounded.truncated && !bounded.cycle,
          "long chain diagnostic bounds traversal without losing its incomplete marker");
    Check(ShouldHookVulkanPresent(false, true, 1), "Present hooks install with overlay disabled");
    Check(ShouldHookVulkanPresent(false, true, 2), "Auto Present hooks install with overlay disabled");
    Check(ShouldHookVulkanPresent(true, false, 0), "overlay retains its own hook");
    Check(ShouldHookVulkanPresent(false, false, 1),
          "late Present selection remains observable after device creation");
    Check(ShouldPrepareVulkanModelExtensions(true, 0) &&
              ShouldPrepareVulkanModelExtensions(true, 1) &&
              ShouldPrepareVulkanModelExtensions(true, 2) &&
              ShouldPrepareVulkanModelExtensions(false, 1) &&
              !ShouldPrepareVulkanModelExtensions(true, 1, false),
          "model preparation is independent of activation and respects explicit opt-out");
    const auto base = EligibleRequest();
    const auto caps = EligibleCapabilities();
    auto unsupportedCaps = caps;
    unsupportedCaps.modelExtensionsEnabled = false;
    unsupportedCaps.routePreparedAtDeviceCreation = false;
    unsupportedCaps.modelExtensionsUnsupported = true;
    Check(DecideVkPresent(base, unsupportedCaps).reason == VkPresentRefusal::ModelExtensionsUnsupported &&
          !DecideVkPresent(base, unsupportedCaps).needsRecreate,
          "known unsupported model requirements do not send the user into a restart loop");
    unsupportedCaps.modelExtensionsUnsupported = false;
    unsupportedCaps.storageWriteUnsafeChain = true;
    Check(DecideVkPresent(base, unsupportedCaps).reason == VkPresentRefusal::StorageWriteUnsafeChain &&
          !DecideVkPresent(base, unsupportedCaps).needsRecreate,
          "unsafe caller feature chain refuses without promising restart repair");
    unsupportedCaps.storageWriteUnsafeChain = false;
    unsupportedCaps.storageWriteUnsupported = true;
    Check(DecideVkPresent(base, unsupportedCaps).reason == VkPresentRefusal::StorageWriteUnsupported &&
          !DecideVkPresent(base, unsupportedCaps).needsRecreate,
          "known unsupported composition feature does not ask for restart");
    auto result = DecideVkPresent(base, caps);
    Check(result.allowed && result.actualInputClass == PresentInputDecision::InputClass::ImageOnly &&
              result.reason == VkPresentRefusal::None,
          "route 1 admits SDR image only");
    auto implicitTransfer = caps;
    implicitTransfer.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    Check(DecideVkPresent(base, implicitTransfer).allowed,
          "graphics and compute queues implicitly support transfer commands");

    const auto prepared = PrepareVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        caps.surfaceUsage, true, true);
    Check(prepared.amended && prepared.reason == VkPresentRefusal::None &&
              prepared.usage == base.imageUsage,
          "supported transfer flags are added only to a local descriptor");
    const auto untouched = PrepareVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        caps.surfaceUsage, false, true);
    Check(!untouched.amended && untouched.usage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT &&
              untouched.reason == VkPresentRefusal::UnknownUsageChain,
          "unknown usage pNext keeps original swapchain usage");

    auto r = base;
    r.route = 2;
    r.policy = PresentInput::Policy::AutoGuides;
    result = DecideVkPresent(r, caps);
    Check(result.allowed && result.actualInputClass == PresentInputDecision::InputClass::ImageOnly &&
              result.reason == VkPresentRefusal::AutoImageOnly,
          "route 2 Auto reports image-only fallback");

    r.policy = PresentInput::Policy::RequireGuides;
    result = DecideVkPresent(r, caps);
    Check(!result.allowed && result.reason == VkPresentRefusal::GuidesRequired,
          "strict guides refuse without Vulkan guide binding");
    auto guidedCaps=caps;guidedCaps.guidesQualified=true;
    result=DecideVkPresent(r,guidedCaps);
    Check(result.allowed&&result.actualInputClass==PresentInputDecision::InputClass::Guided,
          "strict Enhanced admits authenticated private guides");

    r = base; r.enabled = false;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::Disabled, "NR off is inert");
    r = base; r.route = 0;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::NativeRoute, "Native route excludes Present");
    r = base; r.fgKnownActive = true;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::FrameGenerationActive,
          "known FG activity is not admitted");

    auto c = caps; c.routePreparedAtDeviceCreation = false;
    result = DecideVkPresent(base, c);
    Check(!result.allowed && result.needsRecreate && result.reason == VkPresentRefusal::DeviceRestart,
          "late route selection asks for restart");
    c = caps; c.modelExtensionsEnabled = false;
    result = DecideVkPresent(base, c);
    Check(!result.allowed && result.needsRecreate && result.reason == VkPresentRefusal::ModelExtensionsMissing,
          "missing model extensions ask for restart");
    c = caps; c.surfaceUsage &= ~VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(DecideVkPresent(base, c).reason == VkPresentRefusal::TransferUnsupported,
          "surface without transfer source is refused");
    c = caps; c.swapchainPNextKnown = false;
    Check(DecideVkPresent(base, c).reason == VkPresentRefusal::UnknownUsageChain,
          "unknown usage pNext cannot be amended");
    r = base; r.imageUsage &= ~VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    result = DecideVkPresent(r, caps);
    Check(!result.allowed && result.needsRecreate && result.reason == VkPresentRefusal::TransferNotEnabled,
          "late transfer usage asks for recreate");

    r = base; r.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::UnsupportedSdrRepresentation,
          "HDR format remains outside first slice");
    r = base; r.colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::UnsupportedSdrRepresentation,
          "HDR color space remains outside first slice");
    r = base; r.flags = VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::ProtectedSwapchain,
          "protected swapchain refuses");
    r = base; r.swapchainCount = 2;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::MultipleSwapchains,
          "multi-swapchain Present refuses");
    r = base; r.imageIndex = 3;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::StaleImage,
          "out-of-range image index refuses");
    r = base; r.acquiredObserved = false;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::ImageNotAcquired,
          "unobserved acquisition refuses before effects");
    r = base; r.deviceGeneration = 6;
    Check(DecideVkPresent(r, caps).reason == VkPresentRefusal::StaleGeneration,
          "different device generation refuses");
    c = caps; c.queueFamilyMatches = false;
    Check(DecideVkPresent(base, c).reason == VkPresentRefusal::WrongQueue,
          "wrong present queue family refuses");
    c = caps; c.queueFlags = VK_QUEUE_GRAPHICS_BIT;
    Check(DecideVkPresent(base, c).reason == VkPresentRefusal::WrongQueue,
          "queue without compute and transfer refuses");

    Check(base.imageUsage == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT) && caps.imageCount == 3,
          "pure refusals leave request facts untouched");

    VulkanPresentRegistry registry;
    const auto device = reinterpret_cast<VkDevice>(0x100);
    const auto physical = reinterpret_cast<VkPhysicalDevice>(0x110);
    const auto queue = reinterpret_cast<VkQueue>(0x120);
    const auto swapchain = (VkSwapchainKHR) (uintptr_t) 0x200;
    registry.DeviceCreated(device, physical, true, true, { 3 });
    Check(registry.PhysicalDevice(device) == physical, "swapchain capability query uses owning physical device");
    Check(registry.PresentPrepared(device), "swapchain usage changes require selected Present at device creation");
    registry.QueueObserved(device, queue, 3, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    const auto families = registry.QueueFamilies(device);
    Check(families.size() == 1 && families[0] == 3,
          "surface support queries use only queues created for this device");
    VkSwapchainCreateInfoKHR create {};
    create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create.imageFormat = base.format;
    create.imageColorSpace = base.colorSpace;
    create.imageExtent = base.extent;
    create.imageArrayLayers = 1;
    create.imageUsage = base.imageUsage;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    registry.SwapchainCreated(device, swapchain, create, caps.surfaceUsage, true,
        { (VkImage) (uintptr_t) 0x301, (VkImage) (uintptr_t) 0x302, (VkImage) (uintptr_t) 0x303 },
        { 3 });
    const auto rasterContext=registry.NativeResolutionContext(device,queue,base.extent);
    Check(rasterContext&&rasterContext->swapchain==swapchain&&
        rasterContext->swapchainImageIndex==UINT32_MAX&&!rasterContext->acquireGeneration,
        "unique SR output before acquisition supplies only dimensions context");
    Check(!registry.NativeFrameContext(device,queue,base.extent),
        "final-color context still requires acquisition");
    Check(!registry.NativeResolutionContext(device,reinterpret_cast<VkQueue>(0x121),base.extent)&&
        !registry.NativeResolutionContext(device,queue,{base.extent.width/2,base.extent.height}),
        "unknown queue and different output cannot qualify dimension context");
    const auto otherSwapchain=(VkSwapchainKHR)(uintptr_t)0x201;
    registry.SwapchainCreated(device,otherSwapchain,create,caps.surfaceUsage,true,
        {(VkImage)(uintptr_t)0x304},{3});
    Check(!registry.NativeResolutionContext(device,queue,base.extent),
        "multiple same-size swapchains cannot infer SR destination");
    registry.SwapchainDestroyed(otherSwapchain);
    VkPresentInfoKHR info {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain;
    uint32_t index = 2;
    info.pImageIndices = &index;
    registry.ObserveNativeRenderSize(device,base.extent,{{base.extent.width/2,base.extent.height/2},7});
    auto observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed&&observed->renderSize.value&&observed->renderSize.value->extent.width==base.extent.width/2,
        "scalar render size survives without acquisition or command recording");
    Check(!registry.SnapshotForPresent(queue,info,true,1,PresentInput::Policy::ImageOnly,false)->renderSize.value,
        "Present entry consumes the scalar observation");
    registry.ObserveNativeRenderSize(device,base.extent,{{base.extent.width/2,base.extent.height/2},7});
    registry.PresentSucceeded(swapchain,index);
    Check(registry.SnapshotForPresent(queue,info,false,0,PresentInput::Policy::ImageOnly,false)->renderSize.value.has_value(),
        "new observation during previous Present survives return, then disabled Present consumes it");
    registry.ObserveNativeRenderSize(device,base.extent,{{base.extent.width/2,base.extent.height/2},7});
    registry.InvalidateNativeRenderSize(device);
    Check(!registry.SnapshotForPresent(queue,info,true,1,PresentInput::Policy::ImageOnly,false)->renderSize.value,
        "failed/released SR invalidates prior scalar observations");
    Check(observed.has_value() && !observed->request.acquiredObserved &&
              DecideVkPresent(observed->request, observed->capabilities).reason == VkPresentRefusal::ImageNotAcquired,
          "present info alone does not prove image acquisition");
    registry.ImageAcquired(device, swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() && observed->request.acquiredObserved &&
              DecideVkPresent(observed->request, observed->capabilities).allowed &&
              observed->image == (VkImage) (uintptr_t) 0x303,
          "live acquired image and queue facts reach admission");
    registry.DeviceCreated(device, physical, true, true, { 3, 4 });
    registry.QueueObserved(device, queue, 3, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    registry.SwapchainCreated(device, swapchain, create, caps.surfaceUsage, true,
        { (VkImage) (uintptr_t) 0x301, (VkImage) (uintptr_t) 0x302, (VkImage) (uintptr_t) 0x303 }, { 3 });
    registry.ImageAcquired(device, swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed&&!observed->renderSize.value,"device/swapchain recreation cannot inherit scalar dimensions");
    Check(observed.has_value() &&
              DecideVkPresent(observed->request, observed->capabilities).allowed,
          "current exclusive Present admits with unrelated device queue families");
    registry.QueueObserved(device, queue, 5, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() &&
              DecideVkPresent(observed->request, observed->capabilities).reason == VkPresentRefusal::WrongQueue,
          "current Present cannot qualify a family without surface support");
    registry.QueueObserved(device, queue, 3, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    registry.DeviceCreated(device, physical, true, true, { 4 });
    registry.QueueObserved(device, queue, 3, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    registry.SwapchainCreated(device, swapchain, create, caps.surfaceUsage, true,
        { (VkImage) (uintptr_t) 0x301, (VkImage) (uintptr_t) 0x302, (VkImage) (uintptr_t) 0x303 }, { 3 });
    registry.ImageAcquired(device, swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() &&
              DecideVkPresent(observed->request, observed->capabilities).reason ==
                  VkPresentRefusal::ImageOwnershipUnproven,
          "exclusive Present refuses a family absent from device creation");
    registry.DeviceCreated(device, physical, true, true, { 3, 4 });
    registry.QueueObserved(device, queue, 3, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    const uint32_t concurrentOtherFamilies[] = { 4, 5 };
    create.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create.queueFamilyIndexCount = 2;
    create.pQueueFamilyIndices = concurrentOtherFamilies;
    registry.SwapchainCreated(device, swapchain, create, caps.surfaceUsage, true,
        { (VkImage) (uintptr_t) 0x301, (VkImage) (uintptr_t) 0x302, (VkImage) (uintptr_t) 0x303 }, { 3 });
    registry.ImageAcquired(device, swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() &&
              DecideVkPresent(observed->request, observed->capabilities).reason ==
                  VkPresentRefusal::ImageOwnershipUnproven,
          "concurrent sharing without present queue family refuses");
    const uint32_t concurrentPresentFamilies[] = { 3, 4 };
    create.pQueueFamilyIndices = concurrentPresentFamilies;
    registry.SwapchainCreated(device, swapchain, create, caps.surfaceUsage, true,
        { (VkImage) (uintptr_t) 0x301, (VkImage) (uintptr_t) 0x302, (VkImage) (uintptr_t) 0x303 }, { 3 });
    registry.ImageAcquired(device, swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() && DecideVkPresent(observed->request, observed->capabilities).allowed,
          "concurrent image including the present queue family admits");
    observed->request.fgStateKnown=false;
    Check(!DecideVkPresent(observed->request,observed->capabilities).allowed,"observed FG provider with unknown state cannot be treated as Off");
    registry.PresentSucceeded(swapchain, 2);
    observed = registry.SnapshotForPresent(queue, info, true, 1, PresentInput::Policy::ImageOnly, false);
    Check(observed.has_value() && !observed->request.acquiredObserved,
          "successful original Present consumes acquired fact");
    const auto taggedImage=(VkImage)(uintptr_t)0x303;
    Check(!TaggedContext(registry,taggedImage,create.imageFormat,create.imageExtent),"tag cannot reuse a consumed acquisition");
    registry.ImageAcquired(device,swapchain,2);
    auto tagged=TaggedContext(registry,taggedImage,create.imageFormat,create.imageExtent);
    Check(tagged&&tagged->swapchain==swapchain&&tagged->swapchainImageIndex==2&&tagged->acquireGeneration,
          "public Backbuffer tag resolves exact acquired image without a queue or extent guess");
    Check(!TaggedContext(registry,(VkImage)(uintptr_t)0x302,create.imageFormat,create.imageExtent),"same-size different image is not acquired tag identity");
    Check(TaggedContext(registry,taggedImage,VK_FORMAT_UNDEFINED,{0,0}).has_value(),"sparse public descriptor is filled by exact acquired image facts");
    Check(!TaggedContext(registry,taggedImage,VK_FORMAT_D32_SFLOAT,create.imageExtent),"explicit tag format contradiction refuses");
    Check(!TaggedContext(registry,taggedImage,create.imageFormat,{1,1}),"tag extent must match observed image");
    const auto previousAcquire=tagged->acquireGeneration;
    registry.PresentSucceeded(swapchain,2);registry.ImageAcquired(device,swapchain,2);
    tagged=TaggedContext(registry,taggedImage,create.imageFormat,create.imageExtent);
    Check(tagged&&tagged->acquireGeneration!=previousAcquire,"image reuse carries a new acquisition identity");
    registry.SwapchainDestroyed(swapchain);
    Check(!TaggedContext(registry,taggedImage,create.imageFormat,create.imageExtent),"destroyed swapchain revokes tag context");
    {
        VulkanPresentRegistry application;
        Check(application.ImportDeviceAndQueues(registry,device),"import exact shared device/queue facts");
        const auto proxy=(VkImage)(uintptr_t)0x999;
        application.SwapchainCreated(device,swapchain,create,caps.surfaceUsage,true,{proxy},{3});
        uint32_t proxyIndex=0;auto appInfo=info;appInfo.pImageIndices=&proxyIndex;
        application.ImageAcquired(device,swapchain,0);
        auto app=application.SnapshotForPresent(queue,appInfo,true,1,PresentInput::Policy::ImageOnly,true);
        Check(app&&app->image==proxy&&app->request.deviceGeneration==observed->request.deviceGeneration,
              "proxy namespace shares real device generation but owns different image");
        Check(!DecideVkPresent(app->request,app->capabilities).allowed,"physical FG route stays refused");
        app->capabilities.applicationBeforeProvider=true;
        Check(DecideVkPresent(app->request,app->capabilities).allowed,"explicit pre-provider capability admits FG");
        const auto generation=app->request.swapchainGeneration;
        application.SwapchainImages(device,swapchain,{proxy});
        app=application.SnapshotForPresent(queue,appInfo,true,1,PresentInput::Policy::ImageOnly,true);
        Check(app&&app->request.acquiredObserved&&app->request.swapchainGeneration==generation,"identical images preserve acquisition");
        application.SwapchainImages(device,swapchain,{(VkImage)(uintptr_t)0x998});
        app=application.SnapshotForPresent(queue,appInfo,true,1,PresentInput::Policy::ImageOnly,true);
        Check(app&&!app->request.acquiredObserved&&app->request.swapchainGeneration!=generation,"replaced proxy images invalidate prior acquisition");
        application.DeviceDestroyed(device);
        Check(!application.SnapshotForPresent(queue,appInfo,true,1,PresentInput::Policy::ImageOnly,true),"destroyed proxy device has no snapshot");
    }
    {
        VulkanPresentRegistry devicesOutOfOrder;
        devicesOutOfOrder.DeviceCreated(device,(VkPhysicalDevice)(uintptr_t)77,true,true,{3},false,false,false,false,42);
        devicesOutOfOrder.QueueObserved(device,queue,3,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
        devicesOutOfOrder.SwapchainCreated(device,swapchain,create,caps.surfaceUsage,true,{(VkImage)(uintptr_t)0x303},{3});
        devicesOutOfOrder.ImageAcquired(device,swapchain,0);
        const auto proof=devicesOutOfOrder.LatestAcquireProof(swapchain,0);
        Check(proof&&proof->deviceGeneration==42,"registry adopts authoritative completion generation regardless of observation order");
        auto submitted=VulkanNrRecordings().PrepareSubmission({queue,42,{proof->use}});
        Check(submitted.has_value(),"acquire dependency accepts the actual completion generation");
        VulkanNrRecordings().SubmissionReturned(*submitted,VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    std::puts("PASS: Vulkan Present SDR admission and conservative refusal boundaries");
}
