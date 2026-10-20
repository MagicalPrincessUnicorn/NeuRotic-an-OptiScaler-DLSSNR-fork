"""Compare actual shipped DLL startup in an isolated, non-game process."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'builds/startup-probe'
EVIDENCE = ROOT / 'docs/implementation-evidence/startup-regression-20261006'
CASES = (
    ('previous', 'NeuRotic-Flagship-Installer-Anything-e1774af8-Full-Installer.zip', '4967f039b7bd0eb2de6b88350551725eb577b39a0cf0bea534de78b57450a43a'),
    ('audit', 'NeuRotic-Flagship-Audit-Fixes-0d586996-Full-Installer.zip', '6db683891608c28e8eecb9613bebcd72494fd45d6fd17aa03efee894fd204350'),
)
EVIDENCE.mkdir(parents=True, exist_ok=True)
results = []
for label, archive_name, expected in CASES:
    archive = ROOT.parent / '_exports' / archive_name
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected
    folder = WORK / label
    folder.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        prefix = 'NeuRotic/packages/current/payload/'
        for entry in z.infolist():
            if entry.is_dir() or not entry.filename.startswith(prefix):
                continue
            relative = entry.filename[len(prefix):]
            if relative.startswith('OptiScaler/CharacterInspector/'):
                continue
            assert ':' not in relative and '..' not in Path(relative).parts
            path = folder / ('winmm.dll' if relative == 'OptiScaler.dll' else relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(z.read(entry))
    shutil.copyfile(WORK / 'StartupProbe.exe', folder / 'StartupProbe.exe')
    ini = folder / 'OptiScaler.ini'
    text = ini.read_text(encoding='utf-8-sig').replace('LogToFile=false', 'LogToFile=true').replace('LogLevel=auto', 'LogLevel=0')
    ini.write_text(text, encoding='utf-8')
    try:
        result = subprocess.run([str(folder / 'StartupProbe.exe'), str(folder / 'winmm.dll')],
                                cwd=folder, capture_output=True, timeout=25,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        receipt = dict(case=label, exit_code=result.returncode,
                       stdout=result.stdout.decode(errors='replace'), stderr=result.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as exc:
        receipt = dict(case=label, timeout=True, stdout=(exc.stdout or b'').decode(errors='replace'), stderr=(exc.stderr or b'').decode(errors='replace'))
    for log in folder.glob('*.log'):
        shutil.copyfile(log, EVIDENCE / (label + '-' + log.name))
    results.append(receipt)
    print(json.dumps(receipt), flush=True)
(EVIDENCE / 'initial-startup.json').write_text(json.dumps(results, indent=2) + '\n')
