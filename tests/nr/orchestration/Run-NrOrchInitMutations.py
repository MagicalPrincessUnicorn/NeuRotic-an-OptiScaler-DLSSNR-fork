"""Isolated semantic mutations; actual production sources, named C15 detectors.

Run under vcvars64. No production source/index mutation, no compile-failure kill.
The registry pins CRLF-to-LF canonical baseline, changed and patch bytes;
receipts retain raw provenance. Only isolated copies are normalized.
"""
import argparse,concurrent.futures,contextlib,ctypes,ctypes.wintypes,datetime
import hashlib,json,os,pathlib,shutil,sys,uuid

sys.dont_write_bytecode=True
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'spectre'))
from owned_process import run as owned_run

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def canonical(data):return data.replace(b'\r\n',b'\n')
def canonical_digest(path):return hashlib.sha256(canonical(path.read_bytes())).hexdigest()
def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8',newline='\n')
def inventory(directory):
    return {p.relative_to(directory).as_posix():digest(p) for p in sorted(directory.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'}

@contextlib.contextmanager
def maintenance(path):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    create=kernel.CreateFileW;create.restype=ctypes.wintypes.HANDLE
    create.argtypes=[ctypes.wintypes.LPCWSTR,ctypes.wintypes.DWORD,ctypes.wintypes.DWORD,ctypes.c_void_p,ctypes.wintypes.DWORD,ctypes.wintypes.DWORD,ctypes.wintypes.HANDLE]
    close=kernel.CloseHandle;close.argtypes=[ctypes.wintypes.HANDLE];close.restype=ctypes.wintypes.BOOL
    handle=create(str(path),0xC0000000,0,None,4,0x80,None)
    if handle==ctypes.wintypes.HANDLE(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    try:yield
    finally:
        if not close(handle):raise ctypes.WinError(ctypes.get_last_error())

def command(directory,label,argv,timeout=300):
    started=datetime.datetime.now(datetime.timezone.utc).isoformat()
    out=directory/(label+'.stdout');err=directory/(label+'.stderr')
    failure=None;supervision=None
    with out.open('wb') as stdout,err.open('wb') as stderr:
        try:
            result=owned_run(argv,cwd=directory,stdout=stdout,stderr=stderr,timeout=timeout)
            supervision=result.supervision
            code=result.returncode
            if result.infrastructure_error:
                code=None;failure='Owned process supervision failed: '+str(result.supervision)
        except OSError as exc:code=None;failure=str(exc)
    receipt={'argv':list(map(str,argv)),'cwd':str(directory),'started_utc':started,'ended_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'exit_code':code,'infrastructure_error':failure,'supervision':supervision,
             'stdout':{'path':str(out),'sha256':digest(out)},'stderr':{'path':str(err),'sha256':digest(err)}}
    save(directory/(label+'.command.json'),receipt)
    return code,out

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--maintenance-lock',type=pathlib.Path,required=True)
    parser.add_argument('--jobs',type=int,choices=[1,2],default=2)
    parser.add_argument('--ids',nargs='+',help='Rerun only named corrected mutations, with fresh controls')
    args=parser.parse_args();here=pathlib.Path(__file__).resolve().parent;repo=here.parents[2]
    output=args.output.resolve()
    if output==repo or repo in output.parents:raise ValueError('Mutation output must be outside source repository')
    if os.environ.get('CL') or os.environ.get('_CL_') or os.environ.get('VSCMD_ARG_TGT_ARCH')!='x64':raise ValueError('Clean vcvars64 environment required')
    if os.environ.get('VCToolsVersion','').strip('\\/')!='14.44.35207':raise ValueError('Exact MSVC 14.44.35207 required')
    # Bundled Python may restore its launch PATH. The explicit vcvars toolchain
    # directory survives; bind that exact compiler, not a different PATH tool.
    tool_directory=pathlib.Path(os.environ['VCToolsInstallDir'])/'bin/Hostx64/x64'
    cl=str(tool_directory/'cl.exe') if (tool_directory/'cl.exe').is_file() else None
    os.environ['PATH']=str(tool_directory)+os.pathsep+os.environ.get('PATH','')
    git=shutil.which('git.exe')
    if not cl or not git:raise ValueError('Compiler and Git required')
    registry=json.loads((here/'initial_mutations.json').read_text());run_id=uuid.uuid4().hex
    if args.ids:
        if set(args.ids)-{r['id'] for r in registry}:raise ValueError('Unknown mutation IDs')
        registry=[r for r in registry if r['id'] in args.ids]
    suites={'profile':('initial-profile-preparation','initial_profile_preparation.cpp'),
            'kernel':('initial-fixed-native-kernel','initial_fixed_native_kernel.cpp'),
            'admission':('initial-native-admission','initial_native_admission.cpp'),
            'races':('initial-native-races','initial_native_races.cpp'),
            'protocol':('initial-native-protocol','initial_native_protocol.cpp')}
    core=['nr/orchestration/StreamCoordinatorKernel.cpp','nr/lifecycle/NativeSessionLifetime.cpp','nr/protocol/NativeRouteAdmission.cpp']
    common=['/nologo','/std:c++20','/EHsc','/W4','/WX','/Y-','/UNDEBUG','/DNR_ORCH_INIT_TESTING']
    with maintenance(args.maintenance_lock):
        output.mkdir(parents=True,exist_ok=False)
        before={'nr':inventory(repo/'OptiScaler/nr'),'dlssnr':inventory(repo/'OptiScaler/dlssnr'),'tests':inventory(repo/'tests/nr')}
        code,git_before_path=command(output,'git-index-before',[git,'-C',str(repo),'ls-files','-s'],60)
        if code!=0:raise RuntimeError('Cannot capture source index')
        git_before=git_before_path.read_bytes()
        save(output/'sources-before.json',before)
        save(output/'run.json',{'run_id':run_id,'repository':str(repo),'compiler':{'path':cl,'sha256':digest(pathlib.Path(cl))},'registry_sha256':digest(here/'initial_mutations.json'),
              'baseline_objects':'Reused only for unchanged translation units. Any header mutation recompiles all three core TUs and its test.',
              'runner_sha256':digest(pathlib.Path(__file__)),'process_helper_sha256':digest(here.parent/'spectre/owned_process.py'),
              'normalization':'CRLF-to-LF only, isolated overlay and mechanical patch copies; source repository is never normalized',
              'ignored_generated_files':['__pycache__','*.pyc'],
              'production_attachment':'NOT_EXERCISED; synthetic test owner ports','gpu':'NOT_RUN','model_runtime':'NOT_RUN','gate_approval':'NOT_PERFORMED'})
        base=output/'control';base.mkdir()
        # Every baseline TU and suite is freshly compiled from the pinned live source.
        code,_=command(base,'core',[cl]+common+['/I'+str(repo/'OptiScaler'),'/c']+[str(repo/'OptiScaler'/p) for p in core]+['/Fo'+str(base)+'\\'])
        if code!=0:raise RuntimeError('Baseline core compile failed; no mutation adjudication')
        objects=[base/(pathlib.Path(p).stem+'.obj') for p in core]
        control={}
        for key,(suite,filename) in suites.items():
            if key not in {r['suite'] for r in registry}:continue
            exe=base/(key+'.exe');obj=base/(key+'.obj')
            code,_=command(base,key+'-compile',[cl]+common+['/I'+str(repo/'OptiScaler'),str(here/filename)]+list(map(str,objects))+['/Fo'+str(obj),'/Fe'+str(exe),'/link','/STACK:8388608'])
            if code!=0:raise RuntimeError('Baseline test compile failed: '+key)
            code,log=command(base,key+'-execute',[str(exe),run_id])
            framing,_=command(base,key+'-verify',[sys.executable,'-B',str(here/'verify_initial.py'),'--log',str(log),'--suite',suite,'--run-id',run_id])
            if code!=0 or framing!=0:raise RuntimeError('Baseline test failed: '+key)
            control[key]={'binary_sha256':digest(exe),'object_sha256':digest(obj),'result':'PASS'}
        save(output/'controls.json',control)
        def one(row):
            ident=row['id'];directory=output/ident;directory.mkdir();overlay=directory/'tree'
            result={'id':ident,'detector':row['detector'],'status':'INFRA_ERROR','control':control[row['suite']],'run_id':run_id}
            try:
                shutil.copytree(repo/'OptiScaler/nr',overlay/'OptiScaler/nr')
                # Only isolated copies are normalized. Preserve every raw source
                # and canonical identity in the per-case provenance receipt.
                provenance={}
                for path in sorted((overlay/'OptiScaler/nr').rglob('*')):
                    if path.is_file() and path.suffix in ('.h','.cpp'):
                        raw=path.read_bytes();normalized=canonical(raw)
                        provenance[path.relative_to(overlay).as_posix()]={
                            'raw_sha256':hashlib.sha256(raw).hexdigest(),
                            'canonical_sha256':hashlib.sha256(normalized).hexdigest()}
                        path.write_bytes(normalized)
                save(directory/'overlay-provenance.json',provenance)
                # Isolate Git's path prefix from the enclosing workspace-control
                # repository. Otherwise apply can silently skip prefixed paths.
                code,_=command(directory,'init',[git,'-C',str(overlay),'init','--quiet'])
                if code!=0:raise ValueError('Isolated mutation Git root unavailable')
                patch=here/row['patch']
                if row.get('hash_normalization')!='CRLF-to-LF-only':raise ValueError('Unknown registry hash domain')
                if canonical_digest(patch)!=row['patch_sha256']:raise ValueError('Canonical patch identity mismatch')
                applied_patch=directory/'mutation.canonical.patch'
                applied_patch.write_bytes(canonical(patch.read_bytes()))
                result['patch_provenance']={'source':str(patch),'raw_sha256':digest(patch),
                    'canonical_sha256':digest(applied_patch),'registry_refresh_raw_sha256':row['patch_raw_sha256']}
                for f in row['files']:
                    if digest(overlay/f['path'])!=f['before_sha256']:raise ValueError('Baseline drift: '+f['path'])
                # Git runs at the isolated overlay, never the source repository.
                for label,flags in [('parse',['--numstat']),('check',['-p2','--check','--whitespace=error-all']),('apply',['-p2','--whitespace=error-all'])]:
                    code,_=command(directory,label,[git,'-C',str(overlay),'-c','core.autocrlf=false','-c','core.eol=lf','apply']+flags+[str(applied_patch)],60)
                    if code!=0:raise ValueError('Strict patch '+label+' failed')
                for f in row['files']:
                    if digest(overlay/f['path'])!=f['after_sha256']:raise ValueError('Mutant bytes mismatch')
                save(directory/'source-delta.json',row)
                suite,filename=suites[row['suite']];exe=directory/'mutant.exe'
                includes=['/I'+str(overlay/'OptiScaler'),'/I'+str(repo/'OptiScaler')]
                if any(f['path'].endswith('.h') for f in row['files']):
                    argv=[cl]+common+includes+[str(here/filename)]+[str(overlay/'OptiScaler'/p) for p in core]+['/Fo'+str(directory)+'\\','/Fe'+str(exe),'/link','/STACK:8388608']
                    code,_=command(directory,'compile',argv)
                else:
                    changed={pathlib.Path(f['path']).stem for f in row['files']}
                    code,_=command(directory,'compile',[cl]+common+includes+['/c']+[str(overlay/f['path']) for f in row['files']]+['/Fo'+str(directory)+'\\'])
                    if code==0:
                        links=[directory/p.name if p.stem in changed else p for p in objects]+[base/(row['suite']+'.obj')]
                        code,_=command(directory,'link',[cl,'/nologo']+list(map(str,links))+['/Fe'+str(exe),'/link','/STACK:8388608'])
                if code!=0:raise ValueError('Unexpected compile/link failure; never a semantic kill')
                result['binary_sha256']=digest(exe)
                code,log=command(directory,'execute',[str(exe),run_id])
                if code not in (0,1):raise ValueError('Execution infrastructure failure; never a semantic kill or survivor')
                framing,_=command(directory,'verify',[sys.executable,'-B',str(here/'verify_initial.py'),'--log',str(log),'--suite',suite,'--run-id',run_id])
                if framing!=0:raise ValueError('Malformed/incomplete C15 evidence; never a semantic kill')
                rows=[json.loads(line) for line in log.read_text().splitlines() if line.startswith('{')]
                named=[r for r in rows if r.get('id')==row['detector'] and 'matched' in r]
                if len(named)!=1:raise ValueError('Named detector missing or duplicated')
                result['named_assertion']=named[0];result['exit_code']=code
                result['status']='KILLED' if code==1 and named[0]['matched'] is False else 'SURVIVED'
            except Exception as exc:result['error']=str(exc)
            save(directory/'result.json',result);print(ident+': '+result['status'],flush=True)
            return result
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:results=list(pool.map(one,registry))
        after={'nr':inventory(repo/'OptiScaler/nr'),'dlssnr':inventory(repo/'OptiScaler/dlssnr'),'tests':inventory(repo/'tests/nr')}
        save(output/'sources-after.json',after)
        code,git_after_path=command(output,'git-index-after',[git,'-C',str(repo),'ls-files','-s'],60)
        stable=code==0 and before==after and git_before==git_after_path.read_bytes()
        report={'run_id':run_id,'results':results,'sources_and_index_unchanged':stable,'gpu':'NOT_RUN','model_runtime':'NOT_RUN','gate_approval':'NOT_PERFORMED'}
        save(output/'results.json',report)
        if not stable:raise RuntimeError('Source/index changed during mutations')
        return 0 if all(r['status']=='KILLED' for r in results) else 1
if __name__=='__main__':sys.exit(main())
