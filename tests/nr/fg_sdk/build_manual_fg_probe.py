"""Compile only. The resulting worker requires explicit human opt-in to run."""
from pathlib import Path
import sys,json,hashlib
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
cmd=OUT/'manual-fg-probe.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.51 >nul\ncl /nologo /std:c++20 /EHsc /MD /W4 /O2 /I"'+str(ROOT)+'" /I"'+str(ROOT/'external/FidelityFX-SDK/ffx-api/include')+'" "'+str(ROOT/'tests/nr/fg_sdk/manual_fg_probe.cpp')+'" /Fe"'+str(OUT/'W03FgProbe.exe')+'" /link d3d12.lib dxgi.lib bcrypt.lib user32.lib\nexit /b %errorlevel%\n')
b=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,capture_output=True,text=True,timeout=120)
(OUT/'manual-fg-probe-build.log').write_text(b.stdout+b.stderr);print(b.stdout+b.stderr)
for p in OUT.glob('manual_fg_probe.obj'):p.unlink()
receipt={'build_exit':b.returncode,'worker_executed':False,'toolchain':'MSVC14.51.36231 C++20 /MD Release, same as full SDK','supervision':b.supervision}
if b.returncode==0:receipt['sha256']=hashlib.sha256((OUT/'W03FgProbe.exe').read_bytes()).hexdigest()
(OUT/'manual-fg-probe-build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(b.returncode)
