"""Build one stable Release cache with a case-deduplicated Windows environment."""
import os, pathlib, subprocess, json, datetime
root=pathlib.Path(__file__).resolve().parents[2]
output=root/'builds/capability-facts/release'
output.mkdir(parents=True,exist_ok=True)
commit=subprocess.check_output(['git','-c',f'safe.directory={root}','rev-parse','--short','HEAD'],cwd=root,text=True).strip()
(root/'OptiScaler/resource_build_date.h').write_text('#define VER_BUILD_DATE "'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'"\n')
(root/'OptiScaler/resource_build_commit.h').write_text('#define VER_BUILD_COMMIT "'+commit+'"\n')
msbuild=r'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
args=[msbuild,'OptiScaler/OptiScaler.vcxproj','/m:4','/p:Configuration=Release','/p:Platform=x64',
      '/p:PlatformToolset=v145','/p:VCToolsVersion=14.44.35207',f'/p:SolutionDir={root}/',
      f'/p:OutDir={output}/',f'/p:IntDir={output}/obj/',
      '/p:PostBuildEventUseInBuild=false','/p:PreBuildEventUseInBuild=false',
      '/fl','/flp:logfile=builds/capability-facts/release-build.log;verbosity=normal','/v:minimal']
env={k.upper():v for k,v in os.environ.items()}
result=subprocess.run(args,cwd=root,env=env)
(root/'builds/capability-facts/build-receipt.json').write_text(json.dumps({'arguments':args,'exit_code':result.returncode,'environment_keys_deduplicated':True},indent=2)+'\n')
raise SystemExit(result.returncode)
