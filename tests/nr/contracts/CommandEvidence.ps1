#requires -Version 5.1
# NEW candidate evidence helpers. No command/hash/export failure is interpreted as a C15 verdict.
Set-StrictMode -Version Latest
function Write-NrJsonNew([string]$Path, $Value) {
    $stream=$null
    try {
        $json=$Value | ConvertTo-Json -Depth 25 -Compress
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($json)
        $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true)
    } finally { if ($stream) { $stream.Dispose() } }
}
function Get-NrHashReceipt([string]$Path) {
    $r=[ordered]@{path=$Path;state='Unavailable';sha256=$null;bytes=$null;error=$null}
    $stream=$null; $hasher=$null
    try {
        $item=Get-Item -LiteralPath $Path -Force -ErrorAction Stop
        if ($item.PSIsContainer -or (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw 'Only a regular non-reparse file can supply this hash receipt.'
        }
        # VS developer environments can omit Windows PowerShell module discovery.
        # Hash through the framework already required by this supervisor.
        $stream=[IO.File]::Open($item.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        $hasher=[Security.Cryptography.SHA256]::Create()
        $r.sha256=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()
        $r.bytes=$stream.Length; $r.state='Recorded'
    } catch { $r.error=$_.Exception.ToString() }
    finally { if ($hasher) { $hasher.Dispose() }; if ($stream) { $stream.Dispose() } }
    return $r
}
function Save-NrStage($State,[string]$Name,$Value) {
    try { Write-NrJsonNew (Join-Path $State.run.path $Name) $Value; return $true }
    catch {
        $State.infrastructure.Add([ordered]@{stage=$Name;error=$_.Exception.ToString()})
        Write-Warning ('Evidence stage could not be persisted: ' + $Name)
        return $false
    }
}
function Invoke-NrRecordedCommand($State,[string]$Label,[string]$Executable,[string[]]$Arguments,[int]$TimeoutSeconds) {
    if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid fixed command label.' }
    $id=('{0:D4}-{1}' -f ($State.commands.Count+1),[Guid]::NewGuid().ToString('N'))
    $stem=$id+'-'+$Label
    $environmentOverrides=@{}
    if ([IO.Path]::GetFileName($Executable) -in @('cl.exe','link.exe','lib.exe')) {
        # MSVC's resident VCTIP descendant otherwise prevents this job from becoming empty.
        # This is a child-only compiler telemetry opt-out, qualified on MSVC 14.44.35207.
        $environmentOverrides['VSTEL_EWDK']='1'
    }
    $intent=[ordered]@{run_id=$State.run.run_id;attempt_id=$id;label=$Label;executable=$Executable
        arguments=@($Arguments);windows_argument_string=(($Arguments|ForEach-Object{Quote-NrArgument $_}) -join ' ')
        environment_overrides=$environmentOverrides
        working_directory=$State.repository;intent_utc=[DateTime]::UtcNow.ToString('o')
        stdout=(Join-Path $State.run.path ($stem+'.stdout.log'));stderr=(Join-Path $State.run.path ($stem+'.stderr.log'))}
    $record=[ordered]@{intent=$intent;intent_persisted=$false;pre_hash=$null;process=$null
        result_persisted=$false;post_hash=$null;hash_stage_persisted=$false;error=$null;command_succeeded=$false}
    # Record an attempt in memory before I/O, hashing, native launch, or optional identity enrichment.
    $State.commands.Add($record)
    try {
        $record.intent_persisted=Save-NrStage $State ($stem+'.intent.json') $intent
        if (!$record.intent_persisted) { throw 'Intent persistence failed: command was not launched.' }
        $record.pre_hash=Get-NrHashReceipt $Executable
        if (!(Save-NrStage $State ($stem+'.pre-hash.json') $record.pre_hash) -or $record.pre_hash.state -ne 'Recorded') {
            throw 'Prelaunch executable hash unavailable: command was not launched.'
        }
        $record.process=Invoke-NrProcessGroup $Executable $Arguments $State.repository $intent.stdout $intent.stderr $TimeoutSeconds $environmentOverrides
    } catch { $record.error=$_.Exception.ToString() }
    finally {
        # No hash call, artifact enumeration, or optional compiler metadata occurs before this receipt.
        $outcome=[ordered]@{run_id=$State.run.run_id;attempt_id=$id;intent=$intent;process=$record.process
            error=$record.error;ended_utc=[DateTime]::UtcNow.ToString('o')}
        $record.result_persisted=Save-NrStage $State ($stem+'.result.json') $outcome
    }
    $record.post_hash=Get-NrHashReceipt $Executable
    $hashes=[ordered]@{run_id=$State.run.run_id;attempt_id=$id;before=$record.pre_hash;after=$record.post_hash
        stdout=(Get-NrHashReceipt $intent.stdout);stderr=(Get-NrHashReceipt $intent.stderr)}
    $record.hash_stage_persisted=Save-NrStage $State ($stem+'.hashes.json') $hashes
    $p=$record.process
    $stable=$record.pre_hash -and $record.pre_hash.state -eq 'Recorded' -and $record.post_hash.state -eq 'Recorded' -and
        $record.pre_hash.sha256 -eq $record.post_hash.sha256
    $contained=$p -and $p.group_assigned_before_resume -and $p.parent_exit_observed -and $p.group_empty_observed
    $capture=$p -and $p.capture_complete -and $hashes.stdout.state -eq 'Recorded' -and $hashes.stderr.state -eq 'Recorded'
    $infrastructureGood=$null -eq $record.error -and $contained -and $capture -and $stable -and
        !$p.timed_out -and $p.errors.Count -eq 0 -and $p.cleanup_errors.Count -eq 0 -and
        $record.intent_persisted -and $record.result_persisted -and $record.hash_stage_persisted
    if (!$infrastructureGood) {
        throw 'Command infrastructure is incomplete/uncertain. Stop the run; preserve every prior stage and do not reuse this directory.'
    }
    $record.command_succeeded=$p.numeric_exit_code -eq 0
    return $record.command_succeeded
}
