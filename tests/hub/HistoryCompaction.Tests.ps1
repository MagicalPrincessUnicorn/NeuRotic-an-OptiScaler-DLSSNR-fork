. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub-recovery/history-compaction-fixture'
$script:Support=Join-Path $script:Work 'app/support'
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-recovery/history-compaction-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force;Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
function Write-Json($path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 16))}
function Compact-Plan($game,$keep=2){HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='CompactHistory';keepRestoreRecords=$keep})}
function New-HistoryFixture($name,[switch]$ProxySwitch){
 $game=New-GameFixture $name;$exe=Join-Path $game 'FixtureGame.exe';$chain=@();$history=@();$manager=Join-Path $game 'NeuRotic/Installer';New-Item -ItemType Directory -Path $manager -Force|Out-Null
 foreach($i in 0..5){
  $backup=Join-Path $game ('NeuRotic-backups/install-fixture'+$i);New-Item -ItemType Directory -Path (Join-Path $backup 'previous') -Force|Out-Null
  $targets=@('dxgi.dll','OptiScaler.ini');if($ProxySwitch -and $i -ge 3){$targets=@('winmm.dll','OptiScaler.ini');if($i -eq 3){$targets+='dxgi.dll'}}
  $files=@(foreach($target in $targets){
   $before=if($i -eq 0){'original '+$target}else{'version '+($i-1)+' '+$target};$after='version '+$i+' '+$target
   if($ProxySwitch -and $i -eq 3 -and $target -eq 'dxgi.dll'){$after='original dxgi.dll'}
   $exists=-not($ProxySwitch -and $i -eq 3 -and $target -eq 'winmm.dll')
   $previous=Join-Path $backup ('previous/'+$target);if($exists){[IO.File]::WriteAllText($previous,$before)};$live=Join-Path $game $target;[IO.File]::WriteAllText($live,$after)
   [pscustomobject]@{path=$target;existed=$exists;previous_hash=$(if($exists){(Get-FileHash $previous).Hash}else{$null});installed_hash=(Get-FileHash $live).Hash;operation='copy-package'}
  })
  $path=Join-Path $backup 'INSTALL-MANIFEST.json'
  Write-Json $path ([ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;selected_proxy=$(if($ProxySwitch -and $i -ge 3){'winmm.dll'}else{'dxgi.dll'});files=$files;previous_record=$(if($chain.Count){$chain[-1]}else{$null});owned_files=@();restored_utc=$null;reconciled_paths=@();completed_utc='2026-10-04T12:00:00Z'})
  $chain+=$path;$history+=[pscustomobject]@{record=$path;install_id=[string]$i}
 }
 Write-Json (Join-Path $manager 'Current-Install.json') ([ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';product='NeuRotic';release_name='History fixture';commit=('a'*40);last_install_utc='2026-10-04T12:00:00Z';game_directory=$game;game_executable=$exe;selected_proxy='dxgi.dll';restore_chain=$chain;install_history=$history;manager_files=@();uninstalled_utc=$null;uninstall_mode=$null;first_install_utc='2026-10-01T12:00:00Z'})
 if($ProxySwitch){$p=Join-Path $manager 'Current-Install.json';$s=Get-Content -Raw $p|ConvertFrom-Json;$s.selected_proxy='winmm.dll';Write-Json $p $s}
 return $game
}
Init-HubFixtures
try{
 New-Item -ItemType Directory -Path $script:Support -Force|Out-Null
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)}
 $game=New-HistoryFixture 'ordinary';$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$before=Get-Content -Raw $statePath|ConvertFrom-Json
 $unknown=Join-Path (Split-Path $before.restore_chain[0]) 'user-note.txt';[IO.File]::WriteAllText($unknown,'retain unknown')
 $unused=Join-Path (Split-Path $before.restore_chain[1]) 'Restore.cmd';[IO.File]::WriteAllText($unused,'original owned script');$r=Get-Content -Raw $before.restore_chain[1]|ConvertFrom-Json;$r.owned_files=@([pscustomobject]@{path='Restore.cmd';sha256=(Get-FileHash $unused).Hash});Write-Json $before.restore_chain[1] $r;[IO.File]::AppendAllText($unused,' user change')
 $ini=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($ini,"[User]`r`nKeep=latest settings`r`n");$liveIni=(Get-FileHash $ini).Hash;$liveProxy=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash
 Assert (-not((Get-Content -Raw $before.restore_chain[0]|ConvertFrom-Json).PSObject.Properties.Name -contains 'created_directories')) 'Legacy compaction fixture omits optional created-directory metadata'
 $plan=Compact-Plan $game
 Assert ($plan.status -eq 'Planned') 'History compaction produces reviewed plan'
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded') 'History compaction completes'
 Assert ($result.history.preservedOwnedFiles -eq 1 -and $result.history.retainedBackupFolders -eq 2) 'Result reports changed owned files and folders retained for unknown files'
 $after=Get-Content -Raw $statePath|ConvertFrom-Json
 Assert ($after.restore_record_ledger.Count -eq $after.restore_chain.Count) 'Compaction atomically publishes a ledger for its new chain'
 foreach($entry in $after.restore_record_ledger){Assert ($after.restore_chain -contains $entry.path -and $entry.sha256 -eq (Get-FileHash $entry.path).Hash) 'Compacted ledger contains exact checkpoint and rewritten boundary bytes'}
 Assert ($after.restore_chain.Count -eq 3 -and $after.restore_chain[-1] -eq $before.restore_chain[-1]) 'Original baseline checkpoint plus two newest transactions retained'
 $boundary=Get-Content -Raw $after.restore_chain[1]|ConvertFrom-Json;Assert ($boundary.previous_record -eq $after.restore_chain[0]) 'Legacy previous-record chain points to checkpoint'
 Assert ((Get-FileHash $ini).Hash -eq $liveIni -and (Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $liveProxy) 'Compaction does not change live settings or game payloads'
 Assert ((Test-Path $unknown) -and (Test-Path $unused)) 'Unknown and altered backup files are retained'
 Assert (-not (Test-Path $before.restore_chain[0])) 'Unchanged retired manifest removed after publication'
 Remove-Item -LiteralPath $after.restore_chain[0] -Force
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Uninstall';uninstallMode='Full'}))
 Assert ($result.status -eq 'Succeeded') 'Compacted history uninstalls successfully'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'original dxgi.dll' -and [IO.File]::ReadAllText($ini) -eq 'original OptiScaler.ini') 'Uninstall restores exact original baseline after compaction'
 $game=New-HistoryFixture 'stale';$plan=Compact-Plan $game;$path=Join-Path $game 'NeuRotic/Installer/Current-Install.json';[IO.File]::AppendAllText($path,' ');$result=Execute-Plan $plan;Assert ($result.status -eq 'PreconditionChanged') 'Stale compaction plan cannot mutate history'
 $game=New-HistoryFixture 'reconciled';$s=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json;$r=Get-Content -Raw $s.restore_chain[2]|ConvertFrom-Json;$r.reconciled_paths=@('dxgi.dll');Write-Json $s.restore_chain[2] $r;$result=Execute-Plan (Compact-Plan $game);Assert ($result.status -eq 'Succeeded') 'Reconciled histories compact while retaining the effective original baseline'
 $game=New-HistoryFixture 'proxy-change' -ProxySwitch;$result=Execute-Plan (Compact-Plan $game 1);Assert ($result.status -eq 'Succeeded') 'Compaction supports a normal proxy-change boundary'
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Uninstall';uninstallMode='Full'}));Assert ($result.status -eq 'Succeeded' -and -not(Test-Path (Join-Path $game 'winmm.dll')) -and [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'original dxgi.dll') 'Proxy-change history returns original proxy and removes newly introduced proxy'
 foreach($stage in @('before-stage','during-stage','before-publish','after-boundary','after-publish','during-cleanup')){
  $game=New-HistoryFixture $stage;$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$before=Get-Content -Raw $statePath|ConvertFrom-Json;$iniHash=(Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash
  $plan=Compact-Plan $game;$env:NEUROTIC_HISTORY_TEST_STOP=$stage
  try{$result=Execute-Plan $plan}finally{Remove-Item Env:NEUROTIC_HISTORY_TEST_STOP}
  Assert ($result.status -eq 'RecoveryRequired') ($stage+' interruption leaves recovery journal')
  if($stage -eq 'before-stage'){
   $journalPath=Join-Path $game 'NeuRotic/Installer/History-Transaction.json';$journalBytes=[IO.File]::ReadAllBytes($journalPath);$j=Get-Content -Raw $journalPath|ConvertFrom-Json
   $j.staged[0].path=$j.checkpoint+'\private.txt';Write-Json $journalPath $j
   $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}))
   Assert ($result.status -eq 'RecoveryRequired' -and $result.reason -like '*Unowned history staging target*') 'Forged journal cannot nominate unowned staged files'
   [IO.File]::WriteAllBytes($journalPath,$journalBytes)
  }
  $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}));Assert ($result.status -eq 'Succeeded') ($stage+' recovery succeeds')
  $after=Get-Content -Raw $statePath|ConvertFrom-Json;$expected=if($stage -in @('before-stage','during-stage','before-publish','after-boundary')){6}else{3}
  Assert ($after.restore_chain.Count -eq $expected -and (Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $iniHash) ($stage+' preserves live bytes and the correct committed history')
 }
 $game=New-HistoryFixture 'active-checkpoint';$result=Execute-Plan (Compact-Plan $game 2);Assert ($result.status -eq 'Succeeded') 'Existing checkpoint fixture prepared'
 $plan=Compact-Plan $game 1;$env:NEUROTIC_HISTORY_TEST_STOP='before-stage';try{$result=Execute-Plan $plan}finally{Remove-Item Env:NEUROTIC_HISTORY_TEST_STOP}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$old=Get-Content -Raw $statePath|ConvertFrom-Json;$activeRecord=Get-Content -Raw $old.restore_chain[0]|ConvertFrom-Json;$activeBaseline=Join-Path $activeRecord.backup 'previous/dxgi.dll';$baselineHash=(Get-FileHash $activeBaseline).Hash
 $journalPath=Join-Path $game 'NeuRotic/Installer/History-Transaction.json';$journalBytes=[IO.File]::ReadAllBytes($journalPath);$j=Get-Content -Raw $journalPath|ConvertFrom-Json
 $j.checkpoint=$activeRecord.backup.Substring($game.Length+1);$new=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($j.state.after.bytes))|ConvertFrom-Json;$new.restore_chain[0]=$old.restore_chain[0];$bytes=[Text.Encoding]::UTF8.GetBytes(($new|ConvertTo-Json -Depth 20));$j.state.after.bytes=[Convert]::ToBase64String($bytes);$sha=[Security.Cryptography.SHA256]::Create();try{$j.state.after.sha256=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()};Write-Json $journalPath $j
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}));Assert ($result.status -eq 'RecoveryRequired' -and (Get-FileHash $activeBaseline).Hash -eq $baselineHash) 'Forged checkpoint overlap cannot delete the active original baseline'
 [IO.File]::WriteAllBytes($journalPath,$journalBytes);$result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}));Assert ($result.status -eq 'Succeeded') 'Valid original journal recovers after forged overlap refusal'
 # A deterministic external metadata edit between observation and publication
 # must never become the accepted CAS preimage for stale parsed metadata.
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1','NeuRotic-HubHistory.ps1')){
  $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Support $name),[ref]$tokens,[ref]$errors)
  foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 }
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll');$HubNonInteractive=$true
 $game=New-HistoryFixture 'boundary-race';$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$stateHash=HashFile $statePath;$state=Get-Content -Raw $statePath|ConvertFrom-Json;$script:racePath=$state.restore_chain[4];$boundaryBytes=[IO.File]::ReadAllBytes($script:racePath)
 function Invoke-HubHistoryBoundary([string]$Stage){if($Stage -eq 'before-publish'){[IO.File]::AppendAllText($script:racePath,' ')}}
 $refused=$false;try{Invoke-HubHistoryCompact ([pscustomobject]@{directory=$game;executable=(Join-Path $game 'FixtureGame.exe')}) 2|Out-Null}catch{$refused=$_.Exception.Message -like '*changed before history publication*'}
 Assert ($refused -and (HashFile $statePath) -eq $stateHash -and [IO.File]::ReadAllBytes($script:racePath).Length -eq $boundaryBytes.Length+1) 'External boundary metadata change is preserved and publication is refused'
 [IO.File]::WriteAllBytes($script:racePath,$boundaryBytes);[void](Complete-HubHistory $game)
}finally{End-HubFixtures}
