#include <nr/diagnostics/HostCost.h>
#include "VulkanNrRecording.h"
#include "VulkanNrFlightRecorder.h"
#include <algorithm>
#include <thread>
namespace DlssNr
{
namespace { struct Observation {VkNrObservation operation;uintptr_t identity;VkDevice device;}; thread_local std::vector<Observation> observations; }
VkNrObservationScope::VkNrObservationScope(VkNrObservation operation, uintptr_t identity, VkDevice device)
{
    Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::Observation, 64);
    // A layer can translate legacy Submit to Submit2. Both entry points still
    // describe one application submission while nested on the same queue.
    if(operation==VkNrObservation::Submit2)operation=VkNrObservation::Submit;
    observe_ = true;
    for (const auto& previous : observations) if (previous.operation == operation && previous.identity == identity && previous.device == device) observe_ = false;
    observations.push_back({operation, identity, device});
}
VkNrObservationScope::~VkNrObservationScope() {
    Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::Observation, 64);
    observations.pop_back();
}
uint32_t VkNrObservationScope::SubmissionDepth()
{
    return static_cast<uint32_t>(std::count_if(observations.begin(),observations.end(),
        [](const auto& item){return item.operation==VkNrObservation::Submit;}));
}
VkNrRecordingOwner::GuideTrace* VkNrRecordingOwner::FindGuideTrace(VkNrUseId use)
{
    for(size_t i=0;i<guideTraceCount_;++i)if(guideTraces_[i].use==use.value)return &guideTraces_[i];
    return nullptr;
}
bool VkNrRecordingOwner::TraceGuideCapture(VkNrUseId use)
{
    std::lock_guard lock(mutex_);const auto found=uses_.find(use.value);
    if(found==uses_.end()||guideTraceCount_==guideTraces_.size()||FindGuideTrace(use))return false;
    auto& trace=guideTraces_[guideTraceCount_++];trace.use=use.value;trace.incarnation=found->second.incarnation;
    trace.thread=std::hash<std::thread::id>{}(std::this_thread::get_id());
    trace.capture=++guideTraceSequence_;trace.submitDepth=VkNrObservationScope::SubmissionDepth();return true;
}
void VkNrRecordingOwner::TraceGuidePresent(VkNrUseId selected)
{
    std::lock_guard lock(mutex_);
    for(size_t i=0;i<guideTraceCount_;++i){auto& t=guideTraces_[i];
        if(!t.present)t.present=++guideTraceSequence_;
        if(t.use==selected.value&&!t.selected)t.selected=++guideTraceSequence_;
    }
}
std::vector<VkNrRecordingOwner::GuideTrace> VkNrRecordingOwner::GuideTraces() const
{
    std::lock_guard lock(mutex_);return {guideTraces_.begin(),guideTraces_.begin()+guideTraceCount_};
}
VkNrRecordingOwner& VulkanNrRecordings()
{
    // Registry/overlay proof leases can have process-static storage. Keep the
    // CPU authority alive through their destructors; actual device destruction
    // explicitly clears all device recordings, submissions and use state.
    static auto* owner=new VkNrRecordingOwner;
    return *owner;
}
void VkNrRecordingOwner::OnCreatePool(VkDevice device, VkCommandPool pool, const VkCommandPoolCreateInfo& info, VkResult result)
{
    if (result != VK_SUCCESS || !device || !pool) return;
    std::lock_guard lock(mutex_);
    if (pools_.size() >= 65536) return;
    pools_.insert_or_assign({device,pool}, Pool{device, info.queueFamilyIndex, info.flags});
}
void VkNrRecordingOwner::OnAllocateBuffers(VkDevice device, const VkCommandBufferAllocateInfo& info,
                                          const VkCommandBuffer* buffers, VkResult result)
{
    if (result != VK_SUCCESS || !buffers) return;
    Pool pool{device, UINT32_MAX, 0};
    { std::lock_guard lock(mutex_); const auto found = pools_.find({device,info.commandPool});
      if (found != pools_.end() && found->second.device == device) pool = found->second; }
    for (uint32_t i = 0; i < info.commandBufferCount; ++i)
        OnAllocate(device, info.commandPool, buffers[i], pool.family, pool.flags, info.level);
}
void VkNrRecordingOwner::Close(Recording& recording,const char* reason)
{
    StopObservingWork(recording);
    if(!recording.uses.empty())VkFlight::Current().Write(VkFlight::Kind::Reset,recording.incarnation,0,0,
        recording.uses.front().value,0,recording.uses.size(),0,true);
    for (const auto use : recording.uses) {
        if(auto* t=FindGuideTrace(use);t&&!t->closed){t->closed=++guideTraceSequence_;t->closeReason=reason;}
        const auto found = uses_.find(use.value); if (found != uses_.end()) found->second.recordingReference = false;
    }
    recording.uses.clear(); recording.children.clear(); recording.incarnation = 0;
    recording.recording = false; recording.executable = false; recording.knownLinks = recording.validKnownLinks = true;
    recording.rendering = recording.suspendsRendering = false;recording.workSerial=0;recording.unsupportedBindings=false;
    recording.submissionQueries=0;recording.submissionGraph="not_observed";
}
void VkNrRecordingOwner::OnAllocate(VkDevice device, VkCommandPool pool, VkCommandBuffer buffer, uint32_t family,
                                   VkCommandPoolCreateFlags flags, VkCommandBufferLevel level)
{
    std::lock_guard lock(mutex_);
    if (!device || !pool || !buffer || recordings_.size() >= 65536) return;
    auto& recording = recordings_[buffer]; Close(recording,"allocate");
    recording.device = device; recording.pool = pool; recording.family = family;
    recording.poolFlags = flags; recording.level = level;
}
void VkNrRecordingOwner::OnBegin(VkCommandBuffer buffer, VkCommandBufferUsageFlags flags, VkResult result)
{
    if (result != VK_SUCCESS) return;
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer); if (found == recordings_.end()) return;
    Close(found->second,"begin"); auto& recording = found->second;
    recording.incarnation = nextIncarnation_++; recording.recording = true; recording.flags = flags;
}
void VkNrRecordingOwner::OnEnd(VkCommandBuffer buffer, VkResult result)
{
    if (result != VK_SUCCESS) return;
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer); if (found == recordings_.end() || !found->second.recording) return;
    StopObservingWork(found->second);
    found->second.recording = false; found->second.executable = true;
    for(auto use:found->second.uses)if(auto* t=FindGuideTrace(use);t&&!t->end)t->end=++guideTraceSequence_;
}
void VkNrRecordingOwner::OnReset(VkCommandBuffer buffer, VkResult result)
{
    if (result != VK_SUCCESS) return;
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer); if (found != recordings_.end()) Close(found->second,"reset");
}
void VkNrRecordingOwner::OnResetPool(VkDevice device, VkCommandPool pool, VkResult result)
{
    if (result != VK_SUCCESS) return;
    std::lock_guard lock(mutex_);
    for (auto& [buffer, recording] : recordings_) { (void)buffer; if (recording.device == device && recording.pool == pool) Close(recording,"reset_pool"); }
}
void VkNrRecordingOwner::OnFree(VkCommandBuffer buffer)
{
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer); if (found == recordings_.end()) return;
    Close(found->second,"free"); recordings_.erase(found);
}
void VkNrRecordingOwner::OnDestroyPool(VkDevice device, VkCommandPool pool)
{
    std::lock_guard lock(mutex_);
    pools_.erase({device,pool});
    for (auto found = recordings_.begin(); found != recordings_.end();) {
        if (found->second.device == device && found->second.pool == pool) { Close(found->second,"destroy_pool"); found = recordings_.erase(found); } else ++found;
    }
}
void VkNrRecordingOwner::OnExecute(VkCommandBuffer buffer, std::span<const VkCommandBuffer> children)
{
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer);
    if (found == recordings_.end() || !found->second.recording) return;
    auto& recording = found->second;
    for (auto child : children) {
        const auto secondary = recordings_.find(child);
        if (secondary == recordings_.end()) { recording.knownLinks = false; continue; }
        if (!secondary->second.executable ||
            secondary->second.level != VK_COMMAND_BUFFER_LEVEL_SECONDARY || secondary->second.device != recording.device ||
            secondary->second.family != recording.family || recording.children.size() >= 4096) {
            recording.knownLinks = recording.validKnownLinks = false; continue;
        }
        VkFlight::Current().Write(VkFlight::Kind::Occurrence,recording.incarnation,0,
            reinterpret_cast<uintptr_t>(buffer),secondary->second.incarnation,reinterpret_cast<uintptr_t>(child),recording.children.size());
        recording.children.push_back({child, secondary->second.incarnation});
    }
}
void VkNrRecordingOwner::OnRendering(VkCommandBuffer buffer, bool active, VkRenderingFlags flags)
{
    std::lock_guard lock(mutex_);
    const auto found = recordings_.find(buffer);
    if (found == recordings_.end() || !found->second.recording) return;
    auto& recording = found->second;
    if (active) {
        recording.rendering = true;
        recording.suspendsRendering = (flags & VK_RENDERING_SUSPENDING_BIT) != 0;
    } else recording.rendering = recording.suspendsRendering;
}
const char* VkNrRecordingOwner::ModelRecordingReason(VkCommandBuffer buffer) const
{
    std::lock_guard lock(mutex_);
    const auto it=recordings_.find(buffer);
    if(it==recordings_.end()||!it->second.recording) return "Vulkan command recording is unobserved or closed";
    const auto& r=it->second;
    VkFlight::Current().Write(VkFlight::Kind::Profile,r.incarnation,0,reinterpret_cast<uintptr_t>(buffer),0,0,
        uint64_t(r.flags)|(uint64_t(r.level)<<32));
    if(r.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY) return "Vulkan private model secondary execution is unqualified";
    if(r.flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) return "Vulkan private model simultaneous replay is unqualified";
    if(!(r.flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) return "Vulkan private model requires a one-time command recording";
    return nullptr;
}
std::optional<VkNrUseId> VkNrRecordingOwner::ReserveModel(VkCommandBuffer buffer,uint64_t generation)
{
    // Vulkan requires external synchronization for host recording mutations.
    // The application owns the recording across this check/reservation pair.
    if(ModelRecordingReason(buffer))return {};
    return ReserveRecording(buffer,generation,true);
}
std::optional<VkNrUseId> VkNrRecordingOwner::Reserve(VkCommandBuffer buffer, uint64_t generation)
{ return ReserveRecording(buffer,generation,true); }
std::optional<VkNrUseId> VkNrRecordingOwner::ReserveCopy(VkCommandBuffer buffer, uint64_t generation)
{ return ReserveRecording(buffer,generation,false); }
std::optional<VkNrUseId> VkNrRecordingOwner::ReserveRecording(VkCommandBuffer buffer, uint64_t generation,bool requireBindings)
{
    std::lock_guard lock(mutex_);
    for (auto use = uses_.begin(); use != uses_.end();) {
        if (!use->second.recordingReference && use->second.pending == 0 && !use->second.uncertain && !use->second.observers && !use->second.consumerHolds) use = uses_.erase(use); else ++use;
    }
    const auto found = recordings_.find(buffer);
    if (found == recordings_.end() || !generation || uses_.size() >= 1024) return {};
    auto& recording = found->second;
    if (!recording.recording || recording.rendering || (requireBindings && recording.unsupportedBindings) || recording.family == UINT32_MAX || !recording.knownLinks ||
        (recording.poolFlags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT) ||
        (recording.flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) return {};
    VkNrUseId use {nextUse_++}; Use state{generation,recording.device,buffer,recording.incarnation};state.family=recording.family;
    state.snapshotSequenceAtReserve=snapshotSequence_;
    uses_.emplace(use.value,state); recording.uses.push_back(use);
    // Model consumers may publish an initial zero token before sampling it.
    // Copy/submission-only reservations have no shader-output freshness token.
    if(requireBindings)ObserveWork(recording);
    return use;
}
std::optional<VkNrUseId> VkNrRecordingOwner::ReserveExecutionDependency(VkCommandBuffer buffer, uint64_t generation)
{
    std::lock_guard lock(mutex_);
    for (auto use = uses_.begin(); use != uses_.end();) {
        if (!use->second.recordingReference && use->second.pending == 0 && !use->second.uncertain && !use->second.observers && !use->second.consumerHolds) use = uses_.erase(use); else ++use;
    }
    const auto found = recordings_.find(buffer);
    if (found == recordings_.end() || !generation || uses_.size() >= 1024) return {};
    auto& recording = found->second;
    if (!recording.executable || recording.rendering || recording.unsupportedBindings || recording.family == UINT32_MAX || !recording.knownLinks ||
        (recording.poolFlags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT) ||
        (recording.flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) return {};
    VkNrUseId use {nextUse_++}; Use state{generation,recording.device,buffer,recording.incarnation};state.family=recording.family;
    state.snapshotSequenceAtReserve=snapshotSequence_;
    uses_.emplace(use.value,state); recording.uses.push_back(use); return use;
}
bool VkNrRecordingOwner::RecordingReleased(VkNrUseId use) const
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    return found == uses_.end() || !found->second.recordingReference;
}
std::optional<VkNrUseId> VkNrRecordingOwner::ReserveSubmissionDependency(VkDevice device,uint64_t generation,uint32_t family)
{
    std::lock_guard lock(mutex_);
    std::erase_if(uses_,[](const auto& item){const auto& use=item.second;
        return !use.recordingReference&&!use.pending&&!use.uncertain&&!use.observers&&!use.consumerHolds;});
    if(!device||!generation||uses_.size()>=1024)return {};
    VkNrUseId use{nextUse_++};
    Use state{generation,device,VK_NULL_HANDLE,0};
    state.recordingReference=false;state.observers=1;state.family=family;
    uses_.emplace(use.value,state);
    return use;
}
bool VkNrRecordingOwner::SingleUsePrimary(VkNrUseId use) const
{
    std::lock_guard lock(mutex_);
    const auto found=uses_.find(use.value);if(found==uses_.end())return false;
    const auto recording=recordings_.find(found->second.buffer);
    if(recording==recordings_.end()||recording->second.incarnation!=found->second.incarnation)return false;
    const auto& profile=recording->second;
    return profile.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY&&
        (profile.flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)&&
        !(profile.flags&(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT));
}
void VkNrRecordingOwner::CancelBeforeRecording(VkNrUseId use)
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    if (found == uses_.end() || found->second.submitted || found->second.pending || found->second.consumerHolds || found->second.observers) return;
    for (auto& [buffer,recording] : recordings_) { (void)buffer; std::erase(recording.uses,use); }
    if(auto* t=FindGuideTrace(use);t&&!t->closed){t->closed=++guideTraceSequence_;t->closeReason="cancel";}
    uses_.erase(found);
}
bool VkNrRecordingOwner::RetainUse(VkNrUseId use)
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    if (found == uses_.end() || found->second.observers == UINT32_MAX) return false;
    ++found->second.observers; return true;
}
void VkNrRecordingOwner::ReleaseUse(VkNrUseId use)
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    if (found != uses_.end() && found->second.observers) --found->second.observers;
}
uint64_t VkNrRecordingOwner::Incarnation(VkCommandBuffer buffer) const
{
    std::lock_guard lock(mutex_); const auto found = recordings_.find(buffer);
    return found == recordings_.end() ? 0 : found->second.incarnation;
}
bool VkNrRecordingOwner::Collect(VkCommandBuffer buffer, uint64_t incarnation, std::vector<VkNrUseId>& result,
                                std::vector<VkCommandBuffer>& path, bool allowOpaque,const char** refusal) const
{
    const auto found = recordings_.find(buffer);
    const auto reject=[&](const char* reason){if(refusal)*refusal=reason;return false;};
    if(found==recordings_.end())return reject("unknown_recording");
    if(!found->second.executable)return reject("not_executable");
    if(!found->second.validKnownLinks)return reject("invalid_known_child");
    if(!allowOpaque&&!found->second.knownLinks)return reject("opaque_child");
    if(found->second.incarnation!=incarnation)return reject("stale_child_incarnation");
    if(path.size()>=64)return reject("graph_depth_limit");
    if(std::find(path.begin(),path.end(),buffer)!=path.end())return reject("graph_cycle");
    path.push_back(buffer);
    for (auto use : found->second.uses) if (std::find(result.begin(), result.end(), use) == result.end()) result.push_back(use);
    for (auto child : found->second.children) if (!Collect(child.buffer, child.incarnation, result, path, allowOpaque,refusal)) return false;
    path.pop_back(); return true;
}
std::optional<std::vector<VkNrUseId>> VkNrRecordingOwner::SnapshotUses(VkCommandBuffer buffer) const
{
    std::lock_guard lock(mutex_); const auto found = recordings_.find(buffer);
    if (found == recordings_.end()) return {};
    std::vector<VkNrUseId> result; std::vector<VkCommandBuffer> path;
    if (!Collect(buffer, found->second.incarnation, result, path)) return {};
    return result;
}
std::optional<std::vector<VkNrUseId>> VkNrRecordingOwner::SnapshotSubmissionUses(VkCommandBuffer buffer)
{
    std::lock_guard lock(mutex_);const auto found=recordings_.find(buffer);
    ++snapshotSequence_;
    if(found==recordings_.end()){++unknownSubmissionRoots_;return {};}
    ++found->second.submissionQueries;
    std::vector<VkNrUseId> result;std::vector<VkCommandBuffer> path;
    const char* refusal=nullptr;
    if(!Collect(buffer,found->second.incarnation,result,path,true,&refusal)){
        ++rejectedSubmissionRoots_;found->second.submissionGraph=refusal;return {};
    }
    found->second.submissionGraph="accepted";
    for(auto use:result)if(auto* t=FindGuideTrace(use);t&&!t->root)t->root=++guideTraceSequence_;
    return result;
}
std::optional<VkNrSubmissionId> VkNrRecordingOwner::PrepareSubmission(const VkNrSubmission& submission)
{
    std::lock_guard lock(mutex_);
    if (!submission.queue || !submission.deviceGeneration || submission.uses.empty() || submissions_.size() >= 1024) return {};
    VkNrSubmission unique = submission; unique.uses.clear();
    for (auto use : submission.uses) {
        if (!uses_.contains(use.value)) return {};
        if (std::find(unique.uses.begin(), unique.uses.end(), use) == unique.uses.end()) unique.uses.push_back(use);
    }
    const VkNrSubmissionId id{nextSubmission_++}; submissions_.emplace(id.value, Submission{unique});
    for (auto use : unique.uses) ++uses_.at(use.value).pending;
    return id;
}
void VkNrRecordingOwner::SubmissionReturned(VkNrSubmissionId id, VkResult result)
{
    std::lock_guard lock(mutex_); const auto found = submissions_.find(id.value); if (found == submissions_.end() || found->second.submitted) return;
    for(auto use:found->second.value.uses)if(auto* t=FindGuideTrace(use);t&&!t->returned){t->returned=++guideTraceSequence_;t->result=result;t->returnPath="tracked";}
    const bool unaffectedFailure = result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!unaffectedFailure) {
        found->second.submitted = true;
        for (auto use : found->second.value.uses) {
            auto& state = uses_.at(use.value); state.submitted = true;
            ++state.submissionCount;
            state.producer={id,found->second.value.queue,found->second.value.deviceGeneration};
            if (result != VK_SUCCESS) state.uncertain = true;
        }
    } else {
        for (auto use : found->second.value.uses) --uses_.at(use.value).pending;
        submissions_.erase(found);
    }
}
void VkNrRecordingOwner::CheckpointReturned(VkNrSubmissionId id, VkResult result)
{
    std::lock_guard lock(mutex_); const auto found = submissions_.find(id.value);
    if (found == submissions_.end() || !found->second.submitted) return;
    found->second.checkpoint = result == VK_SUCCESS;
    if (result != VK_SUCCESS) for (auto use : found->second.value.uses) uses_.at(use.value).uncertain = true;
}
void VkNrRecordingOwner::ObserveFence(VkNrSubmissionId id, VkResult result)
{
    std::lock_guard lock(mutex_); const auto found = submissions_.find(id.value);
    if (found == submissions_.end() || !found->second.submitted || !found->second.checkpoint) return;
    if(result==VK_SUCCESS||result==VK_ERROR_DEVICE_LOST)VkFlight::Current().Write(VkFlight::Kind::Completion,id.value,0,0,0,
        reinterpret_cast<uintptr_t>(found->second.value.queue),found->second.value.uses.size(),result,result!=VK_SUCCESS);
    if (result == VK_ERROR_DEVICE_LOST) for (auto use : found->second.value.uses) uses_.at(use.value).uncertain = true;
    if (result != VK_SUCCESS) return;
    for (auto use : found->second.value.uses) --uses_.at(use.value).pending;
    for (auto use : found->second.value.uses) {
        const auto& state=uses_.at(use.value); const auto recording=recordings_.find(state.buffer);
        if(recording==recordings_.end() || recording->second.incarnation!=state.incarnation ||
           !(recording->second.flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) continue;
        const bool complete=std::all_of(recording->second.uses.begin(),recording->second.uses.end(),[&](auto id) {
            const auto& held=uses_.at(id.value); return held.submitted&&!held.pending&&!held.uncertain;
        });
        if(complete) Close(recording->second,"one_time_fence");
    }
    submissions_.erase(found);
}
void VkNrRecordingOwner::UntrackedSubmission(std::span<const VkNrUseId> uses, VkResult result)
{
    std::lock_guard lock(mutex_);
    for(auto use:uses)if(auto* t=FindGuideTrace(use);t&&!t->returned){t->returned=++guideTraceSequence_;t->result=result;t->returnPath="untracked";}
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY) return;
    for (auto use : uses) { const auto found = uses_.find(use.value); if (found != uses_.end()) { found->second.submitted = true; found->second.uncertain = true; } }
}
bool VkNrRecordingOwner::GpuComplete(VkNrUseId use) const
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    return found != uses_.end() && found->second.submitted && found->second.pending == 0 && !found->second.uncertain;
}
bool VkNrRecordingOwner::Reusable(VkNrUseId use) const
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value);
    return found == uses_.end() || (!found->second.recordingReference && !found->second.pending && !found->second.uncertain && !found->second.consumerHolds);
}
bool VkNrRecordingOwner::CanYieldOutput(std::string& reason) const
{
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()){reason="Vulkan recording ownership is busy";return false;}
    if(!submissions_.empty()){reason="Vulkan submission completion remains pending or unknown";return false;}
    for(const auto& [_,use]:uses_){
        if(use.uncertain){reason="Vulkan recording completion is quarantined";return false;}
        if(use.recordingReference){reason="Vulkan private recording remains executable or unsubmitted";return false;}
        if(use.pending){reason="Vulkan recording GPU completion remains pending";return false;}
        if(use.observers||use.consumerHolds){reason="Vulkan source or consumer lease remains active";return false;}
    }
    reason.clear();return true;
}
bool VkNrRecordingOwner::SafeToWriteAfter(VkNrUseId use) const
{
    std::lock_guard lock(mutex_);auto found=uses_.find(use.value);
    return found!=uses_.end()&&found->second.submitted&&!found->second.pending&&!found->second.uncertain&&!found->second.consumerHolds;
}
bool VkNrRecordingOwner::AddConsumerHold(VkNrUseId use)
{
    std::lock_guard lock(mutex_);auto found=uses_.find(use.value);if(found==uses_.end()||found->second.consumerHolds==UINT32_MAX)return false;
    ++found->second.consumerHolds;return true;
}
void VkNrRecordingOwner::RemoveConsumerHold(VkNrUseId use)
{ std::lock_guard lock(mutex_);auto found=uses_.find(use.value);if(found!=uses_.end()&&found->second.consumerHolds)--found->second.consumerHolds; }
bool VkNrRecordingOwner::UseOnRecording(VkNrUseId use,VkCommandBuffer cb) const
{
    std::lock_guard lock(mutex_);auto found=uses_.find(use.value);auto recording=recordings_.find(cb);
    return found!=uses_.end()&&recording!=recordings_.end()&&found->second.buffer==cb&&found->second.incarnation==recording->second.incarnation&&recording->second.recording;
}
void VkNrRecordingOwner::OnUnsupportedBindings(VkCommandBuffer cb){std::lock_guard lock(mutex_);auto i=recordings_.find(cb);if(i!=recordings_.end())i->second.unsupportedBindings=true;}
bool VkNrRecordingOwner::BindingsQualified(VkCommandBuffer cb) const{std::lock_guard lock(mutex_);auto i=recordings_.find(cb);return i!=recordings_.end()&&!i->second.unsupportedBindings;}
void VkNrRecordingOwner::OnWork(VkCommandBuffer cb)
{
    Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::RecordingWork, 64);
    // Normal game draws with no sampled/reserved output need no owner lock.
    // Vulkan externally synchronizes a command buffer's recording mutations;
    // a reservation/sample and later work on that same buffer are ordered.
    if(!workObservers_.load(std::memory_order_acquire))return;
    std::lock_guard lock(mutex_);auto found=recordings_.find(cb);
    if(found!=recordings_.end()&&found->second.recording&&found->second.workObserved)++found->second.workSerial;
}
void VkNrRecordingOwner::ObserveWork(const Recording& recording) const
{
    // Caller owns mutex_. Lifecycle disarms before an incarnation is closed.
    if(!recording.workObserved){recording.workObserved=true;workObservers_.fetch_add(1,std::memory_order_release);}
}
void VkNrRecordingOwner::StopObservingWork(Recording& recording)
{
    if(recording.workObserved){recording.workObserved=false;workObservers_.fetch_sub(1,std::memory_order_release);}
}
uint64_t VkNrRecordingOwner::CommandSerial(VkCommandBuffer cb) const
{
    std::lock_guard lock(mutex_);auto found=recordings_.find(cb);
    if(found==recordings_.end()||!found->second.recording)return UINT64_MAX;
    ObserveWork(found->second);return found->second.workSerial;
}
VkDevice VkNrRecordingOwner::UseDevice(VkNrUseId use) const
{
    std::lock_guard lock(mutex_); const auto found = uses_.find(use.value); return found == uses_.end() ? VK_NULL_HANDLE : found->second.device;
}
uint32_t VkNrRecordingOwner::UseFamily(VkNrUseId use) const
{
    std::lock_guard lock(mutex_);const auto found=uses_.find(use.value);
    return found==uses_.end()?UINT32_MAX:found->second.family;
}
std::optional<VkNrProducerProof> VkNrRecordingOwner::ProducerProof(VkNrUseId use) const
{
    std::lock_guard lock(mutex_);const auto i=uses_.find(use.value);
    if(i==uses_.end()||!i->second.submitted||i->second.uncertain||i->second.submissionCount!=1)return {};
    return i->second.producer;
}
VkDevice VkNrRecordingOwner::CommandDevice(VkCommandBuffer buffer) const
{
    std::lock_guard lock(mutex_);const auto found=recordings_.find(buffer);
    return found==recordings_.end()?VK_NULL_HANDLE:found->second.device;
}
std::vector<VkNrRecordingOwner::ProducerState> VkNrRecordingOwner::ProducerStates(std::span<const VkNrUseId> uses) const
{
    std::lock_guard lock(mutex_);std::vector<ProducerState> result;result.reserve(uses.size());
    for(auto use:uses){const auto i=uses_.find(use.value);
        ProducerState state;state.recordingReleased=i==uses_.end()||!i->second.recordingReference;
        if(i!=uses_.end()&&i->second.submitted&&!i->second.uncertain&&i->second.submissionCount==1)state.proof=i->second.producer;
        result.push_back(state);
    }
    return result;
}
std::string VkNrRecordingOwner::DescribeProducer(VkNrUseId use) const
{
    std::lock_guard lock(mutex_);const auto i=uses_.find(use.value);
    if(i==uses_.end())return "use=missing";
    const auto& u=i->second;
    const auto recording=recordings_.find(u.buffer);
    const bool current=recording!=recordings_.end()&&recording->second.incarnation==u.incarnation;
    auto text="submitted="+std::to_string(u.submitted)+" uncertain="+std::to_string(u.uncertain)+
        " submissions="+std::to_string(u.submissionCount)+" pending="+std::to_string(u.pending)+
        " recordingReference="+std::to_string(u.recordingReference)+" useGeneration="+std::to_string(u.generation)+
        " submissionGeneration="+std::to_string(u.producer.deviceGeneration)+" family="+std::to_string(u.family)+
        " currentRecording="+std::to_string(current)+
        " executable="+std::to_string(current&&recording->second.executable)+
        " allChildrenObserved="+std::to_string(current&&recording->second.knownLinks)+
        " knownChildrenValid="+std::to_string(current&&recording->second.validKnownLinks)+
        " incarnation="+std::to_string(u.incarnation)+
        " level="+(current?(recording->second.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY?std::string("primary"):std::string("secondary")):std::string("released"))+
        " rootQueriesSinceCapture="+std::to_string(snapshotSequence_-u.snapshotSequenceAtReserve)+
        " totalRootQueries="+std::to_string(snapshotSequence_)+
        " unknownRoots="+std::to_string(unknownSubmissionRoots_)+" rejectedRoots="+std::to_string(rejectedSubmissionRoots_);
    if(current)text+=" directQueries="+std::to_string(recording->second.submissionQueries)+" directGraph="+recording->second.submissionGraph;
    // Look up immediate ExecuteCommands parents only for this bounded diagnostic.
    // Parent query/refusal facts do not confer submission or lifetime rights.
    size_t parents=0,inspected=0;bool truncated=false;
    constexpr size_t scanBudget=4096;
    for(const auto& [buffer,parent]:recordings_){
        (void)buffer;
        if(inspected++>=scanBudget){truncated=true;break;}
        bool linked=false;
        for(const auto& child:parent.children){
            if(inspected++>=scanBudget){truncated=true;break;}
            if(child.buffer==u.buffer&&child.incarnation==u.incarnation){linked=true;break;}
        }
        if(truncated)break;
        if(!linked)continue;
        ++parents;if(parents>8)continue;
        text+=" parent=[incarnation="+std::to_string(parent.incarnation)+" executable="+std::to_string(parent.executable)+
            " queries="+std::to_string(parent.submissionQueries)+" graph="+parent.submissionGraph+
            " children="+std::to_string(parent.children.size())+"]";
    }
    return text+" observedParentCount="+std::to_string(parents)+" parentScanTruncated="+std::to_string(truncated);
}
uint32_t VkNrRecordingOwner::CommandFamily(VkCommandBuffer command) const
{
    std::lock_guard lock(mutex_);const auto i=recordings_.find(command);
    return i!=recordings_.end()&&i->second.recording ? i->second.family : UINT32_MAX;
}
void VkNrRecordingOwner::OnDeviceDestroyed(VkDevice device)
{
    std::lock_guard lock(mutex_);
    for (auto receipt = submissions_.begin(); receipt != submissions_.end();) {
        const auto& ids = receipt->second.value.uses;
        if (std::any_of(ids.begin(), ids.end(), [&](auto id) { return uses_.contains(id.value) && uses_.at(id.value).device == device; })) receipt = submissions_.erase(receipt); else ++receipt;
    }
    for (auto found = recordings_.begin(); found != recordings_.end();) { if (found->second.device == device) { Close(found->second,"device_destroy");found = recordings_.erase(found); } else ++found; }
    for (auto found = uses_.begin(); found != uses_.end();) { if (found->second.device == device) found = uses_.erase(found); else ++found; }
    for (auto found = pools_.begin(); found != pools_.end();) { if (found->second.device == device) found = pools_.erase(found); else ++found; }
}
} // namespace DlssNr
