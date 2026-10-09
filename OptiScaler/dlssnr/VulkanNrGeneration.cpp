#include "VulkanNrGeneration.h"
#include <algorithm>
namespace DlssNr
{
namespace {
bool SameTemporal(const std::optional<VkNrTemporalMetadata>& a,const std::optional<VkNrTemporalMetadata>& b)
{
    if(a.has_value()!=b.has_value())return false;
    return !a||(a->color==b->color&&a->depth==b->depth&&a->motion==b->motion&&
        a->motionScaleX==b->motionScaleX&&a->motionScaleY==b->motionScaleY&&
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
        SameTemporal(x.temporal,y.temporal)&&a.colorFormat==b.colorFormat&&a.depthFormat==b.depthFormat&&
        a.motionFormat==b.motionFormat&&a.targetFormat==b.targetFormat&&a.gameHdr==b.gameHdr&&
        a.hold==b.hold&&a.creationFlags==b.creationFlags&&a.passIndex==b.passIndex&&a.privateDlaa==b.privateDlaa&&
        a.srOutput.width==b.srOutput.width&&a.srOutput.height==b.srOutput.height&&
        a.srQuality==b.srQuality&&a.preSrEpoch==b.preSrEpoch;
}
bool VkNrGenerationKey::operator==(const VkNrGenerationKey& b) const
{ return CompatibleVkNrGeneration(*this,b)&&upFilter==b.upFilter&&downFilter==b.downFilter&&tuning==b.tuning; }
VkNrGenerationOwner::VkNrGenerationOwner(VkNrRecordingOwner& r,Create c,Release d,size_t capacity,uint64_t budget)
    : recordings_(r),create_(std::move(c)),release_(std::move(d)),capacity_(capacity),budget_(budget) {}
VkNrGenerationOwner::~VkNrGenerationOwner() { AbandonDevice(); }
VkNrGenerationOwner::Entry* VkNrGenerationOwner::Find(uint64_t id)
{ auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});return i==entries_.end()?nullptr:&*i; }
const VkNrGenerationOwner::Entry* VkNrGenerationOwner::Find(uint64_t id) const
{ auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});return i==entries_.end()?nullptr:&*i; }
VkNrGenerationResult VkNrGenerationOwner::Prepare(const VkNrGenerationKey& key,VkNrUseId use,
    const std::function<void(VkNrUseId)>& wait,bool preparationOnly,bool existingOnly)
{
    RetireCompleted(recordings_);
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
        if(const auto* applied=Find(applied_);applied&&priorResume(*applied))applied_=0;
        RetireCompleted(recordings_);
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
        if(!ReadyForUse(e.id,use))return {false,true,false,e.id,"previous model/image use awaits owned completion"};
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
        applied_=0;
        RetireCompleted(recordings_);
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
void VkNrGenerationOwner::RetireCompleted(const VkNrRecordingOwner& owner)
{
    if(&owner!=&recordings_)return;
    for(auto i=entries_.begin();i!=entries_.end();) {
        if(i->initialization&&owner.GpuComplete(i->initialization))i->initializationComplete=true;
        if(i->ready&&!i->initializationComplete&&owner.Reusable(i->initialization)) {
            i->ready=false;if(applied_==i->id)applied_=0;
        }
        // Layouts/history advance while recording. A canceled unsubmitted recording did not
        // execute those transitions; discard its generation after all remaining references end.
        if(i->ready&&std::any_of(i->uses.begin(),i->uses.end(),[&](auto use) {
            return owner.Reusable(use)&&!owner.GpuComplete(use);
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
bool VkNrGenerationOwner::ReadyForUse(uint64_t id,VkNrUseId use) const
{
    auto* e=Find(id);if(!e||!e->ready)return false;
    const bool familyOnly=!e->key.frame.queue&&e->key.frame.route==VkNrRoute::Native&&e->key.frame.queueFamily!=UINT32_MAX;
    return std::all_of(e->uses.begin(),e->uses.end(),[&](auto old){return old==use||recordings_.Reusable(old)||
        (recordings_.SafeToWriteAfter(old)&&(!familyOnly||recordings_.RecordingReleased(old)));});
}
VkNrGenerationPayload* VkNrGenerationOwner::Payload(uint64_t id) const
{ auto* e=Find(id);return e?e->payload.get():nullptr; }
bool VkNrGenerationOwner::DrainReleased()
{ Disable();RetireCompleted(recordings_);return entries_.empty(); }
void VkNrGenerationOwner::AbandonDevice()
{
    // Payload destructors must not execute driver destruction after device loss or an unproved drain.
    for(auto& e:entries_) { for(auto u:e.uses)recordings_.ReleaseUse(u);e.payload->Abandon(); }
    entries_.clear();bytes_=0;applied_=0;
}
}
