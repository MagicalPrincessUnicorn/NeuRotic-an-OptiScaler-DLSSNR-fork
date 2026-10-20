"""Compare raw .f32 tensors. Reports raw error AND affine-invariant diagnostics."""
import argparse,json
from pathlib import Path
import numpy as np

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path)
    p.add_argument('candidate',type=Path);p.add_argument('--max-relative-rmse',type=float,default=.02)
    a=p.parse_args();r=np.fromfile(a.reference,dtype='<f4');c=np.fromfile(a.candidate,dtype='<f4')
    if not r.size or r.shape!=c.shape or not np.isfinite(r).all() or not np.isfinite(c).all():
        raise ValueError('Shape mismatch, empty output, or nonfinite depth')
    span=float(np.quantile(r,.98)-np.quantile(r,.02))
    if span<=1e-6:raise ValueError('Flat reference is unsuitable for accuracy qualification')
    raw_rmse=float(np.sqrt(np.mean((c.astype('float64')-r)**2)))
    fit=np.linalg.lstsq(np.stack([c,np.ones_like(c)],1),r,rcond=None)[0]
    aligned=c*fit[0]+fit[1]
    rng=np.random.default_rng(7);i=rng.integers(0,len(r),10000);j=rng.integers(0,len(r),10000)
    mask=np.abs(r[i]-r[j])>.05*span
    rank=float(np.mean(np.sign(r[i[mask]]-r[j[mask]])==np.sign(c[i[mask]]-c[j[mask]]))) if mask.any() else None
    passed=raw_rmse/span<=a.max_relative_rmse
    print(json.dumps({'raw_rmse':raw_rmse,'raw_rmse_over_reference_2_98_span':raw_rmse/span,
                      'affine_fit_scale':float(fit[0]),'affine_fit_shift':float(fit[1]),
                      'aligned_rmse':float(np.sqrt(np.mean((aligned-r)**2))),
                      'sampled_order_agreement':rank,'numeric_gate_passed':passed,
                      'note':'This numeric gate does not establish video stability or NR acceptance.'},indent=2))
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
