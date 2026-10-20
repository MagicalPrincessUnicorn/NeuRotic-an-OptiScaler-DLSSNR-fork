$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
$fixture=Join-Path $PSScriptRoot '../../builds/hub/hotkey-chord/ini-fixture'
[IO.Directory]::CreateDirectory($fixture)|Out-Null
$fixture=[IO.Path]::GetFullPath($fixture)
function SafePath([string]$Root,[string]$Name){
 if($Root -cne $fixture -or $Name -cne 'OptiScaler.ini'){throw 'Unexpected fixture path'}
 return Join-Path $Root $Name
}
function Check([bool]$Condition,[string]$Label){if(-not $Condition){throw $Label};Write-Output "PASS $Label"}
$path=Join-Path $fixture 'OptiScaler.ini'
$text="; retained fixture`r`n[Menu]`r`nShortcutKey=0x2D ; unchanged original`r`nLightTheme=true`r`nUnknown=keep`r`n[DlssNr]`r`nToggleKey=631 ; legacy Shift-only preserved`r`n[Private]`r`nFuture=untouched`r`n"
$encoding=New-Object Text.UTF8Encoding($true)
try{
 [IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text)))
 $unchanged=Get-HubSettingsBytes $fixture @()
 Check ([Convert]::ToBase64String($unchanged) -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) 'empty draft preserves legacy key tokens, comments and encoding exactly'
 $projection=@(Get-HubSettingsProjection $fixture)
 $motion=@($projection|Where-Object{$_.section -ceq 'Menu' -and $_.key -ceq 'ReduceMotion'})
 Check ($motion.Count -eq 1 -and $motion[0].value -ceq 'auto') 'missing animation preference projects automatic default'
 foreach($schema in @($script:HubSettingsSchema|Where-Object{$_.type -eq 'keycode'})){
  foreach($value in @('119','375','1143','887','1399')){
   $patch=[pscustomobject]@{section=$schema.section;key=$schema.key;value=$value}
   $bytes=Get-HubSettingsBytes $fixture @($patch)
   [IO.File]::WriteAllBytes($path,$bytes)
   $saved=@(Get-HubSettingsProjection $fixture|Where-Object{$_.section -ceq $schema.section -and $_.key -ceq $schema.key})
   Check ($saved.Count -eq 1 -and $saved[0].value -ceq $value) ($schema.key+' accepts and reopens '+$value)
  }
 }
 foreach($value in @('true','false','auto')){
  $bytes=Get-HubSettingsBytes $fixture @([pscustomobject]@{section='Menu';key='ReduceMotion';value=$value})
  [IO.File]::WriteAllBytes($path,$bytes)
  $doc=Read-HubSettingsDocument $path
  Check ($doc.text.Contains('ReduceMotion='+$value) -and $doc.text.Contains('LightTheme=true') -and $doc.text.Contains('Unknown=keep') -and $doc.text.Contains('Future=untouched')) ('animation preference '+$value+' persists while retaining theme and unknown keys')
 }
}finally{if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}}
