namespace {
std::optional<VkNrRecordedOutput> RefuseVkNrFinalColor(const char* reason){
    static thread_local uint64_t calls=0,reports=0;static thread_local std::string last;
    const auto n=++calls;
    if(reports<32&&(n<=3||last!=reason||n%1024==0)){
        ++reports;last=reason;LOG_INFO("Vulkan public pre-FG final color: refused reason=[{}] calls={}",reason,n);
    }
    return {};
}
bool ReleaseFinalCarrier(VkNrGenerationPayload& payload){
    auto& c=static_cast<VkFinalColorCarrier&>(payload);
    c.color.reset();for(auto* image:{&c.target,&c.depth,&c.motion,&c.linear,&c.modelTarget,&c.encoded}){
        if(image->view)vkDestroyImageView(c.device,image->view,nullptr);
        if(image->image)vkDestroyImage(c.device,image->image,nullptr);
        if(image->memory)vkFreeMemory(c.device,image->memory,nullptr);*image={};
    }return true;
}
VkNrGenerationCreation CreateFinalCarrier(const VkNrGenerationKey& key,VkNrUseId,uint64_t remaining){
    auto owned=std::make_unique<VkFinalColorCarrier>();auto& c=*owned;c.device=ActiveVk().device;
    const auto extent=key.frame.output;
    const bool pq=key.frame.representation.recipeRevision==2;
    bool ok=CreateImage(c.target,extent.width,extent.height,key.targetFormat,true,&remaining,pq?(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT):0)&&
        CreateImage(c.depth,extent.width,extent.height,VK_FORMAT_R32_SFLOAT,false,&remaining)&&
        CreateImage(c.motion,extent.width,extent.height,VK_FORMAT_R16G16_SFLOAT,false,&remaining);
    if(pq){VkFormatProperties packed{},linear{};
        vkGetPhysicalDeviceFormatProperties(ActiveVk().physicalDevice,key.targetFormat,&packed);
        vkGetPhysicalDeviceFormatProperties(ActiveVk().physicalDevice,VK_FORMAT_R16G16B16A16_SFLOAT,&linear);
        ok=ok&&(packed.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT)&&(linear.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT);
        ok=ok&&CreateImage(c.linear,extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,true,&remaining)&&
        CreateImage(c.modelTarget,extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,true,&remaining)&&
        CreateImage(c.encoded,extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT,true,&remaining);
        if(ok){c.color=std::make_unique<PresentColor_Vk>(c.device,extent);ok=c.color->IsInit();}}
    c.privateBytes=c.target.bytes+c.depth.bytes+c.motion.bytes+c.linear.bytes+c.modelTarget.bytes+c.encoded.bytes;
    return {std::move(owned),ok,false,ok?"":"Pre-FG private final-color images or allocation budget unavailable"};
}
void ClearFinalSource(VkState& state){if(state.finalColorSource)VulkanNrRecordings().ReleaseUse(state.finalColorSource->observation.use);
    state.finalColorSource.reset();state.finalColorRequest={};state.finalColorExposure.reset();}
}
std::optional<VkNrRecordedOutput> SelectedVkNrOutput(VkCommandBuffer cb){
    if(auto native=RecordedVkNrOutput(VkNrRoute::Native,cb))return native;return RecordedVkNrOutput(VkNrRoute::Present,cb);
}
void ClearSelectedVkNrFinalColor(VkCommandBuffer cb){std::lock_guard recordLock(g_nativeRecordMutex);std::lock_guard lock(g_vkMutex);
 for(auto* root:{&g_nativeVk,&g_presentVk})if(root->finalColorSource&&VulkanNrRecordings().UseOnRecording(root->finalColorSource->observation.use,cb))ClearFinalSource(*root);}
