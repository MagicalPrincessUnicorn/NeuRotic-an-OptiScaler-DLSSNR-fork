[CmdletBinding()]
param(
    [string]$GameExecutable,
    [ValidateSet('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ProxyName,
    [switch]$CheckOnly,
    [switch]$ConfirmInstall,
    [switch]$Restore
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function SaveRecord([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
}
function SafePath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative -match '[:*?]' -or @($Relative -split '[\\/]' | Where-Object { $_ -in @('..','.','') }).Count) {
        throw "Invalid relative path: $Relative"
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path leaves destination.' }
    $walk = $path
    while ($walk) {
        if (Test-Path -LiteralPath $walk) {
            if ((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked destinations are not supported: $walk"
            }
        }
        $walk = Split-Path -Parent $walk
    }
    return $path
}
function CopyVerified([string]$Source, [string]$Destination, [string]$Hash) {
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    if ((HashFile $Destination) -ne $Hash) { throw "Copied file verification failed: $Destination" }
}
function CheckGameClosed([string]$Executable) {
    if (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Executable)) -ErrorAction SilentlyContinue) {
        throw 'Close the game before continuing.'
    }
}
$allowedProxies = @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
function AllowedTarget([string]$Relative) {
    return ($Relative -in $allowedProxies -or $Relative -in @('nvngx.dll_dlssnr.dll','OptiScaler.ini','NeuRotic-LICENSE.txt') -or
        $Relative -match '^(OptiScaler|Licenses)\\[^:]+$')
}

if ($Restore) {
    $recordPath = Join-Path $PSScriptRoot 'INSTALL-MANIFEST.json'
    $record = Get-Content -Raw -Encoding UTF8 -LiteralPath $recordPath | ConvertFrom-Json
    if ($record.kind -ne 'neurotic-customer-candidate-install' -or $record.status -ne 'installed-verified' -or
        $record.backup -ne $PSScriptRoot -or $record.files.Count -lt 2) { throw 'This is not a completed installation backup.' }
    $gameDir = Split-Path -Parent $record.game_executable
    CheckGameClosed $record.game_executable
    foreach ($file in $record.files) {
        if (-not (AllowedTarget $file.path)) { throw 'Unexpected backup target.' }
        $target = SafePath $gameDir $file.path
        if ($file.path -eq 'OptiScaler.ini') { continue }
        if ((HashFile $target) -ne $file.installed_hash) { throw "A file has changed since installation: $target. Restore stopped." }
        if ($file.existed -and (HashFile (SafePath (Join-Path $PSScriptRoot 'previous') $file.path)) -ne $file.previous_hash) {
            throw 'A backup file failed verification.'
        }
    }
    if ($CheckOnly) { Write-Output 'PASS: restore preflight; no files changed.'; return }
    if (-not $ConfirmInstall -and (Read-Host 'Restore previous files? Current INI and model stay unchanged. Type RESTORE') -cne 'RESTORE') {
        Write-Output 'Restore cancelled.'; return
    }
    $undo = Join-Path $PSScriptRoot ('restore-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $undo | Out-Null
    foreach ($file in $record.files) {
        if ($file.path -eq 'OptiScaler.ini') { continue }
        CopyVerified (SafePath $gameDir $file.path) (SafePath $undo $file.path) $file.installed_hash
    }
    $touched = New-Object System.Collections.Generic.List[object]
    try {
        CheckGameClosed $record.game_executable
        foreach ($file in $record.files) {
            if ($file.path -eq 'OptiScaler.ini') { continue }
            $target = SafePath $gameDir $file.path
            $touched.Add($file)
            if ($file.existed) {
                CopyVerified (SafePath (Join-Path $PSScriptRoot 'previous') $file.path) $target $file.previous_hash
            } else { Remove-Item -LiteralPath $target -Force }
        }
    } catch {
        foreach ($file in $touched) { CopyVerified (SafePath $undo $file.path) (SafePath $gameDir $file.path) $file.installed_hash }
        throw
    }
    $record.status = 'restored'; SaveRecord $recordPath $record
    Write-Output "PASS: previous files restored; INI and model preserved. Removed test files remain recoverable in: $undo"
    return
}

$packageRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$manifestPath = Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json'
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.kind -ne 'neurotic-customer-candidate' -or
    $manifest.commit -notmatch '^[a-f0-9]{40}$') { throw 'Unexpected package identity.' }
$seen = @{}
foreach ($file in $manifest.files) {
    if ($seen.ContainsKey($file.path)) { throw 'Duplicate package path.' }; $seen[$file.path] = $true
    $source = SafePath $packageRoot $file.path
    if ($file.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or (HashFile $source) -ne $file.sha256) {
        throw "Package verification failed: $($file.path)"
    }
}
foreach ($required in @('payload\OptiScaler.dll','payload\nvngx.dll_dlssnr.dll','payload\OptiScaler.ini','support\NeuRotic-Setup-Engine.ps1','support\BUILD-MANIFEST.json','NeuRotic-Setup.cmd')) {
    if (-not $seen.ContainsKey($required)) { throw "Missing manifest entry: $required" }
}
$build = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $PSScriptRoot 'BUILD-MANIFEST.json') | ConvertFrom-Json
if ($build.commit -ne $manifest.commit -or $build.status -ne 'built' -or $build.build.exit_code -ne 0) { throw 'Build identity mismatch.' }
if (-not $GameExecutable) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'NeuRotic Setup - select your game executable'
    $picker.Filter = 'Game executable (*.exe)|*.exe'
    try {
        if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Output 'Installation cancelled.'; return }
        $GameExecutable = $picker.FileName
    } finally { $picker.Dispose() }
}
$GameExecutable = (Resolve-Path -LiteralPath $GameExecutable).Path
if ([IO.Path]::GetExtension($GameExecutable) -ine '.exe') { throw 'Select a game executable.' }
$gameDir = Split-Path -Parent $GameExecutable
if ($gameDir -eq $packageRoot -or $gameDir.StartsWith($packageRoot + '\',[StringComparison]::OrdinalIgnoreCase) -or
    $packageRoot.StartsWith($gameDir + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep the complete installer folder outside the game folder.'
}
CheckGameClosed $GameExecutable
$found = @($allowedProxies | Where-Object {
    $candidate = SafePath $gameDir $_
    (Test-Path -LiteralPath $candidate -PathType Leaf) -and
        (Get-Item -LiteralPath $candidate).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
})
if ($found.Count -gt 1) { throw "Multiple OptiScaler proxies found: $($found -join ', '). Resolve the existing install first." }
if (-not $ProxyName) {
    if ($found.Count -eq 1) { $ProxyName = $found[0] }
    else {
        Write-Output 'Choose the graphics API used by the game:'
        Write-Output '  1 - DirectX (dxgi.dll)'
        Write-Output '  2 - Vulkan (winmm.dll, including Indiana Jones)'
        Write-Output ('  Or enter a supported proxy filename: ' + ($allowedProxies -join ', '))
        $choice = (Read-Host 'Enter 1 or 2, or a proxy filename').Trim()
        $ProxyName = switch ($choice) {
            '1' { 'dxgi.dll' }
            '2' { 'winmm.dll' }
            default { $choice }
        }
    }
}
if ($ProxyName -notin $allowedProxies) { throw 'Unsupported proxy filename.' }
if ($found.Count -eq 1 -and $ProxyName -ine $found[0]) { throw 'Keep the existing OptiScaler proxy filename for this update.' }
$main = SafePath $gameDir $ProxyName
if ((Test-Path -LiteralPath $main) -and $found.Count -eq 0) { throw "Another file owns $ProxyName. Select an unused supported filename." }
$files = @()
foreach ($file in $manifest.files) {
    if (-not $file.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)) { continue }
    $relative = $file.path.Substring(8)
    if ($relative -eq 'OptiScaler.dll') { $relative = $ProxyName }
    if (-not (AllowedTarget $relative)) { throw "Unexpected payload target: $relative" }
    $target = SafePath $gameDir $relative
    $exists = Test-Path -LiteralPath $target -PathType Leaf
    if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "A directory occupies a file destination: $target" }
    if ($relative -eq 'OptiScaler.ini' -and $exists) { continue }
    $files += [ordered]@{path=$relative; source=$file.path; existed=$exists;
        previous_hash=$(if ($exists) { HashFile $target } else { $null }); installed_hash=$file.sha256}
}
$preserved = @()
foreach ($name in @('OptiScaler.ini','nvngx_dlssnr.dll')) {
    $path = SafePath $gameDir $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { $preserved += @{path=$name;sha256=(HashFile $path)} }
}
Write-Output ''
Write-Output ('NeuRotic - Candidate ' + $manifest.commit.Substring(0,8))
Write-Output "Game: $GameExecutable"
Write-Output "Proxy: $ProxyName"
if (@($preserved | Where-Object { $_.path -eq 'OptiScaler.ini' }).Count) {
    Write-Output 'Settings: keep your existing OptiScaler.ini exactly as it is.'
} else {
    Write-Output 'Settings: install the included default profile (NR off; Multipass off; file logging off; DLSS Performance/Ultra Performance Preset L).'
}
Write-Output 'Your existing NVIDIA NR model is preserved. Backups and a Restore.cmd are created before replacement.'
Write-Output 'This candidate combines the updated UI, up to 10 D3D12 NR passes, the Present flicker fix, and Indiana Jones Vulkan NR support.'
Write-Output 'Start with one pass. Extra passes require DirectX 12; try two first. Shared Model Strength and Detail Strength control the selected child passes.'
if (-not (Test-Path -LiteralPath (SafePath $gameDir 'nvngx_dlssnr.dll') -PathType Leaf)) {
    Write-Output 'NR model missing: supply your own nvngx_dlssnr.dll beside the game executable before using NR.'
}
if ($CheckOnly) { Write-Output 'PASS: full installer preflight; no files changed.'; return }
if (-not $ConfirmInstall -and (Read-Host 'Install this test candidate with the settings shown above? Type INSTALL') -cne 'INSTALL') {
    Write-Output 'Installation cancelled.'; return
}
$run = [Guid]::NewGuid().ToString('N').Substring(0,12)
$backup = SafePath $gameDir ("NeuRotic-test-backups\candidate-$run")
foreach ($file in $files) {
    $restorePath = SafePath (Join-Path $backup 'restore-00000000') $file.path
    if ($restorePath.Length -ge 248) { throw 'The game folder path is too long for reliable Windows PowerShell backup/restore. No game files were changed.' }
}
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($file in $files) {
    if ($file.existed) { CopyVerified (SafePath $gameDir $file.path) (SafePath (Join-Path $backup 'previous') $file.path) $file.previous_hash }
}
foreach ($file in $preserved) {
    if ($file.path -eq 'OptiScaler.ini') { CopyVerified (SafePath $gameDir $file.path) (Join-Path $backup 'OptiScaler.ini.snapshot') $file.sha256 }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1') -Destination $backup
[IO.File]::WriteAllText((Join-Path $backup 'Restore.cmd'), "@echo off`r`nsetlocal`r`nset `"PSModulePath=`"`r`n`"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`" -NoProfile -ExecutionPolicy Bypass -File `"%~dp0NeuRotic-Setup-Engine.ps1`" -Restore %*`r`nset `"result=%ERRORLEVEL%`"`r`npause`r`nexit /b %result%`r`n", [Text.Encoding]::ASCII)
$record = [ordered]@{kind='neurotic-customer-candidate-install';status='backed-up';commit=$manifest.commit;
    package_manifest_sha256=(HashFile $manifestPath);
    game_executable=$GameExecutable;backup=$backup;files=$files;preserved=$preserved;
    ini_disposition=$(if (@($preserved | Where-Object { $_.path -eq 'OptiScaler.ini' }).Count) { 'preserve-live' } else { 'reviewed-default' });
    runtime='Inconclusive';started_utc=[DateTime]::UtcNow.ToString('o')}
$recordPath = Join-Path $backup 'INSTALL-MANIFEST.json'
SaveRecord $recordPath $record
$touched = New-Object System.Collections.Generic.List[object]
try {
    CheckGameClosed $GameExecutable
    foreach ($file in $files) {
        $target = SafePath $gameDir $file.path
        if ($file.existed) {
            if ((HashFile $target) -ne $file.previous_hash) { throw 'Destination changed during installation.' }
        } elseif (Test-Path -LiteralPath $target) { throw 'A new destination file appeared during installation.' }
        $touched.Add($file)
        CopyVerified (SafePath $packageRoot $file.source) $target $file.installed_hash
    }
    foreach ($file in $preserved) {
        if ((HashFile (SafePath $gameDir $file.path)) -ne $file.sha256) { throw 'A preserved file changed externally.' }
    }
    $record.status='installed-verified'; SaveRecord $recordPath $record
} catch {
    $record.error=$_.Exception.Message
    try {
        for ($i=$touched.Count-1; $i -ge 0; --$i) {
            $file=$touched[$i]; $target=SafePath $gameDir $file.path
            if ($file.existed) {
                if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq $file.previous_hash) { continue }
                CopyVerified (SafePath (Join-Path $backup 'previous') $file.path) $target $file.previous_hash
            } elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
        }
        $record.status='failed-rolled-back'
    } catch { $record.status='failed-rollback-incomplete';$record.rollback_error=$_.Exception.Message }
    SaveRecord $recordPath $record
    throw "Installation failed: $($record.status). See $recordPath"
}
Write-Output "PASS: full installation verified. Recovery folder: $backup"
Write-Output 'Next: launch the game and press Insert to open NeuRotic, unless you saved a different shortcut. Keep the recovery folder until testing is complete.'
Write-Output 'To revert, close the game and run Restore.cmd in that recovery folder. Current settings and the model are preserved.'
