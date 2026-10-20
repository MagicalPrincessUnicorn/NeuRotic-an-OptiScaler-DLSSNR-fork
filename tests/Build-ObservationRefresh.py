"""Build the isolated product with verified, read-only shared dependency sources."""
from pathlib import Path
import os,subprocess,json,hashlib,datetime
ROOT=Path(__file__).resolve().parents[1]
DEPS=ROOT.parent/'_deps'/'legacy-ui-20261003'
OUT=ROOT/'builds/capability-facts/release'
OUT.mkdir(parents=True,exist_ok=True)
assert (DEPS/'external/spdlog/include/spdlog/spdlog.h').is_file()
def git(*arguments,where=ROOT):
    return subprocess.check_output(['git','-c',f'safe.directory={where}',*arguments],cwd=where,text=True)
commit=git('rev-parse','HEAD').strip()
links=git('ls-tree','-r',commit,'external')
dependency_heads=[]
for line in links.splitlines():
    if not line.startswith('160000 '):continue
    _,_,expected,path=line.split(maxsplit=3)
    location=DEPS/path
    if not (location/'.git').exists():continue
    actual=git('rev-parse','HEAD',where=location).strip()
    assert actual==expected,(path,actual,expected)
    assert not git('diff','HEAD','--name-only',where=location).strip(),path
    dependency_heads.append({'path':str(location),'commit':actual})
for name,value in [('resource_build_date.h',datetime.datetime.now().strftime('%Y%m%d_%H%M%S')),
                   ('resource_build_commit.h',commit[:8])]:
    macro='VER_BUILD_DATE' if 'date' in name else 'VER_BUILD_COMMIT'
    (ROOT/'OptiScaler'/name).write_text(f'#define {macro} "{value}"\n')
args=[r'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    'OptiScaler/OptiScaler.vcxproj','/m:4','/v:minimal','/p:Configuration=Release','/p:Platform=x64',
    '/p:PlatformToolset=v145','/p:VCToolsVersion=14.44.35207',f'/p:SolutionDir={DEPS}\\',
    f'/p:OutDir={OUT}\\',f'/p:IntDir={OUT}/obj/','/p:TrackFileAccess=false',
    '/p:PostBuildEventUseInBuild=false','/p:PreBuildEventUseInBuild=false',
    '/fl','/flp:logfile=builds/capability-facts/release-build.log;verbosity=normal']
result=subprocess.run(args,cwd=ROOT,env={k.upper():v for k,v in os.environ.items()})
source_unchanged=not git('diff',commit,'--name-only','--','OptiScaler').strip()
receipt={'source_commit':commit,'arguments':args,'exit_code':result.returncode,'source_unchanged':source_unchanged,
    'dependency_heads':dependency_heads,'shared_dependencies_read_only':True}
dll=OUT/'OptiScaler.dll'
if result.returncode==0:receipt['product_sha256']=hashlib.sha256(dll.read_bytes()).hexdigest()
(OUT/'exact-build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(result.returncode or not source_unchanged)
