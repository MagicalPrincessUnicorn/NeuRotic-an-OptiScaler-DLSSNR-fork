#include "pch.h"
#include <menu/Localization.h>
#include "menu_common.h"
#include "menu_overlay_base.h"
#include "menu_overlay_vk.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_impl_win32.h>
#include <atomic>
#include <algorithm>
#include <memory>
#include <vector>
#include <chrono>
#include <dlssnr/VulkanPresentRegistry.h>
#include <dlssnr/VulkanNrCompletion.h>
#include "input/input_system.h"
#include "nr/semantic/character/CharacterCaptureVk.h"
#include "nr/semantic/character/CharacterRuntime.h"
#include "nr/semantic/character/CharacterFgActivity.h"

// Vulkan overlay code adopted from here:
// https://gist.github.com/mem99/0ec31ca302927457f86b1d6756aaa8c4
// Need to check resize & recreate fixes

static bool _isInited = false;

static bool _vulkanObjectsCreated = false;
static bool _vkBackendInitialized = false;
static std::atomic<bool> _vkRestartRequired = false;
static std::atomic<VkDevice> _vkOwnedDevice = VK_NULL_HANDLE;
static std::atomic<VkDevice> _vkCreatingDevice = VK_NULL_HANDLE;
static std::mutex _vkOperationMutex;

// imgui stuff
struct ImGui_ImplVulkan_InitInfo _ImVulkan_Info = {};
struct ImGui_ImplVulkanH_Frame* _ImVulkan_Frames = VK_NULL_HANDLE;
static VkSemaphore* _ImVulkan_Semaphores = VK_NULL_HANDLE;
static VkRenderPass _vkRenderPass = VK_NULL_HANDLE;
static uint32_t _scImageCount;
static ULONG64 _frameCount;
static std::atomic<bool> _vkOverlaySubmissionUncertain = false;
static std::atomic<bool> _vkClosingRequested = false;
static VkSwapchainKHR _vkSwapchain = VK_NULL_HANDLE;
static std::vector<bool> _vkSemaphorePending;
static std::vector<uint64_t> _vkSemaphoreAcquisition;
static std::vector<uint64_t> _vkSemaphorePresentSerial;
static uint64_t _vkDeviceGeneration = 0, _vkSwapchainGeneration = 0;
static MenuOverlayVk::BoundaryContext _vkBoundary;
struct RetainedOverlayWait {
    VkDevice device; VkSemaphore semaphore; VkSwapchainKHR swapchain;
    uint64_t deviceGeneration, swapchainGeneration, acquisition;
    uint32_t imageIndex;
    MenuOverlayVk::BoundaryContext boundary;
    uint64_t presentSerial = 0;
    VkQueue presentQueue = VK_NULL_HANDLE;
    std::shared_ptr<const DlssNr::VkNrAcquireProof> proof;
};
static std::vector<RetainedOverlayWait> _vkRetainedWaits;
static constexpr uint32_t MaxOverlayImages = 256;
static constexpr size_t MaxRetainedOverlayWaits = 1024;
static bool DestroyVulkanObjectsLocked(bool shutdown);
struct PendingOverlayCreate {
    VkDevice device = VK_NULL_HANDLE; VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE; HWND window = nullptr;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR info {};
    MenuOverlayVk::BoundaryContext boundary;
};
static PendingOverlayCreate _vkPendingCreate;
static bool _vkCreationPending = false;
static std::chrono::steady_clock::time_point _vkNextCreateAttempt {};
static unsigned _vkCreateFailures = 0;
static uint64_t _vkCapacityBypasses = 0;
static uint32_t _vkLastSubmittedImage = UINT32_MAX;
static Neurotic::Semantic::Character::CharacterCaptureVk _vkCharacterCapture;
struct CharacterRecordingOutcome {
    int slot=-1;
    ~CharacterRecordingOutcome(){_vkCharacterCapture.Cancel(slot);}
};

struct OverlayFrameOutcome
{
    bool cpuProcessed = false;
    bool submitted = false;
    bool transientBusy = false;
    ~OverlayFrameOutcome()
    {
        if (!submitted)
        {
            if (!transientBusy || !MenuCommon::CanRetainRendererCaptureOnBusyFrame())
                MenuCommon::SetRendererCaptureAvailable(false);
            Neurotic::Semantic::Character::CharacterSourceInvalidated();
        }
        if (!cpuProcessed)
            MenuCommon::ProcessUnavailableInput();
    }
};
static bool SameBoundary(const MenuOverlayVk::BoundaryContext& a, const MenuOverlayVk::BoundaryContext& b)
{
    return a.generation == b.generation && a.registry == b.registry && a.getImages == b.getImages;
}

static bool ProvesOverlayWait(const RetainedOverlayWait& wait,
                             const DlssNr::VkNrAcquireProof& proof)
{
    if (!wait.presentSerial || wait.device != proof.device ||
        wait.deviceGeneration != proof.deviceGeneration)
        return false;
    if (wait.swapchain == proof.swapchain && wait.swapchainGeneration == proof.swapchainGeneration &&
        wait.imageIndex == proof.imageIndex && wait.acquisition < proof.acquireGeneration &&
        proof.priorPresentSerial >= wait.presentSerial)
        return true;
    // A completed acquisition after a successful successor Present on the
    // same queue proves earlier retired-swapchain presentation waits finished.
    // Preserve exact device, boundary, queue and oldSwapchain lineage evidence.
    return wait.presentQueue != VK_NULL_HANDLE && wait.presentQueue == proof.priorPresentQueue &&
        proof.priorPresentSerial > wait.presentSerial &&
        std::find(proof.predecessorGenerations.begin(), proof.predecessorGenerations.end(),
                  wait.swapchainGeneration) != proof.predecessorGenerations.end();
}

