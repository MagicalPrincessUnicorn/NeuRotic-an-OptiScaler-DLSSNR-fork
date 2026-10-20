"""Measure disposable workspace output and remove this completed run's fixtures."""
import json
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = ROOT.parent
EVIDENCE = ROOT / 'docs/implementation-evidence/hub-library-polish-20261003'
DISPOSABLE = {'.obj', '.pch', '.ipch', '.ilk', '.idb', '.iobj', '.ipdb'}

def measure(root):
    objects = generated = 0
    for base, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = [d for d in dirs if d not in {'.git', 'external', 'references', 'node_modules'}
                   and not (os.stat(Path(base) / d, follow_symlinks=False).st_file_attributes & 0x400)]
        for name in files:
            path = Path(base) / name
            try:
                stat = path.stat(follow_symlinks=False)
                if stat.st_file_attributes & 0x400: continue
                if path.suffix.lower() in DISPOSABLE: objects += stat.st_size
                if 'builds' in path.parts: generated += stat.st_size
            except OSError: pass
    return {'disposable_bytes': objects, 'generated_bytes': generated}

def validate(path):
    assert path.is_absolute() and path.is_relative_to(ROOT / 'builds') and path != ROOT / 'builds'
    for node in (path, *path.parents):
        if node.exists(): assert not (node.stat(follow_symlinks=False).st_file_attributes & 0x400), str(node)
    if path.is_dir():
        for base, dirs, files in os.walk(path, followlinks=False):
            for name in dirs + files:
                node = Path(base) / name
                assert not (node.stat(follow_symlinks=False).st_file_attributes & 0x400), str(node)

before = measure(WORKSPACE)
removed = []
for relative in ('builds/hub/obj/KeybindRuntimeTests.obj', 'builds/hub/obj/KeybindUiTests.obj',
                 'builds/hub-polish/numeric-tests.obj', 'builds/hub/link-delete-check',
                 'builds/hub/native-selftest-fixture', 'builds/hub/profile-cache-fixture',
                 'builds/hub/profile-tests', 'builds/hub/smoke-polish-isolated'):
    path = ROOT / relative
    if not path.exists(): continue
    validate(path)
    size = sum(p.stat().st_size for p in path.rglob('*') if p.is_file()) if path.is_dir() else path.stat().st_size
    if path.is_dir(): shutil.rmtree(path)
    else: path.unlink()
    removed.append({'path': relative, 'bytes': size})
result = {'before_workspace': before, 'after_workspace': measure(WORKSPACE),
          'after_hub_build': measure(ROOT / 'builds/hub'), 'removed': removed,
          'removed_bytes': sum(r['bytes'] for r in removed),
          'retained': 'One stable Hub native and discovery compiler cache, final binaries/symbols, previews and unique logs; other chats active output untouched.',
          'note': 'Workspace counts include concurrent builds. Initial storage-before.json workspace disposable total is comparable; its old own_* fields referred to another checkout.'}
(EVIDENCE / 'storage-after.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
