Set-StrictMode -Version 2.0

function ConvertTo-MHWildsArgument {
    param([AllowEmptyString()][string]$Value)
    # Quote one Windows argv element, including quotes and trailing backslashes.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Read-MHWildsTraceMarker {
    param([string]$Path, [string]$SessionId, [long]$ProcessId, [long]$Frequency, [long]$AfterQpc)
    if (!(Test-Path -LiteralPath $Path)) { return $null }
    $stream = $null
    $reader = $null
    $waiting = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        if ($stream.Length -gt 512MB) { throw 'Log exceeds the 512 MB capture bound.' }
        $reader = New-Object IO.StreamReader($stream)
        while (!$reader.EndOfStream) {
            $line = $reader.ReadLine()
            if ($line.Contains('NR_FRAME_TRACE_CONTROL v=1 session=' + $SessionId + ' ')) {
                if ($line -notmatch 'NR_FRAME_TRACE_CONTROL v=1 session=([0-9a-f]{32}) pid=(\d+) qpc=(\d+) frequency=(\d+) state=waiting-for-nr-enable$') { continue }
                if ([long]$Matches[2] -ne $ProcessId) { throw 'Waiting marker belongs to a replacement process.' }
                if ([long]$Matches[4] -ne $Frequency -or [long]$Matches[3] -lt $AfterQpc) { throw 'Waiting marker clock does not match this launch.' }
                $waiting = [pscustomobject]@{pid=[long]$Matches[2];qpc=[long]$Matches[3];state='waiting-for-nr-enable';line=$line}
                continue
            }
            if (!$line.Contains('NR_FRAME_TRACE v=1 session=' + $SessionId + ' ')) { continue }
            $pattern = 'NR_FRAME_TRACE v=1 session=([0-9a-f]{32}) pid=(\d+) tid=(\d+) qpc=(\d+) frequency=(\d+) seq=(\d+) native=(\d+) present=(\d+) kind=([a-z0-9-]+) classification=unknown (.*)$'
            if ($line -notmatch $pattern) { continue } # A concurrent write may be incomplete.
            if ([long]$Matches[2] -ne $ProcessId) { throw 'Trace belongs to a replacement process; this launch is not verified.' }
            if ([long]$Matches[5] -ne $Frequency -or [long]$Matches[4] -lt $AfterQpc) { throw 'Trace clock does not match this launch.' }
            if ([long]$Matches[6] -lt 1 -or [long]$Matches[6] -gt 65536) { throw 'Invalid trace sequence.' }
            if ($Matches[9] -eq 'trace-ended') { throw 'Only the trace budget terminal record was found; no usable observation verified.' }
            return [pscustomobject]@{pid=[long]$Matches[2];qpc=[long]$Matches[4];sequence=[long]$Matches[6];state='capturing';line=$line}
        }
    } catch [IO.IOException] {
        return $null # Logger replacement/locking during initialization: retry next poll.
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        elseif ($null -ne $stream) { $stream.Dispose() }
    }
    return $waiting
}

