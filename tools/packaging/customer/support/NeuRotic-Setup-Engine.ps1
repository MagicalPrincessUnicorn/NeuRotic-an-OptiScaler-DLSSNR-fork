[CmdletBinding()]
param(
    [string]$GameExecutable,
    [ValidateSet('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ProxyName,
    [ValidateSet('RenameReShade','Replace','ChooseAnother','Cancel')]
    [string]$ExistingProxyAction,
    [ValidateSet('Rename','Delete','Cancel')]
    [string]$ExistingDxgiAction,
    [switch]$CheckOnly,
    [switch]$ConfirmInstall,
    [switch]$Restore
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function HashBytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','') }
    finally { $sha.Dispose() }
}
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
function WriteVerifiedBytes([byte[]]$Bytes, [string]$Destination, [string]$Hash) {
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    [IO.File]::WriteAllBytes($Destination, $Bytes)
    if ((HashFile $Destination) -ne $Hash) { throw "Written file verification failed: $Destination" }
}
function CheckGameClosed([string]$Executable) {
    if (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Executable)) -ErrorAction SilentlyContinue) {
        throw 'Close the game before continuing.'
    }
}

# Return edited bytes while retaining the source file's encoding, BOM and line endings.
function PlanLoadReshadeEdit([byte[]]$Bytes) {
    $offset = 0
    $preamble = New-Object byte[] 0
    if ($Bytes.Length -ge 4 -and $Bytes[0] -eq 0x00 -and $Bytes[1] -eq 0x00 -and $Bytes[2] -eq 0xFE -and $Bytes[3] -eq 0xFF) {
        $encoding = New-Object Text.UTF32Encoding($true,$true,$true); $offset = 4
    } elseif ($Bytes.Length -ge 4 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE -and $Bytes[2] -eq 0x00 -and $Bytes[3] -eq 0x00) {
        $encoding = New-Object Text.UTF32Encoding($false,$true,$true); $offset = 4
    } elseif ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        $encoding = New-Object Text.UTF8Encoding($true,$true); $offset = 3
    } elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) {
        $encoding = New-Object Text.UnicodeEncoding($true,$true,$true); $offset = 2
    } elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) {
        $encoding = New-Object Text.UnicodeEncoding($false,$true,$true); $offset = 2
    } else {
        $encoding = New-Object Text.UTF8Encoding($false,$true)
        try { [void]$encoding.GetString($Bytes) }
        catch { $encoding = [Text.Encoding]::Default }
    }
    if ($offset) { $preamble = $Bytes[0..($offset-1)] }
    $text = $encoding.GetString($Bytes, $offset, $Bytes.Length - $offset)
    $pattern = '(?im)^(?<prefix>[ \t]*LoadReshade[ \t]*=[ \t]*)(?<value>[^;\r\n]*?)(?<suffix>[ \t]*(?:;[^\r\n]*)?)(?<ending>\r?)$'
    $matches = [regex]::Matches($text, $pattern)
    if ($matches.Count -gt 1) { throw 'OptiScaler.ini contains more than one LoadReshade setting.' }
    if ($matches.Count -eq 1) {
        $match = $matches[0]
        $replacement = $match.Groups['prefix'].Value + 'true' + $match.Groups['suffix'].Value + $match.Groups['ending'].Value
        $text = $text.Substring(0,$match.Index) + $replacement + $text.Substring($match.Index + $match.Length)
    } else {
        $newline = $(if ($text.Contains("`r`n")) { "`r`n" } else { "`n" })
        $plugins = [regex]::Match($text, '(?im)^[ \t]*\[Plugins\][ \t]*\r?$')
        if ($plugins.Success) {
            $sectionRegex = New-Object regex '(?im)^[ \t]*\[[^\]\r\n]+\][ \t]*\r?$'
            $next = $sectionRegex.Match($text, $plugins.Index + $plugins.Length)
            $insertAt = $(if ($next.Success) { $next.Index } else { $text.Length })
            $before = $text.Substring(0,$insertAt)
            if (-not $before.EndsWith($newline)) { $before += $newline }
            $text = $before + 'LoadReshade = true' + $newline + $text.Substring($insertAt)
        } else {
            if ($text.Length -and -not $text.EndsWith($newline)) { $text += $newline }
            $text += '[Plugins]' + $newline + 'LoadReshade = true' + $newline
        }
    }
    $body = $encoding.GetBytes($text)
    $result = New-Object byte[] ($preamble.Length + $body.Length)
    if ($preamble.Length) { [Array]::Copy($preamble,0,$result,0,$preamble.Length) }
    [Array]::Copy($body,0,$result,$preamble.Length,$body.Length)
    return [pscustomobject]@{Bytes=$result;Encoding=$encoding.WebName;HadBom=($preamble.Length -gt 0)}
}

