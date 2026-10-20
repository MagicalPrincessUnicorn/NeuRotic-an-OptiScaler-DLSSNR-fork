import importlib.util
import json
import configparser
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / 'tools/packaging/customer/Build-UniversalConnectionsPackage.py'

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

class NativeShippingTests(unittest.TestCase):
    def test_fresh_native_reshade_loading_is_off(self):
        ini = configparser.ConfigParser()
        ini.read(ROOT / 'integration/OptiScaler.ini', encoding='utf-8-sig')
        self.assertEqual(ini['Plugins']['LoadReshade'].strip().casefold(), 'false')
    def test_registry_contains_only_supported_native_route(self):
        module = load(BUILDER, 'universal_package')
        files = {'packages/current/support/PACKAGE-MANIFEST.json': b'{}',
                 'packages/prepared-x86/support/PACKAGE-MANIFEST.json': b'{}'}
        module.refresh_hub_packages(files)
        rows = json.loads(files['support/Hub-Packages.json'])['packages']
        self.assertEqual([row['id'] for row in rows], ['flagship-approved'])

    def test_native_composition_preserves_required_shared_components(self):
        module = load(BUILDER.with_name('NativePackagePolicy.py'), 'native_policy')
        keep = ['packages/current/payload/OptiScaler.dll',
                'packages/current/payload/WinPixEventRuntime.dll',
                'packages/current/payload/NeuRotic.Fsr3.Vulkan.dll',
                'packages/current/payload/nvngx_dlssnr.dll',
                'packages/current/payload/OptiScaler/CharacterInspector/Version 1/worker.dll']
        retired = ['packages/current/payload/ReShade64.dll',
                   'packages/prepared-x86/payload/ReShade32.dll',
                   'Optional/ReShade/ReShade_Setup_6.8.0_Addon.exe',
                   'Optional/x86/NeuRotic.GpuHost/OptiScaler.dll',
                   'packages/current/payload/NeuRotic-PreparedGuides.addon64',
                   'packages/current/payload/OptiScaler/PreparedGuides/Shaders/MotionEstimation.fx',
                   'Optional/ExternalFeeder/64/NeuRotic-ExternalFeeder.addon64',
                   'support/PreparedSetup.ps1']
        files = {name: b'fixture bytes' for name in keep + retired}
        module.retain_native_shipping(files)
        self.assertEqual(set(files), set(keep))

if __name__ == '__main__':
    unittest.main()