static void MaintainOverlayWaits(const DlssNr::VkObservedPresent* observed = nullptr,
                                MenuOverlayVk::BoundaryContext boundary = {})
{
    if (_vkRestartRequired)
        return; // Quarantined ownership must never dispatch through a dead device.
    // Poll the actual submission owner even with NR disabled or no drawable
    // backend. Leases outlive registry image slots and swapchain destruction.
    DlssNr::PollVkNrCompletions();
    std::shared_ptr<const DlssNr::VkNrAcquireProof> candidate;
    if (observed)
    {
        auto& registry = boundary.registry ? *boundary.registry : DlssNr::GetVulkanPresentRegistry();
        candidate = registry.AcquireProof(observed->swapchain, observed->request.imageIndex,
                                          observed->request.acquireGeneration);
    }
    std::erase_if(_vkRetainedWaits, [&](RetainedOverlayWait& wait) {
        if (!wait.proof && candidate && SameBoundary(wait.boundary, boundary) &&
            ProvesOverlayWait(wait, *candidate))
            wait.proof = candidate;
        if (!wait.proof || !wait.proof->Complete())
            return false;
        vkDestroySemaphore(wait.device, wait.semaphore, nullptr);
        return true;
    });
    if (_vkCapacityBypasses && _vkRetainedWaits.size() + _scImageCount < MaxRetainedOverlayWaits)
    {
        LOG_INFO("Vulkan overlay Present wait capacity recovered: retained={} bypasses={}",
                 _vkRetainedWaits.size(), _vkCapacityBypasses);
        _vkCapacityBypasses = 0;
    }
}

static void SetVkObjectName(VkDevice device, VkInstance instance, VkObjectType objectType, uint64_t objectHandle,
                            const char* name)
{
    static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;

    if (vkSetDebugUtilsObjectNameEXT == nullptr)
        vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));

    VkDebugUtilsObjectNameInfoEXT info {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = objectHandle;
    info.pObjectName = name;

    vkSetDebugUtilsObjectNameEXT(device, &info);
}

