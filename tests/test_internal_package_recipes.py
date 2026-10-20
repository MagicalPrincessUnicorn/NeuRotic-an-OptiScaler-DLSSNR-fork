"""Package identity/ownership checks use synthetic receipts and no binary execution."""
import hashlib, importlib.util, json, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle',ROOT/'tools/packaging/customer/Build-ReleaseBundle.py')
bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
class InternalPackage(unittest.TestCase):
    def test_metadata_only_receipt_cannot_claim_observed_build(self):
        parent=ROOT/'builds/tests/package-receipt-fixture';parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            directory=Path(temporary).resolve();self.assertTrue(directory.is_relative_to(parent.resolve()))
            source=Path(__file__).resolve();inputs=[{'path':str(source),'bytes':source.stat().st_size,'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}]
            manifest=directory/'manifest.json';manifest.write_text(json.dumps({'schema_version':1,'target':'core','repository':str(ROOT),'files':inputs}))
            digest=hashlib.sha256(manifest.read_bytes()).hexdigest()
            product=directory/'OptiScaler.dll';product.write_bytes(b'inert metadata policy fixture; never packaged or executed')
            r={'path':str(product),'bytes':product.stat().st_size,'sha256':hashlib.sha256(product.read_bytes()).hexdigest()}
            evidence={'scope':'OBSERVED_BUILD','target':'core','configuration':'Release|x64','required_inputs':[{**inputs[0],'kind':'source'}],'producers':[{'exit_code':0,'command':'synthetic policy control, not actual build evidence','tools':inputs,'inputs':inputs,'outputs':[r]}],'selected_outputs':[r]}
            receipt={'schema_version':2,'qualification':'INTERNAL_DEVELOPMENT_BUILD_ONLY','target':'core','repository':str(ROOT),'exit_code':0,'source_unchanged':True,'source_manifest_path':str(manifest),'source_manifest_sha256':digest,'build_label':'099-dev-'+digest[:16],'input_pins':inputs,'evidence':evidence,'files':[{**r,'path':product.relative_to(ROOT).as_posix()}]}
            with self.assertRaisesRegex(ValueError,'Actual supported capture adapter required'):bundle.require_development_receipt(receipt,'core')
            receipt['input_pins']=[]
            with self.assertRaises(ValueError):bundle.require_development_receipt(receipt,'core')
    def test_internal_output_is_owned(self):
        self.assertEqual(bundle.internal_output(ROOT/'builds/tests/internal.zip'),ROOT/'builds/tests/internal.zip')
    def test_external_output_refused(self):
        with self.assertRaises(ValueError):bundle.internal_output(ROOT.parent/'_exports/external.zip')
    def test_parent_escape_refused(self):
        with self.assertRaises(ValueError):bundle.internal_output(ROOT/'builds/../../external.zip')
    def test_old_receipt_refused(self):
        with self.assertRaises(ValueError):bundle.require_development_receipt({'exit_code':0,'source_unchanged':True},'core')
    def test_wrong_target_refused(self):
        receipt={'schema_version':1,'qualification':'INTERNAL_DEVELOPMENT_BUILD_ONLY','target':'worker','exit_code':0,'source_unchanged':True,'source_manifest_sha256':'1'*64,'input_pins':[{'path':str(Path(__file__).resolve()),'bytes':0,'sha256':'0'*64}]}
        with self.assertRaises(ValueError):bundle.require_development_receipt(receipt,'core')
    def test_changed_frozen_input_refused(self):
        receipt={'schema_version':1,'qualification':'INTERNAL_DEVELOPMENT_BUILD_ONLY','target':'core','exit_code':0,'source_unchanged':True,'source_manifest_sha256':'1'*64,'input_pins':[{'path':str(Path(__file__).resolve()),'bytes':0,'sha256':'0'*64}]}
        with self.assertRaises(ValueError):bundle.require_development_receipt(receipt,'core')
    def test_canonical_magpie_is_packaging_source(self):
        source=(ROOT/'tools/packaging/customer/Build-ReleaseBundle.py').read_text()
        self.assertIn("ROOT / 'Licenses/Magpie-LICENSE.txt'",source)
        self.assertNotIn('references/window-nr/capture-donor/Magpie.LICENSE',source)
    def test_development_version_preserves_upstream(self):
        header=(ROOT/'OptiScaler/resource.h').read_text()
        self.assertIn('#define NEUROTIC_VERSION_PATCH 9',header)
        self.assertIn('#define VER_MAJOR_VERSION 10',header)
        self.assertIn('#define VER_BUILD_NUMBER 1',header)
        self.assertIn(' DEVELOPMENT (',header)

if __name__=='__main__':unittest.main()
