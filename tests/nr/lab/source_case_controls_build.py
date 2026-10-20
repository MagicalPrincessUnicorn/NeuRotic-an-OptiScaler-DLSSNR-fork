"""Build a pinned CPU-only diagnostic and verify controls against source mutants."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
SOURCE=Path('tools/nr/lab/SourceCaseControls.cpp')
CASES=['NLAB-SIG-005','NLAB-MUL-001','NLAB-API-004']
VC=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvarsall.bat')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def source_snapshot(destination):
    pending=[SOURCE];seen=set()
    while pending:
        relative=pending.pop()
        if relative in seen:continue
        seen.add(relative);source=ROOT/relative;target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        for delim,name in re.findall(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',source.read_text(encoding='utf-8-sig'),re.M):
            candidates=([source.parent/name] if delim=='"' else [])+[ROOT/'OptiScaler'/name,ROOT/name]
            for candidate in candidates:
                resolved=candidate.resolve()
                if resolved.is_relative_to(ROOT) and resolved.is_file():pending.append(resolved.relative_to(ROOT));break
    return sorted(seen)

def compile_control(snapshot,output):
    output.mkdir(parents=True,exist_ok=True);script=output/'build.cmd'
    command=f'cl /nologo /Bv /MT /O2 /utf-8 /std:c++20 /EHsc /W4 /WX /I"{snapshot / "OptiScaler"}" "{snapshot / SOURCE}" /Fe:"{output / "SourceCaseControls.exe"}" /Fo:"{output / "SourceCaseControls.obj"}" /link windowscodecs.lib ole32.lib'
    script.write_text('@echo off\nset CL=\nset _CL_=\ncall "'+str(VC)+'" x64 -vcvars_ver=14.44.35207\nif errorlevel 1 exit /b %errorlevel%\n'+command+'\nexit /b %errorlevel%\n')
    with (output/'build.log').open('wb') as log:
        result=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=output,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError('compile failed: '+str(output/'build.log'))

MUTANTS=[
    ('empty-region','OptiScaler/dlssnr/NativeTemporalInputs.h','region.width && region.height &&','region.height &&','NLAB-SIG-005','zero-width-rejected'),
    ('wrapped-region','OptiScaler/dlssnr/NativeTemporalInputs.h','region.width <= width - region.x','region.x + region.width <= width','NLAB-SIG-005','overflowing-subrect-rejected'),
    ('mip-boundary','OptiScaler/nr/contracts/SemanticDescriptions.h','selectedMip->value >= mipCount->value','selectedMip->value > mipCount->value','NLAB-SIG-005','mip-index-at-count-rejected'),
    ('fractional-pass','OptiScaler/dlssnr/DlssNr_BasicMultipass.h','std::ceil((std::max)(p.model, p.detail))','std::floor((std::max)(p.model, p.detail))','NLAB-MUL-001','basic-1-pass-count'),
    ('advanced-overwrite','OptiScaler/dlssnr/DlssNr_BasicMultipass.h','&& !c.DlssNrBasicMultipass.value_or_default().advanced','/* altered advanced gate */','NLAB-MUL-001','advanced-retains-per-pass-intent'),
    ('shared-resource-refusal','OptiScaler/dlssnr/DlssNr_PresentCompatibility.h','value.api == Api::D3D11 && (!value.sharedResources || !value.synchronization)','value.api == Api::D3D11 && !value.synchronization','NLAB-API-004','missing-shared-resources-refused'),
]

def build(output,mutations=True):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True);snapshot=output/'source-snapshot'
    sources=source_snapshot(snapshot);compile_control(snapshot,output)
    observations=output/'observations';observations.mkdir(exist_ok=True)
    for case in CASES:
        target=observations/(case+'.json')
        if target.exists():raise RuntimeError('observation exists; use a fresh build directory')
        subprocess.run([str(output/'SourceCaseControls.exe'),'--case',case,'--output',str(target)],check=True,timeout=30)
    env=dict(os.environ,NR_SOURCE_CASE_EXE=str(output/'SourceCaseControls.exe'))
    with (output/'test-suite.log').open('wb') as log:
        subprocess.run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests/nr/lab'),'-p','source_case_controls_test.py','-v'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    results=[]
    if mutations:
        for name,file,before,after,case,check in MUTANTS:
            mutant=output/'mutants'/name;mutant_snapshot=mutant/'source-snapshot';shutil.copytree(snapshot,mutant_snapshot)
            header=mutant_snapshot/file;text=header.read_text();assert text.count(before)==1,(name,'mutation anchor')
            header.write_text(text.replace(before,after));compile_control(mutant_snapshot,mutant)
            process=subprocess.run([str(mutant/'SourceCaseControls.exe'),'--case',case,'--output',str(mutant/'observations.json')],timeout=30)
            result=json.loads((mutant/'observations.json').read_text());failed=[c['name'] for c in result['checks'] if not c['passed']]
            killed=process.returncode==1 and result['success'] is False and check in failed
            results.append({'mutation':name,'case_id':case,'changed_source':file,'expected_failed_check':check,'observed_failed_checks':failed,'exit_code':process.returncode,'killed':killed,'mutated_sha256':sha(header)})
            if not killed:raise RuntimeError('mutant survived: '+name)
    package=output/'package';package.mkdir()
    shutil.copyfile(output/'SourceCaseControls.exe',package/'SourceCaseControls.exe')
    shutil.copytree(snapshot,package/'source-snapshot');shutil.copytree(observations,package/'observations')
    for file in ('build.cmd','build.log','test-suite.log'):shutil.copyfile(output/file,package/file)
    (package/'MUTATION_RESULTS.json').write_text(json.dumps(results,indent=2))
    receipt={'schema_version':1,'status':'PASS','scope':'CPU_SOURCE_ONLY','catalog_coverage':'PARTIAL','compiler':'MSVC 14.44.35207 x64 /MT /O2 /std:c++20 /W4 /WX','cases':CASES,'process_tests':4,'mutants_killed':sum(x['killed'] for x in results),'source_hashes':{p.as_posix():sha(snapshot/p) for p in sources},'binary_sha256':sha(output/'SourceCaseControls.exe')}
    (package/'SOURCE_BUILD_MANIFEST.json').write_text(json.dumps(receipt,indent=2))
    files=[{'path':p.relative_to(package).as_posix(),'sha256':sha(p)} for p in sorted(package.rglob('*')) if p.is_file()]
    (package/'SOURCE_CONTROLS_MANIFEST.json').write_text(json.dumps({'schema_version':1,'scope':'CPU_SOURCE_ONLY','catalog_coverage':'PARTIAL','cases':CASES,'files':files},indent=2))
    return package

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--out',required=True);parser.add_argument('--no-mutations',action='store_true');args=parser.parse_args()
    print(build(args.out,not args.no_mutations))
