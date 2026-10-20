#Requires -Version 7.0
<#
.SYNOPSIS
Exercises the packaged native controller with CPU controls and explicit WARP only.
.DESCRIPTION
PackageRoot must contain the complete built native package. EvidenceRoot must be
an absent absolute directory outside PackageRoot. The source package is never
modified. All destructive mutations target disposable copies below EvidenceRoot.

Success commands return zero. Negative commands must return nonzero with the
expected refusal. Receipt mutants refresh artifact sizes/hashes and the summary's
case-result hash so a checksum-only validator cannot pass this suite. A completed
run must remain byte-for-byte unchanged when run again; either success or an
explicit completed/immutable-run refusal is accepted. No physical consent or
private runtime configuration is supplied.

Produces per-command stdout/stderr logs and NEGATIVE_TEST_RESULTS.json. Package
copy/self-test, first_answers, plan, run, validate, cancel and export all use the
native CLI. This script is a development test and requires PowerShell 7; the
packaged runtime does not.
#>
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][string]$EvidenceRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not [IO.Path]::IsPathFullyQualified($PackageRoot) -or
    -not [IO.Path]::IsPathFullyQualified($EvidenceRoot)) { throw 'ABSOLUTE_PATHS_REQUIRED' }
$PackageRoot = [IO.Path]::GetFullPath($PackageRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot 'NeuRoticLab.exe') -PathType Leaf)) { throw 'LAB_CONTROLLER_MISSING' }
if (Test-Path -LiteralPath $EvidenceRoot) { throw 'FRESH_EVIDENCE_ROOT_REQUIRED' }
if ($EvidenceRoot.StartsWith($PackageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'EVIDENCE_MUST_BE_OUTSIDE_PACKAGE' }
$null = New-Item -ItemType Directory -Path $EvidenceRoot
$script:Checks = [Collections.Generic.List[object]]::new()
$script:CommandIndex = 0
$script:CurrentStep = 'setup'
$script:Utf8 = [Text.UTF8Encoding]::new($false)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Mutable-Path([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($EvidenceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "MUTATION_OUTSIDE_EVIDENCE:$full" }
    return $full
}
function Write-Json([string]$Path, $Value) {
    $full = Mutable-Path $Path
    [IO.File]::WriteAllText($full, (($Value | ConvertTo-Json -Depth 100) + "`n"), $script:Utf8)
}
function Read-Json([string]$Path) { return (Get-Content -LiteralPath $Path -Raw -Encoding utf8 | ConvertFrom-Json -Depth 100) }
function File-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Copy-FreshTree([string]$Source, [string]$Destination) {
    $Destination = Mutable-Path $Destination
    Assert-True (-not (Test-Path -LiteralPath $Destination)) 'COPY_DESTINATION_NOT_FRESH'
    $null = New-Item -ItemType Directory -Path $Destination
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        Assert-True (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'COPY_REPARSE_POINT_REFUSED'
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse
    }
    return $Destination
}
function Record-Pass([string]$Name, $Details = $null) {
    $script:Checks.Add([ordered]@{ name = $Name; passed = $true; details = $Details })
    Write-Host "PASS $Name"
}
function Invoke-Lab([string]$Exe, [string[]]$Arguments, [string]$Expectation = 'Success', [string]$ReasonPattern = '') {
    $script:CommandIndex++
    $stem = '{0:D3}-{1}' -f $script:CommandIndex, $script:CurrentStep
    $stdout = Join-Path $EvidenceRoot ($stem + '.stdout.txt')
    $stderr = Join-Path $EvidenceRoot ($stem + '.stderr.txt')
    $info = [Diagnostics.ProcessStartInfo]::new($Exe)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = Split-Path -Parent $Exe
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $outStream = [IO.File]::Create($stdout)
    $errStream = [IO.File]::Create($stderr)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        Assert-True ($process.Start()) 'CONTROLLER_START_FAILED'
        $outCopy = $process.StandardOutput.BaseStream.CopyToAsync($outStream)
        $errCopy = $process.StandardError.BaseStream.CopyToAsync($errStream)
        if (-not $process.WaitForExit(1200000)) {
            $process.Kill($true)
            $null = $process.WaitForExit(10000)
            throw 'CONTROLLER_TEST_DEADLINE_EXCEEDED'
        }
        $outCopy.GetAwaiter().GetResult()
        $errCopy.GetAwaiter().GetResult()
        $code = $process.ExitCode
    } finally {
        $outStream.Dispose()
        $errStream.Dispose()
        $process.Dispose()
    }
    $text = [IO.File]::ReadAllText($stdout) + "`n" + [IO.File]::ReadAllText($stderr)
    if ($Expectation -eq 'Success') { Assert-True ($code -eq 0) "EXPECTED_SUCCESS:$code $text" }
    elseif ($Expectation -eq 'Reject') {
        Assert-True ($code -ne 0) "NEGATIVE_CASE_ACCEPTED:$($Arguments -join ' ')"
        if ($ReasonPattern) { Assert-True ($text -match $ReasonPattern) "WRONG_REFUSAL:$ReasonPattern actual:$text" }
    } elseif ($Expectation -eq 'Repeat') {
        Assert-True (($code -eq 0) -or ($text -match 'COMPLET|IMMUTABLE|ALREADY|EXISTS|RESUME')) "UNEXPECTED_REPEAT_FAILURE:$code $text"
    } elseif ($Expectation -ne 'Any') { throw 'INVALID_TEST_EXPECTATION' }
    return [pscustomobject]@{ exit_code = $code; stdout = $stdout; stderr = $stderr; text = $text }
}
function Assert-NoLaunch([string]$Run, [bool]$AllowUnstartedResults = $false) {
    $forbidden = @('supervisor.json', 'lab-supervision.json', 'worker-receipt.json', 'runner.json', 'runner-stdout.txt', 'runner-stderr.txt')
    if (-not $AllowUnstartedResults) { $forbidden += @('case-result.json', 'events.jsonl') }
    $found = @(Get-ChildItem -LiteralPath (Join-Path $Run 'cases') -Recurse -File | Where-Object { $_.Name -in $forbidden })
    $foundNames = @($found | ForEach-Object { $_.FullName }) -join ','
    Assert-True ($found.Count -eq 0) "REJECTED_PLAN_LAUNCHED_OR_FINALIZED_CASES:$foundNames"
    if ($AllowUnstartedResults) {
        foreach ($result in Get-ChildItem -LiteralPath (Join-Path $Run 'cases') -Recurse -Filter 'case-result.json' -File) {
            Assert-True ((Read-Json $result.FullName).execution.state -eq 'NOT_STARTED') 'CANCELLED_PLAN_EXECUTED_CASE'
        }
    }
}
function Tree-Hashes([string]$Root) {
    $hashes = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
        $hashes[[IO.Path]::GetRelativePath($Root, $file.FullName)] = File-Hash $file.FullName
    }
    return $hashes
}
function Assert-SameTree($Before, $After) {
    Assert-True ($Before.Count -eq $After.Count) 'REPEATED_RUN_CHANGED_FILE_COUNT'
    foreach ($name in $Before.Keys) {
        Assert-True ($After.ContainsKey($name) -and $After[$name] -eq $Before[$name]) "REPEATED_RUN_CHANGED:$name"
    }
}
function Result-Context([string]$Run, [string]$CaseId) {
    $summaryPath = Join-Path $Run 'SUMMARY.json'
    $summary = Read-Json $summaryPath
    $entries = @($summary.case_results | Where-Object { $_.case_id -eq $CaseId })
    Assert-True ($entries.Count -eq 1) "EXACT_CASE_REQUIRED:$CaseId"
    $resultPath = Join-Path $Run $entries[0].result_path
    return [pscustomobject]@{
        run = $Run; summary_path = $summaryPath; summary = $summary; entry = $entries[0]
        path = $resultPath; directory = (Split-Path -Parent $resultPath); result = (Read-Json $resultPath)
    }
}
function Refresh-Artifact($Context, [string]$Relative) {
    $artifacts = @($Context.result.artifacts | Where-Object { $_.relative_path -eq $Relative })
    Assert-True ($artifacts.Count -eq 1) "EXACT_ARTIFACT_REQUIRED:$Relative"
    $file = Join-Path $Context.directory $Relative
    $artifacts[0].sha256 = File-Hash $file
    $artifacts[0].bytes = (Get-Item -LiteralPath $file).Length
}
function Publish-MutatedResult($Context) {
    Write-Json $Context.path $Context.result
    $Context.entry.sha256 = File-Hash $Context.path
    Write-Json $Context.summary_path $Context.summary
}

