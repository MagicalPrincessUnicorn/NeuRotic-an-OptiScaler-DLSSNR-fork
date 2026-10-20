"""Run the real BeforeSR admission block with inert resource facts, without a GPU."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
out = root / 'builds/vulkan-experiment/tests/presr-boundary'
out.mkdir(parents=True, exist_ok=True)
source = (root/'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp').read_text()
if '--baseline' in sys.argv:
    source = subprocess.check_output(['git','show','9eb5b764:OptiScaler/dlssnr/DlssNrFeature_Vk.cpp'],cwd=root,text=True)
start = source.index('#ifndef LOW_PRECISION_TRACKING', source.index('VkRecordResult EvaluateBeforeUpscaleVk('))
end = source.index('#else', start)
body = source[start:end].replace('#ifndef LOW_PRECISION_TRACKING', '')
if '--raw-wrapper-range' in sys.argv:
    # Negative control: the old caller ignored the canonical image-rights range.
    body = body.replace('rights->view.range', 'input->Resource.ImageViewInfo.SubresourceRange')
header = root/'OptiScaler/dlssnr/VulkanNrInputLayout.h'
handoff_start=source.index('        if(delivered&&before&&root.generations->AppliedKey()->privateDlaa)')
handoff_end='} else if(delivered&&before)Transition(base.commandBuffer,root.resources->preSr,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);'
handoff=source[handoff_start:source.index(handoff_end,handoff_start)+len(handoff_end)]
capture=(root/'OptiScaler/dlssnr/VulkanNrPerformanceCapture.inl').read_text()
capture_start=capture.index('    auto& carrier=')
capture=capture[capture_start:capture.index('    const auto evaluate=',capture_start)]
handoff_test='''
struct Image { int view=0; VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL; };
void Transition(VkCommandBuffer,Image& image,VkImageLayout layout){image.layout=layout;}
struct Resources { Image preSr,reJitter; };
struct Key { bool privateDlaa=false; };
struct Generation { Key key; Key* AppliedKey(){return &key;} };
struct Pass { template<class... T> bool Dispatch(T...){return true;} };
struct Constants { int Mode=0;float MvScaleX=0,MvScaleY=0; };
struct Temporal { float jitterX=0,jitterY=0; };
void TestHandoff(){
 Resources resources;Generation generations;Pass pass;Temporal temporal;
 struct { Resources* resources;Generation* generations;Pass* pass; } root{&resources,&generations,&pass};
 struct { VkCommandBuffer commandBuffer{};int use=1;uint32_t width=8,height=8;
  struct { Temporal* temporal; } contract; } base; base.contract.temporal=&temporal;
 Constants display;struct Reservation { std::vector<int> slots{1}; } slots;
 auto* reservation=&slots;
 for(bool dlaa:{false,true})for(bool before:{false,true})for(bool success:{false,true}){
  generations.key.privateDlaa=dlaa;bool delivered=success;
  resources.preSr.layout=resources.reJitter.layout=VK_IMAGE_LAYOUT_GENERAL;
''' + handoff + '''
  if(before&&success) Check((dlaa?resources.reJitter:resources.preSr).layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"delivered private SR input must be shader read-only");
  else Check(resources.preSr.layout==VK_IMAGE_LAYOUT_GENERAL&&resources.reJitter.layout==VK_IMAGE_LAYOUT_GENERAL,"non-delivery must not transition a carrier for SR");
 }
 cout<<"PASS actual final handoff for normal and private-DLAA paths\\n";
}
VkImageLayout borrowedLayout;
void vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
 uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* barriers){
 for(uint32_t i=0;i<count;++i){Check(barriers[i].oldLayout==borrowedLayout,"capture restores the actual borrowed layout");borrowedLayout=barriers[i].newLayout;}
}
void TestCapture(){
 for(bool dlaa:{false,true})for(bool fail:{false,true}){
  Resources resources;resources.preSr.layout=resources.reJitter.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  Generation generations;generations.key.privateDlaa=dlaa;
  struct { Resources* resources;Generation* generations; } root{&resources,&generations};
  struct { Image before,after; } pair;
  VkCommandBuffer cb{};struct { VkImage Image; VkImageSubresourceRange SubresourceRange; } input{};
  input.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  std::unordered_map<VkImage,VkImageLayout> layouts{{input.Image,VK_IMAGE_LAYOUT_GENERAL}};
  auto originalLayout=layouts.find(input.Image);borrowedLayout=originalLayout->second;
  try {
''' + capture + '''
   Check(carrier.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&borrowedLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"both private NGX evaluation inputs are sampled-read");
   if(fail)throw 1;
  }catch(int){}
  Check(borrowedLayout==VK_IMAGE_LAYOUT_GENERAL,"capture success/failure restores game image layout");
  Check((dlaa?resources.reJitter:resources.preSr).layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"capture preserves the live SR carrier layout");
 }
 cout<<"PASS actual capture layout scope on success and exception\\n";
}
'''
unit = '''#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <unordered_map>
#include <string>
#include <iostream>
#include <optional>
#include <vector>
#include <functional>
#include <atomic>
''' + ('#include <dlssnr/VulkanNrInputLayout.h>\n' if header.exists() else '') + '''
using namespace std;
using ULONGLONG=unsigned long long;
ULONGLONG GetTickCount64(){return 3000;}
#define LOG_INFO(...) ((void)0)
namespace DlssNr {
enum class VkRecordStatus { NoWork, Complete };
struct VkRecordResult { VkRecordStatus status; string reason; };
struct VkNrRect {uint32_t x=0,y=0,width=0,height=0;};
struct Request { VkDevice device{}; bool ngxSrInputContract=false; VkImageLayout colorLayout{};
 struct {struct Temporal {VkNrRect color;};std::optional<Temporal> temporal;} contract; };
struct Saved { bool InRenderPass=false; unordered_map<VkImage,VkImageLayout> ImageLayouts;
 unordered_map<VkImage,VkImageSubresourceRange> ImageLayoutRanges; };
bool hasRights=true;
struct Rights {struct {VkImageSubresourceRange range;} view;};
struct Facts {
 std::optional<DlssNr::Rights> Rights(VkDevice,const NVSDK_NGX_Resource_VK& input,VkImageUsageFlags,bool,const char** reason){
  *reason="missing sampled rights";if(!hasRights)return {};
  return DlssNr::Rights{{{input.Resource.ImageViewInfo.SubresourceRange.aspectMask,0,1,0,1}}};
 }
 string Describe(VkDevice,const NVSDK_NGX_Resource_VK&){return "fixture creation facts";}
};
Facts& VulkanNrImageFacts(){static Facts f;return f;}
VkRecordResult Admit(Request request, Request& qualified, Saved& saved, NVSDK_NGX_Resource_VK* input) {
''' + body + '''
 return {VkRecordStatus::Complete,""};
}
}
using namespace DlssNr;
void Check(bool ok,const char* why){if(!ok){cerr<<"FAIL: "<<why<<"\\n";exit(1);}}
''' + handoff_test + '''
int main(){
 TestHandoff();
 TestCapture();
''' + ('return 0;\n' if '--handoff-only' in sys.argv else '') + '''
 Request request,qualified; request.ngxSrInputContract=true;
 Saved saved; NVSDK_NGX_Resource_VK input{};input.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
 auto image=reinterpret_cast<VkImage>(1);input.Resource.ImageViewInfo.Image=image;
 input.Resource.ImageViewInfo.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 auto result=Admit(request,qualified,saved,&input);
 Check(result.status==VkRecordStatus::Complete,"authentic NGX SR input needs no barrier in this command buffer");
 Check(qualified.colorLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"NGX input uses the sampled-read contract");
 Check(saved.ImageLayouts.empty(),"contract inference does not invent a tracker observation");
 input.Resource.ImageViewInfo.SubresourceRange.levelCount=VK_REMAINING_MIP_LEVELS;
 input.Resource.ImageViewInfo.SubresourceRange.layerCount=VK_REMAINING_ARRAY_LAYERS;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::Complete,
  "actual Native admission uses canonical image rights for equivalent remaining-count wrapper ranges");
 input.Resource.ImageViewInfo.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 request.ngxSrInputContract=false;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"unqualified calls cannot assume NGX layout");
 request.ngxSrInputContract=true;hasRights=false;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"layout contract grants no sampled rights");
 hasRights=true;saved.InRenderPass=true;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"no compute injection inside a render pass");
 saved.InRenderPass=false;saved.ImageLayouts[image]=VK_IMAGE_LAYOUT_GENERAL;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"incomplete observed evidence cannot silently fall back");
 saved.ImageLayoutRanges[image]=input.Resource.ImageViewInfo.SubresourceRange;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::Complete&&qualified.colorLayout==VK_IMAGE_LAYOUT_GENERAL,"observed readable layout wins");
 saved.ImageLayouts[image]=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"observed unreadable layout is not overridden");
 saved.ImageLayouts[image]=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;saved.ImageLayoutRanges[image].baseMipLevel=1;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"uncovered color mip is refused");
 saved.ImageLayouts.clear();saved.ImageLayoutRanges.clear();input.Resource.ImageViewInfo.SubresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
 Check(Admit(request,qualified,saved,&input).status==VkRecordStatus::NoWork,"NGX contract is confined to color input");
 cout<<"PASS actual BeforeSR admission: NGX contract, observed layout precedence and refusal cases\\n";
}
'''
(out/'boundary.cpp').write_text(unit)
command = f'''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "{root}"
cl /nologo /std:c++20 /EHsc /W3 /I OptiScaler /I external/vulkan/include /I external/nvngx_dlss_sdk "{out/'boundary.cpp'}" /Fe:"{out/'boundary.exe'}" /Fo:"{out/'boundary.obj'}"
if errorlevel 1 exit /b 1
"{out/'boundary.exe'}"
'''
(out/'run.cmd').write_text(command)
raise SystemExit(subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=root).returncode)
