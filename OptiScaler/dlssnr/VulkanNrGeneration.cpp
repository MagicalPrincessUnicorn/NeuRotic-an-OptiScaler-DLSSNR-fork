#include "VulkanNrGeneration.h"
#include <algorithm>
#include <bit>
#include <type_traits>
namespace DlssNr
{
namespace {
bool SameTemporal(const std::optional<VkNrTemporalMetadata>& a,const std::optional<VkNrTemporalMetadata>& b,
                  bool livePresentGuides)
{
    if(a.has_value()!=b.has_value())return false;
    // Present allocates model images from output/work sizes, not guide rasters.
    // Current guide rectangles/scales are validated and written on each evaluation.
    // Treating dynamic resolution as model creation state caused repeated blank
    // preparation frames even with a fixed Automatic/Manual processing size.
    const bool sameSampling=!a||(a->color==b->color&&a->depth==b->depth&&a->motion==b->motion&&
        a->motionScaleX==b->motionScaleX&&a->motionScaleY==b->motionScaleY);
    return !a||((sameSampling||(livePresentGuides&&ValidVkNrTemporal(*a)&&ValidVkNrTemporal(*b)))&&
        a->featureFlags==b->featureFlags&&a->depthInverted==b->depthInverted&&a->featureGeneration==b->featureGeneration&&a->providerFrameKnown==b->providerFrameKnown&&a->providerGeneration==b->providerGeneration&&a->viewport==b->viewport);
}
}
bool CompatibleVkNrGeneration(const VkNrGenerationKey& a,const VkNrGenerationKey& b)
{
    const auto& x=a.frame;const auto& y=b.frame;
    return x.route==y.route&&x.placement==y.placement&&x.deviceGeneration==y.deviceGeneration&&
        x.routeEpoch==y.routeEpoch&&x.swapchainGeneration==y.swapchainGeneration&&
        x.queue==y.queue&&x.queueFamily==y.queueFamily&&x.resumeGeneration==y.resumeGeneration&&x.guideGeneration==y.guideGeneration&&
        x.inputInterruptionEpoch==y.inputInterruptionEpoch&&
        x.output.width==y.output.width&&x.output.height==y.output.height&&
        x.work.width==y.work.width&&x.work.height==y.work.height&&x.representation==y.representation&&
        SameTemporal(x.temporal,y.temporal,x.route==VkNrRoute::Present&&y.route==VkNrRoute::Present&&
            x.placement==VkNrPlacement::AfterSR&&y.placement==VkNrPlacement::AfterSR&&
            !a.hold&&!b.hold&&!a.privateDlaa&&!b.privateDlaa)&&a.colorFormat==b.colorFormat&&a.depthFormat==b.depthFormat&&
        a.motionFormat==b.motionFormat&&a.targetFormat==b.targetFormat&&a.gameHdr==b.gameHdr&&
        a.hold==b.hold&&a.creationFlags==b.creationFlags&&a.passIndex==b.passIndex&&a.privateDlaa==b.privateDlaa&&
        a.srOutput.width==b.srOutput.width&&a.srOutput.height==b.srOutput.height&&
        a.srQuality==b.srQuality&&a.preSrEpoch==b.preSrEpoch;
}
bool VkNrGenerationKey::operator==(const VkNrGenerationKey& b) const
{ return CompatibleVkNrGeneration(*this,b)&&upFilter==b.upFilter&&downFilter==b.downFilter&&tuning==b.tuning; }
namespace {
template<class T> uint64_t LossBits(T value) noexcept
{
    if constexpr(std::is_same_v<T,float>)return std::bit_cast<uint32_t>(value);
    else if constexpr(std::is_pointer_v<T>)return reinterpret_cast<uintptr_t>(value);
    else return static_cast<uint64_t>(value);
}
}
VkNrKeyDifference FirstVkNrGenerationDifference(const VkNrGenerationKey& a,const VkNrGenerationKey& b) noexcept
{
    const auto& x=a.frame;const auto& y=b.frame;
#define NR_LOSS_DIFF(left,right,name) if(!((left)==(right)))return {name,LossBits(left),LossBits(right)}
    NR_LOSS_DIFF(x.route,y.route,"route");NR_LOSS_DIFF(x.placement,y.placement,"placement");
    NR_LOSS_DIFF(x.deviceGeneration,y.deviceGeneration,"deviceGeneration");
    NR_LOSS_DIFF(x.routeEpoch,y.routeEpoch,"routeEpoch");NR_LOSS_DIFF(x.swapchainGeneration,y.swapchainGeneration,"swapchainGeneration");
    NR_LOSS_DIFF(x.queue,y.queue,"queue");NR_LOSS_DIFF(x.queueFamily,y.queueFamily,"queueFamily");
    NR_LOSS_DIFF(x.resumeGeneration,y.resumeGeneration,"resumeGeneration");NR_LOSS_DIFF(x.guideGeneration,y.guideGeneration,"guideGeneration");
    NR_LOSS_DIFF(x.inputInterruptionEpoch,y.inputInterruptionEpoch,"inputInterruptionEpoch");
    NR_LOSS_DIFF(x.output.width,y.output.width,"output.width");NR_LOSS_DIFF(x.output.height,y.output.height,"output.height");
    NR_LOSS_DIFF(x.work.width,y.work.width,"work.width");NR_LOSS_DIFF(x.work.height,y.work.height,"work.height");
    NR_LOSS_DIFF(x.representation.format,y.representation.format,"representation.format");
    NR_LOSS_DIFF(x.representation.colorSpace,y.representation.colorSpace,"representation.colorSpace");
    NR_LOSS_DIFF(x.representation.recipeRevision,y.representation.recipeRevision,"representation.recipeRevision");
    NR_LOSS_DIFF(x.temporal.has_value(),y.temporal.has_value(),"temporal.presence");
    if(x.temporal){
        const auto& p=*x.temporal;const auto& q=*y.temporal;
        const bool live=x.route==VkNrRoute::Present&&y.route==VkNrRoute::Present&&
            x.placement==VkNrPlacement::AfterSR&&y.placement==VkNrPlacement::AfterSR&&
            !a.hold&&!b.hold&&!a.privateDlaa&&!b.privateDlaa;
        const bool sameSampling=p.color==q.color&&p.depth==q.depth&&p.motion==q.motion&&
            p.motionScaleX==q.motionScaleX&&p.motionScaleY==q.motionScaleY;
        if(!(sameSampling||(live&&ValidVkNrTemporal(p)&&ValidVkNrTemporal(q)))){
#define NR_LOSS_RECT(rect) NR_LOSS_DIFF(p.rect.x,q.rect.x,"temporal." #rect ".x");NR_LOSS_DIFF(p.rect.y,q.rect.y,"temporal." #rect ".y");NR_LOSS_DIFF(p.rect.width,q.rect.width,"temporal." #rect ".width");NR_LOSS_DIFF(p.rect.height,q.rect.height,"temporal." #rect ".height")
            NR_LOSS_RECT(color);NR_LOSS_RECT(depth);NR_LOSS_RECT(motion);
#undef NR_LOSS_RECT
            NR_LOSS_DIFF(p.motionScaleX,q.motionScaleX,"temporal.motionScaleX");
            NR_LOSS_DIFF(p.motionScaleY,q.motionScaleY,"temporal.motionScaleY");
        }
        NR_LOSS_DIFF(p.featureFlags,q.featureFlags,"temporal.featureFlags");
        NR_LOSS_DIFF(p.depthInverted,q.depthInverted,"temporal.depthInverted");
        NR_LOSS_DIFF(p.featureGeneration,q.featureGeneration,"temporal.featureGeneration");
        NR_LOSS_DIFF(p.providerFrameKnown,q.providerFrameKnown,"temporal.providerFrameKnown");
        NR_LOSS_DIFF(p.providerGeneration,q.providerGeneration,"temporal.providerGeneration");
        NR_LOSS_DIFF(p.viewport,q.viewport,"temporal.viewport");
    }
    NR_LOSS_DIFF(a.colorFormat,b.colorFormat,"colorFormat");NR_LOSS_DIFF(a.depthFormat,b.depthFormat,"depthFormat");
    NR_LOSS_DIFF(a.motionFormat,b.motionFormat,"motionFormat");NR_LOSS_DIFF(a.targetFormat,b.targetFormat,"targetFormat");
    NR_LOSS_DIFF(a.gameHdr,b.gameHdr,"gameHdr");NR_LOSS_DIFF(a.hold,b.hold,"hold");
    NR_LOSS_DIFF(a.creationFlags,b.creationFlags,"creationFlags");NR_LOSS_DIFF(a.passIndex,b.passIndex,"passIndex");
    NR_LOSS_DIFF(a.privateDlaa,b.privateDlaa,"privateDlaa");
    NR_LOSS_DIFF(a.srOutput.width,b.srOutput.width,"srOutput.width");NR_LOSS_DIFF(a.srOutput.height,b.srOutput.height,"srOutput.height");
    NR_LOSS_DIFF(a.srQuality,b.srQuality,"srQuality");NR_LOSS_DIFF(a.preSrEpoch,b.preSrEpoch,"preSrEpoch");
    NR_LOSS_DIFF(a.upFilter,b.upFilter,"upFilter");NR_LOSS_DIFF(a.downFilter,b.downFilter,"downFilter");
    NR_LOSS_DIFF(a.tuning.preset,b.tuning.preset,"tuning.preset");NR_LOSS_DIFF(a.tuning.style,b.tuning.style,"tuning.style");
    NR_LOSS_DIFF(a.tuning.intensity,b.tuning.intensity,"tuning.intensity");NR_LOSS_DIFF(a.tuning.structure,b.tuning.structure,"tuning.structure");
    NR_LOSS_DIFF(a.tuning.tone,b.tuning.tone,"tuning.tone");NR_LOSS_DIFF(a.tuning.skin,b.tuning.skin,"tuning.skin");
    NR_LOSS_DIFF(a.tuning.mask,b.tuning.mask,"tuning.mask");
#undef NR_LOSS_DIFF
    return {};
}
void VkNrGenerationOwner::ObserveLoss(VkNrGenerationLoss* out,VkNrGenerationLossCause cause,
    const Entry& entry,const char* origin,VkNrUseId use,bool reusableObserved,bool reusable,
    bool gpuObserved,bool gpu,const VkNrGenerationKey* requested)const noexcept
{
    if(!out||out->cause!=VkNrGenerationLossCause::None||applied_!=entry.id||!applied_)return;
    if(entry.key.frame.route!=VkNrRoute::Present)return;
    out->cause=cause;out->origin=origin;out->oldGeneration=entry.id;out->nextCandidate=next_;
    out->route=entry.key.frame.route;out->pass=entry.key.passIndex;
    out->deviceGeneration=entry.key.frame.deviceGeneration;out->routeEpoch=entry.key.frame.routeEpoch;
    out->resumeGeneration=entry.key.frame.resumeGeneration;out->swapchainGeneration=entry.key.frame.swapchainGeneration;
    out->use=use;out->reusableObserved=reusableObserved;out->reusable=reusable;
    out->gpuCompleteObserved=gpuObserved;out->gpuComplete=gpu;
    if(requested){
        if(cause==VkNrGenerationLossCause::PriorResume){
            const auto& before=entry.key.frame;const auto& after=requested->frame;
            out->difference=before.resumeGeneration!=after.resumeGeneration?
                VkNrKeyDifference{"resumeGeneration",before.resumeGeneration,after.resumeGeneration}:
                VkNrKeyDifference{"inputInterruptionEpoch",before.inputInterruptionEpoch,after.inputInterruptionEpoch};
        }else out->difference=FirstVkNrGenerationDifference(entry.key,*requested);
    }
    // A separate snapshot after the deciding predicate. Never readiness evidence.
    if(use){try{out->postPredicate=recordings_.InspectUseRecording(use);}catch(...){out->snapshotUnavailable=true;}}
}

VkNrGenerationOwner::VkNrGenerationOwner(VkNrRecordingOwner& r,Create c,Release d,size_t capacity,uint64_t budget)
    : recordings_(r),create_(std::move(c)),release_(std::move(d)),capacity_(capacity),budget_(budget) {}
VkNrGenerationOwner::~VkNrGenerationOwner() { AbandonDevice(); }
VkNrGenerationOwner::Entry* VkNrGenerationOwner::Find(uint64_t id)
{ auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});return i==entries_.end()?nullptr:&*i; }
const VkNrGenerationOwner::Entry* VkNrGenerationOwner::Find(uint64_t id) const
{ auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});return i==entries_.end()?nullptr:&*i; }
VkNrGenerationResult VkNrGenerationOwner::Prepare(const VkNrGenerationKey& key,VkNrUseId use,
    const std::function<void(VkNrUseId)>& wait,bool preparationOnly,bool existingOnly,VkNrGenerationLoss* diagnostic)
{
    RetireCompleted(recordings_,diagnostic);
    if(!use||!recordings_.UseDevice(use)||!ValidateVkNrFrameContract(key.frame)||!key.tuning.Valid()||key.passIndex>=10||
       (key.frame.queueFamily!=UINT32_MAX&&recordings_.UseFamily(use)!=key.frame.queueFamily))
        return {false,false,true,0,"invalid Vulkan generation contract"};
    // Resume requires a fresh opaque model session. Retire the previous session
    // before allocating its replacement, as the D3D12 path does. Completed work
    // alone cannot release executable recordings or independent consumer holds;
    // a failed vendor release also keeps the old entry as a retry barrier.
    const auto priorResume = [&](const Entry& entry) {
        return entry.key.frame.route==key.frame.route&&entry.key.passIndex==key.passIndex&&
            (entry.key.frame.resumeGeneration!=key.frame.resumeGeneration||
             entry.key.frame.inputInterruptionEpoch!=key.frame.inputInterruptionEpoch);
    };
    if(std::any_of(entries_.begin(),entries_.end(),priorResume)) {
        if(const auto* applied=Find(applied_);applied&&priorResume(*applied)){
            ObserveLoss(diagnostic,VkNrGenerationLossCause::PriorResume,*applied,"Prepare/prior_resume",{},false,false,false,false,&key);
            applied_=0;
        }
        RetireCompleted(recordings_,diagnostic);
        if(std::any_of(entries_.begin(),entries_.end(),priorResume))
            return {false,true,false,0,"previous Vulkan resume session awaits recording/GPU/consumer or vendor release"};
    }
    for(auto& e:entries_)if(e.key==key&&e.ready) {
        if(wait)for(auto old:e.uses) {
            if(old==use||recordings_.SafeToWriteAfter(old)||recordings_.Reusable(old))continue;
            const auto proof=recordings_.ProducerProof(old);
            const bool familyOnly=!key.frame.queue&&key.frame.route==VkNrRoute::Native&&key.frame.queueFamily!=UINT32_MAX;
            // One-time recording rights close when the owned fence is observed.
            // Requiring closure before waiting prevented Native from ever waiting
            // for that fence, causing alternate frames to lose their model pass.
            if(familyOnly&&!recordings_.RecordingReleased(old)&&!recordings_.SingleUsePrimary(old))continue;
            const bool ordering=familyOnly?recordings_.UseFamily(old)==key.frame.queueFamily:
                proof&&proof->queue==key.frame.queue;
            if(proof&&ordering&&proof->deviceGeneration==key.frame.deviceGeneration&&
               !recordings_.GpuComplete(old))wait(old);
        }
        if(e.initialization&&!e.initializationComplete) {
            if(recordings_.GpuComplete(e.initialization))e.initializationComplete=true;
            else if(e.initialization!=use)return {false,true,false,e.id,"model initialization awaits owned completion"};
        }
        VkNrUseId blocked;
        if(!ReadyForUse(e.id,use,&blocked)){
            VkNrGenerationResult result{false,true,false,e.id,"previous model/image use awaits owned completion"};
            // Bounded metadata only. DescribeProducer samples after the failed
            // predicate; it cannot establish ordering or change readiness.
            if(completionDiagnostics_<64){
                ++completionDiagnostics_;
                try{result.diagnostic="generation="+std::to_string(e.id)+" requestedUse="+std::to_string(use.value)+
                    " firstBlockedUse="+std::to_string(blocked.value)+" retainedUses="+std::to_string(e.uses.size())+
                    " familyOnly="+std::to_string(!e.key.frame.queue&&e.key.frame.route==VkNrRoute::Native&&e.key.frame.queueFamily!=UINT32_MAX)+
                    " snapshotAfterFailedPredicate=1 producer{"+recordings_.DescribeProducer(blocked)+"}";}
                catch(...){/* Diagnostic allocation cannot change readiness. */}
            }
            return result;
        }
        if(!preparationOnly&&!Retain(e.id,use))return {false,true,false,e.id,"recording use is unavailable"};
        return {true,false,false,e.id,{}};
    }
    if(existingOnly)return {false,true,false,0,"Vulkan model requires a separate preparation recording"};
    if(preparationOnly&&refusedKey_&&*refusedKey_==key)
        return {false,false,true,0,refusedReason_};
    // A preparation recording does not deliver the old configuration as fallback.
    // Retire it before creating its replacement so opaque provider allocations
    // cannot overlap during resolution/tuning changes. Actual recording, GPU and
    // consumer ownership (and a successful provider release) still govern drain.
    if(preparationOnly&&!entries_.empty()) {
        if(diagnostic)if(const auto* applied=Find(applied_))
            ObserveLoss(diagnostic,VkNrGenerationLossCause::PreparationReplacement,*applied,"Prepare/preparation_drain",{},false,false,false,false,&key);
        applied_=0;
        RetireCompleted(recordings_,diagnostic);
        if(!entries_.empty())
            return {false,true,false,0,"previous Vulkan model configuration awaits recording/GPU/consumer or vendor release"};
    }
    const auto count=std::count_if(entries_.begin(),entries_.end(),[&](auto& e){return e.key.passIndex==key.passIndex;});
    if(static_cast<size_t>(count)>=capacity_||entries_.size()>=80||bytes_>=budget_)
        return {false,true,false,0,"Vulkan generation capacity awaits recording/GPU release"};
    if(!recordings_.RetainUse(use))return {false,false,true,0,"unknown generation recording use"};
    auto creation=create_(key,use,budget_-bytes_);
    if(preparationOnly&&!creation.ready) {
        refusedKey_=key;
        refusedReason_=creation.reason+"; change model settings or switch NR off/on to retry";
        creation.reason=refusedReason_;
    } else if(creation.ready) {refusedKey_.reset();refusedReason_.clear();}
    if(!creation.payload) { recordings_.ReleaseUse(use);return {false,false,true,0,creation.reason}; }
    if(!creation.ready&&!creation.recorded&&release_(*creation.payload)) {
        recordings_.ReleaseUse(use);return {false,false,true,0,creation.reason};
    }
    const auto allocated=creation.payload->privateBytes;
    const bool validBudget=allocated<=budget_-bytes_;
    const auto id=next_++;
    if(diagnostic&&diagnostic->cause!=VkNrGenerationLossCause::None)diagnostic->allocatedReplacement=id;
    entries_.push_back({id,key,std::move(creation.payload),{use},creation.recorded?use:VkNrUseId{},
                       creation.ready&&validBudget,!creation.recorded});
    bytes_+=allocated;
    if(!entries_.back().ready)return {false,false,true,id,validBudget?creation.reason:"actual private allocation budget exceeded"};
    return {true,false,false,id,{}};
}
bool VkNrGenerationOwner::Commit(uint64_t id)
{ auto* e=Find(id);if(!e||!e->ready)return false;applied_=id;return true; }
bool VkNrGenerationOwner::Retain(uint64_t id,VkNrUseId use)
{
    auto* e=Find(id);if(!e||!use)return false;
    if(std::find(e->uses.begin(),e->uses.end(),use)!=e->uses.end())return true;
    if(e->uses.size()>=1024||!recordings_.RetainUse(use))return false;
    e->uses.push_back(use);return true;
}
void VkNrGenerationOwner::RetireCompleted(const VkNrRecordingOwner& owner,VkNrGenerationLoss* diagnostic)
{
    if(&owner!=&recordings_)return;
    for(auto i=entries_.begin();i!=entries_.end();) {
        const bool initializationGpuComplete=i->initialization&&owner.GpuComplete(i->initialization);
        if(initializationGpuComplete)i->initializationComplete=true;
        if(i->ready&&!i->initializationComplete&&owner.Reusable(i->initialization)) {
            ObserveLoss(diagnostic,VkNrGenerationLossCause::InitializationUnready,*i,"RetireCompleted/initialization",
                i->initialization,true,true,bool(i->initialization),initializationGpuComplete);
            i->ready=false;if(applied_==i->id)applied_=0;
        }
        // Layouts/history advance while recording. A canceled unsubmitted recording did not
        // execute those transitions; discard its generation after all remaining references end.
        if(i->ready&&std::any_of(i->uses.begin(),i->uses.end(),[&](auto use) {
            const bool reusable=owner.Reusable(use);if(!reusable)return false;
            const bool gpuComplete=owner.GpuComplete(use);
            if(!gpuComplete)ObserveLoss(diagnostic,VkNrGenerationLossCause::RetainedUseUnready,*i,
                "RetireCompleted/retained_use",use,true,reusable,true,gpuComplete);
            return !gpuComplete;
        })) { i->ready=false;if(applied_==i->id)applied_=0; }
        // Keep initialization evidence while it can still determine readiness.
        for(auto u=i->uses.begin();u!=i->uses.end();) {
            if(owner.Reusable(*u)&&(*u!=i->initialization||i->initializationComplete||!i->ready)) {
                recordings_.ReleaseUse(*u);u=i->uses.erase(u);
            } else ++u;
        }
        if(i->id!=applied_&&i->uses.empty()) {
            // Release may dismantle a feature before a later parameter release
            // fails. Keep that payload owned for retry, never usable as a model.
            i->ready=false;
            if(release_(*i->payload)) {
                bytes_-=i->payload->privateBytes;i=entries_.erase(i);continue;
            }
        }
        ++i;
    }
}
std::optional<VkNrGenerationKey> VkNrGenerationOwner::AppliedKey() const
{ auto* e=Find(applied_);return e?std::optional(e->key):std::nullopt; }
bool VkNrGenerationOwner::CanApply(const VkNrGenerationKey& key) const
{ auto* e=Find(applied_);return e&&e->ready&&CompatibleVkNrGeneration(e->key,key); }
bool VkNrGenerationOwner::ReadyForUse(uint64_t id,VkNrUseId use,VkNrUseId* firstBlocked) const
{
    if(firstBlocked)*firstBlocked={};
    auto* e=Find(id);if(!e||!e->ready)return false;
    const bool familyOnly=!e->key.frame.queue&&e->key.frame.route==VkNrRoute::Native&&e->key.frame.queueFamily!=UINT32_MAX;
    return std::all_of(e->uses.begin(),e->uses.end(),[&](auto old){
        const bool ready=old==use||recordings_.Reusable(old)||
            (recordings_.SafeToWriteAfter(old)&&(!familyOnly||recordings_.RecordingReleased(old)));
        if(!ready&&firstBlocked)*firstBlocked=old;
        return ready;
    });
}
VkNrGenerationPayload* VkNrGenerationOwner::Payload(uint64_t id) const
{ auto* e=Find(id);return e?e->payload.get():nullptr; }
void VkNrGenerationOwner::Disable(const char* origin,VkNrGenerationLoss* diagnostic)
{
    if(diagnostic)if(const auto* applied=Find(applied_))
        ObserveLoss(diagnostic,VkNrGenerationLossCause::ExplicitDisable,*applied,origin);
    applied_=0;
}
bool VkNrGenerationOwner::DrainReleased(VkNrGenerationLoss* diagnostic)
{ Disable("DrainReleased",diagnostic);RetireCompleted(recordings_,diagnostic);return entries_.empty(); }
void VkNrGenerationOwner::AbandonDevice()
{
    // Payload destructors must not execute driver destruction after device loss or an unproved drain.
    for(auto& e:entries_) { for(auto u:e.uses)recordings_.ReleaseUse(u);e.payload->Abandon(); }
    entries_.clear();bytes_=0;applied_=0;
}
}
