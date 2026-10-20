param([string]$RepoRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
. (Join-Path $RepoRoot 'tests/hub/SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try {
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 . (Join-Path $script:SimpleSupport 'NeuRotic-HubProtocol.ps1')
 # Reuse the exact production entry-point path adapter; no test bypass.
 function SafePath([string]$Root,[string]$Relative){return Resolve-NeuroticOwnedPath $Root $Relative}
 function Write-TargetPe([string]$Path,[int]$Bits,[string]$Kind='Executable') {
  $bytes=New-Object byte[] 512;$bytes[0]=77;$bytes[1]=90
  [BitConverter]::GetBytes([int]64).CopyTo($bytes,60);[BitConverter]::GetBytes([int]17744).CopyTo($bytes,64)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x8664}else{0x14c})).CopyTo($bytes,68)
  [BitConverter]::GetBytes([uint16]1).CopyTo($bytes,70)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){240}else{224})).CopyTo($bytes,84)
  [BitConverter]::GetBytes([uint16]$(if($Kind -eq 'Dll'){0x2002}elseif($Kind -eq 'NonExecutable'){0}else{2})).CopyTo($bytes,86)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x20b}else{0x10b})).CopyTo($bytes,88)
  if($Kind -eq 'ZeroSections'){[BitConverter]::GetBytes([uint16]0).CopyTo($bytes,70)}
  if($Kind -eq 'TruncatedSections'){$bytes=$bytes[0..349]}
  if($Kind -eq 'MismatchedMagic'){[BitConverter]::GetBytes([uint16]0x10b).CopyTo($bytes,88)}
  if($Kind -eq 'ShortDosOffset'){[BitConverter]::GetBytes([int]32).CopyTo($bytes,60)}
  if($Kind -eq 'OutsidePeOffset'){[BitConverter]::GetBytes([int]16777217).CopyTo($bytes,60)}
  if($Kind -eq 'ShortOptionalHeader'){[BitConverter]::GetBytes([uint16]2).CopyTo($bytes,84)}
  if($Kind -eq 'ManyTruncatedSections'){[BitConverter]::GetBytes([uint16]65535).CopyTo($bytes,70)}
  [IO.File]::WriteAllBytes($Path,$bytes)
 }
 $package=New-SimplePackageFixture
 Write-TargetPe (Join-Path $package.root 'payload/OptiScaler.dll') 64 'Dll'
 Assert-Simple ((Get-NeuroticExecutableArchitecture (Join-Path $package.root 'payload/OptiScaler.dll')) -eq 'x64') 'package DLL architecture remains readable'
 $valid=New-SimpleGameFixture 'valid-x64';$exe=Join-Path $valid 'FixtureGame.exe';Write-TargetPe $exe 64
 Assert-Simple ((Get-HubTarget $exe).bitness -eq 64) 'valid x64 target admitted by actual Hub target validator'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'Succeeded' -and [IO.File]::Exists((Join-Path $valid 'dxgi.dll'))) 'valid x64 executable installs real x64 DLL payload'
 $x86=New-SimpleGameFixture 'valid-x86';$exe=Join-Path $x86 'FixtureGame.exe';Write-TargetPe $exe 32
 Assert-Simple ((Get-HubTarget $exe).bitness -eq 32) 'valid x86 target architecture remains observable'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and ($result.notes -join ' ') -match 'NR Anything' -and -not [IO.File]::Exists((Join-Path $x86 'dxgi.dll'))) 'x86 target keeps NR Anything guidance without proxy writes'
 foreach($kind in @('Dll','NonExecutable','ZeroSections','TruncatedSections','MismatchedMagic','ShortDosOffset','OutsidePeOffset','ShortOptionalHeader','ManyTruncatedSections')) {
  $game=New-SimpleGameFixture $kind;$exe=Join-Path $game 'FixtureGame.exe';Write-TargetPe $exe 64 $kind
  $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null
  Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and -not [IO.File]::Exists((Join-Path $game 'dxgi.dll')) -and -not [IO.Directory]::Exists((Join-Path $game 'NeuRotic'))) ($kind+' target refuses actual installation before writes')
  $refused=$false;try{[void](Get-HubTarget $exe)}catch{$refused=$true}
  Assert-Simple $refused ($kind+' target refuses actual Hub planning target validator')
 }
} finally {End-SimpleFixtures}
