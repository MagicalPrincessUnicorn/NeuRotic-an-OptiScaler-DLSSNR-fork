[CmdletBinding()]
param(
    [string]$GameExecutable,
    [ValidateSet('dxgi.dll','winmm.dll','d3d12.dll','dbghelp.dll','version.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ProxyName,
    [switch]$CheckOnly
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function WriteRecord([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
}

$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json') -Raw | ConvertFrom-Json
if ($manifest.kind -ne 'neurotic-multipass-review-fixes-test' -or
    $manifest.ini_disposition -ne 'preserve-live' -or
    $manifest.commit -notmatch '^[a-f0-9]{40}$') { throw 'Invalid test-bundle manifest.' }
$pair = @('OptiScaler.dll','nvngx.dll_dlssnr.dll')
foreach ($name in $pair) {
    $entries = @($manifest.files | Where-Object { $_.name -ceq $name })
    if ($entries.Count -ne 1 -or $entries[0].sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        (HashFile (Join-Path $PSScriptRoot $name)) -ne $entries[0].sha256) {
        throw "Package verification failed: $name. No game files were changed."
    }
}
if (-not $GameExecutable) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'Select the game executable beside your existing NeuRotic installation'
    $picker.Filter = 'Game executable (*.exe)|*.exe'
    try {
        if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw 'Installation cancelled.' }
        $GameExecutable = $picker.FileName
    } finally { $picker.Dispose() }
}
$GameExecutable = (Resolve-Path -LiteralPath $GameExecutable).Path
if ([IO.Path]::GetExtension($GameExecutable) -ine '.exe') { throw 'Select the actual game .exe.' }
$gameDir = Split-Path -Parent $GameExecutable
if ($gameDir -eq $PSScriptRoot) { throw 'Keep this bundle outside the game folder, then run it again.' }
$processName = [IO.Path]::GetFileNameWithoutExtension($GameExecutable)
if (Get-Process -Name $processName -ErrorAction SilentlyContinue) { throw 'Close the game before installing.' }
$ini = Join-Path $gameDir 'OptiScaler.ini'
$model = Join-Path $gameDir 'nvngx_dlssnr.dll'
foreach ($path in @($ini,$model,(Join-Path $gameDir 'nvngx.dll_dlssnr.dll'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "This update requires an existing working installation. Missing: $path"
    }
}
$allowed = @('dxgi.dll','winmm.dll','d3d12.dll','dbghelp.dll','version.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
if (-not $ProxyName) {
    $found = @($allowed | Where-Object {
        $path = Join-Path $gameDir $_
        (Test-Path -LiteralPath $path -PathType Leaf) -and
        (Get-Item -LiteralPath $path).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
    })
    if ($found.Count -ne 1) {
        throw "Expected one existing OptiScaler proxy, found $($found.Count): $($found -join ', '). Use -ProxyName to select your installed OptiScaler filename."
    }
    $ProxyName = $found[0]
}
$main = Join-Path $gameDir $ProxyName
if (-not (Test-Path -LiteralPath $main -PathType Leaf) -or
    (Get-Item -LiteralPath $main).VersionInfo.OriginalFilename -ine 'OptiScaler.dll') {
    throw "The selected proxy is not identified as OptiScaler: $main"
}
$preserved = @(
    [ordered]@{path=$ini; sha256=(HashFile $ini)},
    [ordered]@{path=$model; sha256=(HashFile $model)}
)
$targets = @(
    [ordered]@{name=$ProxyName; package_name='OptiScaler.dll'; path=$main},
    [ordered]@{name='nvngx.dll_dlssnr.dll'; package_name='nvngx.dll_dlssnr.dll'; path=(Join-Path $gameDir 'nvngx.dll_dlssnr.dll')}
)
Write-Output "Test commit: $($manifest.commit)"
Write-Output "Game: $GameExecutable"
Write-Output "Main proxy: $ProxyName"
Write-Output 'INI and NR model: preserve existing bytes. Multipass settings remain as saved.'
if ($CheckOnly) { Write-Output 'PASS: package and destination verified; no files changed.'; return }

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$backup = Join-Path $gameDir "NeuRotic-test-backups\multipass-review-$runId"
New-Item -ItemType Directory -Path $backup | Out-Null
$record = [ordered]@{
    kind='NeuRotic multipass test installation'; status='backing-up'; commit=$manifest.commit;
    parent=$manifest.parent; branch=$manifest.branch; game_executable=$GameExecutable;
    ini_disposition='preserve-live'; preserved=$preserved; backup=$backup;
    started_utc=[DateTime]::UtcNow.ToString('o'); replaced=@(); installed=@(); runtime='Inconclusive'
}
$recordPath = Join-Path $backup 'INSTALL-MANIFEST.json'
foreach ($target in $targets) {
    $oldHash = HashFile $target.path
    $saved = Join-Path $backup $target.name
    Copy-Item -LiteralPath $target.path -Destination $saved
    if ((HashFile $saved) -ne $oldHash) { throw "Backup verification failed: $($target.name)" }
    $record.replaced += [ordered]@{name=$target.name; sha256=$oldHash}
}
Copy-Item -LiteralPath $ini -Destination (Join-Path $backup 'OptiScaler.ini.snapshot')
WriteRecord $recordPath $record
# The same tested rollback helper is copied beside the exact verified old pair.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Restore-MultipassTest.ps1') -Destination $backup
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Restore-MultipassTest.cmd') -Destination $backup
$record.status = 'installing'; WriteRecord $recordPath $record
try {
    if (Get-Process -Name $processName -ErrorAction SilentlyContinue) { throw 'The game started; update cancelled.' }
    foreach ($target in $targets) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $target.package_name) -Destination $target.path -Force
        $expected = @($manifest.files | Where-Object { $_.name -ceq $target.package_name })[0].sha256
        $actual = HashFile $target.path
        if ($actual -ne $expected) { throw "Installed hash mismatch: $($target.name)" }
        $record.installed += [ordered]@{name=$target.name; sha256=$actual}
    }
    foreach ($file in $preserved) {
        if ((HashFile $file.path) -ne $file.sha256) { throw "Preserved file changed externally: $($file.path)" }
    }
    $record.status = 'installed-verified'; WriteRecord $recordPath $record
} catch {
    $record.error = $_.Exception.Message
    try {
        foreach ($target in $targets) {
            $expected = @($record.replaced | Where-Object { $_.name -ceq $target.name })[0].sha256
            if ((HashFile $target.path) -eq $expected) { continue }
            Copy-Item -LiteralPath (Join-Path $backup $target.name) -Destination $target.path -Force
            if ((HashFile $target.path) -ne $expected) { throw "Rollback hash mismatch: $($target.name)" }
        }
        $record.status = 'failed-rolled-back'
    } catch { $record.status = 'failed-rollback-incomplete'; $record.rollback_error = $_.Exception.Message }
    WriteRecord $recordPath $record
    throw "Update failed ($($record.status)). See $recordPath"
}
Write-Output "PASS: matched pair installed and verified. Backup: $backup"
Write-Output 'To revert this test, close the game and run Restore-MultipassTest.cmd inside that backup folder.'
