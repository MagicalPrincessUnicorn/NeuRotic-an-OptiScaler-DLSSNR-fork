from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
OUT.mkdir(parents=True,exist_ok=True)
sdk=ROOT/'external/FidelityFX-SDK/sdk'
folder=sdk/'src/backends/dx12/FrameInterpolationSwapchain'
sources=[ROOT/'tests/nr/fg_sdk/checked_swapchain.cpp',sdk/'src/backends/dx12/ffx_dx12.cpp',sdk/'src/backends/shared/ffx_shader_blobs.cpp',sdk/'src/shared/ffx_assert.cpp',sdk/'src/shared/ffx_breadcrumbs_list.cpp',sdk/'src/shared/ffx_message.cpp']
sources += [folder/name for name in ['FrameInterpolationSwapchainDX12.cpp','FrameInterpolationSwapchainDX12_Helpers.cpp','FrameInterpolationSwapchainDX12_UiComposition.cpp','FrameInterpolationSwapchainDX12_DebugPacing.cpp']]
includes=[ROOT,sdk/'include',sdk/'libs',sdk/'src/backends/shared',sdk/'src/shared']
command='cl /nologo /std:c++20 /EHsc /DNOMINMAX /DUNICODE /D_UNICODE /DFFX_BACKEND_DX12 /DFFX_NR_TEST_ACTION_ADMISSION /W3 /Gy /O1 '+ ' '.join('/I"'+str(p)+'"' for p in includes)
command+=' '+' '.join('"'+str(p)+'"' for p in sources)+' /Fe"'+str(OUT/'checked_swapchain.exe')+'" /link /OPT:REF d3d12.lib dxgi.lib d3dcompiler.lib user32.lib advapi32.lib'
cmd=OUT/'checked_swapchain.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\n'+command+'\nexit /b %errorlevel%\n')
build=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,timeout=180,capture_output=True,text=True)
(OUT/'swapchain-build.log').write_text(build.stdout+'\n'+build.stderr); print(build.stdout[-6500:],build.stderr)
result=None
results=[]
if build.returncode==0:
 for mode in range(7):
  result=run([str(OUT/'checked_swapchain.exe'),str(mode)],cwd=OUT,timeout=20,capture_output=True,text=True)
  (OUT/('swapchain-run-'+str(mode)+'.log')).write_text(result.stdout+'\n'+result.stderr); print(mode,result.stdout,result.stderr)
  results.append({'mode':mode,'exit':result.returncode})
for path in OUT.glob('*.obj'): path.unlink()
(OUT/'swapchain-receipt.json').write_text(json.dumps({'build_exit':build.returncode,'runs':results,'scope':'Actual v1 SDK swapchain and worker mechanics on WARP; synthetic empty FG callback, callback error count1, actual Close rejection, real invalid Present/GetBuffer HRESULT latching; no algorithm/provider/game'},indent=2)+'\n')
raise SystemExit(build.returncode or any(r['exit'] for r in results))
