[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildManifest,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$ReleaseName = 'NeuRotic-Public-Installer-Candidate'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function CopyRequired([string]$Source,[string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source)) { throw "Required package source is missing: $Source" }
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

$templateRoot = $PSScriptRoot
$worktree = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $templateRoot))
$worktree = [IO.Path]::GetFullPath($worktree).TrimEnd('\')
$BuildManifest = (Resolve-Path -LiteralPath $BuildManifest).Path
$build = Get-Content -Raw -Encoding UTF8 -LiteralPath $BuildManifest | ConvertFrom-Json
$commit = (& git -C $worktree rev-parse HEAD).Trim()
$branch = (& git -C $worktree branch --show-current).Trim()
$dirty = @(& git -C $worktree status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Packaging requires a clean committed worktree.' }
if ($LASTEXITCODE -ne 0 -or $build.dirty -or $build.branch -ne $branch -or $build.status -ne 'built' -or $build.build.exit_code -ne 0 -or
    -not $build.build.source_unchanged -or $build.allowed_terminal_phase -ne 'build' -or
    [IO.Path]::GetFullPath([string]$build.worktree).TrimEnd('\') -ne $worktree -or $build.commit -ne $commit) {
    throw 'A successful immutable build-only manifest for this exact worktree and commit is required.'
}
foreach ($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')) {
    $artifact = @($build.artifacts | Where-Object { $_.name -eq $name })
    $expectedPath = Join-Path $worktree ('x64\Release\a\' + $name)
    if ($artifact.Count -ne 1 -or $artifact[0].built_path -ine $expectedPath -or
        (HashFile $expectedPath) -ne $artifact[0].sha256) { throw "Build output no longer matches the manifest: $name" }
}
if ((HashFile (Join-Path $worktree 'integration\OptiScaler.ini')) -ne $build.configuration.sha256) {
    throw 'The package INI no longer matches the recorded configuration.'
}

$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if (Test-Path -LiteralPath $OutputDirectory) { throw "Package destination already exists and was preserved: $OutputDirectory" }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

$payload = Join-Path $OutputDirectory 'payload'
$support = Join-Path $OutputDirectory 'support'
New-Item -ItemType Directory -Path $payload,$support | Out-Null
$releaseOutput = Join-Path $worktree 'x64\Release\a'
CopyRequired (Join-Path $releaseOutput 'OptiScaler.dll') (Join-Path $payload 'OptiScaler.dll')
CopyRequired (Join-Path $releaseOutput 'nvngx.dll_dlssnr.dll') (Join-Path $payload 'nvngx.dll_dlssnr.dll')
CopyRequired (Join-Path $releaseOutput 'OptiScaler') (Join-Path $payload 'OptiScaler')
CopyRequired (Join-Path $worktree 'integration\OptiScaler.ini') (Join-Path $payload 'OptiScaler.ini')
CopyRequired (Join-Path $worktree 'LICENSE') (Join-Path $payload 'NeuRotic-LICENSE.txt')
CopyRequired (Join-Path $worktree 'Licenses') (Join-Path $payload 'Licenses')
$licenseSources = @{
    'DirectX_LICENSE.txt' = 'external\directx_agility_sdk\LICENSE.txt'
    'FidelityFX_v2_LICENSE.md' = 'external\FidelityFX-SDK-v2\docs\license.md'
    'XeSS_LICENSE.txt' = 'external\xess\LICENSE.txt'
}
foreach ($name in $licenseSources.Keys) {
    CopyRequired (Join-Path $worktree $licenseSources[$name]) (Join-Path $payload ('Licenses\' + $name))
}

CopyRequired (Join-Path $templateRoot 'NeuRotic-Setup.cmd') (Join-Path $OutputDirectory 'NeuRotic-Setup.cmd')
CopyRequired (Join-Path $templateRoot 'NeuRotic-Uninstall.cmd') (Join-Path $OutputDirectory 'NeuRotic-Uninstall.cmd')
CopyRequired (Join-Path $templateRoot 'READ ME.txt') (Join-Path $OutputDirectory 'READ ME.txt')
CopyRequired (Join-Path $templateRoot 'support\NeuRotic-Setup-Engine.ps1') (Join-Path $support 'NeuRotic-Setup-Engine.ps1')
CopyRequired (Join-Path $templateRoot 'support\TESTING.txt') (Join-Path $support 'TESTING.txt')
CopyRequired $BuildManifest (Join-Path $support 'BUILD-MANIFEST.json')

if (Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Where-Object { $_.Name -eq 'nvngx_dlssnr.dll' -or $_.Extension -in @('.pdb','.ilk','.exp','.lib') }) {
    throw 'Private model or development artifacts were found in the staged public package.'
}
$files = @(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($OutputDirectory.Length+1);bytes=$_.Length;sha256=(HashFile $_.FullName)}
})
$record = [ordered]@{kind='neurotic-customer-candidate';lifecycle='public-installer-candidate';name=$ReleaseName;
    commit=$commit;branch=$branch;public_release=$false;deployed=$false;runtime_result='Inconclusive';
    decision='installer/uninstaller review candidate; no release or promotion';created_utc=[DateTime]::UtcNow.ToString('o');
    build_manifest='support\BUILD-MANIFEST.json';files=$files}
[IO.File]::WriteAllText((Join-Path $support 'PACKAGE-MANIFEST.json'),($record | ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))

foreach ($file in $record.files) {
    $path = Join-Path $OutputDirectory $file.path
    if ((HashFile $path) -ne $file.sha256) { throw "Final package verification failed: $($file.path)" }
}
Write-Output "PASS: verified public installer candidate: $OutputDirectory"
