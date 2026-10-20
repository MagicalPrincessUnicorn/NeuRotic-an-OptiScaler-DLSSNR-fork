Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'tools/packaging/customer/support/NeuRotic-HubAntiCheat.ps1')
$packet=Join-Path $root 'references/anticheat-detector/20261003/NeuRotic_AntiCheat_Detector_Packet_2026-10-03'
$rules=Get-Content -Raw (Join-Path $packet 'rules/provider_rules.json') | ConvertFrom-Json
$cases=Get-Content -Raw (Join-Path $packet 'fixtures/policy_cases.json') | ConvertFrom-Json
foreach($case in $cases){
 $actual=Invoke-HubAntiCheatPolicy $rules $case.snapshot
 foreach($name in @('game_state','headline','requires_ack','scan_status')){if($actual.$name -cne $case.expected.$name){throw ($case.id+': '+$name+' expected '+$case.expected.$name+' got '+$actual.$name)}}
 if(@($actual.findings).Count -ne @($case.expected.providers).Count -or @($actual.system_context).Count -ne $case.expected.system_count){throw ($case.id+': provider/system counts')}
 if($actual.safety_verified -or $null -ne $actual.protection_active -or $actual.block_for_anticheat){throw 'Unsafe verdict'}
 if($actual.evidence_digest -cne (Invoke-HubAntiCheatPolicy $rules $case.snapshot).evidence_digest){throw 'Nondeterministic evidence'}
 Write-Output ('PASS '+$case.id)
}
$base=($cases|Where-Object {$_.id -eq 'eac_corroborated_layout'}).snapshot
foreach($config in @('{"A":1,"a":2}',('{"large":"'+('x'*40000)+'"}'))){
 $copy=$base|ConvertTo-Json -Depth 16|ConvertFrom-Json;$settings=$copy.files|Where-Object {$_.path -like '*Settings.json'};$settings.text=$config
 $result=Invoke-HubAntiCheatPolicy $rules $copy
 if($result.game_state -ne 'detected' -or $result.scan_status -ne 'complete'){throw 'Valid bounded case-sensitive/long config object incorrectly rejected'}
 Write-Output 'PASS valid bounded config object preserves JSON key case and 64 KiB string budget'
}
foreach($config in @('{"x":1e400}',([char]0xa0+'{"x":1}'),(('['*17)+'1'+(']'*17)))){
 $copy=$base|ConvertTo-Json -Depth 16|ConvertFrom-Json;($copy.files|Where-Object {$_.path -like '*Settings.json'}).text=$config
 $result=Invoke-HubAntiCheatPolicy $rules $copy
 if($result.game_state -ne 'possible' -or $result.scan_status -ne 'incomplete'){throw 'Malformed config could corroborate provider'}
 Write-Output 'PASS malformed nonfinite/non-JSON-whitespace/deep configuration cannot promote detection'
}
$invalid=$rules|ConvertTo-Json -Depth 16|ConvertFrom-Json;$invalid.engine_version=2
if((Invoke-HubAntiCheatPolicy $invalid $base).issues -notcontains 'rules_invalid'){throw 'Unknown engine accepted'}
Write-Output 'PASS unsupported rule engine returns incomplete warning'
$invalid=$rules|ConvertTo-Json -Depth 16|ConvertFrom-Json;$invalid.file_signals[0].id=$invalid.file_signals[1].id
if((Invoke-HubAntiCheatPolicy $invalid $base).issues -notcontains 'rules_invalid'){throw 'Duplicate rule identity accepted'}
Write-Output 'PASS duplicate rule identity rejected'
$reordered=$base|ConvertTo-Json -Depth 16|ConvertFrom-Json;[Array]::Reverse($reordered.files)
if((Invoke-HubAntiCheatPolicy $rules $base).evidence_digest -cne (Invoke-HubAntiCheatPolicy $rules $reordered).evidence_digest){throw 'Observation order affected stable fingerprint'}
Write-Output 'PASS collector enumeration order cannot alter stable evidence fingerprint'
