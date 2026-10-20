"""Compile tiny mutated production headers; tests must reject each unsafe gate."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
out = root / 'builds/interop-safety/mutations'
header = out / 'dlssnr/NrBridgeOutcome.h'
header.parent.mkdir(parents=True, exist_ok=True)
original = (root/'OptiScaler/dlssnr/NrBridgeOutcome.h').read_text(encoding='utf-8')
mutations = {
    'device-loss': ('return observed!=invalid && required!=invalid && observed>=required;', 'return observed>=required;'),
    'stale-identity': ('current==facts_.identity &&', ''),
    'untracked-submit': ('facts_.submitted && facts_.submissionTracked', 'facts_.submitted'),
    'failed-output-restore': ('if (work.outputRestoreFailed) Fail();', ''),
}
for name,(before,after) in mutations.items():
    assert before in original, name
    mutated=original.replace(before,after)
    if name=='untracked-submit': mutated=mutated.replace('if (!tracked) Fail();','')
    header.write_text(mutated,encoding='utf-8')
    exe=out/(name+'.exe')
    command=['cl','/nologo','/std:c++20','/EHsc','/W4','/I'+str(out),'/I'+str(root/'OptiScaler'),
        str(root/'tests/nr_bridge_outcome.cpp'),'/Fe:'+str(exe),'/Fo:'+str(out/(name+'.obj'))]
    compile_result=subprocess.run(command,cwd=root,capture_output=True,text=True)
    (out/(name+'-build.log')).write_text(compile_result.stdout+compile_result.stderr,encoding='utf-8')
    assert compile_result.returncode==0, f'{name}: compilation is not mutation evidence'
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    (out/(name+'-test.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode!=0 and 'FAIL:' in result.stderr, f'unsafe {name} mutation escaped'
    print(f'PASS: rejected {name} production-gate mutation: {result.stderr.strip()}')
header.unlink()  # temporary source fixture, never a second production header
retirement = out / 'dlssnr/NrBridgeRetirement.h'
original_retirement=(root/'OptiScaler/dlssnr/NrBridgeRetirement.h').read_text(encoding='utf-8')
before='if (!Reusable() || !use || !consumer)'
assert before in original_retirement
header.write_text(original,encoding='utf-8')
retirement.write_text(original_retirement.replace(before,'if (!use || !consumer)'),encoding='utf-8')
exe=out/'cache-takeover.exe'
command=['cl','/nologo','/std:c++20','/EHsc','/W4','/I'+str(out),'/I'+str(root/'OptiScaler'),
    '/I'+str(root/'OptiScaler/dlssnr'),'/I'+str(root/'OptiScaler/include'),
    str(root/'tests/nr_bridge_retirement.cpp'),'/Fe:'+str(exe),'/Fo:'+str(out/'cache-takeover.obj'),
    '/link','d3d11.lib','d3d12.lib','dxgi.lib',str(root/'OptiScaler/library/detours/detours.lib')]
compiled=subprocess.run(command,cwd=root,capture_output=True,text=True)
(out/'cache-takeover-build.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
assert compiled.returncode==0,'cache takeover mutation must compile'
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
(out/'cache-takeover-test.log').write_text(result.stdout+result.stderr,encoding='utf-8')
assert result.returncode!=0 and 'replacement writer cannot steal' in result.stderr
print(f'PASS: rejected cache-takeover production-gate mutation: {result.stderr.strip()}')
retirement.unlink(); header.unlink()
parameter_header=out/'dlssnr/NrBridgeParameters.h'
parameter_source=(root/'OptiScaler/dlssnr/NrBridgeParameters.h').read_text(encoding='utf-8')
assert 'if (bridge)' in parameter_source
parameter_header.write_text(parameter_source.replace('if (bridge)','if (false)'),encoding='utf-8')
exe=out/'stale-typed-lane.exe'
compiled=subprocess.run(['cl','/nologo','/std:c++20','/EHsc','/W4','/I'+str(out),'/I'+str(root/'OptiScaler'),
    str(root/'tests/nr_bridge_parameters.cpp'),'/Fe:'+str(exe),'/Fo:'+str(out/'stale-typed-lane.obj')],
    cwd=root,capture_output=True,text=True)
(out/'stale-typed-lane-build.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
assert compiled.returncode==0,'pointer-lane mutation must compile'
result=subprocess.run([str(exe)],capture_output=True,text=True)
(out/'stale-typed-lane-test.log').write_text(result.stdout+result.stderr,encoding='utf-8')
assert result.returncode!=0 and 'bridge ignores stale typed/alias lanes' in result.stderr
parameter_header.unlink()
print('PASS: rejected stale typed parameter lane mutation')
print('PASS: 6 unsafe publication/completion/cache/binding mutations rejected')
