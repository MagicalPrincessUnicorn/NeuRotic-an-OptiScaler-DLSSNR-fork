// Actual initializer + completion/recording/generation owners; Vulkan driver and private provider
// are explicit deterministic CPU seams. The software transport fixture separately checks bytes.
#include "dlssnr/VulkanPresentInitialization.h"
#include "dlssnr/VulkanNrCompletion.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
using namespace DlssNr;
static void Check(bool value,const char* message)
{if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> static T Handle(uintptr_t value){return reinterpret_cast<T>(value);}
namespace Driver {
uintptr_t next=100;
std::map<VkFence,VkResult> fences;
bool complete=true,failCheckpoint=false,failSubmit=false,failEnd=false,failFence=false;
unsigned resets=0,submits=0,checkpoints=0,destroyPools=0,failResetAt=0;
std::vector<uint64_t> waitTimeouts;
void Clear(){complete=true;failCheckpoint=failSubmit=failEnd=failFence=false;resets=submits=checkpoints=destroyPools=failResetAt=0;waitTimeouts.clear();}
}
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks*,VkCommandPool* pool)
{Check(info->queueFamilyIndex==0,"authenticated queue family");*pool=Handle<VkCommandPool>(Driver::next++);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){++Driver::destroyPools;}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* commands)
{Check(info->commandBufferCount==1&&info->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY,"one private primary command");*commands=Handle<VkCommandBuffer>(Driver::next++);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice,VkCommandPool,VkCommandPoolResetFlags)
{return ++Driver::resets==Driver::failResetAt?VK_ERROR_UNKNOWN:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo* info)
{Check(info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,"one-time initialization recording");return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer){return Driver::failEnd?VK_ERROR_UNKNOWN:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* fence)
{if(Driver::failFence)return VK_ERROR_OUT_OF_HOST_MEMORY;*fence=Handle<VkFence>(Driver::next++);Driver::fences[*fence]=VK_NOT_READY;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice,VkFence fence,const VkAllocationCallbacks*){Driver::fences.erase(fence);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice,VkFence fence){return Driver::fences.at(fence);}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice,uint32_t count,const VkFence* fences,VkBool32,uint64_t timeout)
{Check(count==1,"wait only one owned completion fence");Driver::waitTimeouts.push_back(timeout);
 return Driver::fences.at(*fences)==VK_SUCCESS?VK_SUCCESS:VK_TIMEOUT;}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence fence)
{
    Check(count==1&&info->waitSemaphoreCount==0&&info->signalSemaphoreCount==0,
        "private initialization consumes no application waits or presentation signals");
    if(info->commandBufferCount){Check(info->commandBufferCount==1&&!fence,"private command uses separately observed checkpoint");++Driver::submits;return Driver::failSubmit?VK_ERROR_UNKNOWN:VK_SUCCESS;}
    ++Driver::checkpoints;Check(fence!=VK_NULL_HANDLE,"checkpoint owns a real registered fence");
    if(Driver::failCheckpoint)return VK_ERROR_OUT_OF_HOST_MEMORY;
    Driver::fences[fence]=Driver::complete?VK_SUCCESS:VK_NOT_READY;return VK_SUCCESS;
}
}
struct Payload:VkNrGenerationPayload{Payload(){privateBytes=16;}};
struct Fixture
{
    VkDevice device=Handle<VkDevice>(Driver::next++);
    VkQueue queue=Handle<VkQueue>(Driver::next++);
    VkPresentInitializationOwner initializer;
    VkPresentInitializationRequest request;
    std::array<std::unique_ptr<VkNrGenerationOwner>,10> models;
    uint32_t passes=1;unsigned creates=0,releases=0;bool current=true,changeAtCreate=false;
    bool expireAtCreate=false;
    VkNrWaitBudget::Clock::time_point now{};
    Fixture()
    {
        Driver::Clear();
        RegisterVkNrCompletionDevice(device,{vkCreateFence,vkDestroyFence,vkGetFenceStatus,vkQueueSubmit,vkWaitForFences});
        RegisterVkNrCompletionQueue(device,queue,0);
        request.device=device;request.instance=Handle<VkInstance>(1);request.physicalDevice=Handle<VkPhysicalDevice>(2);
        request.frame.queue=queue;request.frame.queueFamily=0;request.frame.deviceGeneration=VkNrCompletionDeviceGeneration(device);
        request.frame.route=VkNrRoute::Present;request.frame.output={64,64};request.frame.work={32,32};
        request.frame.routeEpoch=1;request.frame.acquireGeneration=42;
        request.frame.representation.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        FreshBudget();
        for(auto& owner:models)owner=std::make_unique<VkNrGenerationOwner>(VulkanNrRecordings(),
            [&](const VkNrGenerationKey&,VkNrUseId,uint64_t){
                ++creates;if(changeAtCreate)current=false;if(expireAtCreate)now+=std::chrono::milliseconds(101);
                return VkNrGenerationCreation{std::make_unique<Payload>(),true,true,{}};
            },[&](VkNrGenerationPayload&){++releases;return true;});
    }
    void FreshBudget(){request.waitBudget=std::make_shared<VkNrWaitBudget>(now,[this]{return now;});}
    void Poll(){for(auto& owner:models)owner->RetireCompleted(VulkanNrRecordings());}
    VkPresentInitializationStep PrepareModels(const VkPresentInitializationRequest& req,VkCommandBuffer,VkNrUseId use)
    {
        VkPresentInitializationStep out;out.requestedPasses=passes;
        for(uint32_t pass=0;pass<passes;++pass){
            VkNrGenerationKey key;key.frame=req.frame;key.passIndex=pass;
            CompleteVkPresentGenerationKey(key,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16_SFLOAT,false);
            const auto prior=creates;
            const auto result=models[pass]->Prepare(key,use,{},true,false);
            out.recorded=creates!=prior;
            if(!result.ready){out.status=result.pending?VkPresentInitializationStatus::Pending:VkPresentInitializationStatus::Refused;out.reason=result.reason;return out;}
            Check(models[pass]->Commit(result.generation),"stand-in model committed through actual owner");
            if(out.recorded){out.status=VkPresentInitializationStatus::Recorded;return out;}
        }
        out.status=VkPresentInitializationStatus::Ready;return out;
    }
    VkPresentInitializationResult Run(){return initializer.Prepare(request,[&]{return current;},
        [&](const auto& req,VkCommandBuffer cmd,VkNrUseId use){return PrepareModels(req,cmd,use);},[&]{Poll();});}
    void Complete(){for(auto& [fence,status]:Driver::fences)status=VK_SUCCESS;initializer.Maintain([&]{Poll();});}
    ~Fixture()
    {
        // Explicit simulated device destruction for both successful and quarantined cases.
        VulkanNrRecordings().OnDeviceDestroyed(device);
        initializer.DeviceDestroyed(device);
        for(auto& owner:models)owner->AbandonDevice();
        DestroyVkNrCompletionDevice(device);
    }
};

