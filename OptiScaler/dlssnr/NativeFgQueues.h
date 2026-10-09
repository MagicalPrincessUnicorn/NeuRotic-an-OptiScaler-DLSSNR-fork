#pragma once
#include <vulkan/vulkan.h>
#include <vector>
namespace DlssNr::NativeFg {
// Extra queues are requested at device creation, never borrowed from the game.
class QueuePreparation {
 std::vector<VkDeviceQueueCreateInfo> infos;std::vector<float> priorities;
public:
 unsigned family=UINT32_MAX,first=0;
 QueuePreparation(const VkDeviceCreateInfo& ci,const std::vector<VkQueueFamilyProperties>& families,bool selected){
  if(!selected||!ci.pQueueCreateInfos)return;
  for(unsigned i=0;i<ci.queueCreateInfoCount;++i){const auto& q=ci.pQueueCreateInfos[i];
   if(q.flags||q.pNext||!q.queueCount||!q.pQueuePriorities||q.queueFamilyIndex>=families.size())continue;
   const auto& f=families[q.queueFamilyIndex];
   if((f.queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))!=(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)||f.queueCount<q.queueCount||f.queueCount-q.queueCount<3)continue;
   unsigned matching=0;for(unsigned j=0;j<ci.queueCreateInfoCount;++j)matching+=ci.pQueueCreateInfos[j].queueFamilyIndex==q.queueFamilyIndex;
   if(matching!=1)continue;
   family=q.queueFamilyIndex;first=q.queueCount;infos.assign(ci.pQueueCreateInfos,ci.pQueueCreateInfos+ci.queueCreateInfoCount);
   priorities.assign(q.pQueuePriorities,q.pQueuePriorities+q.queueCount);priorities.insert(priorities.end(),3,1.f);
   infos[i].queueCount+=3;infos[i].pQueuePriorities=priorities.data();break;
  }
 }
 VkDeviceCreateInfo Apply(VkDeviceCreateInfo ci)const{if(!infos.empty())ci.pQueueCreateInfos=infos.data();return ci;}
};
}
