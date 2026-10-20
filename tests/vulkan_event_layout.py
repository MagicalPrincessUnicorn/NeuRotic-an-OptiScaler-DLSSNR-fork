"""Exercise the actual event hook bodies and command tracker without a GPU.

Break caught: forwarding a Vulkan event wait but dropping its image transitions.
The generated translation unit uses the repository hook bodies verbatim; only
the driver calls, logging and ambient feature pointer are inert dependencies.
"""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'builds/vulkan-experiment/tests/event-layout'
out.mkdir(parents=True, exist_ok=True)
(out/'pch.h').write_text('''#pragma once
#include <memory>
#include <limits>
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
''')
(out/'State.h').write_text('''#pragma once
struct FixtureFeature { bool IsWithDx12() const { return false; } };
struct State { FixtureFeature* currentFeature=nullptr; static State& Instance(){static State s;return s;} };
''')
source = (root/'OptiScaler/hooks/VulkanwDx12_Hooks.cpp').read_text()
names = ['vkCmdWaitEvents', 'vkCmdWaitEvents2', 'vkCmdWaitEvents2KHR']
definitions=[]
for name in names:
    start=source.index('void Vulkan_wDx12::hk_'+name+'(')
    end=source.index('\n}', start)+2
    definitions.append(source[start:end])
declarations=[d[:d.index('\n{')].replace('void Vulkan_wDx12::', 'static void ')+';' for d in definitions]
unit='''#include <hooks/CommandBuffer_StateTracker.h>
#include <cassert>
#include <iostream>
VkCommandBuffer lastCmdBuffer=VK_NULL_HANDLE,virtualCmdBuffer=VK_NULL_HANDLE;
struct Vulkan_wDx12 { static inline vk_state::CommandBufferStateTracker cmdBufferStateTracker;
'''+ '\n'.join(declarations)+'''\n};
int forwarded=0;
void VKAPI_CALL legacy(VkCommandBuffer,uint32_t,const VkEvent*,VkPipelineStageFlags,VkPipelineStageFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){++forwarded;}
void VKAPI_CALL sync2(VkCommandBuffer,uint32_t,const VkEvent*,const VkDependencyInfo*){++forwarded;}
PFN_vkCmdWaitEvents o_vkCmdWaitEvents=legacy;
PFN_vkCmdWaitEvents2 o_vkCmdWaitEvents2=sync2;
PFN_vkCmdWaitEvents2KHR o_vkCmdWaitEvents2KHR=sync2;
'''+ '\n'.join(definitions)+'''
int main(){
 auto& tracker=Vulkan_wDx12::cmdBufferStateTracker;tracker.EnableNativeObservations();
 vk_state::VulkanCmdFns fns{};tracker.SetFunctionTable(fns);
 auto cb=reinterpret_cast<VkCommandBuffer>(1);auto pool=reinterpret_cast<VkCommandPool>(2);
 tracker.OnAllocateCommandBuffers(reinterpret_cast<VkDevice>(1),pool,1,&cb,0);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=reinterpret_cast<VkImage>(3);
 b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
 VkImageMemoryBarrier2 b2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};b2.image=b.image;b2.newLayout=b.newLayout;b2.subresourceRange=b.subresourceRange;
 VkDependencyInfo deps[2]{};deps[0].sType=deps[1].sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
 deps[1].imageMemoryBarrierCount=1;deps[1].pImageMemoryBarriers=&b2;
 for(int mode=0;mode<3;++mode){
  tracker.OnBegin(cb,&begin);
  if(mode==0)Vulkan_wDx12::hk_vkCmdWaitEvents(cb,0,nullptr,0,0,0,nullptr,0,nullptr,1,&b);
  else if(mode==1)Vulkan_wDx12::hk_vkCmdWaitEvents2(cb,2,nullptr,deps);
  else Vulkan_wDx12::hk_vkCmdWaitEvents2KHR(cb,2,nullptr,deps);
  vk_state::CommandBufferState saved;assert(tracker.CaptureNrState(cb,saved));
  if(!saved.ImageLayouts.contains(b.image)){std::cerr<<"FAIL event transition absent, mode "<<mode<<"\\n";return 1;}
  assert(saved.ImageLayouts.at(b.image)==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  assert(saved.ImageLayoutRanges.at(b.image).aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
  tracker.OnReset(cb);assert(!tracker.CaptureNrState(cb,saved));
  tracker.OnBegin(cb,&begin);assert(tracker.CaptureNrState(cb,saved));assert(saved.ImageLayouts.empty());
 }
 assert(forwarded==3);
 b2.oldLayout=b2.newLayout=VK_IMAGE_LAYOUT_GENERAL;
 Vulkan_wDx12::hk_vkCmdWaitEvents2(cb,2,nullptr,deps);
 vk_state::CommandBufferState saved;assert(tracker.CaptureNrState(cb,saved));
 if(saved.ImageLayouts.contains(b.image)){std::cerr<<"FAIL sync2 memory dependency invented an image layout\\n";return 1;}
 b2.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b2.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 Vulkan_wDx12::hk_vkCmdWaitEvents2(cb,2,nullptr,deps);
 b2.oldLayout=b2.newLayout=VK_IMAGE_LAYOUT_GENERAL;
 Vulkan_wDx12::hk_vkCmdWaitEvents2(cb,2,nullptr,deps);
 assert(tracker.CaptureNrState(cb,saved));assert(saved.ImageLayouts.at(b.image)==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 std::cout<<"PASS legacy/core/KHR event transitions, ranges, reset, forwarding and ignored sync2 layout values\\n";
}
'''
(out/'event-layout.cpp').write_text(unit)
command=f'''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "{root}"
cl /nologo /std:c++20 /EHsc /W3 /I "{out}" /I OptiScaler /I external/vulkan/include /I external/nvngx_dlss_sdk "{out/'event-layout.cpp'}" /Fe:"{out/'event-layout.exe'}" /Fo:"{out/'event-layout.obj'}"
if errorlevel 1 exit /b 1
"{out/'event-layout.exe'}"
'''
(out/'run.cmd').write_text(command)
raise SystemExit(subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=root).returncode)
