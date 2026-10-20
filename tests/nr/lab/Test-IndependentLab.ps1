[CmdletBinding()]
param([Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $OutputRoot){throw 'FRESH_TEST_OUTPUT_REQUIRED'}
New-Item -ItemType Directory -Path $OutputRoot|Out-Null
$package=Join-Path $BuildRoot 'package';$lab=Join-Path $package 'NeuRoticLab.exe'
$description=& $lab describe | ConvertFrom-Json
$expectedRegistered=$description.implemented_cases.Count
$results=[Collections.Generic.List[object]]::new()
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function SaveJson([string]$p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 80),[Text.UTF8Encoding]::new($false))}
function InvokeCheck([string]$Name,[string]$Exe,[string[]]$Arguments,[int]$Expected=0,[string]$Contains=''){
    $log=Join-Path $OutputRoot ($Name+'.txt')
    & $Exe @Arguments *> $log
    $code=$LASTEXITCODE;$text=Get-Content -LiteralPath $log -Raw
    if($code -ne $Expected -or ($Contains -and !$text.Contains($Contains))){throw ('CHECK_FAILED '+$Name+' exit='+$code+' '+$text)}
    $results.Add(@{check=$Name;exit_code=$code;expected_exit_code=$Expected;status='PASS';log_sha256=(Hash $log)})
    Write-Output ('PASS '+$Name)
}
InvokeCheck 'verify-package' $lab @('verify-package') 0 'PACKAGE_VERIFIED'
$run=Join-Path $OutputRoot 'independent-run'
InvokeCheck 'independent-run' $lab @('run','--preset','independent','--output',$run) 0 'SUMMARY='
InvokeCheck 'validate-independent' $lab @('validate','--run',$run) 0 'RUN_VALIDATED'
$summary=Get-Content -LiteralPath (Join-Path $run 'SUMMARY.json') -Raw|ConvertFrom-Json
if($summary.counts.passed_local_invariants -ne $expectedRegistered -or $summary.counts.not_implemented -ne (22-$expectedRegistered) -or $summary.counts.requested -ne 22){throw 'UNEXPECTED_CASE_COUNTS'}
InvokeCheck 'completion-policy' (Join-Path $BuildRoot 'tests/independent_policies.exe') @() 0 'PASS completion'
InvokeCheck 'timing-mutations' (Join-Path $BuildRoot 'tests/timing_validation.exe') @($run) 0 'PASS 13 timing mutations'
$summaryHash=Hash (Join-Path $run 'SUMMARY.json')
InvokeCheck 'finalized-no-replay' $lab @('run','--plan',(Join-Path $run 'RUN_PLAN.json')) 0 'ALREADY_FINALIZED_NO_EFFECTS'
if((Hash (Join-Path $run 'SUMMARY.json')) -ne $summaryHash){throw 'FINALIZED_SUMMARY_CHANGED'}
# The worker must reject physical input before loading runtime config or selecting DXGI hardware.
$request=Get-Content -LiteralPath (Join-Path $run 'cases/NLAB-OBS-005-1/case-request.json') -Raw|ConvertFrom-Json
$request.variant_id='physical';$request.consent_flags.physical_provider=$true
$request.parameters.adapter='physical';$request.parameters.adapter_luid='00000000:00000001'
$requestPath=Join-Path $OutputRoot 'physical-request.json';SaveJson $requestPath $request
InvokeCheck 'worker-rejects-physical' (Join-Path $package 'LabWorker.exe') @('--request',$requestPath,'--output',(Join-Path $OutputRoot 'physical-must-not-exist')) 2 'THIS_EDITION_IS_CPU_WARP_ONLY'
if(Test-Path -LiteralPath (Join-Path $OutputRoot 'physical-must-not-exist')){throw 'PHYSICAL_REJECTION_TOUCHED_OUTPUT'}
foreach($spec in @(@('NLAB-OBS-005','owned/timestamps.bin'),@('NLAB-OBS-006','samples/light-1/worker-receipt.json'))){
    $mutant=Join-Path $OutputRoot ('omit-'+$spec[0]);Copy-Item -LiteralPath $run -Destination $mutant -Recurse
    $resultPath=Join-Path $mutant ('cases/'+$spec[0]+'-1/case-result.json')
    $result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
    $result.artifacts=@($result.artifacts|Where-Object relative_path -ne $spec[1]);SaveJson $resultPath $result
    InvokeCheck ('reject-uncommitted-'+$spec[0]) $lab @('validate','--run',$mutant) 2 'UNCOMMITTED_ARTIFACT:'
}
# Recommit a truthful unavailable observation while retaining a false PASS verdict.
# The semantic validator must reject it even though the updated bytes hash correctly.
$unavailable=Join-Path $OutputRoot 'unavailable-cannot-pass';Copy-Item -LiteralPath $run -Destination $unavailable -Recurse
$caseDir=Join-Path $unavailable 'cases/NLAB-OBS-005-1'
$workerPath=Join-Path $caseDir 'worker-receipt.json';$resultPath=Join-Path $caseDir 'case-result.json'
$worker=Get-Content -LiteralPath $workerPath -Raw|ConvertFrom-Json
$worker.observations.timing.available=$false;$worker.observations.timing.frequency_hz='0';$worker.observations.timing.frequency_hresult='80004005'
SaveJson $workerPath $worker
Remove-Item -LiteralPath (Join-Path $caseDir 'owned/timestamps.bin')
$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
$result.observations.worker=$worker.observations
$result.artifacts=@($result.artifacts|Where-Object relative_path -ne 'owned/timestamps.bin')
foreach($artifact in $result.artifacts){if($artifact.relative_path -eq 'worker-receipt.json'){$artifact.sha256=Hash $workerPath;$artifact.bytes=(Get-Item -LiteralPath $workerPath).Length}}
SaveJson $resultPath $result
InvokeCheck 'reject-unavailable-success' $lab @('validate','--run',$unavailable) 2 'UNAVAILABLE_TIMING_CANNOT_PASS'
$first=Join-Path $OutputRoot 'first-answers'
InvokeCheck 'first-answers' $lab @('run','--preset','first_answers','--output',$first) 0 'SUMMARY='
InvokeCheck 'validate-first-answers' $lab @('validate','--run',$first) 0 'RUN_VALIDATED'
$f=Get-Content -LiteralPath (Join-Path $first 'SUMMARY.json') -Raw|ConvertFrom-Json
if($f.counts.requested -ne 16 -or $f.counts.passed_local_invariants -ne 10 -or $f.counts.not_implemented -ne 6){throw 'FIRST_ANSWERS_SELECTION_CHANGED'}
SaveJson (Join-Path $OutputRoot 'TEST_RESULTS.json') @{schema_version=1;package_manifest_sha256=(Hash (Join-Path $package 'PACKAGE_MANIFEST.json'));checks=$results;independent_counts=$summary.counts;first_answers_counts=$f.counts;provider_execution='NOT_RUN';physical_execution='NOT_RUN';gpu_execution='WARP_ONLY'}
Write-Output ('TEST_RESULTS='+$OutputRoot)
