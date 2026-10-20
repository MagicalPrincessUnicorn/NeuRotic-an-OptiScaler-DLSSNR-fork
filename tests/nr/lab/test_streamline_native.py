"""CPU-only tests: never pass provider initialization consent to a valid package."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[3]
EXE = Path(os.environ.get('STREAMLINE_SETUP_EXE', 'C:/NeuRotic/builds/lab-development/streamline-control-20260926/native/SetupProbe.exe'))
PACKAGE = Path(os.environ.get('STREAMLINE_SETUP_PACKAGE', 'C:/NeuRotic/builds/lab-development/w03-knowledge-20260926/canonical-streamline-control/package'))
MANIFEST = ROOT / 'tools/nr/lab/provider/canonical-manifest.json'


class NativeSetupTests(unittest.TestCase):
    def invoke(self, *, flags=(), package=PACKAGE, manifest=MANIFEST):
        self.assertTrue(EXE.is_file(), 'native setup worker has not been built')
        with tempfile.TemporaryDirectory(prefix='nr-setup-cpu-') as tmp:
            trace = Path(tmp) / 'trace.jsonl'
            nonce = uuid.uuid4().hex
            result = subprocess.run([str(EXE), '--package', str(package), '--manifest', str(manifest), '--trace', str(trace), '--nonce', nonce, *flags], capture_output=True, timeout=30)
            self.assertTrue(trace.is_file(), result.stderr.decode(errors='replace'))
            rows = [json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
        self.assertEqual([r['seq'] for r in rows], list(range(len(rows))))
        self.assertTrue(all(r['nonce'] == nonce and r['schema_version'] == 1 and r['pid'] > 0 and r['created_filetime'] > 0 for r in rows))
        self.assertFalse(any(r['event'] in ('load', 'init_enter', 'init_return') for r in rows))
        self.assertEqual(rows[-1]['event'], 'terminal')
        self.assertFalse(rows[-1]['data']['control_pass'])
        self.assertEqual(rows[-1]['data']['evaluation'], 'NOT_IMPLEMENTED')
        return result, rows

    def test_missing_consent_refuses_before_loading(self):
        result, rows = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['setup_status'], 'REFUSED')
        self.assertEqual(rows[-1]['data']['reason'], 'PROVIDER_INIT_CONSENT_REQUIRED')

    def test_conflicting_modes_refuse(self):
        result, rows = self.invoke(flags=('--verify-only', '--allow-provider-init'))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['reason'], 'CONFLICTING_MODES')

    def test_unpinned_manifest_refuses(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            manifest = Path(tmp) / 'bad.json'
            manifest.write_text('{"files":[]}', encoding='utf-8')
            result, rows = self.invoke(flags=('--verify-only',), manifest=manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['reason'], 'MANIFEST_IDENTITY_MISMATCH')

    def test_missing_package_refuses(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            result, rows = self.invoke(flags=('--verify-only',), package=Path(tmp) / 'absent')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['setup_status'], 'REFUSED')

    def test_unexpected_member_refuses(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            (Path(tmp) / 'unexpected.dll').write_bytes(b'not executable')
            result, rows = self.invoke(flags=('--verify-only',), package=Path(tmp))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['reason'], 'PACKAGE_MEMBER_SET_MISMATCH')

    def test_tampered_member_refuses_without_loading(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            # All expected names, deliberately incorrect bytes. Never make a valid test DLL.
            for member in json.loads(MANIFEST.read_text())['files']:
                (Path(tmp) / Path(member['path']).name).write_bytes(b'tampered')
            result, rows = self.invoke(flags=('--verify-only',), package=Path(tmp))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['reason'], 'PACKAGE_MEMBER_IDENTITY_MISMATCH')

    def test_hardlinked_manifest_refuses(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            manifest = Path(tmp) / 'manifest.json'
            # Copy first so this test never changes canonical manifest link count.
            manifest.write_bytes(MANIFEST.read_bytes())
            os.link(manifest, Path(tmp) / 'alias.json')
            result, rows = self.invoke(flags=('--verify-only',), manifest=manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(rows[-1]['data']['reason'], 'HARD_LINK_REFUSED')

    def test_trace_inside_package_is_never_created(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            package = Path(tmp)
            trace = package / 'must-not-write.jsonl'
            result = subprocess.run([str(EXE), '--package', str(package), '--manifest', str(MANIFEST), '--trace', str(trace), '--nonce', uuid.uuid4().hex, '--verify-only'], capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(trace.exists(), 'worker modified the package before refusing')
            self.assertIn(b'TRACE_INSIDE_PACKAGE_REFUSED', result.stderr)

    def test_trailing_separator_cannot_bypass_package_trace_guard(self):
        with tempfile.TemporaryDirectory(dir=EXE.parent) as tmp:
            trace = Path(tmp) / 'must-not-write.jsonl'
            result = subprocess.run([str(EXE), '--package', tmp + '\\', '--manifest', str(MANIFEST), '--trace', str(trace), '--nonce', uuid.uuid4().hex, '--verify-only'], capture_output=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(trace.exists(), 'trailing separator bypassed package write protection')
            self.assertIn(b'TRACE_INSIDE_PACKAGE_REFUSED', result.stderr)

    def test_verify_only_observes_bytes_without_loading(self):
        result, rows = self.invoke(flags=('--verify-only',))
        self.assertEqual(result.returncode, 0, rows)
        self.assertTrue(next(r for r in rows if r['event'] == 'preflight')['data']['verified'])
        self.assertTrue(rows[-1]['data']['verification_only'])
        snapshots = [r['data'] for r in rows if r['event'] == 'snapshot']
        self.assertEqual([s['phase'] for s in snapshots], ['before_load'])
        self.assertTrue(snapshots[0]['modules'])
        self.assertEqual(snapshots[0]['errors'], [])
        self.assertFalse(any(Path(m['mapped_path']).name.lower().startswith(('sl.', 'nvngx_dlss')) for m in snapshots[0]['modules']))


if __name__ == '__main__':
    unittest.main()



