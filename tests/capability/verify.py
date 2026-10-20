"""Affected CPU/integration checks, with stable individual logs and exit receipts."""
import pathlib, subprocess, json, sys
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'builds/capability-facts/verification'; out.mkdir(parents=True,exist_ok=True)
names=['NrCapability','NrConfig','NrReadiness','NrHdrObservation','NativeTemporalInputs','VulkanNrFrameParams','NrPreflightSignals','NrPreflightCell','Localization']
results=[]
for name in names:
    with (out/(name+'.log')).open('w') as log:
        result=subprocess.run(['cmd','/d','/c',str(root/'tests'/('Run-'+name+'.cmd'))],cwd=root,stdout=log,stderr=subprocess.STDOUT)
    results.append({'suite':name,'exit_code':result.returncode})
    print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(any(r['exit_code'] for r in results))
