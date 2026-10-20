param([Parameter(Mandatory)][string]$PackageRoot,[Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$exe=Join-Path $PackageRoot 'NeuRoticLab.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'LAB_CONTROLLER_MISSING'}
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
function Invoke-Lab([string[]]$Arguments,[int]$Expected=0){& $exe @Arguments; if($LASTEXITCODE -ne $Expected){throw "LAB_EXIT:$LASTEXITCODE expected:$Expected args:$Arguments"}}
Invoke-Lab @('verify-package')
Invoke-Lab @('self-test','--output',(Join-Path $EvidenceRoot 'self'))
Invoke-Lab @('validate','--run',(Join-Path $EvidenceRoot 'self'))
Invoke-Lab @('run','--preset','first_answers','--output',(Join-Path $EvidenceRoot 'first'))
Invoke-Lab @('validate','--run',(Join-Path $EvidenceRoot 'first'))
$summary=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'first/SUMMARY.json') -Raw|ConvertFrom-Json
if($summary.counts.requested -ne 16){throw 'PRESET_SELECTION_CHANGED'}
foreach($entry in $summary.case_results){if($entry.case_id -like 'NLAB-NR-*'){$r=Get-Content -LiteralPath (Join-Path (Join-Path $EvidenceRoot 'first') $entry.result_path) -Raw|ConvertFrom-Json;if($r.execution.state -ne 'NOT_STARTED'){throw 'UNAUTHORIZED_PROVIDER_EXECUTION'}}}
Invoke-Lab @('export','--run',(Join-Path $EvidenceRoot 'first'),'--output',(Join-Path $EvidenceRoot 'review.zip'))
'LAB_INTEGRATION_PASS'
