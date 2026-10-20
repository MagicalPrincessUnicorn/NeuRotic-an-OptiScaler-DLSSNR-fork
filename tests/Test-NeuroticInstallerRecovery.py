"""Exercise installer file drift and progress in inert folders; never load a DLL."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
WORK = REPO / 'builds/installer-recovery'
TEMP = REPO.parent / '_ir'
PS = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--source-overlay', action='store_true')
    parser.add_argument('--receipt', default='recovery-test')
    parser.add_argument('--case', help='Run a focused scenario whose label contains this text.')
    parser.add_argument('--full-suite', action='store_true')
    parser.add_argument('--hardening-suite', action='store_true')
    args = parser.parse_args()
    WORK.mkdir(exist_ok=True)
    lock_path = WORK / 'fixtures.lock'
    lock = lock_path.open('x')
    assert TEMP.resolve().parent == REPO.parent.resolve() and TEMP.name == '_ir'
    if TEMP.exists():
        lock.close()
        lock_path.unlink()
        raise RuntimeError('The stable fixture folder already exists; preserve it and inspect the earlier run first.')
    TEMP.mkdir()
    checks, failures = [], []
    (WORK / (args.receipt + '.log')).write_text('', encoding='utf-8')
    try:
        with zipfile.ZipFile(args.package) as archive:
            assert archive.testzip() is None
            # Only the compact installer payload is extracted, never a build tree.
            for entry in archive.infolist():
                assert not Path(entry.filename).is_absolute() and '..' not in Path(entry.filename).parts
            archive.extractall(TEMP / 'package')
        package = next((TEMP / 'package').iterdir())
        if args.source_overlay:
            shutil.copyfile(REPO / 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1',
                            package / 'support/NeuRotic-Setup-Engine.ps1')
            manifest_path = package / 'support/PACKAGE-MANIFEST.json'
            manifest = json.loads(manifest_path.read_bytes())
            for row in manifest['files']:
                file = package / row['path'].replace('\\', '/')
                row.update(bytes=file.stat().st_size, sha256=sha(file))
            manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
        engine = package / 'support/NeuRotic-Setup-Engine.ps1'

        def run(game, *options, success=True, engine_path=engine):
            command = [PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(engine_path)]
            result = subprocess.run(command + ['-GameExecutable', str(game / 'FixtureGame.exe'), *options],
                                    capture_output=True, text=True, encoding='utf-8', input='', timeout=90,
                                    env={**os.environ, 'PSModulePath': ''})
            with (WORK / (args.receipt + '.log')).open('a', encoding='utf-8') as log:
                log.write(str(command + list(options)) + '\n' + result.stdout + result.stderr + '\n')
            diagnostics = []
            if (result.returncode == 0) != success:
                for path in game.glob('NeuRotic-backups/*/INSTALL-MANIFEST.json'):
                    record = json.loads(path.read_bytes())
                    if record.get('error'):
                        diagnostics.append({key: record.get(key) for key in ('status', 'error', 'rollback_error')})
            assert (result.returncode == 0) == success, result.stdout + result.stderr + json.dumps(diagnostics)
            return result.stdout + result.stderr

        def fixture(name):
            game = TEMP / name
            game.mkdir()
            (game / 'FixtureGame.exe').write_bytes(b'MZ inert executable')
            (game / 'nvngx_dlssnr.dll').write_bytes(b'user model')
            (game / 'personal.txt').write_bytes(b'user notes')
            return game

        def state(game):
            return json.loads((game / 'NeuRotic/Installer/Current-Install.json').read_bytes())

        def saved(game, data):
            receipts = list(game.glob('NeuRotic-preserved-files/changes-*/FILES.json'))
            for receipt in receipts:
                for entry in json.loads(receipt.read_bytes())['files']:
                    file = receipt.parent / entry['snapshot'].replace('\\', '/')
                    assert sha(file) == entry['sha256'].lower()
                    if file.read_bytes() == data:
                        return True
            return False

        def inventory(game):
            return {str(p.relative_to(game)): sha(p) for p in game.rglob('*') if p.is_file()}

        def uninstall(game):
            output = run(game, '-Uninstall', '-UninstallMode', 'Full', '-ConfirmUninstall')
            assert (game / 'personal.txt').read_bytes() == b'user notes'
            assert (game / 'nvngx_dlssnr.dll').read_bytes() == b'user model'
            return output

        def case(label, test):
            if args.case and args.case.lower() not in label.lower():
                return
            try:
                test()
                checks.append(label)
                print('PASS:', label, flush=True)
            except Exception as error:
                failures.append({'check': label, 'error': str(error)})
                print('FAIL:', label, str(error), flush=True)

        def changed_uninstall():
            game = fixture('changed-uninstall')
            (game / 'dxgi.dll').write_bytes(b'original game loader')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            (game / 'dxgi.dll').write_bytes(b'manually changed loader')
            (game / 'nvngx.dll_dlssnr.dll').write_bytes(b'manually changed forwarder')
            before = inventory(game)
            run(game, '-Uninstall', '-UninstallMode', 'Full', '-CheckOnly')
            assert before == inventory(game), 'preview changed files'
            uninstall(game)
            assert (game / 'dxgi.dll').read_bytes() == b'original game loader'
            assert not (game / 'nvngx.dll_dlssnr.dll').exists()
            assert saved(game, b'manually changed loader') and saved(game, b'manually changed forwarder')

        def changed_repair():
            game = fixture('changed-repair')
            (game / 'dxgi.dll').write_bytes(b'original loader before first install')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            original_record = state(game)['restore_chain'][0]
            (game / 'dxgi.dll').write_bytes(b'edited before repair')
            (game / 'nvngx.dll_dlssnr.dll').unlink()
            run(game, '-ExistingInstallAction', 'Repair')
            assert state(game)['restore_chain'][0] == original_record and len(state(game)['restore_chain']) == 2
            assert sha(game / 'dxgi.dll') == sha(package / 'payload/OptiScaler.dll')
            assert saved(game, b'edited before repair')
            run(game, '-ExistingInstallAction', 'Update')
            uninstall(game)
            assert (game / 'dxgi.dll').read_bytes() == b'original loader before first install'
            assert saved(game, b'edited before repair')

        def renamed_loader():
            for action in ('Uninstall', 'Repair', 'ChangeProxy'):
                game = fixture('renamed-' + action)
                run(game, '-ProxyName', 'dxgi.dll')
                (game / 'dxgi.dll').rename(game / 'winmm.dll')
                (game / 'version.dll').write_bytes(b'unrelated loader')
                if action in ('Repair', 'ChangeProxy'):
                    extra = ['-ProxyName', 'winmm.dll'] if action == 'ChangeProxy' else []
                    run(game, '-ExistingInstallAction', action, *extra)
                    selected = 'winmm.dll' if action == 'ChangeProxy' else 'dxgi.dll'
                    assert sha(game / selected) == sha(package / 'payload/OptiScaler.dll')
                    assert not (game / ('dxgi.dll' if action == 'ChangeProxy' else 'winmm.dll')).exists()
                    if action == 'ChangeProxy':
                        run(game, '-ExistingInstallAction', 'ChangeProxy', '-ProxyName', 'dbghelp.dll')
                        assert not (game / 'winmm.dll').exists(), 'proxy change revived retired relocated loader'
                uninstall(game)
                assert not (game / 'dxgi.dll').exists() and not (game / 'winmm.dll').exists()
                assert (game / 'version.dll').read_bytes() == b'unrelated loader'
                assert saved(game, (package / 'payload/OptiScaler.dll').read_bytes())

        def backup_corruption():
            game = fixture('corrupted-backup')
            (game / 'dxgi.dll').write_bytes(b'original loader')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            record = Path(state(game)['restore_chain'][0])
            (record.parent / 'previous/dxgi.dll').write_bytes(b'corrupted backup')
            (game / 'dxgi.dll').write_bytes(b'edited loader')
            before = inventory(game)
            run(game, '-Uninstall', '-UninstallMode', 'Full', '-ConfirmUninstall', success=False)
            run(game, '-ExistingInstallAction', 'Repair', success=False)
            assert before == inventory(game), 'corrupt backup permitted mutation'

        def progress():
            game = fixture('progress')
            output = run(game, '-ProxyName', 'dxgi.dll')
            assert 'Please wait' in output and 'Verifying package' in output and 'Backing up files' in output
            assert 'Installing files' in output and 'Verifying installation' in output
            output = uninstall(game)
            assert 'Please wait' in output and 'Restoring files' in output

        def historical_rename():
            game = fixture('historical-rename')
            (game / 'dxgi.dll').write_bytes(b'original external loader')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            run(game, '-ExistingInstallAction', 'ChangeProxy', '-ProxyName', 'winmm.dll')
            (game / 'dxgi.dll').unlink()
            (game / 'winmm.dll').rename(game / 'dxgi.dll')
            run(game, '-ExistingInstallAction', 'Repair')
            assert not (game / 'dxgi.dll').exists(), 'historical filename left active'
            uninstall(game)
            assert (game / 'dxgi.dll').read_bytes() == b'original external loader'
            assert not (game / 'winmm.dll').exists()

        def legacy_repair():
            game = fixture('legacy-repair')
            (game / 'dxgi.dll').write_bytes(b'original legacy baseline')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            (game / 'dxgi.dll').write_bytes(b'edited before legacy repair')
            run(game, '-ExistingInstallAction', 'Repair')
            (game / 'NeuRotic/Installer/Current-Install.json').unlink()
            uninstall(game)
            assert (game / 'dxgi.dll').read_bytes() == b'original legacy baseline'
            assert saved(game, b'edited before legacy repair')
            game = fixture('legacy-repair-after-loss')
            (game / 'dxgi.dll').write_bytes(b'original baseline before loss')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            (game / 'NeuRotic/Installer/Current-Install.json').unlink()
            (game / 'dxgi.dll').write_bytes(b'edited after manager loss')
            run(game, '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace')
            uninstall(game)
            assert (game / 'dxgi.dll').read_bytes() == b'original baseline before loss'
            assert saved(game, b'edited after manager loss')
            game = fixture('legacy-renamed-loader')
            run(game, '-ProxyName', 'dxgi.dll')
            (game / 'NeuRotic/Installer/Current-Install.json').unlink()
            (game / 'dxgi.dll').rename(game / 'winmm.dll')
            run(game, '-ExistingInstallAction', 'Repair')
            assert not (game / 'winmm.dll').exists()
            uninstall(game)
            assert not (game / 'dxgi.dll').exists() and not (game / 'winmm.dll').exists()

        def backup_races():
            game = fixture('backup-races')
            run(game, '-ProxyName', 'dxgi.dll')
            # Load actual engine functions; inject edits at the filesystem-copy boundary.
            script = TEMP / 'backup-races.ps1'
            script.write_text(r'''
param($Engine,$Game)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Engine,[ref]$tokens,[ref]$errors)
foreach($definition in $ast.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst]}) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
$allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
$GameDirectory=$Game;$GameExecutable=$null;$UninstallMode='Full';$ConfirmUninstall=$true
$CheckOnly=$false;$Installed=$false;$RemovePrivateModel=$false;$LegacyManifest=$null;$ManualProxyName=$null
$originalTransaction=${function:New-UninstallTransaction}
function New-UninstallTransaction([string]$GameRoot,[string[]]$Chain,[string[]]$AdditionalPaths=@()) {
    [IO.File]::WriteAllText((Join-Path $GameRoot 'dxgi.dll'),'edit arriving during uninstall backup')
    & $originalTransaction $GameRoot $Chain $AdditionalPaths
}
Invoke-PublicUninstall | Out-Null
''', encoding='utf-8')
            result = subprocess.run([PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script),
                                     '-Engine', str(engine), '-Game', str(game)],
                                    capture_output=True, text=True, encoding='utf-8', timeout=90, env={**os.environ, 'PSModulePath': ''})
            assert result.returncode == 0, result.stdout + result.stderr
            assert not (game / 'dxgi.dll').exists()
            assert saved(game, b'edit arriving during uninstall backup')

        case('Changed managed files uninstall to original baseline and survive full cleanup', changed_uninstall)
        case('Repair preserves original stacked history and edited bytes', changed_repair)
        case('Renamed loaders are retired by content; unrelated loaders survive', renamed_loader)
        case('Corrupt original backups refuse repair/uninstall without mutation', backup_corruption)
        case('Install and uninstall report working stages through redirected console output', progress)
        case('A loader moved back into a historical proxy filename is retired and its baseline restored', historical_rename)
        case('Missing manager state still discovers original reconciled repair lineage', legacy_repair)
        case('An edit arriving during uninstall backup survives Full cleanup', backup_races)
        if args.case and not checks and not failures:
            raise ValueError('No recovery scenario matches the requested --case filter.')
        if args.full_suite or args.hardening_suite:
            suite_options = ['-HardeningOnly'] if args.hardening_suite else []
            suite_label = 'Customer installer hardening suite' if args.hardening_suite else 'Full customer installer lifecycle suite'
            result = subprocess.run([PS, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                     str(REPO / 'tests/Test-CustomerInstaller.ps1'), '-PackageRoot', str(package),
                                     '-EvidenceRoot', str(TEMP / 's'), *suite_options],
                                    capture_output=True, text=True, encoding='utf-8', timeout=1200,
                                    env={**os.environ, 'PSModulePath': ''})
            (WORK / (args.receipt + '-lifecycle.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            if result.returncode:
                failures.append({'check': suite_label, 'error': result.stdout + result.stderr})
                print('FAIL:', suite_label, result.stdout[-2000:] + result.stderr, flush=True)
            else:
                checks.append(suite_label)
                print('PASS:', suite_label, flush=True)
        receipt = {
            'status': 'FAIL' if failures else 'PASS', 'package_sha256': sha(args.package),
            'source_overlay': args.source_overlay, 'tested_engine_sha256': sha(engine), 'checks': checks, 'failures': failures,
            'suite_scope': 'hardening-only' if args.hardening_suite else ('full' if args.full_suite else 'focused-recovery'),
            'fixture_bytes_before_cleanup': sum(p.stat().st_size for p in TEMP.rglob('*') if p.is_file()),
        }
    finally:
        assert TEMP.resolve().parent == REPO.parent.resolve() and TEMP.name == '_ir'
        if TEMP.exists():
            shutil.rmtree(TEMP)
        lock.close()
        lock_path.unlink()
    receipt['fixture_bytes_after_cleanup'] = 0
    (WORK / (args.receipt + '.json')).write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