static void CreateVulkanObjects(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                                const VkSwapchainCreateInfoKHR* pCreateInfo, VkSwapchainKHR* pSwapchain,
                                VkQueue queue, uint32_t queueFamily, MenuOverlayVk::BoundaryContext boundary)
{
    LOG_FUNC();

    if (_vkRestartRequired)
        return;
    if (device == VK_NULL_HANDLE || pCreateInfo == nullptr || pSwapchain == nullptr || *pSwapchain == VK_NULL_HANDLE)
    {
        LOG_WARN(
            "device({0:X}) == VK_NULL_HANDLE || pCreateInfo({1:X}) == nullptr || *pSwapchain({2:X}) == VK_NULL_HANDLE",
            (UINT64) device, (UINT64) pCreateInfo, pSwapchain ? (UINT64) *pSwapchain : 0);
        return;
    }

    if (_ImVulkan_Info.Device != VK_NULL_HANDLE)
    {
        LOG_DEBUG("_vulkanObjectsCreated, releasing objects");

        if (!DestroyVulkanObjectsLocked(false))
            return;
    }

    // Initialize ImGui
    if (!MenuOverlayBase::IsInited() || MenuOverlayBase::Handle() != hwnd)
    {
        if (MenuOverlayBase::IsInited())
            MenuOverlayBase::Shutdown();

        LOG_DEBUG("MenuOverlayBase::Init");
        MenuOverlayBase::Init(hwnd, false);
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = static_cast<float>(pCreateInfo->imageExtent.width);
    io.DisplaySize.y = static_cast<float>(pCreateInfo->imageExtent.height);

    VkResult result;

    // The queried count is not an array capacity. Retry changing lists without
    // allowing unbounded allocation or an infinite VK_INCOMPLETE loop.
    std::vector<VkImage> images;
    const auto getImages = boundary.getImages ? boundary.getImages : vkGetSwapchainImagesKHR;
    for (unsigned attempt = 0; attempt < 4; ++attempt)
    {
        uint32_t count = 0;
        result = getImages(device, *pSwapchain, &count, nullptr);
        if (result != VK_SUCCESS || count == 0 || count > MaxOverlayImages)
            return;
        images.resize(count);
        result = getImages(device, *pSwapchain, &count, images.data());
        if (result == VK_SUCCESS && count > 0 && count <= images.size())
        {
            images.resize(count);
            break;
        }
        images.clear();
        if (result != VK_INCOMPLETE)
            return;
    }
    if (images.empty() || _vkRetainedWaits.size() + images.size() > MaxRetainedOverlayWaits)
    {
        LOG_WARN("Vulkan overlay creation deferred: image enumeration or retained WSI wait capacity");
        return;
    }

    // Reopening belongs only to CreateSwapchain admission, before driver calls.
    if (_vkClosingRequested || queue == VK_NULL_HANDLE || queueFamily == UINT32_MAX)
        return;

    // Record ownership before the first allocation, including partial init.
    _scImageCount = static_cast<uint32_t>(images.size());
    _ImVulkan_Info.Device = device;
    _vkOwnedDevice = device;
    _ImVulkan_Info.ImageCount = _scImageCount;
    if (pCreateInfo->imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        _vkCharacterCapture.Initialize(pd,device,pCreateInfo->imageFormat,pCreateInfo->imageColorSpace,
                                       pCreateInfo->imageExtent);
    _vkSwapchain = *pSwapchain;
    _vkBoundary = boundary;
    _vkSemaphorePending.assign(_scImageCount, false);
    _vkSemaphoreAcquisition.assign(_scImageCount, 0);
    _vkSemaphorePresentSerial.assign(_scImageCount, 0);
    _ImVulkan_Frames = (ImGui_ImplVulkanH_Frame*) IM_ALLOC(sizeof(ImGui_ImplVulkanH_Frame) * _scImageCount);
    _ImVulkan_Semaphores = (VkSemaphore*) IM_ALLOC(sizeof(VkSemaphore) * _scImageCount);
    if (_ImVulkan_Frames)
        memset(_ImVulkan_Frames, 0, sizeof(ImGui_ImplVulkanH_Frame) * _scImageCount);
    if (_ImVulkan_Semaphores)
        memset(_ImVulkan_Semaphores, 0, sizeof(VkSemaphore) * _scImageCount);
    if (!_ImVulkan_Frames || !_ImVulkan_Semaphores)
        return;

    // Create the render pool
    VkDescriptorPool pool = VK_NULL_HANDLE;
    {
        VkDescriptorPoolSize sampler_pool_size = {};
        sampler_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_pool_size.descriptorCount = 8; // required by ImGui 1.92

        VkDescriptorPoolCreateInfo desc_pool_info = {};
        desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        desc_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        desc_pool_info.maxSets = 8;
        desc_pool_info.poolSizeCount = 1;
        desc_pool_info.pPoolSizes = &sampler_pool_size;

        result = vkCreateDescriptorPool(device, &desc_pool_info, NULL, &_ImVulkan_Info.DescriptorPool);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateDescriptorPool error: {0:X}", (UINT) result);
            return;
        }
    }

    pool = _ImVulkan_Info.DescriptorPool;

    // Create the render pass
    {
        VkAttachmentDescription attachment_desc = {};

        attachment_desc.format = pCreateInfo->imageFormat;
        attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_desc.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment_desc;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        result = vkCreateRenderPass(device, &render_pass_info, NULL, &_vkRenderPass);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateRenderPass error: {0:X}", (UINT) result);
            return;
        }
    }

    // Create The Image Views
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;

        info.format = pCreateInfo->imageFormat;
        info.components.r = VK_COMPONENT_SWIZZLE_R;
        info.components.g = VK_COMPONENT_SWIZZLE_G;
        info.components.b = VK_COMPONENT_SWIZZLE_B;
        info.components.a = VK_COMPONENT_SWIZZLE_A;

        VkImageSubresourceRange image_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        info.subresourceRange = image_range;

        for (uint32_t i = 0; i < _scImageCount; i++)
        {
            ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
            fd->Backbuffer = images[i];
            info.image = fd->Backbuffer;

            result = vkCreateImageView(device, &info, NULL, &fd->BackbufferView);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateImageView error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_IMAGE_VIEW, (UINT64) fd->BackbufferView,
                            Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_view_dd51f3ea", "ImGui Backbuffer View"));
#endif
        }
    }

    // Create frame Buffer
    {
        VkImageView attachment[1];
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = _vkRenderPass;
        info.attachmentCount = 1;
        info.pAttachments = attachment;

        info.width = pCreateInfo->imageExtent.width;
        info.height = pCreateInfo->imageExtent.height;

        info.layers = 1;

        for (uint32_t i = 0; i < _scImageCount; i++)
        {
            ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
            attachment[0] = fd->BackbufferView;
            result = vkCreateFramebuffer(device, &info, NULL, &fd->Framebuffer);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateFramebuffer error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_FRAMEBUFFER, (UINT64) fd->Framebuffer,
                            Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_framebuffer_cd35dff3", "ImGui Backbuffer Framebuffer"));
#endif
        }
    }

    // Create command pools, command buffers, fences, and semaphores for every image
    for (uint32_t i = 0; i < _scImageCount; i++)
    {
        ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[i];
        VkSemaphore* fsd = &_ImVulkan_Semaphores[i];
        {
            VkCommandPoolCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = queueFamily;
            result = vkCreateCommandPool(device, &info, NULL, &fd->CommandPool);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateCommandPool error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_COMMAND_POOL, (UINT64) fd->CommandPool,
                            Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_command_pool_1935450b", "ImGui Backbuffer Command Pool"));
#endif
        }

        {
            VkCommandBufferAllocateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = fd->CommandPool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            result = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkAllocateCommandBuffers error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_COMMAND_BUFFER, (UINT64) fd->CommandBuffer,
                            Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_command_buffer_ffd5c5f9", "ImGui Backbuffer Command Buffer"));
#endif
        }

        {
            VkFenceCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            result = vkCreateFence(device, &info, NULL, &fd->Fence);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateFence error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_FENCE, (UINT64) fd->Fence, Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_fence_7994636e", "ImGui Backbuffer Fence"));
#endif
        }

        {
            VkSemaphoreCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            result = vkCreateSemaphore(device, &info, NULL, fsd);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("vkCreateSemaphore error: {0:X}", (UINT) result);
                return;
            }

#ifdef VULKAN_DEBUG_LAYER
            SetVkObjectName(device, instance, VK_OBJECT_TYPE_SEMAPHORE, (UINT64) fsd, Neurotic::UiLiteral("ingame.menu-overlay-vk.imgui_backbuffer_semaphore_c86228ed", "ImGui Backbuffer Semaphore"));
#endif
        }
    }

    // Initialize ImGui and upload fonts
    {
        _ImVulkan_Info.Instance = instance;
        _ImVulkan_Info.PhysicalDevice = pd;
        _ImVulkan_Info.Device = device;
        _ImVulkan_Info.QueueFamily = queueFamily;
        _ImVulkan_Info.Queue = queue;
        _ImVulkan_Info.DescriptorPool = pool;
        _ImVulkan_Info.Subpass = 0;
        _ImVulkan_Info.MinImageCount = pCreateInfo->minImageCount;
        _ImVulkan_Info.ImageCount = _scImageCount;
        _ImVulkan_Info.Allocator = NULL;
        _ImVulkan_Info.RenderPass = _vkRenderPass;

        bool initResult = ImGui_ImplVulkan_Init(&_ImVulkan_Info);
        _vkBackendInitialized = initResult;
        LOG_DEBUG("ImGui_ImplVulkan_Init result: {}", initResult);

        if (!initResult)
            return;

        // Upload Fonts
        // Use any command queue
        VkCommandPool command_pool = _ImVulkan_Frames[0].CommandPool;
        VkCommandBuffer command_buffer = _ImVulkan_Frames[0].CommandBuffer;
        result = vkResetCommandPool(device, command_pool, 0);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkBeginCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkBeginCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        // initResult = ImGui_ImplVulkan_CreateFontsTexture();
        // LOG_DEBUG("ImGui_ImplVulkan_CreateFontsTexture result: {}", initResult);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkEndCommandBuffer error: {0:X}", (UINT) result);
            return;
        }

        result = vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) result);
            return;
        }

        result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkDeviceWaitIdle error: {0:X}", (UINT) result);
            return;
        }
    }

    _vulkanObjectsCreated = true;
    LOG_FUNC_RESULT(_vulkanObjectsCreated);
}

