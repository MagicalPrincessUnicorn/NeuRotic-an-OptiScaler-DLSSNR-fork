#include "dlssnr/VulkanPresentGuides.h"
#include "dlssnr/VulkanNrPresentFrame.h"
#include <cstdio>
#include <cstdlib>
#if __has_include("dlssnr/VulkanNrPresentAssociation.h")
#include "dlssnr/VulkanNrPresentAssociation.h"
using namespace DlssNr;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    VkNrRecordingOwner owner;auto device=reinterpret_cast<VkDevice>(1);auto pool=reinterpret_cast<VkCommandPool>(2);
    auto cb=reinterpret_cast<VkCommandBuffer>(3);auto queue=reinterpret_cast<VkQueue>(4);
    owner.OnAllocate(device,pool,cb,0);owner.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
    auto image=[](uintptr_t h){NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;r.Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(h);return r;};
    VulkanPresentGuides guides(owner,[&](const VkFrameRequest&,VkNrUseId,uint64_t){auto p=std::make_shared<VkNrGuideImages>();p->depth=image(5);p->motion=image(6);p->bytes=64;return VkNrGuideCreation{p,true,true,{}};},[](VkNrGuideImages&){return true;});
    auto output=image(7);VkFrameRequest r;r.device=device;r.commandBuffer=cb;r.targetColor=&output;
    auto& c=r.contract;c.deviceGeneration=1;c.queueFamily=0;c.output=c.work={32,32};c.representation.format=VK_FORMAT_R16G16B16A16_SFLOAT;
    c.evaluation={cb,owner.Incarnation(cb),1,9};c.temporal=VkNrTemporalMetadata{};
    auto& t=*c.temporal;t.color=t.depth=t.motion={0,0,32,32};t.providerFrameKnown=true;t.providerGeneration=8;t.frameToken=42;t.viewport=1;t.featureGeneration=9;
    auto use=*owner.ReserveCopy(cb,1);Check(guides.Capture(r,use),"guide capture");guides.CompleteEvaluation(c.evaluation,true);
    owner.OnEnd(cb,VK_SUCCESS);auto submission=*owner.PrepareSubmission({queue,1,{use}});owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
    VkObservedPresent present;present.queue=queue;present.queueFamily=0;present.device=device;present.image=reinterpret_cast<VkImage>(8);present.swapchain=reinterpret_cast<VkSwapchainKHR>(9);
    present.request.deviceGeneration=1;present.request.swapchainGeneration=2;present.request.acquireGeneration=3;present.request.imageIndex=0;present.request.acquiredObserved=true;present.request.swapchainCount=1;
    present.request.extent={32,32};present.request.format=VK_FORMAT_A2B10G10R10_UNORM_PACK32;present.request.colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT;
    VkNrPresentFrames frames;frames.Provider(8);frames.Tags(8,42,1,true);frames.Start(8,42,100,true);
    auto frame=frames.Take(8,100);Check(frame.has_value(),"authentic current public frame");
    auto tag=MakeVkApplicationPresentTag(*frame,present);Check(tag.has_value(),"exact acquired final image accepts public marker association");
    Check(tag->image!=output.Resource.ImageViewInfo.Image,"final image intentionally differs from reconstructed image");
    Check(guides.ObservePresentTag(*tag),"marker association reaches real guide owner without Backbuffer tag");
    auto snapshot=guides.BeginPresent(queue);auto lease=guides.Select(tag->target,snapshot.get(),present.image);
    Check(lease&&lease->orderedProducer&&!lease->completedProducer,"matching frame supplies retained guides under actual same-queue submission order");
    auto wrong=*tag;++wrong.frameToken;guides.ObservePresentTag(wrong);Check(!guides.Select(tag->target,guides.BeginPresent(queue).get(),present.image),"different public frame cannot select old guides");
    auto invalid=present;invalid.request.acquiredObserved=false;Check(!MakeVkApplicationPresentTag(*frame,invalid),"unacquired final image refuses");
    invalid=present;invalid.request.swapchainCount=2;Check(!MakeVkApplicationPresentTag(*frame,invalid),"multiple outputs refuse association");
    invalid=present;invalid.request.acquireGeneration=0;Check(!MakeVkApplicationPresentTag(*frame,invalid),"unknown acquisition refuses");
    std::puts("Application final image receives exact submitted guides without optional Backbuffer tags");return 0;
}
#else
int main(){std::fputs("FAIL: no application Present-to-guide association\n",stderr);return 1;}
#endif
