"""Compare raw output from the same public smoke fixture with two shader builds.
No device/native work: run only after05 supplies both actual stdout files.
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

def read(path,stderr_path,exit_path):
    text=Path(path).read_text(encoding='utf-8-sig')
    cases={}
    for line in text.splitlines():
        if not line.startswith('CASE '): continue
        values=dict(re.findall(r'(\w+)=([^\s]+)',line))
        key=(line.split()[1],)+tuple(values[k] for k in ('recipe','control','exposure','mode','debug','compare'))
        assert key not in cases, f'duplicate case {key}'
        cases[key]=values
    assert len(cases)==47, f'incomplete smoke transcript: {len(cases)} cases'
    assert 'PASS: WARP debug layer clean' in text, 'debug layer clean check missing'
    assert 'DEVICE api=D3D12 adapter=explicit-WARP' in text, 'actual device receipt missing'
    totals=re.findall(r'QUALITY failures=(\d+) geometry=smoke',text)
    assert len(totals)==1, 'missing or duplicate result summary'
    edited=re.findall(r'edited_differs_from_actual_unchanged=(\d+)',text)
    assert len(edited)==4 and all(v=='1' for v in edited), 'edited delivery missing/failed'
    expected_errors=Counter()
    for key,v in cases.items():
        assert v['nonfinite']=='0' and v['alpha_errors']=='0', f'nonfinite/alpha failure {key}'
        boundary=v['mode'] in ('2','4') or v['debug']!='0' or v['compare']!='0'
        if v['fault_injected']=='0' and not boundary and v['control']!='edited' and v['changed']!='0':
            expected_errors[f'FAIL CASE {key[0]} {v["exposure"]} {v["control"]}']+=1
    stderr=Counter(line.strip() for line in Path(stderr_path).read_text(encoding='utf-8-sig').splitlines() if line.strip())
    # Reject encode-reference failures, ignored edits, setup/device diagnostics,
    # assertions and any other stderr, even if an identity also failed.
    assert stderr==expected_errors, f'failure classification mismatch: expected={expected_errors}, actual={stderr}'
    failures=int(totals[0])
    assert failures==sum(expected_errors.values()), 'summary includes failures other than identity'
    exit_code=int(Path(exit_path).read_text(encoding='utf-8-sig').strip())
    assert exit_code==(1 if failures else 0), f'unexpected process exit {exit_code}'
    return cases,failures

def main():
    if len(sys.argv)!=7:
        raise SystemExit('Usage: compare_quality_runs.py baseline.stdout baseline.stderr baseline.exit candidate.stdout candidate.stderr candidate.exit')
    baseline,red=read(*sys.argv[1:4]); candidate,green=read(*sys.argv[4:7])
    assert red>0, 'no baseline counterexample; investigate before claiming repair'
    assert green==0, f'candidate reported {green} failures'
    assert baseline.keys()==candidate.keys(), 'different test cases'
    unchanged=0; corrected=0; negatives=0
    for key,b in baseline.items():
        c=candidate[key]
        for field in ('submitted_hash','model_hash','color','work','final','mode','debug','compare','fault_injected'):
            assert b[field]==c[field], f'input/recipe mismatch {key}: {field}'
        for v in (b,c):
            assert v['nonfinite']=='0' and v['alpha_errors']=='0', f'nonfinite/alpha failure {key}'
        if c['fault_injected']=='1':
            assert b['coherent']==c['coherent']=='0', 'fault-injected pair not detected'
            negatives+=1
            continue
        assert b['coherent']==c['coherent']=='1', f'incoherent admitted pair {key}'
        excluded=c['mode'] in ('2','4') or c['debug']!='0' or c['compare']!='0'
        if excluded or c['control'] in ('edited','apply-off','strength-zero'):
            assert b['output_hash']==c['output_hash'], f'nonzero edit/excluded path changed {key}'
            unchanged+=1
        if not excluded and c['control']!='edited':
            assert c['changed']=='0', f'candidate retained-original failure {key}'
            if b['changed']!='0': corrected+=1
    assert corrected>0 and negatives==1
    print(json.dumps(dict(status='PASS_PUBLIC_SMOKE_COMPOSITOR_COMPARISON',
        baseline_failures=red,candidate_failures=green,corrected_identity_cases=corrected,
        unchanged_nonzero_or_excluded_cases=unchanged,mismatch_negatives_detected=negatives,
        limitations='Noargs signed run requires separate owner audit. Not private inference, actual Pre-SR consumer, or facial motion qualification'),indent=2))

if __name__=='__main__': main()
