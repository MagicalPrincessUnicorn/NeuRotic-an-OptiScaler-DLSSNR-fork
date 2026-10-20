#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ScratchDirectory,
    [string]$CompilerExecutable)
# Candidate fault fixtures. NOT RUN in Chat; run separately on a disposable Windows x64 host.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NativeProcessGroup.ps1')
. (Join-Path $PSScriptRoot 'CommandEvidence.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$run=$null;$checks=[Collections.Generic.List[object]]::new();$failed=$null
function Check-NrInfrastructure([string]$Id,[bool]$Condition) {
    $checks.Add(@{id=$Id;expected=$true;actual=$Condition})
    if (!$Condition) { throw ('Infrastructure fixture mismatch: '+$Id) }
}
function Encoded-NrCommand([string]$Script) { return [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Script)) }
try {
    $run=New-NrOwnedRunDirectory $ScratchDirectory $repo
    $powerShell=(Get-Command powershell.exe -CommandType Application).Source
    $knownHashPath=Join-Path $run.path 'known-hash.dat'
    [IO.File]::WriteAllBytes($knownHashPath,[Text.Encoding]::UTF8.GetBytes('abc'))
    $knownHash=Get-NrHashReceipt $knownHashPath
    Write-NrJsonNew (Join-Path $run.path 'known-hash.receipt.json') $knownHash
    Check-NrInfrastructure 'R07-known-file-hash-recorded' ($knownHash.state -eq 'Recorded' -and $knownHash.bytes -eq 3 -and
        $knownHash.sha256 -eq 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' -and $null -eq $knownHash.error)
    $success=Invoke-NrProcessGroup $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand '[Console]::Out.Write("zero-out"); [Console]::Error.Write("zero-err"); exit 0')) $repo `
        (Join-Path $run.path 'success.stdout.log') (Join-Path $run.path 'success.stderr.log') 3
    Write-NrJsonNew (Join-Path $run.path 'success.process.json') $success
    Check-NrInfrastructure 'R06-zero-exit-completes-without-timeout' ($success.numeric_exit_code -eq 0 -and
        $success.group_assigned_before_resume -and $success.parent_exit_observed -and $success.group_empty_observed -and
        $success.capture_complete -and !$success.timed_out -and !$success.termination_requested -and
        $success.errors.Count -eq 0 -and $success.cleanup_errors.Count -eq 0)
    $parentSentinel=[Environment]::GetEnvironmentVariable('NR_SUPERVISOR_CANARY')
    $childEnvironment=Invoke-NrProcessGroup $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand '[Console]::Out.Write($env:NR_SUPERVISOR_CANARY); exit 0')) $repo `
        (Join-Path $run.path 'environment.stdout.log') (Join-Path $run.path 'environment.stderr.log') 3 @{NR_SUPERVISOR_CANARY='child-only-value'}
    Write-NrJsonNew (Join-Path $run.path 'environment.process.json') $childEnvironment
    Check-NrInfrastructure 'R06-child-environment-isolated' ($childEnvironment.numeric_exit_code -eq 0 -and !$childEnvironment.timed_out -and
        $childEnvironment.group_empty_observed -and $childEnvironment.capture_complete -and
        [IO.File]::ReadAllText((Join-Path $run.path 'environment.stdout.log')) -eq 'child-only-value' -and
        [Environment]::GetEnvironmentVariable('NR_SUPERVISOR_CANARY') -eq $parentSentinel)
    $normal=Invoke-NrProcessGroup $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand '[Console]::Out.Write("raw-out"); [Console]::Error.Write("raw-err"); exit 7')) $repo `
        (Join-Path $run.path 'normal.stdout.log') (Join-Path $run.path 'normal.stderr.log') 10
    Write-NrJsonNew (Join-Path $run.path 'normal.process.json') $normal
    Check-NrInfrastructure 'R06-nonzero-is-contained' ($normal.numeric_exit_code -eq 7 -and $normal.group_empty_observed -and
        $normal.capture_complete -and !$normal.timed_out -and $normal.errors.Count -eq 0 -and $normal.cleanup_errors.Count -eq 0)
    Check-NrInfrastructure 'R06-stdout-raw' (([IO.File]::ReadAllText((Join-Path $run.path 'normal.stdout.log'))) -eq 'raw-out')
    Check-NrInfrastructure 'R06-stderr-raw' (([IO.File]::ReadAllText((Join-Path $run.path 'normal.stderr.log'))) -eq 'raw-err')
    $commandState=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new();infrastructure=[Collections.Generic.List[object]]::new()}
    $recordedZero=Invoke-NrRecordedCommand $commandState 'recorded-zero' $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand 'exit 0')) 3
    Check-NrInfrastructure 'R07-recorded-zero-succeeds' ($recordedZero -and $commandState.commands[0].command_succeeded)
    $recordedNonzero=Invoke-NrRecordedCommand $commandState 'recorded-nonzero' $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand 'exit 7')) 3
    Check-NrInfrastructure 'R07-recorded-nonzero-is-test-failure' (!$recordedNonzero -and
        $commandState.commands[1].process.numeric_exit_code -eq 7 -and $null -eq $commandState.commands[1].error)
    if ($CompilerExecutable) {
        if ($env:CL -or $env:_CL_ -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'Compiler canary requires clean x64 developer environment.' }
        $compiler=(Get-Command $CompilerExecutable -CommandType Application).Source
        $canarySource=Join-Path $run.path 'compiler-canary.cpp'
        $canaryExe=Join-Path $run.path 'compiler-canary.exe'
        [IO.File]::WriteAllText($canarySource,'int main() { return 0; }')
        $compiled=Invoke-NrRecordedCommand $commandState 'compiler-canary' $compiler @('/nologo','/std:c++20','/EHsc','/W4','/WX','/Y-','/UNDEBUG',
            $canarySource,('/Fe'+$canaryExe),('/Fo'+(Join-Path $run.path 'compiler-canary.obj'))) 10
        Check-NrInfrastructure 'R06-compiler-group-completes-naturally' ($compiled -and
            !$commandState.commands[2].process.timed_out -and !$commandState.commands[2].process.termination_requested)
        $executed=Invoke-NrRecordedCommand $commandState 'compiled-zero' $canaryExe @() 3
        Check-NrInfrastructure 'R06-compiled-zero-succeeds' $executed
    }
    # The child closes its own standard output/error before sleeping; pipe EOF would not prove cleanup.
    $child=Encoded-NrCommand '[Console]::Out.Close(); [Console]::Error.Close(); Start-Sleep -Seconds 30'
    $parent='$p=[Diagnostics.ProcessStartInfo]::new();$p.FileName="'+$powerShell.Replace('"','`"')+'";'+
        '$p.Arguments="-NoProfile -EncodedCommand '+$child+'";$p.UseShellExecute=$false;'+
        '$null=[Diagnostics.Process]::Start($p);exit 0'
    $timeout=Invoke-NrProcessGroup $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand $parent)) $repo `
        (Join-Path $run.path 'child.stdout.log') (Join-Path $run.path 'child.stderr.log') 2
    Write-NrJsonNew (Join-Path $run.path 'child.process.json') $timeout
    Check-NrInfrastructure 'R06-descendant-outlives-parent-detected' ($timeout.timed_out -and $timeout.termination_requested)
    Check-NrInfrastructure 'R06-job-empty-after-cleanup' ($timeout.parent_exit_observed -and $timeout.group_empty_observed)
    $state=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new();infrastructure=[Collections.Generic.List[object]]::new()}
    $stopped=$false
    try { $null=Invoke-NrRecordedCommand $state 'timeout-stop' $powerShell @('-NoProfile','-EncodedCommand',(Encoded-NrCommand 'Start-Sleep -Seconds 30')) 2 }
    catch { $stopped=$true }
    Check-NrInfrastructure 'R06-timeout-stops-even-after-cleanup' ($stopped -and $state.commands.Count -eq 1 -and !$state.commands[0].command_succeeded -and
        $null -ne $state.commands[0].process -and $state.commands[0].process.timed_out -and
        $state.commands[0].process.group_empty_observed -and $state.commands[0].process.cleanup_errors.Count -eq 0)
    Check-NrInfrastructure 'R07-timeout-result-retained' ($state.commands[0].intent_persisted -and $state.commands[0].result_persisted)
    $failedLaunch=$false
    try { $null=Invoke-NrRecordedCommand $state 'missing-executable' (Join-Path $run.path 'absent.exe') @() 2 }
    catch { $failedLaunch=$true }
    Check-NrInfrastructure 'R07-prehash-failure-retains-attempt' ($failedLaunch -and $state.commands.Count -eq 2 -and
        $state.commands[1].intent_persisted -and $state.commands[1].result_persisted -and $null -eq $state.commands[1].process)
    # Hash unavailability and export collision are independent from already-persisted result metadata.
    $lockedPath=Join-Path $run.path 'locked-hash.dat'
    $locked=[IO.File]::Open($lockedPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $hash=Get-NrHashReceipt $lockedPath; Check-NrInfrastructure 'R07-hash-failure-is-record' ($hash.state -eq 'Unavailable' -and $null -ne $hash.error) }
    finally { $locked.Dispose() }
    Write-NrJsonNew (Join-Path $run.path 'export-collision.json') @{literal='prior';value=17}
    $prior=Get-NrHashReceipt (Join-Path $run.path 'export-collision.json')
    $saved=Save-NrStage $state 'export-collision.json' @{literal='replacement'}
    $after=Get-NrHashReceipt (Join-Path $run.path 'export-collision.json')
    Check-NrInfrastructure 'CP03-no-stage-overwrite' (!$saved -and $prior.sha256 -eq $after.sha256)
    $rejected=$false
    try { $other=New-NrOwnedRunDirectory $run.path $repo; $other.lock.Dispose() } catch { $rejected=$true }
    Check-NrInfrastructure 'CP03-existing-run-rejected' $rejected
    $inside=Join-Path $repo ('nr-forbidden-output-'+[Guid]::NewGuid().ToString('N'))
    $rejected=$false
    try { $other=New-NrOwnedRunDirectory $inside $repo; $other.lock.Dispose() } catch { $rejected=$true }
    Check-NrInfrastructure 'CP03-inside-source-no-write' ($rejected -and !(Test-Path -LiteralPath $inside))
} catch { $failed=$_.Exception.ToString() }
finally {
    if ($run) {
        try { Write-NrJsonNew (Join-Path $run.path 'infrastructure-fixture-results.json') @{checks=$checks.ToArray();failure=$failed;
            machine_context='Actual host when executed';uncovered=@('Concurrent reservations','SUBST/8.3 aliases','Junction ancestry',
            'Native assignment/query failure injection','Out-of-space/power-loss durability','CMD paths with spaces/unicode/exclamation',
            'Executable removed between termination and post-hash');gate_approval='NOT_PERFORMED'} }
        catch { Write-Error -ErrorAction Continue $_; $failed='Evidence publication failed.' }
        $run.lock.Dispose()
    }
}
if ($failed) { Write-Error -ErrorAction Continue $failed; exit 1 }
exit 0
