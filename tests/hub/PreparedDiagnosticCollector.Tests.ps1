$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo 'builds/universal-connections/x86-ui/prepared-collector-fixture'
if(Test-Path -LiteralPath $root){throw 'Prior owned fixture must be inspected'}
$game=Join-Path $root 'Portal2';$helper=Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost';$output=Join-Path $root 'export'
New-Item -ItemType Directory -Path $helper,$output -Force|Out-Null
$previousTemp=$env:TEMP;$env:TEMP=Join-Path $root 'temp';New-Item -ItemType Directory -Path $env:TEMP|Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
foreach($relative in @('ReShade.log','ReShade.ini','NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.log','NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini','NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler_123456.log','NeuRotic/Prepared/NeuRotic.GpuHost/NeuRotic-NR-admission.json','NeuRotic/Prepared/NeuRotic.GpuHost/NeuRotic-Vulkan-trace.csv')){[IO.File]::WriteAllText((Join-Path $game $relative),"diagnostic $relative",$utf8)}
foreach($relative in @('NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32','NeuRotic/Prepared/NeuRotic.GpuHost/NeuRotic.PreparedGpuHost.exe','NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.dll','NeuRotic/Prepared/NeuRotic.GpuHost/nvngx_dlssnr.dll','NeuRotic/Prepared/NeuRotic.GpuHost/unknown-private-model.dll')){[IO.File]::WriteAllText((Join-Path $game $relative),'PRIVATE BINARY MUST NOT BE COPIED',$utf8)}
[IO.File]::WriteAllText((Join-Path $helper 'OptiScaler.ini'),"[Log]`nLogFileName=custom renderer.log`nSingleFile=false",$utf8)
[IO.File]::WriteAllText((Join-Path $helper 'custom renderer_987654.log'),'configured helper log',$utf8)
[IO.File]::WriteAllText((Join-Path $env:TEMP 'NeuRotic-NR-admission-helper.json'),(@{requested_provider=(Join-Path $helper 'nvngx_dlssnr.dll')}|ConvertTo-Json),$utf8)
[IO.File]::WriteAllText((Join-Path $env:TEMP 'NeuRotic-NR-admission-foreign.json'),(@{requested_provider=(Join-Path $root 'Other/NeuRotic/Prepared/NeuRotic.GpuHost/nvngx_dlssnr.dll')}|ConvertTo-Json),$utf8)
try{
 & (Join-Path $repo 'tools/packaging/customer/support/Collect-NR-Review.ps1') -GameDirectory $game -OutputDirectory $output
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=[IO.Compression.ZipFile]::OpenRead(@(Get-ChildItem -LiteralPath $output -Filter '*.zip')[0].FullName)
 try{
  $reader=[IO.StreamReader]::new(@($zip.Entries|Where-Object Name -eq 'COLLECTION.json')[0].Open());try{$report=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
  foreach($relative in @('ReShade.log','ReShade.ini','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.log','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler_123456.log','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic-NR-admission.json','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic-Vulkan-trace.csv')){if(@($report.files|Where-Object {$_.file -eq $relative -and $_.status -eq 'copied'}).Count -ne 1){throw "Prepared diagnostic missing: $relative"}}
  foreach($relative in @('NeuRotic\Prepared\NeuRotic-PreparedGuides.addon32','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.dll','NeuRotic\Prepared\NeuRotic.GpuHost\nvngx_dlssnr.dll','NeuRotic\Prepared\NeuRotic.GpuHost\unknown-private-model.dll')){if(@($report.files|Where-Object {$_.file -eq $relative -and $_.status -eq 'hash_only' -and $_.sha256}).Count -ne 1){throw "Prepared binary hash missing: $relative"}}
  if(@($zip.Entries|Where-Object {$_.Name -match '\.(dll|exe|addon32)$'}).Count){throw 'Private binary payload copied'}
  if(@($zip.Entries|Where-Object {$_.Name -eq 'NeuRotic__Prepared__NeuRotic.GpuHost__custom renderer_987654.log'}).Count -ne 1){throw 'Configured numbered helper log absent'}
  if(@($zip.Entries|Where-Object {$_.Name -eq 'NeuRotic-NR-admission-helper.json'}).Count -ne 1 -or @($zip.Entries|Where-Object {$_.Name -eq 'NeuRotic-NR-admission-foreign.json'}).Count){throw 'Prepared helper fallback target binding failed'}
 }finally{$zip.Dispose()}
 if(@(Get-ChildItem -LiteralPath $output -Directory).Count){throw 'Completed ZIP retained staging'}
 $outside=Join-Path $root 'outside';New-Item -ItemType Directory -Path $outside|Out-Null;[IO.File]::WriteAllText((Join-Path $outside 'OptiScaler.log'),'PRIVATE OUTSIDE LOG',$utf8)
 $linkedGame=Join-Path $root 'linked-game';New-Item -ItemType Directory -Path (Join-Path $linkedGame 'NeuRotic/Prepared') -Force|Out-Null
 $linkedHelper=Join-Path $linkedGame 'NeuRotic/Prepared/NeuRotic.GpuHost';New-Item -ItemType Junction -Path $linkedHelper -Target $outside|Out-Null
 try{
  $linkedOutput=Join-Path $root 'linked-export';& (Join-Path $repo 'tools/packaging/customer/support/Collect-NR-Review.ps1') -GameDirectory $linkedGame -OutputDirectory $linkedOutput
  $zip=[IO.Compression.ZipFile]::OpenRead(@(Get-ChildItem -LiteralPath $linkedOutput -Filter '*.zip')[0].FullName)
  try{
   if(@($zip.Entries|Where-Object {$_.Name -eq 'NeuRotic__Prepared__NeuRotic.GpuHost__OptiScaler.log'}).Count){throw 'Linked helper escaped selected ordinary directories'}
   $reader=[IO.StreamReader]::new(@($zip.Entries|Where-Object Name -eq 'COLLECTION.json')[0].Open());try{$linkedReport=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
   if(-not @($linkedReport.omissions|Where-Object {$_.file -like '*NeuRotic.GpuHost*'}).Count){throw 'Linked helper omission not reported'}
  }finally{$zip.Dispose()}
 }finally{[IO.Directory]::Delete($linkedHelper,$false)}
 Write-Host 'PASS prepared helper logs/config/traces, ReShade diagnostics, helper/addon/unknown-model hashes only, verified ZIP cleanup'
}finally{
 $env:TEMP=$previousTemp
 $resolved=[IO.Path]::GetFullPath($root);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/universal-connections/x86-ui/prepared-collector-fixture'))){throw 'Fixture escape'}
 foreach($item in Get-ChildItem -LiteralPath $root -Recurse -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $root -Recurse -Force
}
