#include "VulkanNrStreamline.h"
#include "VulkanNrPresentFrame.h"
#include <mfg/MfgOptionsSnapshot.h>
namespace DlssNr {
namespace {thread_local std::optional<VkNrPublicFrame> source;thread_local std::vector<VkNrUseId> uses;
 std::mutex completedMutex;std::vector<VkNrUseId> completed;
}
void VulkanNrStreamline::PrepareOutput(Prepare p){std::lock_guard lock(mutex_);prepare_=std::move(p);}
void VulkanNrStreamline::LookupOutput(Lookup l){std::lock_guard lock(mutex_);lookup_=std::move(l);}
void VulkanNrStreamline::Provider(uint64_t generation,bool qualified){std::lock_guard lock(mutex_);
 if(provider_!=generation){if(provider_)fg_.RevokeProvider(provider_);views_.clear();viewsOverflow_=false;loaded_.reset();provider_=generation;}qualified_=qualified&&generation;
 VulkanPublicPresentFrames().Provider(qualified_?generation:0);}
uint64_t VulkanNrStreamline::Generation() const{std::lock_guard lock(mutex_);return qualified_?provider_:0;}
void VulkanNrStreamline::FeatureLoaded(bool loaded,bool succeeded){std::lock_guard lock(mutex_);
 // An actual successful public unload proves no active FG hooks. It does not
 // prove asynchronous image consumers have released their resources.
 if(provider_&&(!succeeded||!loaded||(loaded_.has_value()&&*loaded_!=loaded)))fg_.RevokeProvider(provider_);
 if(!succeeded){loaded_.reset();views_.clear();viewsOverflow_=false;return;}
 if(!loaded_.has_value()||*loaded_!=loaded){views_.clear();viewsOverflow_=false;}loaded_=loaded;
}
void VulkanNrStreamline::Options(uint32_t v,const sl::DLSSGOptions& o,bool ok){std::lock_guard lock(mutex_);if(views_.size()>=64&&!views_.contains(v)){viewsOverflow_=true;return;}
 auto& s=views_[v];const auto decoded=ok?Neurotic::Mfg::CaptureOptions(o):Neurotic::Mfg::MfgOwnedOptions{};
 s.options=ok&&decoded.supported;
 if(!s.options)return; // Preserve the last successful scalar facts, but revoke freshness.
 const auto& value=decoded.value;s.requested=value.numFramesToGenerate;
 s.activity=value.mode==sl::DLSSGMode::eOff?VkNrFgActivity::Off:value.mode==sl::DLSSGMode::eOn?VkNrFgActivity::On:VkNrFgActivity::Unknown;
 // A new successful Off setter is sufficient at its options boundary; an
 // earlier failed query cannot veto it. A later failed query revokes freshness.
 if(s.activity==VkNrFgActivity::Off)s.stateObserved=false;
 s.compatible=value.mode==sl::DLSSGMode::eOn&&!decoded.hasExtensions&&value.queueParallelismMode==sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue&&
 value.enableUserInterfaceRecomposition==sl::eFalse&&static_cast<uint32_t>(value.flags&sl::DLSSGFlags::eShowOnlyInterpolatedFrame)==0;
 if(s.activity==VkNrFgActivity::Off&&provider_)fg_.RevokeProvider(provider_);
}
void VulkanNrStreamline::State(uint32_t v,const sl::DLSSGState& value,bool ok){std::lock_guard lock(mutex_);if(views_.size()>=64&&!views_.contains(v)){viewsOverflow_=true;return;}
 auto& s=views_[v];s.stateObserved=true;s.state=ok&&value.structVersion>=1&&value.structVersion<=4&&value.structType==sl::DLSSGState::s_structType&&value.status==sl::DLSSGStatus::eOk;
 if(s.state)s.max=value.structVersion>=2&&value.numFramesToGenerateMax<=63?value.numFramesToGenerateMax:0;
}
VkNrFgActivity VulkanNrStreamline::Activity() const{std::lock_guard lock(mutex_);if(!qualified_)return VkNrFgActivity::Unknown;
 if(loaded_==false)return VkNrFgActivity::Off;if(views_.empty())return VkNrFgActivity::Unknown;
 auto activity=viewsOverflow_?VkNrFgActivity::Unknown:VkNrFgActivity::Off;for(const auto& [v,s]:views_){(void)v;if(!s.options||(s.stateObserved&&!s.state)){activity=VkNrFgActivity::Unknown;continue;}if(s.activity==VkNrFgActivity::On&&s.state)return s.activity;if(s.activity!=VkNrFgActivity::Off)activity=VkNrFgActivity::Unknown;}return activity;}
std::string VulkanNrStreamline::Reason(uint32_t v) const{std::lock_guard lock(mutex_);
 if(!qualified_)return "Vulkan Streamline exports and provider generation are unqualified";
 if(loaded_==false)return "Vulkan FG feature is unloaded";
 auto it=views_.find(v);if(it==views_.end()||!it->second.options||!it->second.state)return "Vulkan FG options/state have not both succeeded";
 const auto& s=it->second;if(!s.compatible)return "Vulkan FG requires fixed On mode, blocked client queue and no UI recomposition";
 if(!s.requested||s.requested>s.max)return "Vulkan FG ratio exceeds the observed provider ceiling";return {};
}
VkNrTagDecision VulkanNrStreamline::BeforeTags(const sl::FrameToken* token,const sl::ViewportHandle& viewport,std::span<const sl::ResourceTag> tags,sl::CommandBuffer* command){
 VkNrTagDecision d;auto fail=[&](const char* why){d.reason=why;return std::move(d);};
 if(!token||token->structVersion!=1||viewport.structVersion!=1||!command||tags.empty()||tags.size()>64)return fail("Vulkan FG needs an authentic frame, viewport and recording command buffer");
 const auto v=static_cast<uint32_t>(viewport),frame=static_cast<uint32_t>(*token);Lookup lookup;Prepare prepare;uint64_t generation;uint32_t max;
 {std::lock_guard lock(mutex_);auto i=views_.find(v);
  if(!qualified_)return fail("Vulkan FG public provider generation is unqualified");
  if(loaded_==false)return fail("Vulkan FG feature is unloaded");
  if(i==views_.end()||!i->second.options)return fail("Vulkan FG has no successful options for this viewport");
  if(!i->second.state)return fail("Vulkan FG has no successful provider state for this viewport");
  if(!i->second.compatible)return fail("Vulkan FG requires fixed On mode, blocked client queue and no UI recomposition");
  if(!i->second.requested||i->second.requested>i->second.max)return fail("Vulkan FG ratio exceeds the observed provider ceiling");
  lookup=lookup_;prepare=prepare_;generation=provider_;max=i->second.max;}
 size_t index=tags.size();for(size_t i=0;i<tags.size();++i)if(tags[i].type==sl::kBufferTypeHUDLessColor){if(index!=tags.size())return fail("Vulkan FG has ambiguous HUDless tags");index=i;}
 if(index==tags.size())return fail("Vulkan FG needs the matching HUDless real-frame tag");
 const auto& tag=tags[index];const auto* input=tag.resource;
 if(tag.structVersion!=1||tag.next||!input||input->structVersion!=1||input->next||input->type!=sl::ResourceType::eTex2d||
    !input->native||!input->width||!input->height||tag.extent.left||tag.extent.top||
    (tag.extent.width&&tag.extent.width!=input->width)||(tag.extent.height&&tag.extent.height!=input->height))return fail("Vulkan FG requires one full typed HUDless image");
 auto cb=reinterpret_cast<VkCommandBuffer>(command);
 if(recordings_.ModelRecordingReason(cb))return fail("Vulkan FG volatile copy requires a one-time primary command recording");
 auto candidate=lookup?lookup(cb):std::nullopt;
 if(!candidate&&prepare)candidate=prepare(cb,generation,frame,v,{reinterpret_cast<VkImage>(input->native),static_cast<VkFormat>(input->nativeFormat),{input->width,input->height}});
 if(!candidate||!recordings_.UseOnRecording(candidate->use,cb)||candidate->commandSerial!=recordings_.CommandSerial(cb)||!candidate->frame.temporal)return fail("Vulkan FG has no unchanged complete NR output on this recording");
 const auto& t=*candidate->frame.temporal;if(!t.providerFrameKnown||t.providerGeneration!=generation||t.viewport!=v||t.frameToken!=frame)return fail("Vulkan FG frame or viewport does not match the real NR producer");
 const auto& image=candidate->resource.Resource.ImageViewInfo;
 if(candidate->resource.Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW||!candidate->memory||image.Width!=candidate->frame.output.width||image.Height!=candidate->frame.output.height)return fail("Vulkan FG private resource description is incomplete");

 if(tag.structVersion!=1||tag.next||!input||input->structVersion!=1||input->next||input->type!=sl::ResourceType::eTex2d||input->native!=reinterpret_cast<void*>(candidate->gameInput)||input->nativeFormat!=uint32_t(image.Format)||input->width!=image.Width||input->height!=image.Height||tag.extent.left||tag.extent.top||
 (tag.extent.width&&tag.extent.width!=image.Width)||(tag.extent.height&&tag.extent.height!=image.Height))return fail("Vulkan FG HUDless image, format or subrect differs from the NR output");
 VkNrContribution c;c.frame=candidate->frame;c.producer=candidate->use;c.providerGeneration=generation;c.viewport=v;c.contentRevision=candidate->contentRevision;
 c.image=image.Image;c.view=image.ImageView;c.memory=candidate->memory;c.subresource=image.SubresourceRange;c.layout=VK_IMAGE_LAYOUT_GENERAL;
 c.chain.requested=candidate->requestedPasses;c.chain.completed=candidate->completedPasses;c.chain.deliverable=c.chain.requested&&c.chain.requested==c.chain.completed;c.chain.outputVersion=candidate->contentRevision;c.chain.recording.outputWriteRecorded=true;
 fg_.Publish(c);VkNrFgConsumer consumer{generation,frame,v,c.frame.queue,cb,c.image,c.contentRevision,c.subresource,c.layout,max};
 d.consumer=fg_.Bind(consumer);if(!d.consumer)return fail("Vulkan FG producer or consumer ownership could not be retained");
 d.tags.assign(tags.begin(),tags.end());d.resources.emplace_back();auto& r=d.resources.back();r.type=sl::ResourceType::eTex2d;r.native=reinterpret_cast<void*>(c.image);r.memory=reinterpret_cast<void*>(c.memory);r.view=reinterpret_cast<void*>(c.view);r.state=VK_IMAGE_LAYOUT_GENERAL;
 r.width=image.Width;r.height=image.Height;r.nativeFormat=image.Format;r.mipLevels=1;r.arrayLayers=1;r.flags=0;r.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 d.tags[index].resource=&r;d.tags[index].lifecycle=sl::eOnlyValidNow;d.accepted=true;return d;
}
void VulkanNrStreamline::TagsReturned(const VkNrTagDecision& d,sl::Result result){
 if(d.consumer&&result==sl::Result::eOk)fg_.ConsumerAccepted(*d.consumer);
 // The public volatile-tag contract records any needed source copy into the
 // supplied command buffer (pinned resourceTaggingForFrame + Vulkan::copyResource).
 // CPU return ends the borrowed wrapper lifetime, not source GPU lifetime.
 // Releasing this provider obligation still retains the source through actual
 // copy-command completion AND recording release, including failed calls.
 if(d.consumer)fg_.ConsumerReleased(*d.consumer);fg_.ObserveSubmissions();
}
VkNrStreamlineSourceScope::VkNrStreamlineSourceScope(std::optional<VkNrPublicFrame> v):saved_(source),savedUses_(std::move(uses)){source=v;uses.clear();}
VkNrStreamlineSourceScope::~VkNrStreamlineSourceScope(){source=saved_;uses=std::move(savedUses_);}
void VkNrStreamlineSourceScope::Authenticate(VkCommandBuffer cb,VkNrTemporalMetadata& t){if(source&&source->provider&&source->cmd==cb&&source->viewport!=UINT32_MAX){t.providerFrameKnown=true;t.providerGeneration=source->provider;t.viewport=source->viewport;t.frameToken=source->frame;}}
void VkNrStreamlineSourceScope::Candidate(VkNrUseId use){if(source&&uses.size()<16)uses.push_back(use);}
void VkNrStreamlineSourceScope::Complete(bool ok){if(!ok)return;std::lock_guard lock(completedMutex);for(auto id:uses){if(completed.size()>=128)completed.erase(completed.begin());completed.push_back(id);}}
bool VkNrStreamlineSourceScope::DerivedSucceeded(VkNrUseId child,VkNrUseId parent){std::lock_guard lock(completedMutex);
 if(!child||std::find(completed.begin(),completed.end(),parent)==completed.end())return false;
 if(std::find(completed.begin(),completed.end(),child)==completed.end()){if(completed.size()>=128)completed.erase(completed.begin());completed.push_back(child);}return true;}
bool VkNrStreamlineSourceScope::SourceSucceeded(VkNrUseId id){std::lock_guard lock(completedMutex);return std::find(completed.begin(),completed.end(),id)!=completed.end();}
VulkanNrStreamline& VulkanNrStreamlineAdapter(){static VulkanNrStreamline adapter(VulkanNrRecordings(),VulkanNrFgContributions());return adapter;}
namespace {struct NgxFeature {VkDevice device;uint64_t generation;uint32_t max;};
 std::mutex ngxMutex;std::unordered_map<uint32_t,NgxFeature> ngxFeatures;uint64_t ngxNext=0x2000000000000000ull;
}
void SetVkNrNgxOutputLookup(VulkanNrStreamline::Lookup l,VulkanNrStreamline::Prepare p){
 // No replacement admission until the private NGX consumer release contract is qualified.
 (void)l;(void)p;
}
void ObserveVkNrNgxFgCreate(VkDevice device,const NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* params,NVSDK_NGX_Result result){
 if(result!=NVSDK_NGX_Result_Success||!device||!handle)return;
 uint32_t max=0;if(params)params->Get("DLSSG.MultiFrameCountMax",&max);
 std::lock_guard lock(ngxMutex);if(ngxFeatures.size()>=64)return;
 auto existing=ngxFeatures.find(handle->Id);if(existing!=ngxFeatures.end())VulkanNrFgContributions().RevokeProvider(existing->second.generation);
 ngxFeatures[handle->Id]={device,++ngxNext,max<=63?max:0};
}
bool VkNrNgxFgCeilingObserved(){std::lock_guard lock(ngxMutex);return std::any_of(ngxFeatures.begin(),ngxFeatures.end(),[](const auto& entry){return entry.second.max>0;});}
uint32_t VkNrNgxFgFeatureCount(){std::lock_guard lock(ngxMutex);return static_cast<uint32_t>(ngxFeatures.size());}
void ObserveVkNrNgxFgRelease(uint32_t handleId,NVSDK_NGX_Result result){
 if(result!=NVSDK_NGX_Result_Success)return;std::lock_guard lock(ngxMutex);auto i=ngxFeatures.find(handleId);
 if(i!=ngxFeatures.end()){VulkanNrFgContributions().ProviderReleased(i->second.generation);ngxFeatures.erase(i);}
}
void AuthenticateVkNrNgxFrame(NVSDK_NGX_Parameter* params,VkNrTemporalMetadata& t){
 // A private parameter name establishes neither provider identity nor a shared
 // real-frame contract. Preserve the local evaluation token and any independently
 // authenticated public Streamline identity until a native adapter exists.
 (void)params;(void)t;
}
VkNrNgxFgScope::VkNrNgxFgScope(VkCommandBuffer cb,const NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* params){
 // Private NGX FG input consumption has no qualified release/copy contract.
 // Public Streamline volatile-tag semantics do not apply to this private API.
 // Preserve the game's original parameters before any NR preparation or binding.
 (void)cb;(void)handle;(void)params;
}
void VkNrNgxFgScope::Returned(NVSDK_NGX_Result result){(void)result;}
VkNrNgxFgScope::~VkNrNgxFgScope()=default;
}
