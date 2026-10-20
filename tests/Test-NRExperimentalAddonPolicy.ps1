param([Parameter(Mandatory=$true)][string]$AppRoot,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$AppRoot=[IO.Path]::GetFullPath($AppRoot)
. (Join-Path $AppRoot 'support/NeuRotic-InstallCore.ps1')
$manifest=Get-Content (Join-Path $AppRoot 'packages/current/support/PACKAGE-MANIFEST.json') -Raw|ConvertFrom-Json
$addon=@($manifest.files|Where-Object{$_.path -ceq 'payload/NeuRotic-PreparedGuides.addon64'})[0]
$root=Join-Path (Split-Path $PSScriptRoot) 'builds/experimental-app-fixture/addon-byte-check'
if(Test-Path -LiteralPath $root){throw 'Fixture already exists'}
New-Item -ItemType Directory -Path (Join-Path $root 'payload'),(Join-Path $root 'game') -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $root 'payload/NeuRotic-PreparedGuides.addon64'),'unrecognized addon bytes')
$manifest.files=@($addon)
$package=[pscustomobject]@{root=$root;manifest=$manifest}
$profile=[pscustomobject]@{proxy='dxgi.dll';configurationPaths=@()}
$refused=$false
try{Get-NeuroticInstallActions (Join-Path $root 'game') $package $profile $false $null $null|Out-Null}catch{$refused=$_.Exception.Message -eq 'Experimental prepared-input addon does not match this package.'}
if(-not $refused){throw 'Declared hash must not admit modified addon bytes'}
if(@(Get-ChildItem (Join-Path $root 'game') -Force).Count){throw 'Refused plan wrote a game file'}
[IO.File]::WriteAllText((Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) 'addon-byte-check.json'),'{"modifiedAddonRefused":true,"noGameWrites":true}')
Write-Output 'PASS declared addon hash cannot admit modified source bytes; no game writes'