static void EligibleOnlyWhenOptedIn()
{
    using P=VkNrPlacement;using R=VkNrRoute;
    Check(!VkPresentInitializationEligible(false,R::Present,P::AfterSR,0,0,false,false,false),"default off");
    Check(VkPresentInitializationEligible(true,R::Present,P::AfterSR,0,0,false,false,false),"explicit Match Native opt-in");
    Check(!VkPresentInitializationEligible(true,R::Native,P::BeforeSR,0,0,false,false,false),"NativeBeforeSR excluded");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,3,3,false,false,false),"Manual unchanged");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,4,4,false,false,false),"Automatic unchanged");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,0,4,false,false,false),"no effective-policy substitution");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,0,0,true,false,false),"hold excluded");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,0,0,false,true,false),"private DLAA excluded");
    Check(!VkPresentInitializationEligible(true,R::Present,P::AfterSR,0,0,false,false,true),"diagnostic capture excluded");
}

static void SuccessAndReuse()
{
    Fixture f;f.passes=10;
    auto result=f.Run();
    Check(result.status==VkPresentInitializationStatus::Ready&&result.submissions==10&&f.creates==10,"all ten private passes completed before Ready");
    Check(!f.initializer.Busy()&&Driver::submits==10&&Driver::checkpoints==10,"owned completion latched for every private submission");
    f.request.frame.acquireGeneration++;
    result=f.Run();
    Check(result.status==VkPresentInitializationStatus::Ready&&result.submissions==0&&f.creates==10,"ready compatible chain needs no additional submission");
    f.request.frame.work={40,32};
    result=f.Run();
    Check(result.status==VkPresentInitializationStatus::Ready&&f.creates==20&&f.releases==10,"authentic dimension change retires and recreates each model");
}
static void TimeoutRetainsAndResumes()
{
    Fixture f;Driver::complete=false;
    const auto first=f.Run();const auto resetCount=Driver::resets;
    Check(first.status==VkPresentInitializationStatus::Pending&&f.initializer.Busy()&&f.creates==1,"timeout retains submitted initializer");
    Check(!f.initializer.Release(true,[&]{f.Poll();})&&Driver::destroyPools==0,"cannot release pending command pool");
    auto second=f.Run();
    Check(second.status==VkPresentInitializationStatus::Pending&&Driver::resets==resetCount&&Driver::submits==1,"pending command cannot reset, duplicate or infer");
    f.Complete();second=f.Run();
    Check(second.status==VkPresentInitializationStatus::Ready&&f.creates==1,"actual later completion admits existing generation");
}
static void ExpiredCreationStillRetainsItsSubmission()
{
    Fixture f;Driver::complete=false;f.expireAtCreate=true;
    const auto first=f.Run();
    Check(first.status==VkPresentInitializationStatus::Pending&&Driver::submits==1&&f.initializer.Busy(),"slow create submits private work once, then stops without inference");
    Check(!Driver::waitTimeouts.empty()&&Driver::waitTimeouts.back()==0,"exhausted shared budget cannot renew wait");
    f.expireAtCreate=false;f.Complete();f.FreshBudget();
    Check(f.Run().status==VkPresentInitializationStatus::Ready&&f.creates==1,"completed slow creation is reused on next authentic opportunity");
}
static void StaleCreationCancelsWithoutExternalWork()
{
    Fixture f;f.changeAtCreate=true;
    Check(f.Run().status==VkPresentInitializationStatus::Refused&&Driver::submits==0&&f.releases==1,
        "changed frame cancels unsubmitted private initialization and poisons its generation");
    Check(!f.initializer.Busy(),"successful cancellation releases recording observation");
}
static void UncertaintyQuarantines()
{
    {Fixture f;Driver::failFence=true;Driver::failResetAt=2;
     Check(f.Run().status==VkPresentInitializationStatus::Uncertain&&f.initializer.Busy()&&f.creates==0,"reservation cancellation failure quarantines immediately");}
    {Fixture f;Driver::failFence=true;
     Check(f.Run().status==VkPresentInitializationStatus::Refused&&!f.initializer.Busy()&&f.creates==0,"failed fence allocation cancels without model work");}
    {Fixture f;Driver::failCheckpoint=true;
     Check(f.Run().status==VkPresentInitializationStatus::Uncertain&&f.initializer.Busy(),"failed checkpoint cannot become Ready or ordinary timeout");
     Check(!f.initializer.Release(true,[&]{f.Poll();}),"checkpoint uncertainty retains pool");}
    {Fixture f;Driver::failSubmit=true;
     Check(f.Run().status==VkPresentInitializationStatus::Uncertain&&f.initializer.Busy(),"uncertain submit retains owner");}
    {Fixture f;f.changeAtCreate=true;Driver::failResetAt=2;
     Check(f.Run().status==VkPresentInitializationStatus::Uncertain&&f.initializer.Busy(),"failed cancellation retains recording and payload");}
    {Fixture f;Driver::failEnd=true;
     Check(f.Run().status==VkPresentInitializationStatus::Refused&&!f.initializer.Busy()&&Driver::submits==0,
        "failed end followed by successful cancellation does not submit");}
}
int main()
{
    EligibleOnlyWhenOptedIn();SuccessAndReuse();TimeoutRetainsAndResumes();
    ExpiredCreationStillRetainsItsSubmission();StaleCreationCancelsWithoutExternalWork();UncertaintyQuarantines();
    std::puts("PASS actual initializer CPU state machine; explicit fake driver/provider, no GPU or private model");
}
