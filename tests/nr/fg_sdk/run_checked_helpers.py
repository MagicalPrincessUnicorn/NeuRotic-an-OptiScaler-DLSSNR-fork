from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
OUT.mkdir(parents=True,exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'current'
failed=0
for variant in ('v1','v2'):
    name='helpers-'+variant
    flags='/DSDK_V2' if variant=='v2' else ''
    extra=' '+chr(34)+str(ROOT/'external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12_Helpers.cpp')+chr(34) if variant=='v1' else ''
    cmd=OUT/(name+'.cmd')
    cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /Gy /DNOMINMAX '+flags+' /I"'+str(ROOT)+'" /I"'+str(ROOT/'external/FidelityFX-SDK/sdk/include')+'" "'+str(ROOT/'tests/nr/fg_sdk/checked_helpers.cpp')+'"'+extra+' /Fe"'+str(OUT/(name+'.exe'))+'" /link /OPT:REF d3d12.lib dxgi.lib dxguid.lib\nexit /b %errorlevel%\n')
    build=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,timeout=120,capture_output=True,text=True)
    (OUT/(label+'-'+name+'-build.log')).write_text(build.stdout+'\n'+build.stderr)
    print(build.stdout,build.stderr)
    result=None
    if build.returncode==0:
        result=run([str(OUT/(name+'.exe'))],cwd=OUT,timeout=20,capture_output=True,text=True)
        (OUT/(label+'-'+name+'-run.log')).write_text(result.stdout+'\n'+result.stderr)
        print(result.stdout,result.stderr)
    failed|=build.returncode or (result.returncode if result else 1)
    (OUT/(label+'-'+name+'-receipt.json')).write_text(json.dumps({'build_exit':build.returncode,'run_exit':None if result is None else result.returncode,'gpu':'WARP_ONLY','provider':'NOT_RUN','variant':variant},indent=2)+'\n')
    for obj in OUT.glob('*.obj'): obj.unlink()
raise SystemExit(failed)
