#include "dlssnr/VulkanNrRecording.h"
#include <cstdio>
#include <cstdlib>
#if __has_include("dlssnr/VulkanPresentConsumer.h")
#include "dlssnr/VulkanPresentConsumer.h"
using namespace DlssNr;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 VkNrRecordingOwner owner; VkPresentConsumers consumers(owner);
 auto device=reinterpret_cast<VkDevice>(1);auto semaphore=reinterpret_cast<VkSemaphore>(2);auto queue=reinterpret_cast<VkQueue>(3);
 auto receipt=consumers.Register(device,7,semaphore);Check(bool(receipt),"register before forwarding");
 Check(!consumers.Register(device,7,semaphore),"cannot register overlapping semaphore");
 Check(!receipt->Complete(),"CPU return and producer completion grant nothing");
 Check(consumers.Prepare(device,8,1,{&semaphore,1}).empty(),"wrong device generation");
 auto uses=consumers.Prepare(device,7,1,{&semaphore,1});Check(uses.size()==1,"exact wait on actual consumer family");
 Check(consumers.Prepare(device,7,1,{&semaphore,1}).empty(),"no double claim");
 auto submission=*owner.PrepareSubmission({queue,7,uses});owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
 consumers.Returned(uses,VK_SUCCESS);Check(!receipt->Complete(),"successful submit is not completed wait");
 owner.ObserveFence(submission,VK_SUCCESS);Check(receipt->Complete(),"owned checkpoint proves wait completed");
 Check(!consumers.Register(device,7,semaphore),"old lease must release before reuse");
 receipt.reset();receipt=consumers.Register(device,7,semaphore);Check(bool(receipt),"new semaphore cycle after released receipt");
 uses=consumers.Prepare(device,7,2,{&semaphore,1});submission=*owner.PrepareSubmission({queue,7,uses});owner.SubmissionReturned(submission,VK_ERROR_OUT_OF_DEVICE_MEMORY);consumers.Returned(uses,VK_ERROR_OUT_OF_DEVICE_MEMORY);
 Check(!receipt->Complete(),"failed wait never completes");
 uses=consumers.Prepare(device,7,2,{&semaphore,1});Check(uses.size()==1,"definite OOM allows original provider retry");
 submission=*owner.PrepareSubmission({queue,7,uses});owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_ERROR_OUT_OF_HOST_MEMORY);consumers.Returned(uses,VK_SUCCESS);
 Check(!receipt->Complete(),"missing checkpoint is not proof");
 Check(consumers.Prepare(device,7,2,{&semaphore,1}).empty(),"uncertain checkpoint never resubmits wait");
 auto physicalSignal=reinterpret_cast<VkSemaphore>(99);
 auto physical=consumers.Register(device,7,physicalSignal);bool acquired=false,acquireComplete=false;
 consumers.PhysicalPresent(device,7,{&physicalSignal,1},VK_SUCCESS,[&]{return acquired&&acquireComplete;});
 Check(!physical->Complete(),"physical Present CPU return never releases signal");acquired=true;
 Check(!physical->Complete(),"physical reacquire alone is insufficient");acquireComplete=true;
 Check(physical->Complete(),"exact physical acquire synchronization retires forwarded wait");
 Check(consumers.Prepare(device,7,1,{&physicalSignal,1}).empty(),"physical consumer excludes duplicate queue wait claim");
 consumers.DeviceDestroyed(device);Check(!receipt->Complete(),"device destruction is not completion");
 std::puts("Present provider wait receipt: delayed, failed, retried, uncertain and generation cases pass");
}
#else
int main(){std::fputs("FAIL: provider wait-consumption owner missing\n",stderr);return 1;}
#endif