$allowedProxies = @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
function SelectProxyName {
    Write-Host ''
    Write-Host 'Choose the filename the game should load NeuRotic as:'
    Write-Host '1. dxgi.dll        (normal first choice for DirectX games)'
    Write-Host '2. winmm.dll       (normal first choice for Vulkan games)'
    Write-Host '3. version.dll'
    Write-Host '4. dbghelp.dll'
    Write-Host '5. d3d12.dll'
    Write-Host '6. wininet.dll'
    Write-Host '7. winhttp.dll'
    Write-Host '8. OptiScaler.asi'
    Write-Host '9. OptiScaler.dll'
    Write-Host '0. Cancel'
    $choice = (Read-Host 'Choose 0 through 9').Trim()
    if ($choice -eq '0') { return $null }
    $index = 0
    if (-not [int]::TryParse($choice,[ref]$index) -or $index -lt 1 -or $index -gt $allowedProxies.Count) {
        Write-Host 'That is not a valid choice.'
        return SelectProxyName
    }
    return $allowedProxies[$index-1]
}
function SelectExistingAction([string]$Name) {
    Write-Host ''
    Write-Host "A $Name already exists in this game folder. What would you like to do?"
    Write-Host ''
    if ($Name -ieq 'dxgi.dll') {
        Write-Host '1. Replace the file (Backup of original will be created)'
        Write-Host '2. Rename to ReShade64.dll - Choose this if you want to use NeuRotic and ReShade'
        Write-Host '3. Choose a different Filename'
        Write-Host '4. Cancel'
        $choice = (Read-Host 'Choose 1, 2, 3, or 4').Trim()
        return $(switch ($choice) { '1' {'Replace'} '2' {'RenameReShade'} '3' {'ChooseAnother'} default {'Cancel'} })
    }
    Write-Host "1. Back it up, delete it, and install NeuRotic as $Name"
    Write-Host '2. Choose a different filename'
    Write-Host '3. Cancel'
    $choice = (Read-Host 'Choose 1, 2, or 3').Trim()
    return $(switch ($choice) { '1' {'Replace'} '2' {'ChooseAnother'} default {'Cancel'} })
}
function AllowedTarget([string]$Relative) {
    return ($Relative -in $allowedProxies -or $Relative -in @('ReShade64.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini','NeuRotic-LICENSE.txt') -or
        $Relative -match '^(OptiScaler|Licenses)\\[^:]+$')
}

if ($Restore) {
    $recordPath = Join-Path $PSScriptRoot 'INSTALL-MANIFEST.json'
    $record = Get-Content -Raw -Encoding UTF8 -LiteralPath $recordPath | ConvertFrom-Json
    if ($record.kind -ne 'neurotic-customer-candidate-install' -or $record.status -ne 'installed-verified' -or
        $record.backup -ne $PSScriptRoot -or $record.files.Count -lt 2) { throw 'This is not a completed installation backup.' }
    $gameDir = Split-Path -Parent $record.game_executable
    CheckGameClosed $record.game_executable
    $current = New-Object System.Collections.Generic.List[object]
    foreach ($file in $record.files) {
        if (-not (AllowedTarget $file.path)) { throw 'Unexpected backup target.' }
        $target = SafePath $gameDir $file.path
        $existsNow = Test-Path -LiteralPath $target -PathType Leaf
        $currentHash = $(if ($existsNow) { HashFile $target } else { $null })
        $current.Add([pscustomobject]@{path=$file.path;existed=$existsNow;sha256=$currentHash})
        if ($file.path -ne 'OptiScaler.ini' -and (-not $existsNow -or $currentHash -ne $file.installed_hash)) {
            throw "A file has changed since installation: $target. Restore stopped."
        }
        if ($file.existed -and (HashFile (SafePath (Join-Path $PSScriptRoot 'previous') $file.path)) -ne $file.previous_hash) {
            throw 'A backup file failed verification.'
        }
    }
    if ($CheckOnly) { Write-Output 'PASS: restore preflight; no files changed.'; return }
    $undo = Join-Path $PSScriptRoot ('restore-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $undo | Out-Null
    foreach ($file in $current) {
        if ($file.existed) { CopyVerified (SafePath $gameDir $file.path) (SafePath $undo $file.path) $file.sha256 }
    }
    $touched = New-Object System.Collections.Generic.List[object]
    try {
        CheckGameClosed $record.game_executable
        for ($i=$record.files.Count-1; $i -ge 0; --$i) {
            $file = $record.files[$i]
            $target = SafePath $gameDir $file.path
            $touched.Add($file)
            if ($file.existed) {
                CopyVerified (SafePath (Join-Path $PSScriptRoot 'previous') $file.path) $target $file.previous_hash
            } elseif (Test-Path -LiteralPath $target -PathType Leaf) {
                Remove-Item -LiteralPath $target -Force
            } elseif (Test-Path -LiteralPath $target) {
                throw "A directory occupies a restore target: $target"
            }
        }
    } catch {
        foreach ($file in $touched) {
            $was = @($current | Where-Object { $_.path -eq $file.path })[0]
            $target = SafePath $gameDir $file.path
            if ($was.existed) {
                if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq $was.sha256) { continue }
                CopyVerified (SafePath $undo $file.path) $target $was.sha256
            }
            elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
        }
        throw
    }
    $record.status = 'restored'; $record.restored_utc = [DateTime]::UtcNow.ToString('o'); SaveRecord $recordPath $record
    Write-Output "PASS: exact pre-install files restored, including $($record.selected_proxy) and OptiScaler.ini. Removed candidate files remain recoverable in: $undo"
    return
}

$packageRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$manifestPath = Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json'
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.kind -ne 'neurotic-customer-candidate' -or $manifest.commit -notmatch '^[a-f0-9]{40}$') { throw 'Unexpected package identity.' }
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
$action = 'None'
$scriptedAction = $ExistingProxyAction
$selectedProxy = $ProxyName
if ($ExistingDxgiAction) {
    if ($ExistingProxyAction) { throw 'Use ExistingProxyAction or the legacy ExistingDxgiAction, not both.' }
    if (-not $selectedProxy) { $selectedProxy = 'dxgi.dll' }
    if ($selectedProxy -ine 'dxgi.dll') { throw 'The legacy ExistingDxgiAction parameter applies only to dxgi.dll.' }
    $scriptedAction = $(switch ($ExistingDxgiAction) { 'Rename' {'RenameReShade'} 'Delete' {'Replace'} default {'Cancel'} })
}
while ($true) {
    if (-not $selectedProxy) {
        $selectedProxy = SelectProxyName
        if (-not $selectedProxy) { Write-Output 'Installation cancelled. No game files were changed.'; return }
    }
    $proxyPath = SafePath $gameDir $selectedProxy
    $proxyExists = Test-Path -LiteralPath $proxyPath -PathType Leaf
    if ((Test-Path -LiteralPath $proxyPath) -and -not $proxyExists) { throw "A directory named $selectedProxy occupies the installation target." }
    if (-not $proxyExists) {
        if ($scriptedAction) { throw "ExistingProxyAction was supplied, but no $selectedProxy exists in the selected game folder." }
        break
    }
    $action = $(if ($scriptedAction) { $scriptedAction } else { SelectExistingAction $selectedProxy })
    if ($action -eq 'Cancel') { Write-Output 'Installation cancelled. No game files were changed.'; return }
    if ($action -eq 'ChooseAnother') {
        if ($scriptedAction) { throw 'ChooseAnother requires interactive filename selection.' }
        $selectedProxy = $null; $action = 'None'; continue
    }
    if ($action -eq 'RenameReShade' -and $selectedProxy -ine 'dxgi.dll') {
        throw 'RenameReShade is available only when the selected filename is dxgi.dll.'
    }
    if ($action -eq 'RenameReShade') {
        $reshadePath = SafePath $gameDir 'ReShade64.dll'
        if (Test-Path -LiteralPath $reshadePath) { throw 'ReShade64.dll already exists. Setup will not overwrite it. No game files were changed.' }
    }
    break
}
$ProxyName = $selectedProxy

# Never infer the selected name from an existing DLL. Refuse a second active OptiScaler proxy.
$otherOptiScaler = @($allowedProxies | Where-Object { $_ -ine $ProxyName } | Where-Object {
    $candidate = SafePath $gameDir $_
    (Test-Path -LiteralPath $candidate -PathType Leaf) -and
        (Get-Item -LiteralPath $candidate).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
})
if ($otherOptiScaler.Count) {
    throw "NeuRotic/OptiScaler is already installed under $($otherOptiScaler -join ', '). Restore that installation before installing another proxy."
}

