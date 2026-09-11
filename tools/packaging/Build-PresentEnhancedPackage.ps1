[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildManifest,
    [string]$OutputRoot = 'C:\OptiScaler-NR-Dev\artifacts\handoffs'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Get-Content -Raw -LiteralPath $BuildManifest | ConvertFrom-Json
if ($build.status -ne 'built' -or $build.dirty -or $build.build.exit_code -ne 0 -or
    -not $build.build.source_unchanged -or $build.worktree -ne $root -or
    $build.branch -ne 'exp/present-enhanced-resolution' -or $build.allowed_terminal_phase -ne 'build') {
    throw 'A clean verified build of this experiment is required.'
}
$head = & git --no-optional-locks -c "safe.directory=$root" -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $build.commit) { throw 'Build differs from current source.' }
$status = & git --no-optional-locks -c "safe.directory=$root" -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $status) { throw 'Source must be clean.' }
$package = Join-Path $OutputRoot ('NeuRotic-Present-Enhanced-Resolution-EXPERIMENT-' + $head.Substring(0,8))
if (Test-Path -LiteralPath $package) { throw 'Existing handoff preserved; choose a new output root.' }
New-Item -ItemType Directory -Path $package | Out-Null
# The exact e7d46b89 customer installer/restore implementation is reused unchanged.
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'customer') -Force) {
    Copy-Item -LiteralPath $file.FullName -Destination $package -Recurse
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'present-enhanced\READ ME.txt') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\PRESENT-ENHANCED-RESOLUTION.md') -Destination (Join-Path $package 'support\EXPERIMENT.md')
$payload = Join-Path $package 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
foreach ($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')) {
    $entry = @($build.artifacts | Where-Object name -eq $name)
    if ($entry.Count -ne 1 -or (Get-FileHash -LiteralPath $entry[0].retained_path).Hash -ne $entry[0].sha256) {
        throw 'Retained build artifact identity mismatch.'
    }
    Copy-Item -LiteralPath $entry[0].retained_path -Destination (Join-Path $payload $name)
}
foreach ($name in @('OptiScaler','Licenses')) {
    Copy-Item -LiteralPath (Join-Path $root "x64\Release\a\$name") -Destination $payload -Recurse
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $payload 'NeuRotic-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'integration\OptiScaler.ini') -Destination $payload
Copy-Item -LiteralPath $BuildManifest -Destination (Join-Path $package 'support\BUILD-MANIFEST.json')
$inventory = @(Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
    if ($_.Name -ieq 'nvngx_dlssnr.dll' -or $_.Extension -in @('.pdb','.obj','.lib','.exp','.ilk')) {
        throw 'Private model or development output in package.'
    }
    [ordered]@{path=$_.FullName.Substring($package.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$manifest = [ordered]@{
    kind='neurotic-customer-candidate' # Existing installer schema; lifecycle below is authoritative.
    lifecycle='experiment'; name=(Split-Path $package -Leaf); commit=$head; branch=$build.branch
    parents=$build.parent_commits; source_control='e7d46b899740ed25c78f11ae484366a2ba049b75'
    guide_source='885901ecd2ca3b2c99b813211c297a963971b8bd'
    runtime_result='Inconclusive'; decision='keep experimental'; public_release=$false; deployed=$false
    ini_disposition='unchanged candidate profile for fresh installs; preserve existing settings/model; only explicit inherited ReShade choice changes LoadReshade'
    ini_sha256=(Get-FileHash -LiteralPath (Join-Path $payload 'OptiScaler.ini')).Hash
    build_manifest='support\BUILD-MANIFEST.json'; files=$inventory
}
$manifestPath = Join-Path $package 'support\PACKAGE-MANIFEST.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding($false)))
foreach ($file in $inventory) {
    if ((Get-FileHash -LiteralPath (Join-Path $package $file.path)).Hash -ne $file.sha256) { throw 'Package hash mismatch.' }
}
$status = & git --no-optional-locks -c "safe.directory=$root" -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $status) { throw 'Source changed during packaging.' }
Write-Output "PASS experimental handoff: $package"
Write-Output "Manifest SHA256: $((Get-FileHash -LiteralPath $manifestPath).Hash)"
Write-Output 'No ZIP, live deployment, candidate replacement or stable promotion.'
