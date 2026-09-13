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
    [switch]$Restore,
    [switch]$Uninstall,
    [switch]$Installed,
    [string]$GameDirectory,
    [ValidateSet('Update','Repair','ChangeProxy','Uninstall','Cancel')]
    [string]$ExistingInstallAction,
    [switch]$AdoptModifiedInstall,
    [switch]$RetireObsoleteOptiScalerProxy,
    [ValidateSet('KeepSettings','RemoveSettings','Full')]
    [string]$UninstallMode,
    [ValidateSet('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ManualProxyName,
    [string]$LegacyManifest,
    [switch]$RemovePrivateModel,
    [switch]$ConfirmUninstall
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:UninstallCompleted = $false

function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function HashBytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','') }
    finally { $sha.Dispose() }
}
function Get-UninstallSnapshotRelative($Entry) {
    if ($Entry.PSObject.Properties.Name -contains 'snapshot') {
        $snapshot = [string]$Entry.snapshot
        if ($snapshot -notmatch '^(?:f\\[a-fA-F0-9]{4}|files\\[a-fA-F0-9]{64})\.bin$') { throw 'Invalid uninstall recovery snapshot path.' }
        return $snapshot
    }
    return [string]$Entry.path
}
function SaveRecord([string]$Path, $Value) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $temporary = $Path + '.tmp'
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        [IO.File]::Replace($temporary,$Path,[NullString]::Value)
    } else {
        [IO.File]::Move($temporary,$Path)
    }
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

function Assert-PathUnderRoot([string]$Root,[string]$Path) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $full.StartsWith($base + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Path leaves the selected game folder: $full"
    }
    $walk = $full
    while ($walk -and $walk.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $walk) {
            if ((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked installation records are not supported: $walk"
            }
        }
        if ($walk -eq $base) { break }
        $walk = Split-Path -Parent $walk
    }
    return $full
}

function Read-InstallRecord([string]$RecordPath) {
    if (-not (Test-Path -LiteralPath $RecordPath -PathType Leaf)) { throw "Installation record was not found: $RecordPath" }
    $record = Get-Content -Raw -Encoding UTF8 -LiteralPath $RecordPath | ConvertFrom-Json
    if ($record.kind -ne 'neurotic-customer-candidate-install' -or $record.files.Count -lt 2) {
        throw "This is not a recognized NeuRotic installation record: $RecordPath"
    }
    return $record
}

# Never recursively remove a product directory: it can contain user-added files.
function Remove-OwnedFile([string]$Root,[string]$Relative,[string]$ExpectedHash) {
    $path = SafePath $Root $Relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ((HashFile $path) -ne $ExpectedHash) { Write-Host "Preserved changed file: $path"; return }
        Remove-Item -LiteralPath $path -Force
    }
    $parent = Split-Path -Parent $path
    while ($parent -and $parent -ine $Root) {
        [void](Assert-PathUnderRoot $Root $parent)
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { $parent = Split-Path -Parent $parent; continue }
        if (@(Get-ChildItem -LiteralPath $parent -Force).Count) { break }
        Remove-Item -LiteralPath $parent -Force
        $parent = Split-Path -Parent $parent
    }
}

