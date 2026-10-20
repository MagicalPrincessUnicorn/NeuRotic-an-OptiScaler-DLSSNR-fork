"""Synthetic dependency archives only; never access installed models or games."""
import hashlib, importlib.util, json, stat, tempfile, unittest, zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('offline',ROOT/'tools/packaging/customer/OfflineDependencies.py')
offline=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(offline)
sha=lambda b:hashlib.sha256(b).hexdigest()

class Dependencies(unittest.TestCase):
    def setUp(self):
        fixture_root=ROOT/'builds/tests/offline-dependencies';fixture_root.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=fixture_root);self.root=Path(self.temp.name).resolve()
        assert self.root.is_relative_to(fixture_root.resolve())
        self.files={'licenses/test.txt':b'license','discovery/System.Text.Json.dll':b'vendor',
            'NeuRotic.exe':b'old app','discovery/NeuRotic.Discovery.dll':b'old discovery',
            'Tools/WindowWorker/NeuRotic.WindowWorker.exe':b'old worker',
            'packages/current/payload/OptiScaler.dll':b'old native'}
        self.archive=self.root/'archive.zip';self.proof=self.root/'proof.json'
        self.reseal()
    def tearDown(self): self.temp.cleanup()
    def reseal(self, extra=None):
        with zipfile.ZipFile(self.archive,'w') as z:
            for n,b in self.files.items():z.writestr('NeuRotic/'+n,b)
            if extra: z.writestr(*extra)
        self.proof.write_text(json.dumps({'inventory':{'files':[{'path':n,'bytes':len(b),'sha256':sha(b)} for n,b in self.files.items()]}}))
        self.archive_sha=sha(self.archive.read_bytes());self.proof_sha=sha(self.proof.read_bytes())
    def reader(self):return offline.DependencyArchive(self.archive,self.archive_sha,self.proof,self.proof_sha)
    def test_exact_external_inventory_without_internal_manifest(self):
        reader=self.reader();self.assertEqual(reader.plan(['licenses/test.txt'])[0]['sha256'],sha(b'license'))
        self.assertEqual(reader.read(['licenses/test.txt']),{'licenses/test.txt':b'license'})
    def test_plan_reads_no_member_payload(self):
        from unittest.mock import patch
        with patch.object(zipfile.ZipFile,'read',side_effect=AssertionError('payload read')):
            self.reader().plan(['licenses/test.txt'])
    def test_archive_hash_failure(self):
        self.archive.write_bytes(self.archive.read_bytes()+b'changed')
        with self.assertRaises(ValueError):self.reader()
    def test_proof_hash_failure(self):
        self.proof.write_bytes(self.proof.read_bytes()+b' ')
        with self.assertRaises(ValueError):self.reader()
    def test_unlisted_archive_member(self):
        self.reseal(('NeuRotic/unlisted',b'x'))
        with self.assertRaises(ValueError):self.reader()
    def test_case_duplicate_zip_member(self):
        self.reseal(('NeuRotic/LICENSES/TEST.TXT',b'x'))
        with self.assertRaises(ValueError):self.reader()
    def test_unsafe_member(self):
        self.files['licenses/../outside']=b'x';self.reseal()
        with self.assertRaises(ValueError):self.reader()
    def test_duplicate_inventory_row(self):
        d=json.loads(self.proof.read_bytes());d['inventory']['files'].append(d['inventory']['files'][0]);self.proof.write_text(json.dumps(d));self.proof_sha=sha(self.proof.read_bytes())
        with self.assertRaises(ValueError):self.reader()
    def test_old_firstparty_is_never_inherited(self):
        reader=self.reader()
        for n in self.files:
            if n not in ('licenses/test.txt','discovery/System.Text.Json.dll'):
                with self.subTest(n=n),self.assertRaises(ValueError):reader.plan([n])
    def test_unknown_binary_is_not_a_dependency(self):
        self.files['packages/current/payload/unknown.dll']=b'x';self.reseal()
        with self.assertRaises(ValueError):self.reader().plan(['packages/current/payload/unknown.dll'])
    def test_model_requires_explicit_opt_in(self):
        n='packages/current/payload/OptiScaler/CharacterInspector/Version 1/models/synthetic.onnx';self.files[n]=b'synthetic';self.reseal()
        with self.assertRaises(ValueError):self.reader().plan([n])
        self.assertEqual(len(self.reader().plan([n],allow_models=True)),1)
    def test_member_hash_mismatch(self):
        d=json.loads(self.proof.read_bytes());d['inventory']['files'][0]['sha256']='0'*64;self.proof.write_text(json.dumps(d));self.proof_sha=sha(self.proof.read_bytes())
        with self.assertRaises(ValueError):self.reader().read(['licenses/test.txt'])
    def test_no_silent_selection_of_missing_dependency(self):
        with self.assertRaises(ValueError):self.reader().plan(['licenses/absent.txt'])
    def test_symlink_rejected(self):
        with zipfile.ZipFile(self.archive,'a') as z:
            item=zipfile.ZipInfo('NeuRotic/licenses/link');item.create_system=3;item.external_attr=(stat.S_IFLNK|0o777)<<16;z.writestr(item,b'elsewhere')
        self.archive_sha=sha(self.archive.read_bytes())
        with self.assertRaises(ValueError):self.reader()

if __name__=='__main__':unittest.main()
