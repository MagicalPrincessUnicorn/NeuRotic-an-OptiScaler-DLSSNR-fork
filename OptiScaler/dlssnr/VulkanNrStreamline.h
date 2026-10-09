#pragma once
#include "VulkanNrPreFg.h"
#include <sl_dlss_g.h>
#include <functional>
#include <unordered_map>
namespace DlssNr {
struct VkNrTaggedColor {VkImage image=VK_NULL_HANDLE;VkFormat format=VK_FORMAT_UNDEFINED;VkExtent2D extent{};};
struct VkNrTagDecision {
 bool accepted=false;std::optional<VkNrConsumerId> consumer;std::string reason;
 std::vector<sl::ResourceTag> tags;std::vector<sl::Resource> resources;
};
class VulkanNrStreamline {
 public:
 using Lookup=std::function<std::optional<VkNrRecordedOutput>(VkCommandBuffer)>;
 VulkanNrStreamline(VkNrRecordingOwner& r,VulkanNrPreFg& f,Lookup lookup={}):recordings_(r),fg_(f),lookup_(std::move(lookup)){}
 using Prepare=std::function<std::optional<VkNrRecordedOutput>(VkCommandBuffer,uint64_t,uint64_t,uint32_t,VkNrTaggedColor)>;
 void PrepareOutput(Prepare);
 void LookupOutput(Lookup);void Provider(uint64_t,bool);uint64_t Generation() const;
 void FeatureLoaded(bool loaded,bool succeeded);
 void Options(uint32_t,const sl::DLSSGOptions&,bool);void State(uint32_t,const sl::DLSSGState&,bool);
 VkNrFgActivity Activity() const;std::string Reason(uint32_t viewport) const;
 VkNrTagDecision BeforeTags(const sl::FrameToken*,const sl::ViewportHandle&,std::span<const sl::ResourceTag>,sl::CommandBuffer*);
 void TagsReturned(const VkNrTagDecision&,sl::Result);
 private:
 struct View {VkNrFgActivity activity=VkNrFgActivity::Unknown;uint32_t requested=0,max=0;bool options=false,state=false,stateObserved=false,compatible=false;};
 VkNrRecordingOwner& recordings_;VulkanNrPreFg& fg_;Lookup lookup_;Prepare prepare_;mutable std::mutex mutex_;
 uint64_t provider_=0;bool qualified_=false,viewsOverflow_=false;std::optional<bool> loaded_;std::unordered_map<uint32_t,View> views_;
};
struct VkNrPublicFrame {uint64_t provider=0,frame=0;uint32_t viewport=UINT32_MAX;VkCommandBuffer cmd=VK_NULL_HANDLE;};
class VkNrStreamlineSourceScope {
 public:
 VkNrStreamlineSourceScope(std::optional<VkNrPublicFrame>);~VkNrStreamlineSourceScope();
 void Complete(bool);static void Authenticate(VkCommandBuffer,VkNrTemporalMetadata&);
 static bool SourceSucceeded(VkNrUseId);
 static bool DerivedSucceeded(VkNrUseId child,VkNrUseId parent);
 static void Candidate(VkNrUseId);
 private:std::optional<VkNrPublicFrame> saved_;std::vector<VkNrUseId> savedUses_;
};
VulkanNrStreamline& VulkanNrStreamlineAdapter();
class VkNrNgxFgScope {
 public:
 VkNrNgxFgScope(VkCommandBuffer,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*);
 ~VkNrNgxFgScope();void Returned(NVSDK_NGX_Result);
};
void ObserveVkNrNgxFgCreate(VkDevice,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*,NVSDK_NGX_Result);
uint32_t VkNrNgxFgFeatureCount();
bool VkNrNgxFgCeilingObserved();
void ObserveVkNrNgxFgRelease(uint32_t handleId,NVSDK_NGX_Result);
void AuthenticateVkNrNgxFrame(NVSDK_NGX_Parameter*,VkNrTemporalMetadata&);
void SetVkNrNgxOutputLookup(VulkanNrStreamline::Lookup,VulkanNrStreamline::Prepare prepare={});
}
