"""Exercise production resolver bypasses and preprocess actual hook installation."""
from pathlib import Path
import re, subprocess, sys

no_submit = '--no-submit' in sys.argv
command_only = '--command-only' in sys.argv or no_submit
root = Path(__file__).resolve().parents[1]
out = root / 'builds/vulkan-experiment/tests/vulkan-only'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'OptiScaler/hooks/Vulkan_Hooks.cpp').read_text(encoding='utf-8-sig')
def function(start, end):
    i = source.index(start)
    return source[i:source.index(end, i)]
record = function('static PFN_vkVoidFunction NrRecordingAddress(', '\ntemplate<class PFN')
work = function('static PFN_vkVoidFunction NrWorkAddress(', '// Those aren\'t hooked')
submit = function('static void HookDeviceSubmissions(', '// Native loader trampolines')
hook = function('void VulkanHooks::Hook(', '\nvoid VulkanHooks::Unhook(')
macro = function('#define NR_RECORDING_FUNCTIONS(X)', '\nstatic void VKAPI_CALL nrDestroySwapchainKHR')
# Include the diagnostic-specific macro definition immediately before the original, if present.
pos = source.index('#define NR_RECORDING_FUNCTIONS(X)')
guard = source.rfind('#if', 0, pos)
if guard >= 0 and 'NR_DIAG_VULKAN_' in source[guard:pos]:
    macro = source[guard:source.index('\nstatic void VKAPI_CALL nrDestroySwapchainKHR', pos)]
aliases = re.findall(r'using NrWork\w+=NrWorkHook<PFN_(vk\w+),\d+>;', source)
assert len(aliases) == 39
record_names = ['CreateCommandPool','AllocateCommandBuffers','BeginCommandBuffer','EndCommandBuffer',
 'ResetCommandBuffer','ResetCommandPool','FreeCommandBuffers','DestroyCommandPool','CmdExecuteCommands',
 'CreateImage','CreateImageView','DestroyImage','DestroyImageView']
retained = ['DestroyDevice','DestroySwapchainKHR']
prefix = '#include <cassert>\n#include <cstring>\n#include <vulkan/vulkan.h>\nvoid downstream() {}\n'
work_types = re.findall(r'using (NrWork\w+)=NrWorkHook<PFN_(vk\w+),\d+>;', source)
for cls, api in work_types:
    prefix += f'struct {cls} {{ static inline PFN_{api} original=nullptr; static void Invoke() {{}} }};\n'
for n in record_names + retained:
    prefix += f'void nr{n}() {{}}\nPFN_vkVoidFunction nr_{n}=downstream;\n'
fixture = prefix + record + work + submit + '\nint main() {\n'
for cls, n in work_types:
    if command_only:
        fixture += f'assert(NrWorkAddress("{n}",downstream)=={cls}::Invoke); assert(reinterpret_cast<PFN_vkVoidFunction>({cls}::original)==downstream);\n'
        continue
    fixture += f'assert(NrWorkAddress("{n}",downstream)==downstream); assert(NrWorkAddress("{n}",nullptr)==nullptr);\n'
command_names = record_names[:9]
for n in record_names:
    expected = 'nr'+n if no_submit or (command_only and n in command_names) else 'nullptr'
    fixture += f'assert(NrRecordingAddress("vk{n}")=={expected});\n'
for n in retained:
    fixture += f'assert(NrRecordingAddress("vk{n}")==nr{n});\n'
fixture += 'assert(NrWorkAddress("unknown",downstream)==downstream); assert(NrRecordingAddress("unknown")==nullptr); HookDeviceSubmissions(nullptr); }\n'
(out/'observers.cpp').write_text(fixture,encoding='utf-8')
(out/'observers-install.cpp').write_text(macro + '\n' + hook,encoding='utf-8')
vcvars = r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmdprefix = '@echo off\ncall "'+vcvars+'" -vcvars_ver=14.44 >nul\n'
flags = '/nologo /std:c++17 /DNR_DIAG_VULKAN_NO_OBSERVERS=1 /I"'+str(root/'external/vulkan/include')+'" '
if command_only: flags += '/DNR_DIAG_VULKAN_COMMAND_ONLY=1 '
if no_submit: flags += '/DNR_DIAG_VULKAN_NO_SUBMIT=1 '
commands = cmdprefix + 'cl '+flags+'/EP observers-install.cpp > observers-install.i\nexit /b %errorlevel%\n'
(out/'observers-preprocess.cmd').write_text(commands,encoding='utf-8')
p=subprocess.run(['cmd','/d','/c','observers-preprocess.cmd'],cwd=out,capture_output=True,text=True)
assert p.returncode==0,p.stdout+p.stderr
installed=(out/'observers-install.i').read_text(encoding='utf-8')
disabled = [] if no_submit else record_names[9:] if command_only else record_names
pattern = r'DetourAttach\([^\n]*(?:'+('' if command_only else 'NrWork|')+r'nr_(?:'+'|'.join(disabled)+r'))'
if disabled: assert not re.search(pattern,installed), 'diagnostic still installs disabled observers'
if command_only:
    for cls, api in work_types:
        assert re.search(r'DetourAttach\([^\n]*'+cls+r'::original',installed), 'command diagnostic missing work hook '+api
    for n in (record_names if no_submit else command_names):
        assert re.search(r'DetourAttach\([^\n]*nr_'+n,installed), 'command diagnostic missing lifecycle hook '+n
for n in retained:
    assert re.search(r'DetourAttach\([^\n]*nr_'+n,installed), 'lost destruction/WSI hook'
for n in ['o_vkCreateInstance','o_vkCreateDevice','o_QueuePresentKHR','o_vkGetDeviceProcAddr']:
    assert re.search(r'DetourAttach\([^\n]*'+n,installed), 'lost retained hook '+n
(out/'observers-compile.cmd').write_text(cmdprefix+'cl '+flags+'/EHsc /O2 observers.cpp /Fe:observers.exe /Fo:observers.obj /link /INCREMENTAL:NO\nif errorlevel 1 exit /b %errorlevel%\nobservers.exe\nexit /b %errorlevel%\n',encoding='utf-8')
p=subprocess.run(['cmd','/d','/c','observers-compile.cmd'],cwd=out,capture_output=True,text=True)
print(p.stdout+p.stderr)
if p.returncode==0: print('PASS: production command/resource routing and installation split; device-submit installer bypasses; command_only='+str(command_only)+' no_submit='+str(no_submit))
sys.exit(p.returncode)
