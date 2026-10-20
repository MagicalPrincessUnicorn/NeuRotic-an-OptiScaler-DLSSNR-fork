"""Use the existing offline worker/installer lifecycle verifier for this ZIP.

Also exercise the packaged Hub's registry/Plan route against an inert executable.
No game or GPU is launched. All extracted test material belongs to this run.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/character-inspector'))
verifier = ROOT / 'tools/character-inspector/verify_live_package.py'
source = verifier.read_text(encoding='utf-8')
source = source.replace("root / 'builds/character-inspector'", "root / 'builds/merged-package'")
source = source.replace('_ci-install-fixture', '_merged-install-fixture')
source = source.replace('live-package-receipt.json', 'package-receipt.json')
source = source.replace("    package = fixture / 'package' / folder", "    hub_root = fixture / 'package' / folder\n    package = hub_root / 'packages/current'")
marker = "    game = fixture / 'Other D3D12 Game'"
assert source.count(marker) == 1
source = source.replace(marker, '''    hub_game = fixture / 'Hub inert game'
    hub_game.mkdir()
    hub_exe = hub_game / 'FixtureGame.exe'
    shutil.copyfile('C:/Windows/System32/cmd.exe', hub_exe)
    hub_request = fixture / 'hub-request.json'
    hub_result = fixture / 'hub-result.json'
    hub_request.write_text(json.dumps({'protocolVersion':1,'requestId':'2'*32,
        'kind':'Plan','gameExecutable':str(hub_exe),'operation':'Install',
        'packageId':'flagship-approved','proxyName':'dxgi.dll','continueWithoutRuntime':True}), encoding='utf-8')
    result = subprocess.run(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
        '-NoProfile','-ExecutionPolicy','Bypass','-File',
        str(hub_root/'support/NeuRotic-Setup-Engine.ps1'),
        '-HubNonInteractive','-HubRequestPath',str(hub_request),'-HubResultPath',str(hub_result)],
        cwd=fixture, capture_output=True, text=True, encoding='utf-8', timeout=300,
        env={**os.environ, 'PSModulePath':'', 'NEUROTIC_HUB_FIXTURE_ROOT':str(fixture/'hub-user-data')})
    logs.append('packaged Hub plan\\n'+result.stdout+result.stderr)
    check('packaged Hub plan exit', result.returncode==0)
    planned=json.loads(hub_result.read_text(encoding='utf-8-sig'))
    logs.append('packaged Hub typed result\\n'+json.dumps(planned,indent=2))
    check('packaged Hub resolves merged installer', planned['status']=='Planned' and
        planned['packageDigest'].lower()==receipt_data['hub_package_manifest_sha256'])
    check('Hub plan does not install a game DLL', not (hub_game/'dxgi.dll').exists())

    bound_script = fixture/'copy-bound.ps1'
    bound_script.write_text(r"""$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($args[0],[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Packaged installer syntax errors'}
$definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Initialize-NeuroticFileSafety'},$true)
Invoke-Expression $definition.Extent.Text
Initialize-NeuroticFileSafety
$source=Join-Path $PSScriptRoot 'oversize.bin';$target=Join-Path $PSScriptRoot 'must-not-exist.bin'
$stream=[IO.File]::Open($source,[IO.FileMode]::CreateNew);try{$stream.SetLength(134217729)}finally{$stream.Dispose()}
$refused=$false
try{[NeuRoticFileSafety]::Copy($source,$target,('0'*64),'Absent')}catch{if($_.Exception.ToString().Contains('Managed copy exceeds 128 MiB')){$refused=$true}else{throw}}
if(-not $refused -or (Test-Path -LiteralPath $target)){throw 'Oversize copy did not refuse before writing'}
Write-Output 'PASS bounded copy refuses 128 MiB plus one byte without destination mutation'
""",encoding='utf-8')
    bounded=subprocess.run(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
        '-NoProfile','-ExecutionPolicy','Bypass','-File',str(bound_script),
        str(package/'support/NeuRotic-Setup-Engine.ps1')],cwd=fixture,
        capture_output=True,text=True,encoding='utf-8',timeout=60,env={**os.environ,'PSModulePath':''})
    logs.append('managed copy size guard\\n'+bounded.stdout+bounded.stderr)
    check('128 MiB managed-copy ceiling still enforced',bounded.returncode==0)

''' + marker)
exec(compile(source, str(verifier), 'exec'), {'__file__': str(verifier), '__name__': '__main__'})
