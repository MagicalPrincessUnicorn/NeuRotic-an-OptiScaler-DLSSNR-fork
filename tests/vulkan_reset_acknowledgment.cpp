#include <dlssnr/VulkanNrGeneration.h>
#include <cstdio>
#include <stdexcept>

namespace
{
struct Resource {void* feature=reinterpret_cast<void*>(uintptr_t(2));};
struct OldState
{
    VkDevice device=reinterpret_cast<VkDevice>(uintptr_t(1));
    Resource resource;Resource* resources=&resource;
    bool failed=false,modelRecordedCurrent=false,reset=true;
    uint64_t resetRevision=1;
};
struct CurrentState
{
    VkDevice device=reinterpret_cast<VkDevice>(uintptr_t(1));
    Resource resource;Resource* resources=&resource;
    bool failed=false,modelRecordedCurrent=false;
    DlssNr::VkNrResetRevision resetState{1};
};
#define LOG_ERROR(...) ((void)0)
#define Fail(reason) (state.failed=true)
void OldAck(OldState& state,bool prepared,int evaluated,uint64_t resetRevision,VkDevice evaluationDevice,void* evaluationFeature)
{
#include "old_model_ack.inc"
}
void CurrentAck(CurrentState& state,bool prepared,int evaluated,uint64_t resetRevision,VkDevice evaluationDevice,void* evaluationFeature)
{
#include "current_model_ack.inc"
}
#undef Fail
#undef LOG_ERROR
bool OldInput(const OldState& state)
{
#include "old_model_reset_input.inc"
}
bool CurrentInput(const CurrentState& state)
{
#include "current_model_reset_input.inc"
}
unsigned checks=0;
void Check(bool ok,const char* text)
{++checks;if(!ok)throw std::runtime_error(text);std::printf("PASS %s\n",text);}
}
int main()
{
    try
    {
        for(unsigned scenario=0;scenario<7;++scenario)
        {
            OldState old;CurrentState current;
            if(scenario==5||scenario==6)
            {
                old.reset=false;current.resetState.Acknowledge(current.resetState.Capture());
            }
            if(scenario==6){old.reset=true;current.resetState.MarkPending();}
            const auto captured=old.resetRevision;
            Check(OldInput(old)==CurrentInput(current)&&CurrentInput(current)==(scenario!=5),
                "actual live model Reset argument preserves pending and pending-only policies");
            if(scenario==5){old.reset=true;++old.resetRevision;current.resetState.Request();}
            const bool prepared=scenario!=1;
            const int evaluated=scenario==2?0:1;
            const auto device=scenario==3?reinterpret_cast<VkDevice>(uintptr_t(9)):old.device;
            void* feature=scenario==4?reinterpret_cast<void*>(uintptr_t(9)):old.resources->feature;
            OldAck(old,prepared,evaluated,captured,device,feature);
            CurrentAck(current,prepared,evaluated,captured,device,feature);
            Check(old.reset==current.resetState.Pending()&&old.failed==current.failed&&
                old.modelRecordedCurrent==current.modelRecordedCurrent,
                "actual old/current model guards and acknowledgment produce the same owner state");
            const bool success=scenario==0||scenario==5||scenario==6;
            Check(current.failed==!success&&current.modelRecordedCurrent==success&&
                current.resetState.Pending()==(!success||scenario==5),
                "rejection or changed generation cannot acknowledge; successful stale return retains newer request");
        }
        std::printf("VULKAN_RESET_ACK checks=%u PASS actualSourceGuardBlocks=2 authoredStateProjection=1 privateProvider=0 GPU=0\n",checks);
        return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"VULKAN_RESET_ACK FAIL: %s\n",e.what());return 1;}
}
