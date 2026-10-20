"""T03/T58/T59 fail-closed evidence and provenance regression tests."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import os
import subprocess

ROOT = Path(__file__).resolve().parents[3]
VALIDATOR = ROOT / 'tools/nr/provenance/validate.py'

class W00EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(VALIDATOR.is_file(), 'W00 fail-closed validator has not been implemented')
        spec = importlib.util.spec_from_file_location('w00_validator', VALIDATOR)
        self.v = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.v)
        self.schema = json.loads((VALIDATOR.parent/'provenance-schema.json').read_text())

    def record(self):
        r = {group: {field: None for field in fields} for group,fields in self.schema['required_groups'].items()}
        r['identity'].update(schema_version='1',record_id='fixture',component_id='fixture',artifact_id='source',display_name='Synthetic fixture',artifact_kind='source')
        r['disposition'].update(component_classification='SYNTHETIC_TEST',source_reuse_state='APPROVED_WITH_EVIDENCE',redistribution_state='BLOCKED',new_import_state='APPROVED_WITH_EVIDENCE',approval_evidence=['synthetic test only'],blocked_reason='Not approved for redistribution')
        r['evidence'].update(evidence_class='SOURCE_VERIFIED',reviewer='synthetic fixture',review_date='2026-09-20',evidence_refs=['synthetic fixture'])
        r['selection'] = 'UNSELECTED'
        r['destination']['package_relative_path']='test.bin'
        r['validation']['observed_package_hashes']={'test.bin':hashlib.sha256(b'fixture').hexdigest()}
        return r

    def test_t58_required_fields_and_approval_separation(self):
        r = self.record()
        self.assertEqual([],self.v.validate_records([r],self.schema))
        del r['license']['notice_text']
        self.assertTrue(self.v.validate_records([r],self.schema))
        r=self.record();r['disposition']['source_reuse_state']='PASS'
        self.assertTrue(self.v.validate_records([r],self.schema))
        r=self.record();r['disposition']['approval_evidence']=[]
        self.assertTrue(self.v.validate_records([r],self.schema))

    def test_t58_selected_unknown_identity_or_closure_rejected(self):
        r=self.record();r['selection']='SELECTED'
        self.assertTrue(self.v.validate_records([r],self.schema))
        r['disposition']['new_import_state']='BLOCKED'
        self.assertTrue(self.v.validate_records([r],self.schema))
        self.assertTrue(self.v.validate_records([self.record(),self.record()],self.schema))

    def test_t59_source_permission_does_not_grant_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test.bin').write_bytes(b'fixture')
            entry={'path':'test.bin','sha256':hashlib.sha256(b'fixture').hexdigest(),'bytes':7,'record_id':'fixture','role':'synthetic'}
            policy={'schema_version':1,'variant':'synthetic','members':[entry],'required_notices':[]}
            r=self.record()
            self.assertTrue(self.v.validate_package(root,policy,[r]))
            r['disposition']['redistribution_state']='APPROVED_WITH_EVIDENCE'
            self.assertEqual([],self.v.validate_package(root,policy,[r]))
            (root/'test.bin').write_bytes(b'altered')
            self.assertTrue(self.v.validate_package(root,policy,[r]))

    def test_t59_unknown_missing_notice_duplicate_and_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'unknown.dll').write_bytes(b'x')
            policy={'schema_version':1,'variant':'synthetic','members':[],'required_notices':[]}
            self.assertTrue(self.v.validate_package(root,policy,[]))
            for path in ['../escape','/absolute','C:/drive','a\\b','a//b','a/./b','a:b','a.','a ']:
                self.assertFalse(self.v.safe_relative(path),path)
            self.assertTrue(self.v.safe_relative('Licenses/test.txt'))
            r=self.record();r['disposition']['redistribution_state']='APPROVED_WITH_EVIDENCE'
            entry={'path':'unknown.dll','sha256':hashlib.sha256(b'x').hexdigest(),'bytes':1,'record_id':'fixture','role':'synthetic'}
            policy['members']=[entry,copy.deepcopy(entry)]
            self.assertTrue(self.v.validate_package(root,policy,[r]))
            policy['members']=[entry];policy['required_notices']=['LICENSE']
            self.assertTrue(self.v.validate_package(root,policy,[r]))

    def test_t03_four_axes_and_not_run_are_independent(self):
        axes={name:{'verdict':'NOT_RUN','executed':False,'evidence':[]} for name in self.v.AXES}
        self.assertEqual([],self.v.validate_axes(axes))
        axes['protocol_semantics']={'verdict':'PASS','executed':True,'evidence':['actual synthetic receipt']}
        self.assertEqual([],self.v.validate_axes(axes))
        axes['gpu_resource_correctness']['verdict']='PASS'
        self.assertTrue(self.v.validate_axes(axes))
        del axes['temporal_image_quality']
        self.assertTrue(self.v.validate_axes(axes))

    def test_review_rejects_empty_evidence_elements(self):
        for invalid in [None,'','   ',42,{}]:
            r=self.record();r['disposition']['approval_evidence']=[invalid]
            with self.subTest(kind='approval',value=invalid):
                self.assertTrue(self.v.validate_records([r],self.schema))
            axes={name:{'verdict':'PASS','executed':True,'evidence':[invalid]} for name in self.v.AXES}
            with self.subTest(kind='axis',value=invalid):
                self.assertTrue(self.v.validate_axes(axes))

    def test_review_binds_package_to_provenance_and_required_notice(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test.bin').write_bytes(b'fixture')
            r=self.record();r['disposition']['redistribution_state']='APPROVED_WITH_EVIDENCE'
            entry={'path':'test.bin','sha256':hashlib.sha256(b'fixture').hexdigest(),'bytes':7,'record_id':'fixture','role':'synthetic'}
            policy={'schema_version':1,'variant':'synthetic','members':[entry],'required_notices':[]}
            r['validation']['observed_package_hashes']={'test.bin':'0'*64}
            with self.subTest(kind='artifact binding'):
                self.assertTrue(self.v.validate_package(root,policy,[r]))
            r['validation']['observed_package_hashes']={'test.bin':entry['sha256']}
            r['license'].update(notice_required=True,notice_destination='NOTICE',notice_text='Required notice')
            with self.subTest(kind='required notice'):
                self.assertTrue(self.v.validate_package(root,policy,[r]))

    @unittest.skipUnless(os.name=='nt','Windows junction semantics')
    def test_review_rejects_junction_package_root(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory);target=base/'real';target.mkdir();junction=base/'junction'
            result=subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(junction),str(target)],capture_output=True)
            self.assertEqual(0,result.returncode,result.stderr)
            try:
                policy={'schema_version':1,'variant':'synthetic','members':[],'required_notices':[]}
                self.assertTrue(self.v.validate_package(junction,policy,[]))
            finally:
                junction.rmdir()

if __name__=='__main__':
    unittest.main(verbosity=2)
