#include "../OptiScaler/dlssnr/NativeFgQueues.h"
#include "../OptiScaler/dlssnr/NativeFgLayouts.h"
#include <cassert>
#include <cstdio>
using namespace DlssNr::NativeFg;
template<class T>T H(unsigned n){return reinterpret_cast<T>(static_cast<uintptr_t>(n));}
static unsigned creates=0,destroys=0;static bool failClone=false;
static VkResult VKAPI_CALL Pass(VkDevice,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks*,VkRenderPass* p){
 ++creates;if(creates%2==0){assert(ci->pAttachments[0].finalLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);if(failClone)return VK_ERROR_OUT_OF_HOST_MEMORY;}
 *p=H<VkRenderPass>(creates);return VK_SUCCESS;}
static void VKAPI_CALL Destroy(VkDevice,VkRenderPass,const VkAllocationCallbacks*){++destroys;}
static void VKAPI_CALL Barrier(VkCommandBuffer,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned count,const VkImageMemoryBarrier* b){
 assert(src==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT&&dst==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT&&count==1);
 assert(b[0].newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&(b[0].dstAccessMask&VK_ACCESS_SHADER_READ_BIT));}
static void VKAPI_CALL Wait(VkCommandBuffer,unsigned,const VkEvent*,VkPipelineStageFlags src,VkPipelineStageFlags dst,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned,const VkImageMemoryBarrier* b){
 assert(src==VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT&&dst==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 assert(b[0].srcAccessMask==VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT&&b[0].newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}
int main(){
 float priority=.5f;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueCount=1;q.pQueuePriorities=&priority;
 VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.queueCreateInfoCount=1;ci.pQueueCreateInfos=&q;
 std::vector<VkQueueFamilyProperties> families(1);families[0].queueCount=4;families[0].queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;
 QueuePreparation prepared(ci,families,true);auto changed=prepared.Apply(ci);
 assert(prepared.first==1&&prepared.family==0&&changed.pQueueCreateInfos[0].queueCount==4&&q.queueCount==1);
 assert(changed.pQueueCreateInfos[0].pQueuePriorities[0]==.5f&&changed.pQueueCreateInfos[0].pQueuePriorities[3]==1.f);
 families[0].queueCount=3;assert(QueuePreparation(ci,families,true).family==UINT32_MAX);families[0].queueCount=4;
 assert(QueuePreparation(ci,families,false).family==UINT32_MAX);
 VkDeviceQueueCreateInfo duplicate[2]={q,q};ci.queueCreateInfoCount=2;ci.pQueueCreateInfos=duplicate;assert(QueuePreparation(ci,families,true).family==UINT32_MAX);
 auto d=H<VkDevice>(1),other=H<VkDevice>(2);auto image=H<VkImage>(3);auto swap=H<VkSwapchainKHR>(4);auto view=H<VkImageView>(5);auto fb=H<VkFramebuffer>(6);
 Layouts::Register(d,swap,{&image,1});assert(Layouts::Translate(d,image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 assert(Layouts::Translate(other,image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 assert(Layouts::Translate(d,H<VkImage>(9),VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
 VkAttachmentDescription attachment{};attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};passInfo.attachmentCount=1;passInfo.pAttachments=&attachment;VkRenderPass pass{};
 assert(Layouts::CreatePass(Pass,Destroy,d,&passInfo,nullptr,&pass,true)==VK_SUCCESS&&creates==2);
 Layouts::View(d,view,image);VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};frame.attachmentCount=1;frame.pAttachments=&view;Layouts::Framebuffer(d,fb,frame);
 VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.framebuffer=fb;begin.renderPass=pass;
 assert(Layouts::Begin(d,begin).renderPass==H<VkRenderPass>(2)&&Layouts::Begin(other,begin).renderPass==pass);
 VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
 Layouts::Barrier(Barrier,d,nullptr,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 Layouts::Wait(Wait,d,nullptr,0,nullptr,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,nullptr,0,nullptr,1,&barrier);
 assert(barrier.newLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);Layouts::DestroyPass(Destroy,d,pass,nullptr);assert(destroys==2);
 failClone=true;assert(Layouts::CreatePass(Pass,Destroy,d,&passInfo,nullptr,&pass,true)==VK_ERROR_OUT_OF_HOST_MEMORY&&pass==VK_NULL_HANDLE&&destroys==3);
 Layouts::Forget(swap);Layouts::ForgetDevice(d);assert(!Layouts::active&&Layouts::views.empty()&&Layouts::frames.empty()&&Layouts::passes.empty());
 puts("PASS: private queue admission, layout isolation, compatible render passes, dependency translation and cleanup");
}
