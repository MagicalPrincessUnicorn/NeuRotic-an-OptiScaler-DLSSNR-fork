"""Load the exact diagnostic DLL in an owned process; verify system WinMM forwarding."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import sys

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
                          'vkGetDeviceProcAddr','vkQueuePresentKHR','vkQueueSubmit']}
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
assert before == after, 'selected entrypoint bytes changed'
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
assert kernel.FreeLibrary(test._handle)
assert before == snapshot(), 'diagnostic detach changed selected entrypoints'
assert all(p.read_bytes()==data for p,data in sentinels.items()), 'config/log fixture was modified'
# A second attach exercises reloading after the no-op diagnostic detach.
again = C.WinDLL(str(path),winmode=0x1100)
again.timeGetDevCaps.argtypes = [C.c_void_p,W.UINT]
again.timeGetDevCaps.restype = W.UINT
assert again.timeGetDevCaps(C.byref(proxy_caps),C.sizeof(proxy_caps)) == 0
assert bytes(real_caps)==bytes(proxy_caps)
assert kernel.FreeLibrary(again._handle)
assert before == snapshot()
receipt = {'dll':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
           'loadedExactPath':True,'selectedEntrypointsUnchangedOnAttachAndDetach':list(before),
           'providerModulesBefore':provider_before,'providerModulesAfter':provider_after,
           'winmmTimeGetTimeForwarded':True,'winmmTimeGetDevCapsMatch':True,
           'proxyLoadedBeforeSystemReference':True,
           'reloadForwardingPassed':True,'configAndLogSentinelsUnchanged':True,
           'gameExecuted':False,'scope':'Owned process only; selected byte probes, not proof of all APIs or game performance.'}
output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
