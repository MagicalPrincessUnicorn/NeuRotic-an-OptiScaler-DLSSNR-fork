"""Exercise real proxy forwarding and restricted DXGI loading in child processes."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dll', type=Path, help='Rebuilt candidate override; omit to test the exact archive DLL')
parser.add_argument('--archive', type=Path, default=ROOT.parent / '_exports/NeuRotic-Flagship-Audit-Fixes-0d586996-Full-Installer.zip')
parser.add_argument('--sha256', default='6db683891608c28e8eecb9613bebcd72494fd45d6fd17aa03efee894fd204350')
parser.add_argument('--receipt', type=Path, required=True)
parser.add_argument('--startup-repeats', type=int, default=100,
                    help='Repeat restricted DXGI startup per loader mode to exercise GPU discovery races')
args = parser.parse_args()
assert hashlib.sha256(args.archive.read_bytes()).hexdigest() == args.sha256
probe = ROOT / 'builds/startup-probe'
probe.mkdir(parents=True, exist_ok=True)
build = probe / 'build.cmd'
build.write_text(r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /MD tests\runtime\startup_host.cpp /Fo:builds\startup-probe\startup_host.obj /Fe:builds\startup-probe\StartupProbe.exe /link /PDBALTPATH:StartupProbe.pdb
exit /b %errorlevel%
''')
subprocess.run(['cmd.exe', '/d', '/c', str(build)], cwd=ROOT, check=True)
work = ROOT / 'builds/startup-probe/matrix'
work.mkdir(parents=True, exist_ok=True)
for node in (work, *work.parents):
    assert not node.stat(follow_symlinks=False).st_file_attributes & 0x400
with zipfile.ZipFile(args.archive) as archive:
    prefix = 'NeuRotic/packages/current/payload/'
    core = args.dll.read_bytes() if args.dll else archive.read(prefix + 'OptiScaler.dll')
    for entry in archive.infolist():
        if entry.is_dir() or not entry.filename.startswith(prefix):
            continue
        relative = entry.filename[len(prefix):]
        if relative == 'OptiScaler.dll' or relative.startswith('OptiScaler/CharacterInspector/'):
            continue
        assert ':' not in relative and '..' not in Path(relative).parts
        path = work / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(archive.read(entry))
shutil.copyfile(ROOT / 'builds/startup-probe/StartupProbe.exe', work / 'StartupProbe.exe')
ini = work / 'OptiScaler.ini'
baseline = ini.read_text(encoding='utf-8-sig')
def configured(ntdll):
    content = baseline
    for key, value in (('UseNtdllHooks', str(ntdll).lower()), ('CheckForUpdate', 'false')):
        content, count = re.subn(r'(?m)^' + key + r'\s*=.*$', key + '=' + value, content)
        assert count == 1, 'Missing or duplicated configuration key: ' + key
    return content
results = []
for ntdll in (True, False):
    for proxy in ('winmm.dll', 'version.dll', 'dbghelp.dll', 'wininet.dll', 'dxgi.dll'):
        path = work / proxy
        assert not path.exists(), 'Unexpected preexisting proxy fixture'
        path.write_bytes(core)
        try:
            operations = ['forward'] + ['restricted-dxgi'] * args.startup_repeats if proxy == 'winmm.dll' else ['forward']
            for iteration, operation in enumerate(operations):
                # The product may persist normalized settings during startup.
                # Reapply the requested mode before every independent process.
                ini.write_text(configured(ntdll), encoding='utf-8')
                command = [str(work / 'StartupProbe.exe'), str(path)]
                if operation == 'restricted-dxgi':
                    command.append('dxgi')
                try:
                    process = subprocess.run(command, cwd=work, capture_output=True, timeout=20,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
                    result = dict(proxy=proxy, ntdll=ntdll, operation=operation, iteration=iteration, exit_code=process.returncode,
                                  stdout=process.stdout.decode(errors='replace'), stderr=process.stderr.decode(errors='replace'))
                except subprocess.TimeoutExpired as error:
                    result = dict(proxy=proxy, ntdll=ntdll, operation=operation, timeout=True,
                                  stdout=(error.stdout or b'').decode(errors='replace'), stderr=(error.stderr or b'').decode(errors='replace'))
                results.append(result)
                if result.get('exit_code') != 0 or operation == 'forward' or iteration % 25 == 0:
                    print(('PASS' if result.get('exit_code') == 0 else 'FAIL'), proxy, 'ntdll='+str(ntdll), operation, iteration, flush=True)
        finally:
            assert hashlib.sha256(path.read_bytes()).digest() == hashlib.sha256(core).digest()
            path.unlink()
passed = all(row.get('exit_code') == 0 for row in results)
args.receipt.parent.mkdir(parents=True, exist_ok=True)
args.receipt.write_text(json.dumps(dict(status='PASS' if passed else 'FAIL',
    product_sha256=hashlib.sha256(core).hexdigest(), archive_sha256=args.sha256,
    exact_archive_dll=args.dll is None, game_executed=False, cases=results), indent=2) + '\n')
raise SystemExit(0 if passed else 1)
