"""Supervise the native local builder; publish a verified generation only after successful completion.

Developer tool only. Vendor runtime licenses remain with the supplied official runtime.
"""
import argparse, json, os, pathlib, shutil, subprocess, sys
from bootstrap_dependencies import digest
ROOT=pathlib.Path(__file__).resolve().parents[2]
def publish(building,destination,expected_files,cancelled=False):
    if cancelled:raise RuntimeError('Cache preparation cancelled; existing generation preserved')
    if destination.exists():raise FileExistsError('Existing cache is preserved: '+str(destination))
    manifest=json.loads((building/'depth-cache.json').read_text())
    if manifest['builder']!='neurotic-dav2-local-v1' or manifest['parity']!='synthetic-fp32-reference-passed':raise RuntimeError('Native builder did not validate the generation')
    if set(manifest['files'])!=set(expected_files):raise RuntimeError('Cache files differ from expected generation')
    for name in expected_files:
        if digest(building/name)!=manifest['files'][name]:raise RuntimeError('Completed cache hash mismatch: '+name)
    os.rename(building,destination) # sibling directory; no partial generation visible to the host
def main():
    p=argparse.ArgumentParser();p.add_argument('--export',type=pathlib.Path,required=True);p.add_argument('--runtime',type=pathlib.Path,required=True);p.add_argument('--destination',type=pathlib.Path,required=True);p.add_argument('--device',type=int,default=0);a=p.parse_args()
    destination=a.destination.resolve();building=destination.with_name(destination.name+'.building')
    if destination.exists() or building.exists():raise FileExistsError('Preserve existing cache/building generation; inspect it before retrying')
    required=[a.export/name for name in ('model.onnx','model.manifest.json','parity-input.r32','parity-output.r32')]
    required.extend(a.runtime/name for name in ('cudart64_12.dll','nvinfer_10.dll','nvonnxparser_10.dll','nvinfer_builder_resource_10.dll'))
    required.append(ROOT/'builds/depth-anything-v2/backend/NeuRotic.DepthAnything.dll')
    for path in required:
        if not path.is_file():raise FileNotFoundError(path)
    building.mkdir(parents=True)
    for name in ('model.onnx','model.manifest.json','parity-input.r32','parity-output.r32'):
        shutil.copyfile(a.export/name,building/name)
    for name in ('cudart64_12.dll','nvinfer_10.dll','nvonnxparser_10.dll','nvinfer_builder_resource_10.dll'):
        shutil.copyfile(a.runtime/name,building/name)
    shutil.copyfile(ROOT/'builds/depth-anything-v2/backend/NeuRotic.DepthAnything.dll',building/'NeuRotic.DepthAnything.dll')
    completed=subprocess.run([str(ROOT/'builds/depth-anything-v2/backend/NeuRotic.DepthPrepare.exe'),str(building),str(a.device)],check=False)
    if completed.returncode:raise RuntimeError('Native cache preparation failed; final cache was not replaced')
    expected=('model.onnx','engine.plan','NeuRotic.DepthAnything.dll','cudart64_12.dll','nvinfer_10.dll')
    # Builder-only dependencies are no longer required after the child exits.
    for name in ('nvonnxparser_10.dll','nvinfer_builder_resource_10.dll'):(building/name).unlink()
    publish(building,destination,expected)
    print('Verified local depth cache prepared:',destination)
if __name__=='__main__':main()
