"""Exercise the actual aggregate runner's failure branch with native exit codes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RunnerExitTests(unittest.TestCase):
    def test_nonzero_native_exit_stops_nested_suite(self):
        runner = ROOT / 'tests/Run-NrPresentMultipass.cmd'
        source = runner.read_text(encoding='utf-8')
        if len(sys.argv) > 1 and sys.argv[1] == '--baseline':
            source = subprocess.check_output(['git', 'show', 'e5687c97:tests/Run-NrPresentMultipass.cmd'], cwd=ROOT).decode()
        branch = next(line.strip() for line in source.splitlines() if line.strip().endswith('goto failed'))
        fixture_root = ROOT / 'builds/ui-release'
        with tempfile.TemporaryDirectory(dir=fixture_root, prefix='runner-exit-') as tmp:
            fixture = Path(tmp) / 'fixture.cmd'
            for code in (0, 1, -1073741819, -1073740791):
                fixture.write_text('@echo off\nsetlocal EnableDelayedExpansion\n'
                    'for %%T in (native) do (\n'
                    f' cmd /d /c exit /b {code}\n {branch}\n)\n'
                    'echo REACHED_NEXT_TEST\nexit /b 0\n:failed\nexit /b 1\n', encoding='utf-8')
                result = subprocess.run(['cmd', '/d', '/c', str(fixture)], capture_output=True, text=True)
                with self.subTest(code=code):
                    self.assertEqual(result.returncode, 0 if code == 0 else 1)
                    self.assertEqual('REACHED_NEXT_TEST' in result.stdout, code == 0)


if __name__ == '__main__':
    baseline = '--baseline' in sys.argv
    if baseline:
        # Keep the selection available to the test without unittest parsing it.
        unittest.main(argv=[sys.argv[0]], exit=True)
    else:
        unittest.main()
