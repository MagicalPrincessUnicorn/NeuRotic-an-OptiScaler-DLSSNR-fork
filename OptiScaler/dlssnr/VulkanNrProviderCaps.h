#pragma once
#include <cstdint>
#include <string>
namespace DlssNr {
enum class VkNrProviderKind {NativeNgx,Streamline,Ffx};
struct VkNrProviderObservation {
 VkNrProviderKind kind=VkNrProviderKind::NativeNgx;std::string providerIdentity;uint32_t apiVersion=0,maxGenerated=0;
 bool vulkanApi=false,exportsQualified=false,stateSucceeded=false,consumerContractObserved=false,dynamicReported=false,adaProviderQualified=false;
 // The committed Ada publication profile describes D3D12 objects. A Vulkan profile must be separately authenticated.
 bool adaVulkanProfile=false;
};
struct VkNrProviderQualification {
 std::string providerIdentity;uint32_t apiVersion=0,maxGenerated=0;bool vulkanFg=false,fixedMfg=false,dynamicMfg=false,adaQualified=false;std::string reason;
};
inline VkNrProviderQualification QualifyVkNrProvider(const VkNrProviderObservation& o){
 VkNrProviderQualification q;q.providerIdentity=o.providerIdentity;q.apiVersion=o.apiVersion;
 if(!o.vulkanApi||!o.exportsQualified||o.providerIdentity.empty()){q.reason="Vulkan provider module, exports or API identity is unqualified";return q;}
 if(o.kind==VkNrProviderKind::Ffx&&o.apiVersion>=0x020300){q.reason="AMD SDK 2.3 does not support Vulkan";return q;}
 if(!o.apiVersion||(o.kind==VkNrProviderKind::Ffx&&o.apiVersion!=0x010104)){q.reason="Vulkan provider descriptor ABI is unqualified";return q;}
 if(!o.stateSucceeded||!o.maxGenerated||o.maxGenerated>63){q.reason="Vulkan provider has no successful bounded frame-limit observation";return q;}
 if(!o.consumerContractObserved){q.reason="Vulkan provider final-color frame mapping or consumer release contract is unobserved";return q;}
 q.maxGenerated=o.maxGenerated;q.vulkanFg=true;q.fixedMfg=o.maxGenerated>1;
 q.adaQualified=o.adaProviderQualified&&o.adaVulkanProfile;
 q.reason=o.adaProviderQualified&&!o.adaVulkanProfile?"D3D12 Ada publication has no qualified Vulkan device/profile":o.dynamicReported?"Dynamic MFG is unavailable on Vulkan":std::string{};
 return q;
}
inline bool VkNrProviderRatio(const VkNrProviderQualification& q,uint32_t generated){return q.vulkanFg&&generated&&generated<=q.maxGenerated;}
}
