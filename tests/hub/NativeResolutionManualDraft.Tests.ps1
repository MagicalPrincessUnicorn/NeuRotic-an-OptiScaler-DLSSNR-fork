param([string]$RepositoryRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$ErrorActionPreference='Stop'
. (Join-Path $RepositoryRoot 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function SafePath($root,$relative){Join-Path $root $relative}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$work=Join-Path ([IO.Path]::GetTempPath()) ('NeuRotic-NR-WO-0041-ManualDraft-'+[Guid]::NewGuid().ToString('N'))
$expected=[IO.Path]::GetFullPath($work)
if(Test-Path -LiteralPath $work){throw 'Existing fixture preserved'}
New-Item -ItemType Directory -Path $work -ErrorAction Stop|Out-Null
$owner=Join-Path $work 'NR-WO-0041-owner.txt';[IO.File]::WriteAllText($owner,$expected)
$ini=Join-Path $work 'OptiScaler.ini'
$base="[DlssNr]`r`nRoute=0`r`nNativeResolution=0`r`nRenderingMode=0`r`nWorkingScale=0.67`r`nUiManualResolution=true`r`nUiManualScale=0.67`r`nUnknown=retain`r`n"
function Reset-Ini{[IO.File]::WriteAllText($ini,$base,(New-Object Text.UTF8Encoding($false)))}
function Patch($choice,$memory){[pscustomobject]@{section='DlssNr';key='NativeResolutionChoice';value=$choice;nativeManualScale=$memory}}
function Save($patches){[IO.File]::WriteAllBytes($ini,(Get-HubSettingsBytes $work $patches))}
function Native{return @(Get-HubSettingsProjection $work|Where-Object{$_.key -ceq 'NativeResolutionChoice'})[0]}
try{
 foreach($choice in @('pre','post','full')){
  Reset-Ini
  Save @((Patch $choice '0.5'),[pscustomobject]@{section='DlssNr';key='WorkingScale';value='1'})
  Assert ((Native).nativeManualScale -ceq '0.5') ($choice+' Save/reload preserves unsaved Manual 0.5')
  Assert ([IO.File]::ReadAllText($ini) -notmatch 'NativeResolutionChoice|nativeManualScale=') 'virtual action and request metadata never persist as INI keys'
  Save @([pscustomobject]@{section='DlssNr';key='NativeResolutionChoice';value='manual'})
  Assert ((Native).value -ceq 'manual' -and @((Read-HubSettingsDocument $ini).entries|Where-Object{$_.key -ceq 'WorkingScale'})[0].value -ceq '0.5') 'Manual restores latest remembered draft after reload'
 }
 Reset-Ini;Save @((Patch post '0.67'));Assert ((Native).nativeManualScale -ceq '0.67') 'switch without a Manual edit retains saved scale'
 Reset-Ini;Save @((Patch post '0.67'));Save @((Patch post '0.5'));Assert ((Native).value -ceq 'post' -and (Native).nativeManualScale -ceq '0.5') 'same original Post action can save only changed remembered Manual memory'
 Reset-Ini;Save @();Assert ([IO.File]::ReadAllText($ini) -ceq $base) 'Undo with no remaining edits saves no changes'
 Reset-Ini;Save @((Patch post '0.67'));Assert ((Native).nativeManualScale -ceq '0.67') 'Undo-restored original memory remains original on a later automatic save'
 foreach($bad in @('NaN','0.2','2.1','invalid',9,1)){
  Reset-Ini;$before=(Get-FileHash $ini).Hash;$refused=$false;try{Save @((Patch post $bad))}catch{$refused=$true}
  Assert ($refused -and (Get-FileHash $ini).Hash -ceq $before) 'invalid remembered Manual scale refuses atomically'
 }
 Reset-Ini;$refused=$false;try{Save @([pscustomobject]@{section='DlssNr';key='WorkingScale';value='1';nativeManualScale='0.5'})}catch{$refused=$true};Assert $refused 'remembered scale metadata belongs only to the semantic resolution action'
 Write-Output 'PASS Native Manual draft persistence contract; native Undo/planner execution remains owner-05 QA'
}finally{
 if([IO.Path]::GetFullPath($work) -cne $expected -or [IO.File]::ReadAllText($owner) -cne $expected){throw 'Unowned cleanup refused'}
 if((Get-Item -LiteralPath $work -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked root cleanup refused'}
 foreach($item in Get-ChildItem -LiteralPath $work -Recurse -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused'}}
 Remove-Item -LiteralPath $work -Recurse -Force
}
