[CmdletBinding()]
param()
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
$record = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
if ($record.kind -ne 'NeuRotic multipass test installation' -or $record.replaced.Count -ne 2) {
    throw 'This is not a complete multipass-test backup.'
}
$gameDir = Split-Path -Parent $record.game_executable
$allowed = @('dxgi.dll','winmm.dll','d3d12.dll','dbghelp.dll','version.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll','nvngx.dll_dlssnr.dll')
if (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($record.game_executable)) -ErrorAction SilentlyContinue) {
    throw 'Close the game before restoring.'
}
foreach ($file in $record.replaced) {
    if ($file.name -notin $allowed -or (HashFile (Join-Path $PSScriptRoot $file.name)) -ne $file.sha256) {
        throw 'Backup verification failed. No files were changed.'
    }
}
# Avoid replacing a later, unrelated installation. This backup restores only its own test.
foreach ($file in $record.installed) {
    if ($file.name -notin $allowed -or (HashFile (Join-Path $gameDir $file.name)) -ne $file.sha256) {
        throw 'The installed pair has changed since this test. Preserve the newer installation before restoring.'
    }
}
if ($record.status -ne 'installed-verified' -or $record.installed.Count -ne 2) {
    throw 'Automatic restore requires a verified installation from this backup.'
}
$ini = Join-Path $gameDir 'OptiScaler.ini'; $model = Join-Path $gameDir 'nvngx_dlssnr.dll'
$iniHash = HashFile $ini; $modelHash = HashFile $model
$undo = Join-Path $PSScriptRoot ('before-restore-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $undo | Out-Null
foreach ($file in $record.installed) {
    Copy-Item -LiteralPath (Join-Path $gameDir $file.name) -Destination $undo
    if ((HashFile (Join-Path $undo $file.name)) -ne $file.sha256) { throw 'Restore safety backup failed.' }
}
try {
    foreach ($file in $record.replaced) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file.name) -Destination (Join-Path $gameDir $file.name) -Force
        if ((HashFile (Join-Path $gameDir $file.name)) -ne $file.sha256) { throw 'Restored hash mismatch.' }
    }
    if ((HashFile $ini) -ne $iniHash -or (HashFile $model) -ne $modelHash) { throw 'Preserved file changed externally.' }
} catch {
    foreach ($file in $record.installed) {
        Copy-Item -LiteralPath (Join-Path $undo $file.name) -Destination (Join-Path $gameDir $file.name) -Force
        if ((HashFile (Join-Path $gameDir $file.name)) -ne $file.sha256) { throw "Restore rollback failed; saved pair: $undo" }
    }
    throw
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'RESTORED.txt'), [DateTime]::UtcNow.ToString('o'))
Write-Output 'PASS: previous pair restored; current INI and NR model preserved.'
