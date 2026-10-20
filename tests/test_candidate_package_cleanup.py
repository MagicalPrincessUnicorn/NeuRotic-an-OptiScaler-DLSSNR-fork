"""Cleanup must refuse a junction ancestor without deleting its target."""
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_candidate_package as verifier

class CleanupTests(unittest.TestCase):
    def test_junction_ancestor_refused(self):
        original = verifier.OUT
        with tempfile.TemporaryDirectory(dir=original, prefix='cleanup-fixture-') as scratch:
            root = Path(scratch)
            real = root / 'real'
            real.mkdir()
            child = real / 'package-stage'
            child.mkdir()
            sentinel = child / 'preserve.txt'
            sentinel.write_text('preserve')
            link = root / 'linked-output'
            result = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(link), str(real)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            try:
                verifier.OUT = link
                with self.assertRaisesRegex(ValueError, 'ancestor'):
                    verifier.clean(link / 'package-stage')
                self.assertEqual(sentinel.read_text(), 'preserve')
            finally:
                verifier.OUT = original
                link.rmdir()  # remove the junction itself, never recurse into it

    def test_boundary_refused(self):
        with self.assertRaisesRegex(ValueError, 'leaves'):
            verifier.clean(verifier.OUT)

    def test_active_verification_lock_refused(self):
        with verifier.verification_lock():
            with self.assertRaisesRegex(RuntimeError, 'already active'):
                with verifier.verification_lock():
                    self.fail('Second verification entered the active fixture.')

if __name__ == '__main__':
    unittest.main()