try {
    $script:CurrentStep = 'portable-package'
    $package = Copy-FreshTree $PackageRoot (Join-Path $EvidenceRoot 'portable package Ω with spaces')
    $exe = Join-Path $package 'NeuRoticLab.exe'
    $null = Invoke-Lab $exe @('verify-package')
    $self = Join-Path $EvidenceRoot 'self test Ω'
    $null = Invoke-Lab $exe @('self-test', '--output', $self)
    $null = Invoke-Lab $exe @('validate', '--run', $self)
    Record-Pass 'portable_unicode_package_and_self_test'

    $script:CurrentStep = 'first-answers'
    $first = Join-Path $EvidenceRoot 'first answers'
    $null = Invoke-Lab $exe @('run', '--preset', 'first_answers', '--output', $first)
    $null = Invoke-Lab $exe @('validate', '--run', $first)
    $firstSummary = Read-Json (Join-Path $first 'SUMMARY.json')
    Assert-True ($firstSummary.counts.requested -eq 16) 'FIRST_ANSWERS_SELECTION_CHANGED'
    foreach ($entry in $firstSummary.case_results) {
        $result = Read-Json (Join-Path $first $entry.result_path)
        if ($entry.case_id -like 'NLAB-NR-*') { Assert-True ($result.execution.state -eq 'NOT_STARTED') 'PROVIDER_EXECUTED_WITHOUT_CONSENT' }
        if ($entry.case_id -in @('NLAB-OBS-001', 'NLAB-OBS-002')) { Assert-True ($result.evidence_layer -eq 'WARP_OWNED') 'PHYSICAL_ADAPTER_EXECUTED' }
    }
    Record-Pass 'first_answers_no_physical_or_provider_execution'

    foreach ($mutation in @('missing-schema', 'tampered-worker')) {
        $script:CurrentStep = $mutation
        $damaged = Copy-FreshTree $package (Join-Path $EvidenceRoot $mutation)
        if ($mutation -eq 'missing-schema') {
            Remove-Item -LiteralPath (Mutable-Path (Join-Path $damaged 'schemas/case-request.schema.json'))
            $pattern = 'PACKAGE_INCOMPLETE|PACKAGE_LOCK_FAILED'
        } else {
            $worker = Mutable-Path (Join-Path $damaged 'LabWorker.exe')
            $bytes = [IO.File]::ReadAllBytes($worker)
            Assert-True ($bytes.Length -gt 0) 'WORKER_EMPTY'
            $bytes[0] = $bytes[0] -bxor 1
            [IO.File]::WriteAllBytes($worker, $bytes)
            $pattern = 'PACKAGE_HASH_MISMATCH'
        }
        $null = Invoke-Lab (Join-Path $damaged 'NeuRoticLab.exe') @('verify-package') 'Reject' $pattern
        Record-Pass $mutation
    }

    foreach ($mutation in @('frozen-plan', 'frozen-request')) {
        $script:CurrentStep = $mutation
        $run = Join-Path $EvidenceRoot $mutation
        $null = Invoke-Lab $exe @('plan', '--preset', 'self_test', '--output', $run)
        $planPath = Join-Path $run 'RUN_PLAN.json'
        $plan = Read-Json $planPath
        if ($mutation -eq 'frozen-plan') { $plan.run_id = 'tampered-plan-identity'; Write-Json $planPath $plan }
        else {
            $requestPath = Join-Path $run $plan.ordered_cases[0].request_relative_path
            $request = Read-Json $requestPath
            $request.request_nonce = 'tampered-request-nonce'
            Write-Json $requestPath $request
        }
        $null = Invoke-Lab $exe @('run', '--plan', $planPath) 'Reject' 'FROZEN_PLAN_CHANGED|REQUEST_PLAN_MISMATCH'
        Assert-NoLaunch $run
        Record-Pass $mutation
    }

    foreach ($missing in @('plan', 'runtime', 'definition')) {
        $script:CurrentStep = 'missing-commitment-' + $missing
        $run = Join-Path $EvidenceRoot $script:CurrentStep
        $null = Invoke-Lab $exe @('plan', '--preset', 'self_test', '--output', $run)
        $planPath = Join-Path $run 'RUN_PLAN.json'
        $plan = Read-Json $planPath
        $commitmentsPath = Join-Path $run 'PLAN_HASHES.json'
        $commitments = Read-Json $commitmentsPath
        $entryName = switch ($missing) {
            'plan' { 'RUN_PLAN.json' }
            'runtime' { 'LOCAL_RUNTIME.json' }
            'definition' {
                ([IO.Path]::GetDirectoryName($plan.ordered_cases[0].request_relative_path).Replace('\', '/')) + '/case-definition.json'
            }
        }
        Assert-True ($null -ne $commitments.files.PSObject.Properties[$entryName]) "COMMITMENT_FIXTURE_MISSING:$entryName"
        $commitments.files.PSObject.Properties.Remove($entryName)
        Write-Json $commitmentsPath $commitments
        $null = Invoke-Lab $exe @('run', '--plan', $planPath) 'Reject' 'COMMITMENT|HASH|PLAN|FROZEN'
        Assert-NoLaunch $run
        Record-Pass ('missing_' + $missing + '_commitment_rejected_before_execution')
    }

    $script:CurrentStep = 'successful-not-started'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-CTL-002'
    Assert-True ($context.result.verdict -eq 'PASS_INVARIANT') 'SUCCESS_FIXTURE_REQUIRED'
    $context.result.execution.state = 'NOT_STARTED'
    $context.result.execution.pid = $null
    $context.result.execution.raw_exit_code = $null
    $context.result.execution.exception_code = $null
    Publish-MutatedResult $context
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'SUCCESS|EXECUTION|PROCESS|NOT_STARTED'
    Record-Pass 'successful_not_started_claim_with_refreshed_result_hash'

    $script:CurrentStep = 'stale-result-nonce'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-CTL-002'
    $context.result.request_nonce = 'stale-completed-result'
    Publish-MutatedResult $context
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'RESULT_IDENTITY_MISMATCH'
    Record-Pass 'stale_result_nonce_with_refreshed_result_hash'

    $script:CurrentStep = 'stale-worker-nonce'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-CTL-002'
    $rawPath = Join-Path $context.directory 'worker-receipt.json'
    $raw = Read-Json $rawPath
    $raw.request_nonce = 'stale-worker-observation'
    Write-Json $rawPath $raw
    Refresh-Artifact $context 'worker-receipt.json'
    Publish-MutatedResult $context
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'OBSERVATION_IDENTITY'
    Record-Pass 'stale_worker_nonce_with_refreshed_artifact_hash'

    $script:CurrentStep = 'owned-fence-sentinel'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-OBS-002'
    $rawPath = Join-Path $context.directory 'worker-receipt.json'
    $raw = Read-Json $rawPath
    $raw.observations.fences[0].completed_value = '18446744073709551615'
    Write-Json $rawPath $raw
    $context.result.observations.worker = $raw.observations
    Refresh-Artifact $context 'worker-receipt.json'
    Publish-MutatedResult $context
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'OWNED_TEXT_MISMATCH'
    Record-Pass 'UINT64_MAX_fence_with_refreshed_artifact_hash'

    $script:CurrentStep = 'missing-owned-raw'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-OBS-001'
    Remove-Item -LiteralPath (Mutable-Path (Join-Path $context.directory 'owned/output.bin'))
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'ARTIFACT_HASH_MISMATCH|RAW_ARTIFACT_MISSING'
    Record-Pass 'missing_owned_raw_rejected'

    $script:CurrentStep = 'boolean-supervisor-exit'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $context = Result-Context $run 'NLAB-CTL-002'
    $processPath = Join-Path $context.directory 'supervisor.json'
    $process = Read-Json $processPath
    $process.child_exit_code = $false
    Write-Json $processPath $process
    $context.result.observations.process = $process
    $context.result.execution.raw_exit_code = $null
    Refresh-Artifact $context 'supervisor.json'
    Publish-MutatedResult $context
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'PROCESS_NOT_SUCCESSFUL|PROCESS_OBSERVATION_MISMATCH'
    Record-Pass 'boolean_exit_zero_with_refreshed_artifact_hash'

    $script:CurrentStep = 'summary-counts'
    $run = Copy-FreshTree $self (Join-Path $EvidenceRoot $script:CurrentStep)
    $summaryPath = Join-Path $run 'SUMMARY.json'
    $summary = Read-Json $summaryPath
    $summary.counts.executed = $summary.counts.executed + 1
    Write-Json $summaryPath $summary
    $null = Invoke-Lab $exe @('validate', '--run', $run) 'Reject' 'COUNT|SUMMARY'
    Record-Pass 'mutated_summary_counts_rejected'

    $script:CurrentStep = 'completed-run-repeat'
    $before = Tree-Hashes $self
    $null = Invoke-Lab $exe @('run', '--plan', (Join-Path $self 'RUN_PLAN.json')) 'Repeat'
    Assert-SameTree $before (Tree-Hashes $self)
    $null = Invoke-Lab $exe @('validate', '--run', $self)
    Assert-SameTree $before (Tree-Hashes $self)
    Record-Pass 'completed_run_immutable_no_rerun_effects'

    $script:CurrentStep = 'cancel-before-launch'
    $run = Join-Path $EvidenceRoot $script:CurrentStep
    $null = Invoke-Lab $exe @('plan', '--preset', 'self_test', '--output', $run)
    $null = Invoke-Lab $exe @('cancel', '--run', $run)
    Assert-True (Test-Path -LiteralPath (Join-Path $run 'CANCEL') -PathType Leaf) 'CANCEL_MARKER_MISSING'
    $null = Invoke-Lab $exe @('run', '--plan', (Join-Path $run 'RUN_PLAN.json')) 'Any'
    Assert-NoLaunch $run $true
    Record-Pass 'prelaunch_cancellation_prevents_worker_execution'

    $script:CurrentStep = 'review-export'
    $zip = Join-Path $EvidenceRoot 'review.zip'
    $null = Invoke-Lab $exe @('export', '--run', $first, '--output', $zip)
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        Assert-True ($archive.Entries.Count -gt 0) 'EMPTY_REVIEW_EXPORT'
        foreach ($entry in $archive.Entries) {
            Assert-True ($entry.FullName -notmatch '(^|/)\.\.(/|$)|^[a-zA-Z]:|^[/\\]') 'UNSAFE_EXPORT_MEMBER'
            Assert-True ($entry.FullName -notmatch '(?i)\.(dll|exe|dmp)$|(^|/)(LOCAL_RUNTIME|runtime|config)\.json$') 'PRIVATE_EXPORT_MEMBER'
        }
    } finally { $archive.Dispose() }
    Record-Pass 'native_review_export_excludes_private_payloads'

    Write-Json (Join-Path $EvidenceRoot 'NEGATIVE_TEST_RESULTS.json') ([ordered]@{
        schema_version = 1; success = $true; checks = $script:Checks; physical_provider_authorized = $false
    })
    'LAB_NEGATIVE_INTEGRATION_PASS'
} catch {
    $script:Checks.Add([ordered]@{ name = $script:CurrentStep; passed = $false; detail = $_.Exception.Message })
    Write-Json (Join-Path $EvidenceRoot 'NEGATIVE_TEST_RESULTS.json') ([ordered]@{
        schema_version = 1; success = $false; checks = $script:Checks; physical_provider_authorized = $false
    })
    throw
}
