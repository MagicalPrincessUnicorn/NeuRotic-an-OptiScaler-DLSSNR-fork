// Included inside the guide owner's anonymous namespace. Programs are cached per
// device generation; each recording owns immutable descriptors and constants.
struct GuideCopyProgram
{
    VkDevice device=VK_NULL_HANDLE;
    uint64_t generation=0;
    VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
    std::array<VkPipeline,2> pipeline{};
    void Abandon() {device=VK_NULL_HANDLE;descriptorLayout=VK_NULL_HANDLE;pipelineLayout=VK_NULL_HANDLE;pipeline={};}
    ~GuideCopyProgram() {
        if(!device)return;
        for(auto p:pipeline)if(p)vkDestroyPipeline(device,p,nullptr);
        if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
        if(descriptorLayout)vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);
    }
};
std::unordered_map<VkDevice,std::shared_ptr<GuideCopyProgram>> guideCopyPrograms;
std::shared_ptr<GuideCopyProgram> GetGuideCopyProgram(VkDevice device,uint64_t generation)
{
    const auto found=guideCopyPrograms.find(device);
    if(found!=guideCopyPrograms.end()) {
        if(found->second->generation!=generation)return {};
        return found->second;
    }
    auto p=std::make_shared<GuideCopyProgram>();p->device=device;p->generation=generation;
    const std::array<VkDescriptorSetLayoutBinding,3> bindings{{
        {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount=static_cast<uint32_t>(bindings.size());descriptor.pBindings=bindings.data();
    if(vkCreateDescriptorSetLayout(device,&descriptor,nullptr,&p->descriptorLayout)!=VK_SUCCESS)return {};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1;layout.pSetLayouts=&p->descriptorLayout;
    if(vkCreatePipelineLayout(device,&layout,nullptr,&p->pipelineLayout)!=VK_SUCCESS)return {};
    const std::array<const unsigned char*,2> code{GuideDepthCopy_spv,GuideMotionCopy_spv};
    const std::array<size_t,2> sizes{sizeof(GuideDepthCopy_spv),sizeof(GuideMotionCopy_spv)};
    const std::array<const char*,2> entry{"CopyDepth","CopyMotion"};
    for(size_t i=0;i<2;++i) {
        // SPIR-V pCode needs uint32_t alignment regardless of embedded byte alignment.
        std::vector<uint32_t> aligned(sizes[i]/sizeof(uint32_t));memcpy(aligned.data(),code[i],sizes[i]);
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize=sizes[i];shader.pCode=aligned.data();VkShaderModule module=VK_NULL_HANDLE;
        if(vkCreateShaderModule(device,&shader,nullptr,&module)!=VK_SUCCESS)return {};
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.layout=p->pipelineLayout;compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=module;compute.stage.pName=entry[i];
        const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&p->pipeline[i]);
        vkDestroyShaderModule(device,module,nullptr);
        if(result!=VK_SUCCESS)return {};
    }
    guideCopyPrograms.emplace(device,p);return p;
}
struct GuideCopyBindings
{
    std::shared_ptr<GuideCopyProgram> program;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    std::array<VkDescriptorSet,2> set{};
    std::array<VkBuffer,2> buffer{};
    std::array<VkDeviceMemory,2> memory{};
    void Release(VkDevice device) {
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        for(size_t i=0;i<2;++i) {
            if(buffer[i])vkDestroyBuffer(device,buffer[i],nullptr);
            if(memory[i])vkFreeMemory(device,memory[i],nullptr);
        }
        pool=VK_NULL_HANDLE;set={};buffer={};memory={};program.reset();
    }
    void Abandon() {
        program.reset();pool=VK_NULL_HANDLE;set={};buffer={};memory={};
    }
};
bool PrepareGuideCopyBindings(GuideCopyBindings& b,VkDevice device,VkPhysicalDevice physical,uint64_t generation,
    const std::array<NVSDK_NGX_Resource_VK*,2>& source,const std::array<VkImageView,2>& destination,
    const std::array<VkImageLayout,2>& layouts,const std::array<VkNrRect,2>& rects,uint64_t& budget,uint64_t& bytes)
{
    b.program=GetGuideCopyProgram(device,generation);if(!b.program)return false;
    const std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=2;
    pool.poolSizeCount=static_cast<uint32_t>(sizes.size());pool.pPoolSizes=sizes.data();
    if(vkCreateDescriptorPool(device,&pool,nullptr,&b.pool)!=VK_SUCCESS)return false;
    const std::array<VkDescriptorSetLayout,2> setLayouts{b.program->descriptorLayout,b.program->descriptorLayout};
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sets.descriptorPool=b.pool;
    sets.descriptorSetCount=2;sets.pSetLayouts=setLayouts.data();
    if(vkAllocateDescriptorSets(device,&sets,b.set.data())!=VK_SUCCESS)return false;
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    for(size_t i=0;i<2;++i) {
        const std::array<uint32_t,4> rectangle{rects[i].x,rects[i].y,rects[i].width,rects[i].height};
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=sizeof(rectangle);
        buffer.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(vkCreateBuffer(device,&buffer,nullptr,&b.buffer[i])!=VK_SUCCESS)return false;
        VkMemoryRequirements needs{};vkGetBufferMemoryRequirements(device,b.buffer[i],&needs);
        if(needs.size>budget)return false;
        uint32_t type=UINT32_MAX;
        for(uint32_t j=0;j<memory.memoryTypeCount;++j)if((needs.memoryTypeBits&(1u<<j))&&
            (memory.memoryTypes[j].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=j;break;}
        if(type==UINT32_MAX)return false;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=needs.size;allocation.memoryTypeIndex=type;
        if(vkAllocateMemory(device,&allocation,nullptr,&b.memory[i])!=VK_SUCCESS||
           vkBindBufferMemory(device,b.buffer[i],b.memory[i],0)!=VK_SUCCESS)return false;
        void* mapped=nullptr;if(vkMapMemory(device,b.memory[i],0,sizeof(rectangle),0,&mapped)!=VK_SUCCESS)return false;
        memcpy(mapped,rectangle.data(),sizeof(rectangle));vkUnmapMemory(device,b.memory[i]);
        budget-=needs.size;bytes+=needs.size;
        const std::array<VkDescriptorImageInfo,2> images{{{VK_NULL_HANDLE,source[i]->Resource.ImageViewInfo.ImageView,layouts[i]},
            {VK_NULL_HANDLE,destination[i],VK_IMAGE_LAYOUT_GENERAL}}};
        const VkDescriptorBufferInfo constant{b.buffer[i],0,sizeof(rectangle)};
        std::array<VkWriteDescriptorSet,3> writes{};
        for(uint32_t j=0;j<3;++j){writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[j].dstSet=b.set[i];
            writes[j].dstBinding=j;writes[j].descriptorCount=1;writes[j].descriptorType=sizes[j].type;}
        writes[0].pImageInfo=&images[0];writes[1].pImageInfo=&images[1];writes[2].pBufferInfo=&constant;
        vkUpdateDescriptorSets(device,static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);
    }
    return true;
}
