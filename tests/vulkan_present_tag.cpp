#include "../OptiScaler/dlssnr/VulkanPresentGuides.h"
#include <sl_core_types.h>
#include <cstdio>
#include <cstdlib>
#if __has_include("../OptiScaler/dlssnr/VulkanNrPresentTag.h")
#include "../OptiScaler/dlssnr/VulkanNrPresentTag.h"
#define HAVE_TAG_ADAPTER 1
#endif
using namespace DlssNr;
static void Check(bool x,const char* why){if(!x){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 sl::Resource r{};r.type=sl::ResourceType::eTex2d;r.native=(void*)100;
 sl::ResourceTag t{};t.type=sl::kBufferTypeBackbuffer;t.resource=&r;t.lifecycle=sl::eValidUntilPresent;
 VkNrFrameContract expected;expected.deviceGeneration=1;expected.swapchainGeneration=2;
 expected.swapchain=(VkSwapchainKHR)20;expected.swapchainImageIndex=1;expected.acquireGeneration=3;
 expected.output={64,64};expected.representation.format=VK_FORMAT_R8G8B8A8_UNORM;
 auto resolver=[&](VkImage image,VkFormat format,VkExtent2D extent)->std::optional<VkNrFrameContract>{
  if(image!=(VkImage)100||(format!=VK_FORMAT_UNDEFINED&&format!=expected.representation.format)||
     (extent.width&&extent.width!=64)||(extent.height&&extent.height!=64))return {};return expected;};
#ifdef HAVE_TAG_ADAPTER
 auto result=ParseVkNrPresentTag(5,0,7,&t,1,true,resolver);
 Check(result.tag&&result.tag->frameToken==0&&result.tag->image==(VkImage)100,"explicit backbuffer tag admits exact acquired image with genuine frame zero");
 Check(!ParseVkNrPresentTag(5,0,7,&t,1,false,resolver).tag,"failed tagging cannot associate a frame");
 Check(!ParseVkNrPresentTag(0,0,7,&t,1,true,resolver).tag,"missing provider generation refuses");
 t.type=sl::kBufferTypeHUDLessColor;result=ParseVkNrPresentTag(5,0,7,&t,1,true,resolver);
 Check(!result.observed&&!result.tag,"HUDless is not silently reinterpreted as a backbuffer");t.type=sl::kBufferTypeBackbuffer;
 t.lifecycle=sl::eOnlyValidNow;Check(!ParseVkNrPresentTag(5,0,7,&t,1,true,resolver).tag,"tag lifetime must reach Present");
 t.lifecycle=sl::eValidUntilEvaluate;Check(!ParseVkNrPresentTag(5,0,7,&t,1,true,resolver).tag,"evaluation lifetime cannot authorize Present");t.lifecycle=sl::eValidUntilPresent;
 t.extent.left=1;Check(!ParseVkNrPresentTag(5,0,7,&t,1,true,resolver).tag,"partial backbuffer tag refuses");t.extent.left=0;
 r.width=65;Check(!ParseVkNrPresentTag(5,0,7,&t,1,true,resolver).tag,"explicit extent contradiction refuses");r.width=0;
 r.native=(void*)101;Check(!ParseVkNrPresentTag(5,0,7,&t,1,true,resolver).tag,"unobserved image refuses");r.native=(void*)100;
 auto tags=std::array{t,t};Check(!ParseVkNrPresentTag(5,0,7,tags.data(),2,true,resolver).tag,"duplicate backbuffer ambiguity refuses");
 t.resource=nullptr;result=ParseVkNrPresentTag(5,0,7,&t,1,true,resolver);Check(result.observed&&!result.tag,"null backbuffer revokes prior tags");
 Check(ParseVkNrPresentTag(5,0,7,nullptr,0,true,resolver).observed,"null tag array revokes viewport tags");
 Check(!ParseVkNrPresentTag(5,0,7,&t,65,true,resolver).tag,"oversized batch refuses before reading beyond bounds");
 std::puts("PASS public backbuffer tag validation, missing descriptors and explicit contradictions");
#else
 (void)resolver;Check(false,"public backbuffer tag adapter is missing");
#endif
}
