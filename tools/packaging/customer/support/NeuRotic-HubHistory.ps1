# History maintenance belongs to the installer owner. No game payload is changed.
if(-not (Get-Command Get-HubProperty -ErrorAction SilentlyContinue)){
 function Get-HubProperty($Object,[string]$Name,$Default=$null){if($Object.PSObject.Properties.Name -contains $Name){return $Object.$Name};return $Default}
}
function Get-HubHistoryRecords($Target,$State){
 if(-not $State -or $State.status -ne 'installed-verified'){throw 'No active installation history to compact.'}
 Initialize-RestoreRecordLedger $Target.directory $State
 $records=New-Object Collections.Generic.List[object];$previous=$null;$seen=@{}
 foreach($path in @($State.restore_chain)){
  $full=Assert-PathUnderRoot $Target.directory ([string]$path)
  if($full -ine $path -or $full.Substring($Target.directory.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$' -or $seen.ContainsKey($full)){throw 'History contains an invalid or repeated restore record; keep the backups.'}
  $seen[$full]=$true;$bytes=Get-InstallRecordBytes $full;$r=[Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json
  if($r.kind -ne 'neurotic-customer-candidate-install' -or $r.files.Count -lt 2){throw 'Unrecognized installation history record.'}
  if($r.status -ne 'installed-verified' -or (Split-Path $r.game_executable) -ine $Target.directory -or $r.backup -ine (Split-Path $full)){throw 'History record identity is invalid; keep the backups.'}
  if((Get-HubProperty $r 'previous_record') -ine $previous){throw 'History has an unsupported previous-record link; keep the backups.'}
  $names=@{}
  foreach($f in $r.files){
   Assert-RestoreTarget $f.path $full $names;[void](SafePath $Target.directory $f.path)
   if([bool](Get-HubProperty $f 'original_unknown' $false) -or [bool](Get-HubProperty $f 'ownership_unproven' $false)){throw 'Original recovery metadata is incomplete. Keep these records; installation and uninstall remain available.'}
   if($f.existed -isnot [bool] -or ($f.existed -and $f.previous_hash -notmatch '^[a-fA-F0-9]{64}$')){throw 'History baseline metadata is invalid; keep the backups.'}
   if(($f.PSObject.Properties.Name -contains 'restore_exists') -and $f.restore_exists -isnot [bool]){throw 'History contains invalid effective-baseline metadata.'}
   $exists=Get-HubProperty $f 'installed_exists' $true
   if($exists -isnot [bool] -or ($exists -and $f.installed_hash -notmatch '^[a-fA-F0-9]{64}$')){throw 'History installed-file metadata is invalid; keep the backups.'}
  }
  $records.Add([pscustomobject]@{path=$full;record=$r;bytes=$bytes;sha256=(HashBytes $bytes)});$previous=$full
 }
 return $records.ToArray()
}
function Get-HubHistorySummary($Target,$State,[bool]$Recovering){
 $count=if($State){@(Get-HubProperty $State 'restore_chain' @()).Count}else{0}
 $result=[ordered]@{recordCount=$count;canCompact=$false;defaultKeep=3;reason=$null;automaticKeep=3;baselineSeparate=$true;automaticStatus=$(if($count -le 4){'within-limit'}else{'pending'})}
 $retention=if($State){Get-HubProperty $State 'history_retention'}else{$null}
 if(-not $retention){$retention=[ordered]@{keepRecent=3;baselineSeparate=$true;status=$(if($count -le 4){'Complete'}else{'Deferred'});reason=$(if($count -gt 4){'Older history will be compacted after the next successful install or update.'}else{$null});recordCount=$count}}
 $result.retention=[ordered]@{keepRecent=3;baselineSeparate=$true;status=(Get-HubProperty $retention 'status' 'Deferred');reason=(Get-HubProperty $retention 'reason');recordCount=$count}
 if($Recovering){$result.reason='Finish pending recovery before managing history.';$result.retention.status='RecoveryRequired';$result.retention.reason=$result.reason;return $result}
 if($count -le 4){$result.reason='The three newest restore records and original baseline are already within the retention limit.';return $result}
 if(-not $State -or $State.status -ne 'installed-verified'){$result.reason='No active installation history to compact.';return $result}
 # This admits a review request only. Plan and Execute validate every record;
 # ordinary game inspection must not traverse all history a second time.
 $result.canCompact=$true;$result.reason='Review verifies whether these records can be safely combined.'
 return $result
}
function Get-HubHistoryPlan($Target,$State,[int]$Keep){
 if($Keep -lt 1 -or $Keep -gt 20){throw 'Keep between 1 and 20 recent restore records.'}
 $records=@(Get-HubHistoryRecords $Target $State)
 if($records.Count -le $Keep+1){throw 'No older history can be removed with this retention setting. The original recovery baseline is always retained.'}
 return [ordered]@{beforeCount=$records.Count;keepRestoreRecords=$Keep;afterCount=($Keep+1);retiredRecords=($records.Count-$Keep);removedRecords=($records.Count-$Keep-1)}
}
function ConvertTo-HubHistoryBytes($Value){return ,[Text.Encoding]::UTF8.GetBytes(($Value|ConvertTo-Json -Depth 20))}
function Get-HubHistoryLedger([string[]]$Chain,[hashtable]$Overrides){
 return @(foreach($path in $Chain){
  $bytes=if($Overrides.ContainsKey($path)){$Overrides[$path]}else{Get-InstallRecordBytes $path}
  [pscustomobject]@{path=$path;sha256=(HashBytes $bytes);record_json=[Text.Encoding]::UTF8.GetString($bytes)}
 })
}
function Write-HubHistoryBytes([string]$Path,[byte[]]$Bytes,[string]$Expected){
 if(-not (WriteVerifiedBytes $Bytes $Path (HashBytes $Bytes) $Expected)){throw "History changed at the write boundary; preserved: $Path"}
}
function Invoke-HubHistoryBoundary([string]$Stage){
 # Failure injection exists only in the isolated fixture environment.
 if($env:NEUROTIC_HUB_FIXTURE_ROOT -and $env:NEUROTIC_HISTORY_TEST_STOP -ceq $Stage){throw ('History fixture interruption: '+$Stage)}
}
function Read-HubHistoryJournal([string]$Root){
 $path=SafePath $Root 'NeuRotic/Installer/History-Transaction.json';$j=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
 if($j.kind -cne 'neurotic-history-transaction' -or $j.game_directory -ine $Root -or $j.checkpoint -notmatch '^NeuRotic-backups\\history-[a-f0-9]{32}$'){throw 'Invalid history recovery journal; preserved.'}
 foreach($name in @('state','boundary')){
  $part=$j.$name
  if(($name -eq 'state' -and $part.path -cne 'NeuRotic\Installer\Current-Install.json') -or ($name -eq 'boundary' -and $part.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$')){throw 'Invalid history recovery target.'}
  [void](SafePath $Root $part.path)
  foreach($side in @('before','after')){if($part.$side.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or (HashBytes ([Convert]::FromBase64String($part.$side.bytes))) -ne $part.$side.sha256){throw 'Invalid history metadata snapshot.'}}
 }
 $oldState=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($j.state.before.bytes))|ConvertFrom-Json
 $newState=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($j.state.after.bytes))|ConvertFrom-Json
 if($oldState.game_directory -ine $Root -or $newState.game_directory -ine $Root -or $oldState.kind -ne 'neurotic-public-install' -or $newState.kind -ne 'neurotic-public-install' -or $newState.restore_chain[0] -ine (SafePath $Root ($j.checkpoint+'\INSTALL-MANIFEST.json'))){throw 'History state snapshots do not match this game.'}
 $oldChain=@($oldState.restore_chain);$newChain=@($newState.restore_chain);$keep=$newChain.Count-1;$prefixCount=$oldChain.Count-$keep
 if($keep -lt 1 -or $keep -gt 20 -or $prefixCount -lt 2 -or $oldChain -contains $newChain[0]){throw 'Invalid history checkpoint transition.'}
 $seenRecords=@{}
 foreach($recordPath in $oldChain){$full=Assert-PathUnderRoot $Root $recordPath;if($full -ine $recordPath -or $full.Substring($Root.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$' -or $seenRecords.ContainsKey($full)){throw 'Invalid original history chain.'};$seenRecords[$full]=$true}
 for($i=0;$i -lt $keep;$i++){if($newChain[$i+1] -ine $oldChain[$prefixCount+$i]){throw 'History transition does not preserve the recent suffix.'}}
 if((SafePath $Root $j.boundary.path) -ine $newChain[1]){throw 'History boundary does not match the retained suffix.'}
 foreach($oldPath in $oldChain){if((Split-Path $oldPath) -ieq (SafePath $Root $j.checkpoint)){throw 'History checkpoint overlaps existing backups.'}}
 $expectedState=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($j.state.before.bytes))|ConvertFrom-Json
 $expectedState.restore_chain=$newChain;$expectedState.install_history=@(Get-HubProperty $expectedState 'install_history' @()|Where-Object{$newChain -contains $_.record})
 if((Get-HubProperty $j 'ledgerVersion' 0) -eq 1){
  $ledgerOverrides=@{};$ledgerOverrides[$newChain[0]]=[Convert]::FromBase64String($j.checkpoint_manifest);$ledgerOverrides[$newChain[1]]=[Convert]::FromBase64String($j.boundary.after.bytes)
  $expectedState|Add-Member -NotePropertyName restore_record_ledger -NotePropertyValue @(Get-HubHistoryLedger $newChain $ledgerOverrides) -Force
 }
 if((HashBytes (ConvertTo-HubHistoryBytes $expectedState)) -ne $j.state.after.sha256){throw 'History transaction changes unrelated installation metadata.'}
 $boundaryBefore=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($j.boundary.before.bytes))|ConvertFrom-Json
 if($boundaryBefore.previous_record -ine $oldChain[$prefixCount-1]){throw 'Original history boundary has an unexpected predecessor.'}
 $boundaryBefore.previous_record=$newChain[0]
 if((HashBytes (ConvertTo-HubHistoryBytes $boundaryBefore)) -ne $j.boundary.after.sha256){throw 'History boundary changes unrelated metadata.'}
 # Derive deletion and staging authority from exact pre-publication records.
 # A journal cannot simply nominate any file under an old backup directory.
 if(@($j.records).Count -ne $prefixCount){throw 'History transaction is missing its source records.'}
 $expectedCleanup=@{};$expectedStaged=@{};$expectedDirectories=@();$first=@{};$latest=@{};$previous=$null
 for($i=0;$i -lt $prefixCount;$i++){
  $snapshot=$j.records[$i];$bytes=[Convert]::FromBase64String($snapshot.bytes)
  if($snapshot.path -ine $oldChain[$i] -or (HashBytes $bytes) -ne $snapshot.sha256){throw 'Invalid history source snapshot.'}
  $r=[Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json
  if($r.kind -ne 'neurotic-customer-candidate-install' -or $r.status -ne 'installed-verified' -or $r.backup -ine (Split-Path $snapshot.path) -or (Split-Path $r.game_executable) -ine $Root -or $r.previous_record -ine $previous){throw 'Invalid history source identity.'}
  $previous=$snapshot.path;$names=@{};$owned=@([pscustomobject]@{path='INSTALL-MANIFEST.json';sha256=$snapshot.sha256})
  foreach($directory in @(Get-HubProperty $r 'created_directories' @())){[void](SafePath $Root $directory);$expectedDirectories+=$directory}
  foreach($f in $r.files){Assert-RestoreTarget $f.path $snapshot.path $names;[void](SafePath $Root $f.path);if(-not $first.ContainsKey($f.path)){$first[$f.path]=$f};$latest[$f.path]=$f;if($f.existed){$owned+=[pscustomobject]@{path=('previous\'+$f.path);sha256=$f.previous_hash}}}
  foreach($f in @(Get-HubProperty $r 'owned_files' @())){$owned+=$f}
  foreach($f in $owned){$relative=(SafePath $r.backup $f.path).Substring($Root.Length+1);if($expectedCleanup.ContainsKey($relative) -and $expectedCleanup[$relative] -ne $f.sha256){throw 'Conflicting cleanup ownership.'};$expectedCleanup[$relative]=$f.sha256}
 }
 $checkpointBytes=[Convert]::FromBase64String($j.checkpoint_manifest);$checkpoint=[Text.Encoding]::UTF8.GetString($checkpointBytes)|ConvertFrom-Json
 if($checkpoint.kind -ne 'neurotic-customer-candidate-install' -or $checkpoint.status -ne 'installed-verified' -or $checkpoint.backup -ine (SafePath $Root $j.checkpoint) -or (Split-Path $checkpoint.game_executable) -ine $Root -or $checkpoint.previous_record -or @($checkpoint.owned_files).Count){throw 'Invalid checkpoint snapshot.'}
 $expectedFiles=@(foreach($name in @($first.Keys|Sort-Object)){$f=$first[$name];$end=$latest[$name];$exists=$f.existed -and [bool](Get-HubProperty $f 'restore_exists' $true);$hash=$(if($exists){$f.previous_hash}else{$null});if($exists){$expectedStaged[$j.checkpoint+'\previous\'+$name]=$hash};[pscustomobject]@{path=$name;operation='history-checkpoint';existed=[bool]$exists;previous_hash=$hash;installed_exists=[bool](Get-HubProperty $end 'installed_exists' $true);installed_hash=$end.installed_hash}})
 if((HashBytes (ConvertTo-HubHistoryBytes $expectedFiles)) -ne (HashBytes (ConvertTo-HubHistoryBytes @($checkpoint.files)))){throw 'Checkpoint does not preserve the original baseline and ownership.'}
 # Wrap the arrays so legacy records with no created_directories serialize as
 # an explicit empty array rather than disappearing in PowerShell's pipeline.
 if((HashBytes (ConvertTo-HubHistoryBytes ([ordered]@{directories=@($expectedDirectories|Sort-Object -Unique)}))) -ne (HashBytes (ConvertTo-HubHistoryBytes ([ordered]@{directories=@($checkpoint.created_directories)})))){throw 'Checkpoint does not preserve created-directory ownership.'}
 $expectedStaged[$j.checkpoint+'\INSTALL-MANIFEST.json']=HashBytes $checkpointBytes
 $retired=@{};foreach($record in $oldState.restore_chain){if($newState.restore_chain -notcontains $record){$p=Assert-PathUnderRoot $Root $record;$retired[(Split-Path $p).Substring($Root.Length+1)]=$true}}
 $seen=@{}
 foreach($f in @($j.cleanup)+@($j.staged)){
  [void](SafePath $Root $f.path)
  if($f.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $seen.ContainsKey($f.path)){throw 'Invalid history cleanup entry.'};$seen[$f.path]=$true
 }
 foreach($f in $j.cleanup){$parts=$f.path -split '\\',3;if($parts.Count -ne 3 -or -not $retired.ContainsKey($parts[0]+'\'+$parts[1])){throw 'History cleanup leaves the retired records.'}}
 foreach($f in $j.staged){if(-not $f.path.StartsWith($j.checkpoint+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'History staging cleanup leaves the checkpoint.'}}
 if(@($j.cleanup).Count -ne $expectedCleanup.Count -or @($j.staged).Count -ne $expectedStaged.Count){throw 'History cleanup recipe differs from recorded ownership.'}
 foreach($f in $j.cleanup){if(-not $expectedCleanup.ContainsKey($f.path) -or $expectedCleanup[$f.path] -ne $f.sha256){throw 'Unowned history cleanup target.'}}
 foreach($f in $j.staged){if(-not $expectedStaged.ContainsKey($f.path) -or $expectedStaged[$f.path] -ne $f.sha256){throw 'Unowned history staging target.'}}
 return $j
}
function Complete-HubHistory([string]$Root){
 $journalPath=SafePath $Root 'NeuRotic/Installer/History-Transaction.json';$journalHash=HashFile $journalPath;$j=Read-HubHistoryJournal $Root
 $statePath=SafePath $Root $j.state.path;$boundaryPath=SafePath $Root $j.boundary.path;$stateHash=HashFile $statePath;$boundaryHash=HashFile $boundaryPath
 $outcome=[ordered]@{published=($stateHash -eq $j.state.after.sha256);preservedOwnedFiles=0;retainedBackupFolders=0}
 if($stateHash -eq $j.state.after.sha256){
  if($boundaryHash -ne $j.boundary.after.sha256){throw 'History boundary changed after publication; recovery preserved all remaining files.'}
  foreach($f in $j.staged){if((HashFile (SafePath $Root $f.path)) -ne $f.sha256){throw 'Published history checkpoint changed; cleanup refused.'}}
  Invoke-HubHistoryBoundary 'after-publish'
  $folders=@{}
  foreach($f in $j.cleanup){
   Remove-OwnedFile $Root $f.path $f.sha256
   if(Test-Path -LiteralPath (SafePath $Root $f.path)){$outcome.preservedOwnedFiles++}
   $parts=$f.path -split '\\',3;$folders[$parts[0]+'\'+$parts[1]]=$true
   Invoke-HubHistoryBoundary 'during-cleanup'
  }
  foreach($folder in $folders.Keys){if(Test-Path -LiteralPath (SafePath $Root $folder)){$outcome.retainedBackupFolders++}}
 }elseif($stateHash -eq $j.state.before.sha256){
  if($boundaryHash -eq $j.boundary.after.sha256){Write-HubHistoryBytes $boundaryPath ([Convert]::FromBase64String($j.boundary.before.bytes)) $boundaryHash}
  elseif($boundaryHash -ne $j.boundary.before.sha256){throw 'History boundary changed before publication; recovery preserved all files.'}
  foreach($f in $j.staged){Remove-OwnedFile $Root $f.path $f.sha256}
 }else{throw 'Installation state changed during history recovery; all remaining backups were preserved.'}
 Remove-NeuroticPath -LiteralPath $journalPath -Force -ExpectedHash $journalHash
 return $outcome
}
function Invoke-HubHistoryCompact($Target,[int]$Keep){
 $root=$Target.directory;$statePath=SafePath $root 'NeuRotic/Installer/Current-Install.json';$oldStateBytes=[IO.File]::ReadAllBytes($statePath);$oldStateHash=HashBytes $oldStateBytes;$state=[Text.Encoding]::UTF8.GetString($oldStateBytes)|ConvertFrom-Json
 $plan=Get-HubHistoryPlan $Target $state $Keep;$records=@(Get-HubHistoryRecords $Target $state);$reverse=@($state.restore_chain);[array]::Reverse($reverse)
 [void](Test-RestoreChain $root $reverse -MetadataOnly) # CopyVerified verifies only consumed original preimages below.
 $prefix=@($records|Select-Object -First ($records.Count-$Keep));$suffix=@($records|Select-Object -Last $Keep)
 $first=@{};$latest=@{};$sources=@{};$cleanup=New-Object Collections.Generic.List[object];$cleanupNames=@{}
 foreach($entry in $prefix){
  $r=$entry.record
  foreach($f in $r.files){if(-not $first.ContainsKey($f.path)){$first[$f.path]=$f;$sources[$f.path]=SafePath (Join-Path $r.backup 'previous') $f.path};$latest[$f.path]=$f}
  $owned=@([pscustomobject]@{path='INSTALL-MANIFEST.json';sha256=$entry.sha256})
  foreach($f in $r.files){if($f.existed){$owned+= [pscustomobject]@{path=('previous\'+$f.path);sha256=$f.previous_hash}}}
  foreach($f in @(Get-HubProperty $r 'owned_files' @())){$owned+=$f}
  foreach($f in $owned){
   $path=SafePath $r.backup $f.path;$relative=$path.Substring($root.Length+1)
   if($f.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid owned backup hash; history preserved.'}
   if($cleanupNames.ContainsKey($relative)){if($cleanupNames[$relative] -ne $f.sha256){throw 'Conflicting owned backup entries; history preserved.'};continue}
   $cleanupNames[$relative]=$f.sha256;$cleanup.Add([pscustomobject]@{path=$relative;sha256=$f.sha256})
  }
 }
 $checkpointRelative='NeuRotic-backups\history-'+[Guid]::NewGuid().ToString('N');$checkpoint=SafePath $root $checkpointRelative;$checkpointPath=SafePath $checkpoint 'INSTALL-MANIFEST.json'
 $staged=New-Object Collections.Generic.List[object];$files=@();$copies=@()
 foreach($name in @($first.Keys|Sort-Object)){
  $baseline=$first[$name];$end=$latest[$name]
  $exists=$baseline.existed -and [bool](Get-HubProperty $baseline 'restore_exists' $true);$hash=$(if($exists){$baseline.previous_hash}else{$null})
  $files += [pscustomobject]@{path=$name;operation='history-checkpoint';existed=[bool]$exists;previous_hash=$hash;installed_exists=[bool](Get-HubProperty $end 'installed_exists' $true);installed_hash=$end.installed_hash}
  if($exists){$destination=SafePath $checkpoint ('previous\'+$name);$copies+=[pscustomobject]@{source=$sources[$name];destination=$destination;sha256=$hash};$staged.Add([pscustomobject]@{path=$destination.Substring($root.Length+1);sha256=$hash})}
 }
 $createdDirectories=@($prefix|ForEach-Object{Get-HubProperty $_.record 'created_directories' @()}|Sort-Object -Unique)
 foreach($directory in $createdDirectories){[void](SafePath $root $directory)}
 $record=[ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$Target.executable;backup=$checkpoint;selected_proxy=$prefix[-1].record.selected_proxy;files=$files;owned_files=@();previous_record=$null;reconciled_paths=@();restored_utc=$null;created_directories=$createdDirectories;history_checkpoint=$true;completed_utc=[DateTime]::UtcNow.ToString('o')}
 $checkpointBytes=ConvertTo-HubHistoryBytes $record;$staged.Add([pscustomobject]@{path=$checkpointPath.Substring($root.Length+1);sha256=(HashBytes $checkpointBytes)})
 $boundary=$suffix[0];$boundaryBefore=$boundary.bytes;$boundary.record.previous_record=$checkpointPath;$boundaryAfter=ConvertTo-HubHistoryBytes $boundary.record
 $state.restore_chain=@($checkpointPath)+@($suffix|ForEach-Object{$_.path});$state.install_history=@(Get-HubProperty $state 'install_history' @()|Where-Object{$state.restore_chain -contains $_.record})
 $ledgerOverrides=@{};$ledgerOverrides[$checkpointPath]=$checkpointBytes;$ledgerOverrides[$boundary.path]=$boundaryAfter
 $state|Add-Member -NotePropertyName restore_record_ledger -NotePropertyValue @(Get-HubHistoryLedger $state.restore_chain $ledgerOverrides) -Force
 $stateAfter=ConvertTo-HubHistoryBytes $state
 $journal=[ordered]@{kind='neurotic-history-transaction';game_directory=$root;checkpoint=$checkpointRelative;cleanup=$cleanup.ToArray();staged=$staged.ToArray();state=[ordered]@{path='NeuRotic\Installer\Current-Install.json';before=@{bytes=[Convert]::ToBase64String($oldStateBytes);sha256=$oldStateHash};after=@{bytes=[Convert]::ToBase64String($stateAfter);sha256=(HashBytes $stateAfter)}};boundary=[ordered]@{path=$boundary.path.Substring($root.Length+1);before=@{bytes=[Convert]::ToBase64String($boundaryBefore);sha256=(HashBytes $boundaryBefore)};after=@{bytes=[Convert]::ToBase64String($boundaryAfter);sha256=(HashBytes $boundaryAfter)}}}
 $journal.records=@($prefix|ForEach-Object{[pscustomobject]@{path=$_.path;bytes=[Convert]::ToBase64String($_.bytes);sha256=$_.sha256}});$journal.checkpoint_manifest=[Convert]::ToBase64String($checkpointBytes)
 $journal.ledgerVersion=1
 foreach($entry in $records){if((HashFile $entry.path) -ne $entry.sha256){throw 'Restore records changed during history planning; preserved.'}}
 $journalPath=SafePath $root 'NeuRotic/Installer/History-Transaction.json';Write-HubHistoryBytes $journalPath (ConvertTo-HubHistoryBytes $journal) 'Absent'
 Invoke-HubHistoryBoundary 'before-stage'
 foreach($copy in $copies){CopyVerified $copy.source $copy.destination $copy.sha256 'Absent';Invoke-HubHistoryBoundary 'during-stage'}
 Write-HubHistoryBytes $checkpointPath $checkpointBytes 'Absent'
 Invoke-HubHistoryBoundary 'before-publish'
 foreach($entry in $records){if((HashFile $entry.path) -ne $entry.sha256){throw 'Restore records changed before history publication; recovery required.'}}
 Write-HubHistoryBytes $boundary.path $boundaryAfter (HashBytes $boundaryBefore)
 Invoke-HubHistoryBoundary 'after-boundary'
 Write-HubHistoryBytes $statePath $stateAfter $oldStateHash
 $cleanupResult=Complete-HubHistory $root
 $plan.preservedOwnedFiles=$cleanupResult.preservedOwnedFiles;$plan.retainedBackupFolders=$cleanupResult.retainedBackupFolders
 return $plan
}

function Invoke-AutomaticHistoryRetention($Target){
 $root=$Target.directory;$statePath=SafePath $root 'NeuRotic/Installer/Current-Install.json'
 $result=[ordered]@{keepRecent=3;baselineSeparate=$true;status='Complete';reason=$null;recordCount=0}
 try{
  $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json;$result.recordCount=@($state.restore_chain).Count
  if($result.recordCount -gt 4){[void](Invoke-HubHistoryCompact $Target 3)}
 }catch{
  $result.reason=$_.Exception.Message
  $result.status=$(if(Test-Path -LiteralPath (SafePath $root 'NeuRotic/Installer/History-Transaction.json')){'RecoveryRequired'}else{'Deferred'})
 }
 if($result.status -ne 'RecoveryRequired'){
  try{
   $before=[IO.File]::ReadAllBytes($statePath);$state=[Text.Encoding]::UTF8.GetString($before)|ConvertFrom-Json
   if($state.kind -ne 'neurotic-public-install' -or $state.status -ne 'installed-verified' -or $state.game_directory -ine $root){throw 'The active installation changed before retention status could be recorded.'}
   $result.recordCount=@($state.restore_chain).Count
   $state|Add-Member -NotePropertyName history_retention -NotePropertyValue $result -Force
   Write-HubHistoryBytes $statePath (ConvertTo-HubHistoryBytes $state) (HashBytes $before)
  }catch{$result.status='Deferred';$result.reason='Installation completed; history retention status could not be recorded. '+$_.Exception.Message}
 }
 if($result.status -eq 'RecoveryRequired'){
  $failure=New-Object InvalidOperationException('Installation completed; automatic history cleanup needs recovery. Use Restore in the NeuRotic App. '+$result.reason)
  $failure.Data['NeuRoticMutationStatus']='RecoveryRequired';throw $failure
 }
 return $result
}
