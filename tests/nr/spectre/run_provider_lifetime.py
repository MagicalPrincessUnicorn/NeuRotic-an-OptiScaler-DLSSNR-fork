"""Controlled WARP lifecycle checks and isolated source mutants; no NGX loading.

Run with the proven VS developer environment or --vcvars pointing at vcvarsall.bat.
Artifacts are isolated below x64/. Compilation failure never kills a mutant.
"""
from pathlib import Path
import argparse
import datetime
import json
import hashlib
from owned_process import run as owned_run, developer_environment_script
import os
import re
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--mutations', action='store_true')
parser.add_argument('--case', help='Run one named case (baseline or mutant)')
parser.add_argument('--vcvars', default=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat')
parser.add_argument('--output-parent', type=Path, help='Fresh child directory under C:/NeuRotic/builds for an actual producer invocation')
args = parser.parse_args()
if args.output_parent is not None:
    run=args.output_parent.resolve()
    external=Path('C:/NeuRotic/builds').resolve()
    if os.name!='nt' or run==external or not run.is_relative_to(external) or run.exists():
        raise SystemExit('INFRA_ERROR: fresh C:/NeuRotic/builds child required')
else:
    run = ROOT / 'x64' / ('provider-lifetime-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+uuid.uuid4().hex[:8])
run.mkdir(parents=True)
environment = {k.upper(): v for k, v in os.environ.items()}
setup_script=run / 'environment.cmd'
setup_script.write_text(developer_environment_script(args.vcvars))
setup = owned_run(['cmd.exe', '/d', '/c', str(setup_script)], cwd=ROOT,
                  capture_output=True, text=True, env=environment, timeout=60)
(run/'environment-result.json').write_text(json.dumps(dict(command=setup.args,exit_code=setup.returncode,
    stdout=setup.stdout,stderr=setup.stderr,supervision=setup.supervision),indent=2))
if setup.returncode or setup.infrastructure_error:
    raise SystemExit('INFRA_ERROR: developer environment unavailable')
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        environment[key.upper()] = value
if environment.get('VCTOOLSVERSION','').strip('\\/')!='14.44.35207':
    raise SystemExit('INFRA_ERROR: exact compiler unavailable')
(run/'environment.json').write_text(json.dumps({key:environment.get(key,'') for key in
    ('VCTOOLSVERSION','VSCMD_ARG_TGT_ARCH','VCTOOLSINSTALLDIR','VSINSTALLDIR','WINDOWSSDKDIR')},indent=2))
environment.pop('CL',None);environment.pop('_CL_',None);environment['VSTEL_EWDK']='1'
compiler = Path(environment['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
owner = 'OptiScaler/dlssnr/NativeProviderLifetime.h'
gpu = 'OptiScaler/dlssnr/NrGpuSafety.cpp'
lease = 'OptiScaler/nr/lifecycle/ConsumptionLeaseAdapter.h'
cases = [('baseline', None, [])]
if args.mutations:
    cases += [
        ('unavailable_contract_accepted', owner, [('!contract_.Supported()||closing_||bindings.empty()', 'closing_||bindings.empty()')]),
        ('gpu_hold_hidden', owner, [('if(!Owns(use)||(use->state_!=State::Entered&&use->state_!=State::Returned))return false;', 'if(true)return false;')]),
        ('cpu_borrow_ignored', owner, [('if(use->state_==State::Returned)\n', 'if(use->state_==State::Returned||use->state_==State::Entered)\n')]),
        ('error_return_cancels_gpu', owner, [('if(Owns(use)&&use->state_==State::Entered)use->state_=State::Returned;',
            'if(Owns(use)&&use->state_==State::Entered){use->state_=State::Cancelled;use->resources_.clear();Remove(use);}')]),
        ('replayable_receipt', gpu, [('||!ticket->sealed||\n       ticket->terminal==RecordingTerminalState::None', '')]),
        ('receipt_binding_ignored', owner, [('receipt->binding_==binding&&', '')]),
        ('receipt_authority_ignored', owner, [('return use&&receipt&&use->authority_==authority_&&', 'return use&&receipt&&'),
            ('&&receipt->authority_==authority_&&', '&&')]),
        ('registration_reused', owner, [('if(high&&(!SameIssuer(*high,b.registration)||b.registration.value<=high->value))return {};', '')]),
        ('feature_tail_ignored', owner, [('// Keep the feature use until', 'std::erase_if(pending_,[](const auto& use){return use->state_==State::Terminal;});\n        // Keep the feature use until')]),
        ('c03_reservation_unavailable', lease, [('if(f.providerReservation&&b.sameRecording->providerRegistration&&', 'if(false&&f.providerReservation&&b.sameRecording->providerRegistration&&')]),
        ('provider_capacity_bypassed', owner, [('if(count+bindings.size()>64)return {};', 'if(count+bindings.size()>65536)return {};')]),
    ]
if args.case:
    cases=[case for case in cases if case[0]==args.case]
    if not cases:raise SystemExit('INFRA_ERROR: unknown case')
receipts = []
DETECTORS={
 'unavailable_contract_accepted':'unsupported contract cancels before pool reservation',
 'gpu_hold_hidden':'error return closes CPU only',
 'cpu_borrow_ignored':'CPU borrow blocks terminal receipt',
 'error_return_cancels_gpu':'error return closes CPU only',
 'replayable_receipt':'completed replayable recording retained',
 'receipt_binding_ignored':'wrong registration rejected',
 'receipt_authority_ignored':'foreign receipt owner rejected',
 'registration_reused':'duplicate registration rejected',
 'feature_tail_ignored':'terminal recording issues complete receipt set',
 'c03_reservation_unavailable':'SP03.reserved-not-active',
 'provider_capacity_bypassed':'full unresolved provider table refuses before another recording'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def snapshot():
    return {p.relative_to(ROOT).as_posix():sha(p) for base in ('OptiScaler','tests/nr')
            for p in (ROOT/base).rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.hpp','.c','.cpp','.py','.ps1','.json')}
before=snapshot()
(run/'source-before.json').write_text(json.dumps(before,sort_keys=True,indent=2))
compiler_before=sha(compiler)

for name, target, changes in cases:
    directory = run / name
    include = directory / 'OptiScaler'
    (include / 'dlssnr').mkdir(parents=True)
    (include / 'nr/lifecycle').mkdir(parents=True)
    if target == lease:
        # Keep quote-relative includes on one physical copy; mixing an overlay
        # header with its original would defeat pragma once and duplicate types.
        # Context headers also quote-include lifecycle identities, so isolate
        # the complete small NR header graph, without binaries or build output.
        for header in (ROOT / 'OptiScaler/nr').rglob('*.h'):
            destination=include / header.relative_to(ROOT / 'OptiScaler')
            destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_text(header.read_text())
    for path in [owner, gpu, 'OptiScaler/dlssnr/NrGpuSafety.h', lease, 'OptiScaler/nr/lifecycle/SameRecordingUse.h']:
        text = (ROOT / path).read_text()
        if path == target:
            for mutation_before, mutation_after in changes:
                if text.count(mutation_before) != 1:
                    raise SystemExit(f'INFRA_ERROR: mutation {name} did not match exactly once')
                text = text.replace(mutation_before, mutation_after)
        (directory / path).write_text(text)
    source = ROOT / ('tests/nr/vertical-slice/native_same_recording.cpp' if target == lease else 'tests/nr/spectre/provider_lifetime.cpp')
    text = source.read_text()
    def absolute_include(match):
        path = (source.parent / match[1]).resolve()
        if path == ROOT / gpu:
            path = directory / gpu
        return '#include "' + path.as_posix() + '"'
    text = re.sub(r'#include "([^"]+)"', absolute_include, text)
    local_source = directory / 'test.cpp'
    local_source.write_text(text)
    executable = directory / 'test.exe'
    command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/Y-', '/UNDEBUG',
               '/I' + str(include), '/I' + str(ROOT / 'OptiScaler'), '/I' + str(ROOT / 'OptiScaler/include'),
               str(local_source), '/Fe' + str(executable), '/Fo' + str(directory / 'test.obj'), '/link',
               '/LIBPATH:' + str(ROOT / 'OptiScaler/library/detours'), 'd3d12.lib', 'dxgi.lib', 'ole32.lib', 'detours.lib', '/STACK:8388608']
    receipt = dict(case=name,compile_command=command,result='INFRA_ERROR',expected_detector=DETECTORS.get(name),
        source_sha256=sha(source),compiled_source_sha256=sha(local_source),compiler_sha256=compiler_before,
        positive_control='THIS_RUN' if name=='baseline' else 'PASS' if any(r['case']=='baseline' and r['result']=='PASS' for r in receipts) else 'NOT_RUN',
        campaign_complete=False)
    receipts.append(receipt)
    (run/'receipt.json').write_text(json.dumps(receipts,indent=2))
    compiled=owned_run(command,cwd=ROOT,env=environment,capture_output=True,text=True,timeout=1800)
    (directory/'compile.stdout.log').write_text(compiled.stdout)
    (directory/'compile.stderr.log').write_text(compiled.stderr)
    (directory/'compile.log').write_text(compiled.stdout+compiled.stderr)
    receipt.update(compile_exit=compiled.returncode,compile_supervision=compiled.supervision)
    if compiled.returncode==0 and not compiled.infrastructure_error:
        test_command=[str(executable)]+([uuid.uuid4().hex] if target==lease else [])
        tested=owned_run(test_command,cwd=ROOT,env=environment,capture_output=True,text=True,timeout=120)
        output=tested.stdout+tested.stderr
        (directory/'test.stdout.log').write_text(tested.stdout)
        (directory/'test.stderr.log').write_text(tested.stderr)
        (directory/'test.log').write_text(output)
        receipt.update(test_command=test_command,test_exit=tested.returncode,output=output,
                       test_supervision=tested.supervision,binary_sha256=sha(executable))
        failed=[line[5:] for line in tested.stderr.splitlines() if line.startswith('FAIL ')]
        valid=False
        if target==lease:
            try:rows=[json.loads(line) for line in tested.stdout.splitlines() if line.startswith('{')]
            except ValueError:rows=[]
            summaries=[row for row in rows if row.get('kind')=='suite-summary']
            assertions=[row for row in rows if row.get('kind')=='assertion']
            failed=[row['id'] for row in assertions if row.get('matched') is False]
            if len(summaries)==1:
                summary=summaries[0]
                count=summary.get('checks',summary.get('semantic_assertions'))
                failures=summary.get('failures',summary.get('mismatches'))
                valid=(type(count)is int and count>0 and type(failures)is int and failures==len(failed) and
                       count+summary.get('preconditions',0)==len(assertions) and summary.get('unbound')==0)
                receipt['summary']=summary
        else:
            summaries=re.findall(r'^Provider lifetime: (\d+) checks, (\d+) failures;',tested.stdout,re.M)
            if len(summaries)==1:
                checks,failures=map(int,summaries[0]);receipt['summary']={'checks':checks,'failures':failures}
                valid=checks>0 and 0<=failures<=checks and failures==len(failed)
        receipt.update(failed_assertions=failed,assertion_protocol_valid=valid)
        if valid and not tested.infrastructure_error and tested.returncode in (0,1) and 'INFRA_ERROR' not in output and (tested.returncode==0)==(not failed):
            if name=='baseline':receipt['result']='PASS' if tested.returncode==0 else 'FAIL'
            else:receipt['result']='SURVIVED' if tested.returncode==0 else 'KILLED' if DETECTORS[name] in failed else 'FAIL'
    (run/'receipt.json').write_text(json.dumps(receipts,indent=2))
    print(name,receipt['result'],flush=True)
    if receipt['result']=='INFRA_ERROR':break
after=snapshot()
(run/'source-after.json').write_text(json.dumps(after,sort_keys=True,indent=2))
for receipt in receipts:
    receipt['source_unchanged']=before==after
    receipt['compiler_sha256_after']=sha(compiler)
    if before!=after or compiler_before!=receipt['compiler_sha256_after']:
        receipt.update(result='INFRA_ERROR',failure='source or compiler changed during run')
    receipt['campaign_complete']=(len(receipts)==12 and all(r['result'] in ('PASS','KILLED') for r in receipts))
(run/'receipt.json').write_text(json.dumps(receipts,indent=2))
print('RECEIPT='+str(run/'receipt.json'),flush=True)
raise SystemExit(any(r['result'] not in ('PASS','KILLED') for r in receipts))
