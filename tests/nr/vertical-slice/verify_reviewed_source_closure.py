"""Read-only audit of the code-pinned source-review closure; never refresh pins."""
import hashlib
import json
import pathlib
import re
import subprocess

import gate_evaluator


ROOT = pathlib.Path(__file__).resolve().parents[3]


def audit():
    manifests = sorted(pathlib.Path(__file__).parent.glob('*_reviewed_source_closure.json'))
    errors = []
    if len(manifests) != 6:
        errors.append('expected all six reviewed-source manifests')
    pins = {}
    pending = []
    for path in manifests:
        raw = path.read_bytes()
        document = json.loads(raw)
        expected = hashlib.sha256(raw).hexdigest()
        # Independently require the literal digest to be registered by evaluator.
        if expected not in pathlib.Path(gate_evaluator.__file__).read_text():
            errors.append(f'{path.name}: digest is not code-registered')
        loaded = gate_evaluator._reviewed_source_closure(path.name, expected)
        if not loaded or loaded != document['source_sha256']:
            errors.append(f'{path.name}: review evidence does not authenticate')
        if pins and pins != loaded:
            errors.append(f'{path.name}: inconsistent union closure')
        pins = loaded
        pending.extend(ROOT / name for name in document['entry_points'])
    seen = set()
    while pending:
        path = pending.pop().resolve()
        if path in seen:
            continue
        seen.add(path)
        name = path.relative_to(ROOT).as_posix()
        if name not in pins:
            errors.append(f'{name}: missing include dependency')
        for include in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',
                                  path.read_text(), re.MULTILINE):
            found = next((candidate.resolve() for candidate in (
                path.parent / include, ROOT / 'OptiScaler' / include,
                ROOT / 'OptiScaler/include' / include) if candidate.is_file()), None)
            if found:
                pending.append(found)
    for name, expected in pins.items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            errors.append(f'{name}: raw bytes differ from reviewed source')
    if pins:
        result = subprocess.run(['git', 'check-attr', '--stdin', 'text'], cwd=ROOT,
            input=('\n'.join(pins) + '\n').encode('utf-8'), capture_output=True, check=True)
        for line in result.stdout.decode('utf-8').splitlines():
            if not line.endswith(': text: unset'):
                errors.append(f'{line}: raw-byte checkout preservation missing')
    return dict(schema_version='NeuRotic.ReviewedClosureAudit/1',
        manifests=len(manifests), pinned_files=len(pins), local_include_files=len(seen),
        errors=errors, status='FAIL' if errors else 'PASS',
        scope='Source-review provenance only; no product coverage or runtime qualification.')


if __name__ == '__main__':
    result = audit()
    print(json.dumps(result, indent=2))
    raise SystemExit(bool(result['errors']))
