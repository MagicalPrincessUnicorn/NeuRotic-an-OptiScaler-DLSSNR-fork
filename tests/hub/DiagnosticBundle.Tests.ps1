$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo 'builds\hub-diagnostic-fixture'
if(Test-Path -LiteralPath $root){throw 'Fixture already exists; inspect and remove the owned fixture before rerunning.'}
New-Item -ItemType Directory -Path $root | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$game=Join-Path $root ('Game space '+[char]0x03A9)
$output=Join-Path $root 'Export space'
$temporary=Join-Path $root 'Temporary records';$previousTemp=$env:TEMP;$env:TEMP=$temporary
New-Item -ItemType Directory -Path $game,$output,$temporary,(Join-Path $game 'NeuRotic\Installer') -Force | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.log'),(('full log line '+[char]0x03A9+"`r`n")*40000)+'END-OF-FULL-LOG',$utf8)
[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[Log]`nLogFileName=custom log.txt`nSingleFile=false`n[NR]",$utf8)
[IO.File]::WriteAllText((Join-Path $game 'OptiScaler_123456.log'),'numbered default log',$utf8)
[IO.File]::WriteAllText((Join-Path $game 'custom log_987654.log'),'numbered configured log',$utf8)
[IO.File]::WriteAllText((Join-Path $game 'NeuRotic\Installer\Current-Install.json'),'{"fixture":true}',$utf8)
[IO.File]::WriteAllText((Join-Path $game 'nvngx_dlssnr.dll'),'PRIVATE MODEL DO NOT COPY',$utf8)
[IO.File]::WriteAllText((Join-Path $output 'keep.txt'),'unrelated output',$utf8)
[IO.File]::WriteAllText((Join-Path $game 'Game.exe'),'fixture executable never run',$utf8)
$snapshot=Join-Path $root 'snapshot.json';[IO.File]::WriteAllText($snapshot,'{"kind":"CachedHubInspection","runtime":"NotAttempted"}',$utf8)
[IO.File]::WriteAllText((Join-Path $temporary 'NeuRotic-NR-admission-bound.json'),(@{requested_provider=(Join-Path $game 'nvngx_dlssnr.dll')}|ConvertTo-Json),$utf8)
[IO.File]::WriteAllText((Join-Path $temporary 'NeuRotic-NR-admission-other.json'),(@{requested_provider=(Join-Path $root 'Other\nvngx_dlssnr.dll')}|ConvertTo-Json),$utf8)
$collector=Join-Path $repo 'tools\packaging\customer\support\Collect-NR-Review.ps1'
try {
 & $collector -GameDirectory $game -GameExecutable (Join-Path $game 'Game.exe') -OutputDirectory $output -HubSnapshotPath $snapshot -ResultPath (Join-Path $root 'result.json')
 $archives=@(Get-ChildItem -LiteralPath $output -Filter '*.zip')
 if($archives.Count -ne 1){throw 'Expected one completed ZIP'}
 $zip=[IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
 try {
  $log=@($zip.Entries | Where-Object {$_.Name -eq 'OptiScaler.log'})
  if($log.Count -ne 1 -or $log[0].Length -ne (Get-Item -LiteralPath (Join-Path $game 'OptiScaler.log')).Length){throw 'Full log was truncated'}
  $stream=$log[0].Open();$hash=[Security.Cryptography.SHA256]::Create()
  try{$digest=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$hash.Dispose()}
  if($digest -ne (Get-FileHash -LiteralPath (Join-Path $game 'OptiScaler.log')).Hash){throw 'Full log contents differ'}
  if(@($zip.Entries | Where-Object {$_.FullName -match 'Current-Install.json$'}).Count -ne 1){throw 'Installer receipt is missing from full diagnostic ZIP'}
  if(@($zip.Entries | Where-Object {$_.Name -like '*.dll'}).Count){throw 'Private binary was copied'}
  if(@($zip.Entries | Where-Object {$_.Name -eq 'HUB-INSPECTION.json'}).Count -ne 1){throw 'Hub snapshot missing'}
  if(@($zip.Entries | Where-Object {$_.Name -in @('OptiScaler_123456.log','custom log_987654.log')}).Count -ne 2){throw 'Runtime numbered/configured game log missing'}
  if(@($zip.Entries | Where-Object {$_.Name -eq 'NeuRotic-NR-admission-bound.json'}).Count -ne 1 -or @($zip.Entries | Where-Object {$_.Name -eq 'NeuRotic-NR-admission-other.json'}).Count){throw 'Fallback target binding failed'}
  $reader=[IO.StreamReader]::new(@($zip.Entries | Where-Object {$_.Name -eq 'COLLECTION.json'})[0].Open())
  try{$report=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
  if(-not $report.files -or -not $report.provider_execution){throw 'Collection provenance missing'}
  if(@($report.files|Where-Object {$_.file -eq 'NeuRotic\Installer\Cleanup-Pending.json' -and $_.status -eq 'absent'}).Count -ne 1){throw 'Absent artifact not reported'}
 } finally {$zip.Dispose()}
 if(@(Get-ChildItem -LiteralPath $output -Directory).Count){throw 'Successful export retained staging'}
 if([IO.File]::ReadAllText((Join-Path $output 'keep.txt')) -ne 'unrelated output'){throw 'Export changed unrelated output'}
 $receipt=Get-Content -Raw -LiteralPath (Join-Path $root 'result.json')|ConvertFrom-Json
 if(-not $receipt.success -or $receipt.sha256 -ne (Get-FileHash -LiteralPath $archives[0].FullName).Hash){throw 'Archive receipt hash mismatch'}
 $locked=[IO.File]::Open((Join-Path $game 'OptiScaler.log'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $lockedOutput=Join-Path $root 'Unreadable export'
 try{& $collector -GameDirectory $game -OutputDirectory $lockedOutput}finally{$locked.Dispose()}
 $zip=[IO.Compression.ZipFile]::OpenRead(@(Get-ChildItem -LiteralPath $lockedOutput -Filter '*.zip')[0].FullName)
 try{
  $reader=[IO.StreamReader]::new(@($zip.Entries|Where-Object {$_.Name -eq 'COLLECTION.json'})[0].Open())
  try{$report=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
  if(@($report.omissions|Where-Object {$_.file -eq 'OptiScaler.log'}).Count -ne 1){throw 'Unreadable log silently omitted'}
 }finally{$zip.Dispose()}
 $linked=Join-Path $root 'Linked game'
 New-Item -ItemType Junction -Path $linked -Target $game|Out-Null
 try{
  $failed=$false;try{& $collector -GameDirectory $linked -OutputDirectory $output}catch{$failed=$true}
  if(-not $failed){throw 'Linked game directory accepted'}
 }finally{[IO.Directory]::Delete($linked,$false)}
 $failed=$false;try{& $collector -GameDirectory $game -OutputDirectory (Join-Path $output 'keep.txt')}catch{$failed=$true}
 if(-not $failed -or [IO.File]::ReadAllText((Join-Path $output 'keep.txt')) -ne 'unrelated output'){throw 'Failed export touched unrelated output'}
 Write-Host 'PASS full log bytes/hash, receipts/snapshot, binary exclusion, exact fallback binding, unicode/space paths, missing/unreadable reports, link refusal, failed export safety, verified staging cleanup'
} finally {
 $env:TEMP=$previousTemp
 # Fixture owns this exact tree; reject links before recursive cleanup.
 $resolved=[IO.Path]::GetFullPath($root)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds\hub-diagnostic-fixture'))){throw 'Unexpected fixture root'}
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($resolved)
 while($pending.Count){foreach($item in Get-ChildItem -LiteralPath $pending.Pop() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture; cleanup refused'};if($item.PSIsContainer){$pending.Push($item.FullName)}}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
