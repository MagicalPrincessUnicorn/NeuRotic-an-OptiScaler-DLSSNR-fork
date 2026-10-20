"""Bounded metadata/benign-module checks; never loads the NR provider."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tests/nr/spectre'))
from owned_process import run, developer_environment_script

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provider', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--red', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = [*ROOT.glob('OptiScaler/dlssnr/CanonicalProvider*.h'),
               ROOT/'OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp',
               *ROOT.glob('tests/nr/provider/*.*'), *ROOT.glob('tools/nr/provider/*.*'),
               ROOT/'tests/nr/spectre/owned_process.py']
    def inventory():
        return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sources if p.is_file()}
    before = inventory()
    (out/'sources-before.json').write_text(json.dumps(before, indent=2)+'\n')
    env = {k.upper(): v for k, v in os.environ.items()}
    setup = out/'environment.cmd'
    setup.write_text(developer_environment_script(Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvarsall.bat')))
    result = run(['cmd.exe', '/d', '/c', str(setup)], cwd=out, env=env,
                 timeout=60, capture_output=True, text=True)
    assert not result.returncode and not result.infrastructure_error, 'ENVIRONMENT_FAILED'
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key.upper()] = value
    env['VSTEL_EWDK'] = '1'
    env.pop('CL', None); env.pop('_CL_', None)
    commands = []
    def command(label, argv, expected=0):
        with (out/(label+'.log')).open('wb') as log:
            result = run([str(a) for a in argv], cwd=out, env=env, stdout=log,
                         stderr=subprocess.STDOUT, timeout=180)
        commands.append({'label': label, 'command': [str(a) for a in argv],
                         'exit_code': result.returncode, 'expected_exit': expected,
                         'infrastructure_error': result.infrastructure_error,
                         'supervision': result.supervision})
        (out/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
        print(label+': '+str(result.returncode), flush=True)
        if result.returncode != expected or result.infrastructure_error:
            raise SystemExit('CHECK_FAILED:'+label)
    compiler = Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe')
    def compile(name, path, dll=False):
        binary = out/(name+('.dll' if dll else '.exe'))
        command(name+'-build', [compiler, '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX',
                '/O2', '/MT', '/Y-', *(['/LD'] if dll else []), ROOT/path,
                '/Fo'+str(out/(name+'.obj')), '/Fe'+str(binary), '/link', 'd3d12.lib'])
        return binary
    test = compile('canonical_identity', 'tests/nr/provider/canonical_identity.cpp')
    benign = compile('sl.common', 'tests/nr/provider/benign_module.cpp', True)
    direct = out/'direct'; direct.mkdir()
    # A hard link saves disk space; no command writes or loads these provider bytes.
    os.link(args.provider.resolve(), direct/'nvngx_dlssnr.dll')
    command('direct-only', [test, 'direct', direct/'nvngx_dlssnr.dll', benign], 20 if args.red else 0)
    if args.red:
        print('EXPECTED_OLD_GUARD_REFUSAL', flush=True)
        return
    unmatched = out/'unmatched'; unmatched.mkdir()
    (unmatched/'nvngx_dlssnr.dll').write_bytes(benign.read_bytes())
    command('direct-unpinned', [test, 'direct-unpinned', unmatched/'nvngx_dlssnr.dll', benign])
    compatible_fixture = compile('compatible_nr_fixture', 'tests/nr/provider/compatible_nr_fixture.cpp', True)
    compatible = out/'compatible'; compatible.mkdir()
    (compatible/'nvngx_dlssnr.dll').write_bytes(compatible_fixture.read_bytes())
    verify = compile('ProviderIdentity', 'tools/nr/provider/ProviderIdentity.cpp')
    forwarder = compile('nvngx.dll_dlssnr', 'OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp', True)
    forwarder_test = compile('forwarder_identity', 'tests/nr/provider/forwarder_identity.cpp')
    for mode in ['positive', 'mixed', 'late-mixed', 'missing']:
        command('reference-'+mode, [test, mode, args.provider if mode!='missing' else out/'missing/nvngx_dlssnr.dll', benign])
    command('reference-requires-whole-package', [test, 'missing', direct/'nvngx_dlssnr.dll', benign])
    for mode in ['direct-unrelated', 'direct-late-unrelated']:
        command(mode, [test, mode, direct/'nvngx_dlssnr.dll', benign])
    extra = out/'sl.unlisted.dll'; extra.write_bytes(benign.read_bytes())
    command('reference-unlisted', [test, 'mixed', args.provider, extra])
    command('direct-unlisted', [test, 'direct-unrelated', direct/'nvngx_dlssnr.dll', extra])
    conflict = out/'nvngx_dlssnr.dll'; conflict.write_bytes(benign.read_bytes())
    command('direct-conflict', [test, 'direct-conflict', direct/'nvngx_dlssnr.dll', conflict])
    command('direct-late-conflict', [test, 'direct-late-conflict', direct/'nvngx_dlssnr.dll', conflict])
    command('direct-required-handle', [test, 'direct-required-handle', direct/'nvngx_dlssnr.dll', benign])
    command('direct-missing', [test, 'direct-missing', out/'missing/nvngx_dlssnr.dll', benign])
    command('direct-benign-variant', [test, 'direct-unpinned', conflict, benign])
    tampered = out/'tampered'; tampered.mkdir()
    for path in args.provider.parent.iterdir():
        if path.name == 'nis.license.txt':
            data = path.read_bytes(); (tampered/path.name).write_bytes(bytes([data[0]^1])+data[1:])
        elif path.is_file():
            os.link(path, tampered/path.name)
    command('reference-tamper', [test, 'tampered', tampered/args.provider.name, benign])
    command('verify-direct', [verify, direct/'nvngx_dlssnr.dll'])
    command('verify-reference', [verify, '--reference-package', args.provider])
    direct_evidence = json.loads((out/'verify-direct.log').read_text())
    reference_evidence = json.loads((out/'verify-reference.log').read_text())
    assert direct_evidence['schema_version'] == 2
    assert direct_evidence['verification_scope'] == 'DIRECT_NR'
    assert direct_evidence['selection_verified'] and not direct_evidence['package_verified']
    assert direct_evidence['archive_path'] is None and len(direct_evidence['members']) == 1
    assert not direct_evidence['provider_loaded'] and not direct_evidence['dependency_completeness_established']
    assert reference_evidence['verification_scope'] == 'MATCHED_REFERENCE'
    assert reference_evidence['package_verified'] and len(reference_evidence['members']) == 15
    command('forwarder-refusal', [forwarder_test, forwarder, out/'missing/nvngx_dlssnr.dll', args.provider])
    command('forwarder-unpinned', [forwarder_test, forwarder, '--unmatched', unmatched/'nvngx_dlssnr.dll'])
    command('forwarder-compatible-unpinned', [forwarder_test, forwarder, '--unmatched-compatible', compatible/'nvngx_dlssnr.dll'])
    relocated_test = compile('forwarder_relocated', 'tests/nr/provider/forwarder_relocated.cpp')
    relocated = out/'_storage_'; relocated.mkdir()
    (relocated/'nvngx_dlssnr.dll').write_bytes(compatible_fixture.read_bytes())
    command('forwarder-relocated-reuse', [relocated_test, forwarder, compatible/'nvngx_dlssnr.dll',
                                        relocated/'nvngx_dlssnr.dll', 'same'])
    command('forwarder-relocated-conflict', [relocated_test, forwarder, compatible/'nvngx_dlssnr.dll',
                                           unmatched/'nvngx_dlssnr.dll', 'different'])
    churn_test = compile('identity_module_churn', 'tests/nr/provider/identity_module_churn.cpp')
    for mode in ['unrelated', 'optional', 'required']:
        command('module-churn-'+mode, [churn_test, mode, compatible/'nvngx_dlssnr.dll', benign])
    after = inventory()
    assert before == after, 'SOURCE_DRIFT'
    (out/'sources-after.json').write_text(json.dumps(after, indent=2)+'\n')
    receipt = {'result': 'PASS', 'source_files_unchanged': True, 'commands': len(commands),
               'provider': 'NOT_RUN', 'game': 'NOT_RUN',
               'outputs': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [test, benign, compatible_fixture, verify, forwarder, forwarder_test, relocated_test, churn_test]}}
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt), flush=True)

if __name__ == '__main__':
    main()
