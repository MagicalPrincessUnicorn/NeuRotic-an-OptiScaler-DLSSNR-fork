param([Parameter(Mandatory=$true)][string]$SupportRoot,[Parameter(Mandatory=$true)][string]$FixtureRoot,
 [ValidateSet('selected','changed','empty','update-removed')][string]$Case='selected')
$ErrorActionPreference='Stop';$script:Checks=0
function Check($value,[string]$why){if(-not $value){throw $why};$script:Checks++}
function Pe([string]$path,[bool]$dll){
 $b=New-Object byte[] 512;$b[0]=77;$b[1]=90;[BitConverter]::GetBytes([int]64).CopyTo($b,60);[BitConverter]::GetBytes([int]17744).CopyTo($b,64)
 [BitConverter]::GetBytes([uint16]0x8664).CopyTo($b,68);[BitConverter]::GetBytes([uint16]1).CopyTo($b,70)
 [BitConverter]::GetBytes([uint16]240).CopyTo($b,84);[BitConverter]::GetBytes([uint16]$(if($dll){0x2002}else{2})).CopyTo($b,86);[BitConverter]::GetBytes([uint16]0x20b).CopyTo($b,88);[IO.File]::WriteAllBytes($path,$b)
}
try{
 $work=[IO.Path]::GetFullPath($FixtureRoot)
 if(-not $work.StartsWith('C:\NeuRotic\',[StringComparison]::OrdinalIgnoreCase)-or(Test-Path -LiteralPath $work)){throw 'new owned workspace child required'}
 [void][IO.Directory]::CreateDirectory($work);$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
 . (Join-Path $SupportRoot 'NeuRotic-InstallCore.ps1')
 $payload=Join-Path $work 'package/payload';[void][IO.Directory]::CreateDirectory($payload);Pe (Join-Path $payload 'OptiScaler.dll') $true
 [IO.File]::WriteAllText((Join-Path $payload 'OptiScaler.ini'),"[Hooks]`r`nDxgi=auto`r`n[DlssNr]`r`nEnabled=false`r`n")
 [IO.File]::WriteAllText((Join-Path $payload 'dependency.dll'),'own updated bytes')
 $inspector='OptiScaler/CharacterInspector/Version 1';[void][IO.Directory]::CreateDirectory((Join-Path $payload $inspector))
 foreach($n in @('worker.py','models.lock.json')){[IO.File]::WriteAllText((Join-Path (Join-Path $payload $inspector) $n),'inert')}
 $package=[pscustomobject]@{id='fixture';root=(Join-Path $work 'package');architecture='x64';manifest=[pscustomobject]@{components=[pscustomobject]@{characterInspector=[pscustomobject]@{version='Version 1';relativeRoot=$inspector}};files=@('OptiScaler.dll','OptiScaler.ini','dependency.dll',($inspector+'/worker.py'),($inspector+'/models.lock.json')|ForEach-Object{[pscustomobject]@{path=('payload/'+$_)}})}}
 $game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory($game);$exe=Join-Path $game 'Game.exe';Pe $exe $false
 $folder=Join-Path $work 'profile';[void][IO.Directory]::CreateDirectory($folder);$source=Join-Path $folder 'nvngx_dlssnr.dll'
 [IO.File]::WriteAllText($source,$(if($Case -eq 'empty'){''}else{'actual selected user bytes'}))
 $copy=[pscustomobject]@{mode='profile';profileId=('a'*32);profileName='User profile';source=$source;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant();bytes=[uint64](Get-Item -LiteralPath $source).Length}
 if($Case -eq 'changed'){[IO.File]::WriteAllText($source,'other bytes after submit')}
 if($Case -eq 'update-removed'){
  $first=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null $null ([pscustomobject]@{mode='none'})
  Check ($first.status -in @('Succeeded','SucceededWithNotes')) 'manual initial install control'
  [IO.File]::WriteAllText((Join-Path $game 'nvngx_dlssnr.dll'),'existing manual provider KEEP');[IO.File]::Delete($source);[IO.File]::Delete((Join-Path $game 'dependency.dll'))
 }
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null $null $copy
 if($Case -eq 'changed'){
  Check ($result.status -eq 'FailedWithoutChanges') 'changed selected source refuses before own-file mutation'
  Check (-not [IO.File]::Exists((Join-Path $game 'dxgi.dll'))) 'changed source has no partial installation side effect'
 }else{
  Check ($result.status -in @('Succeeded','SucceededWithNotes')) 'selected or source-independent update completes'
  Check ([IO.File]::Exists((Join-Path $game 'dependency.dll'))) 'NeuRotic own dependency is delivered'
  $target=Join-Path $game 'nvngx_dlssnr.dll'
  if($Case -eq 'update-removed'){Check ([IO.File]::ReadAllText($target) -ceq 'existing manual provider KEEP') 'update never acquires removed selected provider'}
  else{Check ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ieq $copy.sha256) 'actual selected bytes copied with exact continuity, including zero-byte choice'}
 }
 [Console]::Out.WriteLine(('PASS {0} real selected NR copy checks case={1}; synthetic files, no provider load' -f $script:Checks,$Case));exit 0
}catch{[Console]::Error.WriteLine(('FAIL '+$_.Exception.Message));exit 1}
