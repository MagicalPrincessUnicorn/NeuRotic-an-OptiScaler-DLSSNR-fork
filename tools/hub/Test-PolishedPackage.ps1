param([Parameter(Mandatory=$true)][string]$Archive)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../tests/hub/FixtureCommon.ps1')
$taskPackageWork=[IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-delivery-check'))
if(Test-Path -LiteralPath $taskPackageWork){throw 'Inspect retained delivery fixture before reuse'}
New-Item -ItemType Directory -Path $taskPackageWork|Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskPackage=Join-Path $taskPackageWork 'NeuRotic'
$script:Work=$taskPackageWork
$script:Support=Join-Path $taskPackage 'support'
$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $taskPackageWork 'user-data'
$taskArchive=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
try{
 foreach($entry in $taskArchive.Entries){
  if(-not $entry.FullName.StartsWith('NeuRotic/support/') -and -not $entry.FullName.StartsWith('NeuRotic/packages/current/') -and -not $entry.FullName.StartsWith('NeuRotic/discovery/')){continue}
  if($entry.FullName -match '(^|/)\.\.(/|$)|:|^/'){throw 'Unsafe archive entry'}
  if(-not $entry.Name){continue}
  $path=[IO.Path]::GetFullPath((Join-Path $taskPackageWork $entry.FullName))
  if(-not $path.StartsWith($taskPackageWork+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive escaped fixture'}
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path)|Out-Null
  [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$path,$false)
 }
}finally{$taskArchive.Dispose()}
try{
 $helper=Join-Path $taskPackage 'discovery/NeuRotic.Discovery.exe'
 & $helper --self-test (Join-Path $taskPackageWork 'discovery-fixtures')
 Assert ($LASTEXITCODE -eq 0) 'PACKAGE extracted self-contained discovery runs without development runtime'
 $game=New-GameFixture 'Game'
 $private=(Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash
 $plan=Plan-Game $game 'Install'
 Assert ($plan.status -eq 'Planned' -and $plan.antiCheat.acknowledgementRequired) 'PACKAGE actual registry produces reviewed local-risk plan'
 Assert (-not (Test-Path (Join-Path $game 'dxgi.dll'))) 'PACKAGE read-only planning installs nothing'
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded') 'PACKAGE current private approval installs through existing owner'
 Assert ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq '04c83bc21beb447af682bebcbf30e1aef96cbcb6b9e41fa22130a5edacab94f1') 'PACKAGE installed product matches verified e20908bd DLL'
 Assert ((Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash -eq $private) 'PACKAGE private NR model preserved'
 $ini=Join-Path $game 'OptiScaler.ini';$before=(Get-FileHash $ini).Hash
 $plan=Plan-Game $game 'Update';$result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and (Get-FileHash $ini).Hash -eq $before) 'PACKAGE update preserves saved configuration'
 $plan=Plan-Game $game 'Uninstall'
 Assert ($plan.status -eq 'Planned' -and -not $plan.antiCheat) 'PACKAGE uninstall available without detector approval'
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and -not (Test-Path (Join-Path $game 'dxgi.dll'))) 'PACKAGE ownership-safe uninstall restores original absence'
 Assert ((Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash -eq $private -and (Test-Path (Join-Path $game 'private.txt'))) 'PACKAGE uninstall preserves private and unknown files'
 Write-Output 'PASS packaged Hub lifecycle'
}finally{
 if($taskPackageWork -cne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-delivery-check'))){throw 'Unexpected cleanup root'}
 if(@(Get-ChildItem -LiteralPath $taskPackageWork -Recurse -Force|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Linked delivery fixture cleanup refused'}
 Remove-Item -LiteralPath $taskPackageWork -Recurse -Force
}
