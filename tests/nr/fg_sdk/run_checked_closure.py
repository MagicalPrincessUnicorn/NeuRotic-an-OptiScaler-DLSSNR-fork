"""Compile/run SDK closure CPU controls under the existing bounded process owner."""
from pathlib import Path
import sys, json, hashlib, datetime, os
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run, developer_environment_script
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
OUT.mkdir(parents=True,exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'current'
def size():
    total=0
    for base,dirs,files in os.walk(OUT,followlinks=False):
        dirs[:]=[d for d in dirs if not (Path(base)/d).is_junction()]
        total+=sum((Path(base)/f).stat().st_size for f in files)
    return total
before=size()
cmd=OUT/'compile.cmd'
cmd.write_text('@echo off\ncall "'+r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'+'" x64 -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /WX /I"'+str(ROOT)+'" "'+str(ROOT/'tests/nr/fg_sdk/checked_closure.cpp')+'" "'+str(ROOT/'external/FidelityFX-CheckedClosure/CheckedSdkExports.cpp')+'" /Fe"'+str(OUT/'checked_closure.exe')+'"\nexit /b %errorlevel%\n')
build=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,timeout=120,capture_output=True,text=True)
(OUT/(label+'-build.log')).write_text(build.stdout+'\n'+build.stderr)
print(build.stdout,build.stderr)
result=None
if build.returncode==0:
    result=run([str(OUT/'checked_closure.exe')],cwd=OUT,timeout=20,capture_output=True,text=True)
    (OUT/(label+'-run.log')).write_text(result.stdout+'\n'+result.stderr)
    print(result.stdout,result.stderr)
for path in OUT.glob('*.obj'): path.unlink()
receipt={'label':label,'build_exit':build.returncode,'run_exit':None if result is None else result.returncode,'before_bytes':before,'after_bytes':size(),'source_sha256':hashlib.sha256((ROOT/'tests/nr/fg_sdk/checked_closure.cpp').read_bytes()).hexdigest(),'provider':'NOT_RUN','gpu':'NOT_RUN'}
(OUT/(label+'-receipt.json')).write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(build.returncode or (result.returncode if result else 1))