function Test-RestoreChain([string]$GameRoot,[string[]]$Chain) {
    $hashes = @{}; $seen = @{}
    $mismatches = New-Object System.Collections.Generic.List[string]
    foreach ($path in $Chain) {
        [void](Assert-PathUnderRoot $GameRoot $path)
        if ($path.Substring($GameRoot.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$') {
            throw 'Restore records must be in a NeuRotic installation backup folder.'
        }
        if ($seen.ContainsKey($path)) { throw 'Duplicate restore record.' }; $seen[$path] = $true
        $r = Read-InstallRecord $path
        if ($r.status -ne 'installed-verified' -or (Split-Path -Parent $r.game_executable) -ine $GameRoot -or
            $r.backup -ine (Split-Path -Parent $path)) { throw 'Restore record identity or status is invalid.' }
        $names = @{}
        foreach ($f in $r.files) {
            if (-not (AllowedTarget $f.path) -or $names.ContainsKey($f.path)) { throw 'Unexpected or duplicate restore target.' }
            $names[$f.path] = $true
            $target = SafePath $GameRoot $f.path
            if (-not $hashes.ContainsKey($f.path)) {
                if ((Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Directory occupies restore target: $target" }
                $hashes[$f.path] = $(if (Test-Path -LiteralPath $target -PathType Leaf) { HashFile $target } else { $null })
            }
            $expected = -not ($f.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$f.installed_exists
            if ($f.path -ne 'OptiScaler.ini' -and (($expected -and $null -ne $hashes[$f.path] -and $hashes[$f.path] -ne $f.installed_hash) -or
                (-not $expected -and $null -ne $hashes[$f.path]))) { [void]$mismatches.Add($f.path) }
            if ($f.existed) {
                $previous = SafePath (Join-Path $r.backup 'previous') $f.path
                if ((HashFile $previous) -ne $f.previous_hash) { throw "Backup verification failed: $previous" }
            }
            $hashes[$f.path] = $(if ($f.existed) { $f.previous_hash } else { $null })
        }
    }
    if ($mismatches.Count) { throw ('Restore chain does not match: ' + (($mismatches | Select-Object -Unique) -join ', ')) }
}

function New-UninstallTransaction([string]$GameRoot,[string[]]$Chain,[string[]]$AdditionalPaths=@()) {
    $journalPath = SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json'
    if (Test-Path -LiteralPath $journalPath) { throw 'An unfinished uninstall requires recovery first.' }
    $recoveryRelative = 'NeuRotic-uninstall-recovery\transaction-' + [Guid]::NewGuid().ToString('N')
    $recoveryRoot = SafePath $GameRoot $recoveryRelative
    $paths = @('OptiScaler.ini','NeuRotic\UserData\OptiScaler.ini','NeuRotic\Installer\Current-Install.json','Uninstall NeuRotic.cmd') + $AdditionalPaths
    foreach ($recordPath in $Chain) {
        $paths += $recordPath.Substring($GameRoot.Length+1)
        $paths += @((Read-InstallRecord $recordPath).files | ForEach-Object { [string]$_.path })
    }
    $snapshotIndex = 0
    $entries = @($paths | Select-Object -Unique | ForEach-Object {
        $target = SafePath $GameRoot $_
        $exists = Test-Path -LiteralPath $target -PathType Leaf
        if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "Directory occupies transaction target: $target" }
        $hash = $(if ($exists) { HashFile $target } else { $null })
        $snapshot = 'f\' + ('{0:x4}.bin' -f $snapshotIndex)
        $snapshotIndex++
        if ($exists) { CopyVerified $target (SafePath $recoveryRoot $snapshot) $hash }
        [pscustomobject]@{path=$_;snapshot=$snapshot;existed=$exists;sha256=$hash}
    })
    $journal = [pscustomobject]@{kind='neurotic-uninstall-transaction';game_directory=$GameRoot;recovery=$recoveryRelative;files=$entries}
    SaveRecord $journalPath $journal
    return $journal
}

function Restore-UninstallTransaction([string]$GameRoot,$Journal) {
    if ($Journal.kind -ne 'neurotic-uninstall-transaction' -or $Journal.game_directory -ine $GameRoot -or
        $Journal.recovery -notmatch '^NeuRotic-uninstall-recovery\\transaction-[a-f0-9]{32}$') { throw 'Invalid uninstall recovery journal.' }
    $recoveryRoot = SafePath $GameRoot $Journal.recovery
    foreach ($f in $Journal.files) {
        if (-not (AllowedTarget $f.path) -and $f.path -notin @('NeuRotic\UserData\OptiScaler.ini','NeuRotic\Installer\Current-Install.json','Uninstall NeuRotic.cmd') -and
            $f.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST.json$') { throw 'Invalid recovery target.' }
        [void](SafePath $GameRoot $f.path)
        $snapshot = Get-UninstallSnapshotRelative $f
        if ($f.existed -and (HashFile (SafePath $recoveryRoot $snapshot)) -ne $f.sha256) { throw 'Uninstall recovery snapshot failed verification.' }
    }
    foreach ($f in $Journal.files) {
        $target = SafePath $GameRoot $f.path
        if ($f.existed) {
            if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq $f.sha256) { continue }
            CopyVerified (SafePath $recoveryRoot (Get-UninstallSnapshotRelative $f)) $target $f.sha256
        } elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
    }
    Remove-Item -LiteralPath (SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json') -Force
    Write-Host 'The pre-uninstall files and settings were recovered. You can retry uninstall.'
}

function Remove-UninstallSnapshots([string]$GameRoot,$Journal) {
    foreach ($f in $Journal.files) {
        if ($f.existed) { Remove-OwnedFile $GameRoot ($Journal.recovery + '\' + (Get-UninstallSnapshotRelative $f)) $f.sha256 }
    }
}

function New-UninstallCleanup([string]$GameRoot,[string[]]$Chain,$State,$Journal,[string]$Mode) {
    $entries = @()
    if ($Mode -eq 'Full') {
    foreach ($recordPath in $Chain) {
        $r = Read-InstallRecord $recordPath
        $folder = Assert-PathUnderRoot $GameRoot (Split-Path -Parent $recordPath)
        $prefix = $folder.Substring($GameRoot.Length+1) + '\'
        foreach ($f in $r.files) {
            if ($f.existed) { $entries += [pscustomobject]@{path=($prefix + 'previous\' + $f.path);sha256=$f.previous_hash} }
        }
        if ($r.PSObject.Properties.Name -contains 'owned_files') {
            foreach ($f in $r.owned_files) { $entries += [pscustomobject]@{path=($prefix + $f.path);sha256=$f.sha256} }
        }
        $entries += [pscustomobject]@{path=($prefix + 'INSTALL-MANIFEST.json');sha256=(HashFile $recordPath)}
    }
    }
    foreach ($f in $Journal.files) {
        if ($f.existed) { $entries += [pscustomobject]@{path=($Journal.recovery + '\' + (Get-UninstallSnapshotRelative $f));sha256=$f.sha256} }
    }
    if ($Mode -eq 'Full' -and $State) {
        if ($State.PSObject.Properties.Name -contains 'manager_files') { $entries += @($State.manager_files) }
        $entries += [pscustomobject]@{path='NeuRotic\Installer\Current-Install.json';sha256=(HashFile (SafePath $GameRoot 'NeuRotic\Installer\Current-Install.json'))}
    }
    $plan = [pscustomobject]@{kind='neurotic-uninstall-cleanup';game_directory=$GameRoot;files=$entries}
    SaveRecord (SafePath $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json') $plan
    return $plan
}

function Complete-UninstallCleanup([string]$GameRoot,$Plan) {
    if ($Plan.kind -ne 'neurotic-uninstall-cleanup' -or $Plan.game_directory -ine $GameRoot) { throw 'Invalid cleanup receipt.' }
    foreach ($f in $Plan.files) {
        if ($f.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\' -and
            $f.path -notmatch '^NeuRotic-uninstall-recovery\\transaction-[a-f0-9]{32}\\' -and
            $f.path -notin @('NeuRotic\Installer\Current-Install.json','NeuRotic\Installer\NeuRotic-Setup-Engine.ps1')) { throw 'Unexpected cleanup target.' }
        [void](SafePath $GameRoot $f.path)
        if ($f.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid cleanup hash.' }
    }
    foreach ($f in $Plan.files) { Remove-OwnedFile $GameRoot $f.path $f.sha256 }
    $receipt = SafePath $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
    Remove-OwnedFile $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json' (HashFile $receipt)
}

function Invoke-RestoreRecord([string]$RecordPath,[switch]$AllowRestored) {
    $record = Read-InstallRecord $RecordPath
    if ($record.status -ne 'installed-verified' -and -not ($AllowRestored -and $record.status -eq 'restored')) {
        throw "This installation record is not active: $RecordPath"
    }
    if ($record.status -eq 'restored') { return $record }
    $backupRoot = Split-Path -Parent $RecordPath
    if ($record.backup -ne $backupRoot) { throw 'Installation record backup path does not match its location.' }
    $gameDir = Split-Path -Parent $record.game_executable
    CheckGameClosed $record.game_executable
    $current = New-Object System.Collections.Generic.List[object]
    foreach ($file in $record.files) {
        if (-not (AllowedTarget $file.path)) { throw 'Unexpected backup target.' }
        $target = SafePath $gameDir $file.path
        $existsNow = Test-Path -LiteralPath $target -PathType Leaf
        $currentHash = $(if ($existsNow) { HashFile $target } else { $null })
        $current.Add([pscustomobject]@{path=$file.path;existed=$existsNow;sha256=$currentHash})
        $expectsInstalled = -not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
        if ($file.path -ne 'OptiScaler.ini') {
            if ($expectsInstalled -and $existsNow -and $currentHash -ne $file.installed_hash) {
                throw "A file has changed since installation: $target. Restore stopped."
            }
            if (-not $expectsInstalled -and $existsNow) { throw "A removed proxy name is occupied again: $target. Restore stopped." }
        }
        if ($file.existed -and (HashFile (SafePath (Join-Path $backupRoot 'previous') $file.path)) -ne $file.previous_hash) {
            throw 'A backup file failed verification.'
        }
    }
    if ($CheckOnly) { Write-Output 'PASS: restore preflight; no files changed.'; return }
    $undo = Join-Path $backupRoot ('restore-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $undo | Out-Null
    foreach ($file in $current) {
        if ($file.existed) { CopyVerified (SafePath $gameDir $file.path) (SafePath $undo $file.path) $file.sha256 }
    }
    $owned = @()
    if ($record.PSObject.Properties.Name -contains 'owned_files') { $owned = @($record.owned_files) }
    foreach ($file in $current) {
        if ($file.existed) { $owned += [pscustomobject]@{path=((Split-Path -Leaf $undo) + '\' + $file.path);sha256=$file.sha256} }
    }
    $record | Add-Member -NotePropertyName owned_files -NotePropertyValue $owned -Force
    SaveRecord $RecordPath $record
    $touched = New-Object System.Collections.Generic.List[object]
    try {
        CheckGameClosed $record.game_executable
        for ($i=$record.files.Count-1; $i -ge 0; --$i) {
            $file = $record.files[$i]
            $target = SafePath $gameDir $file.path
            $touched.Add($file)
            if ($file.existed) {
                CopyVerified (SafePath (Join-Path $backupRoot 'previous') $file.path) $target $file.previous_hash
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
    if ($record.PSObject.Properties.Name -contains 'created_directories') {
        foreach ($relative in @($record.created_directories | Sort-Object Length -Descending)) {
            $directory = SafePath $gameDir $relative
            if ((Test-Path -LiteralPath $directory -PathType Container) -and @(Get-ChildItem -Force -LiteralPath $directory).Count -eq 0) {
                Remove-Item -LiteralPath $directory -Force
            }
        }
    }
    Write-Host "PASS: exact pre-install files restored, including $($record.selected_proxy) and OptiScaler.ini. Removed candidate files remain recoverable in: $undo"
    return $record
}

function Select-UninstallMode {
    Write-Host ''
    Write-Host 'What should happen to your NeuRotic settings?'
    Write-Host '1. Keep my current settings for reinstalling (recommended)'
    Write-Host '2. Remove NeuRotic settings and restore the exact pre-install state'
    Write-Host '3. Full cleanup: remove settings plus installer history and backups'
    Write-Host '0. Cancel'
    $choice = (Read-Host 'Choose 0, 1, 2, or 3').Trim()
    return $(switch ($choice) { '1' {'KeepSettings'} '2' {'RemoveSettings'} '3' {'Full'} default {$null} })
}

function Test-RecordAgainstHashes($Record,$Hashes) {
    foreach ($file in $Record.files) {
        if ($file.path -eq 'OptiScaler.ini') { continue }
        $expectsInstalled = -not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
        if ($expectsInstalled -and (-not $Hashes.ContainsKey([string]$file.path) -or $Hashes[[string]$file.path] -ne [string]$file.installed_hash)) { return $false }
        if (-not $expectsInstalled -and $Hashes.ContainsKey([string]$file.path)) { return $false }
    }
    return $true
}

function Get-LegacyRestoreChain([string]$GameRoot,[string]$SelectedManifest) {
    $paths = @()
    if ($SelectedManifest) {
        $paths = @((Assert-PathUnderRoot $GameRoot (Resolve-Path -LiteralPath $SelectedManifest).Path))
    } else {
        foreach ($relativeRoot in @('NeuRotic-test-backups','NeuRotic-backups')) {
            $root = SafePath $GameRoot $relativeRoot
            if (Test-Path -LiteralPath $root -PathType Container) {
                foreach ($folder in Get-ChildItem -LiteralPath $root -Directory) {
                    [void](Assert-PathUnderRoot $GameRoot $folder.FullName)
                    $candidate = SafePath $folder.FullName 'INSTALL-MANIFEST.json'
                    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $paths += $candidate }
                }
            }
        }
    }
    $records = @($paths | ForEach-Object {
        try {
            $r = Read-InstallRecord $_
            if ($r.status -eq 'installed-verified' -and (Split-Path -Parent $r.game_executable) -eq $GameRoot) {
                [pscustomobject]@{Path=$_;Record=$r}
            }
        } catch {}
    })
    if (-not $records.Count) { return @() }
    $hashes = @{}
    foreach ($name in $allowedProxies + @('ReShade64.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini','NeuRotic-LICENSE.txt')) {
        $candidate = SafePath $GameRoot $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $hashes[$name] = HashFile $candidate }
    }
    foreach ($dir in @('OptiScaler','Licenses')) {
        $candidateRoot = SafePath $GameRoot $dir
        if (Test-Path -LiteralPath $candidateRoot -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $candidateRoot -File -Recurse) {
                $relative = $file.FullName.Substring($GameRoot.Length + 1)
                [void](SafePath $GameRoot $relative)
                $hashes[$relative] = HashFile $file.FullName
            }
        }
    }
    $chain = New-Object System.Collections.Generic.List[string]
    while ($true) {
        $matches = @($records | Where-Object { Test-RecordAgainstHashes $_.Record $hashes })
        if (-not $matches.Count) { break }
        if ($matches.Count -gt 1) { throw 'More than one legacy installation record matches the current files. Select the exact game or use the installed uninstaller.' }
        $match = $matches[0]
        $chain.Add($match.Path)
        foreach ($file in $match.Record.files) {
            if ($file.existed) { $hashes[[string]$file.path] = [string]$file.previous_hash }
            else { [void]$hashes.Remove([string]$file.path) }
        }
        $records = @($records | Where-Object { $_.Path -ne $match.Path })
    }
    return @($chain)
}

function Get-GameForUninstall {
    if ($GameDirectory) {
        $root = [IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Game folder was not found: $root" }
        [void](SafePath $root 'NeuRotic\Installer')
        return [pscustomobject]@{Root=$root;Executable=$null}
    }
    if (-not $GameExecutable) {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title = 'NeuRotic Uninstall - select your game executable'
        $picker.Filter = 'Game executable (*.exe)|*.exe'
        try {
            if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { return $null }
            $script:GameExecutable = $picker.FileName
        } finally { $picker.Dispose() }
    }
    $exe = (Resolve-Path -LiteralPath $GameExecutable).Path
    if ([IO.Path]::GetExtension($exe) -ine '.exe') { throw 'Select the game executable.' }
    return [pscustomobject]@{Root=(Split-Path -Parent $exe);Executable=$exe}
}

function Invoke-ManualUninstall([string]$GameRoot,[string]$Mode) {
    $packageManifestPath = Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json'
    $knownHash = $null
    $payload = @()
    if (Test-Path -LiteralPath $packageManifestPath -PathType Leaf) {
        $packageManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageManifestPath | ConvertFrom-Json
        $payload = @($packageManifest.files | Where-Object { $_.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase) })
        $runtime = @($payload | Where-Object { $_.path -eq 'payload\OptiScaler.dll' })
        if ($runtime.Count -eq 1) { $knownHash = [string]$runtime[0].sha256 }
    }
    $detected = @($allowedProxies | Where-Object {
        $path = Join-Path $GameRoot $_
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        if ($knownHash -and (HashFile $path) -eq $knownHash) { return $true }
        return (Get-Item -LiteralPath $path).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
    })
    $proxy = $ManualProxyName
    if (-not $proxy -and $detected.Count -eq 1) { $proxy = $detected[0] }
    if (-not $proxy) {
        Write-Host ''
        if ($detected.Count) { Write-Host ('Possible NeuRotic proxy files: ' + ($detected -join ', ')) }
        Write-Host 'No trustworthy installation record was found. Choose the filename you installed NeuRotic as.'
        $proxy = SelectProxyName
    }
    if (-not $proxy) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $proxyPath = SafePath $GameRoot $proxy
    if (-not (Test-Path -LiteralPath $proxyPath -PathType Leaf)) { throw "The selected proxy does not exist: $proxyPath" }
    $managed = New-Object System.Collections.Generic.List[string]
    $managed.Add($proxy)
    foreach ($file in $payload) {
        $relative = [string]$file.path.Substring(8)
        if ($relative -eq 'OptiScaler.dll' -or $relative -eq 'OptiScaler.ini') { continue }
        if (-not (AllowedTarget $relative)) { continue }
        $target = SafePath $GameRoot $relative
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq [string]$file.sha256) { $managed.Add($relative) }
    }
    Write-Output ''
    Write-Output 'NeuRotic uninstall - manual recovery mode'
    Write-Output "Game folder: $GameRoot"
    Write-Output "Selected proxy: $proxy"
    Write-Output ('Verified files to remove: ' + (($managed | Select-Object -Unique) -join ', '))
    Write-Output 'No original proxy can be restored because no installation record was found.'
    if ($CheckOnly) { Write-Output 'PASS: manual uninstall preview; no files changed.'; return }
    if (-not $ConfirmUninstall -and (Read-Host 'Type UNINSTALL to continue') -cne 'UNINSTALL') { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $recovery = SafePath $GameRoot ('NeuRotic-uninstall-recovery\manual-' + [Guid]::NewGuid().ToString('N').Substring(0,12))
    New-Item -ItemType Directory -Path $recovery -Force | Out-Null
    foreach ($relative in @($managed | Select-Object -Unique)) {
        $target = SafePath $GameRoot $relative
        CopyVerified $target (SafePath $recovery $relative) (HashFile $target)
    }
    $journal = New-UninstallTransaction $GameRoot @() @($managed)
    try {
    foreach ($relative in @($managed | Select-Object -Unique)) { Remove-Item -LiteralPath (SafePath $GameRoot $relative) -Force }
    if ($Mode -ne 'KeepSettings') {
        $iniPath = SafePath $GameRoot 'OptiScaler.ini'
        if (Test-Path -LiteralPath $iniPath -PathType Leaf) { Copy-Item -LiteralPath $iniPath -Destination $recovery; Remove-Item -LiteralPath $iniPath -Force }
    }
    Remove-Item -LiteralPath (SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json') -Force
    } catch { Restore-UninstallTransaction $GameRoot $journal; throw }
    Remove-UninstallSnapshots $GameRoot $journal
    if ($RemovePrivateModel -and ($ConfirmUninstall -or (Read-Host 'Type DELETE MODEL to remove the user-supplied NVIDIA model') -ceq 'DELETE MODEL')) {
        $model = SafePath $GameRoot 'nvngx_dlssnr.dll'
        if (Test-Path -LiteralPath $model -PathType Leaf) { Remove-Item -LiteralPath $model -Force }
    }
    $script:UninstallCompleted = $true
    Write-Output "PASS: manually selected NeuRotic files removed. Recovery: $recovery"
}

function Invoke-PublicUninstall {
    $game = Get-GameForUninstall
    if (-not $game) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $gameRoot = $game.Root
    $statePath = SafePath $gameRoot 'NeuRotic\Installer\Current-Install.json'
    $journalPath = SafePath $gameRoot 'NeuRotic\Installer\Uninstall-Transaction.json'
    if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
        Write-Host 'A previous uninstall was interrupted. Its verified snapshot can recover the original files.'
        if ($CheckOnly) { throw 'Recovery is required before uninstall can proceed.' }
        if (-not $ConfirmUninstall -and (Read-Host 'Type RECOVER to restore the pre-uninstall state') -cne 'RECOVER') { return }
        $journal = Get-Content -Raw -LiteralPath $journalPath | ConvertFrom-Json
        foreach ($exe in Get-ChildItem -LiteralPath $gameRoot -Filter '*.exe' -File) { CheckGameClosed $exe.FullName }
        Restore-UninstallTransaction $gameRoot $journal
        $staleCleanup = SafePath $gameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
        if (Test-Path -LiteralPath $staleCleanup -PathType Leaf) { Remove-Item -LiteralPath $staleCleanup -Force }
        Remove-UninstallSnapshots $gameRoot $journal
        return
    }
    $cleanupPath = SafePath $gameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
    if (Test-Path -LiteralPath $cleanupPath -PathType Leaf) {
        Write-Host 'The game files are already restored. Backup cleanup remains to be completed.'
        if ($CheckOnly) { Write-Output 'Cleanup is pending; preview made no changes.'; return }
        if (-not $ConfirmUninstall -and (Read-Host 'Type CLEANUP to finish removing recorded backup files') -cne 'CLEANUP') { return }
        Complete-UninstallCleanup $gameRoot (Get-Content -Raw -LiteralPath $cleanupPath | ConvertFrom-Json)
        $script:UninstallCompleted = $true
        Write-Output 'PASS: remaining uninstall cleanup completed.'
        return
    }
    $state = $null
    $chain = @()
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        $state = Get-Content -Raw -Encoding UTF8 -LiteralPath $statePath | ConvertFrom-Json
        if ($state.kind -ne 'neurotic-public-install' -or $state.schema_version -ne 2 -or $state.status -ne 'installed-verified') {
            throw 'The managed NeuRotic installation record is invalid or not active.'
        }
        if ([IO.Path]::GetFullPath([string]$state.game_directory).TrimEnd('\') -ne $gameRoot) { throw 'The managed installation record belongs to another game folder.' }
        if ($state.game_executable) { $game.Executable = [string]$state.game_executable }
        foreach ($item in @($state.restore_chain)) {
            $candidate = Assert-PathUnderRoot $gameRoot ([string]$item)
            $chain += $candidate
        }
        [array]::Reverse($chain)
    } else {
        $chain = @(Get-LegacyRestoreChain $gameRoot $LegacyManifest)
    }
    if ($game.Executable) { CheckGameClosed $game.Executable }
    else { foreach ($exe in Get-ChildItem -LiteralPath $gameRoot -Filter '*.exe' -File) { CheckGameClosed $exe.FullName } }
    $mode = $UninstallMode
    if (-not $mode) { $mode = Select-UninstallMode }
    if (-not $mode) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    if (-not $chain.Count) { Invoke-ManualUninstall $gameRoot $mode; return }
    $validatedRecords = @($chain | ForEach-Object {
        $recordPath = Assert-PathUnderRoot $gameRoot $_
        $record = Read-InstallRecord $recordPath
        $recordGameRoot = [IO.Path]::GetFullPath((Split-Path -Parent ([string]$record.game_executable))).TrimEnd('\')
        $recordBackup = [IO.Path]::GetFullPath([string]$record.backup).TrimEnd('\')
        $expectedBackup = [IO.Path]::GetFullPath((Split-Path -Parent $recordPath)).TrimEnd('\')
        if (-not $recordGameRoot.Equals($gameRoot,[StringComparison]::OrdinalIgnoreCase) -or
            -not $recordBackup.Equals($expectedBackup,[StringComparison]::OrdinalIgnoreCase)) {
            throw "An installation record does not belong to the selected game folder: $recordPath"
        }
        $record
    })
    $newest = $validatedRecords[0]
    Test-RestoreChain $gameRoot $chain
    [void](SafePath $gameRoot 'NeuRotic\UserData\OptiScaler.ini')
    [void](SafePath $gameRoot 'nvngx_dlssnr.dll')
    if ($mode -eq 'Full') {
        foreach ($cleanupRecord in $validatedRecords) {
            if ($cleanupRecord.PSObject.Properties.Name -contains 'owned_files') {
                foreach ($f in $cleanupRecord.owned_files) { [void](SafePath $cleanupRecord.backup $f.path) }
            }
        }
        if ($state -and $state.PSObject.Properties.Name -contains 'manager_files') {
            foreach ($f in $state.manager_files) {
                if ($f.path -ne 'NeuRotic\Installer\NeuRotic-Setup-Engine.ps1') { throw 'Unexpected installer-owned file.' }
                [void](SafePath $gameRoot $f.path)
            }
        }
    }
    $currentIniPath = SafePath $gameRoot 'OptiScaler.ini'
    $currentIniBytes = $null
    if (Test-Path -LiteralPath $currentIniPath -PathType Leaf) { $currentIniBytes = [IO.File]::ReadAllBytes($currentIniPath) }
    Write-Output ''
    Write-Output 'NeuRotic uninstall plan'
    Write-Output "Game: $gameRoot"
    Write-Output "Installed as: $($newest.selected_proxy)"
    if ($state) {
        Write-Output "Installed release: $($state.release_name)"
        Write-Output "Last installed: $($state.last_install_utc)"
        Write-Output "Installation history entries: $($chain.Count)"
    } else { Write-Output "Legacy installation records: $($chain.Count)" }
    Write-Output "Settings choice: $mode"
    Write-Output 'Private NVIDIA NR model: keep (user-owned)'
    if ($RemovePrivateModel) { Write-Output 'Private NVIDIA NR model: REMOVE (separately requested)' }
    if ($CheckOnly) { Write-Output 'PASS: uninstall preview; no files changed.'; return }
    if (-not $ConfirmUninstall -and (Read-Host 'Type UNINSTALL to continue') -cne 'UNINSTALL') { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $journal = New-UninstallTransaction $gameRoot $chain
    try {
    $savedSettingsPath = SafePath $gameRoot 'NeuRotic\UserData\OptiScaler.ini'
    if ($mode -eq 'KeepSettings' -and $null -ne $currentIniBytes) {
        $settingsHash = HashBytes $currentIniBytes
        WriteVerifiedBytes $currentIniBytes $savedSettingsPath $settingsHash
        Write-Output "Current NeuRotic settings saved for reinstalling: $savedSettingsPath"
    } elseif (Test-Path -LiteralPath $savedSettingsPath -PathType Leaf) {
        Remove-OwnedFile $gameRoot 'NeuRotic\UserData\OptiScaler.ini' (HashFile $savedSettingsPath)
    }
    foreach ($recordPath in $chain) { [void](Invoke-RestoreRecord $recordPath) }
    $installedLauncher = SafePath $gameRoot 'Uninstall NeuRotic.cmd'
    if ((Test-Path -LiteralPath $installedLauncher -PathType Leaf) -and -not $Installed) { Remove-Item -LiteralPath $installedLauncher -Force }
    if ($state) {
        $state.status = 'uninstalled'; $state.uninstalled_utc = [DateTime]::UtcNow.ToString('o'); $state.uninstall_mode = $mode
        $restoredIni = SafePath $gameRoot 'OptiScaler.ini'
        $restoredIniHash = $(if (Test-Path -LiteralPath $restoredIni -PathType Leaf) { HashFile $restoredIni } else { $null })
        $state | Add-Member -NotePropertyName restored_ini_sha256 -NotePropertyValue $restoredIniHash -Force
        SaveRecord $statePath $state
    }
    $cleanup = New-UninstallCleanup $gameRoot $chain $state $journal $mode
    Remove-Item -LiteralPath $journalPath -Force
    } catch {
        $failure = $_
        try {
            Restore-UninstallTransaction $gameRoot $journal
            if (Test-Path -LiteralPath $cleanupPath -PathType Leaf) { Remove-Item -LiteralPath $cleanupPath -Force }
        }
        catch { throw "Uninstall stopped; recovery is pending. Rerun the downloaded uninstaller after closing the game. $($_.Exception.Message)" }
        throw $failure
    }
    if ($RemovePrivateModel) {
        $deleteModel = $true
        if (-not $ConfirmUninstall) {
            if ((Read-Host 'The private NVIDIA model was not installed by NeuRotic. Type DELETE MODEL to remove it') -cne 'DELETE MODEL') {
                $deleteModel = $false
                Write-Output 'Private-model removal was not confirmed. The model was preserved.'
            }
        }
        $model = SafePath $gameRoot 'nvngx_dlssnr.dll'
        if ($deleteModel -and (Test-Path -LiteralPath $model -PathType Leaf)) { Remove-Item -LiteralPath $model -Force }
    }
    Complete-UninstallCleanup $gameRoot $cleanup
    $script:UninstallCompleted = $true
    Write-Output 'PASS: NeuRotic was uninstalled and the verified pre-install files were restored.'
}

if ($Restore) {
    $restoreRecordPath = Join-Path $PSScriptRoot 'INSTALL-MANIFEST.json'
    $restoreRecord = Read-InstallRecord $restoreRecordPath
    Test-RestoreChain (Split-Path -Parent $restoreRecord.game_executable) @($restoreRecordPath)
    [void](Invoke-RestoreRecord $restoreRecordPath)
    return
}
if ($Uninstall) {
    Invoke-PublicUninstall
    if ($Installed -and $script:UninstallCompleted) { exit 10 }
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
foreach ($required in @('payload\OptiScaler.dll','payload\nvngx.dll_dlssnr.dll','payload\OptiScaler.ini','support\NeuRotic-Setup-Engine.ps1','support\BUILD-MANIFEST.json','NeuRotic-Setup.cmd','NeuRotic-Uninstall.cmd')) {
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
$managerRoot = SafePath $gameDir 'NeuRotic\Installer'
$currentStatePath = Join-Path $managerRoot 'Current-Install.json'
if (Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic\Installer\Uninstall-Transaction.json')) {
    throw 'An interrupted uninstall must be recovered using NeuRotic-Uninstall.cmd before Setup can continue.'
}
if (Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic\Installer\Cleanup-Pending.json')) {
    throw 'Finish the pending cleanup using NeuRotic-Uninstall.cmd before running Setup.'
}
$priorState = $null
$retainedState = $null
$adoptedModifiedInstall = $false
$adoptedPriorRelease = $null
$changingFromProxy = $null
if (Test-Path -LiteralPath $currentStatePath -PathType Leaf) {
    $priorState = Get-Content -Raw -Encoding UTF8 -LiteralPath $currentStatePath | ConvertFrom-Json
    if ($priorState.kind -ne 'neurotic-public-install' -or $priorState.schema_version -ne 2 -or
        [IO.Path]::GetFullPath([string]$priorState.game_directory).TrimEnd('\') -ne $gameDir) {
        throw 'An invalid managed NeuRotic installation record is present. No files were changed.'
    }
    if ($priorState.status -eq 'uninstalled') {
        $retainedState = $priorState
        $priorState = $null
    } elseif ($priorState.status -ne 'installed-verified') {
        throw 'The managed NeuRotic installation record is incomplete. No files were changed.'
    }
}
if ($priorState) {
    $verifyChain = @($priorState.restore_chain | ForEach-Object { [string]$_ }); [array]::Reverse($verifyChain)
    try {
        Test-RestoreChain $gameDir $verifyChain
    } catch {
        $chainFailure = $_
        if ($chainFailure.Exception.Message -notlike 'Restore chain does not match:*') { throw }
        $adopt = $AdoptModifiedInstall
        if (-not $adopt -and -not $ExistingInstallAction) {
            Write-Host ''
            Write-Host 'A previous NeuRotic installation record does not match the files now in this game folder.'
            Write-Host 'Setup has not changed any files.'
            Write-Host ('Changed recorded targets: ' + $chainFailure.Exception.Message.Substring('Restore chain does not match: '.Length))
            Write-Host '1. Preserve the current files as this update''s recovery baseline and continue'
            Write-Host '0. Cancel'
            $adopt = ((Read-Host 'Choose 0 or 1').Trim() -eq '1')
        }
        if (-not $adopt) {
            throw 'The managed installation was changed after its recorded install. Setup made no changes. Re-run with -AdoptModifiedInstall only if the current game files are the state you want this update to restore on uninstall.'
        }
        $adoptedModifiedInstall = $true
        $adoptedPriorRelease = [string]$priorState.release_name
        Write-Host 'The current game files will be preserved as the recovery baseline for this update. Earlier backup records will remain untouched.'
        $priorState = $null
    }
    if ($priorState) {
    if ($ExistingInstallAction -eq 'Cancel') { Write-Output 'Setup cancelled. No files were changed.'; return }
    if ($ExistingInstallAction -eq 'Uninstall') { $script:GameDirectory = $gameDir; Invoke-PublicUninstall; return }
    if (-not $ProxyName) {
        $choice = $ExistingInstallAction
        if (-not $choice) {
            Write-Host ''
            Write-Host 'NeuRotic is already installed in this game.'
            Write-Host ("Release: {0}" -f $priorState.release_name)
            Write-Host ("Installed as: {0}" -f $priorState.selected_proxy)
            Write-Host ("Last installed: {0}" -f $priorState.last_install_utc)
            Write-Host '1. Update or repair using the same filename'
            Write-Host '2. Change the proxy filename'
            Write-Host '3. Uninstall NeuRotic'
            Write-Host '0. Cancel'
            $answer = (Read-Host 'Choose 0, 1, 2, or 3').Trim()
            $choice = $(switch ($answer) { '1' {'Update'} '2' {'ChangeProxy'} '3' {'Uninstall'} default {'Cancel'} })
        }
        if ($choice -eq 'Cancel') { Write-Output 'Setup cancelled. No files were changed.'; return }
        if ($choice -eq 'Uninstall') {
            $script:GameDirectory = $gameDir
            Invoke-PublicUninstall
            return
        }
        if ($choice -eq 'ChangeProxy') {
            $changingFromProxy = [string]$priorState.selected_proxy
            $ProxyName = SelectProxyName
            if (-not $ProxyName) { Write-Output 'Setup cancelled. No files were changed.'; return }
            if ($ProxyName -ieq $changingFromProxy) { $changingFromProxy = $null; $ExistingProxyAction = 'Replace' }
        } else {
            $ProxyName = [string]$priorState.selected_proxy
            if (-not $ExistingProxyAction -and (Test-Path -LiteralPath (SafePath $gameDir $ProxyName) -PathType Leaf)) { $ExistingProxyAction = 'Replace' }
        }
    } elseif ($ProxyName -ine [string]$priorState.selected_proxy) {
        if ($ExistingInstallAction -ne 'ChangeProxy') {
            throw "NeuRotic is managed as $($priorState.selected_proxy). Choose ChangeProxy or keep the recorded filename."
        }
        $changingFromProxy = [string]$priorState.selected_proxy
    } elseif (-not $ExistingProxyAction -and (Test-Path -LiteralPath (SafePath $gameDir $ProxyName) -PathType Leaf)) {
        $ExistingProxyAction = 'Replace'
    }
}
}
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
$retiredObsoleteProxy = $null
$otherOptiScaler = @($allowedProxies | Where-Object { $_ -ine $ProxyName -and $_ -ine $changingFromProxy } | Where-Object {
    $candidate = SafePath $gameDir $_
    (Test-Path -LiteralPath $candidate -PathType Leaf) -and
        (Get-Item -LiteralPath $candidate).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
})
if ($otherOptiScaler.Count) {
    $canRetire = $adoptedModifiedInstall -and $otherOptiScaler.Count -eq 1 -and $otherOptiScaler[0] -ieq 'OptiScaler.dll'
    $retire = $RetireObsoleteOptiScalerProxy
    if ($canRetire -and -not $retire -and -not $ExistingInstallAction) {
        Write-Host ''
        Write-Host 'An older bare OptiScaler.dll is also present. Setup did not create a record for it.'
        Write-Host '1. Preserve it in this update''s recovery backup and retire it before continuing'
        Write-Host '0. Cancel'
        $retire = ((Read-Host 'Choose 0 or 1').Trim() -eq '1')
    }
    if (-not $canRetire -or -not $retire) {
        throw "NeuRotic/OptiScaler is already installed under $($otherOptiScaler -join ', '). Restore that installation before installing another proxy."
    }
    $obsoletePath = SafePath $gameDir 'OptiScaler.dll'
    $retiredObsoleteProxy = [pscustomobject]@{path='OptiScaler.dll';target=$obsoletePath;sha256=(HashFile $obsoletePath)}
    Write-Host 'The older bare OptiScaler.dll will be preserved in this update''s recovery backup and restored if this update is uninstalled.'
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
        $savedSettings = SafePath $gameDir 'NeuRotic\UserData\OptiScaler.ini'
        $unchangedRestoredIni = $exists -and $retainedState -and
            ($retainedState.PSObject.Properties.Name -contains 'restored_ini_sha256') -and $previousHash -eq $retainedState.restored_ini_sha256
        $reuseSavedSettings = (-not $exists -or $unchangedRestoredIni) -and (Test-Path -LiteralPath $savedSettings -PathType Leaf)
        $baseBytes = $(if ($reuseSavedSettings) { [IO.File]::ReadAllBytes($savedSettings) } elseif ($exists) { [IO.File]::ReadAllBytes($target) } else { [IO.File]::ReadAllBytes((SafePath $packageRoot $file.path)) })
        if ($exists) { $iniOriginalBytes = [IO.File]::ReadAllBytes($target) }
        if ($action -eq 'RenameReShade') {
            $iniPlan = PlanLoadReshadeEdit $baseBytes
            $installedHash = HashBytes $iniPlan.Bytes
            $operation = 'write-loadreshade-true'
        } elseif ($exists -and -not $reuseSavedSettings) {
            $installedHash = $previousHash
            $operation = 'preserve-existing'
        } else {
            $iniPlan = [pscustomobject]@{Bytes=$baseBytes;Encoding='package';HadBom=$false}
            if ($reuseSavedSettings) { $installedHash = HashBytes $baseBytes; $operation = 'reuse-saved-settings' }
        }
    }
    $files.Add([pscustomobject][ordered]@{path=$relative;source=$file.path;operation=$operation;existed=$exists;
        previous_hash=$previousHash;installed_hash=$installedHash})
}
if ($action -eq 'RenameReShade') {
    $files.Add([pscustomobject][ordered]@{path='ReShade64.dll';source=$null;operation='rename-existing-proxy';existed=$false;
        previous_hash=$null;installed_hash=(HashFile $proxyPath)})
}
if ($retiredObsoleteProxy) {
    $files.Add([pscustomobject][ordered]@{path=$retiredObsoleteProxy.path;source=$null;operation='retire-obsolete-optiscaler-proxy';existed=$true;
        previous_hash=$retiredObsoleteProxy.sha256;installed_exists=$false;installed_hash=$null})
}
$oldProxyTransition = $null
if ($changingFromProxy) {
    if ($changingFromProxy -ieq 'dxgi.dll' -and (Test-Path -LiteralPath (Join-Path $gameDir 'ReShade64.dll') -PathType Leaf)) {
        throw 'A managed ReShade proxy move is active. Uninstall first so dxgi.dll and ReShade64.dll can be restored together, then choose the new proxy.'
    }
    if (-not $priorState.restore_chain.Count) { throw 'The managed installation has no restore history for a proxy change.' }
    $latestRecord = Read-InstallRecord ([string]$priorState.restore_chain[$priorState.restore_chain.Count-1])
    $latestEntry = @($latestRecord.files | Where-Object { $_.path -ieq $changingFromProxy })
    if ($latestEntry.Count -ne 1) { throw 'The active proxy is not represented exactly once in the latest installation record.' }
    $oldProxyPath = SafePath $gameDir $changingFromProxy
    if (-not (Test-Path -LiteralPath $oldProxyPath -PathType Leaf) -or (HashFile $oldProxyPath) -ne [string]$latestEntry[0].installed_hash) {
        throw "The managed $changingFromProxy changed or is missing. Proxy change stopped."
    }
    $oldestRecordPath = $null
    foreach ($historyPath in $priorState.restore_chain) {
        [void](Assert-PathUnderRoot $gameDir $historyPath)
        if (@((Read-InstallRecord $historyPath).files | Where-Object { $_.path -ieq $changingFromProxy }).Count) {
            $oldestRecordPath = [string]$historyPath; break
        }
    }
    if (-not $oldestRecordPath) { throw 'No original state was recorded for this proxy.' }
    $oldestRecord = Read-InstallRecord $oldestRecordPath
    $baselineEntry = @($oldestRecord.files | Where-Object { $_.path -ieq $changingFromProxy })
    if ($baselineEntry.Count -ne 1) { throw 'The original proxy baseline is ambiguous.' }
    $baselineExists = [bool]$baselineEntry[0].existed
    $baselineHash = $(if ($baselineExists) {[string]$baselineEntry[0].previous_hash} else {$null})
    $baselineSource = $(if ($baselineExists) { SafePath (Join-Path (Split-Path -Parent $oldestRecordPath) 'previous') $changingFromProxy } else {$null})
    if ($baselineExists -and (HashFile $baselineSource) -ne $baselineHash) { throw 'The original proxy baseline backup failed verification.' }
    $oldProxyTransition = [pscustomobject]@{path=$changingFromProxy;target=$oldProxyPath;final_exists=$baselineExists;final_hash=$baselineHash;source=$baselineSource}
    $files.Add([pscustomobject][ordered]@{path=$changingFromProxy;source=$null;operation='restore-old-managed-proxy';existed=$true;
        previous_hash=(HashFile $oldProxyPath);installed_exists=$baselineExists;installed_hash=$baselineHash})
}
$preserved = @()
$modelPath = SafePath $gameDir 'nvngx_dlssnr.dll'
if (Test-Path -LiteralPath $modelPath -PathType Leaf) { $preserved += @{path='nvngx_dlssnr.dll';sha256=(HashFile $modelPath)} }

Write-Output ''
$identityLabel = if ($manifest.PSObject.Properties.Name -contains 'lifecycle' -and
    $manifest.lifecycle -eq 'experimental-review') { 'Experimental Review ' } else { 'Candidate ' }
Write-Output ('NeuRotic - ' + $identityLabel + $manifest.commit.Substring(0,8))
Write-Output "Game: $GameExecutable"
Write-Output "Install target: $ProxyName"
if ($oldProxyTransition) { Write-Output "Proxy change: restore/remove managed $changingFromProxy, then install as $ProxyName in one rollback transaction." }
if ($retiredObsoleteProxy) { Write-Output 'Older bare OptiScaler.dll: preserve in recovery backup, then retire it before installing the selected proxy.' }
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
$backup = SafePath $gameDir ("NeuRotic-backups\install-$run")
$priorChain = @()
if ($priorState) { $priorChain = @($priorState.restore_chain | ForEach-Object { [string]$_ }) }
else { $priorChain = @(Get-LegacyRestoreChain $gameDir $null); [array]::Reverse($priorChain) }
$verifyChain = @($priorChain); [array]::Reverse($verifyChain)
if ($verifyChain.Count) { Test-RestoreChain $gameDir $verifyChain }
if (-not $priorState -and -not $priorChain.Count -and -not (Test-Path -LiteralPath $currentStatePath) -and
    ((Test-Path -LiteralPath (SafePath $gameDir 'Uninstall NeuRotic.cmd')) -or
     (Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic\Installer\NeuRotic-Setup-Engine.ps1')))) {
    throw 'An unrecognized installer or uninstaller already occupies the destination. It was preserved.'
}
$createdDirectories = @()
foreach ($file in $files) {
    $parent = Split-Path -Parent (SafePath $gameDir $file.path)
    while ($parent -ine $gameDir) {
        if (-not (Test-Path -LiteralPath $parent)) { $createdDirectories += $parent.Substring($gameDir.Length+1) }
        $parent = Split-Path -Parent $parent
    }
}
foreach ($file in $files) {
    $restorePath = SafePath (Join-Path $backup 'restore-00000000') $file.path
    if ($restorePath.Length -ge 248) { throw 'The game folder path is too long for reliable Windows PowerShell backup/restore. No game files were changed.' }
}
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($file in $files) {
    if ($file.existed) { CopyVerified (SafePath $gameDir $file.path) (SafePath (Join-Path $backup 'previous') $file.path) $file.previous_hash }
}
$managerWasPresent = Test-Path -LiteralPath $managerRoot -PathType Container
$managerSnapshot = Join-Path $backup 'manager-previous'
if ($managerWasPresent) {
    foreach ($name in @('NeuRotic-Setup-Engine.ps1','Current-Install.json')) {
        $managerFile = SafePath $managerRoot $name
        if (Test-Path -LiteralPath $managerFile -PathType Leaf) { CopyVerified $managerFile (SafePath $managerSnapshot $name) (HashFile $managerFile) }
    }
}
$priorInstalledLauncher = Join-Path $gameDir 'Uninstall NeuRotic.cmd'
$launcherSnapshot = Join-Path $backup 'Uninstall NeuRotic.cmd.previous'
if (Test-Path -LiteralPath $priorInstalledLauncher -PathType Leaf) { Copy-Item -LiteralPath $priorInstalledLauncher -Destination $launcherSnapshot -Force }
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
    ini_disposition=$(if ($action -eq 'RenameReShade') {'LoadReshade=true targeted encoding-preserving edit'} elseif ($iniEntry.operation -eq 'reuse-saved-settings') {'reuse-saved-settings'} elseif ($iniEntry.existed) {'preserve-live'} else {'reviewed-default'});
    runtime='Inconclusive';started_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null;restored_utc=$null;
    error=$null;rollback_error=$null;owned_files=@();created_directories=@($createdDirectories | Select-Object -Unique);
    previous_record=$(if ($priorChain.Count) { $priorChain[$priorChain.Count-1] } else { $null })}
foreach ($ownedFile in @(Get-ChildItem -LiteralPath $backup -Recurse -File)) {
    $relative = $ownedFile.FullName.Substring($backup.Length+1)
    if (-not $relative.StartsWith('previous\')) { $record.owned_files += [pscustomobject]@{path=$relative;sha256=(HashFile $ownedFile.FullName)} }
}
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
    if ($oldProxyTransition) {
        Touch $oldProxyTransition.path
        if ($oldProxyTransition.final_exists) {
            CopyVerified $oldProxyTransition.source $oldProxyTransition.target $oldProxyTransition.final_hash
        } elseif (Test-Path -LiteralPath $oldProxyTransition.target -PathType Leaf) {
            Remove-Item -LiteralPath $oldProxyTransition.target -Force
        }
    }
    if ($retiredObsoleteProxy) {
        Touch $retiredObsoleteProxy.path
        if ((HashFile $retiredObsoleteProxy.target) -ne $retiredObsoleteProxy.sha256) { throw 'The older bare OptiScaler.dll changed before installation.' }
        Remove-Item -LiteralPath $retiredObsoleteProxy.target -Force
        if (Test-Path -LiteralPath $retiredObsoleteProxy.target) { throw 'The older bare OptiScaler.dll could not be retired.' }
    }
    foreach ($file in $files) {
        if ($file.operation -in @('rename-existing-proxy','restore-old-managed-proxy','retire-obsolete-optiscaler-proxy')) { continue }
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
        $target = SafePath $gameDir $file.path
        $expectsInstalled = -not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
        if ($expectsInstalled) {
            if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or (HashFile $target) -ne $file.installed_hash) { throw "Installed verification failed: $($file.path)" }
        } elseif (Test-Path -LiteralPath $target) { throw "Removed proxy still exists after the managed proxy change: $($file.path)" }
    }
    foreach ($file in $preserved) {
        if ((HashFile (SafePath $gameDir $file.path)) -ne $file.sha256) { throw 'The NVIDIA model changed externally.' }
    }
    $record.status='installed-verified'; $record.completed_utc=[DateTime]::UtcNow.ToString('o'); SaveRecord $recordPath $record

    New-Item -ItemType Directory -Path $managerRoot -Force | Out-Null
    $installedEngine = Join-Path $managerRoot 'NeuRotic-Setup-Engine.ps1'
    CopyVerified (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1') $installedEngine (HashFile (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1'))
    $installedLauncher = Join-Path $gameDir 'Uninstall NeuRotic.cmd'
    $launcherText = @"
@echo off
setlocal
title Uninstall NeuRotic
echo NeuRotic Uninstaller
echo This copy already knows which game folder it belongs to.
echo.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0NeuRotic\Installer\NeuRotic-Setup-Engine.ps1" -Uninstall -Installed -GameDirectory "%~dp0." %*
set "result=%ERRORLEVEL%"
if "%result%"=="10" goto completed
if not "%result%"=="0" echo Uninstall did not complete. Read the message above.
pause
exit /b %result%
:completed
pause
(goto) 2>nul & del /f /q "%~f0" >nul 2>&1
"@
    [IO.File]::WriteAllText($installedLauncher,$launcherText,[Text.Encoding]::ASCII)
    $chain = @($priorChain + $recordPath)
    $history = @()
    if ($priorState -and $priorState.install_history) { $history += @($priorState.install_history) }
    $history += [pscustomobject][ordered]@{install_id=$run;release_name=$(if ($manifest.name) {[string]$manifest.name} else {'NeuRotic'});
        commit=[string]$manifest.commit;installed_utc=$record.completed_utc;selected_proxy=$ProxyName;record=$recordPath}
    $state = [ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';product='NeuRotic';
        release_name=$(if ($manifest.name) {[string]$manifest.name} else {'NeuRotic'});commit=[string]$manifest.commit;
        game_executable=$GameExecutable;game_directory=$gameDir;selected_proxy=$ProxyName;
        first_install_utc=$(if ($priorState) {[string]$priorState.first_install_utc} else {$record.completed_utc});last_install_utc=$record.completed_utc;
        install_history=$history;restore_chain=$chain;installed_uninstaller='Uninstall NeuRotic.cmd';
        preserved=$preserved;uninstalled_utc=$null;uninstall_mode=$null;
        manager_files=@([pscustomobject]@{path='NeuRotic\Installer\NeuRotic-Setup-Engine.ps1';sha256=(HashFile $installedEngine)})}
    if ($adoptedModifiedInstall) {
        $state.adopted_modified_install = $true
        $state.adopted_previous_release = $adoptedPriorRelease
        $state.adopted_utc = $record.completed_utc
    }
    SaveRecord $currentStatePath $state
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
        foreach ($name in @('NeuRotic-Setup-Engine.ps1','Current-Install.json')) {
            $target = SafePath $managerRoot $name
            $saved = SafePath $managerSnapshot $name
            if (Test-Path -LiteralPath $saved -PathType Leaf) { CopyVerified $saved $target (HashFile $saved) }
            elseif (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
        }
        $installedLauncher = Join-Path $gameDir 'Uninstall NeuRotic.cmd'
        if (Test-Path -LiteralPath $installedLauncher -PathType Leaf) { Remove-Item -LiteralPath $installedLauncher -Force }
        if (Test-Path -LiteralPath $launcherSnapshot -PathType Leaf) { Copy-Item -LiteralPath $launcherSnapshot -Destination $installedLauncher -Force }
        $record.status='failed-rolled-back'
    } catch { $record.status='failed-rollback-incomplete';$record.rollback_error=$_.Exception.Message }
    SaveRecord $recordPath $record
    throw "Installation failed: $($record.status). See $recordPath"
}
Write-Output "PASS: full installation verified. Recovery folder: $backup"
Write-Output 'Next: launch the game and press Insert to open NeuRotic, unless you saved a different shortcut. Keep the recovery folder until testing is complete.'
Write-Output "To remove NeuRotic, close the game and run 'Uninstall NeuRotic.cmd' in the game folder. Setup also retains the exact recovery record at: $backup"
