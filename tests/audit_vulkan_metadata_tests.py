from pathlib import Path
s=Path('OptiScaler/dlssnr/NativeVulkanGuides.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a);n=1;i=b+1
 while n:
  n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
a=s.index('struct View {');b=s.index('struct Scope {',a)
c=s.index('std::map<std::pair<VkDevice,VkImageView>');d=s.index('std::unordered_map<VkCommandBuffer,Scope>',c)
source='''#include <vulkan/vulkan.h>
#include <map>
#include <mutex>
#include <vector>
#include <optional>
#include <cassert>
#include <cstdio>
#include <cstdint>
bool Enabled(){return true;} void Reject(const char*){}
unsigned viewObservations=0,passObservations=0,framebufferObservations=0;
std::mutex mutex;
struct Fact {uint64_t generation=17;}; struct Facts {std::optional<Fact> Image(VkDevice,VkImage){return Fact{};}};
Facts& VulkanNrImageFacts(){static Facts facts;return facts;}
'''+s[a:b]+s[c:d]
for sig in ['void ViewCreated(','void ViewDestroyed(','void RenderPassCreated(','void RenderPassCreated2(','void RenderPassDestroyed(','void FramebufferCreated(','void FramebufferDestroyed(']:source+=fn(sig)+'\n'
source+='''int main(){
 const auto a=reinterpret_cast<VkDevice>(0x10),b=reinterpret_cast<VkDevice>(0x20);
 const auto view=reinterpret_cast<VkImageView>(0x30);const auto pass=reinterpret_cast<VkRenderPass>(0x40);const auto fb=reinterpret_cast<VkFramebuffer>(0x50);
 VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
 vi.format=VK_FORMAT_D32_SFLOAT;ViewCreated(a,vi,view);vi.format=VK_FORMAT_D16_UNORM;ViewCreated(b,vi,view);
 assert(views.size()==2&&views.at({a,view}).info.format==VK_FORMAT_D32_SFLOAT);
 ViewDestroyed(b,view);assert(views.size()==1&&views.contains({a,view}));
 VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};RenderPassCreated(a,rp,pass);
 VkRenderPassCreateInfo2 rp2{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};RenderPassCreated2(b,rp2,pass);
 assert(passes.size()==2);RenderPassDestroyed(b,pass);assert(passes.size()==1&&passes.contains({a,pass}));
 VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.width=17;FramebufferCreated(a,fi,fb);fi.width=23;FramebufferCreated(b,fi,fb);
 assert(framebuffers.size()==2&&framebuffers.at({a,fb}).width==17);FramebufferDestroyed(b,fb);assert(framebuffers.size()==1&&framebuffers.contains({a,fb}));
 puts("PASS production Vulkan guide view/pass/framebuffer maps retain equal-valued live peer handles");
}
'''
Path('builds/audit-api/guide-metadata.cpp').write_text(source)
