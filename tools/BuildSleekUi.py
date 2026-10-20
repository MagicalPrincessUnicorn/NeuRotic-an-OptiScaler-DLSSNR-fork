"""Build the committed UI candidate with the pinned toolchain and one stable cache."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=root/'builds/ui-release'
evidence=build/'sleek'
evidence.mkdir(parents=True,exist_ok=True)
def git(*args):
    return subprocess.check_output(['git','-c','safe.directory='+root.as_posix(),'-C',str(root),*args],text=True).strip()
commit=git('rev-parse','HEAD')
if git('status','--porcelain','--','OptiScaler'):
    raise RuntimeError('Commit the product source before sealing the exact build.')
product_tree=git('rev-parse','HEAD:OptiScaler')
(root/'OptiScaler/resource_build_commit.h').write_text(f'#define VER_BUILD_COMMIT "{commit[:8]}"\n',encoding='ascii')
from datetime import datetime
(root/'OptiScaler/resource_build_date.h').write_text(f'#define VER_BUILD_DATE "{datetime.now():%Y%m%d_%H%M%S}"\n',encoding='ascii')
environment={key:value for key,value in os.environ.items() if key.casefold()!='path'}
environment['PATH']=os.environ['PATH']
msbuild=Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe')
dependencies=root.parent/'_deps'/'legacy-ui-20261003'
arguments=[str(msbuild),str(root/'OptiScaler/OptiScaler.vcxproj'),'/m:4',
 '/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v145','/p:VCToolsVersion=14.44.35207',
 f'/p:SolutionDir={dependencies}\\',f'/p:OutDir={build / "bin"}\\',f'/p:IntDir={build / "obj"}\\',
 '/p:PreBuildEventUseInBuild=false','/p:PostBuildEventUseInBuild=false','/p:TrackFileAccess=false','/nologo','/v:minimal']
log=evidence/'product-build.log'
with log.open('w',encoding='utf-8',errors='replace') as output:
    result=subprocess.run(arguments,cwd=root,env=environment,stdout=output,stderr=subprocess.STDOUT)
unchanged=git('rev-parse','HEAD')==commit and not git('status','--porcelain','--','OptiScaler')
receipt={'source_commit':commit,'product_tree':product_tree,'exit_code':result.returncode,
 'source_unchanged':unchanged,'configuration':'Release x64','toolset':'v145','compiler':'14.44.35207',
 'cache':str(build/'obj'),'game_runtime_verified':False,'prior_acceptance_transfers':False,
 'build_log':str(log),'build_log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
if result.returncode==0 and unchanged:
    dll=build/'bin/OptiScaler.dll'
    receipt['product']={'path':str(dll),'bytes':dll.stat().st_size,'sha256':hashlib.sha256(dll.read_bytes()).hexdigest()}
(evidence/'exact-build.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,indent=2))
print('\n'.join(log.read_text(encoding='utf-8',errors='replace').splitlines()[-12:]))
sys.exit(result.returncode if result.returncode else (0 if unchanged else 1))
