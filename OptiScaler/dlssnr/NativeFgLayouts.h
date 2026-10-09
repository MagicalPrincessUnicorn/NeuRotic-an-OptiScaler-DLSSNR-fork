#pragma once
#include <vulkan/vulkan.h>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <span>
#include <type_traits>
#include <atomic>
namespace DlssNr::NativeFg::Layouts {
inline std::recursive_mutex mutex;
inline std::map<VkSwapchainKHR,std::vector<VkImage>> swapImages;
inline std::set<VkImage> images;
inline std::atomic<bool> active{false};
inline VkDevice activeDevice{};
inline std::map<std::pair<VkDevice,VkImageView>,VkImage> views;
inline std::map<std::pair<VkDevice,VkRenderPass>,VkRenderPass> passes;
inline std::map<std::pair<VkDevice,VkFramebuffer>,bool> frames;
inline void Register(VkDevice device,VkSwapchainKHR swap,std::span<const VkImage> value){std::lock_guard lock(mutex);activeDevice=device;swapImages[swap]={value.begin(),value.end()};images.insert(value.begin(),value.end());active.store(!images.empty(),std::memory_order_relaxed);}
inline void Forget(VkSwapchainKHR swap){std::lock_guard lock(mutex);auto i=swapImages.find(swap);if(i!=swapImages.end()){for(auto image:i->second)images.erase(image);swapImages.erase(i);active.store(!images.empty(),std::memory_order_relaxed);}}
inline VkImageLayout Translate(VkDevice device,VkImage image,VkImageLayout layout){if(layout!=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR||!active.load(std::memory_order_relaxed))return layout;std::lock_guard lock(mutex);return device==activeDevice&&images.contains(image)?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:layout;}
inline bool VirtualViews(VkDevice device,unsigned count,const VkImageView* values){if(!values||device!=activeDevice)return false;for(unsigned i=0;i<count;++i){auto it=views.find({device,values[i]});if(it!=views.end()&&images.contains(it->second))return true;}return false;}
inline void View(VkDevice device,VkImageView view,VkImage image){std::lock_guard lock(mutex);views[{device,view}]=image;}
inline void ForgetView(VkDevice device,VkImageView view){std::lock_guard lock(mutex);views.erase({device,view});}
inline void Framebuffer(VkDevice device,VkFramebuffer frame,const VkFramebufferCreateInfo& ci){std::lock_guard lock(mutex);frames[{device,frame}]=VirtualViews(device,ci.attachmentCount,ci.pAttachments);}
inline void ForgetDevice(VkDevice device){std::lock_guard lock(mutex);std::erase_if(views,[&](const auto& e){return e.first.first==device;});std::erase_if(passes,[&](const auto& e){return e.first.first==device;});std::erase_if(frames,[&](const auto& e){return e.first.first==device;});}
inline void ForgetFrame(VkDevice device,VkFramebuffer frame){std::lock_guard lock(mutex);frames.erase({device,frame});}
// Only attachment initial/final layout changes; render-pass compatibility is preserved.
template<class Create,class CI>VkResult CreatePass(Create create,PFN_vkDestroyRenderPass destroy,VkDevice device,const CI* info,const VkAllocationCallbacks* alloc,VkRenderPass* out,bool selected){
 auto result=create(device,info,alloc,out);if(result!=VK_SUCCESS||!selected||!info||!out||!info->pAttachments)return result;
 using Attachment=std::remove_cv_t<std::remove_reference_t<decltype(info->pAttachments[0])>>;
 std::vector<Attachment> attachments(info->pAttachments,info->pAttachments+info->attachmentCount);bool changed=false;
 for(auto& a:attachments){for(auto* l:{&a.initialLayout,&a.finalLayout})if(*l==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR){*l=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;changed=true;}}
 if(!changed)return result;auto local=*info;local.pAttachments=attachments.data();VkRenderPass alternate{};
 result=create(device,&local,alloc,&alternate);if(result!=VK_SUCCESS){destroy(device,*out,alloc);*out=VK_NULL_HANDLE;return result;}
 std::lock_guard lock(mutex);passes[{device,*out}]=alternate;return result;
}
inline void DestroyPass(PFN_vkDestroyRenderPass destroy,VkDevice device,VkRenderPass pass,const VkAllocationCallbacks* alloc){VkRenderPass alternate{};{std::lock_guard lock(mutex);auto it=passes.find({device,pass});if(it!=passes.end()){alternate=it->second;passes.erase(it);}}if(alternate)destroy(device,alternate,alloc);destroy(device,pass,alloc);}
inline VkRenderPassBeginInfo Begin(VkDevice device,const VkRenderPassBeginInfo& info){auto local=info;if(!active.load(std::memory_order_relaxed))return local;std::lock_guard lock(mutex);bool owned=frames.contains({device,info.framebuffer})&&frames.at({device,info.framebuffer});
 unsigned links=0;for(auto* p=static_cast<const VkBaseInStructure*>(info.pNext);p&&links++<32;p=p->pNext)if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO){auto* a=reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(p);owned|=VirtualViews(device,a->attachmentCount,a->pAttachments);}
 if(owned){auto it=passes.find({device,info.renderPass});if(it!=passes.end())local.renderPass=it->second;}return local;}
