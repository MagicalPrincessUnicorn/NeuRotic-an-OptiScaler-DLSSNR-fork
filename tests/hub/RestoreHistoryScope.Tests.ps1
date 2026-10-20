param([switch]$Baseline)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/hub-refinement/installer-history-fixture'
$evidence=Join-Path $repo 'docs/implementation-evidence/app-refinement-20261004'
if(Test-Path -LiteralPath $work){throw 'Installer history fixture already exists; inspect before reuse'}
New-Item -ItemType Directory -Path $work -Force|Out-Null
New-Item -ItemType Directory -Path $evidence -Force|Out-Null
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output "PASS $message"}
try{
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1')){
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo ('tools/packaging/customer/support/'+$name)),[ref]$tokens,[ref]$errors)
  if($errors.Count){throw ($errors|Out-String)}
  foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 }
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
 $HubNonInteractive=$true
 $game=$work;$exe=Join-Path $game 'FixtureGame.exe'
 [IO.File]::WriteAllText($exe,'inert fixture executable')
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'installed proxy')
 [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),'user settings')
 $chain=@()
 foreach($i in 1..8){
  $backup=Join-Path $game ('NeuRotic-backups/active-'+$i);New-Item -ItemType Directory -Path (Join-Path $backup 'previous') -Force|Out-Null
  $files=@(foreach($name in @('dxgi.dll','OptiScaler.ini')){
   $live=Join-Path $game $name;$previous=Join-Path $backup ('previous/'+$name)
   Copy-Item -LiteralPath $live -Destination $previous
   [pscustomobject]@{path=$name;existed=$true;previous_hash=(HashFile $previous);installed_hash=(HashFile $live)}
  })
  $record=Join-Path $backup 'INSTALL-MANIFEST.json'
  [IO.File]::WriteAllText($record,(@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;files=$files}|ConvertTo-Json -Depth 8));$chain+= $record
 }
 $state=[pscustomobject]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory=$game;restore_chain=$chain}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';New-Item -ItemType Directory -Path (Split-Path -Parent $statePath) -Force|Out-Null
 [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8))
 $sw=[Diagnostics.Stopwatch]::StartNew();$small=@(Get-HubInventory $game);$sw.Stop();$smallMs=$sw.Elapsed.TotalMilliseconds
 $payload=New-Object byte[] 32768
 foreach($i in 1..24){
  $old=Join-Path $game ('NeuRotic-backups/retired-'+$i);New-Item -ItemType Directory -Path $old|Out-Null
  foreach($j in 1..16){[IO.File]::WriteAllBytes((Join-Path $old ($j.ToString()+'.bin')),$payload)}
 }
 $sw.Restart();$large=@(Get-HubInventory $game);$sw.Stop();$largeMs=$sw.Elapsed.TotalMilliseconds
 $receipt=[ordered]@{baseline=[bool]$Baseline;activeRecords=8;retiredFiles=384;retiredBytes=12582912;smallInventoryFiles=$small.Count;largeInventoryFiles=$large.Count;smallMilliseconds=[math]::Round($smallMs,2);largeMilliseconds=[math]::Round($largeMs,2)}
 $receipt|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence $(if($Baseline){'installer-benchmark-before.json'}else{'installer-benchmark-after.json'}))
 $receipt|ConvertTo-Json|Write-Output
 if($Baseline){return}
 Assert ($large.Count -eq $small.Count) 'Retired payloads do not increase action revision inventory'
 Assert (@($large|Where-Object{$_.path -match 'previous|retired'}).Count -eq 0) 'Action revision excludes backup payload bytes'
 Assert (@($large|Where-Object{$_.path -like '*INSTALL-MANIFEST.json'}).Count -eq 8) 'All required chain manifests remain fingerprinted beyond five records'
 $locked=Join-Path $old '1.bin';$handle=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{[void](Get-HubInventory $game)}finally{$handle.Dispose()}
 Write-Output 'PASS Unreadable retired payload does not block action inspection'
 [array]::Reverse($chain)
 Test-RestoreChain $game $chain
 $required=Join-Path (Split-Path -Parent $chain[-1]) 'previous/dxgi.dll';[IO.File]::AppendAllText($required,'corruption')
 Test-RestoreChain $game $chain -MetadataOnly
 $refused=$false;try{Test-RestoreChain $game $chain}catch{if($_.Exception.Message -like 'Backup verification failed:*'){$refused=$true}else{throw}}
 Assert $refused 'Full validation refuses corrupt required backup older than five entries'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'installed proxy') 'Validation leaves live bytes unchanged'
 $record=Get-Content -Raw -LiteralPath $chain[0]|ConvertFrom-Json;$record.files[0].path='../outside.dll';[IO.File]::WriteAllText($chain[0],($record|ConvertTo-Json -Depth 8))
 $refused=$false;try{Test-RestoreChain $game $chain -MetadataOnly}catch{$refused=$true}
 Assert $refused 'Metadata preview still refuses paths outside ownership'
 $record.files[0].path='dxgi.dll';[IO.File]::WriteAllText($chain[0],($record|ConvertTo-Json -Depth 8))
 $previous=Join-Path $record.backup 'previous';$savedPrevious=Join-Path $record.backup 'previous-local'
 Move-Item -LiteralPath $previous -Destination $savedPrevious
 try{
  New-Item -ItemType Junction -Path $previous -Target $savedPrevious|Out-Null
  $refused=$false;try{Test-RestoreChain $game $chain -MetadataOnly}catch{if($_.Exception.Message -like 'Linked destinations are not supported:*'){$refused=$true}else{throw}}
  Assert $refused 'Metadata preview still rejects a linked required backup directory'
 }finally{
  if(Test-Path -LiteralPath $previous){[IO.Directory]::Delete($previous)}
  Move-Item -LiteralPath $savedPrevious -Destination $previous
 }
 $hardSource=Join-Path $game 'hard-source.bin';$hardAlias=Join-Path $game 'hard-alias.bin';$copyTarget=Join-Path $game 'refused-copy.bin'
 [IO.File]::WriteAllText($hardSource,'owned bytes');New-Item -ItemType HardLink -Path $hardAlias -Target $hardSource|Out-Null
 $refused=$false;try{CopyVerified $hardSource $copyTarget (HashFile $hardSource)}catch{if($_.Exception.Message -like '*Linked or unexpected target refused*'){$refused=$true}else{throw}}
 Assert ($refused -and -not (Test-Path -LiteralPath $copyTarget)) 'Verified preimage/restore copy still refuses a hardlinked source'
}finally{
 $resolved=[IO.Path]::GetFullPath($work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub-refinement/installer-history-fixture'))){throw 'Unexpected cleanup root'}
 # This fixture creates no links. Inspect entries before recursive cleanup.
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Force -Recurse){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
