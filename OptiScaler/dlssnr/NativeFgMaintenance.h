#pragma once
#include "PreparedVulkanDevice.h"

namespace DlssNr::NativeFg {
inline constexpr const char* MaintenanceExtension="VK_EXT_swapchain_maintenance1";
inline constexpr const char* SurfaceMaintenanceExtension="VK_EXT_surface_maintenance1";
inline bool HasInstanceExtension(const VkInstanceCreateInfo& ci,const char* name){
 for(uint32_t i=0;ci.ppEnabledExtensionNames&&i<ci.enabledExtensionCount;++i)
  if(ci.ppEnabledExtensionNames[i]&&!std::strcmp(ci.ppEnabledExtensionNames[i],name))return true;
 return false;
}
class MaintenanceInstancePreparation {
 std::vector<const char*> names;bool amend=false;
public:
 MaintenanceInstancePreparation(const VkInstanceCreateInfo& ci,const std::vector<std::string>& supported,bool selected){
  if(!selected||!HasInstanceExtension(ci,"VK_KHR_surface")||(ci.enabledExtensionCount&&!ci.ppEnabledExtensionNames))return;
  for(auto name:{"VK_KHR_get_surface_capabilities2",SurfaceMaintenanceExtension})
   if(!HasInstanceExtension(ci,name)&&std::find(supported.begin(),supported.end(),name)==supported.end())return;
  if(ci.enabledExtensionCount)names.assign(ci.ppEnabledExtensionNames,ci.ppEnabledExtensionNames+ci.enabledExtensionCount);
  for(auto name:{"VK_KHR_get_surface_capabilities2",SurfaceMaintenanceExtension})if(!HasInstanceExtension(ci,name))names.push_back(name);
  amend=true;
 }
 VkInstanceCreateInfo Apply(VkInstanceCreateInfo ci)const{if(amend){ci.enabledExtensionCount=static_cast<uint32_t>(names.size());ci.ppEnabledExtensionNames=names.data();}return ci;}
};
struct MaintenanceFacts {bool complete=true,seen=false,enabled=false;};
inline MaintenanceFacts InspectMaintenance(const void* head){
 MaintenanceFacts out;std::array<const void*,64> visited{};size_t count=0;
 for(auto* n=static_cast<const VkBaseInStructure*>(head);n;n=n->pNext){
  if(count==visited.size()||std::find(visited.begin(),visited.begin()+count,n)!=visited.begin()+count){out.complete=false;break;}
  visited[count++]=n;
  if(n->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT){
   if(out.seen){out.complete=false;break;}
   out.seen=true;out.enabled=reinterpret_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*>(n)->swapchainMaintenance1==VK_TRUE;
  }
 }
 return out;
}
inline bool ObserveMaintenance(const VkDeviceCreateInfo& ci,VkResult result){
 const auto facts=InspectMaintenance(ci.pNext);
 return result==VK_SUCCESS&&facts.complete&&facts.seen&&facts.enabled&&PreparedVulkan::Has(ci,MaintenanceExtension);
}
class MaintenancePreparation {
 std::vector<const char*> names;
 VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
 bool amend=false,prepend=false;
public:
 MaintenancePreparation(const VkDeviceCreateInfo& ci,const std::vector<std::string>& supported,bool featureSupported,bool selected){
  const auto facts=InspectMaintenance(ci.pNext);
  if(!selected||!featureSupported||!PreparedVulkan::Has(ci,"VK_KHR_swapchain")||!facts.complete||(facts.seen&&!facts.enabled)||
     (ci.enabledExtensionCount&&!ci.ppEnabledExtensionNames))return;
  if(!PreparedVulkan::Has(ci,MaintenanceExtension)&&std::find(supported.begin(),supported.end(),MaintenanceExtension)==supported.end())return;
  if(ci.enabledExtensionCount)names.assign(ci.ppEnabledExtensionNames,ci.ppEnabledExtensionNames+ci.enabledExtensionCount);
  if(!PreparedVulkan::Has(ci,MaintenanceExtension))names.push_back(MaintenanceExtension);
  amend=true;prepend=!facts.seen;feature.swapchainMaintenance1=VK_TRUE;feature.pNext=const_cast<void*>(ci.pNext);
 }
 MaintenancePreparation(const MaintenancePreparation&)=delete;
 VkDeviceCreateInfo Apply(VkDeviceCreateInfo ci)const{
  if(amend){ci.enabledExtensionCount=static_cast<uint32_t>(names.size());ci.ppEnabledExtensionNames=names.data();}
  if(prepend)ci.pNext=&feature;return ci;
 }
};
}
