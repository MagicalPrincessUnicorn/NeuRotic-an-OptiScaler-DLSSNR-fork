$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function SafePath($root,$relative){return Join-Path $root $relative}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$work=Join-Path $repo 'builds/hub/settings-schema';New-Item -ItemType Directory -Path $work -Force|Out-Null
$path=Join-Path $work 'OptiScaler.ini'
try{
 $mfgKeys=@('NativeMfgExperimental','ExperimentalUnlockRTX30','ExperimentalUnlockRTX20')
 $mfg=@($script:HubSettingsSchema|Where-Object{$_.section -eq 'DLSSG' -and $_.key -in $mfgKeys})
 Assert (($mfg.key -join ',') -ceq ($mfgKeys -join ',')) 'MFG exposes exactly the bound RTX 40, 30, 20 preferences in order'
 Assert (@($script:HubSettingsSchema|Where-Object{$_.section -eq 'Menu' -and $_.key -eq 'OptiClip'}).Count -eq 0) 'Retired OptiClip has no editable App setting'
 foreach($spec in $mfg){
  Assert ($spec.group -eq 'Frame Generation' -and $spec.missingValue -ceq 'false' -and ($spec.values -join ',') -ceq 'false,true') ('MFG default-off explicit preference '+$spec.key)
  Assert ($spec.description -match 'next launch' -and $spec.description -match 'restart' -and $spec.description -match 'provider' -and $spec.description -match 'game' -and $spec.label -match 'Experimental') ('MFG describes request and eligibility '+$spec.key)
 }
 [IO.File]::WriteAllText($path,"; old profile`r`n[Menu]`r`nOptiClip=true ; preserved retired preference`r`n")
 $projected=@(Get-HubSettingsProjection $work|Where-Object{$_.section -eq 'DLSSG' -and $_.key -in $mfgKeys})
 Assert ($projected.Count -eq 3 -and @($projected|Where-Object{$_.value -cne 'false' -or $_.present -or -not $_.available}).Count -eq 0) 'Old INI projects missing MFG preferences as available and off'
 $patches=@($mfgKeys|ForEach-Object{[pscustomobject]@{section='DLSSG';key=$_;value='true'}})
 [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work $patches));$inserted=Read-HubSettingsDocument $path
 Assert (@($inserted.sections|Where-Object{$_.name -eq 'DLSSG'}).Count -eq 1 -and @($inserted.entries|Where-Object{$_.section -eq 'DLSSG'}).Count -eq 3) 'MFG inserts one section and exactly the requested GPU keys'
 Assert ($inserted.text.Contains('OptiClip=true ; preserved retired preference')) 'Retired OptiClip bytes are preserved as an unknown key'
 foreach($invalid in @(
  @{text="[DLSSG]`nNativeMfgExperimental=false`n";section='DLSSG';key='NativeMfgExperimental';value='auto'},
  @{text="[DLSSG]`nExperimentalUnlockRTX30=false`n";section='DLSSG';key='ExperimentalUnlockRTX30';value='enabled'},
  @{text="[DLSSG]`nExperimentalUnlockRTX20=false`nExperimentalUnlockRTX20=true`n";section='DLSSG';key='ExperimentalUnlockRTX20';value='true'},
  @{text="[DLSSG]`nNativeMfgExperimental=false`n[DLSSG]`nUnknown=keep`n";section='DLSSG';key='NativeMfgExperimental';value='true'},
  @{text="[Menu]`nOptiClip=true`n";section='Menu';key='OptiClip';value='false'}
 )){
  [IO.File]::WriteAllText($path,$invalid.text);$before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));$refused=$false
  try{Get-HubSettingsBytes $work @([pscustomobject]@{section=$invalid.section;key=$invalid.key;value=$invalid.value})|Out-Null}catch{$refused=$true}
  Assert ($refused -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $before) 'Invalid, retired or ambiguous MFG edits refuse without mutation'
 }
 Copy-Item -LiteralPath (Join-Path $repo 'integration/OptiScaler.ini') -Destination $path -Force
 $full=@(Get-HubSettingsProjection $work)
 Assert ($full.Count -ge 60 -and @($full|Where-Object{-not $_.available}).Count -eq 0) 'Full integration INI projects every supported setting, including safely insertable fields'
 Assert (@($full|Where-Object{$_.section -eq 'Log' -and $_.key -eq 'LogLevel' -and $_.present}).Count -eq 1) 'File logging maps to actual Log section'
 Assert (@($full|Where-Object{$_.key -eq 'Passes' -and $_.type -eq 'integer' -and $_.maximum -eq 10}).Count -eq 1) 'Projection exposes typed integer range to Hub'
 Assert (@($full|Where-Object{$_.key -eq 'ShortcutKey' -and $_.type -eq 'keycode' -and $_.maximum -eq 2047}).Count -eq 1) 'Projection exposes the current shortcut chord range'
 $sample=Get-HubSettingsBytes $work @([pscustomobject]@{section='DlssNr';key='Preset';value='3'},[pscustomobject]@{section='DlssNr';key='Style';value='0'},[pscustomobject]@{section='DlssNr';key='PreparedDepth';value='true'},[pscustomobject]@{section='DlssNr';key='ExperimentalMode';value='true'},[pscustomobject]@{section='Menu';key='ShortcutKey';value='0x70'})
 [IO.File]::WriteAllBytes($path,$sample)
 Assert ((Read-HubSettingsDocument $path).text.Contains('ShortcutKey = 0x70')) 'Full integration INI draft updates actual shortcut token'
 foreach($spec in $script:HubSettingsSchema){
  if($spec.type -in @('integer','float')){
   foreach($value in @(([double]$spec.minimum).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$spec.maximum).ToString('R',[Globalization.CultureInfo]::InvariantCulture),'auto')){Assert (Test-HubSettingValue $spec $value) ('Accepted bound '+$spec.section+'/'+$spec.key+'='+$value)}
   # Adding one is rounded away at a float-sized maximum. Use a genuinely
   # greater finite double so this still checks rejection at that boundary.
   $upper=[double]$spec.maximum;$above=$upper+[Math]::Max(1.0,[Math]::Abs($upper)*1e-6)
   Assert ($above -gt $upper -and -not (Test-HubSettingValue $spec $above.ToString('R',[Globalization.CultureInfo]::InvariantCulture))) ('Upper bound enforced '+$spec.key)
  }
 }
 $dmfg=@($script:HubSettingsSchema|Where-Object{$_.section -eq 'DLSSG' -and $_.key -eq 'FramerateTargetDMFG'})
 Assert ($dmfg.Count -eq 1 -and $dmfg[0].minimum -eq 0 -and $dmfg[0].maximum -eq [double][float]::MaxValue) 'DMFG target uses the actual nonnegative finite float range'
 foreach($value in @('auto','0','240','360','240.5')){
  [IO.File]::WriteAllText($path,"[DLSSG]`nFramerateTargetDMFG=auto`nUnknown=preserved`n")
  $bytes=Get-HubSettingsBytes $work @([pscustomobject]@{section='DLSSG';key='FramerateTargetDMFG';value=$value})
  [IO.File]::WriteAllBytes($path,$bytes);$doc=Read-HubSettingsDocument $path
  Assert (@($doc.entries|Where-Object{$_.section -eq 'DLSSG' -and $_.key -eq 'FramerateTargetDMFG' -and $_.value -ceq $value}).Count -eq 1 -and $doc.text.Contains('Unknown=preserved')) ('DMFG exact saved token round trips '+$value)
 }
 foreach($value in @('-1','NaN','Infinity','-Infinity','1e39','1e309')){Assert (-not (Test-HubSettingValue $dmfg[0] $value)) ('DMFG rejects nonfinite, negative or float overflow '+$value)}
 foreach($route in @('1','2','auto')){
  [IO.File]::WriteAllText($path,"[DlssNr]`nRoute=$route`n")
  $entry=Get-HubSettingsProjection $work|Where-Object{$_.key -eq 'PresentInputPolicy'}
  $expected=@{'1'='0';'2'='1';'auto'='2'}[$route]
  Assert ($entry.value -eq $expected -and -not $entry.present) 'Missing Present policy reflects legacy route semantics'
 }
 foreach($case in @(
  @{text="[DlssNr]`nEnhancedResolution=0`nEnhancedCustomScale=0`n";patches=@(@{section='DlssNr';key='EnhancedResolution';value='3'},@{section='DlssNr';key='EnhancedCustomScale';value='125'});ok=$true},
  @{text="[DlssNr]`nEnhancedResolution=0`nEnhancedCustomScale=0`n";patches=@(@{section='DlssNr';key='EnhancedCustomScale';value='125'});ok=$false},
  @{text="[DlssNr]`nEnhancedResolution=3`nEnhancedCustomScale=100`n";patches=@(@{section='DlssNr';key='EnhancedCustomScale';value='24'});ok=$false},
  @{text="[DlssNrBasic]`nMaximumPasses=2`nModelStrength=1`n";patches=@(@{section='DlssNrBasic';key='ModelStrength';value='2.5'});ok=$false},
  @{text="[DlssNrBasic]`nMaximumPasses=2`nModelStrength=1`n";patches=@(@{section='DlssNrBasic';key='ModelStrength';value='2.5'},@{section='DlssNrBasic';key='MaximumPasses';value='3'});ok=$true}
 )){
  [IO.File]::WriteAllText($path,$case.text);$accepted=$true
  try{Get-HubSettingsBytes $work @($case.patches|ForEach-Object{[pscustomobject]$_})|Out-Null}catch{$accepted=$false}
  Assert ($accepted -eq $case.ok) 'Coupled resolution and basic pass values validate the entire draft'
 }
 $text="; preserve caf$([char]233)`r`n[Menu]`r`nShortcutKey=auto ; retain shortcut note`r`nUnknown=untouched`r`n[DlssNr]`r`nEnabled=false`r`nStyle=2`r`n[Log]`r`nLogLevel=auto`r`n"
 foreach($encoding in @((New-Object Text.UTF8Encoding($true)),(New-Object Text.UnicodeEncoding($false,$true)),(New-Object Text.UnicodeEncoding($true,$true)),[Text.Encoding]::GetEncoding(1252))){
  [IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text)))
  $projection=@(Get-HubSettingsProjection $work)
  Assert (@($projection|Where-Object{$_.section -eq 'DlssNr' -and $_.key -eq 'ExperimentalMode' -and $_.available}).Count -eq 1) 'Missing known experimental key can be edited before first launch'
  $patches=@(@{section='Menu';key='ShortcutKey';value='0x24'},@{section='Menu';key='FpsShortcutKey';value='113'},@{section='DlssNr';key='Style';value='1'},@{section='DlssNr';key='RenderingMode';value='0'},@{section='DlssNr';key='Passes';value='3'},@{section='DlssNr';key='ExperimentalMode';value='true'},@{section='Log';key='LogLevel';value='3'},@{section='Screenshots';key='Key';value='-1'}) | ForEach-Object{[pscustomobject]$_}
  $bytes=Get-HubSettingsBytes $work $patches;[IO.File]::WriteAllBytes($path,$bytes);$doc=Read-HubSettingsDocument $path
  Assert ($doc.text.Contains('ShortcutKey=0x24 ; retain shortcut note') -and $doc.text.Contains('Unknown=untouched') -and $doc.text.Contains("; preserve caf$([char]233)")) ('Comments and unknown settings preserved in encoding '+$encoding.CodePage)
  Assert ([Convert]::ToBase64String($doc.bom) -eq [Convert]::ToBase64String($encoding.GetPreamble())) 'Original BOM preserved'
  foreach($patch in $patches){$found=@($doc.entries|Where-Object{$_.section -ieq $patch.section -and $_.key -ieq $patch.key});Assert ($found.Count -eq 1 -and $found[0].value -ceq $patch.value) ($patch.section+'/'+$patch.key+' has one exact saved value')}
  Assert ($doc.text -notmatch '(?<!\r)\n') 'Inserted keys preserve CRLF'
 }
 foreach($bad in @(@{section='Menu';key='ShortcutKey';value='256'},@{section='Menu';key='ShortcutKey';value='0x100'},@{section='Menu';key='ShortcutKey';value='1.5'},@{section='Menu';key='ShortcutKey';value='-2'},@{section='DlssNr';key='Passes';value='2.5'},@{section='DlssNr';key='Passes';value='1e1'},@{section='DlssNr';key='Passes';value='11'},@{section='DlssNr';key='Preset';value='4'},@{section='DlssNr';key='Intensity';value='NaN'},@{section='DlssNr';key='WorkingScale';value='0'},@{section='Debug';key='LogLevel';value='2'})){
  $before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));$refused=$false;try{Get-HubSettingsBytes $work @([pscustomobject]$bad)|Out-Null}catch{$refused=$true};Assert $refused ('Invalid typed value refused: '+$bad.key+'='+$bad.value);Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -eq $before) 'Validation does not mutate INI'
 }
 [IO.File]::WriteAllText($path,"[Menu]`nShortcutKey=")
 $blank=Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='ShortcutKey';value='45'},[pscustomobject]@{section='Menu';key='FpsShortcutKey';value='33'})
 Assert ([Text.Encoding]::UTF8.GetString($blank) -eq "[Menu]`nShortcutKey=45`nFpsShortcutKey=33`n") 'Replacing an empty final value and inserting another key preserves both values'
 [IO.File]::WriteAllText($path,"[Menu]`nScale=")
 $blank=Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='Scale';value='1.25'},[pscustomobject]@{section='Menu';key='ShowFps';value='true'})
 Assert ([Text.Encoding]::UTF8.GetString($blank) -eq "[Menu]`nScale=1.25`nShowFps=true`n") 'Exact empty Scale plus new ShowFps draft cannot reorder equal-offset writes'
 [IO.File]::WriteAllText($path,"[Menu]`rScale=1`rShowFps=false`r")
 $cr=Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='ShowFps';value='true'})
 Assert ([Text.Encoding]::UTF8.GetString($cr) -eq "[Menu]`rScale=1`rShowFps=true`r") 'CR-only INI updates original value without creating a duplicate section or key'
 foreach($badText in @("[Menu]`nShortcutKey=45`nShortcutKey=36`n","[Menu]`nShortcutKey=45`n[Menu]`nOther=1`n")){
  [IO.File]::WriteAllText($path,$badText);$refused=$false;try{Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='ShortcutKey';value='0X24'})|Out-Null}catch{$refused=$true};Assert $refused 'Duplicate key or section refuses ambiguous edit'
 }
 [IO.File]::WriteAllText($path,"[Menu]`nShortcutKey=auto")
 foreach($key in @('auto','-1','0','45','255','0x08','0XFF')){Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='ShortcutKey';value=$key})|Out-Null}
 Assert $true 'Decimal and hexadecimal Windows shortcut values accepted'
}finally{
 if([IO.Path]::GetFullPath($work) -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/settings-schema'))){throw 'Unexpected fixture root'}
 foreach($item in Get-ChildItem -LiteralPath $work -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture'};Remove-Item -LiteralPath $item.FullName -Force}
 Remove-Item -LiteralPath $work -Force
}
