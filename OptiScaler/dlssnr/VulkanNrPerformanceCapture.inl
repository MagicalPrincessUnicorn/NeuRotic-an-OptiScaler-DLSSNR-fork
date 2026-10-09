// Included in the native Vulkan recorder so the temporary SR feature shares its
// device, recording, generation budget and teardown authority.
namespace {
thread_local std::array<unsigned,12> performanceSignature{};
thread_local VkCommandBuffer performanceCommand=VK_NULL_HANDLE;
thread_local uint32_t performanceLiveId=0;
bool ReleasePerformancePair(VkNrGenerationPayload& payload){
    auto& pair=static_cast<VkPerformancePair&>(payload);
    if(pair.feature){
        auto release=NVNGXProxy::VULKAN_ReleaseFeature();auto* lock=ActiveVk().controlLock;
        if(lock)lock->unlock();auto result=release?release(pair.feature):NVSDK_NGX_Result_Fail;if(lock)lock->lock();
        if(result!=NVSDK_NGX_Result_Success)return false;pair.feature=nullptr;
    }
    if(pair.parameters){auto destroy=NVNGXProxy::VULKAN_DestroyParameters();
        if(!destroy||destroy(pair.parameters)!=NVSDK_NGX_Result_Success)return false;pair.parameters=nullptr;}
    DestroyImage(pair.before);DestroyImage(pair.after);return true;
}
VkNrGenerationCreation CreatePerformancePair(const VkNrGenerationKey& key,VkNrUseId use,uint64_t remaining){
    auto owned=std::make_unique<VkPerformancePair>();auto& pair=*owned;pair.creation=use;
    auto allocate=NVNGXProxy::VULKAN_AllocateParameters();auto create=NVNGXProxy::VULKAN_CreateFeature();
    if(!NVNGXProxy::IsVulkanInited()||!allocate||!create||!NVNGXProxy::VULKAN_EvaluateFeature()||
       !NVNGXProxy::VULKAN_ReleaseFeature()||!NVNGXProxy::VULKAN_DestroyParameters())
        return {std::move(owned),false,false,"Performance capture awaits native Vulkan DLSS exports"};
    if(!CreateImage(pair.before,key.srOutput.width,key.srOutput.height,key.targetFormat,true,&remaining)||
       !CreateImage(pair.after,key.srOutput.width,key.srOutput.height,key.targetFormat,true,&remaining)){
        pair.privateBytes=pair.before.bytes+pair.after.bytes;
        return {std::move(owned),false,false,"Performance private outputs exceed available allocation rights or budget"};
    }
    pair.privateBytes=pair.before.bytes+pair.after.bytes;
    if(allocate(&pair.parameters)!=NVSDK_NGX_Result_Success||!pair.parameters)
        return {std::move(owned),false,false,"Performance private SR parameter allocation failed"};
    for(size_t i=0;i<performanceSignature.size();++i)
        if(!VkTuning::WriteChecked(pair.parameters,VkNrPerformanceCreationKeys[i],performanceSignature[i]))
            return {std::move(owned),false,false,"Performance private SR creation setting rejected"};
    if(!VkTuning::WriteChecked(pair.parameters,NVSDK_NGX_Parameter_CreationNodeMask,1u)||
       !VkTuning::WriteChecked(pair.parameters,NVSDK_NGX_Parameter_VisibilityNodeMask,1u))
        return {std::move(owned),false,false,"Performance private SR node settings rejected"};
    auto* lock=ActiveVk().controlLock;lock->unlock();
    const auto result=create(performanceCommand,NVSDK_NGX_Feature_SuperSampling,pair.parameters,&pair.feature);
    lock->lock();
    if(pair.feature&&pair.feature->Id==performanceLiveId){pair.feature=nullptr;
        return {std::move(owned),false,true,"Performance provider did not create a distinct temporary SR feature"};}
    pair.creationSeed.Record(use,result==NVSDK_NGX_Result_Success&&pair.feature);
    return {std::move(owned),result==NVSDK_NGX_Result_Success&&pair.feature,true,
        result==NVSDK_NGX_Result_Success?"":"Performance private SR creation failed"};
}
}
void RecordPerformanceCaptureVk(VkCommandBuffer cb,NVSDK_NGX_Parameter* params,const NVSDK_NGX_Handle* live){
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted())return;
    if(!WantsVulkanNrCapture()||!cb||!params||!live)return;
    auto cfg=TryNrConfigSnapshot(*Config::Instance());
    if(!cfg||cfg->DlssNrRoute.value_or_default()!=0||!cfg->DlssNrRunBeforeSr.value_or_default())return;
    std::lock_guard recordLock(g_nativeRecordMutex);std::unique_lock lock(g_vkMutex);ScopedVkSession active(VkNrRoute::Native);
    auto& root=ActiveVk();root.controlLock=&lock;
    struct EndControl {VkState& state;~EndControl(){state.controlLock=nullptr;}} end{root};
    auto wait=[](const char* reason){VulkanNrCaptureWaiting(reason);};
    if(!root.resources||!root.performanceSource||!root.performanceUse||!root.chain.deliverable||
       root.chain.requested!=root.chain.completed||!VulkanNrRecordings().UseOnRecording(root.performanceUse,cb)||
       root.performanceSerial!=VulkanNrRecordings().CommandSerial(cb)){
        wait("Performance capture awaits a complete unchanged pre-SR chain on this recording");return;}
    if(!ReadVkNrPerformanceSignature(params,performanceSignature)){
        wait("Performance capture awaits complete current native DLSS creation settings");return;}
    NVSDK_NGX_Resource_VK* target=nullptr;VkFrame::Failure failure;
    if(params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&target))!=NVSDK_NGX_Result_Success||
       !VkFrame::ValidateImage("Performance.Output",target,true,failure))return;
    auto& source=*root.performanceSource;const auto& input=source.Resource.ImageViewInfo;
    const auto& output=target->Resource.ImageViewInfo;unsigned x=0,y=0;
    params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,&x);params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,&y);
    const auto applied=root.generations?root.generations->AppliedKey():std::nullopt;
    if(!applied||!applied->frame.temporal||applied->frame.temporal->color.x||applied->frame.temporal->color.y||
       applied->frame.output.width!=input.Width||applied->frame.output.height!=input.Height){
        wait("Performance paired capture requires full zero-origin SR color inputs; active-subrect NR remains available");return;}
    if(input.Width!=performanceSignature[0]||input.Height!=performanceSignature[1]||
       output.Width!=performanceSignature[2]||output.Height!=performanceSignature[3]||x||y||
       !VulkanNrImageFacts().Rights(root.device,source,VK_IMAGE_USAGE_SAMPLED_BIT,false)){
        wait("Performance capture needs full native SR extents and observed sampled source rights");return;}
    vk_state::CommandBufferState saved;auto& tracker=Vulkan_wDx12::cmdBufferStateTracker;
    if(!tracker.CaptureNrState(cb,saved)||!saved.Recording||saved.InRenderPass)return;
    auto originalLayout=saved.ImageLayouts.find(input.Image);
    if(originalLayout==saved.ImageLayouts.end()||originalLayout->second==VK_IMAGE_LAYOUT_UNDEFINED){
        wait("Performance capture source layout is unproved");return;}
    struct Replay {vk_state::CommandBufferStateTracker& tracker;VkCommandBuffer cb;vk_state::CommandBufferState& state;
        ~Replay(){vk_state::ReplayParams p;p.RequiredGraphicsSetMask=UINT32_MAX;p.ReplayComputeToo=p.ReplayVertexIndex=true;tracker.ReplaySaved(cb,state,p);}
    } replay{tracker,cb,saved};
    auto key=*root.generations->AppliedKey();key.srOutput={output.Width,output.Height};key.targetFormat=output.Format;
    key.preSrEpoch=1469598103934665603ull;for(auto value:performanceSignature)key.preSrEpoch=(key.preSrEpoch^value)*1099511628211ull;
    performanceCommand=cb;performanceLiveId=live->Id;
    if(!g_performancePairs)g_performancePairs=std::make_unique<VkNrGenerationOwner>(VulkanNrRecordings(),CreatePerformancePair,ReleasePerformancePair,8,512ull*1024*1024);
    const auto others=GenerationBytesVk()-g_performancePairs->PrivateBytes();constexpr uint64_t total=1536ull*1024*1024;
    if(others>=total){wait("Performance capture waits for retained generations to retire");return;}
    g_performancePairs->SetPrivateBudget(std::min<uint64_t>(512ull*1024*1024,total-others));
    auto prepared=g_performancePairs->Prepare(key,root.performanceUse);
    if(!prepared.ready){VulkanNrCaptureWaiting(prepared.reason);return;}
    g_performancePairs->Commit(prepared.generation);
    auto& pair=*static_cast<VkPerformancePair*>(g_performancePairs->Payload(prepared.generation));
    if(pair.creation==root.performanceUse||!pair.creationSeed.Ready()){
        wait("Performance capture awaits actual temporary SR creation completion");return;}
    auto& carrier=root.generations->AppliedKey()->privateDlaa?root.resources->reJitter:root.resources->preSr;
    // The private pair and the following live NGX evaluation sample this carrier.
    Transition(cb,carrier,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);Transition(cb,pair.before,VK_IMAGE_LAYOUT_GENERAL);Transition(cb,pair.after,VK_IMAGE_LAYOUT_GENERAL);
    auto sourceBarrier=[&](VkImageLayout from,VkImageLayout to){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image=input.Image;b.subresourceRange=input.SubresourceRange;b.oldLayout=from;b.newLayout=to;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=b.srcAccessMask;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
    sourceBarrier(originalLayout->second,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    struct SourceRestore {std::function<void()> restore;~SourceRestore(){restore();}} restore{[&]{sourceBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,originalLayout->second);}};
    const auto evaluate=NVNGXProxy::VULKAN_EvaluateFeature();
    const bool ok=Screenshots::FreshSrPair<NVSDK_NGX_Parameter,void>(params,&source,&carrier.ngx,&pair.before.ngx,&pair.after.ngx,[&]{
        lock.unlock();NVSDK_NGX_Result result;try{result=evaluate(cb,pair.feature,params,nullptr);}catch(...){lock.lock();throw;}lock.lock();
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
        return result==NVSDK_NGX_Result_Success;});
    if(!ok){wait("Performance private SR evaluation failed; live parameters restored");return;}
    VkNrCaptureRequest capture;capture.frame=key.frame;capture.frame.output=key.srOutput;capture.frame.representation.format=output.Format;
    capture.use=root.performanceUse;capture.requestedPasses=root.chain.requested;capture.completedPasses=root.chain.completed;
    capture.settings="Performance same-frame full-resolution pair; two temporary native DLSS evaluations with Reset=1; live history unchanged; accumulated live history is not reproduced\n"+cfg->Describe();
    const float white=root.diagnosticConstants.Passthrough?0.f:root.diagnosticConstants.WhitePoint;
    capture.images={{"original",pair.before.image,pair.before.format,pair.before.layout,key.srOutput,white},
                    {"final",pair.after.image,pair.after.format,pair.after.layout,key.srOutput,white}};
    RecordVulkanNrCapture(capture,cb,root.physicalDevice,root.device);
}
