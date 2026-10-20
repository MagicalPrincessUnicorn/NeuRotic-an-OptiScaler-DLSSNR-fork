#include "../OptiScaler/dlssnr/VulkanNrPassRecorder.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    VkNrPassHistory history;
    Check(history.Observe(1,{{11,1}}),"first child owns its independent upstream history seed");
    Check(!history.Observe(1,{{11,1}}),"unchanged upstream does not reset child history");
    Check(history.Observe(2,{{11,1},{21,1}})&&history.Observe(2,{{11,1},{22,1}}),
        "changed preceding model resets only its downstream histories");
    Check(!history.Observe(1,{{11,1}}),"later-pass change cannot reset an earlier pass");
    Check(history.Observe(1,{{11,2}}),"upstream explicit reset propagates without recreating the model");
    VkNrRecordingOwner owner;VkNrReservationPool slots(owner);auto device=reinterpret_cast<VkDevice>(1);
    auto cb=reinterpret_cast<VkCommandBuffer>(2);owner.OnAllocate(device,reinterpret_cast<VkCommandPool>(3),cb,0);
    owner.OnBegin(cb,0,VK_SUCCESS);auto use=*owner.Reserve(cb,1);
    VkFrameRequest frame;frame.device=device;frame.use=use;
    NVSDK_NGX_Resource_VK images[11]{};for(unsigned i=0;i<11;++i){images[i].Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        images[i].Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(uintptr_t(100+i));}
    frame.color=&images[0];VkNrPassPlan plan;plan.requireComplete=true;plan.passes.resize(10);
    for(unsigned i=0;i<10;++i)plan.passes[i].passIndex=i;
    auto reservation=slots.Reserve(use,VkNrPassDemand(10));Check(reservation&&reservation->slots.size()==82,"entire ten-pass demand reserves before recording");
    unsigned modelCalls=0,deliveries=0;uint32_t fail=2;
    VkNrPassCallbacks callbacks;
    callbacks.record=[&](uint32_t pass,const VkFrameRequest& input,const VkNrGenerationKey&,const VkNrReservation& local,uint64_t version){
        Check(input.color==&images[pass]&&version==pass,"pass N consumes previous private output version");
        Check(local.use==use&&local.slots.size()==8,"immutable per-pass slice of whole-chain reservation");++modelCalls;
        if(pass==fail)return VkNrPassStep{{VkRecordStatus::Partial,"middle failure",true,false},nullptr,version};
        return VkNrPassStep{{VkRecordStatus::Complete,"",true,true},&images[pass+1],version+1};
    };
    callbacks.deliver=[&](const VkNrPassStep& final,uint32_t slot){Check(slot==reservation->slots[reservation->slots.size()-2]&&final.output==&images[10],"one final publication");++deliveries;return true;};
    auto failed=RecordVkNrPassChain(frame,plan,*reservation,callbacks);
    Check(failed.requested==10&&failed.completed==2&&!failed.deliverable&&deliveries==0&&modelCalls==3,"middle failure preserves original and cannot publish required chain");
    fail=99;modelCalls=0;auto good=RecordVkNrPassChain(frame,plan,*reservation,callbacks);
    Check(good.completed==10&&good.outputVersion==10&&good.deliverable&&deliveries==1,"only complete chain writes final output");
    auto shortReservation=*reservation;shortReservation.slots.pop_back();modelCalls=0;
    Check(!RecordVkNrPassChain(frame,plan,shortReservation,callbacks).deliverable&&modelCalls==0,"insufficient reservation refuses before any model call");
    plan.requireComplete=false;fail=2;
    callbacks.deliver=[&](const VkNrPassStep& final,uint32_t){
        Check(final.passIndex==1&&final.requestedPasses==10&&final.completedPasses==2,
            "partial Native delivery identifies the actual completed pass and genuine counts");return true;};
    Check(RecordVkNrPassChain(frame,plan,*reservation,callbacks).deliverable,"eligible Native prefix remains deliverable");
    fail=99;
    callbacks.deliver=[](const VkNrPassStep&,uint32_t){return false;};
    Check(!RecordVkNrPassChain(frame,plan,*reservation,callbacks).deliverable,"failed final delivery is never success");
    std::puts("serial previous-output consumption, entire-chain reservation, middle failure and final-only delivery verified");
    unsigned preparations=0;fail=2;modelCalls=0;
    callbacks.prepare=[&](uint32_t pass,const VkFrameRequest& input,const VkNrGenerationKey&,const VkNrReservation&,uint64_t){
        ++preparations;Check(input.color->Resource.ImageViewInfo.Image==images[pass].Resource.ImageViewInfo.Image,"prepare uses prior private allocation metadata");
        if(pass==fail)return VkNrPassStep{{VkRecordStatus::Partial,"new model initialization",true,false},nullptr,0};
        return VkNrPassStep{{VkRecordStatus::Complete,"",false,false},&images[pass+1],0};
    };
    auto creating=RecordVkNrPassChain(frame,plan,*reservation,callbacks);
    Check(modelCalls==0&&preparations==3&&creating.recording.preparationOnly&&creating.completed==0&&!creating.deliverable,
        "no existing model evaluates on another layer's creation command buffer");
    callbacks.prepare=[&](uint32_t pass,const VkFrameRequest&,const VkNrGenerationKey&,const VkNrReservation&,uint64_t){
        if(pass==2)return VkNrPassStep{{VkRecordStatus::NoWork,"insufficient VRAM",false,false},nullptr,0};
        return VkNrPassStep{{VkRecordStatus::Complete,"",false,false},&images[pass+1],0};
    };
    auto refused=RecordVkNrPassChain(frame,plan,*reservation,callbacks);
    Check(modelCalls==0&&!refused.recording.preparationOnly&&refused.recording.reason=="insufficient VRAM",
        "allocation refusal leaves every healthy prefix history untouched and preserves reason");
    callbacks.prepare=[&](uint32_t pass,const VkFrameRequest&,const VkNrGenerationKey&,const VkNrReservation&,uint64_t){
        return VkNrPassStep{{VkRecordStatus::Complete,"",false,false},&images[pass+1],0};};
    fail=99;callbacks.deliver=[](const auto&,uint32_t){return true;};
    Check(RecordVkNrPassChain(frame,plan,*reservation,callbacks).deliverable&&modelCalls==10,"prepared chain renders all ten passes");
}
