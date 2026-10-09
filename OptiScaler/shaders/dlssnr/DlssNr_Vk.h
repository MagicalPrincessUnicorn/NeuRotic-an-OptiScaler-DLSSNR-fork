#pragma once

// The composition pass on Vulkan.
//
// Same shader as the D3D12 pass, compiled to SPIR-V from the same source: the modes, the white point,
// the proxy encode and the transfer back are all shared, and any behavioural difference between the
// two APIs would be a bug rather than a design. What differs is only how a compute dispatch is
// expressed.
//
// Three things are worth knowing before reading the implementation.
//
// Every binding is written every dispatch. The shader declares all seven resources at file scope and
// branches on gMode, so all of them are statically reachable from the entry point and Vulkan requires
// a valid descriptor for each one whether a given mode reads it or not. Slots a mode has no use for
// get a 1x1 dummy rather than a null handle.
//
// Constants are slotted rather than overwritten. A single mapped uniform buffer would be wrong here:
// encode and resolve run in the same frame with different constants, and the second write would land
// before the first dispatch had read it. Each use reserves immutable slots until its GPU work and
// recording references are released. Physical chunks follow the device's uniform-buffer alignment.
//
// Layouts are the caller's to declare and this pass's to respect. It never guesses what state an
// image arrived in.

#ifndef NR_VK_GPU_FIXTURE
#include "SysUtils.h"
#endif
#include <shaders/Shader_Vk.h>
#include "DlssNr_Common.h"
#include <dlssnr/VulkanNrReservations.h>
#include <memory>

class DlssNr_Vk : public Shader_Vk
{
    VkDeviceSize _slotStride = 0;   // sizeof(DlssNrConstants), rounded up to the device's alignment
    struct Batch
    {
        uint32_t base = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
    };
    std::vector<std::unique_ptr<Batch>> _batches;
    uint32_t _capacity = 0;
    DlssNr::VkNrReservationPool _reservations;
    bool EnsureCapacity(uint32_t capacity);
    void DestroyBatch(Batch& batch);

    // Stands in for a resource a given mode does not read. One pixel, never sampled for its content,
    // present only because Vulkan will not accept an unwritten binding.
    VkImage _dummyImage = VK_NULL_HANDLE;
    VkDeviceMemory _dummyMemory = VK_NULL_HANDLE;
    VkImageView _dummyView = VK_NULL_HANDLE;
    bool _dummyReady = false;
    bool _dummyCommitted = false;
    DlssNr::VkNrUseId _dummyUse;

    bool CreateDummy(VkCommandBuffer cmdList, DlssNr::VkNrUseId use);
    bool CanUseDummy(DlssNr::VkNrUseId use);

    void WriteDescriptors(VkDescriptorSet set, VkBuffer constants, VkDeviceSize constantOffset, VkImageView source, VkImageView model,
                          VkImageView original, VkImageView motion, VkImageView target, VkImageView keep,
                          VkImageLayout sourceLayout, VkImageLayout motionLayout);

  public:
    DlssNr_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
              bool storageWriteWithoutFormatEnabled);
    ~DlssNr_Vk();
    void AbandonDevice() override;
    bool DrainReleased() { return _reservations.DrainReleased() && DlssNr::VulkanNrRecordings().Reusable(_dummyUse); }
    std::optional<DlssNr::VkNrReservation> Reserve(DlssNr::VkNrUseId use,uint32_t demand)
    { return CanUseDummy(use) ? _reservations.Reserve(use,demand) : std::nullopt; }

    // One dispatch of the composition shader.
    //
    // Any of the four read views may be VK_NULL_HANDLE, in which case the dummy is bound; the two
    // written views may not, because a mode that writes nothing has no reason to run. This records
    // the dispatch and the barrier that follows it, not the transitions that got them there.
    //
    // The source and motion slots are the two that ever carry an image this pass does not own -- the
    // frame the upscaler wrote, and the game's exposure -- and a descriptor has to name the layout
    // its image will be in when the shader runs. Those two are therefore the caller's to state.
    // Everything else is ours and is in the layout this pass put it in.
    //
    // The default is what a resource read by a compute shader is normally in, and is what every slot
    // holding one of our own images uses.
    bool Dispatch(VkCommandBuffer InCmdList, DlssNr::VkNrUseId use, uint32_t slot,
                  const DlssNrConstants& InConstants, uint32_t InThreadsX,
                  uint32_t InThreadsY, VkImageView InSource, VkImageView InModel, VkImageView InOriginal,
                  VkImageView InMotion, VkImageView InTarget, VkImageView InKeep,
                  VkImageLayout InSourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VkImageLayout InMotionLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};
