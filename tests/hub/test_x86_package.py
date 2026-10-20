"""Verify the real composed architecture-specific package and Hub catalog."""
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package', root / 'tools/packaging/customer/Build-UniversalConnectionsPackage.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
files, _ = package.read_base(package.BASE, package.BASE_SHA)
def read(path): return (root / path).read_bytes()
core = read('builds/universal-guides/bin/OptiScaler.dll')
addon = read('builds/prepared-guides/x86/NeuRotic-PreparedGuides.addon32')
helper = read('builds/universal-connections/helper/production/NeuRotic.PreparedGpuHost.exe')
pix = read('builds/universal-connections/helper/production/WinPixEventRuntime.dll')
setup = Path('C:/NeuRotic/universal-guides-experimental/references/prepared-guide-installer/ReShade_Setup_6.8.0_Addon.exe')
package.add_prepared_workflow(files, 'packages/current/', pix, setup)
package.add_x86_package(files, core, addon, helper, pix, 'fixture-commit', '2c897e5a', setup)
package.refresh_hub_packages(files)
prefix = 'packages/prepared-x86/'
build = json.loads(files[prefix + 'support/BUILD-MANIFEST.json'])
assert build['install_route'] == 'prepared-x86' and build['target_bit_size'] == 32 and build['bit_size'] == 64
assert not any(n == prefix+'payload/OptiScaler.dll' for n in files)
for path, expected in [('ReShade32.dll', 0x14c), ('NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32', 0x14c),
                       ('NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.dll', 0x8664),
                       ('NeuRotic/Prepared/NeuRotic.GpuHost/NeuRotic.PreparedGpuHost.exe', 0x8664)]:
    raw=files[prefix+'payload/'+path]; offset=struct.unpack_from('<I',raw,0x3c)[0]
    assert struct.unpack_from('<H',raw,offset+4)[0] == expected, path
assert prefix+'payload/NeuRotic/Prepared/Start-PreparedGame.ps1' in files
assert prefix+'payload/NeuRotic/Prepared/Shaders/MotionEstimation.fx' in files
assert b'NRPG_MV_PROVIDER=0' in files[prefix+'payload/ReShade.ini']
for row in json.loads(files['support/Hub-Packages.json'])['packages']:
    path='packages/'+ ('current' if row['id']=='flagship-approved' else 'prepared-x86')+'/support/PACKAGE-MANIFEST.json'
    assert hashlib.sha256(files[path]).hexdigest()==row['manifestSha256'],row['id']
print('PASS actual pinned ReShade32/addon32/core64/host64 composition, shader closure, activation launcher and architecture-specific Hub catalog hashes')
