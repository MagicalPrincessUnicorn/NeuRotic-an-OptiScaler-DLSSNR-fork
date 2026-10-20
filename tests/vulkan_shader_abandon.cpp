#define NOMINMAX
#include "fixtures/vulkan_nr_gpu_host.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_Vk.h"
#include "../OptiScaler/shaders/format_transfer/PresentColor_Vk.h"
#include <stdexcept>

int main(){try{
    std::unique_ptr<DlssNr_Vk> composition;
    std::unique_ptr<DlssNr::PresentColor_Vk> color;
    {
        VkGpuHost host;
        composition=std::make_unique<DlssNr_Vk>("shutdown partial-record fixture",host.device,host.physical,host.storageWriteWithoutFormatEnabled);
        color=std::make_unique<DlssNr::PresentColor_Vk>(host.device,host.extent);
        if(!composition->CanRender()||!color->IsInit())throw std::runtime_error("actual shader allocation");
        host.Begin();
        if(!composition->Reserve(host.use,3)||!color->Reserve(host.use))throw std::runtime_error("actual descriptor reservations");
        if(composition->DrainReleased())throw std::runtime_error("partial recording must protect shared pass");
        // No model or dispatch was created. Actual reset ends this partial recording.
        VkGpuCheck(vkResetCommandBuffer(host.command,0),"partial reset");
        DlssNr::VulkanNrRecordings().OnReset(host.command,VK_SUCCESS);
        if(!composition->DrainReleased())throw std::runtime_error("actual reset must drain partial reservations");
        // GPU handles must be explicitly freed BEFORE VkDevice destruction. Validate
        // abandonment against the now-dead handle without making driver calls below.
        // The test uses a separate owned device solely for this exceptional path.
    }
    composition->AbandonDevice();color->AbandonDevice();
    composition->AbandonDevice();color->AbandonDevice();
    if(composition->CanRender()||color->IsInit())throw std::runtime_error("abandoned wrappers cannot render");
    composition.reset();color.reset();
    std::puts("PASS actual Vulkan shader CPU wrappers destruct after device destruction; partial reservations drained");
    return 0;
}catch(const VkGpuUnavailable& e){std::printf("UNAVAILABLE: %s\n",e.what());return 2;}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