$files = New-Object System.Collections.Generic.List[object]
$iniPlan = $null
$iniOriginalBytes = $null
foreach ($file in $manifest.files) {
    if (-not $file.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)) { continue }
    $relative = $file.path.Substring(8)
    if ($relative -eq 'OptiScaler.dll') { $relative = $ProxyName }
    if (-not (AllowedTarget $relative)) { throw "Unexpected payload target: $relative" }
    $target = SafePath $gameDir $relative
    $exists = Test-Path -LiteralPath $target -PathType Leaf
    if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "A directory occupies a file destination: $target" }
    $previousHash = $(if ($exists) { HashFile $target } else { $null })
    $operation = 'copy-package'
    $installedHash = $file.sha256
    if ($relative -eq 'OptiScaler.ini') {
        $baseBytes = $(if ($exists) { [IO.File]::ReadAllBytes($target) } else { [IO.File]::ReadAllBytes((SafePath $packageRoot $file.path)) })
        if ($exists) { $iniOriginalBytes = $baseBytes }
        if ($action -eq 'RenameReShade') {
            $iniPlan = PlanLoadReshadeEdit $baseBytes
            $installedHash = HashBytes $iniPlan.Bytes
            $operation = 'write-loadreshade-true'
        } elseif ($exists) {
            $installedHash = $previousHash
            $operation = 'preserve-existing'
        } else {
            $iniPlan = [pscustomobject]@{Bytes=$baseBytes;Encoding='package';HadBom=$false}
        }
    }
    $files.Add([pscustomobject][ordered]@{path=$relative;source=$file.path;operation=$operation;existed=$exists;
        previous_hash=$previousHash;installed_hash=$installedHash})
}
if ($action -eq 'RenameReShade') {
    $files.Add([pscustomobject][ordered]@{path='ReShade64.dll';source=$null;operation='rename-existing-proxy';existed=$false;
        previous_hash=$null;installed_hash=(HashFile $proxyPath)})
}
$preserved = @()
$modelPath = SafePath $gameDir 'nvngx_dlssnr.dll'
if (Test-Path -LiteralPath $modelPath -PathType Leaf) { $preserved += @{path='nvngx_dlssnr.dll';sha256=(HashFile $modelPath)} }

Write-Output ''
Write-Output ('NeuRotic - Candidate ' + $manifest.commit.Substring(0,8))
Write-Output "Game: $GameExecutable"
Write-Output "Install target: $ProxyName"
if ($action -eq 'RenameReShade') {
    Write-Output 'Existing dxgi.dll: rename to ReShade64.dll and enable LoadReshade.'
} elseif ($action -eq 'Replace') {
    Write-Output "Existing ${ProxyName}: preserve in recovery backup, then delete and replace it."
} else {
    Write-Output "No existing ${ProxyName}: install immediately."
}
Write-Output "Setup creates a recovery folder before changing files. Restore returns $ProxyName and OptiScaler.ini to their exact original state."
if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
    Write-Output 'NR model missing: supply your own nvngx_dlssnr.dll beside the game executable before using NR.'
}
if ($CheckOnly) { Write-Output 'PASS: full installer preflight; no files changed.'; return }

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
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1') -Destination $backup
[IO.File]::WriteAllText((Join-Path $backup 'Restore.cmd'), "@echo off`r`nsetlocal`r`ntitle NeuRotic Restore`r`nset `"PSModulePath=`"`r`n`"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`" -NoProfile -ExecutionPolicy Bypass -File `"%~dp0NeuRotic-Setup-Engine.ps1`" -Restore %*`r`nset `"result=%ERRORLEVEL%`"`r`nif not `"%result%`"==`"0`" echo Restore did not complete. Read the message above.`r`npause`r`nexit /b %result%`r`n", [Text.Encoding]::ASCII)
$iniEntry = @($files | Where-Object { $_.path -eq 'OptiScaler.ini' })[0]
$record = [ordered]@{kind='neurotic-customer-candidate-install';status='backed-up';commit=$manifest.commit;
    package_manifest_sha256=(HashFile $manifestPath);game_executable=$GameExecutable;backup=$backup;
    selected_proxy=$ProxyName;existing_proxy_action=$action;files=$files;preserved=$preserved;
    original_proxy=[ordered]@{name=$ProxyName;existed=$proxyExists;sha256=$(if ($proxyExists) { HashFile $proxyPath } else {$null});backup_path=$(if ($proxyExists) {"previous\$ProxyName"} else {$null})};
    original_ini=[ordered]@{name='OptiScaler.ini';existed=$iniEntry.existed;sha256=$iniEntry.previous_hash;
        bytes_base64=$(if ($iniEntry.existed) {[Convert]::ToBase64String($iniOriginalBytes)} else {$null});backup_path=$(if ($iniEntry.existed) {'previous\OptiScaler.ini'} else {$null})};
    reshade_operation=$(if ($action -eq 'RenameReShade') {[ordered]@{original_name='dxgi.dll';new_name='ReShade64.dll';sha256=(HashFile $proxyPath);loadreshade=$true;encoding=$iniPlan.Encoding;bom_preserved=$iniPlan.HadBom}} else {$null});
    ini_disposition=$(if ($action -eq 'RenameReShade') {'LoadReshade=true targeted encoding-preserving edit'} elseif ($iniEntry.existed) {'preserve-live'} else {'reviewed-default'});
    runtime='Inconclusive';started_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null;restored_utc=$null;
    error=$null;rollback_error=$null}
