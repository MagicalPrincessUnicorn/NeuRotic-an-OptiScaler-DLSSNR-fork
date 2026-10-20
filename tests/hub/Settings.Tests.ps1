. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 foreach($encoding in @((New-Object Text.UTF8Encoding($true)),(New-Object Text.UnicodeEncoding($false,$true)),(New-Object Text.UnicodeEncoding($true,$true)),[Text.Encoding]::GetEncoding(1252))){
  $game=New-GameFixture ('settings-'+$encoding.CodePage)
  $text="; retain caf$([char]233)`r`n[Menu]`r`nOverlayMenu=false ; preserve comment`r`nCustomUserKey=literal`r`nOptiClip=true ; retired, preserve`r`n[DlssNr]`r`nEnabled=false`r`n[DLSSG]`r`nNativeMfgExperimental=false ; RTX 40`r`nExperimentalUnlockRTX30=false ; RTX 30`r`nExperimentalUnlockRTX20=false ; RTX 20`r`nOverrideInterpolationCount=3`r`nOverrideForceDMFG=true`r`nUnknownDlssg=keep`r`n[FrameGen]`r`nEnabled=false`r`nFGInput=dlssg`r`nFGOutput=fsrfg`r`nFGNvngxReplacement=nukems`r`n[fakenvapi]`r`nForceXeLL=true`r`n"
  $path=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text)))
  $request=[ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='SaveSettings';configRevision=(Get-FileHash $path).Hash;settings=@([ordered]@{section='Menu';key='OverlayMenu';value='true'})}
  $plan=HubCall $request;$result=Execute-Plan $plan
  Assert ($result.status -eq 'Succeeded') 'NH-06-CONFIG-ROUNDTRIP: owner saves draft'
  $expected=[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text.Replace('OverlayMenu=false','OverlayMenu=true')))
  Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -eq [Convert]::ToBase64String($expected)) ('NH-06-CONFIG-ROUNDTRIP: encoding/comments/unknown keys '+$encoding.CodePage)
  $current=$text.Replace('OverlayMenu=false','OverlayMenu=true')
  foreach($change in @(@{key='NativeMfgExperimental';from='false';value='true'},@{key='ExperimentalUnlockRTX30';from='false';value='true'},@{key='ExperimentalUnlockRTX20';from='false';value='true'},@{key='ExperimentalUnlockRTX30';from='true';value='false'})){
   $request.configRevision=(Get-FileHash $path).Hash;$request.settings=@([ordered]@{section='DLSSG';key=$change.key;value=$change.value})
   $result=Execute-Plan (HubCall $request);Assert ($result.status -eq 'Succeeded') ('NH-06-MFG: owner saves '+$change.key+'='+$change.value)
   $current=$current.Replace(($change.key+'='+$change.from),($change.key+'='+$change.value));$expected=[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($current))
   Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq [Convert]::ToBase64String($expected)) ('NH-06-MFG: only requested GPU changes; route, ratio, other GPU, comments, unknown keys and encoding preserved '+$encoding.CodePage)
  }
  $request.configRevision=(Get-FileHash $path).Hash;$plan=HubCall $request
  [IO.File]::AppendAllText($path,'; external edit',$encoding)
  $before=(Get-FileHash $path).Hash;$result=Execute-Plan $plan
  Assert ($result.status -eq 'PreconditionChanged' -and (Get-FileHash $path).Hash -eq $before) 'NH-06-CONCURRENT-EDIT: stale draft preserves external edit'
 }
 $game=New-GameFixture 'mfg-refusal';$path=Join-Path $game 'OptiScaler.ini'
 foreach($case in @(
  @{text="[DLSSG]`nNativeMfgExperimental=false`n";section='DLSSG';key='NativeMfgExperimental';value='enabled'},
  @{text="[DLSSG]`nExperimentalUnlockRTX30=false`nExperimentalUnlockRTX30=true`n";section='DLSSG';key='ExperimentalUnlockRTX30';value='true'},
  @{text="[DLSSG]`nExperimentalUnlockRTX20=false`n[DLSSG]`nUnknown=keep`n";section='DLSSG';key='ExperimentalUnlockRTX20';value='true'},
  @{text="[Menu]`nOptiClip=true`n";section='Menu';key='OptiClip';value='false'}
 )){
  [IO.File]::WriteAllText($path,$case.text);$before=(Get-FileHash $path).Hash
  $request=[ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='SaveSettings';configRevision=$before;settings=@([ordered]@{section=$case.section;key=$case.key;value=$case.value})}
  $result=HubCall $request
  Assert ($result.status -eq 'FailedWithoutChanges' -and (Get-FileHash $path).Hash -eq $before) 'NH-06-MFG: owner refuses invalid, ambiguous or retired preference without mutation'
 }
}finally{End-HubFixtures}