function Invoke-MHWildsTraceCapture {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$GameDirectory,
        [Parameter(Mandatory=$true)][string]$BuildManifest,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot,
        [Parameter(Mandatory=$true)][AllowEmptyString()][string[]]$GameCommand,
        [ValidateRange(1,180)][int]$VerifySeconds = 90,
        [switch]$CheckOnly,
        [switch]$RequireAssociationProfile
    )
    $ErrorActionPreference = 'Stop'
    $gameExe = Join-Path $GameDirectory 'MonsterHunterWilds.exe'
    if ($GameCommand.Count -lt 1 -or
        ![string]::Equals([IO.Path]::GetFullPath($GameCommand[0]), [IO.Path]::GetFullPath($gameExe), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Steam must supply the exact MonsterHunterWilds.exe command through %command%.'
    }
    if (Get-Process -Name MonsterHunterWilds -ErrorAction SilentlyContinue) {
        throw 'Close Monster Hunter Wilds before starting a fresh trace session.'
    }
    $manifestHash = (Get-FileHash -LiteralPath $BuildManifest -Algorithm SHA256).Hash
    $build = Get-Content -LiteralPath $BuildManifest -Raw | ConvertFrom-Json
    $installed = @{}
    foreach ($pair in @(@('OptiScaler.dll','dxgi.dll'), @('nvngx.dll_dlssnr.dll','nvngx.dll_dlssnr.dll'))) {
        $artifact = @($build.artifacts | Where-Object name -eq $pair[0])
        if ($artifact.Count -ne 1) { throw 'Build manifest has a missing or ambiguous DLL.' }
        $actual = (Get-FileHash -LiteralPath (Join-Path $GameDirectory $pair[1]) -Algorithm SHA256).Hash
        if ($actual -ne $artifact[0].sha256) { throw ('Installed DLL differs from this test build: ' + $pair[1]) }
        $installed[$pair[1]] = $actual
    }
    if ($CheckOnly) { Write-Host 'PASS: installed DLL pair verified; no game launched or settings changed.'; return }
    if ($env:SteamAppId -ne '2246340' -and $env:SteamGameId -ne '2246340') {
        throw 'Launch Monster Hunter Wilds from Steam with the supplied Launch Options entry. Direct launch is disabled because it lost tracing.'
    }
    $session = [Guid]::NewGuid().ToString('N')
    $evidence = Join-Path $EvidenceRoot $session
    New-Item -ItemType Directory -Path $evidence | Out-Null
    $logPath = Join-Path $GameDirectory 'OptiScaler.log'
    $record = [ordered]@{
        session_id=$session;commit=$build.commit;phase='Wilds Enhanced FG-on dropout trace';runtime_result='Inconclusive'
        qpc_frequency=[Diagnostics.Stopwatch]::Frequency;launch_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
        launch_utc=[DateTime]::UtcNow.ToString('o');game_executable=$gameExe
        proxy_sha256=$installed['dxgi.dll'];forwarder_sha256=$installed['nvngx.dll_dlssnr.dll']
        ini_sha256=(Get-FileHash -LiteralPath (Join-Path $GameDirectory 'OptiScaler.ini') -Algorithm SHA256).Hash
        model_sha256=(Get-FileHash -LiteralPath (Join-Path $GameDirectory 'nvngx_dlssnr.dll') -Algorithm SHA256).Hash
        source_manifest_sha256=$manifestHash;log_path=$logPath;status='prepared'
        launch_method='Steam command wrapper; explicit child environment';trace_verified=$false
        trace_trigger='nr-enable';trace_ready=$false
        association_profile_requested=[bool]$RequireAssociationProfile
        limitations='Trace is bounded at 65536 events. Verification proves capture only, not NR/FG correctness. No PresentMon capture is performed.'
    }
    $manifestPath = Join-Path $evidence 'SESSION.json'
    function Save-MHWildsSession {
        [IO.File]::WriteAllText($manifestPath, ($record | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
    }
    function Retain-MHWildsLog {
        if ($record.trace_verified -and !(Test-Path -LiteralPath $logPath)) { throw 'Verified log disappeared before retention.' }
        if (Test-Path -LiteralPath $logPath) {
            $retained = Join-Path $evidence 'OptiScaler.log'
            Copy-Item -LiteralPath $logPath -Destination $retained
            $record['retained_log'] = $retained
            $record['retained_log_sha256'] = (Get-FileHash -LiteralPath $retained -Algorithm SHA256).Hash
            $record['log_snapshot_utc'] = [DateTime]::UtcNow.ToString('o')
            $record['trace_budget_exhausted'] = $false
            foreach ($savedLine in [IO.File]::ReadLines($retained)) {
                if ($savedLine.Contains('session=' + $session + ' ') -and $savedLine.Contains('kind=trace-ended ')) {
                    $record.trace_budget_exhausted = $true
                    break
                }
            }
        }
    }
    Save-MHWildsSession
    $gameProcess = $null
    try {
        if (Test-Path -LiteralPath $logPath) {
            $previousLog = Join-Path $evidence 'prelaunch-OptiScaler.log'
            Copy-Item -LiteralPath $logPath -Destination $previousLog
            $record['prelaunch_log_sha256'] = (Get-FileHash -LiteralPath $previousLog -Algorithm SHA256).Hash
        }
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = $gameExe
        $start.WorkingDirectory = $GameDirectory
        $start.UseShellExecute = $false
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Normal # User-launched interactive game.
        $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_SESSION'] = $session
        $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_TRIGGER'] = 'nr-enable'
        if ($RequireAssociationProfile) { $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_PROFILE'] = 'frame-association' }
        else { $start.EnvironmentVariables.Remove('NEUROTIC_FRAME_TRACE_PROFILE') }
        $quoted = @()
        for ($i=1; $i -lt $GameCommand.Count; $i++) { $quoted += ConvertTo-MHWildsArgument $GameCommand[$i] }
        $start.Arguments = $quoted -join ' '
        $gameProcess = [Diagnostics.Process]::Start($start)
        $record['process_id'] = $gameProcess.Id
        $record.status = 'waiting for matching trace'
        Save-MHWildsSession
        Write-Host ('Waiting for trace from MHWilds process {0}. Session: {1}' -f $gameProcess.Id,$session)
        Write-Host ('Evidence: ' + $evidence)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $marker = $null
        while ($true) {
            $marker = Read-MHWildsTraceMarker -Path $logPath -SessionId $session -ProcessId $gameProcess.Id -Frequency $record.qpc_frequency -AfterQpc $record.launch_qpc
            if ($null -ne $marker -and $marker.state -eq 'capturing') { break }
            if ($null -ne $marker -and !$record.trace_ready) {
                $record.trace_ready = $true
                $record['ready_marker'] = $marker
                $record.status = 'trace ready; waiting for user to enable NR'
                Save-MHWildsSession
                Write-Host 'TRACE READY: load gameplay with NR off. Select Present Enhanced with FG 2x on, then enable NR to start capture.' -ForegroundColor Cyan
                Write-Host 'If NR is already on, turn it off before enabling it again. No capture budget is used while waiting.'
            }
            if ($gameProcess.HasExited) { throw 'MHWilds exited before active capture. If TRACE READY appeared, NR was not enabled to start tracing.' }
            if (!$record.trace_ready -and $timer.Elapsed.TotalSeconds -ge $VerifySeconds) { throw 'No matching trace setup marker arrived. Exit MHWilds normally and report CAPTURE FAILED.' }
            Start-Sleep -Milliseconds 500
        }
        if ($null -eq $marker) { throw 'No matching trace arrived within the verification window. Stop the long test and exit MHWilds normally.' }
        if ($RequireAssociationProfile -and !$marker.line.Contains('profile=frame-association')) {
            throw 'Wrong trace profile. Exit normally; this capture is not the association experiment.'
        }
        $record.trace_verified = $true
        $record.trace_ready = $true
        $record['first_trace'] = $marker
        $record['trace_verified_utc'] = [DateTime]::UtcNow.ToString('o')
        $record.status = 'trace verified; waiting for game exit'
        Save-MHWildsSession
        Write-Host 'TRACE VERIFIED: matching session, game process and clock.' -ForegroundColor Green
        Write-Host 'Move with Enhanced and FG 2x until one dropout, or 60 seconds. After a dropout keep moving for 5 seconds, then turn FG off for 10 seconds, turn NR off and exit.'
        # Wait for the user-controlled game, never for a compiler/process tree.
        $gameProcess.WaitForExit()
        $record['game_exit_code'] = $gameProcess.ExitCode
        $record['ended_qpc'] = [Diagnostics.Stopwatch]::GetTimestamp()
        Retain-MHWildsLog
        if ($RequireAssociationProfile) {
            $kinds = @{}
            foreach ($line in [IO.File]::ReadLines($record.retained_log)) {
                if ($line.Contains('session=' + $session + ' ') -and $line -match ' kind=([a-z0-9-]+) ') {
                    $kind=$Matches[1]; if (!$kinds.ContainsKey($kind)) { $kinds[$kind]=0 }; $kinds[$kind]++
                }
            }
            $record['association_event_counts']=$kinds
            $record['association_missing_kinds']=@(@('nr-api','nr-ledger','nr-pcl','nr-before-fg-forward') | Where-Object { !$kinds.ContainsKey($_) })
            if ($record.association_missing_kinds.Count) {
                Write-Host ('ASSOCIATION COVERAGE INCOMPLETE: ' + ($record.association_missing_kinds -join ', ')) -ForegroundColor Yellow
            }
        }
        $record.status = 'trace verified; game exited; log retained'
        Save-MHWildsSession
        if ($record.trace_budget_exhausted) { Write-Host 'Trace reached its event budget; later gameplay was not traced.' -ForegroundColor Yellow }
        Write-Host ('CAPTURE SAVED: ' + $evidence) -ForegroundColor Green
    } catch {
        $record.status = 'capture failed'
        $record['error'] = $_.Exception.Message
        try { Retain-MHWildsLog } catch { $record['retention_error'] = $_.Exception.Message }
        Save-MHWildsSession
        Write-Host ('CAPTURE FAILED: ' + $record.error) -ForegroundColor Red
        Write-Host ('Evidence retained: ' + $evidence)
        throw
    } finally {
        if ($null -ne $gameProcess) { $gameProcess.Dispose() }
    }
}
