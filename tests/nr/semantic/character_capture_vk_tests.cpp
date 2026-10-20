#include "nr/semantic/character/CharacterCaptureVk.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace Neurotic::Semantic::Character;
static void Require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static void Check(VkResult value,const char* what){Require(value==VK_SUCCESS,what);}
struct Host {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{};
    VkQueue queue{}; unsigned family=0; VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{};
    Host(){
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;
        Check(vkCreateInstance(&ic,nullptr,&instance),"create instance");
        unsigned n=0;Check(vkEnumeratePhysicalDevices(instance,&n,nullptr),"device count");Require(n!=0,"no Vulkan devices");
        std::vector<VkPhysicalDevice> ps(n);Check(vkEnumeratePhysicalDevices(instance,&n,ps.data()),"devices");physical=ps[0];
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,qs.data());
        for(;family<n;++family)if(qs[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;Require(family<n,"no graphics queue");
        float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;
        Check(vkCreateDevice(physical,&dc,nullptr,&device),"create device");vkGetDeviceQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(device,&pc,nullptr,&pool),"pool");VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
        Check(vkAllocateCommandBuffers(device,&ca,&command),"command");VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Check(vkCreateFence(device,&fc,nullptr,&fence),"fence");
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::printf("GPU %s\n",props.deviceName);
    }
    ~Host(){if(device){vkDeviceWaitIdle(device);vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);}if(instance)vkDestroyInstance(instance,nullptr);}
    unsigned Memory(unsigned bits){VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);for(unsigned i=0;i<p.memoryTypeCount;++i)if(bits&(1u<<i))return i;throw std::runtime_error("memory type");}
    void Begin(){Check(vkResetCommandBuffer(command,0),"reset command");Check(vkResetFences(device,1,&fence),"reset fence");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(command,&b),"begin");}
    void Submit(){Check(vkEndCommandBuffer(command),"end");VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&command;Check(vkQueueSubmit(queue,1,&s,fence),"submit");}
    void Wait(){Check(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull),"fixture bounded wait");}
};
struct Source {
    Host& h;VkImage image{};VkDeviceMemory memory{};
    Source(Host& host,VkFormat format):h(host){VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;c.format=format;c.extent={1920,1080,1};c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.tiling=VK_IMAGE_TILING_OPTIMAL;c.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        Check(vkCreateImage(h.device,&c,nullptr,&image),"source image");VkMemoryRequirements m{};vkGetImageMemoryRequirements(h.device,image,&m);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=m.size;a.memoryTypeIndex=h.Memory(m.memoryTypeBits);Check(vkAllocateMemory(h.device,&a,nullptr,&memory),"source memory");Check(vkBindImageMemory(h.device,image,memory,0),"source bind");}
    ~Source(){vkDestroyImage(h.device,image,nullptr);vkFreeMemory(h.device,memory,nullptr);}
    void Fill(float r,float g,float b,bool first=false){VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};v.image=image;v.oldLayout=first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL;v.newLayout=VK_IMAGE_LAYOUT_GENERAL;v.srcQueueFamilyIndex=v.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};v.srcAccessMask=first?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;v.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(h.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&v);VkClearColorValue color{{r,g,b,1}};vkCmdClearColorImage(h.command,image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&v.subresourceRange);}
};
static void Pixels(Host& h,VkFormat format,VkColorSpaceKHR color,float r,float g,float b,unsigned red,unsigned green,unsigned blue){
    CharacterCaptureVk capture;Require(capture.Initialize(h.physical,h.device,format,color,{1920,1080}),"supported source initializes");
    Source source(h,format);h.Begin();source.Fill(r,g,b,true);CpuFrame meta;meta.key.sequence=42;meta.key.captureNs=123;meta.colorPolicy=*CharacterCaptureVk::ColorPolicy(format,color);
    const auto slot=capture.Record(h.command,source.image,meta,VK_IMAGE_LAYOUT_GENERAL);Require(slot>=0,"record pre-overlay capture");Require(!capture.Poll(),"unsubmitted capture is unreadable");
    source.Fill(0,0,1);h.Submit();capture.Submitted(slot,h.fence);h.Wait();auto frame=capture.Poll();Require(frame.has_value(),"submitted fence makes pixels available");Require(ValidCharacterVkPixels(*frame),"bounded native pixel contract");Require(ConvertCharacterVkPixels(*frame),"worker pixel conversion");Require(frame->width==960&&frame->height==540&&frame->pixels.size()==960*540*4,"GPU bounded thumbnail");Require(frame->key.sequence==42,"frame identity retained");
    for(size_t p=0;p<frame->pixels.size();p+=4){Require(std::abs(int(frame->pixels[p])-int(red))<=3,"red value");Require(std::abs(int(frame->pixels[p+1])-int(green))<=3,"green value");Require(std::abs(int(frame->pixels[p+2])-int(blue))<=3,"blue value");Require(frame->pixels[p+3]==255,"opaque alpha");}
    Require(!capture.Poll(),"capture consumed once");capture.ReleaseAfterIdle();Require(!capture.Ready(),"idle release closes capture");std::printf("PASS format=%d color=%d pre-overlay pixels and bounded downsample\n",int(format),int(color));
}
static void Slots(Host& h){
    CharacterCaptureVk capture;Require(capture.Initialize(h.physical,h.device,VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,{1920,1080}),"slot setup");
    Source source(h,VK_FORMAT_R8G8B8A8_UNORM);h.Begin();source.Fill(1,0,0,true);
    CpuFrame meta;meta.key.captureNs=1;int slots[3];
    for(unsigned i=0;i<3;++i){meta.key.sequence=i+1;slots[i]=capture.Record(h.command,source.image,meta,VK_IMAGE_LAYOUT_GENERAL);Require(slots[i]>=0,"free ring slot");}
    Require(capture.Record(h.command,source.image,meta,VK_IMAGE_LAYOUT_GENERAL)<0,"full ring refuses without overwrite");
    Require(!capture.Initialize(h.physical,h.device,VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,{1280,720}),"live generation cannot be replaced");
    for(int slot:slots){capture.Submitted(slot,h.fence);capture.Cancel(slot);}
    Require(!capture.Poll(),"unsignaled completion cannot publish");
    h.Submit();h.Wait();auto newest=capture.Poll();Require(newest&&newest->key.sequence==3,"only latest completed raw thumbnail copied");
    Require(ValidCharacterVkPixels(*newest),"raw latest contract");Require(!capture.Poll(),"older completed images discarded");
    h.Begin();meta.key.sequence=4;const auto cancelled=capture.Record(h.command,source.image,meta,VK_IMAGE_LAYOUT_GENERAL);Require(cancelled>=0,"retired ring reusable");
    capture.Cancel(cancelled);Require(!capture.Poll(),"cancelled record cannot publish");Check(vkEndCommandBuffer(h.command),"close abandoned recording");Check(vkResetCommandBuffer(h.command,0),"discard abandoned recording");
    capture.ReleaseAfterIdle();
    CpuFrame malformed;malformed.externalVkFormat=VK_FORMAT_R16G16B16A16_SFLOAT;malformed.colorPolicy=2;malformed.width=malformed.height=1;malformed.stride=4;malformed.pixels.resize(4);
    Require(!ValidCharacterVkPixels(malformed)&&!ConvertCharacterVkPixels(malformed),"truncated half-float rejected");
    malformed.stride=8;malformed.pixels.resize(8);Require(ValidCharacterVkPixels(malformed),"scRGB raw stride");malformed.colorPolicy=0;Require(!ValidCharacterVkPixels(malformed),"format and policy mismatch rejected");
    std::puts("PASS bounded ring, no premature publication, latest-only retirement, cancel and pixel validation");
}
int main(){try{
    Require(CharacterCaptureVk::ColorPolicy(VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)==0u,"SDR format admission");
    Require(!CharacterCaptureVk::ColorPolicy(VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_HDR10_HLG_EXT),"unsupported HLG refused");
    Require(CharacterVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true,0)==(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT),"supported precreate usage added");
    Require(CharacterVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,0,true,0)==VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,"unsupported surface unchanged");
    Require(CharacterVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,false,0)==VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,"unknown pNext chain unchanged");
    Require(CharacterVkSwapchainUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true,VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)==VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,"protected source unchanged");
    Host h;
    Pixels(h,VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1,0,0,255,0,0);
    Pixels(h,VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1,0.5f,0,255,128,0);
    Pixels(h,VK_FORMAT_R8G8B8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1,0,0,255,0,0);
    Pixels(h,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,0,1,0,0,255,0);
    Pixels(h,VK_FORMAT_A2R10G10B10_UNORM_PACK32,VK_COLOR_SPACE_HDR10_ST2084_EXT,0.580689f,0.580689f,0.580689f,186,186,186);
    Pixels(h,VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,2.5375f,2.5375f,2.5375f,186,186,186);
    Slots(h);
    std::puts("PASS Vulkan Inspector GPU capture");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
