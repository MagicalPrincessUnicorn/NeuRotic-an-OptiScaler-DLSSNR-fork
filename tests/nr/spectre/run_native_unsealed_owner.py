"""Actual Native owner retirement with synthetic strict enrollment and WARP.

Runs the baseline and isolated early-close, exclusion, and history mutants. Source and
binary hashes bind each result. A compile/launch/timeout error is INFRA_ERROR,
never a killed mutant. This does not run or qualify an NVIDIA provider, model,
game, production renderer borrow, or strict source enrollment.
"""
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
CORE = ['nr/orchestration/StreamCoordinatorKernel.cpp',
        'nr/lifecycle/NativeSessionLifetime.cpp', 'nr/protocol/NativeRouteAdmission.cpp']
SCOPE = 'synthetic strict enrollment plus controlled WARP lifecycle; NVIDIA provider NOT_RUN'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    detectors={'early_close_invocation':'receipt production leaves provider pending and teardown refused',
        'missing_recording_exclusion':'one feature history excludes a second invocation with distinct allocations',
        'fresh_invocation_history':'next invocation retains reset debt and stable feature history identity',
        'missing_allocation_exclusion':'different feature cannot reserve allocations held by an older recording'}
    case_names=['baseline','early_close_invocation','missing_recording_exclusion','fresh_invocation_history','missing_allocation_exclusion']
    parser.add_argument('--case', choices=case_names)
    parser.add_argument('--vcvars', default=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat')
    args = parser.parse_args()
    run = ROOT / 'x64' / ('native-unsealed-owner-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    print('RUN=' + str(run), flush=True)
    receipts = []

    def save():
        (run / 'receipt.json').write_text(json.dumps(receipts, indent=2) + '\n', encoding='utf-8')

    try:
        env = {key.upper(): value for key, value in os.environ.items()}
        setup_script = run / 'environment.cmd'
        setup_script.write_text(developer_environment_script(args.vcvars))
        setup = owned_run(['cmd.exe', '/d', '/c', str(setup_script)], capture_output=True, text=True, timeout=60, env=env)
        (run/'environment-result.json').write_text(json.dumps(dict(command=setup.args, exit_code=setup.returncode,
            stdout=setup.stdout, stderr=setup.stderr, supervision=setup.supervision), indent=2))
        if setup.returncode:
            raise RuntimeError('developer environment unavailable: ' + setup.stderr)
        for line in setup.stdout.splitlines():
            if '=' in line and not line.startswith('='):
                key, value = line.split('=', 1)
                env[key.upper()] = value
        if env.get('VCTOOLSVERSION', '').strip('\\/') != '14.44.35207':
            raise RuntimeError('exact compiler unavailable')
        env.pop('CL', None)
        env.pop('_CL_', None)
        env['VSTEL_EWDK'] = '1'
        compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
        source = ROOT / 'tests/nr/spectre/native_unsealed_owner.cpp'
        source_hash = sha(source)
        cases=[args.case] if args.case else case_names
        for name in cases:
            mutate=name!='baseline'
            directory = run / name
            directory.mkdir()
            include = directory / 'OptiScaler'
            hashes = {}
            # All cases use exactly the baseline's immutable NR source graph.
            # This is source copying, not wildcard selection of tests to execute.
            graph = run / 'baseline/OptiScaler' if mutate and not args.case else ROOT / 'OptiScaler'
            for path in [path for folder in ('nr','dlssnr') for path in (graph / folder).rglob('*')]:
                if path.is_file() and path.suffix in ('.h', '.cpp'):
                    relative = path.relative_to(graph)
                    destination = include / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    data = path.read_bytes()
                    hashes[relative.as_posix()] = hashlib.sha256(data).hexdigest()
                    destination.write_bytes(data)
            local_source=directory / 'test.cpp'
            def bind_include(match):
                path=(source.parent / match[1]).resolve()
                if path==ROOT / 'OptiScaler/dlssnr/NrGpuSafety.cpp':
                    path=include / 'dlssnr/NrGpuSafety.cpp'
                return '#include "'+path.as_posix()+'"'
            local_source.write_text(re.sub(r'#include "([^"]+)"',bind_include,source.read_text()))
            header = include / 'nr/lifecycle/NativeInvocationOwner.h'
            if name=='early_close_invocation':
                before = 'if(!providerOwner_->Complete(providerUse_))return false;'
                text = header.read_text()
                if text.count(before) != 1:
                    raise RuntimeError('early-close mutation anchor is not unique')
                header.write_text(text.replace(before, 'if(!providerOwner_->Complete(providerUse_)||!providerOwner_->CloseInvocation(providerUse_))return false;'))
            elif name in ('missing_recording_exclusion','fresh_invocation_history'):
                owner=include / 'dlssnr/NativeProviderLifetime.h'
                before,after=('closing_||recordingUse_||!pending_.empty()','closing_||!pending_.empty()') if name=='missing_recording_exclusion' else ('if(history_)\n','if(history_&&firstHistory.value==0)\n')
                content=owner.read_text()
                if content.count(before)!=1:raise RuntimeError(name+' mutation anchor is not unique')
                owner.write_text(content.replace(before,after))
            elif name=='missing_allocation_exclusion':
                owner=include / 'dlssnr/NativeProviderLifetime.h'
                before='if(retainedUse)\n'
                content=owner.read_text()
                if content.count(before)!=1:raise RuntimeError(name+' mutation anchor is not unique')
                owner.write_text(content.replace(before,'if(retainedUse&&retainedUse->closed_)\n'))
            command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/Y-', '/UNDEBUG',
                       '/I' + str(include), '/I' + str(ROOT / 'OptiScaler'), '/I' + str(ROOT / 'OptiScaler/include'), str(local_source)]
            command += [str(include / path) for path in CORE]
            command += ['/Fe' + str(directory / 'test.exe'), '/Fo' + str(directory) + '\\', '/link',
                        '/LIBPATH:' + str(ROOT / 'OptiScaler/library/detours'), 'd3d12.lib', 'dxgi.lib', 'ole32.lib', 'detours.lib', '/STACK:8388608']
            receipt = {'case': name, 'compile_command': command, 'cwd': str(directory), 'compiler_sha256': sha(compiler),
                       'source_sha256': source_hash, 'source_graph_sha256': hashes, 'implementation_sha256': sha(header),
                       'provider_implementation_sha256': sha(include / 'dlssnr/NativeProviderLifetime.h'),
                       'runner_sha256': sha(Path(__file__)),
                       'compile_timeout_seconds': 180, 'test_timeout_seconds': 60, 'result': 'INFRA_ERROR', 'scope': SCOPE}
            receipts.append(receipt)
            save()
            print(name + ' compile', flush=True)
            compiled = owned_run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=180)
            (directory / 'compile.log').write_text(compiled.stdout + compiled.stderr)
            receipt.update(compile_exit=compiled.returncode, compile_log_sha256=sha(directory / 'compile.log'), compile_supervision=compiled.supervision)
            if compiled.returncode == 0:
                receipt['test_command'] = [str(directory / 'test.exe')]
                tested = owned_run(receipt['test_command'], cwd=directory, env=env, capture_output=True, text=True, timeout=60)
                output = tested.stdout + tested.stderr
                (directory / 'test.log').write_text(output)
                receipt.update(test_exit=tested.returncode, output=output, binary_sha256=sha(directory / 'test.exe'),
                               test_supervision=tested.supervision,
                               test_log_sha256=sha(directory / 'test.log'))
                failed=[line[5:] for line in tested.stderr.splitlines() if line.startswith('FAIL ')]
                summaries=re.findall(r'^Native unsealed owner: (\d+) checks, (\d+) failures;',tested.stdout,re.M)
                receipt.update(expected_detector=detectors.get(name),failed_assertions=failed,
                    positive_control='THIS_RUN' if not mutate else 'PASS' if any(r['case']=='baseline' and r['result']=='PASS' for r in receipts) else 'NOT_RUN')
                valid=False
                if len(summaries)==1:
                    checks,failures=map(int,summaries[0]);receipt['summary']={'checks':checks,'failures':failures}
                    valid=checks>0 and 0<=failures<=checks and failures==len(failed) and (tested.returncode==0)==(failures==0)
                receipt['assertion_protocol_valid']=valid
                if valid and not tested.infrastructure_error and tested.returncode in (0,1) and 'INFRA_ERROR' not in output:
                    receipt['result']=('SURVIVED' if tested.returncode==0 else 'KILLED' if detectors[name] in failed else 'FAIL') if mutate else ('PASS' if tested.returncode==0 else 'FAIL')
            receipt['test_source_unchanged'] = source_hash == sha(source)
            if not receipt['test_source_unchanged']:
                receipt['result'] = 'INFRA_ERROR'
            save()
            print(name + ' ' + receipt['result'], flush=True)
            if receipt['result'] == 'INFRA_ERROR':
                break
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        receipts.append({'case': 'runner', 'result': 'INFRA_ERROR', 'exception': repr(error), 'scope': SCOPE})
        save()
        print('INFRA_ERROR ' + repr(error), flush=True)
    print('RECEIPT=' + str(run / 'receipt.json'), flush=True)
    return int(len(receipts) != (1 if args.case else len(case_names)) or any(item['result'] not in ('PASS', 'KILLED') for item in receipts))


if __name__ == '__main__':
    raise SystemExit(main())
