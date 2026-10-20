param([Parameter(Mandatory)][string]$PackageRoot,[Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$exe=Join-Path $PackageRoot 'P1Diagnostic.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'STAGE_DIAGNOSTIC_MISSING'}
if(Test-Path -LiteralPath $EvidenceRoot){throw 'FRESH_TEST_DIRECTORY_REQUIRED'}
[void](New-Item -ItemType Directory -Path $EvidenceRoot)
$checks=[Collections.Generic.List[object]]::new()
function Invoke-Check([string]$Name,[string[]]$Arguments,[int]$Expected=0){
    $text=& $exe @Arguments 2>&1; $code=$LASTEXITCODE
    $text|Set-Content -LiteralPath (Join-Path $EvidenceRoot ($Name+'.txt'))
    if($code -ne $Expected){throw "$Name exit=$code expected=$Expected $text"}
    $checks.Add(@{name=$Name;exit_code=$code;passed=$true})
}
Invoke-Check 'package' @('verify-package')
foreach($mode in @('create-only','split-evaluate')){
    $run=Join-Path $EvidenceRoot $mode
    Invoke-Check $mode @('self-test','--mode',$mode,'--output',$run)
    Invoke-Check ($mode+'-validate') @('validate','--run',$run)
    $result=Get-Content -Raw -LiteralPath (Join-Path $run 'RESULT.json')|ConvertFrom-Json
    if($result.verdict -ne 'CONTROL_PASS'){throw "Unexpected WARP result $mode"}
    $probe=Get-Content -Raw -LiteralPath (Join-Path $run 'worker/probe.json')|ConvertFrom-Json
    if($probe.backend -ne 'warp' -or $probe.status -ne 'COMPLETED'){throw 'WARP_SCOPE_INVALID'}
    $raw=Get-Content -Raw -LiteralPath (Join-Path $run 'worker/runner.json')|ConvertFrom-Json
    if($raw.create_count -ne 0 -or $raw.evaluate_count -ne 0 -or $raw.module_load.provider -or $raw.module_load.core -or $raw.module_load.forwarder){throw 'PROVIDER_EFFECTS_IN_CONTROL'}
    $expectedStages=if($mode -eq 'create-only'){1}else{3}
    if($probe.stages.Count -ne $expectedStages){throw 'STAGE_COUNT_MISMATCH'}
    Invoke-Check ($mode+'-repeat-refused') @('self-test','--mode',$mode,'--output',$run) 2
}
$template=Join-Path $PackageRoot 'LOCAL_RUNTIME.template.json'
Invoke-Check 'consent-refused' @('run','--mode','create-only','--config',$template,'--output',(Join-Path $EvidenceRoot 'must-not-launch')) 2
if(Test-Path -LiteralPath (Join-Path $EvidenceRoot 'must-not-launch')){throw 'CONSENT_REFUSAL_CREATED_RUN'}
Invoke-Check 'config-consent-refused' @('run','--mode','create-only','--config',$template,'--output',(Join-Path $EvidenceRoot 'must-not-launch'),'--allow-provider') 2
if(Test-Path -LiteralPath (Join-Path $EvidenceRoot 'must-not-launch')){throw 'CONFIG_CONSENT_REFUSAL_CREATED_RUN'}
Invoke-Check 'warp-provider-flag-refused' @('self-test','--mode','create-only','--output',(Join-Path $EvidenceRoot 'bad-warp'),'--allow-provider') 2
Invoke-Check 'mode-required' @('self-test','--output',(Join-Path $EvidenceRoot 'bad-mode')) 2
$run=Join-Path $EvidenceRoot 'split-evaluate'
$zip=Join-Path $EvidenceRoot 'split-review.zip'
Invoke-Check 'export' @('export','--run',$run,'--output',$zip)
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{
    if(@($archive.Entries|Where-Object {$_.FullName -match '(?i)\.(dll|exe|dmp)$|LOCAL_RUNTIME|runner-(stdout|stderr)'}).Count){throw 'PRIVATE_EXPORT_MEMBER'}
    if(!$archive.GetEntry('EXPORT_MANIFEST.json') -or !$archive.GetEntry('projections/worker/probe.json')){throw 'MISSING_EXPORT_EVIDENCE'}
}finally{$archive.Dispose()}
$mutant=Join-Path $EvidenceRoot 'missing-stage-snapshot'
Copy-Item -LiteralPath $run -Destination $mutant -Recurse
Remove-Item -LiteralPath (Join-Path $mutant 'worker/runner-initialize.json')
Invoke-Check 'missing-stage-snapshot' @('validate','--run',$mutant) 2
@{schema_version=1;provider_execution='NOT_RUN';physical_execution='NOT_RUN';warp_execution='EXPLICIT_SOFTWARE_ONLY';checks=$checks;success=$true}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'TEST_RESULTS.json')
'STAGE_DIAGNOSTIC_INTEGRATION_PASS'