static bool DestroyVulkanObjectsLocked(bool shutdown)
{
    MenuCommon::SetRendererCaptureAvailable(false);
    _vulkanObjectsCreated = false;
    _isInited = false;
    if (_vkRestartRequired)
        return false;
    if (_ImVulkan_Info.Device == VK_NULL_HANDLE)
        return true;

    const auto device = _ImVulkan_Info.Device;
    const auto result = vkDeviceWaitIdle(device);
    if (result != VK_SUCCESS)
    {
        LOG_WARN("Vulkan overlay retained after failed idle: result={} shutdown={}", (int) result, shutdown);
        return false;
    }

    // Backend pipelines/descriptors may still refer to the pool/render pass.
    // Never run backend shutdown before the GPU drain succeeds.
    _vkCharacterCapture.ReleaseAfterIdle();
    Neurotic::Semantic::Character::CharacterSourceInvalidated();
    if (_vkBackendInitialized && MenuOverlayBase::IsInited() && ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().BackendRendererUserData != nullptr)
        ImGui_ImplVulkan_Shutdown(false);
    _vkBackendInitialized = false;

    for (uint32_t i = 0; _ImVulkan_Frames && i < _scImageCount; ++i)
    {
        auto& frame = _ImVulkan_Frames[i];
        if (frame.Fence)
            vkDestroyFence(device, frame.Fence, nullptr);
        if (frame.CommandBuffer)
            vkFreeCommandBuffers(device, frame.CommandPool, 1, &frame.CommandBuffer);
        if (frame.CommandPool)
            vkDestroyCommandPool(device, frame.CommandPool, nullptr);
        if (frame.Framebuffer)
            vkDestroyFramebuffer(device, frame.Framebuffer, nullptr);
        if (frame.BackbufferView)
            vkDestroyImageView(device, frame.BackbufferView, nullptr);
        frame = {};
    }
    for (uint32_t i = 0; _ImVulkan_Semaphores && i < _scImageCount; ++i)
    {
        const auto semaphore = _ImVulkan_Semaphores[i];
        if (semaphore == VK_NULL_HANDLE)
            continue;
        if (_vkSemaphorePending[i])
        {
            // GPU idle does not prove a presentation engine consumed its wait.
            // Keep the small WSI owner until actual acquire-completion or an
            // enabled maintenance present fence supplies that proof.
            _vkRetainedWaits.push_back({device, semaphore, _vkSwapchain, _vkDeviceGeneration,
                _vkSwapchainGeneration, _vkSemaphoreAcquisition[i], i, _vkBoundary,
                _vkSemaphorePresentSerial[i], _ImVulkan_Info.Queue});
        }
        else
            vkDestroySemaphore(device, semaphore, nullptr);
    }
    if (_vkRenderPass)
        vkDestroyRenderPass(device, _vkRenderPass, nullptr);
    if (_ImVulkan_Info.DescriptorPool)
        vkDestroyDescriptorPool(device, _ImVulkan_Info.DescriptorPool, nullptr);
    IM_FREE(_ImVulkan_Frames);
    IM_FREE(_ImVulkan_Semaphores);
    _ImVulkan_Frames = nullptr;
    _ImVulkan_Semaphores = nullptr;
    _vkRenderPass = VK_NULL_HANDLE;
    _vkSwapchain = VK_NULL_HANDLE;
    _scImageCount = 0;
    _vkSemaphorePending.clear();
    _vkSemaphoreAcquisition.clear();
    _vkSemaphorePresentSerial.clear();
    _ImVulkan_Info = {};
    _vkOwnedDevice = VK_NULL_HANDLE;
    _vkOverlaySubmissionUncertain = false;
    if (!_vkRetainedWaits.empty())
        LOG_WARN("Vulkan overlay retained WSI waits={} (Present consumption proof unavailable)", _vkRetainedWaits.size());
    return true;
}

void MenuOverlayVk::DestroyVulkanObjects(bool shutdown)
{
    _vkClosingRequested = true;
    std::unique_lock<std::mutex> lock(_vkOperationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        LOG_WARN("Vulkan overlay cleanup deferred: operation active");
        return;
    }
    DestroyVulkanObjectsLocked(shutdown);
}

void MenuOverlayVk::ShutdownDevice(VkDevice device)
{
    if (_vkOwnedDevice == device || _vkCreatingDevice == device)
        _vkClosingRequested = true;
    std::unique_lock<std::mutex> lock(_vkOperationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        LOG_WARN("Vulkan overlay device cleanup deferred: operation active");
        return;
    }
    if (_vkPendingCreate.device == device)
    {
        _vkCreationPending = false;
        _vkCreatingDevice = VK_NULL_HANDLE;
    }
    if (_ImVulkan_Info.Device == device)
        DestroyVulkanObjectsLocked(true);
}

void MenuOverlayVk::ShutdownSwapchain(VkDevice device, VkSwapchainKHR swapchain)
{
    std::unique_lock<std::mutex> lock(_vkOperationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        LOG_WARN("Vulkan overlay swapchain cleanup deferred: operation active");
        return;
    }
    if (_vkCreationPending && _vkPendingCreate.device == device && _vkPendingCreate.swapchain == swapchain)
    {
        _vkCreationPending = false;
        _vkCreatingDevice = VK_NULL_HANDLE;
    }
    if (_ImVulkan_Info.Device == device && _vkSwapchain == swapchain)
    {
        // A replacement may already have been admitted by CreateSwapchain while
        // the old renderer awaited its first Present. Preserve that admission;
        // never reopen after a driver call (a concurrent shutdown may close it).
        if (!_vkCreationPending)
            _vkClosingRequested = true;
        if (!DestroyVulkanObjectsLocked(false))
            _vkClosingRequested = true;
    }
}

