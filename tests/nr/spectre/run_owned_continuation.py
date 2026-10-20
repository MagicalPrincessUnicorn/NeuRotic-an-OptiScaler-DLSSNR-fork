"""Bounded controlled WARP copy test. No model loading, product DLL or game execution."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import re
import shutil
from owned_process import run as owned_run, developer_environment_script
import uuid

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--vcvars', default=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat')
parser.add_argument('--hardware', action='store_true', help='Use the first enumerated D3D12 hardware adapter; never fall back to WARP')
parser.add_argument('--mutation', choices=['omit_copy', 'ignore_capacity', 'ignore_source_version', 'retain_terminal', 'mutable_snapshot', 'wrong_wait_value', 'wrong_wait_fence', 'wrong_wait_queue', 'ignore_wait_failure', 'retain_cross_queue', 'identity_target_ignored', 'path_unknown_completion', 'path_stale_version', 'retain_private_resource', 'consume_refused_owner'], help='Isolated safe semantic mutant; compile failure is never a kill')
parser.add_argument('--output-parent', type=Path, help='Exact fresh external run directory below C:/NeuRotic/builds')
args = parser.parse_args()
run = args.output_parent.resolve() if args.output_parent else ROOT / 'x64' / ('owned-continuation-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+uuid.uuid4().hex[:8])
if args.output_parent:
    external = Path('C:/NeuRotic/builds').resolve()
    if not run.is_relative_to(external) or run == external or run.exists():
        raise SystemExit('INFRA_ERROR: output-parent must be a fresh child below C:/NeuRotic/builds')
run.mkdir(parents=True, exist_ok=False)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def snapshot():
    return {p.relative_to(ROOT).as_posix():sha(p) for base in ('OptiScaler','tests/nr')
            for p in (ROOT/base).rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.hpp','.c','.cpp','.py','.ps1','.json')}

receipt = dict(result='INFRA_ERROR', stages=[], mutation=args.mutation,
               positive_control='NOT_RUN' if args.mutation else 'THIS_RUN', campaign_complete=False,
               evidence_layer='HARDWARE_CONTROLLED_COPY' if args.hardware else 'WARP_CONTROLLED_COPY')
def persist():
    (run/'receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')

def invoke(label, command, environment, timeout):
    executable = Path(shutil.which(str(command[0]),path=environment.get('PATH')) or command[0]).resolve()
    stage=dict(label=label, command=list(map(str,command)), cwd=str(ROOT), timeout_seconds=timeout,
               executable=str(executable), executable_sha256_before=sha(executable) if executable.is_file() else None)
    receipt['stages'].append(stage); persist()
    (run/(label+'.intent.json')).write_text(json.dumps(stage,indent=2),encoding='utf-8')
    out_path=run/(label+'.stdout.log'); err_path=run/(label+'.stderr.log')
    with out_path.open('xb') as output, err_path.open('xb') as error:
        result=owned_run(command,cwd=ROOT,env=environment,stdout=output,stderr=error,timeout=timeout)
    result.stdout=out_path.read_text(errors='replace'); result.stderr=err_path.read_text(errors='replace')
    stage.update(returncode=result.returncode, supervision=result.supervision,
                 infrastructure_error=result.infrastructure_error, stdout_path=str(out_path),stderr_path=str(err_path),
                 stdout_sha256=sha(out_path),stderr_sha256=sha(err_path),
                 executable_sha256_after=sha(executable) if executable.is_file() else None)
    if stage['executable_sha256_before']!=stage['executable_sha256_after']:
        stage['infrastructure_error']=True; result.infrastructure_error=True; result.returncode=125
    (run/(label+'.result.json')).write_text(json.dumps(stage,indent=2),encoding='utf-8'); persist()
    return result

before=snapshot()
(run/'source-before.json').write_text(json.dumps(before,sort_keys=True,indent=2),encoding='utf-8')
receipt['source_before_sha256']=sha(run/'source-before.json')
receipt['source_before_path']=str(run/'source-before.json')
receipt['runner_sha256']=sha(__file__)
receipt['process_helper_sha256']=sha(Path(__file__).with_name('owned_process.py'))
persist()
env = {k.upper(): v for k, v in os.environ.items()}
script = run/'environment.cmd'; script.write_text(developer_environment_script(args.vcvars))
setup = invoke('setup',['cmd.exe','/d','/c',str(script)],env,60)
if setup.returncode or setup.infrastructure_error:
    receipt['phase']='developer_environment';persist()
    print('RECEIPT='+str(run/'receipt.json'),flush=True)
    raise SystemExit('INFRA_ERROR: developer environment unavailable')
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key.upper()]=value
if env.get('VCTOOLSVERSION','').strip('\\/')!='14.44.35207':
    receipt['phase']='exact_toolchain';persist()
    raise SystemExit('INFRA_ERROR: exact MSVC version unavailable')
env.pop('CL',None);env.pop('_CL_',None);env['VSTEL_EWDK']='1'
receipt['child_environment_overrides']={'VSTEL_EWDK':'1','CL':None,'_CL_':None}
compiler=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
receipt['compiler_sha256']=sha(compiler)
source = ROOT / 'tests/nr/spectre/owned_continuation.cpp'
header = ROOT/'OptiScaler/nr/lifecycle/NativeOwnedContinuation.h'
gpu_source = ROOT/'OptiScaler/dlssnr/NrGpuSafety.cpp'
compile_source=source
overlay_arguments=[]
if args.mutation:
    changes={
        'omit_copy': ('s.list->CopyResource(to,from);', '/* isolated mutation: omitted copy */'),
        'ignore_capacity': ('s.count==s.capacity||', 'false||'),
        'ignore_source_version': ('!Fresh(request.source,request.sourceVersion)||', 'false||'),
        'identity_target_ignored': ('return request.source==request.destination&&request.sourceVersion==request.destinationVersion?Result::Identity:Result::Refused;', 'return Result::Identity;'),
        'path_unknown_completion': ('return terminal&&(terminal->state==', 'return !terminal||(terminal->state=='),
        'path_stale_version': ('!Fresh(index,expected)||!RecordedTicket(state_->uploadTicket)', 'false||!RecordedTicket(state_->uploadTicket)'),
        'retain_private_resource': ('registryHold=s.owners->Resources().DetachPrivateAllocation(this,s.textures[index].Get(),expected,s.uses[index]);',
            'if(false)registryHold=s.owners->Resources().DetachPrivateAllocation(this,s.textures[index].Get(),expected,s.uses[index]);'),
        'consume_refused_owner': ('std::unique_ptr<NativeOwnerSet>&& owners,UINT width', 'std::unique_ptr<NativeOwnerSet> owners,UINT width'),
        'retain_terminal': ('slot.pending&&Terminal(*slot.pending)', 'false&&Terminal(*slot.pending)'),
        'retain_cross_queue': ('slot.pending&&Terminal(*slot.pending)', 'slot.pending&&!slot.pending->producer.ticket&&Terminal(*slot.pending)'),
        'mutable_snapshot': ('std::optional<C::ResourceView> View(std::size_t index)const noexcept\n    {std::lock_guard lock(mutex_);return state_&&index<=state_->capacity&&state_->textures[index]?std::optional{state_->views[index]}:std::nullopt;}',
            'const C::ResourceView* View(std::size_t index)const noexcept\n    {std::lock_guard lock(mutex_);return state_&&index<=state_->capacity&&state_->textures[index]?&state_->views[index]:nullptr;}'),
    }
    gpu_changes={
        'wrong_wait_value': ('const auto result = consumer->Wait(point.timeline->fence.Get(), point.value);',
            'const auto result = consumer->Wait(point.timeline->fence.Get(), 0);'),
        'wrong_wait_fence': ('const auto result = consumer->Wait(point.timeline->fence.Get(), point.value);',
            'ComPtr<ID3D12Fence> substituted; if(FAILED(producerDevice->CreateFence(point.value,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&substituted))))return false; const auto result = consumer->Wait(substituted.Get(), point.value);'),
        'wrong_wait_queue': ('const auto result = consumer->Wait(point.timeline->fence.Get(), point.value);',
            'ComPtr<ID3D12CommandQueue> substituted; auto description=consumer->GetDesc(); if(FAILED(consumerDevice->CreateCommandQueue(&description,IID_PPV_ARGS(&substituted))))return false; static std::vector<ComPtr<ID3D12CommandQueue>> held; held.push_back(substituted); const auto result = substituted->Wait(point.timeline->fence.Get(), point.value);'),
        'ignore_wait_failure': ('return SUCCEEDED(result);\n}\nbool BindExternalWait', 'return true;\n}\nbool BindExternalWait'),
    }
    on_gpu=args.mutation in gpu_changes
    mutation_before,mutation_after=(gpu_changes if on_gpu else changes)[args.mutation]
    original=(gpu_source if on_gpu else header).read_text()
    if original.count(mutation_before)!=1:
        raise SystemExit('INFRA_ERROR: mutation target is not unique')
    overlay=run/('overlay/NrGpuSafety.cpp' if on_gpu else 'overlay/nr/lifecycle/NativeOwnedContinuation.h')
    overlay.parent.mkdir(parents=True)
    mutated=original.replace(mutation_before,mutation_after)
    if on_gpu:
        # Keep a single physical #pragma-once header identity even though the
        # implementation lives in an isolated mutant directory.
        mutated=mutated.replace('"NrGpuSafety.h"','"'+gpu_source.with_suffix('.h').as_posix()+'"')
        compile_source=run/'test.cpp'
        compile_source.write_text(source.read_text().replace('"../../../OptiScaler/dlssnr/NrGpuSafety.cpp"', '"'+overlay.as_posix()+'"'))
    else:
        overlay_arguments=['/I'+str(run/'overlay'), '/I'+str(ROOT/'OptiScaler/nr/lifecycle')]
    overlay.write_text(mutated)
command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/Y-', '/UNDEBUG',
           *overlay_arguments,
           '/I'+str(ROOT/'OptiScaler'), '/I'+str(ROOT/'OptiScaler/include'), str(compile_source),
           '/Fe'+str(run/'test.exe'), '/Fo'+str(run/'test.obj'), '/link',
           '/LIBPATH:'+str(ROOT/'OptiScaler/library/detours'), 'd3d12.lib', 'dxgi.lib', 'ole32.lib', 'detours.lib', '/STACK:8388608']
receipt.update(compile_command=command,toolchain=env['VCTOOLSVERSION'],timeout_seconds=60,
               source_sha256=sha(source),gpu_source_sha256=sha(gpu_source),compiler_source_sha256=sha(compile_source),
               implementation_sha256=sha(header) if header.exists() else None)
if args.mutation:receipt['mutation_sha256']=sha(overlay)
compiled=invoke('compile',command,env,1800)
(run/'compile.log').write_text(compiled.stdout+compiled.stderr)
receipt['compile_exit']=compiled.returncode
DETECTORS={
 'omit_copy':'real fence completion and footprint row pitches yield exact two-copy pixel equality',
 'ignore_capacity':'capacity refuses before destination content changes',
 'ignore_source_version':'unknown or stale source revision refuses',
 'retain_terminal':'actual terminal completion reclaims every bounded abandoned host',
 'mutable_snapshot':'published view snapshot stays immutable across later owned writes',
 'wrong_wait_value':'L07 sealed unique producer establishes actual second queue wait before completion',
 'wrong_wait_fence':'L07 sealed unique producer establishes actual second queue wait before completion',
 'wrong_wait_queue':'L07 every admitted producer read waits on exact owner signal fence value and consumer queue',
 'ignore_wait_failure':'L07 actual E_FAIL from exact consumer Wait refuses admission',
 'retain_cross_queue':'L07 every terminal ticket permits downstream root reclamation',
 'identity_target_ignored':'Identity refuses a distinct current destination without content or edge changes',
 'path_unknown_completion':'submitted recorded path refuses unknown completion behind actual GPU gate',
 'path_stale_version':'later actual output overwrite invalidates the exact old recorded path',
 'retain_private_resource':'independently terminal scratch retires while downstream physical output remains retained',
 'consume_refused_owner':'nonempty owner refusal preserves caller ownership current allocation and native use rights'}
EXPECTED_COUNTS={None:100,'omit_copy':52,'ignore_capacity':52,'ignore_source_version':52,'retain_terminal':52,
 'mutable_snapshot':52,'wrong_wait_value':57,'wrong_wait_fence':57,'wrong_wait_queue':72,
 'ignore_wait_failure':72,'retain_cross_queue':78,'identity_target_ignored':52,
 'path_unknown_completion':100,'path_stale_version':100,'retain_private_resource':100,'consume_refused_owner':100}
receipt['expected_detector']=DETECTORS.get(args.mutation)
receipt['expected_check_count']=EXPECTED_COUNTS[args.mutation]
if compiled.returncode==0 and not compiled.infrastructure_error:
    receipt['binary_sha256']=sha(run/'test.exe')
    test_command=[str(run/'test.exe')]+(['--hardware'] if args.hardware else [])
    receipt['test_command']=test_command
    tested=invoke('test',test_command,env,60)
    output=tested.stdout+tested.stderr
    (run/'test.log').write_text(output)
    receipt.update(test_exit=tested.returncode,output=output)
    rows=[];protocol_error=False
    for line in tested.stdout.splitlines():
        if line.startswith('ASSERT '):
            try:
                value=json.loads(line[7:])
                if set(value)!= {'name','matched'} or not isinstance(value['name'],str) or type(value['matched']) is not bool:
                    raise ValueError('invalid assertion shape')
                rows.append(value)
            except (ValueError,TypeError):protocol_error=True
    summaries=re.findall(r'^owned-continuation checks=(\d+) failed=(\d+);',tested.stdout,re.M)
    failed=[line[5:] for line in tested.stderr.splitlines() if line.startswith('FAIL ')]
    receipt.update(assertions=rows,failed_assertions=failed,summary=None)
    valid=False
    if len(summaries)==1:
        checks,failures=map(int,summaries[0]);receipt['summary']={'checks':checks,'failures':failures}
        valid=(not protocol_error and checks==EXPECTED_COUNTS[args.mutation] and checks==len(rows) and
               failures==len(failed) and failed==[r['name'] for r in rows if not r['matched']] and
               len({r['name'] for r in rows})==len(rows) and (tested.returncode==0)==(failures==0))
    receipt['assertion_protocol_valid']=valid
    if not tested.infrastructure_error and 'INFRA_ERROR' not in output:
        if tested.returncode in (0,1) and valid:
            receipt['result']=('PASS' if tested.returncode==0 else 'FAIL') if not args.mutation else (
                'SURVIVED' if tested.returncode==0 else 'KILLED' if DETECTORS[args.mutation] in failed else 'FAIL')
        elif tested.returncode==3 and args.hardware and 'NOT_RUN' in output:
            receipt['result']='NOT_RUN'
after=snapshot()
(run/'source-after.json').write_text(json.dumps(after,sort_keys=True,indent=2),encoding='utf-8')
receipt.update(source_after_path=str(run/'source-after.json'),source_after_sha256=sha(run/'source-after.json'),
               source_unchanged=before==after,compiler_sha256_after=sha(compiler))
if before!=after or receipt['compiler_sha256']!=receipt['compiler_sha256_after']:
    receipt['result']='INFRA_ERROR';receipt['failure']='source or compiler changed during run'
persist()
print(receipt['result'],flush=True)
print('RECEIPT='+str(run/'receipt.json'),flush=True)
raise SystemExit(receipt['result'] not in ('PASS','KILLED'))
