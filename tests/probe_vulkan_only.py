"""Load the exact diagnostic DLL in an owned process; verify system WinMM forwarding."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import sys

no_submit = '--no-submit' in sys.argv
command_only = '--command-only' in sys.argv or no_submit
no_observers = '--no-observers' in sys.argv or command_only
no_augment = '--no-augment' in sys.argv or no_observers
no_legacy = '--no-legacy' in sys.argv or no_augment
path = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
assert path.name.lower() == 'winmm.dll'
kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.GetModuleFileNameW.argtypes = [W.HMODULE, W.LPWSTR, W.DWORD]
kernel.GetModuleFileNameW.restype = W.DWORD
kernel.GetModuleHandleW.argtypes = [W.LPCWSTR]
kernel.GetModuleHandleW.restype = W.HMODULE
kernel.FreeLibrary.argtypes = [W.HMODULE]
kernel.FreeLibrary.restype = W.BOOL

libraries = {name:C.WinDLL(name, winmode=0x800) for name in
             ['kernelbase.dll','ntdll.dll','user32.dll','dxgi.dll','vulkan-1.dll']}
checks = {'kernelbase.dll':['LoadLibraryExW','GetProcAddress'],
          'ntdll.dll':['LdrLoadDll','LdrGetProcedureAddress'],
          'user32.dll':['GetRawInputData','PeekMessageW','SetCursorPos'],
          'dxgi.dll':['CreateDXGIFactory','CreateDXGIFactory1'],
          'vulkan-1.dll':['vkCreateInstance','vkCreateDevice','vkGetInstanceProcAddr',
                          'vkGetDeviceProcAddr','vkQueuePresentKHR','vkQueueSubmit',
                          'vkCmdBindPipeline','vkCmdBindDescriptorSets','vkCmdPipelineBarrier',
                          'vkCmdDrawIndexed','vkBeginCommandBuffer']}
observer_entries = ['vkCreateCommandPool','vkAllocateCommandBuffers','vkBeginCommandBuffer','vkEndCommandBuffer',
    'vkResetCommandBuffer','vkResetCommandPool','vkFreeCommandBuffers','vkDestroyCommandPool','vkCmdExecuteCommands',
    'vkCreateImage','vkCreateImageView','vkDestroyImage','vkDestroyImageView',
    'vkCmdDraw','vkCmdDrawIndexed','vkCmdDispatch','vkCmdCopyImage','vkCmdBlitImage']
if no_observers:
    checks['vulkan-1.dll'] = list(dict.fromkeys(checks['vulkan-1.dll'] + observer_entries + ['vkDestroyDevice','vkDestroySwapchainKHR']))
command_entries = [n for n in observer_entries if no_submit or n not in ['vkCreateImage','vkCreateImageView','vkDestroyImage','vkDestroyImageView']]
if command_only:
    observer_entries = [n for n in observer_entries if n not in command_entries]
def snapshot():
    return {name+'!'+entry:C.string_at(C.cast(getattr(libraries[name],entry),C.c_void_p),32).hex()
            for name,entries in checks.items() for entry in entries}
before = snapshot()
sentinels = {p:p.read_bytes() for p in [path.parent/'OptiScaler.ini',path.parent/'OptiScaler.log']}
providers = ['_nvngx.dll','nvngx.dll','nvngx_dlss.dll','nvngx_dlssd.dll','nvngx_dlssnr.dll',
             'libxess.dll','amd_fidelityfx_dx12.dll','sl.interposer.dll']
provider_before = {p:bool(kernel.GetModuleHandleW(p)) for p in providers}
assert not kernel.GetModuleHandleW('winmm.dll'), 'probe requires proxy-first loading in a fresh process'
test = C.WinDLL(str(path),winmode=0x1100)
module_path = C.create_unicode_buffer(32768)
assert kernel.GetModuleFileNameW(test._handle,module_path,len(module_path))
assert Path(module_path.value).resolve() == path
after = snapshot()
non_vulkan = [key for key in before if not key.startswith('vulkan-1.dll!')]
assert all(before[key]==after[key] for key in non_vulkan), 'unrelated API entrypoint changed'
required = ['vkCreateInstance','vkCreateDevice','vkGetInstanceProcAddr','vkGetDeviceProcAddr','vkQueuePresentKHR','vkQueueSubmit']
legacy = ['vkQueueSubmit','vkCmdBindPipeline','vkCmdBindDescriptorSets','vkCmdPipelineBarrier']
if no_legacy:
    required.remove('vkQueueSubmit')
    assert all(before['vulkan-1.dll!'+name]==after['vulkan-1.dll!'+name] for name in legacy), 'legacy interceptor still active'
if no_observers:
    assert all(before['vulkan-1.dll!'+name]==after['vulkan-1.dll!'+name] for name in observer_entries), 'command/resource interceptor still active'
    required += ['vkDestroyDevice','vkDestroySwapchainKHR']
    if command_only: required += command_entries
else:
    required += ['vkCmdDrawIndexed','vkBeginCommandBuffer']
assert all(before['vulkan-1.dll!'+name]!=after['vulkan-1.dll!'+name] for name in required), 'required Vulkan hook entrypoint unchanged'
startup = (path.parent/'OptiScaler.log').read_text()
assert 'NR_DIAGNOSTIC_VULKAN_ONLY' in startup, 'missing diagnostic startup receipt'
expected_status = 'loader_hook_commit=0 recording_hook_commit=50' if no_legacy else 'loader_hook_commit=0 recording_hook_commit=0'
assert expected_status in startup, 'hook installation/split not confirmed'
if no_legacy: assert 'legacy_command_interceptor=disabled' in startup
if no_augment:
    assert 'generic_startup_augmentation=disabled' in startup
    assert ('diagnostic-vulkan-no-submit' if no_submit else 'diagnostic-vulkan-command-only' if command_only else 'diagnostic-vulkan-no-observers' if no_observers else 'diagnostic-vulkan-no-augmentation') in startup
if no_submit:
    assert 'command_resource_observers=enabled device_submit_observers=disabled' in startup
    if '--indexed-images' in sys.argv: assert 'image_view_cleanup=indexed' in startup
elif command_only:
    assert 'command_observers=enabled resource_submit_observers=disabled' in startup
elif no_observers: assert 'command_resource_submit_observers=disabled' in startup
assert 'NR=off overlay=off provider_replacement=off' in startup
provider_after = {p:bool(kernel.GetModuleHandleW(p)) for p in providers}
assert provider_before == provider_after, 'provider DLL loaded by diagnostic startup'
system_dir = C.create_unicode_buffer(32768)
kernel.GetSystemDirectoryW.argtypes = [W.LPWSTR,W.UINT]
kernel.GetSystemDirectoryW.restype = W.UINT
length = kernel.GetSystemDirectoryW(system_dir,len(system_dir))
assert 0 < length < len(system_dir)
libraries['winmm.dll'] = C.WinDLL(str(Path(system_dir.value)/'winmm.dll'),winmode=0x800)
assert libraries['winmm.dll']._handle != test._handle
for module in [libraries['winmm.dll'],test]:
    module.timeGetTime.restype = W.DWORD
    module.timeGetDevCaps.argtypes = [C.c_void_p,W.UINT]
    module.timeGetDevCaps.restype = W.UINT
class TIMECAPS(C.Structure):
    _fields_ = [('minimum',W.UINT),('maximum',W.UINT)]
real_caps,proxy_caps = TIMECAPS(),TIMECAPS()
assert libraries['winmm.dll'].timeGetDevCaps(C.byref(real_caps),C.sizeof(real_caps)) == 0
assert test.timeGetDevCaps(C.byref(proxy_caps),C.sizeof(proxy_caps)) == 0
assert bytes(real_caps) == bytes(proxy_caps)
start = libraries['winmm.dll'].timeGetTime()
value = test.timeGetTime()
end = libraries['winmm.dll'].timeGetTime()
assert (value-start)&0xffffffff <= (end-start)&0xffffffff
# This variant pins its code for process lifetime: unloading live detours is unsafe.
assert all(p.read_bytes()==data for p,data in sentinels.items() if p.suffix=='.ini'), 'config changed'
receipt = {'dll':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
           'loadedExactPath':True,'selectedNonVulkanEntrypointsUnchanged':non_vulkan,
           'selectedVulkanEntrypointsChanged':required,'startupReceipt':startup,
           'legacyEntrypointsUnchanged':legacy if no_legacy else [],
           'observerEntrypointsUnchanged':observer_entries if no_observers else [],
           'providerModulesBefore':provider_before,'providerModulesAfter':provider_after,
           'winmmTimeGetTimeForwarded':True,'winmmTimeGetDevCapsMatch':True,
           'configUnchanged':True,'lifetime':'process pinned; no runtime unload',
           'gameExecuted':False,'scope':'Owned process API/forwarding probe; game performance untested.'}
output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
