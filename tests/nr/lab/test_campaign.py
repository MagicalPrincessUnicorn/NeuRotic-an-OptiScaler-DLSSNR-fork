"""Behavioral controls for the portable combined diagnostic campaign."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[3] / 'tools/nr/lab/campaign.py'

class CampaignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('campaign', SOURCE)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_zero_exit_without_assertions_cannot_pass(self):
        self.assertEqual(self.module.source_result(0, '')['status'], 'EVIDENCE_INVALID')

    def test_redirected_windows_output_can_print_unicode_paths(self):
        import io
        target=io.BytesIO()
        stream=io.TextIOWrapper(target,encoding='cp1252')
        self.module.configure_output(stream)
        stream.write('REVIEW=C:/Extracted Ω Package/review.zip\n');stream.flush()
        self.assertEqual(target.getvalue().decode('utf-8').splitlines(),['REVIEW=C:/Extracted Ω Package/review.zip'])

    def test_owned_diagnostic_errors_are_failures_not_inconclusive(self):
        self.assertEqual(self.module.owned_result(3,True,'EVIDENCE_VALIDATED VALIDATION_ERRORS')['status'],'FAIL')
        self.assertTrue(self.module.owned_result(3,True,'EVIDENCE_VALIDATED VALIDATION_ERRORS')['gpu_uncertain'])
        incomplete=self.module.owned_result(3,True,'EVIDENCE_VALIDATED CAPTURE_INCOMPLETE')
        self.assertEqual(incomplete['status'],'INCONCLUSIVE')
        self.assertEqual(incomplete['diagnostic_status'],'CAPTURE_INCOMPLETE')
        self.assertFalse(incomplete['gpu_uncertain'])
        self.assertEqual(self.module.owned_result(0,True,'EVIDENCE_VALIDATED CAPTURE_INCOMPLETE')['status'],'EVIDENCE_INVALID')
        self.assertEqual(self.module.owned_result(0,True,'EVIDENCE_VALIDATED COMPLETE_NO_CAPTURED_ERRORS')['status'],'PASS')

    def test_runner_id_matches_accepted_source_contract(self):
        self.assertRegex(self.module.new_run_id(), r'^[0-9a-f]{32}$')
        self.assertNotEqual(self.module.new_run_id(), self.module.new_run_id())

    def test_scoped_control_requires_consistent_actual_assertions(self):
        row={'case_id':'NLAB-SIG-005','success':True,'scope':'CPU_SOURCE_ONLY',
             'catalog_coverage':'PARTIAL','case_status':'PASS_SCOPED',
             'checks':[{'name':'actual owner assertion','passed':True}],
             'covered_scope':['source helper'],'unproved':['physical rendering']}
        result=self.module.source_control_result(0,row,'NLAB-SIG-005')
        self.assertEqual(result['status'],'PASS')
        self.assertEqual(result['catalog_coverage'],'PARTIAL')
        for patch in ({'checks':[]},{'success':1},{'case_id':'NLAB-SIG-006'},
                      {'catalog_coverage':'COMPLETE'},{'unproved':[]},
                      {'checks':[{'name':'x','passed':1}]},
                      {'checks':[{'name':'x','passed':True},{'name':'x','passed':True}]},
                      {'checks':[{'name':'x','passed':False}]}):
            with self.subTest(patch=patch):
                self.assertEqual(self.module.source_control_result(0,{**row,**patch},'NLAB-SIG-005')['status'],'EVIDENCE_INVALID')
        failed={**row,'success':False,'case_status':'FAIL','checks':[{'name':'x','passed':False}]}
        self.assertEqual(self.module.source_control_result(1,failed,'NLAB-SIG-005')['status'],'FAIL')

    def test_contradictory_exit_and_assertion_counts_cannot_pass(self):
        self.assertEqual(self.module.source_result(0, '12 checks, 1 failures')['status'], 'FAIL')

    def test_duplicate_summaries_cannot_double_count(self):
        self.assertEqual(self.module.source_result(0, '12 checks, 0 failures\n12 checks, 0 failures')['status'], 'EVIDENCE_INVALID')

    def test_observed_source_summary_passes(self):
        self.assertEqual(self.module.source_result(0, '12 checks, 0 failures'),
                         {'status':'PASS', 'checks':12, 'failures':0})

    def test_boolean_count_is_not_an_assertion_count(self):
        text=json.dumps({'kind':'suite-summary','checks':True,'failures':0})
        self.assertEqual(self.module.source_result(0,text)['status'],'EVIDENCE_INVALID')

    def test_negative_text_count_cannot_pass(self):
        self.assertEqual(self.module.source_result(0,'-12 checks, 0 failures')['status'],'EVIDENCE_INVALID')

    def test_actual_c12_prefix_is_accepted(self):
        self.assertEqual(self.module.source_result(0,'Native C12 attachment: 12 checks, 0 failures; CPU controls')['status'],'PASS')

    def test_unknown_gpu_counterexample_halts(self):
        self.assertTrue(self.module.gpu_case_halted({'evidence_layer':'WARP_OWNED','verdict':'COUNTEREXAMPLE'}))

    def test_rehashed_empty_summary_cannot_omit_planned_job(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package='a'*64
            self.module.save(root/'PLAN.json',{'run_id':'r','package_sha256':package,'jobs':[{'id':'required'}]})
            self.module.save(root/'SUMMARY.json',{'run_id':'r','package_sha256':package,'jobs':[],'counts':{}})
            self.module.save(root/'EVIDENCE_MANIFEST.json',{'package_sha256':package,'files':self.module.commitments(root)})
            with self.assertRaisesRegex(ValueError,'CAMPAIGN_JOB_SET_CHANGED'):
                self.module.validate(root,package)

    def test_package_lock_refuses_concurrent_writer(self):
        import os
        if os.name!='nt':self.skipTest('Windows sharing semantics')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'sample.txt').write_text('original')
            self.module.save(root/'CAMPAIGN_PACKAGE.json',{'schema_version':1,'files':[{'path':'sample.txt','sha256':self.module.sha(root/'sample.txt')}],'jobs':[]})
            with self.module.lock_package(root):
                with self.assertRaises(PermissionError):(root/'sample.txt').write_text('changed')

    def test_uncertain_gpu_failure_blocks_later_gpu_but_not_cpu(self):
        self.assertEqual(self.module.schedule_reason({'gpu':True}, True, False), 'GPU_SESSION_HALTED')
        self.assertIsNone(self.module.schedule_reason({'gpu':False}, True, False))

    def test_cancel_blocks_all_later_jobs(self):
        self.assertEqual(self.module.schedule_reason({'gpu':False}, False, True), 'OPERATOR_CANCELLED')

    def test_safe_child_refuses_escape_and_windows_streams(self):
        with tempfile.TemporaryDirectory() as root:
            for path in ('../other', 'C:/outside', '/outside', 'file.txt:stream', 'a/../b', 'a\\..\\b'):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    self.module.child(Path(root),path)

    def test_package_tamper_is_detected(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root); (root/'sample.txt').write_text('original')
            manifest={'schema_version':1,'files':[{'path':'sample.txt','sha256':self.module.sha(root/'sample.txt')}],'jobs':[]}
            (root/'CAMPAIGN_PACKAGE.json').write_text(json.dumps(manifest))
            self.module.verify_package(root)
            (root/'sample.txt').write_text('changed')
            with self.assertRaisesRegex(ValueError,'PACKAGE_HASH_MISMATCH'):
                self.module.verify_package(root)

    def test_export_rejects_renamed_pe_and_preserves_result(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'run'; root.mkdir()
            (root/'SUMMARY.json').write_text('{"status":"FAIL"}')
            (root/'hidden.txt').write_bytes(b'MZ'+b'x'*100)
            (root/'crash.dmp').write_bytes(b'dump')
            archive=Path(temp)/'review.zip'
            self.module.export_review(root,archive)
            import zipfile
            with zipfile.ZipFile(archive) as z:
                self.assertIn('SUMMARY.json',z.namelist())
                self.assertNotIn('hidden.txt',z.namelist())
                self.assertNotIn('crash.dmp',z.namelist())

if __name__=='__main__': unittest.main()
