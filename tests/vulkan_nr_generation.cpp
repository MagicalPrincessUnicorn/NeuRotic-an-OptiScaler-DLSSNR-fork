#include "../OptiScaler/dlssnr/VulkanNrGeneration.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* text) { if(!v) { std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1); } }
struct Resource : VkNrGenerationPayload { explicit Resource(uint64_t bytes) { privateBytes=bytes; } };
template<class Owner, class Wait> auto PrepareWaiting(Owner& owner,const VkNrGenerationKey& key,VkNrUseId use,Wait wait)
{
    if constexpr(requires { owner.Prepare(key,use,wait); }) return owner.Prepare(key,use,wait);
    else return owner.Prepare(key,use);
}
template<class Frame> void SetFamily(Frame& frame,uint32_t family)
{ if constexpr(requires { frame.queueFamily=family; })frame.queueFamily=family; }
static void ResumeRetiresBeforeReplacement()
{
    VkNrRecordingOwner recordings;
    const auto device=reinterpret_cast<VkDevice>(1);
    const auto queue=reinterpret_cast<VkQueue>(2);
    const auto pool=reinterpret_cast<VkCommandPool>(3);
    uintptr_t nextBuffer=100;VkCommandBuffer buffer{};
    auto use=[&] {
        buffer=reinterpret_cast<VkCommandBuffer>(nextBuffer++);
        recordings.OnAllocate(device,pool,buffer,0);recordings.OnBegin(buffer,0,VK_SUCCESS);
        return *recordings.Reserve(buffer,1);
    };
    auto submit=[&](VkNrUseId id) {
        recordings.OnEnd(buffer,VK_SUCCESS);
        const auto receipt=recordings.PrepareSubmission({queue,1,{id}});
        Check(receipt.has_value(),"resume predecessor submission known");
        recordings.SubmissionReturned(*receipt,VK_SUCCESS);recordings.CheckpointReturned(*receipt,VK_SUCCESS);
        return *receipt;
    };
    unsigned live=0,creates=0,releases=0;bool releaseFails=false,createFails=false;
    VkNrGenerationOwner owner(recordings,[&](const auto&,VkNrUseId,uint64_t) {
        Check(live==0,"resume replacement releases predecessor before allocating fresh model/images");
        ++creates;++live;
        return VkNrGenerationCreation{std::make_unique<Resource>(16),!createFails,true,createFails?"creation failed":""};
    },[&](VkNrGenerationPayload&) { if(releaseFails)return false;--live;++releases;return true; });
    VkNrGenerationKey key;key.frame.route=VkNrRoute::Present;key.frame.queue=queue;
    key.frame.output=key.frame.work={32,32};key.frame.deviceGeneration=key.frame.routeEpoch=1;
    key.frame.resumeGeneration=1;key.frame.representation.format=key.colorFormat=key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto firstUse=use();const auto firstBuffer=buffer;
    const auto first=owner.Prepare(key,firstUse);Check(first.ready&&owner.Commit(first.generation),"resume baseline ready");
    const auto firstReceipt=submit(firstUse);
    auto resumed=key;resumed.frame.resumeGeneration=2;
    const auto secondUse=use();
    auto pending=owner.Prepare(resumed,secondUse);
    Check(pending.pending&&!pending.ready&&creates==1&&owner.Count()==1,"resume waits for actual predecessor GPU completion");
    recordings.ObserveFence(firstReceipt,VK_SUCCESS);
    pending=owner.Prepare(resumed,secondUse);
    Check(pending.pending&&!pending.ready&&creates==1,"resume waits for replayable recording release after completion");
    Check(recordings.AddConsumerHold(firstUse),"resume predecessor independent consumer retained");
    recordings.OnReset(firstBuffer,VK_SUCCESS);
    pending=owner.Prepare(resumed,secondUse);
    Check(pending.pending&&!pending.ready&&creates==1,"resume preserves independent consumer ownership");
    recordings.RemoveConsumerHold(firstUse);releaseFails=true;
    pending=owner.Prepare(resumed,secondUse);
    Check(pending.pending&&!pending.ready&&creates==1&&live==1&&owner.PrivateBytes()==16,
        "failed vendor release retains predecessor allocation and blocks replacement");
    pending=owner.Prepare(resumed,secondUse);
    Check(pending.pending&&creates==1&&live==1,"repeated failed release cannot bypass resume drain");
    releaseFails=false;
    const auto second=owner.Prepare(resumed,secondUse);
    Check(second.ready&&owner.Commit(second.generation)&&creates==2&&releases==1&&owner.Count()==1,
        "resume creates fresh generation only after old model release succeeds");
    const auto secondBuffer=buffer;const auto secondReceipt=submit(secondUse);
    recordings.ObserveFence(secondReceipt,VK_SUCCESS);recordings.OnReset(secondBuffer,VK_SUCCESS);
    const auto normalUse=use();const auto normal=owner.Prepare(resumed,normalUse);
    Check(normal.ready&&normal.generation==second.generation&&creates==2,"ordinary compatible frame reuses resumed model");
    const auto normalBuffer=buffer;const auto normalReceipt=submit(normalUse);
    recordings.ObserveFence(normalReceipt,VK_SUCCESS);recordings.OnReset(normalBuffer,VK_SUCCESS);
    auto thirdKey=resumed;thirdKey.frame.resumeGeneration=3;createFails=true;
    const auto failedUse=use();const auto failedBuffer=buffer;
    const auto failed=owner.Prepare(thirdKey,failedUse);
    Check(failed.failed&&!failed.ready&&creates==3&&releases==2&&owner.Count()==1&&!owner.AppliedKey(),
        "failed resume creation retains its own recorded resources without restoring retired feature");
    const auto failedReceipt=submit(failedUse);recordings.ObserveFence(failedReceipt,VK_SUCCESS);
    recordings.OnReset(failedBuffer,VK_SUCCESS);createFails=false;
    const auto retryUse=use();const auto retry=owner.Prepare(thirdKey,retryUse);
    Check(retry.ready&&owner.Commit(retry.generation)&&creates==4&&releases==3&&owner.Count()==1,
        "failed recorded creation retries after actual completion and recording release");
    std::puts("resume retirement ordering, GPU/recording/consumer waits, release failure and creation retry verified");
}
static void PreparationRetiresBeforeResize()
{
    VkNrRecordingOwner recordings;
    const auto device=reinterpret_cast<VkDevice>(1);
    const auto queue=reinterpret_cast<VkQueue>(2);
    const auto pool=reinterpret_cast<VkCommandPool>(3);
    auto oldBuffer=reinterpret_cast<VkCommandBuffer>(500), newBuffer=reinterpret_cast<VkCommandBuffer>(501);
    auto use=[&](VkCommandBuffer cb) {
        recordings.OnAllocate(device,pool,cb,0);recordings.OnBegin(cb,0,VK_SUCCESS);
        return *recordings.Reserve(cb,1);
    };
    unsigned live=0,creates=0;bool releaseFails=false;
    VkNrGenerationOwner owner(recordings,[&](const auto&,VkNrUseId,uint64_t) {
        Check(live==0,"resolution preparation releases old opaque model before allocating replacement");
        ++live;++creates;return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};
    },[&](auto&) {if(releaseFails)return false;--live;return true;});
    VkNrGenerationKey key;key.frame.route=VkNrRoute::Present;key.frame.queue=queue;
    key.frame.output=key.frame.work={32,32};key.frame.deviceGeneration=key.frame.routeEpoch=1;
    key.frame.representation.format=key.colorFormat=key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto oldUse=use(oldBuffer);auto first=owner.Prepare(key,oldUse,{},true);
    Check(first.ready&&owner.Commit(first.generation),"resize preparation baseline");
    recordings.OnEnd(oldBuffer,VK_SUCCESS);auto receipt=*recordings.PrepareSubmission({queue,1,{oldUse}});
    recordings.SubmissionReturned(receipt,VK_SUCCESS);recordings.CheckpointReturned(receipt,VK_SUCCESS);
    auto changed=key;changed.frame.work={64,64};const auto newUse=use(newBuffer);
    auto pending=owner.Prepare(changed,newUse,{},true);
    Check(pending.pending&&!pending.ready&&creates==1,"resize preparation waits for predecessor GPU completion");
    recordings.ObserveFence(receipt,VK_SUCCESS);
    Check(owner.Prepare(changed,newUse,{},true).pending&&creates==1,"resize cannot free replayable recording");
    Check(recordings.AddConsumerHold(oldUse),"resize consumer hold");recordings.OnReset(oldBuffer,VK_SUCCESS);
    Check(owner.Prepare(changed,newUse,{},true).pending&&creates==1,"resize cannot free consumer-held model");
    recordings.RemoveConsumerHold(oldUse);releaseFails=true;
    Check(owner.Prepare(changed,newUse,{},true).pending&&creates==1&&live==1,"resize release failure blocks replacement");
    Check(owner.Prepare(key,newUse,{},true).pending&&!owner.CanApply(key)&&creates==1,
        "returning to old resolution cannot revive a model whose release already started");
    releaseFails=false;auto resized=owner.Prepare(changed,newUse,{},true);
    Check(resized.ready&&owner.Commit(resized.generation)&&creates==2&&live==1&&owner.Count()==1,
        "resize replacement has no overlapping model allocation");
}
static void PresentDynamicGuidesKeepModel()
{
    VkNrRecordingOwner recordings;
    const auto device=reinterpret_cast<VkDevice>(1);
    const auto queue=reinterpret_cast<VkQueue>(2);
    const auto pool=reinterpret_cast<VkCommandPool>(3);
    unsigned creates=0;uintptr_t serial=1000;
    VkNrGenerationOwner owner(recordings,[&](const auto&,VkNrUseId,uint64_t) {
        ++creates;return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};
    },[](auto&){return true;});
    VkNrGenerationKey key;key.frame.route=VkNrRoute::Present;key.frame.queue=queue;
    key.frame.output=key.frame.work={3840,2160};key.frame.deviceGeneration=key.frame.routeEpoch=1;
    key.frame.guideGeneration=7;key.frame.representation.format=key.colorFormat=key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkNrTemporalMetadata temporal;temporal.featureGeneration=7;temporal.providerGeneration=8;
    temporal.providerFrameKnown=true;temporal.viewport=0;
    temporal.color=temporal.depth=temporal.motion={0,0,2689,1513};key.frame.temporal=temporal;
    uint64_t generation=0;
    // Actual Dark Ages source-size sequence while Automatic NR stays 3840x2160.
    for(const auto extent:{VkExtent2D{2689,1513},{2634,1482},{2606,1466},{2490,1401},
                          {1921,1081},{2033,1144},{2139,1204},{1921,1081},{1996,1123},
                          {2139,1204},{1921,1081},{1996,1123},{2139,1204},{1921,1081}}) {
        auto desired=key;
        auto& t=*desired.frame.temporal;t.color=t.depth=t.motion={0,0,extent.width,extent.height};
        t.motionScaleX=3840.f/extent.width;t.motionScaleY=2160.f/extent.height;t.frameToken=serial;
        const auto cb=reinterpret_cast<VkCommandBuffer>(serial++);
        recordings.OnAllocate(device,pool,cb,0);recordings.OnBegin(cb,0,VK_SUCCESS);
        const auto use=*recordings.Reserve(cb,1);
        const auto ready=owner.Prepare(desired,use,{},true);
        Check(ready.ready&&owner.Commit(ready.generation),"dynamic Present guides remain ready");
        if(!generation)generation=ready.generation;
        Check(ready.generation==generation&&creates==1,"dynamic Present guides must not recreate fixed-size model");
        const auto evaluated=owner.Prepare(desired,use,{},false,true);
        Check(evaluated.ready&&evaluated.generation==generation,"same-frame inference uses retained model");
        recordings.OnEnd(cb,VK_SUCCESS);const auto receipt=*recordings.PrepareSubmission({queue,1,{use}});
        recordings.SubmissionReturned(receipt,VK_SUCCESS);recordings.CheckpointReturned(receipt,VK_SUCCESS);
        recordings.ObserveFence(receipt,VK_SUCCESS);recordings.OnReset(cb,VK_SUCCESS);
    }
    auto changed=key;changed.frame.temporal->depth={5,3,1921,1081};
    Check(CompatibleVkNrGeneration(key,changed),"live guide subrect origin is an evaluation parameter");
    auto native=key;native.frame.route=VkNrRoute::Native;changed=native;changed.frame.temporal->depth.width--;
    Check(!CompatibleVkNrGeneration(native,changed),"Native compatibility unchanged");
    for(unsigned control=0;control<12;++control) {
        auto first=key;changed=key;
        switch(control) {
        case 0:changed.frame.work.width-=8;break;
        case 1:changed.frame.output.width-=8;break;
        case 2:changed.frame.temporal->featureGeneration++;break;
        case 3:changed.frame.temporal->providerGeneration++;break;
        case 4:changed.frame.temporal->viewport++;break;
        case 5:changed.frame.temporal->depthInverted=true;break;
        case 6:changed.frame.temporal->featureFlags++;break;
        case 7:changed.frame.temporal.reset();break;
        case 8:first.hold=changed.hold=true;changed.frame.temporal->depth.width--;break;
        case 9:first.privateDlaa=changed.privateDlaa=true;changed.frame.temporal->depth.width--;break;
        case 10:changed.frame.temporal->motionScaleX=NAN;break;
        case 11:changed.frame.temporal->motion.width=0;break;
        }
        Check(!CompatibleVkNrGeneration(first,changed),"structural, held or invalid guide changes remain incompatible");
    }
    std::puts("Present dynamic-resolution sequence retains one model; Native, held inputs and structural guards preserved");
}
int main()
{
    PresentDynamicGuidesKeepModel();
    PreparationRetiresBeforeResize();
    {
        VkNrRecordingOwner r;auto device=reinterpret_cast<VkDevice>(1);
        auto queue=reinterpret_cast<VkQueue>(2);auto pool=reinterpret_cast<VkCommandPool>(3);
        uintptr_t serial=20;VkCommandBuffer cb{};
        auto use=[&]{cb=reinterpret_cast<VkCommandBuffer>(serial++);r.OnAllocate(device,pool,cb,0);r.OnBegin(cb,0,VK_SUCCESS);return *r.Reserve(cb,1);};
        unsigned creates=0;bool fail=false;
        VkNrGenerationOwner owner(r,[&](const auto&,VkNrUseId,uint64_t){++creates;
            return VkNrGenerationCreation{std::make_unique<Resource>(16),!fail,!fail,fail?"insufficient VRAM":""};},[](auto&){return true;});
        VkNrGenerationKey key;key.frame.route=VkNrRoute::Present;key.frame.queue=queue;
        key.frame.output=key.frame.work={32,32};key.frame.deviceGeneration=key.frame.routeEpoch=1;
        key.frame.representation.format=key.colorFormat=key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
        auto initial=use();auto first=owner.Prepare(key,initial);Check(first.ready&&owner.Commit(first.generation),"preparation baseline");
        r.OnEnd(cb,VK_SUCCESS);auto receipt=*r.PrepareSubmission({queue,1,{initial}});
        r.SubmissionReturned(receipt,VK_SUCCESS);r.CheckpointReturned(receipt,VK_SUCCESS);r.ObserveFence(receipt,VK_SUCCESS);r.OnReset(cb,VK_SUCCESS);
        auto probe=use();
        // A preparation-only lookup must not claim that healthy model histories were modified.
        auto inspect=[&]<class O>(O& o,const VkNrGenerationKey& k,VkNrUseId u){
            if constexpr(requires{o.Prepare(k,u,{},true);})return o.Prepare(k,u,{},true);
            else return o.Prepare(k,u);
        };
        Check(inspect(owner,key,probe).ready,"healthy prefix is available to prepare chain");
        r.OnReset(cb,VK_SUCCESS);owner.RetireCompleted(r);
        Check(owner.CanApply(key),"canceling preparation-only lookup must not poison healthy prefix");
        auto changed=key;changed.tuning.intensity=1.2f;fail=true;
        auto rejected=use();Check(inspect(owner,changed,rejected).failed,"allocation refusal recorded");
        r.OnReset(cb,VK_SUCCESS);auto retry=use();Check(inspect(owner,changed,retry).failed&&creates==2,"unchanged failed plan does not recreate every frame");
        changed.frame.settingsRevision++;
        Check(inspect(owner,changed,retry).failed&&creates==2,"observation revision does not bypass allocation refusal");
        owner.RetryPreparation();fail=false;
        Check(inspect(owner,changed,retry).ready&&creates==3,"structural chain change permits retry of refused layer");
        r.OnEnd(cb,VK_SUCCESS);auto createdReceipt=*r.PrepareSubmission({queue,1,{retry}});
        r.SubmissionReturned(createdReceipt,VK_SUCCESS);r.CheckpointReturned(createdReceipt,VK_SUCCESS);r.ObserveFence(createdReceipt,VK_SUCCESS);r.OnReset(cb,VK_SUCCESS);
        retry=use();
        changed.frame.resumeGeneration++;fail=false;
        Check(inspect(owner,changed,retry).ready,"explicit resume permits allocation retry");
        auto evaluationKey=changed;evaluationKey.tuning.intensity=0.7f;
        auto beforeCreates=creates;
        Check(owner.Prepare(evaluationKey,retry,{},false,true).pending&&creates==beforeCreates,
            "evaluation phase can never allocate a missing model");
    }
    ResumeRetiresBeforeReplacement();
    VkNrRecordingOwner recordings;
    const auto device=reinterpret_cast<VkDevice>(1);const auto queue=reinterpret_cast<VkQueue>(2);
    auto cb=reinterpret_cast<VkCommandBuffer>(3);auto pool=reinterpret_cast<VkCommandPool>(4);
    uintptr_t nextCb=10;std::vector<VkCommandBuffer> buffers;
    auto resetAll=[&] { for(auto buffer:buffers) recordings.OnReset(buffer,VK_SUCCESS); };
    auto use=[&](VkCommandBufferUsageFlags flags=0) { cb=reinterpret_cast<VkCommandBuffer>(nextCb++);buffers.push_back(cb);recordings.OnAllocate(device,pool,cb,0);recordings.OnBegin(cb,flags,VK_SUCCESS);return *recordings.Reserve(cb,1); };
    auto complete=[&](VkNrUseId id) { recordings.OnEnd(cb,VK_SUCCESS);auto receipt=recordings.PrepareSubmission({queue,1,{id}});
        Check(receipt.has_value(),"submission known");recordings.SubmissionReturned(*receipt,VK_SUCCESS);
        recordings.CheckpointReturned(*receipt,VK_SUCCESS);recordings.ObserveFence(*receipt,VK_SUCCESS); };
    bool failCreate=false,failRelease=false;unsigned released=0;
    VkNrGenerationOwner owner(recordings,[&](const auto&,VkNrUseId,uint64_t budget) {
        Check(budget>=16,"remaining actual private budget");
        return VkNrGenerationCreation{std::make_unique<Resource>(16),!failCreate,true,failCreate?"vendor creation failed":""};
    },[&](VkNrGenerationPayload&) { if(failRelease)return false;++released;return true; },8,128);
    VkNrGenerationKey key;key.frame.output=key.frame.work={32,32};key.frame.deviceGeneration=1;
    key.frame.routeEpoch=1;key.frame.representation.format=key.colorFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    auto firstUse=use();auto first=owner.Prepare(key,firstUse);
    Check(first.ready&&owner.Commit(first.generation),"first generation usable in its initialization recording");
    complete(firstUse);
    auto secondUse=use();auto resize=key;resize.frame.work={64,64};auto next=owner.Prepare(resize,secondUse);
    Check(next.ready&&owner.Commit(next.generation)&&owner.Count()==2,"resize keeps old recorded resources");
    owner.RetireCompleted(recordings);Check(owner.Count()==2&&released==0,"completed executable CB still retains images");
    Check(!owner.CanApply(key),"incompatible old generation cannot record");
    complete(secondUse);resetAll();owner.RetireCompleted(recordings);
    Check(owner.Count()==1&&released==1,"retired generation released only after actual terminality");
    auto failedUse=use();auto tune=resize;tune.tuning.intensity=1.1f;failCreate=true;
    auto failed=owner.Prepare(tune,failedUse);Check(!failed.ready&&failed.failed&&owner.AppliedKey().has_value()&&owner.CanApply(tune),"failed compatible create preserves applied");
    Check(owner.Count()==2,"failed vendor recording remains owned");failCreate=false;
    complete(failedUse);resetAll();owner.RetireCompleted(recordings);
    Check(owner.Count()==1,"failed recording recovers after terminality");
    for(unsigned i=0;i<9;++i) { auto id=use();tune.tuning.intensity=float(i)/10;
        auto prepared=owner.Prepare(tune,id);Check(prepared.ready&&owner.Commit(prepared.generation),"ninth tuning recovers without restart");
        complete(id);resetAll();owner.RetireCompleted(recordings); }
    auto pendingUse=use();auto format=tune;format.colorFormat=VK_FORMAT_B8G8R8A8_UNORM;
    auto formatReady=owner.Prepare(format,pendingUse);Check(formatReady.ready&&owner.Commit(formatReady.generation)&&!owner.CanApply(tune),"format change has independent resources");
    owner.Disable();failRelease=true;owner.RetireCompleted(recordings);
    Check(owner.Count()>0,"checked vendor release failure retains allocations");
    complete(pendingUse);resetAll();owner.RetireCompleted(recordings);Check(owner.Count()>0,"release error stays owned after drain");
    failRelease=false;owner.RetireCompleted(recordings);Check(owner.Count()==0&&owner.PrivateBytes()==0,"disable drains proven released generations");
    auto cancelledUse=use();auto cancelled=owner.Prepare(key,cancelledUse);
    Check(cancelled.ready&&owner.Commit(cancelled.generation),"cancel fixture owns initialized generation");
    recordings.OnReset(cb,VK_SUCCESS);owner.RetireCompleted(recordings);
    auto retryUse=use();auto retry=owner.Prepare(key,retryUse);
    Check(retry.ready&&retry.generation!=cancelled.generation,"unsubmitted initialization is rebuilt without invented layouts");
    Check(owner.Commit(retry.generation),"rebuilt generation committed");complete(retryUse);
    auto discardedUse=use();auto reused=owner.Prepare(key,discardedUse);Check(reused.ready,"completed model reused");
    recordings.OnReset(cb,VK_SUCCESS);owner.RetireCompleted(recordings);
    Check(!owner.CanApply(key),"discarded image-write recording invalidates predicted layouts");
    owner.Disable();resetAll();owner.RetireCompleted(recordings);
    VkNrGenerationOwner pressure(recordings,[&](const auto&,VkNrUseId,uint64_t budget) {
        Check(budget>=16,"retained generations included in budget");return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};
    },[&](VkNrGenerationPayload&) { ++released;return true; },8,128);
    std::vector<VkNrUseId> inFlight;
    for(unsigned i=0;i<8;++i) {
        auto id=use();inFlight.push_back(id);auto desired=key;desired.tuning.intensity=float(i)/10;
        auto prepared=pressure.Prepare(desired,id);Check(prepared.ready&&pressure.Commit(prepared.generation),"eight pending histories owned");
        recordings.OnEnd(cb,VK_SUCCESS);
    }
    auto ninthUse=use();auto ninthKey=key;ninthKey.tuning.intensity=1.4f;
    auto ninth=pressure.Prepare(ninthKey,ninthUse);Check(ninth.pending&&!ninth.ready&&pressure.Count()==8&&pressure.PrivateBytes()==128,"ninth tuning waits without eviction or allocation");
    auto many=recordings.PrepareSubmission({queue,1,inFlight});Check(many.has_value(),"pending histories really submitted");
    recordings.SubmissionReturned(*many,VK_SUCCESS);recordings.CheckpointReturned(*many,VK_SUCCESS);recordings.ObserveFence(*many,VK_SUCCESS);
    for(size_t i=buffers.size()-9;i<buffers.size()-1;++i) recordings.OnReset(buffers[i],VK_SUCCESS);
    ninth=pressure.Prepare(ninthKey,ninthUse);Check(ninth.ready&&pressure.Commit(ninth.generation),"ninth tuning admitted after actual old-use retirement");
    const auto releaseBeforeLoss=released;pressure.AbandonDevice();
    Check(pressure.Count()==0&&released==releaseBeforeLoss,"device loss never invokes dead provider release");
    VkNrResetRevision reset;auto old=reset.Capture();reset.Request();reset.Complete(old);Check(reset.Pending(),"old reset return cannot clear newer request");reset.Complete(reset.Capture());Check(!reset.Pending(),"matching reset completes");
    for(uint64_t initial:{0ull,1ull})
    {
        VkNrResetRevision resetOwner(initial);
        Check(resetOwner.Pending()&&resetOwner.Capture()==initial,"session0 and model1 initial pending revisions preserved");
        Check(resetOwner.Acknowledge(initial)&&!resetOwner.Pending(),"matching acknowledgment clears current owner");
        resetOwner.MarkPending();
        Check(resetOwner.Pending()&&resetOwner.Capture()==initial,"pending-only event reopens acknowledgment without revision increment");
        const auto captured=resetOwner.Capture();resetOwner.Request();
        Check(resetOwner.Capture()==captured+1&&!resetOwner.Acknowledge(captured)&&resetOwner.Pending(),"new request increments once and defeats stale acknowledgment");
        Check(resetOwner.Acknowledge(resetOwner.Capture())&&!resetOwner.Pending(),"new matching request acknowledgment clears pending");
    }
    VkNrResetRevision independent;independent.Complete(independent.Capture());
    reset.Request();Check(reset.Pending()&&!independent.Pending(),"request on one pass does not reopen another pass reset");
    independent.Request();independent.Request();const auto independentRevision=independent.Capture();
    Check(reset.Capture()==independentRevision,"independent pass owners can have coincident numerical revisions");
    reset.Complete(reset.Capture());Check(!reset.Pending()&&independent.Pending()&&independent.Capture()==independentRevision,
        "acknowledgment stays within its actual owner even when revisions coincide");
    auto variant=key;variant.frame.settingsRevision++;Check(variant==key,"composition revision excluded");
    VkNrTemporalMetadata temporal;temporal.color=temporal.depth=temporal.motion={0,0,32,32};key.frame.temporal=temporal;variant=key;
    variant.frame.temporal->jitterX=1;variant.frame.temporal->frameToken=9;variant.frame.temporal->reset=true;Check(variant==key,"frame motion sample and reset excluded");
    variant.frame.temporal->motionScaleX=2;Check(!(variant==key),"guide convention part of generation key");
    variant=key;variant.frame.route=VkNrRoute::Present;Check(!(variant==key),"route ownership independent");
    key.frame.queue=queue;
    VkNrGenerationOwner delayed(recordings,[](const auto&,VkNrUseId,uint64_t){return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};},
        [](VkNrGenerationPayload&){return true;});
    auto delayedUse=use();auto ready=delayed.Prepare(key,delayedUse);Check(ready.ready&&delayed.Commit(ready.generation),"delayed initialized model owned");
    recordings.OnEnd(cb,VK_SUCCESS);
    auto receipt=*recordings.PrepareSubmission({queue,1,{delayedUse}});
    recordings.SubmissionReturned(receipt,VK_SUCCESS);recordings.CheckpointReturned(receipt,VK_SUCCESS);
    auto currentUse=use();unsigned waits=0;
    auto waited=PrepareWaiting(delayed,key,currentUse,[&](VkNrUseId old){++waits;Check(old==delayedUse,"wait targets exact owned predecessor");recordings.ObserveFence(receipt,VK_SUCCESS);});
    Check(waited.ready&&waited.generation==ready.generation&&waits==1,
        "pending previous model completes through owned wait instead of skipping alternating frames");
    recordings.OnEnd(cb,VK_SUCCESS);
    auto unsubmitted=use();auto blocked=PrepareWaiting(delayed,key,unsubmitted,[&](VkNrUseId){++waits;});
    Check(blocked.pending&&!blocked.ready&&waits==1,"unsubmitted recording cannot be waited or assumed complete");
    auto secondReceipt=*recordings.PrepareSubmission({queue,1,{currentUse}});
    recordings.SubmissionReturned(secondReceipt,VK_SUCCESS);recordings.CheckpointReturned(secondReceipt,VK_SUCCESS);
    auto timedOut=PrepareWaiting(delayed,key,unsubmitted,[&](VkNrUseId){++waits;});
    Check(timedOut.pending&&!timedOut.ready&&waits==2,"timeout cannot fabricate predecessor completion");
    recordings.ObserveFence(secondReceipt,VK_SUCCESS);
    auto recovered=PrepareWaiting(delayed,key,unsubmitted,[&](VkNrUseId){++waits;});
    Check(recovered.ready&&waits==2,"actual completion recovers the same temporal history");
    // A Native command recording has a family before it has a submission queue.
    auto familyKey=key;familyKey.frame.queue=VK_NULL_HANDLE;SetFamily(familyKey.frame,0);
    VkNrGenerationOwner familyOwner(recordings,[](const auto&,VkNrUseId,uint64_t){return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};},
        [](VkNrGenerationPayload&){return true;});
    const auto familyUse=use();auto familyReady=familyOwner.Prepare(familyKey,familyUse);
    const auto familyBuffer=cb;
    Check(familyReady.ready&&familyOwner.Commit(familyReady.generation),"family recording initializes its own resources");
    recordings.OnEnd(cb,VK_SUCCESS);
    const auto familyReceipt=*recordings.PrepareSubmission({queue,1,{familyUse}});
    recordings.SubmissionReturned(familyReceipt,VK_SUCCESS);recordings.CheckpointReturned(familyReceipt,VK_SUCCESS);
    const auto familyNext=use();unsigned familyWaits=0;
    auto retained=PrepareWaiting(familyOwner,familyKey,familyNext,[&](VkNrUseId){++familyWaits;});
    Check(retained.pending&&!retained.ready&&familyWaits==0,"family reuse does not wait on a replayable predecessor");
    recordings.OnReset(familyBuffer,VK_SUCCESS);
    const auto familyResult=PrepareWaiting(familyOwner,familyKey,familyNext,[&](VkNrUseId old){
        Check(old==familyUse,"family history waits the actual submitted predecessor");++familyWaits;
        recordings.ObserveFence(familyReceipt,VK_SUCCESS);
    });
    Check(familyResult.ready&&familyResult.generation==familyReady.generation&&familyWaits==1,
        "same-family unknown next queue can reuse history only after actual predecessor completion");
    auto otherFamily=familyKey;SetFamily(otherFamily.frame,2);
    Check(!(otherFamily==familyKey),"different queue families cannot share exclusive history images");
    Check(familyOwner.Prepare(otherFamily,familyNext).failed,"recording must match actual family");
    recordings.OnEnd(cb,VK_SUCCESS);
    auto replayReceipt=*recordings.PrepareSubmission({reinterpret_cast<VkQueue>(888),1,{familyNext}});
    recordings.SubmissionReturned(replayReceipt,VK_SUCCESS);recordings.CheckpointReturned(replayReceipt,VK_SUCCESS);
    recordings.ObserveFence(replayReceipt,VK_SUCCESS);
    const auto replayBuffer=cb;const auto afterReplay=use();
    Check(!familyOwner.Prepare(familyKey,afterReplay).ready&&!familyOwner.ReadyForUse(familyReady.generation,afterReplay),
        "completed but executable cross-queue recording blocks both normal and fallback history reuse");
    Check(recordings.AddConsumerHold(familyNext),"hold family predecessor for external consumer");
    recordings.OnReset(replayBuffer,VK_SUCCESS);
    Check(!familyOwner.Prepare(familyKey,afterReplay).ready,"consumer hold blocks family reuse after reset and completion");
    recordings.RemoveConsumerHold(familyNext);
    Check(familyOwner.Prepare(familyKey,afterReplay).ready,"closed complete predecessor can share history across actual queues");
    // Production Native now admits one-time primary recordings. Waiting on its
    // accepted owned fence must be allowed before that very fence closes recording rights.
    VkNrGenerationOwner singleUseOwner(recordings,[](const auto&,VkNrUseId,uint64_t){return VkNrGenerationCreation{std::make_unique<Resource>(16),true,true,{}};},
        [](VkNrGenerationPayload&){return true;});
    auto singleUse=use(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    const auto singleReady=singleUseOwner.Prepare(familyKey,singleUse);
    Check(singleReady.ready&&singleUseOwner.Commit(singleReady.generation),"one-time family history initialized");
    unsigned singleWaits=0;
    for(unsigned frame=0;frame<6;++frame) {
        recordings.OnEnd(cb,VK_SUCCESS);
        const auto submitted=*recordings.PrepareSubmission({queue,1,{singleUse}});
        recordings.SubmissionReturned(submitted,VK_SUCCESS);recordings.CheckpointReturned(submitted,VK_SUCCESS);
        Check(!recordings.RecordingReleased(singleUse),"one-time predecessor still held until actual fence completion");
        const auto nextUse=use(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        const auto timeout=PrepareWaiting(singleUseOwner,familyKey,nextUse,[&](VkNrUseId old){
            Check(old==singleUse,"bounded wait targets exact one-time predecessor");++singleWaits;
        });
        Check(timeout.pending&&!timeout.ready&&singleWaits==frame*2+1,"one-time predecessor receives bounded wait without invented completion");
        const auto completed=PrepareWaiting(singleUseOwner,familyKey,nextUse,[&](VkNrUseId old){
            Check(old==singleUse,"completion belongs to exact earlier submission");++singleWaits;
            recordings.ObserveFence(submitted,VK_SUCCESS);
        });
        Check(completed.ready&&completed.generation==singleReady.generation&&singleWaits==frame*2+2,
            "one-time family Native history advances every completed frame without alternating skipped frames");
        Check(recordings.RecordingReleased(singleUse)&&recordings.GpuComplete(singleUse),"reuse retains both recording release and completion proofs");
        singleUse=nextUse;
    }
    const auto unsubmittedBuffer=cb;
    const auto consumerNext=use(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    unsigned finalWaits=0;
    Check(!PrepareWaiting(singleUseOwner,familyKey,consumerNext,[&](VkNrUseId){++finalWaits;}).ready&&finalWaits==0,
        "one-time recording without an accepted submission cannot be waited");
    recordings.OnEnd(unsubmittedBuffer,VK_SUCCESS);
    const auto consumedReceipt=*recordings.PrepareSubmission({queue,1,{singleUse}});
    recordings.SubmissionReturned(consumedReceipt,VK_SUCCESS);recordings.CheckpointReturned(consumedReceipt,VK_SUCCESS);
    Check(recordings.AddConsumerHold(singleUse),"one-time predecessor consumer retained");
    const auto held=PrepareWaiting(singleUseOwner,familyKey,consumerNext,[&](VkNrUseId){
        ++finalWaits;recordings.ObserveFence(consumedReceipt,VK_SUCCESS);
    });
    Check(!held.ready&&finalWaits==1&&recordings.RecordingReleased(singleUse)&&recordings.GpuComplete(singleUse),
        "owned fence completion never releases an independent consumer");
    recordings.RemoveConsumerHold(singleUse);
    Check(singleUseOwner.Prepare(familyKey,consumerNext).ready,"consumer release allows history recovery");
    std::puts("transactional generations, compatible failure, retirement, capacity recovery and revisioned resets verified");
}
