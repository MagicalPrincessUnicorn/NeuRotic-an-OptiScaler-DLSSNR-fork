"""Real Windows process-containment canaries; no product/C15/G2 evidence."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import sys
import unittest
import uuid

try:
    import owned_process
except ImportError:
    owned_process = None

ROOT = Path(__file__).resolve().parents[3]
RUN = ROOT / 'x64' / ('process-supervision-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
RUN.mkdir(parents=True)
RECEIPTS = []


def observed(command, **kwargs):
    result = owned_process.run(command, capture_output=True, text=True, **kwargs)
    RECEIPTS.append(dict(command=list(map(str, command)), returncode=result.returncode,
                         stdout=result.stdout, stderr=result.stderr, supervision=result.supervision))
    return result


class OwnedProcessTests(unittest.TestCase):
    def test_regression_summary_requires_exact_positive_protocol(self):
        from run_spectre_regressions import summaries, summary_is_valid
        self.assertFalse(summary_is_valid(summaries('')))
        self.assertFalse(summary_is_valid(summaries('checks=0 failures=0')))
        self.assertFalse(summary_is_valid(summaries('checks=1 failures=0\nchecks=1 failures=0')))
        self.assertTrue(summary_is_valid(summaries('checks=1 failures=0')))

    def test_shared_supervisor_exists(self):
        self.assertIsNotNone(owned_process, 'direct runners require bounded whole-job process ownership')

    @unittest.skipUnless(owned_process is not None, 'implementation absent; existence assertion is RED')
    def test_normal_and_nonzero_capture(self):
        for code in (0, 7):
            result = observed([sys.executable, '-c', f'import sys;print("out");print("err",file=sys.stderr);sys.exit({code})'], timeout=5)
            self.assertEqual(result.returncode, code)
            self.assertEqual(result.stdout, 'out\n')
            self.assertEqual(result.stderr, 'err\n')
            self.assertFalse(result.infrastructure_error)
            self.assertEqual(result.supervision['supervisor_source_sha256'], owned_process._SOURCE_SHA256)
            self.assertTrue(result.supervision['group_assigned_before_resume'])
            self.assertTrue(result.supervision['parent_exit_observed'])
            self.assertTrue(result.supervision['group_empty_observed'])
            self.assertTrue(result.supervision['capture_complete'])

    @unittest.skipUnless(owned_process is not None, 'implementation absent')
    def test_nested_child_outlives_parent_and_closes_output(self):
        child = 'import sys,time;sys.stdout.close();sys.stderr.close();time.sleep(30)'
        parent = f'import subprocess,sys;subprocess.Popen([sys.executable,"-c",{child!r}]);print("parent-complete",flush=True)'
        result = observed([sys.executable, '-c', parent], timeout=.5)
        self.assertEqual(result.returncode, 124)
        self.assertTrue(result.infrastructure_error)
        self.assertTrue(result.supervision['timed_out'])
        self.assertTrue(result.supervision['termination_requested'])
        self.assertTrue(result.supervision['timeout_observation']['parent_exit_observed'])
        self.assertGreater(result.supervision['timeout_observation']['active_processes'], 0)
        self.assertTrue(result.supervision['group_empty_observed'])
        self.assertEqual(result.supervision['last_active_processes'], 0)
        self.assertEqual(result.supervision['cleanup_errors'], [])
        self.assertIn('parent-complete', result.stdout)

    @unittest.skipUnless(owned_process is not None, 'implementation absent')
    def test_timeout_preserves_partial_output_and_next_run_is_safe(self):
        result = observed([sys.executable, '-c', 'import time;print("partial",flush=True);time.sleep(30)'], timeout=.5)
        self.assertEqual(result.returncode, 124)
        self.assertIn('partial', result.stdout)
        self.assertTrue(result.supervision['parent_exit_observed'])
        self.assertTrue(result.supervision['group_empty_observed'])
        followup = observed([sys.executable, '-c', 'print("after-cleanup")'], timeout=5)
        self.assertEqual(followup.returncode, 0)

    @unittest.skipUnless(owned_process is not None, 'implementation absent')
    def test_launch_failure_is_infrastructure_and_logs_are_retained(self):
        result = observed([str(RUN / 'missing.exe')], timeout=1)
        self.assertEqual(result.returncode, 125)
        self.assertTrue(result.infrastructure_error)
        self.assertFalse(result.supervision['process_created'])
        self.assertTrue(result.supervision['errors'])

    @unittest.skipUnless(owned_process is not None, 'implementation absent')
    def test_file_capture_and_child_environment(self):
        original = os.environ.get('NR_PROCESS_CANARY')
        environment = dict(os.environ, NR_PROCESS_CANARY='child-only')
        with (RUN / 'combined.log').open('wb') as stream:
            result = owned_process.run([sys.executable, '-c', 'import os,sys;print(os.getenv("NR_PROCESS_CANARY"));print("err",file=sys.stderr)'],
                env=environment, stdout=stream, stderr=owned_process.STDOUT, timeout=5)
        RECEIPTS.append(dict(command=result.args, returncode=result.returncode, supervision=result.supervision))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(os.environ.get('NR_PROCESS_CANARY'), original)
        self.assertIn('child-only', (RUN / 'combined.log').read_text())
        self.assertIn('err', (RUN / 'combined.log').read_text())

    @unittest.skipUnless(owned_process is not None, 'implementation absent')
    def test_exact_msvc_compiler_descendants_finish_naturally(self):
        setup = RUN / 'environment.cmd'
        setup.write_text(owned_process.developer_environment_script(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'))
        selected = observed([str(Path(os.environ['SystemRoot']) / 'System32/cmd.exe'), '/d', '/c', str(setup)], timeout=60)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        environment = {key.upper(): value for key, value in os.environ.items()}
        for line in selected.stdout.splitlines():
            if '=' in line and not line.startswith('='):
                key, value = line.split('=', 1)
                environment[key.upper()] = value
        self.assertEqual(environment['VCTOOLSVERSION'].strip('\\/'), '14.44.35207')
        environment.pop('CL', None); environment.pop('_CL_', None)
        environment['VSTEL_EWDK'] = '1'
        compiler = Path(environment['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
        source = RUN / 'canary.cpp'; source.write_text('int main(){return 0;}\n')
        result = observed([str(compiler), '/nologo', '/EHsc', '/W4', '/WX', str(source), '/Fo'+str(RUN/'canary.obj'), '/Fe'+str(RUN/'canary.exe')],
                          env=environment, cwd=RUN, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(result.supervision['timed_out'])
        self.assertFalse(result.supervision['termination_requested'])
        self.assertTrue(result.supervision['group_empty_observed'])
        self.assertEqual(observed([str(RUN/'canary.exe')], timeout=5).returncode, 0)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OwnedProcessTests))
    sources = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in [Path(__file__), Path(__file__).with_name('owned_process.py')] if path.exists()}
    (RUN/'receipt.json').write_text(json.dumps(dict(tests=result.testsRun, failures=len(result.failures), errors=len(result.errors),
        skipped=len(result.skipped), result='PASS' if result.wasSuccessful() else 'FAIL', source_hashes=sources,
        commands=RECEIPTS, scope='process containment only; no product/C15/G2'), indent=2))
    print('RECEIPT='+str(RUN/'receipt.json'))
    raise SystemExit(not result.wasSuccessful())
