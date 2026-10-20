"""Compile the actual legacy resolver in the diagnostic bypass configuration."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'builds/vulkan-experiment/tests/vulkan-only'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'OptiScaler/hooks/VulkanwDx12_Hooks.cpp').read_text(encoding='utf-8-sig')
start = source.index('PFN_vkVoidFunction Vulkan_wDx12::GetAddress(')
end = source.index('\nvoid Vulkan_wDx12::InitializeStateTrackerFunctionTable', start)
fixture = '''#include <cassert>
using PFN_vkVoidFunction=void(*)();
struct Vulkan_wDx12 { static PFN_vkVoidFunction GetAddress(PFN_vkVoidFunction,const char*); };
void downstream() {}
''' + source[start:end] + '''
int main() {
const char* names[]={"vkCmdBindPipeline","vkCmdBindDescriptorSets","vkCmdPipelineBarrier",
    "vkQueueSubmit","vkQueueSubmit2","vkQueueSubmit2KHR","unknown"};
for(const auto* name:names)assert(Vulkan_wDx12::GetAddress(downstream,name)==nullptr);
assert(Vulkan_wDx12::GetAddress(nullptr,"vkCmdDraw")==nullptr);
assert(Vulkan_wDx12::GetAddress(downstream,nullptr)==nullptr);
}
'''
(out / 'legacy-resolver.cpp').write_text(fixture, encoding='utf-8')
command = r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
cl /nologo /EHsc /O2 /DNR_DIAG_VULKAN_NO_LEGACY=1 legacy-resolver.cpp /Fe:legacy-resolver.exe /Fo:legacy-resolver.obj /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
legacy-resolver.exe
exit /b %errorlevel%
'''
(out / 'legacy-resolver.cmd').write_text(command, encoding='utf-8')
result = subprocess.run(['cmd', '/d', '/c', 'legacy-resolver.cmd'], cwd=out, capture_output=True, text=True)
print(result.stdout + result.stderr)
if result.returncode == 0:
    print('PASS: production legacy resolver supplies no replacement in the diagnostic bypass')
sys.exit(result.returncode)
