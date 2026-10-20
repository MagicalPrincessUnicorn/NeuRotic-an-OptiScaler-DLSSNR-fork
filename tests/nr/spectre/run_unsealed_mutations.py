"""Actual finalizer/header graph mutants; compile failures are never kills."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import uuid
from owned_process import run as owned_run, developer_environment_script

ROOT = Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser()
parser.add_argument('--case', help='Run one exact mutation or baseline')
parser.add_argument('--suite', choices=['unsealed','parameter'], default='unsealed')
args=parser.parse_args()
run = ROOT / 'x64' / (args.suite+'-mutations-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
run.mkdir(parents=True)
env = {k.upper(): v for k, v in os.environ.items()}
setup = run / 'environment.cmd'
setup.write_text(developer_environment_script(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'))
result = owned_run(['cmd.exe', '/d', '/c', str(setup)], env=env, capture_output=True, text=True, timeout=60)
(run/'environment-result.json').write_text(json.dumps(dict(command=result.args, exit_code=result.returncode,
    stdout=result.stdout, stderr=result.stderr, supervision=result.supervision), indent=2))
if result.returncode:
    raise SystemExit('INFRA_ERROR: developer environment')
for line in result.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value
env['VSTEL_EWDK'] = '1'
env.pop('CL',None); env.pop('_CL_',None)
compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
if env.get('VCTOOLSVERSION', '').strip('\\/') != '14.44.35207':
    raise SystemExit('INFRA_ERROR: exact compiler unavailable')
finalizer = 'nr/lifecycle/FinalRealFrameFinalizer.h'
runtime = 'nr/lifecycle/FinalRealFrameRuntime.h'
cases = [
    ('baseline', None, '', '', None),
    ('wrong_recipe', finalizer, 'terminal.recipe_!=e->request.recipe||', '', 'F09.exact-recipe'),
    ('wrong_sample', finalizer, '*sample!=terminal.sample_', 'false', 'F09.feature-recreation'),
    ('prepared_hold_ignored', finalizer, 'if(e->binding)\n', 'if(e->binding&&e->scope.base.value==0)\n', 'F09.prepared-hold-not-covered'),
    ('lost_registration', finalizer, 'if(matches!=1)return Reject(FinalizationReason::InvalidRights);', '', 'F09.no-lost-registration'),
    ('prefix_ack_lost', finalizer, '||(&entries_[i]!=&e&&!entries_[i].packet&&!entries_[i].releasedClaim)', '', 'F09.prefix-keeps-unacknowledged'),
    ('claim_drain_ignored', finalizer, '||!e.packet||e.handoff!=HandoffPhase::Released;', '||(e.packet&&e.handoff!=HandoffPhase::Released);', 'F09.unacknowledged-claim-not-drained'),
    ('wrong_exclusion_owner', finalizer, 'registry.owner!=C::OwnerDomain::IdentityRegistry||', '', 'F09.wrong-exclusion-owner'),
    ('recording_binding_ignored', runtime, '&&a.sameRecording==b.sameRecording', '', 'F09.binding-recording-exact'),
]
if args.suite=='parameter':
    facts='nr/protocol/NativeEvaluationResult.h'
    observer='nr/protocol/NativeTemporalRuntime.h'
    override='dlssnr/NativeParameterOverride.h'
    cases=[('baseline',None,'','',None),
        ('lost_outer_refusal',facts,'return !facts.outputRestoreFailed;','return facts.failure.Empty()||!facts.failure.Empty();','uncertain Output blocks outer SR'),
        ('lost_sticky_failure',observer,'facts_.outputRestoreFailed=true;','facts_.outputRestoreFailed=false;','restoration failure retained after earlier failure'),
        ('restore_unverified',override,'restored_=Matches(original_);','restored_=true;','silent restoration failure explicit'),
        ('stale_original',override,'!Ready()||!replacement||!Matches(original_)','!Ready()||!replacement','recheck original at mutation')]
if args.case:
    cases=[case for case in cases if case[0]==args.case]
    if not cases:raise SystemExit('INFRA_ERROR: unknown mutation')
source = ROOT / ('tests/nr/finalizer/unsealed_retirement.cpp' if args.suite=='unsealed' else 'tests/nr/spectre/native_parameter_restore.cpp')
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
graph = {str(path.relative_to(ROOT / 'OptiScaler')): path.read_bytes()
         for path in (ROOT / 'OptiScaler/nr').rglob('*.h')}
if args.suite=='parameter':
    graph['dlssnr/NativeParameterOverride.h']=(ROOT/'OptiScaler/dlssnr/NativeParameterOverride.h').read_bytes()
receipts = []
for name, target, before, after, detector in cases:
    directory = run / name
    include = directory / 'OptiScaler'
    for relative, data in graph.items():
        path = include / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    if target:
        path = include / target
        text = path.read_text()
        if text.count(before) != 1:
            raise SystemExit('INFRA_ERROR: mutation target is not unique: ' + name)
        path.write_text(text.replace(before, after))
    command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/Y-', '/UNDEBUG',
               '/I' + str(include), '/I' + str(ROOT / 'OptiScaler'), str(source),
               '/Fe' + str(directory / 'test.exe'), '/Fo' + str(directory / 'test.obj'), '/link', '/STACK:8388608']
    receipt = {'case': name, 'compile_command': command, 'source_sha256': source_hash, 'result': 'INFRA_ERROR',
               'detector': detector, 'toolchain': env['VCTOOLSVERSION'],
               'header_hashes': {relative: hashlib.sha256((include / relative).read_bytes()).hexdigest() for relative in graph}}
    receipts.append(receipt)
    (run / 'receipt.json').write_text(json.dumps(receipts, indent=2))
    print('START ' + name, flush=True)
    compiled = owned_run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
    (directory / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    receipt['compile_exit'] = compiled.returncode
    receipt['compile_supervision'] = compiled.supervision
    if compiled.returncode == 0:
        test_command = [str(directory / 'test.exe'), uuid.uuid4().hex]
        executed = owned_run(test_command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        (directory / 'test.log').write_text(executed.stdout + executed.stderr)
        receipt.update(test_command=test_command, exit_code=executed.returncode,
                       test_supervision=executed.supervision,
                       binary_sha256=hashlib.sha256((directory / 'test.exe').read_bytes()).hexdigest())
        rows = [json.loads(line) for line in executed.stdout.splitlines() if line.startswith('{')]
        summaries = [row for row in rows if row.get('kind') == 'suite-summary']
        failed = [row['id'] for row in rows if row.get('kind') == 'assertion' and not row.get('matched')]
        if args.suite=='parameter':
            matches=re.findall(r'^checks=(\d+) failures=(\d+)$',executed.stdout,re.M)
            summaries=[{'checks':int(a),'failures':int(b),'unbound':0} for a,b in matches]
            failed=[line[5:] for line in executed.stderr.splitlines() if line.startswith('FAIL ')]
        receipt['failed_assertions'] = failed
        if executed.returncode in (0,1) and not executed.infrastructure_error and len(summaries) == 1 and summaries[0]['unbound'] == 0:
            receipt['summary'] = summaries[0]
            if name == 'baseline':
                receipt['result'] = 'PASS' if executed.returncode == 0 and not failed else 'FAIL'
            elif executed.returncode == 1 and detector in failed:
                receipt['result'] = 'KILLED'
            elif executed.returncode == 0:
                receipt['result'] = 'SURVIVED'
    (run / 'receipt.json').write_text(json.dumps(receipts, indent=2))
    print(name + ' ' + receipt['result'], flush=True)
    if receipt['result']=='INFRA_ERROR':break
print('RECEIPT=' + str(run / 'receipt.json'), flush=True)
raise SystemExit(any(item['result'] not in ('PASS', 'KILLED') for item in receipts))
