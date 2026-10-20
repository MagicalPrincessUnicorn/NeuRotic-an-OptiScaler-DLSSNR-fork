"""Developer-only static batch-one exporter with optional ORT numerical verification."""
import argparse, hashlib, importlib.util, json, subprocess, sys
from pathlib import Path
from bootstrap_dependencies import digest,verify_source
# Keep the pinned upstream checkout clean across multiple exports/inferences.
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True);ap.add_argument('--checkpoint',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--width',type=int,default=518)
    ap.add_argument('--height',type=int,default=518);ap.add_argument('--verify-ort',action='store_true')
    ap.add_argument('--reuse-verified-export',action='store_true')
    a=ap.parse_args();lock=json.loads((ROOT/'dependencies.lock.json').read_text())
    if min(a.width,a.height)<14 or max(a.width,a.height)>2048 or a.width%14 or a.height%14:
        raise ValueError('Static H and W must be 14..2048 and multiples of 14')
    head=verify_source(a.source,lock['official_source']['commit'])
    if digest(a.checkpoint)!=lock['checkpoint']['sha256']:raise RuntimeError('Untrusted checkpoint hash')
    # This upstream revision does not honor XFORMERS_DISABLED. Use a clean exporter environment.
    if importlib.util.find_spec('xformers') is not None:
        raise RuntimeError('Use a dedicated exporter venv WITHOUT xformers; do not edit upstream source')
    import torch
    import numpy as np
    import onnx
    sys.path.insert(0,str(a.source.resolve()))
    from depth_anything_v2.dpt import DepthAnythingV2
    torch.set_num_threads(min(4,torch.get_num_threads()))
    torch.manual_seed(7)
    model=DepthAnythingV2(encoder='vits',features=64,out_channels=[48,96,192,384]).eval()
    model.load_state_dict(torch.load(a.checkpoint,map_location='cpu',weights_only=True),strict=True)
    x=torch.randn(1,3,a.height,a.width,dtype=torch.float32)
    with torch.inference_mode():y=model(x)
    if tuple(y.shape)!=(1,a.height,a.width) or not torch.isfinite(y).all():
        raise RuntimeError('Unexpected model output')
    existing=a.output.exists()
    if existing:
        if not a.reuse_verified_export:raise FileExistsError(a.output)
        prior=json.loads(a.output.with_suffix('.manifest.json').read_text())
        if prior['onnx_sha256']!=digest(a.output) or prior['checkpoint_sha256']!=lock['checkpoint']['sha256'] or prior['source_commit']!=head or prior['input_shape']!=[1,3,a.height,a.width]:raise RuntimeError('Existing export is not this pinned profile')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if not existing:
        torch.onnx.export(model,x,str(a.output),input_names=['image'],output_names=['relative_depth'],
                          opset_version=17,dynamo=False,do_constant_folding=True)
    onnx.checker.check_model(str(a.output))
    verification='not run'
    if a.verify_ort:
        import onnxruntime as ort
        sess=ort.InferenceSession(str(a.output),providers=['CPUExecutionProvider'])
        result=sess.run(['relative_depth'],{'image':x.numpy()})[0]
        np.testing.assert_allclose(result,y.numpy(),rtol=1e-3,atol=1e-3)
        verification='passed: deterministic synthetic FP32 tensor only; real-image and TRT parity still required'
    manifest={'schema_version':1,'model':'DepthAnythingV2SmallRelative','source_commit':head,
              'checkpoint_sha256':lock['checkpoint']['sha256'],'onnx_sha256':digest(a.output),
              'input_name':'image','output_name':'relative_depth','input_shape':[1,3,a.height,a.width],
              'output_shape':[1,a.height,a.width],'io_dtype':'float32','opset':17,
              'torch_version':torch.__version__,'onnx_version':onnx.__version__,'ort_verification':verification,
              'preprocess_contract':'display-rgb-letterbox-cubic-a-0.75-imagenet-v1',
              'qualification_status':'exported; not product/GPU qualified'}
    a.output.with_suffix('.manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    x.numpy().astype('<f4').tofile(a.output.parent/'parity-input.r32')
    y.numpy().astype('<f4').tofile(a.output.parent/'parity-output.r32')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
