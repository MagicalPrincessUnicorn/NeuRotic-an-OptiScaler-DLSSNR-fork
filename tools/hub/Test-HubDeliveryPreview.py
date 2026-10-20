"""Run the sealed desktop entry point with isolated data, then remove extraction."""
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'docs/implementation-evidence/hub-library-polish-20261003'
delivery = json.loads((EVIDENCE / 'delivery.json').read_text(encoding='utf-8-sig'))
archive = Path(delivery['path'])
assert hashlib.sha256(archive.read_bytes()).hexdigest() == delivery['sha256']
work = ROOT / 'builds/hub-delivery-preview'
preview = ROOT / 'builds/hub/preview-delivery-dark'
assert not work.exists() and not preview.exists(), 'Preserve existing test output'
for node in (work, preview, *work.parents, *preview.parents):
    if node.exists(): assert not (node.stat(follow_symlinks=False).st_file_attributes & 0x400)
work.mkdir()
try:
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            relative = entry.filename.removeprefix('NeuRotic/')
            if relative != 'NeuRotic.Hub.exe' and not relative.startswith(('support/', 'packages/current/', 'discovery/', 'Tools/WindowWorker/', 'models/', 'streamline/')): continue
            assert entry.filename.startswith('NeuRotic/') and ':' not in relative and '..' not in Path(relative).parts
            target = work / relative
            assert target.is_relative_to(work)
            if entry.is_dir(): continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.read(entry))
    env = os.environ.copy()
    env['NEUROTIC_HUB_FIXTURE_ROOT'] = str(work / 'user-data')
    env['PSMODULEPATH'] = str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/Modules')
    result = subprocess.run([str(work / 'NeuRotic.Hub.exe'), '--smoke-test', str(preview)],
                            cwd=work, env=env, capture_output=True, timeout=120,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    smoke = json.loads((preview / 'smoke.json').read_text())
    assert smoke['pagesRendered'] == 6 and smoke['brandEmbedded']
    assert not any(smoke[k] for k in ('proxyLoaded', 'ngxLoaded', 'captureStarted'))
    receipt = {'exitCode': result.returncode, 'archiveSha256': delivery['sha256'],
               'hubSha256': hashlib.sha256((work / 'NeuRotic.Hub.exe').read_bytes()).hexdigest(),
               'preview': str(preview), 'smoke': smoke}
    assert receipt['hubSha256'] == delivery['hub_sha256']
    (EVIDENCE / 'package-entry-preview.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
finally:
    assert work == ROOT / 'builds/hub-delivery-preview'
    for base, dirs, files in os.walk(work, followlinks=False):
        for name in dirs + files:
            assert not ((Path(base) / name).stat(follow_symlinks=False).st_file_attributes & 0x400)
    shutil.rmtree(work)