void MenuOverlayVk::NotifyDeviceDestroyed(VkDevice device)
{
    std::unique_lock<std::mutex> lock(_vkOperationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        // A valid caller externally synchronizes destruction with queue work.
        // If that contract was violated, prevent future dead-device dispatch.
        if (_vkOwnedDevice == device || _vkCreatingDevice == device)
            _vkRestartRequired = true;
        LOG_ERROR("Vulkan overlay device destroyed during active operation; ownership quarantined");
        return;
    }
    std::erase_if(_vkRetainedWaits, [device](const RetainedOverlayWait& wait) { return wait.device == device; });
    if (_vkPendingCreate.device == device)
    {
        _vkPendingCreate = {};
        _vkCreationPending = false;
        _vkCreatingDevice = VK_NULL_HANDLE;
    }
    if (_ImVulkan_Info.Device != device)
        return;

    _vkCharacterCapture.AbandonDestroyedDevice();
    Neurotic::Semantic::Character::CharacterSourceInvalidated();

    // The ImGui backend has no CPU-only abandonment entry point. Its opaque CPU
    // allocations remain quarantined if live cleanup could not retire it.
    // Never run ImGui_ImplVulkan_Shutdown against an already destroyed device.
    if (_vkBackendInitialized)
    {
        _vkRestartRequired = true;
        LOG_ERROR("Vulkan overlay backend quarantined after device destruction; restart required");
    }
    IM_FREE(_ImVulkan_Frames);
    IM_FREE(_ImVulkan_Semaphores);
    _ImVulkan_Frames = nullptr;
    _ImVulkan_Semaphores = nullptr;
    _ImVulkan_Info = {};
    _vkOwnedDevice = VK_NULL_HANDLE;
    _vkRenderPass = VK_NULL_HANDLE;
    _vkSwapchain = VK_NULL_HANDLE;
    _scImageCount = 0;
    _vkSemaphorePending.clear();
    _vkSemaphoreAcquisition.clear();
    _vkSemaphorePresentSerial.clear();
    _vulkanObjectsCreated = false;
    _vkBackendInitialized = false;
    _isInited = false;
    _vkClosingRequested = true;
}

MenuOverlayVk::PresentLock MenuOverlayVk::LockPresent()
{
    return PresentLock(_vkOperationMutex, std::try_to_lock);
}

void MenuOverlayVk::Unavailable()
{
    auto lock = LockPresent();
    if (!lock.owns_lock())
        return; // The active Present owns both renderer and input publication.
    MaintainOverlayWaits();
    Neurotic::Semantic::Character::CharacterSourceInvalidated();
    MenuCommon::ProcessUnavailableInput();
}

void MenuOverlayVk::Presented(const PresentLock& lock, bool accepted, BoundaryContext boundary)
{
    if (!lock.owns_lock() || lock.mutex() != &_vkOperationMutex)
        return;
    if(!accepted)Neurotic::Semantic::Character::CharacterSourceInvalidated();
    if (accepted && _vkLastSubmittedImage < _vkSemaphorePresentSerial.size() &&
        SameBoundary(boundary, _vkBoundary))
    {
        auto& registry = boundary.registry ? *boundary.registry : DlssNr::GetVulkanPresentRegistry();
        _vkSemaphorePresentSerial[_vkLastSubmittedImage] =
            registry.LastPresentSerial(_vkSwapchain, _vkLastSubmittedImage);
    }
    MenuCommon::SetRendererCaptureAvailable(accepted && !_vkClosingRequested &&
        _vulkanObjectsCreated && SameBoundary(boundary, _vkBoundary));
}

static bool OverlayPresentChainSupported(const void* chain)
{
    // These nodes contain downstream scheduling/completion metadata, which stays
    // borrowed and unchanged for the synchronous Present call. Damage regions
    // need owned full-damage replacements, and device groups need matching draw
    // masks; neither is implemented. Refuse all other mutations before GPU work.
    auto node = static_cast<const VkBaseInStructure*>(chain);
    for (unsigned count = 0; node && count < 32; ++count, node = node->pNext)
    {
        switch (static_cast<int>(node->sType))
        {
        case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
        case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE:
        case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT:
        case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT:
        // VK_STRUCTURE_TYPE_SET_PRESENT_CONFIG_NV (VK_NV_present_metering).
        // The SDK guards only the enum behind VK_ENABLE_BETA_EXTENSIONS. Use
        // its registered value without enabling unrelated provisional APIs.
        // This node controls downstream pacing, with feedback in caller-owned
        // storage. Preserve its exact pointer through the synchronous Present;
        // the overlay neither changes the batch nor issues another Present.
        case 1000613000:
            break;
        default:
            LOG_DEBUG("Vulkan overlay bypass: unsupported Present extension type={}", static_cast<int>(node->sType));
            return false;
        }
    }
    if (node)
        LOG_DEBUG("Vulkan overlay bypass: Present extension chain exceeds bounded traversal");
    return node == nullptr;
}

