"""Pinned W03 regression/build driver; receipts never imply runtime promotion.

Run under the bundled Python. The VS environment is loaded explicitly. Existing
supervised suites retain their own process containment and exact-row verifier.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import uuid
from owned_process import run as owned_run, developer_environment_script

ROOT = Path(__file__).resolve().parents[3]
VS = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community')
PS = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
SUITES = [
    'contracts/Run-NrContracts.ps1', 'contracts/Run-NrTest002.ps1',
    'identity/Run-NrIdentity.ps1', 'feed/Run-NrFeed.ps1', 'context/Run-NrContext.ps1',
    'representations/Run-NrRepresentations.ps1', 'lifecycle/Run-NrConsumption.ps1',
    'protocol/Run-NrProtocol.ps1', 'orchestration/Run-NrRouting.ps1',
    'orchestration/Run-NrOrchInit.ps1', 'finalizer/Run-NrFinalizer.ps1',
]
WIRING = ['spectre/encode_wiring.py', 'spectre/source_owner_wiring.py',
          'spectre/process_source_wiring.py', 'spectre/renderer_preparation_wiring.py',
          'vertical-slice/native_config_wiring.py', 'vertical-slice/native_renderer_wiring.py']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--group', choices=['supervised', 'build', 'python', 'all'], default='all')
    parser.add_argument('--only', help='one supervised script stem')
    args = parser.parse_args()
    label = 'w03-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
    run = ROOT / 'x64' / label
    run.mkdir(parents=True)
    print('RUN=' + str(run), flush=True)
    env = {k.upper(): v for k, v in os.environ.items()}
    setup = run / 'environment.cmd'
    setup.write_text(developer_environment_script(VS / 'VC/Auxiliary/Build/vcvarsall.bat'))
    result = owned_run(['cmd.exe', '/d', '/c', str(setup)], capture_output=True, text=True, env=env, timeout=60)
    save(run/'environment-result.json', {'command': result.args, 'exit_code': result.returncode,
        'stdout': result.stdout, 'stderr': result.stderr, 'supervision': result.supervision, 'build_child_overrides': {'VSTEL_EWDK':'1','CL':None,'_CL_':None}})
    if result.returncode:
        raise RuntimeError('INFRA_ERROR: pinned developer environment failed: ' + result.stderr)
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    if env.get('VCTOOLSVERSION', '').rstrip('\\/') != '14.44.35207':
        raise RuntimeError('INFRA_ERROR: wrong selected compiler')
    env['VSTEL_EWDK'] = '1'
    env.pop('CL', None)
    env.pop('_CL_', None)
    env['PYTHONDONTWRITEBYTECODE'] = '1'

    git_probes=[]
    def git(*arguments):
        command=['git','-c','safe.directory='+ROOT.as_posix(),*arguments]
        result=owned_run(command,cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8',errors='strict',timeout=30)
        git_probes.append(dict(command=command,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr,
                               supervision=result.supervision,infrastructure_error=result.infrastructure_error))
        save(run/'git-probes.json',git_probes)
        if result.infrastructure_error or result.returncode!=0:
            raise RuntimeError('INFRA_ERROR: owned git source probe failed')
        return result.stdout.strip()

    def sources():
        names = git('ls-files', '--cached', '--others', '--exclude-standard').splitlines()
        return {name: sha(ROOT / name) for name in names if (ROOT / name).is_file() and
                (name.startswith('OptiScaler/') or name.startswith('tests/')) and
                (ROOT / name).suffix.lower() in {'.h', '.hpp', '.cpp', '.c', '.py', '.ps1', '.json', '.vcxproj', '.props', '.targets'}}

    before = sources()
    identity = {'commit': git('rev-parse', 'HEAD'), 'tree': git('rev-parse', 'HEAD^{tree}'),
                'branch': git('branch', '--show-current'), 'status': git('status', '--porcelain=v2'),
                'source_files': before}
    save(run / 'source-before.json', identity)
    commands = []
    receipt = {'run': str(run), 'toolset': 'v145', 'compiler': '14.44.35207',
               'environment': {k: env.get(k) for k in ['VCTOOLSVERSION', 'VCTOOLSINSTALLDIR', 'VISUALSTUDIOVERSION', 'WINDOWSSDKVERSION', 'VSCMD_ARG_TGT_ARCH']},
               'source_manifest_sha256': sha(run / 'source-before.json'), 'commands': commands, 'git_probes': git_probes,
               'provider': 'NOT_RUN', 'game': 'NOT_RUN', 'iq': 'NOT_RUN', 'performance': 'NOT_RUN', 'G2': 'NOT_PERFORMED'}

    def execute(name, argv, timeout=3600):
        log = run / (name + '.log')
        item = {'name': name, 'command': list(map(str, argv)), 'cwd': str(ROOT), 'log': str(log),
                'started': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        print('START ' + name, flush=True)
        with log.open('wb') as output:
            try:
                result = owned_run(argv, cwd=ROOT, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, timeout=timeout)
                item['exit_code'] = result.returncode
                item['supervision'] = result.supervision
                if result.infrastructure_error:
                    item['INFRA_ERROR'] = 'owned process supervision failed'
            except (OSError, subprocess.TimeoutExpired) as error:
                item['exit_code'] = None
                item['INFRA_ERROR'] = str(error)
        item['ended'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        item['log_sha256'] = sha(log)
        commands.append(item)
        save(run / 'receipt.json', receipt)
        print('END ' + name + ': ' + str(item['exit_code']), flush=True)
        return item

    if args.group in {'supervised', 'all'}:
        external = ROOT.parent / 'builds' / label
        external.mkdir(parents=True)
        for relative in SUITES:
            script = ROOT / 'tests/nr' / relative
            if args.only and script.stem != args.only:
                continue
            argv = [str(PS), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script),
                    '-OutputDirectory', str(external / script.stem), '-TimeoutSeconds', '1800']
            parameters = script.read_text(encoding='utf-8-sig').split('Set-StrictMode', 1)[0]
            if '$MaintenanceLockPath' in parameters:
                argv += ['-MaintenanceLockPath', str(external / 'maintenance.lock')]
            if '$PythonExecutable' in parameters:
                argv += ['-PythonExecutable', sys.executable]
            execute(script.stem, argv)
        receipt['supervised_root'] = str(external)
    if args.group in {'python', 'all'}:
        for relative in WIRING:
            execute(Path(relative).stem, [sys.executable, '-B', str(ROOT / 'tests/nr' / relative)])
        execute('evaluator-unittests', [sys.executable, '-B', '-m', 'unittest', 'discover', '-s',
                                      str(ROOT / 'tests/nr/vertical-slice'), '-p', 'test_*.py', '-v'])
    if args.group in {'build', 'all'}:
        msbuild = VS / 'MSBuild/Current/Bin/amd64/MSBuild.exe'
        for name, relative in [('OptiScaler', 'OptiScaler/OptiScaler.vcxproj'),
                               ('dlssnr_forwarder', 'OptiScaler/dlssnr/forwarder/dlssnr_forwarder.vcxproj')]:
            project_run = run / name
            out = project_run / 'out'
            obj = project_run / 'obj'
            out.mkdir(parents=True)
            obj.mkdir()
            argv = [str(msbuild), str(ROOT / relative), '/t:Rebuild', '/m:1', '/p:Configuration=Release',
                    '/p:Platform=x64', '/p:PlatformToolset=v145', '/p:VCToolsVersion=14.44.35207',
                    '/p:SolutionDir=' + str(ROOT) + '\\', '/p:OutDir=' + str(out) + '\\',
                    '/p:IntDir=' + str(obj) + '\\', '/p:PostBuildEventUseInBuild=false', '/v:minimal', '/nologo']
            item = execute(name, argv)
            text = Path(item['log']).read_text(encoding='utf-8', errors='replace')
            item['MSB8028_count'] = text.count('MSB8028')
            item['outputs'] = {str(p): sha(p) for p in out.iterdir() if p.is_file()}
            save(run / 'receipt.json', receipt)
    after = sources()
    save(run / 'source-after.json', after)
    receipt['source_files_unchanged'] = before == after
    receipt['all_commands_exit_zero'] = bool(commands) and all(c['exit_code'] == 0 and 'INFRA_ERROR' not in c for c in commands)
    save(run / 'receipt.json', receipt)
    print('RECEIPT=' + str(run / 'receipt.json'), flush=True)
    return 0 if receipt['all_commands_exit_zero'] and before == after else 1


if __name__ == '__main__':
    sys.exit(main())
