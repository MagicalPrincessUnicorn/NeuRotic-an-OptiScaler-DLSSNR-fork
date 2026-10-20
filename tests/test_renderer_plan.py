"""No-write renderer planning with synthetic object receipts; no compilation/GPU."""
import hashlib,json,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];ENTRY=ROOT/'tests/menu_production_preview.py'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def row(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p)}
class RendererPlan(unittest.TestCase):
    def setUp(self):
        base=ROOT/'builds/source/core/renderer-plan-fixture';base.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=base);self.dir=Path(self.temp.name).resolve();assert self.dir.is_relative_to(base.resolve())
        self.obj=self.dir/'synthetic.obj';self.obj.write_bytes(b'synthetic object, never linked')
        self.libs=[]
        for i in range(3):
            p=self.dir/f'synthetic{i}.lib';p.write_bytes(b'synthetic library');self.libs.append(row(p))
        self.manifest=self.dir/'source.json';self.manifest.write_text(json.dumps({'files':[row(ENTRY)]}))
        self.receipt=self.dir/'objects.json';self.output=ROOT/'builds/tests/renderer-plan-no-write'
        self.data={'qualification':'INTERNAL_DEVELOPMENT_OBJECTS_ONLY','repository':str(ROOT),'source_manifest_path':str(self.manifest),'source_manifest_sha256':sha(self.manifest),'objects':[row(self.obj)],'flow_libraries':self.libs,'resources':[],'tools':[]}
    def tearDown(self):self.temp.cleanup()
    def invoke(self,wrong_sha=False,optimized=False):
        self.receipt.write_text(json.dumps(self.data))
        return subprocess.run([sys.executable,*(['-O'] if optimized else []),'-B',str(ENTRY),'--object-receipt',str(self.receipt),'--receipt-sha256','0'*64 if wrong_sha else sha(self.receipt),'--output-root',str(self.output),'--plan-only'],capture_output=True,text=True,timeout=15)
    def test_single_script_empty_tools_manifest_refused_without_output(self):
        self.assertFalse(self.output.exists());p=self.invoke();self.assertNotEqual(p.returncode,0);self.assertFalse(self.output.exists());self.assertIn('Production renderer input absent',p.stderr)
    def test_optimized_python_keeps_same_production_input_gate(self):
        p=self.invoke(optimized=True);self.assertNotEqual(p.returncode,0);self.assertIn('Production renderer input absent',p.stderr);self.assertFalse(self.output.exists())
    def test_changed_object_rejected_before_output(self):
        self.obj.write_bytes(b'changed');p=self.invoke();self.assertNotEqual(p.returncode,0);self.assertFalse(self.output.exists())
    def test_old_object_root_rejected(self):
        self.data['objects']=[row(ENTRY)];p=self.invoke();self.assertNotEqual(p.returncode,0);self.assertFalse(self.output.exists())
    def test_changed_receipt_rejected(self):
        p=self.invoke(True);self.assertNotEqual(p.returncode,0);self.assertFalse(self.output.exists())

if __name__=='__main__':unittest.main()
