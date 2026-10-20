"""Stable local verification/build driver; never runs or installs a game."""
import json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'builds/candidate-observer'
VCVARS=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
def run(args,name):
    OUT.mkdir(parents=True,exist_ok=True)
    command='call "'+VCVARS+'" x64 -vcvars_ver=14.44 >nul && '+subprocess.list2cmdline(args)
    env={}
    for key,value in os.environ.items():
        old=next((k for k in env if k.lower()==key.lower()),None)
        if old is None:env[key]=value
        elif key.lower()=='path' and len(value)>len(env[old]):env[old]=value
    with (OUT/(name+'.log')).open('w',encoding='utf-8') as log:
        result=subprocess.run(command,shell=True,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    print(name,result.returncode,flush=True)
    print('\n'.join((OUT/(name+'.log')).read_text(errors='replace').splitlines()[-15:]),flush=True)
    return result.returncode
def storage():
    rows=[]
    for checkout in Path(r'C:\NeuRotic').iterdir():
        if not checkout.is_dir() or checkout.is_junction() or checkout.is_symlink():continue
        for base in ([checkout] if checkout.name=='builds' else [checkout/'builds',checkout/'x64',checkout/'OptiScaler/x64']):
            if not base.exists():continue
            total=disposable=0
            for directory,dirs,files in os.walk(base,followlinks=False):
                dirs[:]=[n for n in dirs if not os.stat(Path(directory)/n,follow_symlinks=False).st_file_attributes & 0x400]
                for name in files:
                    p=Path(directory)/name;st=os.stat(p,follow_symlinks=False)
                    if st.st_file_attributes & 0x400:continue
                    total+=st.st_size
                    if p.suffix.lower() in {'.obj','.pch','.iobj','.ipdb','.ilk'}:disposable+=st.st_size
            rows.append({'root':str(base),'total_bytes':total,'disposable_bytes':disposable})
    return rows
if __name__=='__main__':
    mode=sys.argv[1]
    if mode=='storage':
        rows=storage();OUT.mkdir(parents=True,exist_ok=True)
        (OUT/(sys.argv[2]+'.json')).write_text(json.dumps(rows,indent=2))
        print('Workspace disposable GiB:',round(sum(r['disposable_bytes'] for r in rows)/2**30,3))
    elif mode=='build':
        for name,define,value in [('resource_build_date.h','VER_BUILD_DATE','local_candidate'),
                                  ('resource_build_commit.h','VER_BUILD_COMMIT','local_candidate_dirty')]:
            p=ROOT/'OptiScaler'/name
            if not p.exists():p.write_text('#define '+define+' "'+value+'"\n')
        sys.exit(run(['msbuild','OptiScaler/OptiScaler.vcxproj','/nologo','/m:4','/verbosity:minimal',
          '/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v145','/p:VCToolsVersion=14.44.35207',
          '/p:SolutionDir='+str(ROOT)+'\\','/p:OutDir='+str(OUT/'release/out')+'\\',
          '/p:IntDir='+str(OUT/'release/obj')+'\\','/p:PostBuildEventUseInBuild=false','/p:PreBuildEventUseInBuild=false'],'release-build'))
    elif mode=='budget':
        from verify_candidate_package import clean
        base=OUT/'budget'
        for part in ('obj','out'):(base/part).mkdir(parents=True,exist_ok=True)
        common=['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/W4','/WX','/IOptiScaler']
        code=run(common+['tests/nr_candidate_budget.cpp','/Fo:'+str(base/'obj')+'\\',
          '/Fe:'+str(base/'out/budget.exe')],'budget-build')
        if not code:code=run([str(base/'out/budget.exe')],'budget-test')
        if not code:code=run(common+['/DNR_CANDIDATE_TEST_GATE','tests/nr_candidate_budget_capture.cpp','OptiScaler/nr/diagnostics/candidate/Observer.cpp',
          '/Fo:'+str(base/'obj')+'\\','/Fe:'+str(base/'out/capture.exe')],'budget-capture-build')
        capture=base/'capture-verified';report=base/'report-verified'
        if not code:
            clean(capture);clean(report)
            code=run([str(base/'out/capture.exe'),str(capture)],'budget-capture-test')
        if not code:
            code=run([sys.executable,'tools/diagnostics/candidate_observer.py','analyze','--trace',
              str(capture/'candidate-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/trace.jsonl'),'--out',str(report)],'budget-trace-analysis')
        if not code:
            result=json.loads((report/'report.json').read_text())
            rows=result['budget_refusals']['reasons']
            assert rows[4]=={'reason':'window_quota_slots','count':'3','exact':True}
            assert sum(int(r['count']) for r in rows)==3 and not result['complete_transport'] and not result['may_admit']
        sys.exit(code)
    elif mode=='publication':
        from verify_candidate_package import clean
        base=OUT/'publication'
        for part in ('obj','out'):(base/part).mkdir(parents=True,exist_ok=True)
        capture=base/'capture-verified';clean(capture)
        common=['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/W4','/WX']
        code=run(common+['/GL','/LD','/IOptiScaler','tests/nr_candidate_publication_dll.cpp',
          'OptiScaler/nr/diagnostics/candidate/Observer.cpp','/Fo:'+str(base/'obj')+'\\',
          '/Fe:'+str(base/'out/publication.dll'),'/link','/LTCG','/OPT:REF','/OPT:ICF'],'publication-dll-build')
        if not code:code=run(common+['tests/nr_candidate_publication_host.cpp','/Fo:'+str(base/'obj')+'\\',
          '/Fe:'+str(base/'out/host.exe')],'publication-host-build')
        if not code:code=run([str(base/'out/host.exe'),str(base/'out/publication.dll'),str(capture)],'publication-test')
        sys.exit(code)
    elif mode=='idle':
        from verify_candidate_package import clean
        base=OUT/'idle'
        for part in ('obj','out'):(base/part).mkdir(parents=True,exist_ok=True)
        capture=base/'capture-verified';clean(capture)
        code=run(['cl','/nologo','/std:c++20','/EHsc','/MD','/O2','/GL','/W4','/WX','/IOptiScaler',
          'tests/nr_candidate_idle.cpp','OptiScaler/nr/diagnostics/candidate/Observer.cpp',
          '/Fo:'+str(base/'obj')+'\\','/Fe:'+str(base/'out/idle.exe'),'/link','/LTCG'],'idle-build')
        if not code:code=run([str(base/'out/idle.exe'),str(capture)],'idle-test')
        sys.exit(code)
    elif mode=='hooks':
        from verify_candidate_package import clean
        clean(Path(sys.argv[2])) # startup capture intentionally refuses an existing session directory
        subprocess.run([sys.executable,str(ROOT/'tests/generate_candidate_hooks.py')],check=True)
        code=run(['cl','/nologo','/std:c++20','/EHsc','/W4','/WX','/IOptiScaler','/I'+str(OUT),
          'tests/nr_candidate_hooks.cpp','OptiScaler/nr/diagnostics/candidate/Observer.cpp','/Fe:'+str(OUT/'hooks.exe'),'/Fo:'+str(OUT)+'\\'],'hooks-build')
        if not code:code=run([str(OUT/'hooks.exe'),sys.argv[2]],'hooks-test')
        sys.exit(code)
    elif mode=='frame-trace':
        for source in ('nr_frame_trace','nr_frame_trace_runtime'):
            code=run(['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/DSPDLOG_USE_STD_FORMAT','/IOptiScaler','/Iexternal/spdlog/include',
                'tests/'+source+'.cpp','/Fe:'+str(OUT/(source+'.exe')),'/Fo:'+str(OUT)+'\\'],source+'-build')
            if code:sys.exit(code)
            for args in ([[]] if source=='nr_frame_trace' else [[m] for m in ('off','invalid','invalid-trigger','invalid-profile','on','delayed','association')]):
                code=run([str(OUT/(source+'.exe'))]+args,source+'-'+(args[0] if args else 'test'))
                if code:sys.exit(code)
