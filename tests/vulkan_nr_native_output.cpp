#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
static unsigned barriers=0,dispatches=0;
static VkPipelineStageFlags sourceStages{},destinationStages{};
static VkImageMemoryBarrier captured{};
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,
 VkDependencyFlags,uint32_t memoryCount,const VkMemoryBarrier*,uint32_t bufferCount,const VkBufferMemoryBarrier*,uint32_t imageCount,const VkImageMemoryBarrier* images){
 Check(dispatches==1,"export follows actual successful dispatch");
 Check(memoryCount==0&&bufferCount==0&&imageCount==1,"export is restricted to the written image");
 ++barriers;sourceStages=source;destinationStages=destination;captured=images[0];
}
#if __has_include("dlssnr/VulkanNrNativeOutput.h")
#include "dlssnr/VulkanNrNativeOutput.h"
#endif
namespace DlssNr {
enum class VkNrRoute { Native, Present };
struct Resource {struct {struct {VkImage Image{};VkImageView ImageView{};VkImageSubresourceRange SubresourceRange{};} ImageViewInfo;} Resource;};
struct Pass {bool succeeds=true;template<class... T> bool Dispatch(T...){++dispatches;return succeeds;}};
struct Base {bool captureOnly=false,ownedPresentInputs=false,finalColorOnly=false;VkNrRoute route=VkNrRoute::Native;VkCommandBuffer commandBuffer{};unsigned use=1,width=32,height=8;};
static bool Deliver(Base base,bool before,bool succeeds,Resource* target){
 Pass pass;pass.succeeds=succeeds;
 struct Resources {struct {VkImageView view{};} keep;} resources;
 struct {Pass* pass;Resources* resources;} root{&pass,&resources};
 struct {VkImageView diagnosticProxy{},diagnosticAnswer{};} last;
 Resource output;struct {Resource* output;} final{&output};
 const bool diagnostic=false;const unsigned slot=0,display=6;
 #include "native_output_actual.inc"
 return delivered;
}
}
int main(){using namespace DlssNr;
 Resource target;target.Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(0x1234);
 target.Resource.ImageViewInfo.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,2,1,3,1};
 Base base;Check(Deliver(base,false,true,&target),"Native delivery succeeds");
 Check(barriers==1,"successful final Native write exports graphics and transfer visibility");
 Check(sourceStages==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&destinationStages==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,"export covers later graphics and transfer stages");
 Check(captured.srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&captured.dstAccessMask==(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT),"export includes read and overwrite hazards");
 Check(captured.image==target.Resource.ImageViewInfo.Image&&captured.subresourceRange.baseMipLevel==2&&captured.subresourceRange.levelCount==1&&captured.subresourceRange.baseArrayLayer==3&&captured.subresourceRange.layerCount==1,"export covers the actual target view range");
 Check(captured.oldLayout==VK_IMAGE_LAYOUT_GENERAL&&captured.newLayout==VK_IMAGE_LAYOUT_GENERAL&&captured.srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&captured.dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED,"export preserves layout and queue ownership");
 for(unsigned scenario=0;scenario<5;++scenario){base={};bool before=false,succeeds=true;
  if(scenario==0)succeeds=false;if(scenario==1)before=true;if(scenario==2)base.captureOnly=true;if(scenario==3)base.finalColorOnly=true;if(scenario==4)base.route=VkNrRoute::Present;
  barriers=dispatches=0;Deliver(base,before,succeeds,&target);Check(barriers==0,"failed, skipped, private or Present delivery does not export Native output");
 }
 std::puts("PASS actual final Native delivery publishes target graphics/transfer visibility; failed/skipped/private/Present paths do not");
}
