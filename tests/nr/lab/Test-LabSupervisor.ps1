param(
    [Parameter(Mandatory=$true)][string]$Supervisor,
    [Parameter(Mandatory=$true)][string]$Control,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (!(Test-Path -LiteralPath $Supervisor -PathType Leaf) -or !(Test-Path -LiteralPath $Control -PathType Leaf)) { throw 'SUPERVISOR_AND_CONTROL_REQUIRED' }
if (![IO.Path]::IsPathFullyQualified($EvidenceRoot) -or (Test-Path -LiteralPath $EvidenceRoot)) { throw 'FRESH_ABSOLUTE_EVIDENCE_REQUIRED' }
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
$results=@()
function Start-Control([string]$Name, [string[]]$ChildArgs, [int]$Timeout=5000, [string]$Cancel='') {
    $dir=Join-Path $EvidenceRoot $Name
    New-Item -ItemType Directory -Path $dir | Out-Null
    $info=[Diagnostics.ProcessStartInfo]::new($Supervisor)
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    foreach ($arg in @('--exe',$Control,'--receipt',$dir,'--timeout-ms',[string]$Timeout)) { $info.ArgumentList.Add($arg) }
    if ($Cancel) { $info.ArgumentList.Add('--cancel-file'); $info.ArgumentList.Add($Cancel) }
    $info.ArgumentList.Add('--')
    foreach ($arg in $ChildArgs) { $info.ArgumentList.Add($arg) }
    return @{process=[Diagnostics.Process]::Start($info); directory=$dir; watch=[Diagnostics.Stopwatch]::StartNew()}
}
function Finish-Control($Run, [string]$Status, [int]$ExitCode) {
    if (!$Run.process.WaitForExit(20000)) { $Run.process.Kill(); throw 'SUPERVISOR_EXCEEDED_BOUND' }
    $Run.watch.Stop()
    if ($Run.process.ExitCode -ne $ExitCode) { throw "SUPERVISOR_EXIT_EXPECTED_${ExitCode}_ACTUAL_$($Run.process.ExitCode)" }
    $receipt=Get-Content -LiteralPath (Join-Path $Run.directory 'supervisor.json') -Raw | ConvertFrom-Json
    if ($receipt.process_status -ne $Status -or !$receipt.child_drained) { throw 'SUPERVISION_STATUS_OR_DRAIN_INVALID' }
    if (Get-Process -Id $receipt.child_pid -ErrorAction SilentlyContinue) { throw 'WORKER_REMAINS_ALIVE' }
    return $receipt
}
function Wait-Ready([string]$Path, $Run) {
    $end=[DateTime]::UtcNow.AddSeconds(5)
    while (!(Test-Path -LiteralPath $Path)) {
        if ($Run.process.HasExited) { throw "SUPERVISOR_EXITED_BEFORE_READY_$($Run.process.ExitCode)" }
        if ([DateTime]::UtcNow -ge $end) { $Run.process.Kill(); throw 'CONTROL_NOT_READY' }
        Start-Sleep -Milliseconds 20
    }
}
# Removing cancellation observation must fail these real worker tests.
foreach ($cooperative in @($true,$false)) {
    $name=if ($cooperative) {'cancel-cooperative'} else {'cancel-forced-descendant'}
    $cancel=Join-Path $EvidenceRoot ($name+'.cancel')
    $ready=Join-Path $EvidenceRoot ($name+'.ready')
    $childArgs=if ($cooperative) {@('--cooperative-cancel',$cancel,$ready)} else {@('--descendant-timeout',$ready)}
    $run=Start-Control $name $childArgs 10000 $cancel
    Wait-Ready $ready $run
    [IO.File]::WriteAllText($cancel,'cancel')
    $receipt=Finish-Control $run 'cancelled' 130
    if ($receipt.timed_out -or $null -ne $receipt.unhandled_exception) { throw 'CANCELLATION_MISCLASSIFIED' }
    if ($cooperative -and $receipt.child_exit_code -ne 0) { throw 'COOPERATIVE_GRACE_MISSING' }
    if (!$cooperative -and $receipt.child_exit_code -ne 130) { throw 'FORCED_CANCEL_CODE_INVALID' }
    $detail=Get-Content -LiteralPath (Join-Path $run.directory 'lab-supervision.json') -Raw | ConvertFrom-Json
    if (!$detail.cancel_requested -or $detail.cancel_forced -eq $cooperative -or !$detail.capture_complete) { throw 'CANCEL_DETAIL_INVALID' }
    if ($cooperative -and (Get-Content -LiteralPath (Join-Path $run.directory 'runner-stdout.txt') -Raw) -notmatch 'COOPERATIVE_CANCEL_OBSERVED') { throw 'COOPERATIVE_MARKER_NOT_OBSERVED' }
    foreach ($pidValue in (Get-Content -LiteralPath $ready)) {
        if ([int]$pidValue -gt 0 -and (Get-Process -Id ([int]$pidValue) -ErrorAction SilentlyContinue)) { throw 'CANCELLED_DESCENDANT_REMAINS_ALIVE' }
    }
    $results += @{case=$name;elapsed_ms=$run.watch.ElapsedMilliseconds;status=$receipt.process_status}
}
# Regression suite observes actual CPU exception, dump, timeout, quoting and refusal.
& (Join-Path $PSScriptRoot '../../../tools/nr/p1/Test-Supervisor.ps1') -Supervisor $Supervisor -Control $Control -EvidenceRoot (Join-Path $EvidenceRoot 'original-p1')
foreach ($mode in @('descendant','descendant-timeout')) {
    $ready=Join-Path $EvidenceRoot ($mode+'.ready')
    $timeout=if ($mode -eq 'descendant') {5000} else {700}
    $run=Start-Control $mode @("--$mode",$ready) $timeout
    $status=if ($mode -eq 'descendant') {'exited'} else {'timeout'}
    $exitCode=if ($mode -eq 'descendant') {0} else {124}
    $null=Finish-Control $run $status $exitCode
    foreach ($pidValue in (Get-Content -LiteralPath $ready)) {
        if ([int]$pidValue -gt 0 -and (Get-Process -Id ([int]$pidValue) -ErrorAction SilentlyContinue)) { throw 'DESCENDANT_REMAINS_ALIVE' }
    }
    $results += @{case=$mode;elapsed_ms=$run.watch.ElapsedMilliseconds}
}
# Killing the owner must close its job and drain even an uncooperative descendant.
$ready=Join-Path $EvidenceRoot 'owner-terminated.ready'
$run=Start-Control 'owner-terminated' @('--descendant-timeout',$ready) 15000
Wait-Ready $ready $run
$run.process.Kill()
if (!$run.process.WaitForExit(5000)) { throw 'OWNER_TERMINATION_FAILED' }
$end=[DateTime]::UtcNow.AddSeconds(5)
foreach ($pidValue in (Get-Content -LiteralPath $ready)) {
    while ([int]$pidValue -gt 0 -and (Get-Process -Id ([int]$pidValue) -ErrorAction SilentlyContinue)) {
        if ([DateTime]::UtcNow -ge $end) { throw 'OWNER_TERMINATION_LEFT_DESCENDANT' }
        Start-Sleep -Milliseconds 20
    }
}
$results += @{case='owner-terminated';status='owned-tree-drained'}
$run=Start-Control 'bounded-output' @('--flood') 15000
$receipt=Finish-Control $run 'exited' 0
if ($receipt.child_exit_code -ne 0) { throw 'OUTPUT_COLLECTOR_BLOCKED_WORKER' }
foreach ($file in @('runner-stdout.txt','runner-stderr.txt')) {
    if ((Get-Item -LiteralPath (Join-Path $run.directory $file)).Length -ne 1048576) { throw 'OUTPUT_NOT_BOUNDED' }
}
$detail=Get-Content -LiteralPath (Join-Path $run.directory 'lab-supervision.json') -Raw | ConvertFrom-Json
if (!$detail.capture_complete -or !$detail.stdout.truncated -or !$detail.stderr.truncated -or $detail.stdout.bytes_seen -ne 2097152 -or $detail.stderr.bytes_seen -ne 2097152) { throw 'CAPTURE_TRUNCATION_NOT_REPORTED' }
$results += @{case='bounded-output';elapsed_ms=$run.watch.ElapsedMilliseconds}
# Reject invalid/stale cancellation requests before a worker can run.
foreach ($cancel in @('relative.cancel',(Join-Path $EvidenceRoot 'cancel-cooperative.cancel'))) {
    $name=if ([IO.Path]::IsPathFullyQualified($cancel)) {'stale-cancel'} else {'relative-cancel'}
    $ready=Join-Path $EvidenceRoot ($name+'.ready')
    $run=Start-Control $name @('--cooperative-cancel',$cancel,$ready) 5000 $cancel
    if (!$run.process.WaitForExit(5000) -or $run.process.ExitCode -ne 125 -or (Test-Path -LiteralPath $ready)) { throw 'INVALID_CANCEL_REQUEST_LAUNCHED_WORKER' }
    $results += @{case=$name;status='refused-before-launch'}
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'lab-test-results.json') -Encoding utf8
Write-Output 'LAB_SUPERVISOR_CONTROLS_VERIFIED=8; P1_REGRESSIONS_VERIFIED=6'
