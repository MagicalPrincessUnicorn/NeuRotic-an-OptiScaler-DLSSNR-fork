"""Developer-only FP32 PyTorch oracle on the exact prepared tensor, not a video runtime."""
import argparse, importlib.util, json, subprocess, sys
from pathlib import Path
from bootstrap_dependencies import digest,verify_source
# Keep the pinned upstream checkout clean across multiple exports/inferences.
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True)
    a=p.parse_args();lock=json.loads((ROOT/'dependencies.lock.json').read_text())
    if min(a.width,a.height)<14 or max(a.width,a.height)>2048 or a.width%14 or a.height%14:
        raise ValueError('Invalid patch-aligned dimensions')
    head=verify_source(a.source,lock['official_source']['commit'])
    if digest(a.checkpoint)!=lock['checkpoint']['sha256']:raise RuntimeError('Checkpoint hash mismatch')
    if importlib.util.find_spec('xformers') is not None:raise RuntimeError('Use an exporter venv without xformers')
    if a.output.exists():raise FileExistsError(a.output)
    import numpy as np
    import torch
    sys.path.insert(0,str(a.source.resolve()))
    from depth_anything_v2.dpt import DepthAnythingV2
    x=np.fromfile(a.input,dtype='<f4')
    if x.size!=3*a.width*a.height or not np.isfinite(x).all():raise ValueError('Invalid input tensor')
    model=DepthAnythingV2(encoder='vits',features=64,out_channels=[48,96,192,384]).eval()
    model.load_state_dict(torch.load(a.checkpoint,map_location='cpu',weights_only=True),strict=True)
    torch.set_num_threads(min(4,torch.get_num_threads()))
    with torch.inference_mode():y=model(torch.from_numpy(x.reshape(1,3,a.height,a.width)))
    if tuple(y.shape)!=(1,a.height,a.width) or not torch.isfinite(y).all():raise RuntimeError('Invalid reference output')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    y.numpy().astype('<f4').tofile(a.output)
    print(json.dumps({'status':'FP32_REFERENCE_WRITTEN','source_commit':head,
        'checkpoint_sha256':lock['checkpoint']['sha256'],'input_sha256':digest(a.input),
        'output_sha256':digest(a.output),'torch_version':torch.__version__},indent=2))
if __name__=='__main__':main()
