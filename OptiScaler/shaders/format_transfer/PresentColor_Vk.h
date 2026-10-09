#pragma once
#include <dlssnr/VulkanPresentColor.h>
#include <dlssnr/VulkanNrReservations.h>
namespace DlssNr {
class PresentColor_Vk {
    VkExtent2D extent_;VkDevice device_;VkDescriptorSetLayout setLayout_=VK_NULL_HANDLE;VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkPipeline decode_=VK_NULL_HANDLE,encode_=VK_NULL_HANDLE;
    struct Batch {uint32_t base=0;VkDescriptorPool pool=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;};
    std::vector<Batch> batches_;uint32_t capacity_=0;
    VkNrReservationPool slots_;
    bool Grow(uint32_t);
    bool Dispatch(VkCommandBuffer,const VkNrReservation&,uint32_t,VkPipeline,VkImageView,VkImageView,VkImageView);
  public:
    PresentColor_Vk(VkDevice,VkExtent2D);
    ~PresentColor_Vk();
    void AbandonDevice();
    bool IsInit() const {return decode_&&encode_;}
    std::optional<VkNrReservation> Reserve(VkNrUseId use) {return IsInit()?slots_.Reserve(use,2):std::nullopt;}
    bool Decode(VkCommandBuffer,const VkNrReservation&,VkImageView,VkImageView,const VkNrRepresentation&);
    bool Encode(VkCommandBuffer,const VkNrReservation&,VkImageView,VkImageView,VkImageView,const VkNrRepresentation&);
};
}
