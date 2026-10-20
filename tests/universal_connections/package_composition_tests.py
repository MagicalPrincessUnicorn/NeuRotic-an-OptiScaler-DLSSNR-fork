import argparse
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('package',root/'tools/packaging/customer/Build-UniversalConnectionsPackage.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=argparse.ArgumentParser();p.add_argument('--reshade-setup',type=Path,required=True);args=p.parse_args()
files,manifest=m.read_base(m.BASE,m.BASE_SHA)
pix=(root/'builds/universal-connections/helper/production/WinPixEventRuntime.dll').read_bytes()
m.add_prepared_workflow(files,'packages/current/',pix,args.reshade_setup)
for name in ('ReShade64.dll','WinPixEventRuntime.dll','OptiScaler/PreparedGuides/Shaders/NeuRotic_PreparedGuides.fx','OptiScaler/PreparedGuides/Shaders/ReShade.fxh','OptiScaler/PreparedGuides/Shaders/MotionEstimation.fx','OptiScaler/PreparedGuides/Prepared.ini'):
 assert 'packages/current/payload/'+name in files,name
for name in ('support/PreparedSetup.ps1','support/PreparedProfile.ps1','support/Start-PreparedTest.ps1','ReShade-Setup.cmd','Start-Prepared-Test.cmd'):
 assert name in files,name
assert b"InputSource','3'" in files['support/PreparedProfile.ps1']
assert 'instead of the ordinary prepared' not in (root/'tools/packaging/customer/Build-UniversalConnectionsPackage.py').read_text()
print('PASS complete baseline hashes, pinned runtime/dependencies/licenses, prepared shader includes, presets and managed setup/profile/launch workflow; no package or game mutation')
