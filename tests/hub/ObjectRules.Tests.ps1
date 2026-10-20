$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
$schema=$script:HubSettingsSchema | Where-Object type -eq 'objectrules'
function Enc([string]$text){return ([BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($text))).Replace('-','').ToLowerInvariant()}
$example=Get-Content -Raw -LiteralPath (Join-Path $root 'references/object-rules/20261004/examples/grapes-and-armor.object-rules.json')
if(-not(Test-HubSettingValue $schema (Enc $example))){throw 'valid profile rejected'}
$bad=$example | ConvertFrom-Json;$bad.rules[0].overlay.opacity=3
if(Test-HubSettingValue $schema (Enc ($bad|ConvertTo-Json -Depth 16))){throw 'range accepted'}
if(Test-HubSettingValue $schema (Enc '{"format":"a","format":"b"}')){throw 'duplicate key accepted'}
if(Test-HubSettingValue $schema ('a'*4194306)){throw 'oversize accepted'}
$bad=$example|ConvertFrom-Json;$bad.rules[0]|Add-Member raw_image 'not allowed'
if(Test-HubSettingValue $schema (Enc ($bad|ConvertTo-Json -Depth 16))){throw 'unknown field accepted'}
'PASS Object Rules settings envelope/strict JSON/schema bounds'

# The existing settings transaction preserves unrelated text and projects the full profile.
function SafePath($root,$relative){Join-Path $root $relative}
$work=Join-Path $root 'builds/inspector-motion/object-rules/settings-fixture'
New-Item -ItemType Directory -Path $work -Force|Out-Null
$ini=Join-Path $work 'OptiScaler.ini'
try {
 [IO.File]::WriteAllText($ini,"; preserved comment`r`n[Unknown]`r`nKeep=secret-placeholder`r`n[Menu]`r`nOverlayMenu=true`r`n")
 $encoded=Enc $example
 $updated=Get-HubSettingsBytes $work @([pscustomobject]@{section='ObjectRules';key='ProfileHex';value=$encoded})
 [IO.File]::WriteAllBytes($ini,$updated)
 $projection=@(Get-HubSettingsProjection $work|Where-Object type -eq 'objectrules')
 if($projection.Count -ne 1 -or $projection[0].value -cne $encoded){throw 'profile roundtrip truncated'}
 if(-not (Read-HubSettingsDocument $ini).text.Contains('Keep=secret-placeholder')){throw 'unrelated setting changed'}
 # Settings request permits the bounded hex field while retaining strict duplicate/depth checks.
 $request=[ordered]@{kind='PlanSettings';settings=@([ordered]@{section='ObjectRules';key='ProfileHex';value=$encoded})}|ConvertTo-Json -Depth 8
 [NeuRoticHubJson]::ValidateSettingsRequest($request)
 'PASS Object Rules existing save bytes/projection/request roundtrip'
}finally{Remove-Item -LiteralPath $ini -Force;Remove-Item -LiteralPath $work}
