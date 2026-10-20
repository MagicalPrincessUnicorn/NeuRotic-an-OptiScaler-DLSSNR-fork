[CmdletBinding()]
param([Parameter(Mandatory)][string]$Package,[Parameter(Mandatory)][string]$Run,[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Output){throw 'FRESH_TEST_OUTPUT_REQUIRED'}
New-Item -ItemType Directory -Path $Output|Out-Null
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))}
$lab=Join-Path $Package 'NeuRoticLab.exe'
$mutant=Join-Path $Output 'changed-owner-pid';Copy-Item -LiteralPath $Run -Destination $mutant -Recurse
$case=Join-Path $mutant 'cases/NLAB-EVD-001-1'
$receiptPath=Join-Path $case 'owner/owner-receipt.json'
$receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
$receipt.pid=1;Save $receiptPath $receipt
$resultPath=Join-Path $case 'case-result.json';$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
foreach($a in $result.artifacts){if($a.relative_path -eq 'owner/owner-receipt.json'){$a.sha256=Hash $receiptPath;$a.bytes=(Get-Item -LiteralPath $receiptPath).Length}}
Save $resultPath $result
$log= & $lab validate --run $mutant 2>&1
if($LASTEXITCODE -ne 2 -or !($log -join "`n").Contains('OWNER_PROCESS_BINDING')){throw 'OWNER_PROCESS_BINDING_NEGATIVE_FAILED'}
$log|Set-Content -LiteralPath (Join-Path $Output 'changed-owner-pid.txt')

# A separately sealed test package simulates a completed owner counterexample.
$negative=Join-Path $Output 'negative-package';Copy-Item -LiteralPath $Package -Destination $negative -Recurse
$bridge=Join-Path $negative 'tools/nr/lab/owner_bridge.py';$text=[IO.File]::ReadAllText($bridge)
$marker='    if type(observation.get("success")) is not bool or not observation.get("checks"):'
$injection="    if case == 'NLAB-EVD-001':`n        observation['checks'].append({'name': 'test-only completed counterexample', 'passed': False})`n        observation['success'] = False`n"
if(!$text.Contains($marker)){throw 'NEGATIVE_INJECTION_MARKER_MISSING'}
[IO.File]::WriteAllText($bridge,$text.Replace($marker,$injection+$marker),[Text.UTF8Encoding]::new($false))
$manifestPath=Join-Path $negative 'PACKAGE_MANIFEST.json';$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
foreach($f in $manifest.files){if($f.path -eq 'tools/nr/lab/owner_bridge.py'){$f.sha256=Hash $bridge}}
Save $manifestPath $manifest
$negativeLab=Join-Path $negative 'NeuRoticLab.exe';$negativeRun=Join-Path $Output 'completed-negative-run'
& $negativeLab run --preset self_test --output $negativeRun *> (Join-Path $Output 'completed-negative-run.txt')
if($LASTEXITCODE -ne 2){throw 'COUNTEREXAMPLE_RUN_EXIT'}
$negativeResult=Get-Content -LiteralPath (Join-Path $negativeRun 'cases/NLAB-EVD-001-1/case-result.json') -Raw|ConvertFrom-Json
if($negativeResult.verdict -ne 'COUNTEREXAMPLE' -or $negativeResult.reason -ne 'WORKER_INVARIANT_FAILED' -or $negativeResult.execution.raw_exit_code -ne '3'){throw 'COUNTEREXAMPLE_MISCLASSIFIED'}
& $negativeLab validate --run $negativeRun *> (Join-Path $Output 'validate-completed-negative.txt')
if($LASTEXITCODE -ne 0){throw 'COUNTEREXAMPLE_RUN_INVALID'}

$badBinding=Get-Content -LiteralPath (Join-Path $Package 'build-logs/signal-gym/SOURCE_BINDING.json') -Raw|ConvertFrom-Json
$badBinding.implementation_sha256=('0'*64);$badPath=Join-Path $Output 'wrong-adapter-binding.json';Save $badPath $badBinding
& (Join-Path $Package 'runtime/python.exe') -I -B (Join-Path $Package 'tools/nr/lab/Check-OwnerAdapter.py') --adapter (Join-Path $Package 'SignalGymSourceAdapter.exe') --binding $badPath --output (Join-Path $Output 'must-not-publish.json') *> (Join-Path $Output 'reject-adapter-binding.txt')
if($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath (Join-Path $Output 'must-not-publish.json'))){throw 'ADAPTER_BINDING_NEGATIVE_FAILED'}
Save (Join-Path $Output 'TEST_RESULTS.json') @{status='PASS';checks=@('Changed owner PID rejected after artifact rehash','Completed owner false observation classified COUNTEREXAMPLE','Completed negative run validates','Mismatched adapter source descriptor rejected');test_package_modified_for_negative_control=$true;production_package_unchanged=$true}
Write-Output 'PASS owner integration controls'
