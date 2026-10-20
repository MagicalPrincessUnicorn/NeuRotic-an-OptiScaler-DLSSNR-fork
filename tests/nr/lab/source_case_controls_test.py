"""Exercise the standalone source controls through their actual process API."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

EXE=Path(os.environ.get('NR_SOURCE_CASE_EXE','C:/NeuRotic/builds/source-case-controls-20260926/SourceCaseControls.exe'))

class SourceCaseControlsTests(unittest.TestCase):
    def run_case(self,case):
        self.assertTrue(EXE.is_file(),'source case control executable must be built')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'observations.json'
            result=subprocess.run([str(EXE),'--case',case,'--output',str(output)],capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            value=json.loads(output.read_text());self.assertEqual(value['case_id'],case)
            self.assertTrue(value['success']);self.assertTrue(value['checks']);self.assertTrue(all(c['passed'] for c in value['checks']))
            self.assertEqual(value['scope'],'CPU_SOURCE_ONLY');self.assertEqual(value['catalog_coverage'],'PARTIAL')
            self.assertEqual(value['case_status'],'PASS_SCOPED');self.assertTrue(value['covered_scope']);self.assertTrue(value['unproved'])
            before=output.read_bytes()
            rerun=subprocess.run([str(EXE),'--case',case,'--output',str(output)],capture_output=True,timeout=30)
            self.assertNotEqual(rerun.returncode,0);self.assertEqual(output.read_bytes(),before)
            return value

    def test_signal_regions_bounds_mips_and_real_padded_row_conversion(self):
        value=self.run_case('NLAB-SIG-005')
        checks={c['name'] for c in value['checks']}
        self.assertTrue({'active-offset-retained','zero-width-rejected','overflowing-subrect-rejected','mip-index-at-count-rejected','array-slice-at-count-rejected','padded-row-preserves-visible-pixels'}<=checks)

    def test_real_basic_projection_preserves_zero_three_and_advanced(self):
        value=self.run_case('NLAB-MUL-001')
        self.assertEqual(value['observations']['basic_pass_counts'],[0,1,2,3])
        self.assertEqual(value['observations']['three_pass_model_strengths'],[1,1,0.25])
        self.assertEqual(value['observations']['three_pass_detail_strengths'],[1,0.5,0])
        self.assertTrue({'default-disabled-stays-disabled','advanced-retains-per-pass-intent','zero-strength-no-extra-planned-passes'}<={c['name'] for c in value['checks']})

    def test_real_capability_refusal_keeps_independent_supported_lane(self):
        value=self.run_case('NLAB-API-004')
        self.assertTrue({'missing-shared-resources-refused','missing-synchronization-refused','missing-direct-queue-refused','independent-d3d12-lane-still-supported','default-capabilities-refused'}<={c['name'] for c in value['checks']})
        self.assertIn('Windows Vulkan results do not qualify Linux or Proton',value['unproved'])

    def test_unknown_case_does_not_publish_success(self):
        self.assertTrue(EXE.is_file(),'source case control executable must be built')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'invalid.json'
            run=subprocess.run([str(EXE),'--case','NLAB-UNKNOWN','--output',str(output)],capture_output=True,timeout=30)
            self.assertNotEqual(run.returncode,0);self.assertFalse(output.exists())

if __name__=='__main__':unittest.main()
