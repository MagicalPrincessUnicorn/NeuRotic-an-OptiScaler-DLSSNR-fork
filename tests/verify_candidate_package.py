"""Stable, CPU-only package verification. Never installs or launches a real game."""
import argparse
import contextlib
import hashlib
import json
import os
import msvcrt
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'builds/candidate-observer'
STAGE = OUT / 'package-stage'
PYTHON = sys.executable
PS = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'

def clean(path):
    lexical = path.absolute()
    for parent in (lexical, *lexical.parents):
        if parent.exists() and (parent.is_symlink() or parent.is_junction() or os.stat(parent, follow_symlinks=False).st_file_attributes & 0x400):
            raise ValueError('Linked cleanup ancestor.')
    target = lexical.resolve()
    boundary = OUT.resolve()
    if not target.is_relative_to(boundary) or target == boundary or not target.is_relative_to(ROOT.parent.resolve()):
        raise ValueError('Cleanup leaves own verification output.')
    if target.exists():
        if target.is_symlink() or target.is_junction():
            raise ValueError('Linked cleanup root.')
        if target.is_dir():
            for directory, dirs, files in os.walk(target, followlinks=False):
                for name in dirs + files:
                    p = Path(directory) / name
                    if p.is_symlink() or p.is_junction():
                        raise ValueError('Linked cleanup member.')
            shutil.rmtree(target)
        else:
            target.unlink()

def run(args, name):
    # Avoid case-colliding PATH/Path in a Windows child environment.
    env = {}; seen = {}
    for key, value in os.environ.items():
        lower = key.lower()
        if lower not in seen:
            seen[lower] = key; env[key] = value
        elif lower == 'path' and len(value) > len(env[seen[lower]]):
            env[seen[lower]] = value
    # This driver runs Windows PowerShell 5.1, independent of the host shell's modules.
    for key in list(env):
        if key.lower() == 'psmodulepath':
            del env[key]
    env['PSModulePath'] = r'C:\Windows\System32\WindowsPowerShell\v1.0\Modules;C:\Program Files\WindowsPowerShell\Modules'
    log = OUT / (name + '.log')
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(args, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
    print(name, result.returncode, flush=True)
    if result.returncode:
        print(log.read_text(errors='replace')[-5000:])
        raise RuntimeError('Verification failed: ' + name)

@contextlib.contextmanager
def verification_lock():
    for parent in (OUT, *OUT.parents):
        if parent.exists() and os.stat(parent, follow_symlinks=False).st_file_attributes & 0x400:
            raise ValueError('Linked verification output.')
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / 'package-verification.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise RuntimeError('Package verification already active; its fixtures must not be cleaned.') from error
        try:
            yield
        finally:
            lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)

def verify(sealed=None, seed_trace=None, budget_trace=None):
    OUT.mkdir(parents=True, exist_ok=True)
    clean(STAGE)
    STAGE.mkdir()
    draft = sealed.resolve() if sealed else STAGE / 'candidate-package.zip'
    if sealed is None:
        run([PYTHON, 'tools/packaging/customer/Build-CandidateGamePackage.py', '--output', str(draft)], 'package-build')
    with zipfile.ZipFile(draft) as archive:
        prefix = archive.namelist()[0].split('/')[0]
        archive.extractall(STAGE / 'extracted')
    package = STAGE / 'extracted' / prefix
    seed_trace=seed_trace or OUT / 'hooks-final/candidate-dddddddddddddddddddddddddddddddd/trace.jsonl'
    run([PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/Test-CandidateCapture.ps1',
         '-PackageRoot', str(package), '-FixtureRoot', str(STAGE / 'launch-fixture'),
         '-SeedTrace', str(seed_trace)], 'package-launch-tests')
    run([PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/Test-CandidateSteamCapture.ps1',
         '-PackageRoot', str(package), '-FixtureRoot', str(STAGE / 'steam-fixture'),
         '-SeedTrace', str(seed_trace)], 'package-steam-tests')
    run([PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/Test-W03GameTestPackage.ps1',
         '-PackageRoot', str(package), '-FixtureRoot', str(STAGE / 'installer-fixture')], 'package-installer-tests')
    traces = list((package / 'captures').glob('pair-*/on/candidate-*/trace.jsonl'))
    traces = [p for p in traces if json.loads((p.parent.parent / 'run.json').read_text(encoding='utf-8-sig'))['capture_status'] == 'TRACE_FOOTER_PRESENT_PENDING_ANALYSIS']
    if len(traces) != 2:
        raise RuntimeError('Expected direct and Steam positive fixture traces.')
    report = STAGE / 'report'
    run([PYTHON, str(package / 'support/candidate_observer.py'), 'analyze', '--trace', str(traces[0]),
         '--out', str(report)], 'package-trace-analysis')
    summary = json.loads((report / 'report.json').read_text())
    second = STAGE / 'second-report'
    run([PYTHON, str(package / 'support/candidate_observer.py'), 'analyze', '--trace', str(traces[1]),
         '--out', str(second)], 'package-second-trace-analysis')
    second_summary = json.loads((second / 'report.json').read_text())
    if not summary['complete_transport'] or not second_summary['complete_transport']:
        raise RuntimeError('Positive fixture trace did not pass full transport validation.')
    budget_summary=None
    if budget_trace:
        budget_report=STAGE/'budget-report'
        run([PYTHON,str(package/'support/candidate_observer.py'),'analyze','--trace',str(budget_trace),
             '--out',str(budget_report)],'package-budget-trace-analysis')
        budget_summary=json.loads((budget_report/'report.json').read_text())
        if budget_summary['complete_transport'] or budget_summary['may_admit'] or budget_summary['budget_refusals']['reasons'][4]!={'reason':'window_quota_slots','count':'3','exact':True}:
            raise RuntimeError('Packaged analyzer lost precise budget diagnostics or qualification refusal.')
    # Preserve unique verification receipts, not installed payload copies or temporary fixtures.
    evidence = OUT / 'package-evidence'
    clean(evidence)
    evidence.mkdir()
    for path in (package / 'captures').glob('*.zip'):
        shutil.copyfile(path, evidence / path.name)
    shutil.copytree(report, evidence / 'report')
    shutil.copytree(second, evidence / 'second-report')
    if budget_trace:shutil.copytree(budget_report,evidence/'budget-report')
    log_names=['package-launch-tests','package-steam-tests','package-installer-tests','package-trace-analysis','package-second-trace-analysis']
    if sealed is None:log_names.insert(0,'package-build')
    if budget_trace:log_names.append('package-budget-trace-analysis')
    receipt = {'status': 'LOCAL_PACKAGE_AND_CPU_FIXTURES_PASS_GAME_NOT_RUN',
               'archive_sha256': hashlib.sha256(draft.read_bytes()).hexdigest(),
               'logs': [str(OUT / (n + '.log')) for n in log_names],
               'analyzed_report': summary, 'second_analyzed_report': second_summary, 'game_launched': False}
    if budget_trace:receipt['budget_report']=budget_summary
    (OUT / 'package-fixture-receipt.json').write_text(json.dumps(receipt, indent=2))
    clean(STAGE)
    print(receipt['status'])

def main(sealed=None, seed_trace=None, budget_trace=None):
    with verification_lock():
        verify(sealed, seed_trace, budget_trace)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='Verify an already sealed local package instead of building a draft')
    parser.add_argument('--seed-trace', type=Path, help='Use a fresh native CPU capture in launch fixtures')
    parser.add_argument('--budget-trace', type=Path, help='Check precise refusals through the sealed package analyzer')
    args=parser.parse_args();main(args.archive,args.seed_trace,args.budget_trace)
