param([switch]$Baseline)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/hub/inspection-hash-fixture'
if(Test-Path -LiteralPath $work){throw 'Inspect the existing fixture before reuse'}
New-Item -ItemType Directory -Path $work | Out-Null
try {
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1','NeuRotic-HubHistory.ps1','NeuRotic-HubSettings.ps1')) {
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo ('tools/packaging/customer/support/'+$name)),[ref]$tokens,[ref]$errors)
  if($errors.Count){throw ($errors|Out-String)}
  foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 }
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
 $HubNonInteractive=$true
 function Get-HubSettingsProjection([string]$Root){return @()}
 function Assert($ok,$message){if(-not $ok){throw $message};Write-Output "PASS $message"}
 $exe=Join-Path $work 'FixtureGame.exe';[IO.File]::WriteAllText($exe,'inert fixture')
 $proxy=Join-Path $work 'dxgi.dll';[IO.File]::WriteAllBytes($proxy,(New-Object byte[] 1048576))
 $ini=Join-Path $work 'OptiScaler.ini';[IO.File]::WriteAllText($ini,"[Menu]`nLanguage=en`n")
 $chain=@()
 foreach($i in 1..8){
  $backup=Join-Path $work ('NeuRotic-backups/active-'+$i)
  New-Item -ItemType Directory -Path (Join-Path $backup 'previous') -Force|Out-Null
  $files=@(foreach($name in @('dxgi.dll','OptiScaler.ini')){
   $live=Join-Path $work $name;$previous=Join-Path $backup ('previous/'+$name)
   Copy-Item -LiteralPath $live -Destination $previous
   [pscustomobject]@{path=$name;existed=$true;previous_hash=(HashFile $previous);installed_hash=(HashFile $live)}
  })
  $record=Join-Path $backup 'INSTALL-MANIFEST.json'
  [IO.File]::WriteAllText($record,(@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;files=$files}|ConvertTo-Json -Depth 8));$chain+=$record
 }
 $state=[pscustomobject]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory=$work;restore_chain=$chain}
 $statePath=Join-Path $work 'NeuRotic/Installer/Current-Install.json';New-Item -ItemType Directory -Path (Split-Path -Parent $statePath) -Force|Out-Null
 [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8))
 $script:reads=@{}
 function HashFile([string]$Path){$key=[IO.Path]::GetFullPath($Path);$script:reads[$key]++;(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
 $target=[pscustomobject]@{executable=$exe;directory=$work}
 $timer=[Diagnostics.Stopwatch]::StartNew();$first=Get-HubInspection $target;$timer.Stop()
 Write-Output "Measured proxy reads=$($script:reads[$proxy]); inspection ms=$($timer.Elapsed.TotalMilliseconds)"
 if($Baseline){return}
 Assert ($script:reads[$proxy] -eq 1) 'One inspection reads each current managed payload once'
 Assert ($first.conflicts.Count -eq 0 -and $first.missing.Count -eq 0) 'Existing verified chain remains valid'
 $script:reads=@{};$timer.Restart();$quick=Get-HubInspection $target -Quick;$timer.Stop()
 Assert (-not $script:reads.ContainsKey($proxy) -and -not $script:reads.ContainsKey($exe)) 'Game selection reads no executable or managed payload bytes'
 Assert ($quick.verification -eq 'NotChecked' -and $quick.state.status -eq 'installed-verified') 'Quick status distinguishes recorded installation from byte verification'
 Write-Output "Measured quick inspection ms=$($timer.Elapsed.TotalMilliseconds); payload reads=0"
 [IO.File]::AppendAllText($proxy,'modified between operations')
 $second=Get-HubInspection $target
 Assert ($second.revision -ne $first.revision -and $second.conflicts -contains 'dxgi.dll') 'Separate inspection rehashes changed bytes and changes revision'
 Remove-Item -LiteralPath $proxy
 $reverse=@($chain);[array]::Reverse($reverse)
 $gone=@(Test-RestoreChain $work $reverse -MetadataOnly -ReportLiveFiles -LiveHashes @{'dxgi.dll'=($first.inventory|Where-Object path -eq 'dxgi.dll').sha256})
 Assert (-not ($gone|Where-Object path -eq 'dxgi.dll').existed) 'A file removed after inventory is reported absent, not restored from cached observation'
 $refused=$false;try{Test-RestoreChain $work $reverse -LiveHashes @{'dxgi.dll'=('0'*64)}|Out-Null}catch{$refused=$true}
 Assert $refused 'Full restoration refuses metadata-only hash reuse'
 $missing=Get-HubInspection $target
 Assert ($missing.missing -contains 'dxgi.dll') 'Missing current managed file remains visible'
 $originalRecord=[IO.File]::ReadAllBytes($chain[0]);$unknownRecord=Get-Content -Raw -LiteralPath $chain[0]|ConvertFrom-Json
 $unknownRecord.files[0]|Add-Member -NotePropertyName original_unknown -NotePropertyValue $true
 [IO.File]::WriteAllText($chain[0],($unknownRecord|ConvertTo-Json -Depth 8))
 $refused=$false;try{Get-HubHistoryRecords $target ([pscustomobject]@{status='installed-verified';restore_chain=@($chain[0])})|Out-Null}catch{$refused=$_.Exception.Message -like '*Original recovery metadata is incomplete*'}
 Assert $refused 'Compaction cannot turn an unknown original baseline into a known checkpoint'
 [IO.File]::WriteAllBytes($chain[0],$originalRecord)
 $record=Get-Content -Raw -LiteralPath $chain[0]|ConvertFrom-Json;$record.files[0].path='../outside.dll';[IO.File]::WriteAllText($chain[0],($record|ConvertTo-Json -Depth 8))
 $refused=$false;try{Get-HubInspection $target|Out-Null}catch{$refused=$true}
 Assert $refused 'Invalid older history target still refuses inspection'
 $state.status='uninstalled';[IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8))
 $inactive=Get-HubInspection $target
 Assert ($inactive.state.status -eq 'uninstalled' -and -not $inactive.conflicts.Count) 'Reinstall inspection ignores retired or damaged inactive history'
} finally {
 $resolved=[IO.Path]::GetFullPath($work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/inspection-hash-fixture'))){throw 'Unexpected cleanup root'}
 $stack=[Collections.Generic.Stack[string]]::new();$stack.Push($resolved)
 while($stack.Count){foreach($item in Get-ChildItem -LiteralPath $stack.Pop() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};if($item.PSIsContainer){$stack.Push($item.FullName)}}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
