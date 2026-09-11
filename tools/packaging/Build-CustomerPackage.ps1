[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildManifest,
    [string]$OutputRoot = 'C:\OptiScaler-NR-Dev\artifacts\handoffs',
    [string]$EvidenceRoot = 'C:\OptiScaler-NR-Dev\logs\neurotic-customer-candidate'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Get-Content -Raw -Encoding UTF8 -LiteralPath $BuildManifest | ConvertFrom-Json
if ($build.status -ne 'built' -or $build.dirty -or $build.build.exit_code -ne 0 -or
    -not $build.build.source_unchanged -or $build.worktree -ne $root -or
    $build.branch -ne 'exp/neurotic-customer-candidate' -or $build.allowed_terminal_phase -ne 'build') { throw 'Unverified or mismatched build.' }
$head = & git --no-optional-locks -c "safe.directory=$root" -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $build.commit) { throw 'Build is not current source.' }
$status = & git --no-optional-locks -c "safe.directory=$root" -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $status) { throw 'Packaging requires clean source.' }
$name = 'NeuRotic-Candidate-' + $head.Substring(0,8)
$package = Join-Path $OutputRoot $name
$zip = $package + '.zip'
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $zip)) { throw 'Existing candidate preserved; choose a new output location.' }
New-Item -ItemType Directory -Path $package | Out-Null
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'customer') -Force) {
    Copy-Item -LiteralPath $file.FullName -Destination $package -Recurse
}
$payload = Join-Path $package 'payload'
New-Item -ItemType Directory -Path $payload | Out-Null
foreach ($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')) {
    $entry = @($build.artifacts | Where-Object name -eq $name)
    if ($entry.Count -ne 1 -or (Get-FileHash -LiteralPath $entry[0].retained_path).Hash -ne $entry[0].sha256) { throw 'Retained binary identity mismatch.' }
    Copy-Item -LiteralPath $entry[0].retained_path -Destination (Join-Path $payload $name)
}
foreach ($name in @('OptiScaler','Licenses')) {
    Copy-Item -LiteralPath (Join-Path $root "x64\Release\a\$name") -Destination $payload -Recurse
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $payload 'NeuRotic-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'integration\OptiScaler.ini') -Destination $payload
$ini = Get-Content -Raw -LiteralPath (Join-Path $payload 'OptiScaler.ini')
if ($ini -notmatch '(?m)^LogToFile = false\r?$' -or $ini -notmatch '(?m)^LogLevel = 2\r?$') { throw 'Customer logging defaults mismatch.' }
Copy-Item -LiteralPath $BuildManifest -Destination (Join-Path $package 'support\BUILD-MANIFEST.json')
$inventory = @(Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
    if ($_.Name -ieq 'nvngx_dlssnr.dll' -or $_.Extension -in @('.pdb','.obj','.lib','.exp','.ilk')) { throw 'Unexpected model/development file.' }
    [ordered]@{path=$_.FullName.Substring($package.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$manifest = [ordered]@{
    kind='neurotic-customer-candidate'; commit=$head; branch=$build.branch; parents=$build.parent_commits
    source_control='6b5580d3d41ffb12f63d3edd47c87e4fc177e69f'; runtime_result='Inconclusive'
    predecessor_evidence='User reports MHWilds, Dragons Dogma 2 and Crimson Desert smoke tests with no showstopper crashes observed; DD2 performance concern unresolved.'
    ini_disposition='preserve existing; fresh install uses reviewed NR/Multipass-off profile with file logging off, Info when enabled'
    ini_sha256=(Get-FileHash -LiteralPath (Join-Path $payload 'OptiScaler.ini')).Hash
    private_model='not included; preserve existing'; public_release=$false
    build_manifest='support\BUILD-MANIFEST.json'; files=$inventory
}
$manifestPath = Join-Path $package 'support\PACKAGE-MANIFEST.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding($false)))
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($package, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$extract = Join-Path $EvidenceRoot ('extracted-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)
$extracted = Join-Path $extract (Split-Path $package -Leaf)
foreach ($file in $inventory) {
    $actual = Get-Item -LiteralPath (Join-Path $extracted $file.path)
    if ($actual.Length -ne $file.bytes -or (Get-FileHash -LiteralPath $actual.FullName).Hash -ne $file.sha256) { throw 'Extracted ZIP differs from manifest.' }
}
$manifestHash = (Get-FileHash -LiteralPath $manifestPath).Hash
if ((Get-FileHash -LiteralPath (Join-Path $extracted 'support\PACKAGE-MANIFEST.json')).Hash -ne $manifestHash -or
    @(Get-ChildItem -LiteralPath $extracted -Recurse -File).Count -ne $inventory.Count + 1) { throw 'Extracted ZIP inventory mismatch.' }
$result = [ordered]@{commit=$head;package=$package;zip=$zip;zip_sha256=(Get-FileHash -LiteralPath $zip).Hash;
    zip_bytes=(Get-Item -LiteralPath $zip).Length;package_manifest_sha256=$manifestHash;extracted=$extracted;
    file_count=$inventory.Count+1;archive_roundtrip_verified=$true;runtime='Inconclusive'}
[IO.File]::WriteAllText((Join-Path $EvidenceRoot 'PACKAGE-RESULT.json'), ($result | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
$after = & git --no-optional-locks -c "safe.directory=$root" -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $after) { throw 'Source changed during packaging.' }
Write-Output "PASS package and ZIP extraction hashes: $zip"
Write-Output "Test the extracted customer package: $extracted"