void ObserveSelectedVkNrFinalColor(VkCommandBuffer cb,NVSDK_NGX_Parameter* params,VkInstance instance,
    VkPhysicalDevice physical,VkDevice device,const VkNrTemporalMetadata& temporal,const NrConfigSnapshot<Config>& cfg,
    const VkNrEvaluationIdentity& selected){
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted())return;
    auto& recordings=VulkanNrRecordings();
    if(!params||!cb||!cfg.GetDlssNrRuntimeSnapshot().enabled||cfg.IsPrivateDiagnostic()||!temporal.providerFrameKnown||
       !temporal.providerGeneration||!selected||selected.commandBuffer!=cb||selected.incarnation!=recordings.Incarnation(cb)||
       selected.featureGeneration!=temporal.featureGeneration||recordings.ModelRecordingReason(cb)||!recordings.BindingsQualified(cb))return;
    const bool present=cfg.DlssNrRoute.value_or_default()!=0;
    if(!present&&!cfg.DlssNrRunBeforeSr.value_or_default())return;
    NVSDK_NGX_Resource_VK* output=nullptr;VkFrame::Failure failure;
    if(params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&output))!=NVSDK_NGX_Result_Success||
       !VkFrame::ValidateImage("SelectedSR.FinalColor",output,false,failure))return;
    std::lock_guard recordLock(g_nativeRecordMutex);std::lock_guard lock(g_vkMutex);
    auto& root=present?g_presentVk:g_nativeVk;ClearFinalSource(root);
    if(g_vkSessionClosed||g_vkShutdownFailed||ClassifyVkRouteDevice(root.device,g_vkRuntime.Device(),device)==VkRouteDeviceUse::Conflict)return;
    const auto family=recordings.CommandFamily(cb);
    if(!VkNrRecordingQueueContext(device,family))return;
    const auto& image=output->Resource.ImageViewInfo;
    if(!present&&(!root.resources||!root.generations||!root.generations->AppliedKey()||!root.preSrDelivered||!root.performanceUse||root.performanceEvaluation!=selected||!root.chain.deliverable||root.chain.completed!=root.chain.requested))return;
    auto use=present?VulkanNrRecordings().Reserve(cb,VkNrCompletionDeviceGeneration(device)):std::optional(root.performanceUse);
    if(!use||!recordings.UseOnRecording(*use,cb)||!ReserveVkNrCompletion(device,*use)||!recordings.RetainUse(*use))return;
    VkNrFinalColorSource source;source.selectedSrSucceeded=true;source.performance=!present;
    auto& observation=source.observation;observation.use=*use;observation.resource=*output;observation.gameInput=image.Image;
    // The selected evaluation and tagged image identify this source. Neither a
    // unique device queue nor an extent-matched acquired image identifies it.
    observation.frame=present?VkNrFrameContract{}:root.generations->AppliedKey()->frame;
    observation.frame.deviceGeneration=VkNrCompletionDeviceGeneration(device);
    observation.frame.queue=VK_NULL_HANDLE;observation.frame.queueFamily=family;
    observation.frame.swapchain=VK_NULL_HANDLE;observation.frame.swapchainGeneration=0;
    observation.frame.swapchainImageIndex=UINT32_MAX;observation.frame.acquireGeneration=0;
    observation.frame.evaluation=selected;observation.frame.output={image.Width,image.Height};
    observation.frame.temporal=temporal;observation.frame.route=present?VkNrRoute::Present:VkNrRoute::Native;
    observation.frame.placement=VkNrPlacement::AfterSR;observation.frame.routeEpoch=g_vkRuntime.Epoch();
    if(!present){observation.requestedPasses=root.chain.requested;observation.completedPasses=root.chain.completed;observation.frame.representation.format=image.Format;}
    root.finalColorSource=source;root.finalColorRequest.contract=observation.frame;
    auto& request=root.finalColorRequest;request.route=observation.frame.route;request.commandBuffer=cb;
    request.instance=instance;request.physicalDevice=physical;request.device=device;request.settings=std::make_shared<NrConfigSnapshot<Config>>(cfg);
    params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&request.preExposure);
    NVSDK_NGX_Resource_VK* exposure=nullptr;params->Get(NVSDK_NGX_Parameter_ExposureTexture,reinterpret_cast<void**>(&exposure));
    if(exposure&&exposure->Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)root.finalColorExposure=*exposure;
    VkNrStreamlineSourceScope::Candidate(*use);
}
std::optional<VkNrRecordedOutput> PrepareVkNrFinalColor(VkCommandBuffer cb,uint64_t provider,uint64_t frame,
    uint32_t viewport,VkNrTaggedColor tagged){
    FinalFallback::RenderScope renderAdmission;
    if(!renderAdmission.Admitted())return RefuseVkNrFinalColor("Output handoff is active");
    auto current=TryNrConfigSnapshot(*Config::Instance());
    if(!current||!current->GetDlssNrRuntimeSnapshot().enabled||!current->DlssNrApplyModel.value_or_default()||
       current->DlssNrHoldFrame.value_or_default()||current->DlssNrDebugView.value_or_default()||current->DlssNrCompare.value_or_default())
        return RefuseVkNrFinalColor("NR final-color settings are disabled or diagnostic-only");
    const bool present=current->DlssNrRoute.value_or_default()!=0;
    std::lock_guard recordLock(g_nativeRecordMutex);std::unique_lock lock(g_vkMutex);ScopedVkSession active(present?VkNrRoute::Present:VkNrRoute::Native);
    auto& root=ActiveVk();auto& recordings=VulkanNrRecordings();
    if(!root.finalColorSource)return RefuseVkNrFinalColor("No retained selected public SR/RR final-color source");
    if(!root.finalColorRequest.settings||!root.finalColorRequest.settings->SameConfiguration(*current))
        return RefuseVkNrFinalColor("NR settings changed since the selected source evaluation");
    if(!VkNrFinalColorAdmits(*root.finalColorSource,recordings,cb,provider,frame,viewport,tagged.image,root.finalColorRequest.contract.evaluation))
        return RefuseVkNrFinalColor("Tagged HUDless image or public frame/viewport/recording identity differs from the selected source");
    if(provider&&!VkNrStreamlineSourceScope::SourceSucceeded(root.finalColorSource->observation.use))
        return RefuseVkNrFinalColor("Selected public SR/RR evaluation has not returned success");
    auto source=*root.finalColorSource;auto request=root.finalColorRequest;auto original=source.observation.resource;
    const auto& input=original.Resource.ImageViewInfo;
    if(tagged.format!=input.Format||tagged.extent.width!=input.Width||tagged.extent.height!=input.Height)
        return RefuseVkNrFinalColor("Tagged HUDless format or extent differs from selected source");
    const bool nativeHdr=(source.observation.frame.temporal->featureFlags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)!=0;
    auto recipe=SelectVkPresentColorRecipe(input.Format,request.contract.representation.colorSpace);
    if(present&&nativeHdr){
        if(request.contract.representation.format!=input.Format||!recipe.supported||recipe.revision!=2){
            root.tuningMessage="Vulkan FG HDR needs a matching observed packed PQ final-color representation; FP16 scRGB DLSS-G is unsupported";
            return RefuseVkNrFinalColor(root.tuningMessage.c_str());}
        request.contract.representation.recipeRevision=recipe.revision;
    }else if(present){request.contract.representation={input.Format,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1};}
    const bool pq=present&&nativeHdr&&recipe.revision==2;
    vk_state::CommandBufferState saved;auto& tracker=Vulkan_wDx12::cmdBufferStateTracker;
    if(!tracker.CaptureNrState(cb,saved)||!saved.Recording||saved.InRenderPass)
        return RefuseVkNrFinalColor("Public tag command state is unavailable or inside a render pass");
    auto observedLayout=saved.ImageLayouts.find(input.Image);auto observedRange=saved.ImageLayoutRanges.find(input.Image);
    const auto usage=present?VK_IMAGE_USAGE_SAMPLED_BIT:VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto rights=VulkanNrImageFacts().Rights(request.device,original,usage,false);
    if(!rights||observedLayout==saved.ImageLayouts.end()||observedRange==saved.ImageLayoutRanges.end()||
       observedLayout->second==VK_IMAGE_LAYOUT_UNDEFINED||observedLayout->second==VK_IMAGE_LAYOUT_PREINITIALIZED||
       observedRange->second.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||observedRange->second.baseMipLevel||observedRange->second.baseArrayLayer||
       !observedRange->second.levelCount||!observedRange->second.layerCount)
        return RefuseVkNrFinalColor("Selected final-color image usage, layout or subresource rights are unavailable");
    if(!root.finalColorAttempt.Claim(source.observation.use))
        return RefuseVkNrFinalColor("Selected final-color source was already attempted");
    root.device=request.device;root.instance=request.instance;root.physicalDevice=request.physicalDevice;
    VkNrGenerationKey key;key.frame=request.contract;key.frame.work=key.frame.output;key.targetFormat=input.Format;
    key.colorFormat=input.Format;key.creationFlags=present?0:1;key.frame.representation.format=input.Format;
    auto use=recordings.Reserve(cb,key.frame.deviceGeneration);
    if(!use||!ReserveVkNrCompletion(request.device,*use))return {};
    if(!g_finalColorCarriers)g_finalColorCarriers=std::make_unique<VkNrGenerationOwner>(recordings,CreateFinalCarrier,ReleaseFinalCarrier,8,512ull*1024*1024);
    constexpr uint64_t total=1536ull*1024*1024;const auto others=GenerationBytesVk()-g_finalColorCarriers->PrivateBytes();
    if(others>=total)return {};g_finalColorCarriers->SetPrivateBudget(std::min<uint64_t>(512ull*1024*1024,total-others));
    auto prepared=g_finalColorCarriers->Prepare(key,*use);if(!prepared.ready){root.tuningMessage=prepared.reason;return {};}
    g_finalColorCarriers->Commit(prepared.generation);
    auto& carrier=*static_cast<VkFinalColorCarrier*>(g_finalColorCarriers->Payload(prepared.generation));
    struct Replay {vk_state::CommandBufferStateTracker& tracker;VkCommandBuffer cb;vk_state::CommandBufferState& saved;bool model=false,done=false,ok=false;
        bool Finish(){if(!done){done=true;vk_state::ReplayParams p;p.RequiredGraphicsSetMask=UINT32_MAX;
            p.ReplayComputeToo=p.ReplayVertexIndex=true;p.ReplayPushConstants=model;ok=tracker.ReplaySaved(cb,saved,p);}return ok;}
        ~Replay(){Finish();}
    } replay{tracker,cb,saved,present};
    auto sourceBarrier=[&](VkImageLayout from,VkImageLayout to){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=input.Image;
        b.subresourceRange=input.SubresourceRange;b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=b.srcAccessMask;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
    const auto workingLayout=present?(pq?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL):VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sourceBarrier(observedLayout->second,workingLayout);
    struct RestoreSource {std::function<void()> action;~RestoreSource(){action();}} restore{[&]{sourceBarrier(workingLayout,observedLayout->second);}};
    if(!present){
        Transition(cb,carrier.target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={input.Width,input.Height,1};
        vkCmdCopyImage(cb,input.Image,workingLayout,carrier.target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        Transition(cb,carrier.target,VK_IMAGE_LAYOUT_GENERAL);
        auto output=source.observation;output.use=*use;output.resource=carrier.target.ngx;output.memory=carrier.target.memory;
        output.contentRevision=use->value;output.commandSerial=recordings.CommandSerial(cb);
        if(!replay.Finish()){root.candidate.reset();root.tuningMessage="Final-color command-state restoration failed";return {};}
        root.candidate=output;
        VkNrStreamlineSourceScope::Candidate(output.use);
        // Public SR has already succeeded; its later tagged copy has identical authentic identity.
        if(provider&&!VkNrStreamlineSourceScope::DerivedSucceeded(output.use,source.observation.use))return {};
        return output;
    }
    for(auto* guide:{&carrier.depth,&carrier.motion}){
        Transition(cb,*guide,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue clear{};
        if(guide==&carrier.depth)clear.float32[0]=1.f;
        auto range=guide->ngx.Resource.ImageViewInfo.SubresourceRange;
        vkCmdClearColorImage(cb,guide->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
        Transition(cb,*guide,VK_IMAGE_LAYOUT_GENERAL);
    }
    Transition(cb,carrier.target,VK_IMAGE_LAYOUT_GENERAL);
    request.use=*use;request.color=&original;request.targetColor=&carrier.target.ngx;request.depth=&carrier.depth.ngx;request.motion=&carrier.motion.ngx;
    request.exposure=nullptr;request.preExposure=1.f;
    request.providerTemporal=source.observation.frame.temporal;request.resolutionMetadata=source.observation.frame.temporal;
    const auto guides=SelectVulkanRecordingGuides(request.contract,cb,input.Image);
    request.presentInput=PresentInputDecision::Choose(PresentInput::Selected(*current),guides.has_value(),true,PresentResolution::Selected(*current));
    if(request.presentInput->input==PresentInputDecision::InputClass::Refused)return {};
    request.contract.temporal.reset();
    if(request.presentInput->input==PresentInputDecision::InputClass::Guided){
        request.depth=&guides->images->depth;request.motion=&guides->images->motion;request.contract.temporal=guides->contract.temporal;
        request.contract.temporal->depth.x=request.contract.temporal->depth.y=0;
        request.contract.temporal->motion.x=request.contract.temporal->motion.y=0;
        request.bindGuides=[lease=*guides,queue=request.contract.queue](VkNrUseId id){return BindVulkanPresentGuides(lease,id,queue);};
    }
    request.ownedPresentInputs=false;request.finalColorOnly=true;request.width=request.guideWidth=input.Width;request.height=request.guideHeight=input.Height;
    std::optional<VkNrReservation> colorReservation;
    if(pq){colorReservation=carrier.color->Reserve(*use);if(!colorReservation)return {};
        Transition(cb,carrier.linear,VK_IMAGE_LAYOUT_GENERAL);Transition(cb,carrier.modelTarget,VK_IMAGE_LAYOUT_GENERAL);
        if(!carrier.color->Decode(cb,*colorReservation,input.ImageView,carrier.linear.view,request.contract.representation))return {};
        Transition(cb,carrier.linear,VK_IMAGE_LAYOUT_GENERAL);request.color=&carrier.linear.ngx;request.targetColor=&carrier.modelTarget.ngx;
    }
    request.colorLayout=VK_IMAGE_LAYOUT_GENERAL;request.gameHdr=(source.observation.frame.temporal->featureFlags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)!=0;
    lock.unlock();
    auto result=g_presentSession.Record(request,[&](const VkFrameRequest& recording,bool reset){
        if(reset){std::lock_guard stateLock(g_vkMutex);g_presentVk.reset=true;++g_presentVk.resetRevision;}
        {std::lock_guard stateLock(g_vkMutex);if(!g_finalColorCarriers->Retain(prepared.generation,recording.use))return VkRecordResult{VkRecordStatus::NoWork,"Pre-FG carrier lifetime unavailable"};}
        return EvaluateVkChain(recording,nullptr,*current);});
    lock.lock();
    if(!result.finalOutput)return {};
    auto output=*result.finalOutput;output.gameInput=input.Image;
    if(pq){
        auto* composed=PassStateVk(VkNrRoute::Present,result.completedPasses-1).resources;if(!composed)return {};
        Transition(cb,composed->composed,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);Transition(cb,carrier.encoded,VK_IMAGE_LAYOUT_GENERAL);
        if(!carrier.color->Encode(cb,*colorReservation,input.ImageView,composed->composed.view,carrier.encoded.view,request.contract.representation))return {};
        Transition(cb,carrier.encoded,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);Transition(cb,carrier.target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]={static_cast<int>(input.Width),static_cast<int>(input.Height),1};
        vkCmdBlitImage(cb,carrier.encoded.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,carrier.target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        Transition(cb,carrier.target,VK_IMAGE_LAYOUT_GENERAL);output.resource=carrier.target.ngx;output.memory=carrier.target.memory;
        output.gameInput=input.Image;output.frame.representation=request.contract.representation;
    }
    if(!replay.Finish()){root.candidate.reset();root.tuningMessage="Final-color command-state restoration failed";return {};}
    output.commandSerial=recordings.CommandSerial(cb);root.candidate=output;
    if(provider&&!VkNrStreamlineSourceScope::DerivedSucceeded(output.use,source.observation.use))return {};
    return output;
}
