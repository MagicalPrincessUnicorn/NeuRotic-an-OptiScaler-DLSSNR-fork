#include "../OptiScaler/dlssnr/VulkanNrReservations.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool value,const char* name) { if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);} }
int main()
{
    VkNrRecordingOwner owner;
    auto device = reinterpret_cast<VkDevice>(1); auto poolHandle = reinterpret_cast<VkCommandPool>(2);
    auto a = reinterpret_cast<VkCommandBuffer>(3),b = reinterpret_cast<VkCommandBuffer>(4); auto queue = reinterpret_cast<VkQueue>(5);
    for(auto buffer:{a,b}) { owner.OnAllocate(device,poolHandle,buffer,0);owner.OnBegin(buffer,0,VK_SUCCESS); }
    Check(owner.CommandDevice(a)==device&&owner.CommandDevice(VK_NULL_HANDLE)==VK_NULL_HANDLE,"reset cleanup uses the actual recording device");
    auto first=owner.Reserve(a,1),other=owner.Reserve(b,1);Check(first&&other,"known uses reserved");
    int grows=0;bool allocationFails=false;
    VkNrReservationPool pool(owner,[&](uint32_t capacity){++grows;Check(capacity<=256,"physical cap enforced");return !allocationFails;});
    auto pending=pool.Reserve(*first,19);Check(pending&&pending->slots.size()==19,"nineteenth dispatch gets distinct immutable slot");
    Check(pool.Capacity()==48&&grows==1,"donor initial capacity 48");
    for(auto slot:pending->slots){Check(pool.SlotOwned(slot)&&pool.Consume(*first,slot),"reserved slot consumed once");Check(!pool.Consume(*first,slot),"constants and descriptors cannot be rewritten in same reservation");}
    Check(!pool.Consume(*other,pending->slots[0]),"foreign use cannot overwrite constants");
    Check(!pool.Reserve(*other,257)&&pool.SlotOwned(pending->slots[0]),"overflow refuses without releasing pending slots");
    allocationFails=true;Check(!pool.Reserve(*other,40)&&pool.Capacity()==48,"failed growth leaves prior slots immutable");allocationFails=false;
    auto second=pool.Reserve(*other,40);Check(second&&pool.Capacity()==80,"donor grows by 32");
    owner.OnEnd(a,VK_SUCCESS);owner.OnEnd(b,VK_SUCCESS);
    auto submission=owner.PrepareSubmission({queue,1,{*first}});Check(submission.has_value(),"actual submission reserved");
    owner.SubmissionReturned(*submission,VK_SUCCESS);owner.CheckpointReturned(*submission,VK_SUCCESS);
    Check(!VkNrReadbackReady(owner,*first,true,true,false),"query availability is not whole-use completion");
    owner.ObserveFence(*submission,VK_SUCCESS);
    Check(!VkNrReadbackReady(owner,*first,true,false,false),"noncoherent memory needs invalidation");
    Check(VkNrReadbackReady(owner,*first,true,false,true)&&VkNrReadbackReady(owner,*first,true,true,false),"completed coherent or invalidated data can publish");
    pool.ReleaseCompleted(owner);Check(pool.SlotOwned(pending->slots[0]),"completion without recording reset retains descriptor slots");
    owner.OnReset(a,VK_SUCCESS);pool.ReleaseCompleted(owner);Check(!pool.SlotOwned(pending->slots[0])&&pool.SlotOwned(second->slots[0]),"only complete terminal recording releases slots");
    owner.OnReset(b,VK_SUCCESS);pool.ReleaseCompleted(owner);
    owner.OnBegin(a,0,VK_SUCCESS);auto whole=owner.Reserve(a,2);Check(whole.has_value(),"large chain use known");
    auto all=pool.Reserve(*whole,256);Check(all&&all->slots.size()==256&&pool.Capacity()==256,"growth caps at 256");
    Check(!pool.DrainReleased(),"partial unsubmitted record keeps its shader/query reservations at shutdown");
    owner.OnReset(a,VK_SUCCESS);
    Check(pool.DrainReleased()&&pool.DrainReleased(),"actual reset permits repeatable reservation drain");
    Check(!pool.Reserve(*other,1),"pool refuses exhausted or obsolete use");
    VkNrRecordingOwner oneTimeOwner;
    oneTimeOwner.OnAllocate(device,poolHandle,b,0);oneTimeOwner.OnBegin(b,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
    auto oneTime=oneTimeOwner.Reserve(b,3);Check(oneTime.has_value(),"one-time use reserved");oneTimeOwner.OnEnd(b,VK_SUCCESS);
    auto once=oneTimeOwner.PrepareSubmission({queue,1,{*oneTime}});Check(once.has_value(),"one-time submission reserved");
    oneTimeOwner.SubmissionReturned(*once,VK_SUCCESS);oneTimeOwner.CheckpointReturned(*once,VK_SUCCESS);oneTimeOwner.ObserveFence(*once,VK_SUCCESS);
    Check(oneTimeOwner.GpuComplete(*oneTime)&&oneTimeOwner.RecordingReleased(*oneTime),"completed one-time recording is actually terminal without reset");
    std::puts("immutable slots, transactional growth, replay lifetimes and completed readback visibility verified");
}