MenuOverlayVk::QueuePresentStatus MenuOverlayVk::QueuePresent(VkQueue queue, VkPresentInfoKHR* pPresentInfo,
                                                            const PresentLock& lock,
                                                            const DlssNr::VkObservedPresent* observed,
                                                            BoundaryContext boundary)
{
    LOG_FUNC();
    if (!lock.owns_lock() || lock.mutex() != &_vkOperationMutex)
        return QueuePresentStatus::Bypassed;

    MenuCommon::DeferInputCapture();
    OverlayFrameOutcome outcome;
    MaintainOverlayWaits(observed, boundary);

    // Protected by the operation guard. Log each refusal twice, then once per
    // 600 occurrences, so a provider transition exposes the failed gate without
    // turning ordinary per-frame bypasses into unbounded log traffic.
    const auto bypass = [&](unsigned gate, const char* reason) {
        static uint64_t counts[16] {};
        const auto count = ++counts[gate];
        if (count <= 2 || count % 600 == 0)
            LOG_INFO("Vulkan overlay bypass: reason={} count={} queue={} family={} flags=0x{:X} "
                     "queueObserved={} access={} acquired={} acquireGeneration={} boundary={} "
                     "swapchain={} rendererQueue={} rendererReady={} pending={}",
                     reason, count, reinterpret_cast<uintptr_t>(queue), observed ? observed->queueFamily : UINT32_MAX,
                     observed ? observed->capabilities.queueFlags : 0,
                     observed && observed->capabilities.queueObserved,
                     observed && observed->capabilities.presentQueueAccessQualified,
                     observed && observed->request.acquiredObserved,
                     observed ? observed->request.acquireGeneration : 0, boundary.generation,
                     observed ? reinterpret_cast<uintptr_t>(observed->swapchain) : 0,
                     reinterpret_cast<uintptr_t>(_ImVulkan_Info.Queue), _vulkanObjectsCreated, _vkCreationPending);
        return QueuePresentStatus::Bypassed;
    };
    if (_vkRestartRequired || _vkClosingRequested)
        return bypass(0, _vkRestartRequired ? Neurotic::UiLiteral("ingame.menu-overlay-vk.renderer_quarantined_58c33a8b", "renderer quarantined") : Neurotic::UiLiteral("ingame.menu-overlay-vk.renderer_closing_4bebec11", "renderer closing"));
    if (!observed)
        return bypass(1, Neurotic::UiLiteral("ingame.menu-overlay-vk.present_observation_unavailable_78c00600", "Present observation unavailable"));
    if (!pPresentInfo || pPresentInfo->swapchainCount != 1 || !pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices)
        return bypass(2, Neurotic::UiLiteral("ingame.menu-overlay-vk.single_swapchain_present_tuple_unavailable_11379b89", "single swapchain Present tuple unavailable"));
    if (observed->queue != queue || !observed->capabilities.queueObserved)
        return bypass(3, Neurotic::UiLiteral("ingame.menu-overlay-vk.present_queue_identity_unavailable_b36d2343", "Present queue identity unavailable"));
    if (!(observed->capabilities.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        return bypass(4, Neurotic::UiLiteral("ingame.menu-overlay-vk.present_queue_lacks_graphics_capability_85d58561", "Present queue lacks graphics capability"));
    if (!observed->capabilities.presentQueueAccessQualified)
        return bypass(5, Neurotic::UiLiteral("ingame.menu-overlay-vk.present_queue_image_access_unqualified_44d9f069", "Present queue image access unqualified"));
    if (observed->swapchain != pPresentInfo->pSwapchains[0] ||
        observed->request.imageIndex != pPresentInfo->pImageIndices[0])
        return bypass(6, Neurotic::UiLiteral("ingame.menu-overlay-vk.observed_image_differs_from_present_tuple_881c122d", "observed image differs from Present tuple"));
    if (!observed->request.acquiredObserved || !observed->request.acquireGeneration)
        return bypass(7, Neurotic::UiLiteral("ingame.menu-overlay-vk.image_acquisition_unobserved_406bb866", "image acquisition unobserved"));
    if (observed->request.flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)
        return bypass(8, Neurotic::UiLiteral("ingame.menu-overlay-vk.protected_swapchain_3fb9b45d", "protected swapchain"));
    if (!(observed->request.imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return bypass(9, Neurotic::UiLiteral("ingame.menu-overlay-vk.swapchain_lacks_color_attachment_usage_a4bfa7ee", "swapchain lacks color attachment usage"));
    if (!DlssNr::VkNrRecordingQueueContext(observed->device, observed->queueFamily, queue))
        return bypass(10, Neurotic::UiLiteral("ingame.menu-overlay-vk.recording_queue_context_unavailable_ce2cdd63", "recording queue context unavailable"));

    if (!OverlayPresentChainSupported(pPresentInfo->pNext))
    {
        // The outcome guard services CPU shortcuts and releases capture.
        // Unsupported metadata cannot admit renderer or GPU work.
        return bypass(11, Neurotic::UiLiteral("ingame.menu-overlay-vk.unsupported_present_extension_chain_507f00f5", "unsupported Present extension chain"));
    }

    if (_vulkanObjectsCreated && !_vkCreationPending && observed->device == _ImVulkan_Info.Device &&
        SameBoundary(boundary, _vkBoundary) &&
        observed->swapchain == _vkSwapchain && observed->request.deviceGeneration == _vkDeviceGeneration &&
        observed->request.swapchainGeneration == _vkSwapchainGeneration && queue != _ImVulkan_Info.Queue)
        _vkCreationPending = true;
    if (_vkCreationPending)
    {
        if (observed->device != _vkPendingCreate.device || observed->swapchain != _vkPendingCreate.swapchain ||
            observed->physicalDevice != _vkPendingCreate.physical || !SameBoundary(boundary, _vkPendingCreate.boundary))
            return bypass(12, Neurotic::UiLiteral("ingame.menu-overlay-vk.pending_renderer_boundary_differs_from_present_2842af86", "pending renderer boundary differs from Present"));
        if (std::chrono::steady_clock::now() < _vkNextCreateAttempt)
            return bypass(13, Neurotic::UiLiteral("ingame.menu-overlay-vk.renderer_creation_retry_pending_0b9c89fd", "renderer creation retry pending"));
        CreateVulkanObjects(_vkPendingCreate.device, _vkPendingCreate.physical, _vkPendingCreate.instance,
            _vkPendingCreate.window, &_vkPendingCreate.info, &_vkPendingCreate.swapchain, queue, observed->queueFamily,
            boundary);
        if (_vulkanObjectsCreated)
        {
            _vkCreationPending = false;
            _vkCreatingDevice = VK_NULL_HANDLE;
            if (_vkCreateFailures)
                LOG_INFO("Vulkan overlay creation recovered after {} attempts", _vkCreateFailures);
            _vkCreateFailures = 0;
            _vkNextCreateAttempt = {};
            _vkDeviceGeneration = observed->request.deviceGeneration;
            _vkSwapchainGeneration = observed->request.swapchainGeneration;
            _isInited = true;
            MenuOverlayBase::VulkanReady();
        }
        else
        {
            ++_vkCreateFailures;
            const auto delay = std::chrono::milliseconds(250u << std::min(_vkCreateFailures, 4u));
            _vkNextCreateAttempt = std::chrono::steady_clock::now() + delay;
            LOG_WARN("Vulkan overlay unavailable: creation attempt={} retryMs={}", _vkCreateFailures, delay.count());
        }
    }
    if (_vkClosingRequested || !_vulkanObjectsCreated || observed->device != _ImVulkan_Info.Device ||
        !SameBoundary(boundary, _vkBoundary) ||
        observed->request.deviceGeneration != _vkDeviceGeneration ||
        observed->request.swapchainGeneration != _vkSwapchainGeneration)
        return bypass(14, Neurotic::UiLiteral("ingame.menu-overlay-vk.renderer_ownership_or_generation_unavailable_b0d3ef56", "renderer ownership or generation unavailable"));

    if (!MenuOverlayBase::IsInited() || _ImVulkan_Info.Device == VK_NULL_HANDLE)
        return bypass(15, Neurotic::UiLiteral("ingame.menu-overlay-vk.ui_backend_unavailable_59ca1d45", "UI backend unavailable"));

    // The overlay command pool was created for its own queue family. A different Present queue
    // keeps the caller's waits and bypasses overlay submission.
    if (queue != _ImVulkan_Info.Queue)
    {
        static std::atomic<uint64_t> bypasses{0};const auto n=++bypasses;
        if(n<=8||n%600==0)LOG_INFO("Vulkan overlay queue bypass: presentQueue={} overlayQueue={} overlayFamily={} swapchains={} count={}",
            reinterpret_cast<uintptr_t>(queue),reinterpret_cast<uintptr_t>(_ImVulkan_Info.Queue),
            _ImVulkan_Info.QueueFamily,pPresentInfo?pPresentInfo->swapchainCount:0,n);
        return QueuePresentStatus::Bypassed;
    }

    if (pPresentInfo == nullptr || pPresentInfo->swapchainCount == 0 ||
        pPresentInfo->pImageIndices == nullptr || pPresentInfo->pSwapchains == nullptr)
        return QueuePresentStatus::FailedBeforeSubmit;

    // This global backend currently supports one known swapchain only.
    if (pPresentInfo->swapchainCount != 1 || pPresentInfo->pSwapchains[0] != _vkSwapchain)
        return QueuePresentStatus::Bypassed;
    if (_vkOverlaySubmissionUncertain)
        return QueuePresentStatus::SubmitUncertain;
    LOG_DEBUG("rendering menu, swapchain count: {0}", pPresentInfo->swapchainCount);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    _frameCount++;

    using namespace Neurotic::Semantic::Character;
    const auto& request=observed->request;
    const auto policy=CharacterCaptureVk::ColorPolicy(request.format,request.colorSpace);
    ExternalCharacterSource source;
    source.api=2;source.width=request.extent.width;source.height=request.extent.height;
    source.deviceIdentity=request.deviceGeneration;source.swapchainGeneration=request.swapchainGeneration;
    source.resizeGeneration=request.swapchainGeneration;source.colorPolicy=policy.value_or(0);
    source.srgbAttachment=request.format==VK_FORMAT_R8G8B8A8_SRGB||request.format==VK_FORMAT_B8G8R8A8_SRGB;
    source.fgActive=request.fgKnownActive||NativeFgWork().Read(CharacterActivityNow()).active;
    source.focused=OptiInput::IsFocused();
    source.qualified=_vkCharacterCapture.Ready()&&policy.has_value()&&observed->capabilities.swapchainPNextKnown&&
        (request.imageUsage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&request.arrayLayers==1;
    auto characterFrame=CharacterBeginExternalFrame(source,ReadSettings(*Config::Instance()));
    // Poll before any owner fence can be reset. Its completion covers the
    // capture and overlay commands in the same existing submission.
    if(auto completed=_vkCharacterCapture.Poll())CharacterPublishExternalFrame(std::move(*completed));

    {
        ImGui_ImplVulkan_NewFrame();

        MenuCommon::DeferInputCapture();
        outcome.cpuProcessed = true;
        if (MenuOverlayBase::RenderMenu())
        {
            // Finish the CPU frame even when a busy slot or missing proof skips
            // GPU drawing. Every successful RenderMenu starts an ImGui frame.
            MenuCommon::FinalizeFrame();
            // Physical Present owns this image and the overlay's completion
            // checks below. Do not wait for frame generation to be disabled.
            {
                uint32_t idx = pPresentInfo->pImageIndices[0];
                if (idx >= _scImageCount || _ImVulkan_Semaphores == nullptr || _ImVulkan_Frames == nullptr)
                    return QueuePresentStatus::FailedBeforeSubmit;
                const auto semaphoreIndex = idx;
                ImGui_ImplVulkanH_Frame* fd = &_ImVulkan_Frames[idx];
                if (fd->Backbuffer != observed->image)
                    return QueuePresentStatus::Bypassed;

                // Overlay work is optional. A busy render slot keeps the caller's
                // original waits and must not stall its Present indefinitely.
                const auto slotFenceResult = vkWaitForFences(_ImVulkan_Info.Device, 1, &fd->Fence, VK_TRUE, 0);
                if (slotFenceResult != VK_SUCCESS)
                {
                    outcome.transientBusy = slotFenceResult == VK_TIMEOUT || slotFenceResult == VK_NOT_READY;
                    return QueuePresentStatus::FailedBeforeSubmit;
                }
                // It may have signaled after the earlier poll. Consume that
                // exact completion before the shared fence is reset below.
                if(auto completed=_vkCharacterCapture.Poll())CharacterPublishExternalFrame(std::move(*completed));

                // Query only this explicitly pinned observation boundary. A
                // loader tuple and a semantic proxy cannot borrow one another's
                // acquire proof even if their numeric handles happen to match.
                auto& registry = boundary.registry ? *boundary.registry : DlssNr::GetVulkanPresentRegistry();
                auto proof = registry.AcquireProof(_vkSwapchain, idx, observed->request.acquireGeneration);
                RetainedOverlayWait previous {_ImVulkan_Info.Device, _ImVulkan_Semaphores[idx], _vkSwapchain,
                    _vkDeviceGeneration, _vkSwapchainGeneration, _vkSemaphoreAcquisition[idx], idx, boundary,
                    _vkSemaphorePresentSerial[idx], _ImVulkan_Info.Queue};
                if (proof && ProvesOverlayWait(previous, *proof))
                    previous.proof = std::move(proof);
                if (_vkSemaphorePending[idx] && !(previous.proof && previous.proof->Complete()))
                {
                    if (_vkRetainedWaits.size() + _scImageCount >= MaxRetainedOverlayWaits)
                    {
                        if (++_vkCapacityBypasses == 1 || _vkCapacityBypasses % 600 == 0)
                            LOG_WARN("Vulkan overlay bypass: unresolved Present wait budget exhausted count={}", _vkCapacityBypasses);
                        return QueuePresentStatus::Bypassed;
                    }
                    VkSemaphore next = VK_NULL_HANDLE;
                    VkSemaphoreCreateInfo info {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                    if (vkCreateSemaphore(_ImVulkan_Info.Device, &info, nullptr, &next) != VK_SUCCESS)
                        return QueuePresentStatus::FailedBeforeSubmit;
                    _vkRetainedWaits.push_back(std::move(previous));
                    _ImVulkan_Semaphores[idx] = next;
                    _vkSemaphorePending[idx] = false;
                }

                {
                    if (vkResetCommandPool(_ImVulkan_Info.Device, fd->CommandPool, 0) != VK_SUCCESS)
                        return QueuePresentStatus::FailedBeforeSubmit;
                    VkCommandBufferBeginInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    if (vkBeginCommandBuffer(fd->CommandBuffer, &info) != VK_SUCCESS)
                        return QueuePresentStatus::FailedBeforeSubmit;
                }

                CharacterRecordingOutcome characterRecording;
                if(characterFrame)
                    characterRecording.slot=_vkCharacterCapture.Record(fd->CommandBuffer,fd->Backbuffer,
                                                                        std::move(*characterFrame));

                {
                    VkRenderPassBeginInfo info = {};
                    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    info.renderPass = _vkRenderPass;
                    info.framebuffer = fd->Framebuffer;
                    info.renderArea.extent.width = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.x);
                    info.renderArea.extent.height = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.y);
                    vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
                }

                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);

                // Submit command buffer
                vkCmdEndRenderPass(fd->CommandBuffer);
                auto ecbResult = vkEndCommandBuffer(fd->CommandBuffer);
                if (ecbResult != VK_SUCCESS)
                {
                    LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) ecbResult);
                    return QueuePresentStatus::FailedBeforeSubmit;
                }

                // Submit queue and semaphores
                LOG_DEBUG("waitSemaphoreCount: {0}", pPresentInfo->waitSemaphoreCount);
                if (pPresentInfo->waitSemaphoreCount > 64 ||
                    (pPresentInfo->waitSemaphoreCount != 0 && pPresentInfo->pWaitSemaphores == nullptr))
                    return QueuePresentStatus::FailedBeforeSubmit;
                std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount,
                                                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

                VkSubmitInfo submit_info = {};
                submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers = &fd->CommandBuffer;
                submit_info.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
                submit_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
                submit_info.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
                submit_info.signalSemaphoreCount = 1;
                submit_info.pSignalSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];

                if (vkResetFences(_ImVulkan_Info.Device, 1, &fd->Fence) != VK_SUCCESS)
                    return QueuePresentStatus::FailedBeforeSubmit;
                _vkSemaphorePending[semaphoreIndex] = true;
                _vkSemaphoreAcquisition[semaphoreIndex] = observed->request.acquireGeneration;
                _vkSemaphorePresentSerial[semaphoreIndex] = 0;
                // Binding before dispatch conservatively retains the slot even
                // if submit returns an uncertain/device-lost result.
                _vkCharacterCapture.Submitted(characterRecording.slot,fd->Fence);
                auto qResult = vkQueueSubmit(queue, 1, &submit_info, fd->Fence);
                if (qResult != VK_SUCCESS)
                {
                    LOG_ERROR("vkQueueSubmit error: {0:X}", (UINT) qResult);
                    _vkOverlaySubmissionUncertain = true;
                    return QueuePresentStatus::SubmitUncertain;
                }

                pPresentInfo->waitSemaphoreCount = 1;
                pPresentInfo->pWaitSemaphores = &_ImVulkan_Semaphores[semaphoreIndex];
                _vkLastSubmittedImage = idx;
                outcome.submitted = true;
                return QueuePresentStatus::Accepted;
            }
        }
    }

    return QueuePresentStatus::Bypassed;
}

