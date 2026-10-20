"""Focused infrastructure regressions; synthetic fixtures, no product binaries."""
from pathlib import Path
import importlib.util,json,os,subprocess,sys,tempfile,unittest,shutil
import copy,hashlib
ROOT=Path(__file__).resolve().parents[1]
PWSH=os.environ.get('NEUROTIC_TEST_PWSH') or shutil.which('pwsh') or 'pwsh'
class Repairs(unittest.TestCase):
 def test_normalized_parent_escape_refused(self):
  sys.path.insert(0,str(ROOT/'tools/packaging/customer'));from OfflineDependencies import owned_output
  with self.assertRaisesRegex(ValueError,'Output must stay'):owned_output(ROOT,ROOT/'builds/../../outside-file')
 def test_native_output_selection_rejects_stale_discovery_without_deleting_it(self):
  sys.path.insert(0,str(ROOT/'tools/build'));from BuildEvidence import validate_receipt,validate_contract
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   d=Path(d)
   def row(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
   src=d/'source.cpp';src.write_text('inert');pins=[row(src)];outputs=[]
   for name in ['NeuRotic.exe','NeuRotic.Discovery.exe']:
    p=d/name;p.write_bytes(b'inert output sentinel');outputs.append(row(p))
   evidence={'scope':'OBSERVED_BUILD','target':'app','configuration':'Release|x64','required_inputs':[{**pins[0],'kind':'source'}],'producers':[{'command':'fixture-policy only','tools':pins,'inputs':pins,'outputs':outputs,'exit_code':0}],'selected_outputs':outputs}
   validate_contract(evidence,pins)
   receipt={'schema_version':2,'target':'app','evidence':evidence,'input_pins':pins,'files':[{**r,'path':Path(r['path']).relative_to(ROOT).as_posix()} for r in outputs],'repository':str(ROOT),'exit_code':0,'source_unchanged':True}
   with self.assertRaisesRegex(ValueError,'Selected target output set differs'):validate_receipt(receipt)
   self.assertEqual((d/'NeuRotic.Discovery.exe').read_bytes(),b'inert output sentinel')
 def test_linked_receipt_destination_refused(self):
  sys.path.insert(0,str(ROOT/'tools/packaging/customer'));from OfflineDependencies import write_owned
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   d=Path(d);sentinel=d/'sentinel';sentinel.write_bytes(b'unchanged');link=d/'dependency-receipt.json'
   try:os.symlink(sentinel,link)
   except OSError as e:self.skipTest('File symlink creation unavailable without changing privileges: '+str(e))
   try:
    with self.assertRaisesRegex(ValueError,'Linked output refused'):write_owned(ROOT,link,b'changed')
    self.assertEqual(sentinel.read_bytes(),b'unchanged')
   finally:link.unlink()
 def test_each_missing_support_file_refuses_before_copy(self):
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   d=Path(d);names=json.loads((ROOT/'tools/packaging/customer/SupportInventory.json').read_bytes())['files']
   rels=['tools/hub/Stage-Hub.ps1','tools/build/Build.Context.ps1','tools/packaging/customer/SupportInventory.json']+['tools/packaging/customer/support/'+n for n in names]
   for rel in rels:
    p=d/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p)
   out=d/'builds/output';out.mkdir(parents=True);sentinel=out/'keep';sentinel.write_text('unchanged')
   for name in names:
    with self.subTest(name=name):
     src=d/'tools/packaging/customer/support'/name;hidden=src.with_suffix(src.suffix+'.withheld');src.rename(hidden)
     try:
      p=subprocess.run([PWSH,'-NoProfile','-File',str(d/'tools/hub/Stage-Hub.ps1'),'-SupportOnly','-BinRoot',str(out)],capture_output=True,text=True)
      self.assertNotEqual(p.returncode,0);self.assertIn('Missing support input: '+name,p.stderr);self.assertFalse((out/'support').exists());self.assertEqual(sentinel.read_text(),'unchanged')
     finally:hidden.rename(src)
 def test_build_evidence_requires_each_typed_input_and_named_producer(self):
  sys.path.insert(0,str(ROOT/'tools/build'))
  from BuildEvidence import validate_contract
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   d=Path(d);rows=[]
   for kind,name in [('source','HubMain.cpp'),('link','d3d11.lib'),('shader','PrepareInput.hlsl'),('resource','HubResources.rc'),('import','effective.targets'),('tool','cl.exe')]:
    p=d/name;p.write_text(kind);rows.append({'kind':kind,'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size})
   contract={'scope':'FIXTURE_POLICY_ONLY','target':'app','configuration':'Release|x64','required_inputs':rows,'producers':[],'selected_outputs':[]}
   validate_contract(contract,rows)
   for missing in rows:
    with self.subTest(kind=missing['kind']):
     with self.assertRaisesRegex(ValueError,missing['kind']+' input absent'):
      validate_contract(contract,[r for r in rows if r!=missing])
   extra=d/'unproven.h';extra.write_text('extra')
   with self.assertRaisesRegex(ValueError,'Unproven generated'):
    validate_contract(contract,rows,generated_inputs=[str(extra)])
 def test_fresh_support_import_and_requirements(self):
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   p=subprocess.run([PWSH,'-NoProfile','-File',str(ROOT/'tools/hub/Stage-Hub.ps1'),'-SupportOnly','-BinRoot',d],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr)
   support=Path(d)/'support'
   p=subprocess.run([PWSH,'-NoProfile','-Command',"$ErrorActionPreference='Stop'; . '"+str(support/'NeuRotic-InstallCore.ps1')+"'; $r=Get-NeuroticRenderingRequirements; if(-not $r){throw 'Missing requirements'}; 'FRESH_SUPPORT_PASS'"],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr);self.assertIn('FRESH_SUPPORT_PASS',p.stdout)
 def test_descendant_junction_refused_in_plan(self):
  base=ROOT/'builds/tests/infra-repair-fixtures';base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=base) as d:
   root=Path(d)/'output';root.mkdir();sentinel=Path(d)/'sentinel';sentinel.mkdir();(sentinel/'keep').write_text('unchanged')
   link=root/'obj'
   p=subprocess.run([PWSH,'-NoProfile','-Command',"New-Item -ItemType Junction -Path '"+str(link)+"' -Target '"+str(sentinel)+"' | Out-Null"],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr)
   try:
    p=subprocess.run([PWSH,'-NoProfile','-File',str(ROOT/'tools/build/Build-App.ps1'),'-PlanOnly','-OutputRoot',str(root)],capture_output=True,text=True)
    self.assertNotEqual(p.returncode,0,'Descendant junction must refuse');self.assertEqual((sentinel/'keep').read_text(),'unchanged')
   finally:os.rmdir(link)
if __name__=='__main__':unittest.main()
