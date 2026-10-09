#pragma once
#include <string>
namespace DlssNr {
struct VkNrCapabilityCell {bool available=false,pending=false;std::string reason;};
struct VkNrCapabilityFacts {bool enabled=true,present=false,beforeSr=false,rayReconstruction=false,apply=true,debug=false,compare=false,guides=false,shaderObjects=false,descriptorBuffer=false;};
struct VkNrCapabilitySnapshot {
 VkNrCapabilityCell resolution,multipass,hdr,enhanced,hold,capture,performanceCapture,streamlineFg,nativeFg,adaMfg,ffxFg,coexistence;
};
inline VkNrCapabilitySnapshot BuildVkNrCapabilities(const VkNrCapabilityFacts& f){
 VkNrCapabilitySnapshot s;s.resolution=s.multipass=s.hdr={true,false,{}};
 s.enhanced={true,!f.guides,f.guides?"":"Enhanced awaits fresh matching temporal guides"};
 s.hold={!(f.beforeSr&&!f.rayReconstruction),false,""};if(!s.hold.available)s.hold.reason="Vulkan Performance Hold requires a completed post-SR analysis frame";
 s.performanceCapture={true,true,"Performance capture awaits selected native DLSS creation settings and temporary-feature GPU completion"};
 s.capture={f.apply&&!f.debug&&!f.compare,false,{}};
 if(!s.capture.available)s.capture.reason="Vulkan captures need Apply Model on, Debug view and Compare off";
 s.streamlineFg={false,true,"Vulkan FG needs matching public frame/viewport, fixed provider state and volatile HUDless copy"};
 s.nativeFg={false,false,"Native Vulkan FG adapter needs an authenticated frame and documented input release contract"};
 s.adaMfg={false,false,"Ada publication has no qualified Vulkan device/profile"};
 s.ffxFg={false,true,"Vulkan FFX final-color frame identity and consumer release are unqualified"};
 s.coexistence={!f.shaderObjects&&!f.descriptorBuffer,false,{}};
 if(!s.coexistence.available)s.coexistence.reason="Vulkan shader-object or descriptor-buffer bindings have no restoration adapter";
 return s;
}
}
