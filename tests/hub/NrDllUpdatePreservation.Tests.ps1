param(
 [Parameter(Mandatory=$true)][string]$SupportRoot,
 [Parameter(Mandatory=$true)][string]$FixtureRoot,
 [ValidateSet('initial','no-copy','update-existing','update-missing','fresh-update','partial-update','corrupt-marker','orphan-ini')][string]$Case='update-existing'
)
$ErrorActionPreference='Stop'
function Check-Nr($Value,[string]$Message){if(-not $Value){throw $Message};$script:Checks++}
$script:Checks=0
function Write-TestPe([string]$Path,[bool]$Library){
 $bytes=New-Object byte[] 512;$bytes[0]=77;$bytes[1]=90
 [BitConverter]::GetBytes([int]64).CopyTo($bytes,60);[BitConverter]::GetBytes([int]17744).CopyTo($bytes,64)
 [BitConverter]::GetBytes([uint16]0x8664).CopyTo($bytes,68);[BitConverter]::GetBytes([uint16]1).CopyTo($bytes,70)
 [BitConverter]::GetBytes([uint16]$(if($Library){0x2002}else{0x0002})).CopyTo($bytes,86)
 [BitConverter]::GetBytes([uint16]240).CopyTo($bytes,84);[BitConverter]::GetBytes([uint16]0x20b).CopyTo($bytes,88)
 [IO.File]::WriteAllBytes($Path,$bytes)
}
try{
 $work=[IO.Path]::GetFullPath($FixtureRoot);$workspace=[IO.Path]::GetFullPath('C:/NeuRotic').TrimEnd('\','/')+'\'
 if(-not $work.StartsWith($workspace,[StringComparison]::OrdinalIgnoreCase)-or $work -eq $workspace.TrimEnd('\')){throw 'Fixture must be an owned child of the workspace'}
 if(Test-Path -LiteralPath $work){throw 'Fixture already exists; another run or crash may own it'}
 [void][IO.Directory]::CreateDirectory($work);$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
 . (Join-Path $SupportRoot 'NeuRotic-InstallCore.ps1')
 $packageRoot=Join-Path $work 'package';$payload=Join-Path $packageRoot 'payload';[void][IO.Directory]::CreateDirectory($payload)
 Write-TestPe (Join-Path $payload 'OptiScaler.dll') $true
 [IO.File]::WriteAllText((Join-Path $payload 'OptiScaler.ini'),"[Hooks]`r`nDxgi=auto`r`nRestoreComputeSignature=auto`r`n[Plugins]`r`nLoadReshade=false`r`n[DlssNr]`r`nEnabled=false`r`n")
 [IO.File]::WriteAllText((Join-Path $payload 'dependency.dll'),'current NeuRotic dependency')
 $inspector='OptiScaler/CharacterInspector/Version 1';[void][IO.Directory]::CreateDirectory((Join-Path $payload $inspector))
 foreach($name in @('worker.py','models.lock.json')){[IO.File]::WriteAllText((Join-Path (Join-Path $payload $inspector) $name),'inert fixture source')}
 $package=[pscustomobject]@{id='fixture-native';root=$packageRoot;architecture='x64';manifest=[pscustomobject]@{components=[pscustomobject]@{characterInspector=[pscustomobject]@{version='Version 1';relativeRoot=$inspector}};files=@('OptiScaler.dll','OptiScaler.ini','dependency.dll',($inspector+'/worker.py'),($inspector+'/models.lock.json')|ForEach-Object{[pscustomobject]@{path=('payload/'+$_)}})}}
 $game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory($game);$exe=Join-Path $game 'FixtureGame.exe';Write-TestPe $exe $false
 $sourceRoot=Join-Path $work 'selected-profile';[void][IO.Directory]::CreateDirectory($sourceRoot);$source=Join-Path $sourceRoot 'nvngx_dlssnr.dll';[IO.File]::WriteAllText($source,'selected user profile A')
 function Components {
  $info=Get-Item -LiteralPath $source
  return [pscustomobject]@{neuralModel=[pscustomobject]@{status='Present';path=$source;bytes=$info.Length;lastWriteTicks=$info.LastWriteTimeUtc.Ticks;fileIdentity=[NeuRoticSimpleFileIo]::Stat($source)}}
 }
 $initial=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $(if($Case -eq 'no-copy'){$null}else{Components}) $null
 Check-Nr ($initial.status -in @('Succeeded','SucceededWithNotes')) 'initial setup control must succeed'
 $provider=Join-Path $game 'nvngx_dlssnr.dll'
 if($Case -eq 'no-copy'){
  Check-Nr (-not [IO.File]::Exists($provider)) 'manual no-copy install remains available'
 }elseif($Case -eq 'initial'){
  Check-Nr ([IO.File]::ReadAllText($provider) -ceq 'selected user profile A') 'new install copies selected user bytes'
 }else{
  [IO.File]::WriteAllText($provider,'user-selected installed bytes KEEP')
  $expected=[IO.File]::ReadAllBytes($provider)
  if($Case -eq 'update-missing'){[IO.File]::Delete($provider)}
  if($Case -eq 'orphan-ini'){
   $installer=[IO.Path]::GetFullPath((Join-Path $game 'NeuRotic/Installer'));$boundary=[IO.Path]::GetFullPath($game).TrimEnd('\','/')+'\'
   if(-not $installer.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)){throw 'installer fixture cleanup escaped owned game root'}
   $entries=@(Get-ChildItem -LiteralPath $installer -Force)
   foreach($entry in $entries){if($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'unexpected linked or nested installer fixture'}}
   if((Get-Item -LiteralPath $installer -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'linked installer fixture refused'}
   [IO.Directory]::Delete($installer,$true)
   Check-Nr ([IO.File]::Exists((Join-Path $game 'OptiScaler.ini'))) 'existing configuration survives missing ownership metadata'
  }
  if($Case -eq 'partial-update'){$old=(Read-NeuroticInstallReceipt $game).receipt;$old.status='Partial';Save-NeuroticInstallReceipt $game $old}
  if($Case -eq 'corrupt-marker'){[IO.File]::WriteAllText((Join-Path $game 'NeuRotic/Installer/Current-Install.json'),'{ invalid receipt')}
  [IO.File]::WriteAllText($source,'different newly selected/default user profile B')
  [IO.File]::Delete((Join-Path $game 'dependency.dll'))
  $decisions=@()
  if($Case -in @('corrupt-marker','orphan-ini')){
   $profile=Get-NeuroticInstallProfile $exe $package $null;$profile.proxy='dxgi.dll'
   $decisions=@(Get-NeuroticInstallActions $game $package $profile $false $null (Components)|Where-Object{$_.operation -ne 'Omit' -and $_.conflict}|ForEach-Object{[pscustomobject]@{path=$_.path;action='Replace'}})
  }
  $update=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' ($Case -eq 'fresh-update') $decisions '' (Components) $null
  Check-Nr ($update.status -in @('Succeeded','SucceededWithNotes')) 'ordinary update still installs NeuRotic-owned files'
  Check-Nr ([IO.File]::Exists((Join-Path $game 'dependency.dll'))) 'missing NeuRotic dependency is updated'
  $preserved=if($Case -eq 'update-missing'){-not [IO.File]::Exists($provider)}else{[Convert]::ToBase64String([IO.File]::ReadAllBytes($provider)) -ceq [Convert]::ToBase64String($expected)}
  Check-Nr $preserved 'ordinary updates must preserve game NR DLL exactly'
  if($Case -notin @('corrupt-marker','orphan-ini')){Check-Nr (@($update.receipt.files|Where-Object{$_.path -ieq 'nvngx_dlssnr.dll' -and $_.removeOnUninstall}).Count -eq 1) 'existing provider receipt ownership remains intact'}
  else{Check-Nr (-not @($update.receipt.files|Where-Object{$_.path -ieq 'nvngx_dlssnr.dll'}).Count) 'unproven provider ownership is not invented'}
 }
 [Console]::Out.WriteLine(('PASS {0} real installer file checks mode={1}; synthetic files, no DLL loading' -f $script:Checks,$Case));exit 0
}catch{
 [Console]::Error.WriteLine(('FAIL '+$_.Exception.Message));exit 1
}