void MenuOverlayVk::GeneratedPresentationFinished(const PresentLock& lock, VkResult result)
{
    if (!lock.owns_lock()) return;
    // Successful/WSI-recovery returns have joined any generated GPU submission.
    // Other errors may leave that submission using ImGui's shared upload ring.
    // Quarantine it until the normal device-idle teardown proves completion.
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR &&
        result != VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT && result != VK_ERROR_SURFACE_LOST_KHR)
        _vkOverlaySubmissionUncertain = true;
}

VkResult MenuOverlayVk::CompositeGenerated(const PresentLock& lock, VkSwapchainKHR swapchain,
                                           VkCommandBuffer command, VkImage image, unsigned width, unsigned height)
{
    if (!lock.owns_lock() || swapchain != _vkSwapchain || !_ImVulkan_Frames || !_vkRenderPass ||
        !command || !image || width != static_cast<unsigned>(ImGui::GetIO().DisplaySize.x) ||
        height != static_cast<unsigned>(ImGui::GetIO().DisplaySize.y))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    ImGui_ImplVulkanH_Frame* target = nullptr;
    std::vector<VkFence> fences;
    for (unsigned i = 0; i < _scImageCount; ++i)
    {
        if (_ImVulkan_Frames[i].Backbuffer == image) target = &_ImVulkan_Frames[i];
        fences.push_back(_ImVulkan_Frames[i].Fence);
    }
    if (!target || fences.empty()) return VK_ERROR_FEATURE_NOT_PRESENT;
    // ImGui advances its shared upload-buffer ring on each RenderDrawData.
    // Finish previous overlay uploads before replaying; a busy backend skips
    // interpolation without consuming any new Present waits.
    const auto ready = vkWaitForFences(_ImVulkan_Info.Device, static_cast<uint32_t>(fences.size()),
                                       fences.data(), VK_TRUE, 5000000);
    if (ready != VK_SUCCESS) return ready;
    VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkRenderPassBeginInfo info {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = _vkRenderPass;
    info.framebuffer = target->Framebuffer;
    info.renderArea.extent = {width, height};
    vkCmdBeginRenderPass(command, &info, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRenderPass(command);
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    return VK_SUCCESS;
}

void MenuOverlayVk::CreateSwapchain(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                                    const VkSwapchainCreateInfoKHR* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain,
                                    BoundaryContext boundary)
{
    LOG_FUNC();

    std::unique_lock<std::mutex> lock(_vkOperationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        LOG_WARN("Vulkan overlay creation deferred: operation active");
        return;
    }

    if (!device || !pd || !pCreateInfo || !pSwapchain || !*pSwapchain)
        return;
    _vkClosingRequested = false;
    _vkCreateFailures = 0;
    _vkNextCreateAttempt = {};
    _vkCreatingDevice = device;
    _vkPendingCreate = {device, pd, instance, hwnd, *pSwapchain, *pCreateInfo};
    _vkPendingCreate.boundary = boundary;
    // Only these scalar creation facts are used by our renderer. Never retain
    // caller-owned extension/queue-family arrays past vkCreateSwapchainKHR.
    _vkPendingCreate.info.pNext = nullptr;
    _vkPendingCreate.info.pQueueFamilyIndices = nullptr;
    _vkPendingCreate.info.queueFamilyIndexCount = 0;
    _vkCreationPending = true;
}