template<class B>bool Fix(VkDevice device,B& barrier){const auto old=Translate(device,barrier.image,barrier.oldLayout),next=Translate(device,barrier.image,barrier.newLayout);bool changed=false;
 if(old!=barrier.oldLayout){barrier.oldLayout=old;barrier.srcAccessMask|=VK_ACCESS_SHADER_READ_BIT;changed=true;}
 if(next!=barrier.newLayout){barrier.newLayout=next;barrier.dstAccessMask|=VK_ACCESS_SHADER_READ_BIT;changed=true;}
 return changed;}
inline void Barrier(PFN_vkCmdPipelineBarrier fn,VkDevice device,VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,unsigned mc,const VkMemoryBarrier* m,unsigned bc,const VkBufferMemoryBarrier* b,unsigned ic,const VkImageMemoryBarrier* i){
 if(!active.load(std::memory_order_relaxed)){fn(cb,src,dst,flags,mc,m,bc,b,ic,i);return;}
 std::vector<VkImageMemoryBarrier> copy;if(ic&&i)copy.assign(i,i+ic);bool changed=false;for(auto& x:copy)changed|=Fix(device,x);
 fn(cb,changed?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:src,changed?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:dst,flags,mc,m,bc,b,ic,changed?copy.data():i);
}
struct Dependency {VkDependencyInfo info{};std::vector<VkImageMemoryBarrier2> images;
 explicit Dependency(VkDevice device,const VkDependencyInfo& original):info(original){if(info.imageMemoryBarrierCount&&info.pImageMemoryBarriers){images.assign(info.pImageMemoryBarriers,info.pImageMemoryBarriers+info.imageMemoryBarrierCount);for(auto& b:images)if(Fix(device,b)){b.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;b.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;}info.pImageMemoryBarriers=images.data();}}
};
inline void Barrier(PFN_vkCmdPipelineBarrier2 fn,VkDevice device,VkCommandBuffer cb,const VkDependencyInfo* info){if(!info||!active.load(std::memory_order_relaxed)){fn(cb,info);return;}Dependency copy(device,*info);fn(cb,&copy.info);}
inline void Wait(PFN_vkCmdWaitEvents fn,VkDevice device,VkCommandBuffer cb,unsigned ec,const VkEvent* events,VkPipelineStageFlags src,VkPipelineStageFlags dst,unsigned mc,const VkMemoryBarrier* m,unsigned bc,const VkBufferMemoryBarrier* b,unsigned ic,const VkImageMemoryBarrier* i){
 if(!active.load(std::memory_order_relaxed)){fn(cb,ec,events,src,dst,mc,m,bc,b,ic,i);return;}
 std::vector<VkImageMemoryBarrier> copy;if(ic&&i)copy.assign(i,i+ic);bool changed=false;for(unsigned n=0;n<copy.size();++n){changed|=Fix(device,copy[n]);copy[n].srcAccessMask=i[n].srcAccessMask;}
 fn(cb,ec,events,src,changed?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:dst,mc,m,bc,b,ic,changed?copy.data():i);
}
inline void Wait(PFN_vkCmdWaitEvents2 fn,VkDevice device,VkCommandBuffer cb,unsigned count,const VkEvent* events,const VkDependencyInfo* info){if(!info||!active.load(std::memory_order_relaxed)){fn(cb,count,events,info);return;}
 std::vector<Dependency> copies;copies.reserve(count);for(unsigned i=0;i<count;++i){copies.emplace_back(device,info[i]);for(unsigned b=0;b<copies.back().images.size();++b){copies.back().images[b].srcStageMask=info[i].pImageMemoryBarriers[b].srcStageMask;copies.back().images[b].srcAccessMask=info[i].pImageMemoryBarriers[b].srcAccessMask;}}std::vector<VkDependencyInfo> descriptions;for(const auto& c:copies)descriptions.push_back(c.info);fn(cb,count,events,descriptions.data());}
}
