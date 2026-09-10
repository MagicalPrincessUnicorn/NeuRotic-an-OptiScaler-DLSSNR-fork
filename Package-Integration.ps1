[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BuildManifest)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifest = Get-Content -LiteralPath $BuildManifest -Raw | ConvertFrom-Json
if ($manifest.build.exit_code -ne 0 -or -not $manifest.build.source_unchanged -or
    $manifest.allowed_terminal_phase -ne 'build' -or $manifest.worktree -ne $root) {
    throw 'A successful immutable build-only manifest for this exact worktree is required.'
}
$commit = (& git -c "safe.directory=$root" -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -ne $manifest.commit) { throw 'Build commit does not match source.' }
$status = & git -c "safe.directory=$root" -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $status) { throw 'Package only clean committed source.' }
$stage = Join-Path $root 'release\NeuRotic-present-compatibility-diagnostic'
if (Test-Path -LiteralPath $stage) { throw 'Preserving existing package: destination already exists.' }
$output = Join-Path $root 'x64\Release\a'
$pair = @('OptiScaler.dll', 'nvngx.dll_dlssnr.dll')
foreach ($name in $pair) {
    $path = Join-Path $output $name
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if (-not ($manifest.artifacts | Where-Object { $_.sha256 -eq $hash })) {
        throw "Artifact hash is not in the build manifest: $name"
    }
}
$ini = Join-Path $root 'integration\OptiScaler.ini'
if ((Get-FileHash -LiteralPath $ini).Hash -ne 'CD3D9E9908A3A61B4CDADFE2689E9444511DDADEB885CE12988C3D944A8D044D') {
    throw 'Reviewed package INI changed.'
}
foreach ($name in @('setup_linux.sh', '!! EXTRACT ALL FILES TO GAME FOLDER !!', 'Licenses', 'OptiScaler')) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $name))) { throw "Missing package input: $name" }
}
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($name in $pair + @('setup_linux.sh', '!! EXTRACT ALL FILES TO GAME FOLDER !!', 'Licenses', 'OptiScaler')) {
    Copy-Item -LiteralPath (Join-Path $output $name) -Destination $stage -Recurse
}
Copy-Item -LiteralPath $ini -Destination (Join-Path $stage 'OptiScaler.ini')
Copy-Item -LiteralPath (Join-Path $root 'setup_windows.bat') -Destination (Join-Path $stage 'NeuRotic-Setup.bat')
foreach ($name in @('NeuRotic-IniEdit.ps1', 'INSTALLATION AND NOTES.txt', 'PRESENT-COMPATIBILITY.md')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
}
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
if ($files | Where-Object { $_.Name -ieq 'nvngx_dlssnr.dll' -or $_.Extension -in @('.pdb','.lib','.exp','.ilk') }) {
    throw 'Unexpected model or development artifact in package; preserve stage for inspection.'
}
$records = @($files | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path=$_.FullName.Substring($stage.Length+1); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
})
$record = [ordered]@{ kind='Present compatibility diagnostic experiment; not a release'; branch=$manifest.branch; commit=$commit;
    source_provenance=[ordered]@{ parent='606346ea4d6ed5418428ba8d0101cc9db5f57dc5'; delta=$commit };
    package_provenance=[ordered]@{ reviewed_layer='659b9801c2a339cacaddda04a842278bd8967d6f';
        ini_sha256='CD3D9E9908A3A61B4CDADFE2689E9444511DDADEB885CE12988C3D944A8D044D';
        dlss_performance_preset=12; dlss_ultra_performance_preset=12; dlssd_presets='auto' };
    build_manifest=(Resolve-Path -LiteralPath $BuildManifest).Path; ini_disposition='package only; live untouched';
    runtime_result='Inconclusive'; decision='keep experimental'; files=$records }
[IO.File]::WriteAllText((Join-Path $stage 'PACKAGE-MANIFEST.json'), ($record | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Write-Output "PASS: local integration package at $stage"
