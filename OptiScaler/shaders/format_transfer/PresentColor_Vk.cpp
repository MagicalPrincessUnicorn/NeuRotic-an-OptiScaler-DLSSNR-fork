#include "PresentColor_Vk.h"
#include "precompile/PresentColor_Shader_Vk.h"
#include <cstring>
namespace DlssNr {
PresentColor_Vk::PresentColor_Vk(VkDevice device,VkExtent2D extent):extent_(extent),device_(device),slots_(VulkanNrRecordings(),[this](uint32_t n){return Grow(n);}) {
    VkDescriptorSetLayoutBinding bindings[3]{};
    for(uint32_t i=0;i<3;++i){bindings[i].binding=i;bindings[i].descriptorType=i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=3;set.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device_,&set,nullptr,&setLayout_)!=VK_SUCCESS)return;
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&setLayout_;
    if(vkCreatePipelineLayout(device_,&layout,nullptr,&layout_)!=VK_SUCCESS)return;
    const auto create=[&](const unsigned char* bytes,size_t size,const char* entry,VkPipeline& pipeline){
        std::vector<uint32_t> aligned(size/4);std::memcpy(aligned.data(),bytes,size);
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=size;module.pCode=aligned.data();VkShaderModule shader{};
        if(vkCreateShaderModule(device_,&module,nullptr,&shader)!=VK_SUCCESS)return;
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=layout_;
        info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;info.stage.module=shader;info.stage.pName=entry;
        vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&pipeline);vkDestroyShaderModule(device_,shader,nullptr);
    };
    create(PresentColorDecode_spv,sizeof(PresentColorDecode_spv),"Decode",decode_);
    create(PresentColorEncode_spv,sizeof(PresentColorEncode_spv),"Encode",encode_);
}
PresentColor_Vk::~PresentColor_Vk(){
    for(auto& batch:batches_)vkDestroyDescriptorPool(device_,batch.pool,nullptr);
    if(decode_)vkDestroyPipeline(device_,decode_,nullptr);if(encode_)vkDestroyPipeline(device_,encode_,nullptr);
    if(layout_)vkDestroyPipelineLayout(device_,layout_,nullptr);if(setLayout_)vkDestroyDescriptorSetLayout(device_,setLayout_,nullptr);
}
void PresentColor_Vk::AbandonDevice(){
    batches_.clear();decode_=encode_=VK_NULL_HANDLE;layout_=VK_NULL_HANDLE;
    setLayout_=VK_NULL_HANDLE;device_=VK_NULL_HANDLE;
}
bool PresentColor_Vk::Grow(uint32_t capacity){
    const auto count=capacity-capacity_;Batch batch;batch.base=capacity_;
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count*2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,count}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=count;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(device_,&pool,nullptr,&batch.pool)!=VK_SUCCESS)return false;
    std::vector<VkDescriptorSetLayout> layouts(count,setLayout_);batch.sets.resize(count);
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=batch.pool;
    allocate.descriptorSetCount=count;allocate.pSetLayouts=layouts.data();
    if(vkAllocateDescriptorSets(device_,&allocate,batch.sets.data())!=VK_SUCCESS){vkDestroyDescriptorPool(device_,batch.pool,nullptr);return false;}
    batches_.push_back(std::move(batch));capacity_=capacity;return true;
}
bool PresentColor_Vk::Dispatch(VkCommandBuffer command,const VkNrReservation& reservation,uint32_t index,
    VkPipeline pipeline,VkImageView source,VkImageView original,VkImageView target){
    if(!command||!source||!original||!target||reservation.slots.size()!=2||!slots_.Consume(reservation.use,reservation.slots[index]))return false;
    const auto slot=reservation.slots[index];VkDescriptorSet set=VK_NULL_HANDLE;
    for(const auto& batch:batches_)if(slot>=batch.base&&slot-batch.base<batch.sets.size()){set=batch.sets[slot-batch.base];break;}
    if(!set)return false;
    VkDescriptorImageInfo images[3]={{VK_NULL_HANDLE,source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE,original,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,target,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[3]{};
    for(uint32_t i=0;i<3;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;
        writes[i].descriptorCount=1;writes[i].descriptorType=i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[i].pImageInfo=&images[i];}
    vkUpdateDescriptorSets(device_,3,writes,0,nullptr);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout_,0,1,&set,0,nullptr);
    vkCmdDispatch(command,(extent_.width+15)/16,(extent_.height+15)/16,1);
    return true;
}
bool PresentColor_Vk::Decode(VkCommandBuffer command,const VkNrReservation& slots,VkImageView source,VkImageView target,const VkNrRepresentation& representation){
    if(SelectVkPresentColorRecipe(representation.format,representation.colorSpace).revision!=2||!Dispatch(command,slots,0,decode_,source,source,target))return false;
    return true;
}
bool PresentColor_Vk::Encode(VkCommandBuffer command,const VkNrReservation& slots,VkImageView original,VkImageView source,VkImageView target,const VkNrRepresentation& representation){
    if(SelectVkPresentColorRecipe(representation.format,representation.colorSpace).revision!=2||!Dispatch(command,slots,1,encode_,source,original,target))return false;
    return true;
}
}
