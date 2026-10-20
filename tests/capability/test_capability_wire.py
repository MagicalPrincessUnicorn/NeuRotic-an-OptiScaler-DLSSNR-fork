"""Cross-validate real native exports against the preserved packet oracle."""
import pathlib, importlib.util, sys, json, unittest, copy, subprocess
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'builds/capability-facts/python'))
packet=pathlib.Path(sys.argv.pop(1))
spec=importlib.util.spec_from_file_location('wire_oracle',packet/'tests/wire_validation.py')
oracle=importlib.util.module_from_spec(spec); spec.loader.exec_module(oracle)
class WireTests(unittest.TestCase):
    def test_actual_owner_exports(self):
        files=list((root/'builds/capability-facts/tests').glob('export-*.json'))
        self.assertEqual(len(files),10)
        for path in files:
            with self.subTest(path=path.name): oracle.strict_load(path.read_bytes())
    def test_closed_registry_is_exact(self):
        self.assertEqual(json.loads((root/'tests/capability/registry.json').read_text()),json.loads((packet/'evidence/CAPABILITY_REGISTRY.json').read_text()))
        self.assertEqual((root/'tests/capability/schema-v1.json').read_bytes(),(packet/'legacy/I01_Capability_Fingerprint_Schema.json').read_bytes())
    def test_present_capture_precedes_all_early_returns(self):
        source=(root/'OptiScaler/dlssnr/DlssNr_Present.cpp').read_text()
        body=source[source.index('PresentCallIdentity EvaluatePresentImageOnly'):]
        self.assertLess(body.index('Capability::PresentObservationScope'),body.index('return'))
        self.assertLess(body.index('capabilityChain.values.chainIdentity=hdrObservation.identityGeneration'),body.index('colorSpace = hdrObservation.colorSpace'))
        baseline=subprocess.check_output(['git','-c',f'safe.directory={root}','show','23b1cb299d8e378371ba9eb7753f499d6ae4bc6b:OptiScaler/dlssnr/DlssNr_Present.cpp'],cwd=root,text=True)
        self.assertEqual(source.count('HdrObservation::Registry::Instance().Read(swapChain)'),baseline.count('HdrObservation::Registry::Instance().Read(swapChain)'))
    def test_never_authoritative(self):
        for path in (root/'builds/capability-facts/tests').glob('export-*.json'):
            data=json.loads(path.read_text())
            self.assertFalse(data['execution_permission']); self.assertFalse(data['persist_runtime_readiness'])
            self.assertEqual(data['legacy_fingerprints'],[])
            for field in ('execution_permission','persist_runtime_readiness'):
                bad=copy.deepcopy(data); bad[field]=True
                with self.assertRaises(oracle.ContractError): oracle.validate(bad)
if __name__=='__main__': unittest.main()