$recordPath = Join-Path $backup 'INSTALL-MANIFEST.json'
SaveRecord $recordPath $record
$touched = New-Object System.Collections.Generic.List[string]
function Touch([string]$Path) { if (-not $touched.Contains($Path)) { $touched.Add($Path) } }
try {
    CheckGameClosed $GameExecutable
    if ($proxyExists -and (HashFile $proxyPath) -ne $record.original_proxy.sha256) { throw "$ProxyName changed before installation." }
    if ($action -eq 'RenameReShade') {
        $reshadePath = SafePath $gameDir 'ReShade64.dll'
        if (Test-Path -LiteralPath $reshadePath) { throw 'ReShade64.dll appeared before installation.' }
        Touch $ProxyName; Touch 'ReShade64.dll'
        Move-Item -LiteralPath $proxyPath -Destination $reshadePath
        if ((Test-Path -LiteralPath $proxyPath) -or (HashFile $reshadePath) -ne $record.original_proxy.sha256) { throw 'Existing dxgi.dll rename verification failed.' }
    } elseif ($action -eq 'Replace') {
        Touch $ProxyName; Remove-Item -LiteralPath $proxyPath -Force
        if (Test-Path -LiteralPath $proxyPath) { throw "Existing $ProxyName could not be removed." }
    }
    foreach ($file in $files) {
        if ($file.operation -eq 'rename-existing-proxy') { continue }
        $target = SafePath $gameDir $file.path
        if ($file.operation -eq 'preserve-existing') {
            if ((HashFile $target) -ne $file.previous_hash) { throw 'OptiScaler.ini changed before installation.' }
            continue
        }
        if ($file.path -ne $ProxyName) {
            if ($file.existed) {
                if ((HashFile $target) -ne $file.previous_hash) { throw "Destination changed during installation: $($file.path)" }
            } elseif (Test-Path -LiteralPath $target) { throw "A new destination appeared during installation: $($file.path)" }
        } elseif (Test-Path -LiteralPath $target) { throw "$ProxyName unexpectedly exists before candidate copy." }
        Touch $file.path
        if ($file.path -eq 'OptiScaler.ini') {
            WriteVerifiedBytes $iniPlan.Bytes $target $file.installed_hash
        } else {
            CopyVerified (SafePath $packageRoot $file.source) $target $file.installed_hash
        }
    }
    foreach ($file in $files) {
        if ((HashFile (SafePath $gameDir $file.path)) -ne $file.installed_hash) { throw "Installed verification failed: $($file.path)" }
    }
    foreach ($file in $preserved) {
        if ((HashFile (SafePath $gameDir $file.path)) -ne $file.sha256) { throw 'The NVIDIA model changed externally.' }
    }
    $record.status='installed-verified'; $record.completed_utc=[DateTime]::UtcNow.ToString('o'); SaveRecord $recordPath $record
} catch {
    $record.error=$_.Exception.Message
    try {
        for ($i=$touched.Count-1; $i -ge 0; --$i) {
            $path=$touched[$i]
            $file=@($files | Where-Object { $_.path -eq $path })[0]
            $target=SafePath $gameDir $path
            if ($file.existed) {
                if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq $file.previous_hash) { continue }
                CopyVerified (SafePath (Join-Path $backup 'previous') $path) $target $file.previous_hash
            } elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
        }
        $record.status='failed-rolled-back'
    } catch { $record.status='failed-rollback-incomplete';$record.rollback_error=$_.Exception.Message }
    SaveRecord $recordPath $record
    throw "Installation failed: $($record.status). See $recordPath"
}
Write-Output "PASS: full installation verified. Recovery folder: $backup"
Write-Output 'Next: launch the game and press Insert to open NeuRotic, unless you saved a different shortcut. Keep the recovery folder until testing is complete.'
Write-Output "To revert, close the game and run Restore.cmd in that recovery folder. Restore runs immediately and returns $ProxyName and OptiScaler.ini to their exact pre-install state."
