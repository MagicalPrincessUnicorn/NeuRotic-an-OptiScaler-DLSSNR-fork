import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/packaging/customer'))
import InspectorLayout

class InspectorLayoutTests(unittest.TestCase):
    def test_host_version_drives_package_paths_without_changing_cohort(self):
        prefix = 'packages/current/payload/OptiScaler/CharacterInspector/'
        cohort = {'worker.py': b'worker', 'models/a.onnx': b'model', 'DISTRIBUTION-MANIFEST.json': b'inventory'}
        files = {prefix + name: raw for name, raw in cohort.items()}
        InspectorLayout.version_inspector(files)
        version = InspectorLayout.required_version()
        self.assertEqual({name.removeprefix(prefix + version + '/'): raw for name, raw in files.items()}, cohort)
        self.assertEqual(InspectorLayout.component(), {'version': version, 'relativeRoot': 'OptiScaler/CharacterInspector/' + version})
        InspectorLayout.version_inspector(files)
        self.assertEqual(len(files), len(cohort))

    def test_staging_and_duplicate_members_are_refused(self):
        prefix = 'payload/OptiScaler/CharacterInspector/'
        for files in ({prefix + '.Version 1.installing/a': b'a'},
                      {prefix + 'a': b'a', prefix + 'Version 1/a': b'b'}):
            with self.assertRaises(ValueError):
                InspectorLayout.version_inspector(files)

if __name__ == '__main__':
    unittest.main()
