"""Exercise actual no-write PowerShell entry points; never compile or restore."""
import json, os, shutil, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PWSH=os.environ.get('NEUROTIC_TEST_PWSH') or shutil.which('pwsh') or 'pwsh'
class Recipes(unittest.TestCase):
    def invoke(self,entry,*extra):
        return subprocess.run([PWSH,'-NoProfile','-File',str(ROOT/entry),'-RepositoryRoot',str(ROOT),'-PlanOnly',*extra],capture_output=True,text=True,timeout=30)
    def test_core_has_explicit_shared_dependencies_sdk_and_no_node_reuse(self):
        p=self.invoke('tools/build/Build-Core.ps1');self.assertEqual(p.returncode,0,p.stderr);d=json.loads(p.stdout)
        self.assertEqual(len(d['sharedDependencies']),9)
        self.assertIn('/nr:false',d['commands'][-1]['arguments'])
        self.assertIn('/p:WindowsTargetPlatformVersion=10.0.28000.0',d['commands'][-1]['arguments'])
        self.assertTrue(any('FidelityFX-SDK' in a and 'external' in a for a in d['commands'][0]['arguments']))
        self.assertEqual(d['qualification'],'INTERNAL_DEVELOPMENT_PLAN_ONLY')
    def test_no_write_output_override(self):
        out=ROOT/'builds/tests/recipe-no-write-not-created'
        self.assertFalse(out.exists())
        for entry in ('tools/build/Build-Core.ps1','tools/build/Build-App.ps1','tools/window-worker/Build-Worker.ps1'):
            with self.subTest(entry=entry):
                p=self.invoke(entry,'-OutputRoot',str(out));self.assertEqual(p.returncode,0,p.stderr);self.assertFalse(out.exists())
    def test_output_cannot_escape_checkout(self):
        p=self.invoke('tools/build/Build-App.ps1','-OutputRoot',str(ROOT.parent/'other-output'));self.assertNotEqual(p.returncode,0)
    def test_invalid_build_label_fails_even_in_plan(self):
        p=self.invoke('tools/build/Build-Core.ps1','-BuildLabel','invalid label');self.assertNotEqual(p.returncode,0)
    def test_app_uses_shared_offline_nuget_cache(self):
        p=self.invoke('tools/build/Build-App.ps1');self.assertEqual(p.returncode,0,p.stderr);d=json.loads(p.stdout)
        self.assertTrue(d['nugetPackages'].replace('\\','/').endswith('/_deps/hub/nuget'))
        self.assertIn('--no-restore',d['commands'][1]['arguments'])
    def test_worker_shipping_pair_separate_intermediates(self):
        p=self.invoke('tools/window-worker/Build-Worker.ps1');self.assertEqual(p.returncode,0,p.stderr);d=json.loads(p.stdout)
        self.assertIn('/nr:false',d['commands'][0]['arguments']);self.assertEqual(d['sourceGraph'],'shipping/source')
        self.assertNotEqual(d['intermediate'],d['forwarderIntermediate'])
    def test_missing_source_manifest_does_not_create_output(self):
        p=self.invoke('tools/build/Build-Core.ps1','-SourceManifest',str(ROOT/'does-not-exist.json'));self.assertNotEqual(p.returncode,0)

if __name__=='__main__':unittest.main()
