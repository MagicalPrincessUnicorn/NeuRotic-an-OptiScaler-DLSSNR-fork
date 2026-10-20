"""Compile affected SDK units without executing providers or retaining object caches."""
from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
OUT.mkdir(parents=True,exist_ok=True)
variants={
'v1': ('external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain',
       ['external/FidelityFX-SDK/sdk/include','external/FidelityFX-SDK/sdk/libs','external/FidelityFX-SDK/ffx-api/include'],
       'external/FidelityFX-SDK/ffx-api/src/dx12/ffx_provider_framegenerationswapchain_dx12.cpp'),
'v2': ('external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/dx12',
       ['external/FidelityFX-SDK-v2/Kits/FidelityFX/api/include','external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/include','external/FidelityFX-SDK-v2/Kits/FidelityFX/upscalers/include','external/FidelityFX-SDK-v2/Kits/OpenSource'],
       'external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/dx12/ffx_provider_fsr3framegenerationswapchain_dx12.cpp')}
receipts=[]
for variant,(folder,includes,provider) in variants.items():
    for index,source in enumerate([folder+'/FrameInterpolationSwapchainDX12.cpp',folder+'/FrameInterpolationSwapchainDX12_Helpers.cpp',provider]):
        name=variant+'-unit-'+str(index); cmd=OUT/(name+'.cmd')
        command='cl /nologo /c /std:c++20 /EHsc /DNOMINMAX /DFFX_BACKEND_DX12 /W3 '+ ' '.join('/I"'+str(ROOT/i)+'"' for i in includes)
        command+=' "'+str(ROOT/source)+'" /Fo"'+str(OUT/(name+'.obj'))+'"'
        cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\n'+command+'\nexit /b %errorlevel%\n')
        result=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,timeout=120,capture_output=True,text=True)
        (OUT/(name+'-build.log')).write_text(result.stdout+'\n'+result.stderr)
        print(name,result.returncode,result.stdout[-3000:],result.stderr)
        receipts.append({'variant':variant,'source':source,'exit':result.returncode})
        obj=OUT/(name+'.obj')
        if obj.exists(): obj.unlink()
(OUT/'sdk-units-receipt.json').write_text(json.dumps(receipts,indent=2)+'\n')
raise SystemExit(any(r['exit']!=0 for r in receipts))

