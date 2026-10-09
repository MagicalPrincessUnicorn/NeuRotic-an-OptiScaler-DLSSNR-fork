#include "VulkanPresentGuides.h"
#include <algorithm>
namespace DlssNr
{
VulkanPresentGuides::VulkanPresentGuides(VkNrRecordingOwner& o,Copy c,Release r):owner_(o),copy_(std::move(c)),release_(std::move(r)){}
VulkanPresentGuides::~VulkanPresentGuides() { AbandonDevice(); }
VkNrGuideSelection::~VkNrGuideSelection()
{
    if(owner_&&metadataUse_){owner_->RemoveConsumerHold(metadataUse_);owner_->ReleaseUse(metadataUse_);}
}
void VulkanPresentGuides::ClearPending()
{
    for(const auto& p:pending_)owner_.ReleaseUse(p.use);
    pending_.clear();
}
std::shared_ptr<const VkNrGuideSelection> VulkanPresentGuides::BeginPresent(VkQueue queue)
{
    auto selection=std::make_shared<VkNrGuideSelection>();
    selection->owner_=&owner_;selection->source_=this;selection->context_=context_;
    selection->reason_=reason_;
    // Resolve against sources already published at entry. Later CPU recording
    // cannot insert another source, substitute a tag, or reuse this acquisition.
    for(const auto& tag:presentTags_){
        VkNrGuideLease lease;
        if(const auto* slot=AssociatedSource(tag)){
            lease=slot->lease;lease.selectionHold=HoldSelection(lease.sourceUse);
            if(const auto proof=owner_.ProducerProof(lease.sourceUse)){lease.producerSubmission=proof->submission;lease.producerQueue=proof->queue;}
        }
        // Keep unmatched tags too: a second viewport for the same image is
        // ambiguous even if only one viewport managed to copy its guides.
        selection->associations_.push_back({tag,std::move(lease)});
    }
    // Recording can run a frame ahead. Only actual submissions observed at
    // this atomic boundary enter this Present; future recordings stay pending.
    std::vector<VkNrUseId> uses;uses.reserve(pending_.size());
    for(const auto& p:pending_)uses.push_back(p.use);
    const auto states=owner_.ProducerStates(uses);
    for(size_t n=0;n<pending_.size();++n){const auto& p=pending_[n];
        if(!p.succeeded||!states[n].proof||(queue&&states[n].proof->queue!=queue))continue;
        ++selection->captures_;
        if(selection->captures_==1){
            selection->metadata_=p.frame;selection->metadata_->queue=states[n].proof->queue;selection->context_=selection->metadata_;selection->candidate_=p.candidate;selection->proof_=states[n].proof;
            if(owner_.RetainUse(p.use)){
                if(owner_.AddConsumerHold(p.use))selection->metadataUse_=p.use;
                else owner_.ReleaseUse(p.use);
            }
            if(!selection->metadataUse_){selection->metadata_.reset();selection->candidate_=0;
                selection->reason_="Present guide lifetime reservation unavailable";}
        }
    }
    if(rejections_)selection->captures_+=rejections_+1; // conservative refusal, even with no valid capture
    // Consume all submitted observations, including ambiguous ones. An old
    // submitted frame must never become a later frame's sole candidate.
    for(size_t n=pending_.size();n-->0;){const auto& p=pending_[n];
        const bool consumed=p.succeeded&&states[n].proof&&(!queue||states[n].proof->queue==queue);
        const bool abandoned=!states[n].proof&&states[n].recordingReleased;
        if(consumed||abandoned){owner_.ReleaseUse(p.use);pending_.erase(pending_.begin()+n);}
    }
    owner_.TraceGuidePresent(selection->metadataUse_);
    captures_=candidate_=rejections_=0;
    if(metadataUse_)owner_.ReleaseUse(metadataUse_);
    metadataUse_={};metadata_.reset();reason_.clear();
    return selection;
}
bool VulkanPresentGuides::Capture(const VkFrameRequest& request,VkNrUseId use)
{
    ++captures_;candidate_=0;
    if(metadataUse_)owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();
    RetireCompleted(owner_);
    const auto& f=request.contract;
    if(!use||owner_.UseDevice(use)!=request.device||!f.deviceGeneration||!f.evaluation||
        f.evaluation.commandBuffer!=request.commandBuffer||f.evaluation.incarnation!=owner_.Incarnation(request.commandBuffer)||
        !owner_.UseOnRecording(use,request.commandBuffer)||owner_.ModelRecordingReason(request.commandBuffer)||
        f.queueFamily!=owner_.UseFamily(use)||!request.targetColor||request.targetColor->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW||
        !request.targetColor->Resource.ImageViewInfo.Image||!f.temporal||f.evaluation.featureGeneration!=f.temporal->featureGeneration||
        !ValidVkNrTemporal(*f.temporal)||!f.temporal->featureGeneration||(!f.temporal->frameToken&&!f.temporal->providerFrameKnown)) {
        ++rejections_;ClearPending();reason_="Native guide selected evaluation or output identity is unproved";return false;
    }
    if(!owner_.RetainUse(use)) { ++rejections_;ClearPending();reason_="Native metadata use unavailable";return false; }
    metadata_=f;metadataUse_=use;context_=f;
    // Scalar observations can be discarded under pressure after their real GPU
    // use has retired. Publication/selected-image lifetimes are independent.
    for(auto i=pending_.begin();pending_.size()>=16&&i!=pending_.end();){
        if(owner_.GpuComplete(i->use)&&owner_.RecordingReleased(i->use)){owner_.ReleaseUse(i->use);i=pending_.erase(i);}
        else ++i;
    }
    if(pending_.size()>=16||!owner_.RetainUse(use)){
        ++rejections_;ClearPending();reason_="sixteen guide metadata records await submission or reset";return false;
    }
    pending_.push_back({f,use,0});
    if(!f.temporal->providerFrameKnown||!f.temporal->providerGeneration){
        reason_="Local evaluation metadata supplies dimensions, not authenticated guide-image rights";return false;
    }
    const auto& t=*f.temporal;
    // Keep ambiguity evidence even when a source copy fails. Prune only frames
    // which have neither packets nor public tags left to select.
    std::erase_if(presentSources_,[&](const auto& source){
        const auto same=[&](const auto& temporal){return temporal&&temporal->providerGeneration==source.provider&&temporal->frameToken==source.frame&&temporal->viewport==source.viewport;};
        return std::none_of(slots_.begin(),slots_.end(),[&](const auto& s){return same(s.lease.contract.temporal);})&&
            std::none_of(presentTags_.begin(),presentTags_.end(),[&](const auto& tag){return tag.providerGeneration==source.provider&&tag.frameToken==source.frame&&tag.viewport==source.viewport;})&&
            !(t.providerGeneration==source.provider&&t.frameToken==source.frame&&t.viewport==source.viewport);
    });
    if(std::none_of(presentSources_.begin(),presentSources_.end(),[&](const auto& s){return s.evaluation==f.evaluation;})){
        if(presentSources_.size()>=128){reason_="Public guide source identity capacity exhausted";return false;}
        presentSources_.push_back({f.evaluation,f.deviceGeneration,t.providerGeneration,t.frameToken,t.viewport,request.targetColor->Resource.ImageViewInfo.Image});
    }
    // The packet is immutable but unpublished until this exact SR call succeeds.
    // A later Present interval never supplies missing source-image identity.
    constexpr uint64_t budget=512ull*1024*1024;
    // Evict only future publication rights. Selected leases and accepted GPU
    // consumers retain their independent holds; RetireCompleted rechecks them.
    if(slots_.size()>=8||bytes_>=budget){
        std::vector<uint64_t> oldest;
        for(const auto& slot:slots_)if(slot.published)oldest.push_back(slot.lease.slot);
        for(auto id:oldest){
            const auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.lease.slot==id;});
            if(found!=slots_.end())RevokePublication(*found);
            for(auto& p:pending_)if(p.candidate==id)p.candidate=0;
            RetireCompleted(owner_);if(slots_.size()<8&&bytes_<budget)break;
        }
    }
    if(slots_.size()>=8||bytes_>=budget||!owner_.RetainUse(use)) {
        reason_="eight guide slots or 512 MiB budget await actual recording release";return false;
    }
    auto made=copy_(request,use,budget-bytes_);
    if(!made.images) {owner_.ReleaseUse(use);reason_=made.reason;return false;}
    if(!made.recorded&&!made.ready&&release_(*made.images)) {owner_.ReleaseUse(use);reason_=made.reason;return false;}
    const bool valid=made.ready&&made.recorded&&made.images->bytes<=budget-bytes_&&
        made.images->depth.Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW&&
        made.images->motion.Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW&&
        made.images->depth.Resource.ImageViewInfo.Image&&made.images->motion.Resource.ImageViewInfo.Image;
    const auto id=next_++;VkNrGuideLease lease;lease.slot=id;lease.contract=f;lease.sourceUse=use;
    lease.producerQueue=f.queue;lease.images=made.images;
    lease.outputImage=request.targetColor->Resource.ImageViewInfo.Image;
    bytes_+=made.images->bytes;slots_.push_back({std::move(lease),{},valid});
    if(!valid) {reason_=made.reason.empty()?"private guide copy failed or exceeded allocation budget":made.reason;return false;}
    candidate_=id;pending_.back().candidate=id;reason_.clear();return true;
}
void VulkanPresentGuides::CompleteEvaluation(const VkNrEvaluationIdentity& evaluation,bool succeeded)
{
    if(!evaluation)return;
    for(auto& slot:slots_)if(slot.lease.contract.evaluation==evaluation){
        if(!succeeded)RevokePublication(slot);
        else if(!slot.resolved&&slot.ready){slot.publicationHeld=owner_.AddConsumerHold(slot.lease.sourceUse);slot.published=slot.publicationHeld;}
        slot.resolved=true;
    }
    for(auto i=pending_.begin();i!=pending_.end();){
        if(i->frame.evaluation!=evaluation){++i;continue;}
        if(succeeded){i->succeeded=true;++i;}
        else {if(candidate_==i->candidate)candidate_=0;owner_.ReleaseUse(i->use);i=pending_.erase(i);}
    }
    if(!succeeded&&metadata_&&metadata_->evaluation==evaluation){
        if(metadataUse_)owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();
    }
}
void VulkanPresentGuides::RevokePublication(Slot& slot)
{
    slot.published=false;slot.resolved=true;
    if(slot.publicationHeld){owner_.RemoveConsumerHold(slot.lease.sourceUse);slot.publicationHeld=false;}
}
std::optional<VkNrGuideLease> VulkanPresentGuides::SelectRecording(const VkNrFrameContract& f,VkCommandBuffer command,VkImage output)
{
    if(!command||!output||!f.evaluation||!f.temporal||!f.temporal->providerFrameKnown||!f.temporal->providerGeneration||
       owner_.ModelRecordingReason(command))return {};
    const Slot* selected=nullptr;
    for(const auto& slot:slots_){const auto& a=slot.lease.contract;
        if(!slot.ready||!slot.published||slot.lease.outputImage!=output||a.evaluation!=f.evaluation||
           a.deviceGeneration!=f.deviceGeneration||a.output.width!=f.output.width||a.output.height!=f.output.height||
           a.representation!=f.representation||!a.temporal||!a.temporal->providerFrameKnown||
           a.temporal->providerGeneration!=f.temporal->providerGeneration||a.temporal->viewport!=f.temporal->viewport||
           a.temporal->featureGeneration!=f.temporal->featureGeneration||a.temporal->frameToken!=f.temporal->frameToken)continue;
        if(selected)return {};selected=&slot;
    }
    if(!selected)return {};
    auto lease=selected->lease;
    if(owner_.CommandDevice(command)!=owner_.UseDevice(lease.sourceUse))return {};
    const auto family=owner_.CommandFamily(command),sourceFamily=owner_.UseFamily(lease.sourceUse);
    if(family!=sourceFamily&&(std::find(lease.images->concurrentFamilies.begin(),lease.images->concurrentFamilies.end(),family)==lease.images->concurrentFamilies.end()||
       std::find(lease.images->concurrentFamilies.begin(),lease.images->concurrentFamilies.end(),sourceFamily)==lease.images->concurrentFamilies.end()))return {};
    lease.consumerCommand=command;
    if(owner_.UseOnRecording(lease.sourceUse,command))lease.sameRecording=command;
    else {
        const auto proof=owner_.ProducerProof(lease.sourceUse);
        if(!proof||proof->deviceGeneration!=f.deviceGeneration||!owner_.GpuComplete(lease.sourceUse))return {};
        lease.producerSubmission=proof->submission;lease.producerQueue=proof->queue;lease.completedProducer=true;
    }
    // A completed producer can otherwise retire between selection and Bind.
    if(!owner_.RetainUse(lease.sourceUse))return {};
    if(!owner_.AddConsumerHold(lease.sourceUse)){owner_.ReleaseUse(lease.sourceUse);return {};}
    lease.selectionHold=std::shared_ptr<VkNrUseId>(new VkNrUseId(lease.sourceUse),[owner=&owner_](VkNrUseId* use){
        owner->RemoveConsumerHold(*use);owner->ReleaseUse(*use);delete use;});
    return lease;
}
bool VulkanPresentGuides::ObservePresentTag(const VkNrPresentGuideTag& tag)
{
    const auto& f=tag.target;
    if(!tag.providerGeneration||tag.viewport==UINT32_MAX||!tag.image||!f.deviceGeneration||!f.swapchainGeneration||
       !f.swapchain||!f.acquireGeneration||f.swapchainImageIndex==UINT32_MAX||!f.output.width||!f.output.height)return false;
    // Replacement is scoped to this provider/viewport and exact WSI image. A
    // second viewport tagging it remains visible as ambiguity at selection.
    std::erase_if(presentTags_,[&](const auto& old){return old.providerGeneration==tag.providerGeneration&&old.viewport==tag.viewport&&
        old.target.deviceGeneration==f.deviceGeneration&&old.target.swapchain==f.swapchain&&old.target.swapchainImageIndex==f.swapchainImageIndex;});
    // Tags are scalar future-selection metadata, not GPU resource ownership.
    // Drop the oldest under pressure rather than permanently refusing every new
    // swapchain after 64 recreations. Existing Present snapshots own copies and
    // independent guide holds; concurrently live swapchains can publish again.
    if(presentTags_.size()>=64)presentTags_.erase(presentTags_.begin());
    presentTags_.push_back(tag);return true;
}
void VulkanPresentGuides::RevokePresentTags(uint64_t provider,uint32_t viewport)
{
    std::erase_if(presentTags_,[&](const auto& tag){return (!provider||tag.providerGeneration==provider)&&(viewport==UINT32_MAX||tag.viewport==viewport);});
}
const VulkanPresentGuides::Slot* VulkanPresentGuides::AssociatedSource(const VkNrPresentGuideTag& tag) const
{
    const PresentSource* source=nullptr;
    for(const auto& candidate:presentSources_){
        if(candidate.deviceGeneration!=tag.target.deviceGeneration||candidate.provider!=tag.providerGeneration||
           candidate.frame!=tag.frameToken||candidate.viewport!=tag.viewport||(tag.selectedOutput&&candidate.output!=tag.selectedOutput))continue;
        if(source)return nullptr;source=&candidate;
    }
    if(!source)return nullptr;
    const Slot* result=nullptr;
    for(const auto& slot:slots_)if(slot.ready&&slot.published&&slot.lease.contract.evaluation==source->evaluation&&
        slot.lease.outputImage==source->output&&slot.lease.contract.output.width==tag.target.output.width&&slot.lease.contract.output.height==tag.target.output.height){
        if(result)return nullptr;result=&slot;
    }
    return result;
}
std::shared_ptr<VkNrUseId> VulkanPresentGuides::HoldSelection(VkNrUseId use)
{
    if(!owner_.RetainUse(use))return {};
    if(!owner_.AddConsumerHold(use)){owner_.ReleaseUse(use);return {};}
    return std::shared_ptr<VkNrUseId>(new VkNrUseId(use),[owner=&owner_](VkNrUseId* held){owner->RemoveConsumerHold(*held);owner->ReleaseUse(*held);delete held;});
}
std::optional<VkNrGuideLease> VulkanPresentGuides::Select(const VkNrFrameContract& f,const VkNrGuideSelection* selection,VkImage image)
{
    selectionReason_="Physical Present has no authenticated selected-output association";
    if(!selection||selection->source_!=this||!image)return {};
    const VkNrGuideSelection::Association* association=nullptr;
    for(const auto& candidate:selection->associations_){const auto& a=candidate.tag.target;
        if(candidate.tag.image!=image||a.deviceGeneration!=f.deviceGeneration||a.swapchainGeneration!=f.swapchainGeneration||
           a.swapchain!=f.swapchain||a.swapchainImageIndex!=f.swapchainImageIndex||a.acquireGeneration!=f.acquireGeneration||
           a.output.width!=f.output.width||a.output.height!=f.output.height||a.representation.format!=f.representation.format||a.representation.colorSpace!=f.representation.colorSpace)continue;
        if(association){selectionReason_="Public backbuffer association is ambiguous";return {};}
        association=&candidate;
    }
    if(!association)return {};
    auto lease=association->lease;
    if(!lease.slot||!lease.selectionHold){selectionReason_="Public backbuffer has no unique successful source evaluation";return {};}
    const auto slot=std::find_if(slots_.begin(),slots_.end(),[&](const auto& s){return s.lease.slot==lease.slot&&s.ready&&s.published;});
    if(slot==slots_.end()){selectionReason_="Public guide packet publication was revoked";return {};}
    const auto proof=owner_.ProducerProof(lease.sourceUse);
    if(!proof||proof->deviceGeneration!=f.deviceGeneration||proof->submission!=lease.producerSubmission||proof->queue!=lease.producerQueue){
        selectionReason_="Public guide producer was not submitted at Present entry";return {};
    }
    const auto sourceFamily=owner_.UseFamily(lease.sourceUse);
    const auto& families=lease.images->concurrentFamilies;
    if(f.queueFamily!=sourceFamily&&(std::find(families.begin(),families.end(),f.queueFamily)==families.end()||
       std::find(families.begin(),families.end(),sourceFamily)==families.end())){
        selectionReason_="Public guide private images do not support the consumer queue family";return {};
    }
    lease.completedProducer=owner_.GpuComplete(lease.sourceUse);
    lease.orderedProducer=!lease.completedProducer&&f.queue&&f.queue==proof->queue;
    if(!lease.completedProducer&&!lease.orderedProducer){selectionReason_="Public guide producer needs completion or its actual submission queue";return {};}
    selectionReason_.clear();return lease;
}
std::optional<VkNrFrameContract> VulkanPresentGuides::SelectMetadata(const VkNrFrameContract& f,const VkNrGuideSelection* selection)
{
    (void)f;(void)selection;
    // This legacy metadata entry point has no physical image identity. Public
    // image rights require Select's exact tagged-image and acquisition checks.
    selectionReason_="Physical Present has no authenticated selected-output association";
    return {};
}
std::optional<VkNrFrameContract> VulkanPresentGuides::SelectResolutionMetadata(const VkNrFrameContract& f,const VkNrGuideSelection* selection)
{
    if(selection&&selection->source_!=this)return {};
    const auto captures=selection?selection->captures_:this->captures_;
    const auto& metadata=selection?selection->metadata_:this->metadata_;
    const auto metadataUse=selection?selection->metadataUse_:this->metadataUse_;
    if(captures!=1||!metadata||!metadataUse)return {};
    const auto& a=*metadata;const auto proof=selection?selection->proof_:owner_.ProducerProof(metadataUse);
    if(!proof||proof->queue!=a.queue||proof->deviceGeneration!=a.deviceGeneration||
        a.deviceGeneration!=f.deviceGeneration||a.swapchainGeneration!=f.swapchainGeneration||a.swapchain!=f.swapchain||
        a.queue!=f.queue||a.output.width!=f.output.width||a.output.height!=f.output.height||
        a.representation!=f.representation||
        (a.acquireGeneration&&(a.acquireGeneration!=f.acquireGeneration||a.swapchainImageIndex!=f.swapchainImageIndex))||
        (f.temporal&&(a.temporal->featureGeneration!=f.temporal->featureGeneration||a.temporal->frameToken!=f.temporal->frameToken)))return {};
    // These are scalar dimensions from one submitted SR evaluation, not evidence
    // that its depth/motion images belong to this acquired presentation image.
    return a;
}
std::string VulkanPresentGuides::DescribeSelection(const VkNrFrameContract& f,const VkNrGuideSelection* selection) const
{
    const auto captures=selection?selection->captures_:this->captures_;
    const auto candidate=selection?selection->candidate_:this->candidate_;
    const auto& metadata=selection?selection->metadata_:this->metadata_;
    const auto metadataUse=selection?selection->metadataUse_:this->metadataUse_;
    const auto& reason=selection?selection->reason_:this->reason_;
    const auto describe=[](const VkNrFrameContract& a){
        return "deviceGeneration="+std::to_string(a.deviceGeneration)+" swapchainGeneration="+std::to_string(a.swapchainGeneration)+
            " image="+std::to_string(a.swapchainImageIndex)+" acquire="+std::to_string(a.acquireGeneration)+
            " output="+std::to_string(a.output.width)+"x"+std::to_string(a.output.height)+
            " representation="+std::to_string(a.representation.format)+","+std::to_string(a.representation.colorSpace)+","+std::to_string(a.representation.recipeRevision);
    };
    std::string text="captureOrRejectObservations="+std::to_string(captures)+" candidate="+std::to_string(candidate!=0)+
        " slots="+std::to_string(slots_.size())+" privateBytes="+std::to_string(bytes_)+
        " publicBackbufferTags="+std::to_string(selection?selection->associations_.size():presentTags_.size())+
        " captureReason=["+reason+"] present=["+describe(f)+"]";
    if(metadata){
        const auto& a=*metadata;const auto proof=selection?selection->proof_:owner_.ProducerProof(metadataUse);
        text+=" capture=["+describe(a)+"] queueMatch="+std::to_string(a.queue==f.queue)+
            " swapchainMatch="+std::to_string(a.swapchain==f.swapchain)+
            " producerQueueMatch="+std::to_string(proof&&proof->queue==a.queue)+
            " producer=["+owner_.DescribeProducer(metadataUse)+"] entrySnapshot="+std::to_string(selection!=nullptr)+
            " submittedAtEntry="+std::to_string(proof.has_value());
    }else text+=" capture=missing";
    return text;
}
uint64_t VulkanPresentGuides::ContextGeneration(const VkNrFrameContract& f,const VkNrGuideSelection* selection) const
{
    if(selection&&selection->source_!=this)return 0;
    const auto& context=selection?selection->context_:this->context_;
    if(!context)return 0;
    const auto& a=*context;
    return a.deviceGeneration==f.deviceGeneration&&a.swapchainGeneration==f.swapchainGeneration&&
        a.swapchain==f.swapchain&&a.queue==f.queue&&a.output.width==f.output.width&&a.output.height==f.output.height
        ? a.temporal->featureGeneration : 0;
}
bool VulkanPresentGuides::Bind(const VkNrGuideLease& lease,VkNrUseId consumer,VkQueue queue)
{
    const auto i=std::find_if(slots_.begin(),slots_.end(),[&](const auto& s){return s.lease.slot==lease.slot&&s.ready&&s.lease.images==lease.images;});
    if(i==slots_.end()||!consumer||i->lease.sourceUse!=lease.sourceUse||i->lease.outputImage!=lease.outputImage||
       i->lease.contract.evaluation!=lease.contract.evaluation||!lease.selectionHold||*lease.selectionHold!=lease.sourceUse||!lease.consumerCommand||
       !owner_.UseOnRecording(consumer,lease.consumerCommand)||owner_.ModelRecordingReason(lease.consumerCommand)||
       owner_.UseDevice(consumer)!=owner_.UseDevice(lease.sourceUse))return false;
    const auto proof=owner_.ProducerProof(lease.sourceUse);
    const bool same=lease.sameRecording==lease.consumerCommand&&owner_.UseOnRecording(lease.sourceUse,lease.consumerCommand);
    const auto family=owner_.UseFamily(consumer),sourceFamily=owner_.UseFamily(lease.sourceUse);
    const bool sharing=family==sourceFamily||
        (std::find(i->lease.images->concurrentFamilies.begin(),i->lease.images->concurrentFamilies.end(),family)!=i->lease.images->concurrentFamilies.end()&&
         std::find(i->lease.images->concurrentFamilies.begin(),i->lease.images->concurrentFamilies.end(),sourceFamily)!=i->lease.images->concurrentFamilies.end());
    const bool producerMatches=proof&&proof->submission==lease.producerSubmission&&proof->queue==lease.producerQueue&&
        proof->deviceGeneration==lease.contract.deviceGeneration;
    const bool completed=lease.completedProducer&&producerMatches&&owner_.GpuComplete(lease.sourceUse);
    const bool ordered=lease.orderedProducer&&producerMatches&&queue&&queue==proof->queue&&family==sourceFamily;
    if(!sharing||(!same&&!completed&&!ordered)) {
        reason_="guide consumer lacks exact recording, completed producer or accepted same-queue dependency";return false;
    }
    // orderedProducer requires the caller to record a write-to-read barrier and
    // submit this private consumer to the actual earlier producer queue.
    if(std::find(i->consumers.begin(),i->consumers.end(),consumer)!=i->consumers.end())return true;
    if(i->consumers.size()>=1024||!owner_.RetainUse(consumer))return false;
    i->consumers.push_back(consumer);
    // The publication survives physical Present and producer completion until an
    // authentic consumer takes it. Already selected immutable leases stay valid.
    RevokePublication(*i);if(candidate_==i->lease.slot)candidate_=0;
    for(auto& p:pending_)if(p.candidate==i->lease.slot)p.candidate=0;
    return true;
}
void VulkanPresentGuides::RejectNative(const char* reason)
{ ++captures_;++rejections_;ClearPending();for(auto& slot:slots_)RevokePublication(slot);candidate_=0;if(metadataUse_)owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();reason_=reason?reason:"Native guide capture rejected"; }
void VulkanPresentGuides::PresentObserved() {ClearPending();rejections_=0;captures_=0;candidate_=0;if(metadataUse_)owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();}
void VulkanPresentGuides::RetireInactive(VkDevice device)
{
    if(!device)return;
    for(auto& slot:slots_)if(owner_.UseDevice(slot.lease.sourceUse)==device){
        RevokePublication(slot);
        if(candidate_==slot.lease.slot)candidate_=0;
    }
    for(auto i=pending_.begin();i!=pending_.end();){
        if(owner_.UseDevice(i->use)!=device){++i;continue;}
        if(candidate_==i->candidate)candidate_=0;
        owner_.ReleaseUse(i->use);i=pending_.erase(i);
    }
    if(metadataUse_&&owner_.UseDevice(metadataUse_)==device){
        owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();
    }
    // Publication is CPU-owned intent, not a fence/reset/consumer completion
    // proof. Existing immutable leases can still Bind; Reusable retains every
    // pending, replayable, uncertain or independently selected use.
    RetireCompleted(owner_);
}
void VulkanPresentGuides::RetireCompleted(const VkNrRecordingOwner& owner)
{
    if(&owner!=&owner_)return;
    for(auto i=slots_.begin();i!=slots_.end();) {
        const bool pending=std::any_of(pending_.begin(),pending_.end(),[&](const auto& p){return p.candidate==i->lease.slot;});
        const bool done=!pending&&i->lease.slot!=candidate_&&owner_.Reusable(i->lease.sourceUse)&&
            std::all_of(i->consumers.begin(),i->consumers.end(),[&](auto use){return owner_.Reusable(use);});
        if(done&&release_(*i->lease.images)) {
            owner_.ReleaseUse(i->lease.sourceUse);for(auto use:i->consumers)owner_.ReleaseUse(use);
            bytes_-=i->lease.images->bytes;i=slots_.erase(i);
        }else ++i;
    }
}
void VulkanPresentGuides::AbandonDevice()
{
    ClearPending();rejections_=0;context_.reset();presentTags_.clear();presentSources_.clear();
    if(metadataUse_)owner_.ReleaseUse(metadataUse_);metadataUse_={};metadata_.reset();
    for(auto& s:slots_){RevokePublication(s);s.lease.images->Abandon();owner_.ReleaseUse(s.lease.sourceUse);for(auto use:s.consumers)owner_.ReleaseUse(use);}
    slots_.clear();bytes_=captures_=candidate_=0;
}
}
