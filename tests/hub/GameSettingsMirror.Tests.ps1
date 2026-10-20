$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function SafePath($root,$relative){Join-Path $root $relative}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$work=Join-Path $repo 'builds/hub/settings-mirror'
New-Item -ItemType Directory -Path $work -Force|Out-Null
$path=Join-Path $work 'OptiScaler.ini'
try{
 Assert (@($script:HubSettingsSchema|Group-Object section,key|Where-Object Count -ne 1).Count -eq 0) 'All saved setting identities are unique'
 Assert ($script:HubSettingsSchema.Count -gt 350 -and $script:HubSettingsSchema.Count -le 512) 'Expanded editor stays within its bounded editable schema'
 $compatibility=@($script:HubSettingsSchema|Where-Object group -eq 'Installation & compatibility')
 Assert ($compatibility.Count -eq 9 -and @($compatibility|Where-Object missingValue -ne 'auto').Count -eq 0) 'Nine compatibility settings preserve automatic defaults without a game preset'
 [IO.File]::WriteAllText($path,"[Plugins]`r`nLoadReShade=false ; keep comment`r`n[Spoofing]`r`nDxgi=false`r`n[Hotfix]`r`nRestoreComputeSignature=true`r`n[OptiFG]`r`nDisableHUDFix=true`r`n")
 $before=[IO.File]::ReadAllBytes($path)
 $projection=@(Get-HubSettingsProjection $work)
 Assert ([Convert]::ToBase64String($before) -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) 'Reading compatibility options makes no automatic changes'
 [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work @([pscustomobject]@{section='Plugins';key='LoadReShade';value='true'})))
 $changed=(Read-HubSettingsDocument $path).text
 Assert ($changed.Contains('LoadReShade=true ; keep comment') -and $changed.Contains('Dxgi=false') -and $changed.Contains('RestoreComputeSignature=true') -and $changed.Contains('DisableHUDFix=true')) 'ReShade toggle changes only its explicit loader preference and preserves Capcom overrides'
 $layers=@($script:HubSettingsSchema|Where-Object section -like 'DlssNrLayer*')
 Assert ($layers.Count -eq 135 -and @($layers|Where-Object missingValue -ne 'auto').Count -eq 0) 'Nine independent 15-key pass profiles preserve missing-value inheritance'
 $template="; preserve caf$([char]233)`r`n[Menu]`r`nPreflightExpanded=false ; preferred collapsed`r`nFuture=keep`r`n[DlssNr]`r`nEnabled=false`r`nRoute=0`r`nStyle=2`r`n[Private]`r`nUnrecognized=value`r`n"
 foreach($encoding in @((New-Object Text.UTF8Encoding($true)),(New-Object Text.UnicodeEncoding($false,$true)),(New-Object Text.UnicodeEncoding($true,$true)),[Text.Encoding]::GetEncoding(1252))){
  [IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($template)))
  $patches=@(
   [pscustomobject]@{section='Menu';key='PreflightExpanded';value='false'},
   [pscustomobject]@{section='CharacterInspector';key='MaximumPersons';value='16'},
   [pscustomobject]@{section='DlssNr';key='Compare';value='2'},
   [pscustomobject]@{section='DlssNrLayer10';key='WorkingScale';value='1.25'},
   [pscustomobject]@{section='Magnifier';key='ZoomFactor';value='8'},
   [pscustomobject]@{section='Hotfix';key='DepthResourceBarrier';value='64'},
   [pscustomobject]@{section='DLSSD';key='RenderPresetForAll';value='6'})
  [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work $patches));$doc=Read-HubSettingsDocument $path
  Assert ($doc.text.Contains('PreflightExpanded=false ; preferred collapsed') -and $doc.text.Contains('Future=keep') -and $doc.text.Contains('Unrecognized=value') -and $doc.text.Contains("Enabled=false`r`nRoute=0`r`nStyle=2")) ('New tab edits preserve unknown keys and explicit NR settings in encoding '+$encoding.CodePage)
  Assert ([Convert]::ToBase64String($doc.bom) -ceq [Convert]::ToBase64String($encoding.GetPreamble())) 'New field edits preserve the original BOM'
 }
 foreach($case in @(@{section='CharacterInspector';key='MaximumPersons';value='17'},@{section='DlssNrLayer10';key='Style';value='3'},@{section='DlssNrLayer3';key='ReversibleMode';value='4'},@{section='Hotfix';key='DepthResourceBarrier';value='999'},@{section='DLSSD';key='RenderPresetForAll';value='15'})){
  $before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));$refused=$false
  try{Get-HubSettingsBytes $work @([pscustomobject]$case)|Out-Null}catch{$refused=$true}
  Assert ($refused -and $before -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) ('Invalid newly exposed setting is refused without a write: '+$case.section+'/'+$case.key)
 }
 [IO.File]::WriteAllText($path,"[Menu]`nFuture=keep`n")
 $projection=@(Get-HubSettingsProjection $work)
 Assert (@($projection|Where-Object{$_.key -eq 'PreflightExpanded' -and $_.value -ceq 'true' -and -not $_.present}).Count -eq 1) 'Missing preflight preference projects expanded without writing the INI'
 foreach($case in @(
  @{text="[Menu]`nExtendedLimits=false`n";patches=@(@{section='QualityOverrides';key='QualityRatioDLAA';value='.5'});ok=$false},
  @{text="[Menu]`nExtendedLimits=false`n";patches=@(@{section='QualityOverrides';key='QualityRatioDLAA';value='.5'},@{section='Menu';key='ExtendedLimits';value='true'});ok=$true},
  @{text="[Menu]`nExtendedLimits=auto`n";patches=@(@{section='UpscaleRatio';key='UpscaleRatioOverrideValue';value='3'});ok=$true},
  @{text="[Menu]`nExtendedLimits=false`n[QualityOverrides]`nQualityRatioDLAA=.5`n";patches=@(@{section='Menu';key='Brightness';value='1.1'});ok=$true},
  @{text="[CAS]`nUnknown=keep`n";patches=@(@{section='CAS';key='DADepthBias';value='.004'});ok=$false},
  @{text="[CAS]`nUnknown=keep`n";patches=@(@{section='CAS';key='DADepthBias';value='.003'});ok=$true},
  @{text="[CAS]`nUnknown=keep`n";patches=@(@{section='CAS';key='DADepthBias';value='.005'});ok=$true},
  @{text="[InitFlags]`nDisplayResolution=true`n";patches=@(@{section='OutputScaling';key='Enabled';value='true'},@{section='OutputScaling';key='Downscaler';value='4'});ok=$false},
  @{text="[InitFlags]`nDisplayResolution=true`n";patches=@(@{section='OutputScaling';key='Enabled';value='true'},@{section='InitFlags';key='DisplayResolution';value='false'},@{section='OutputScaling';key='Downscaler';value='7'});ok=$true}
 )){
  [IO.File]::WriteAllText($path,$case.text);$before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));$ok=$true
  try{Get-HubSettingsBytes $work @($case.patches|ForEach-Object{[pscustomobject]$_})|Out-Null}catch{$ok=$false}
  Assert ($ok -eq $case.ok -and $before -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) 'Extended ratios, disjoint depth-bias ranges and output-scaling/MV dependencies validate the whole draft without mutation'
 }
 [IO.File]::WriteAllText($path,"[Menu]`nFuture=keep`n")
 $bulk=@($layers|ForEach-Object{[pscustomobject]@{section=$_.section;key=$_.key;value='auto'}})
 [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work $bulk));$doc=Read-HubSettingsDocument $path
 Assert (@($doc.entries|Where-Object section -like 'DlssNrLayer*').Count -eq 135 -and $doc.text.Contains('Future=keep')) 'A complete 135-field pass-profile draft saves once without truncation'
 [IO.File]::WriteAllText((Join-Path $work 'schema.json'),($script:HubSettingsSchema|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
}finally{if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Force}}
