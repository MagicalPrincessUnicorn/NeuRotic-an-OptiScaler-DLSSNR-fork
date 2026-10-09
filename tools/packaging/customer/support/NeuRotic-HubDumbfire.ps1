# Explicit direct payload installation. Never consult or rewrite game-local
# managed history, compatibility, anti-cheat, ReShade or saved-settings policy.
function Get-HubDumbfireStore($Target){
 $key=HashBytes ([Text.Encoding]::UTF8.GetBytes($Target.directory.ToUpperInvariant()))
 # Existing recovery bytes remain authoritative across the mutable-document
 # namespace migration. Never hide the only original behind a new empty store.
 if(-not $env:NEUROTIC_HUB_FIXTURE_ROOT){
  $legacy=SafePath (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NeuRotic/Hub') ('Dumbfire\'+$key)
  if(Test-Path -LiteralPath $legacy -PathType Container){return $legacy}
 }
 return SafePath (Get-HubUserRoot) ('Dumbfire\'+$key)
}
function Read-HubDumbfireJson([string]$Path,[long]$Maximum=16777216){
 [void](SafePath (Split-Path -Parent $Path) (Split-Path -Leaf $Path))
 $stream=[IO.File]::OpenRead($Path);$reader=$null
 try{if($stream.Length -gt $Maximum){throw 'Dumbfire receipt exceeds its limit'};$reader=New-Object IO.StreamReader($stream,$true);return ($reader.ReadToEnd()|ConvertFrom-Json)}finally{if($reader){$reader.Dispose()};$stream.Dispose()}
}
function Get-HubDumbfirePending($Target,[switch]$IncludeCompleted){
 $store=Get-HubDumbfireStore $Target;$marker=SafePath $store 'Pending.json'
 $completed=$false
 if(-not (Test-Path -LiteralPath $marker)){
  if(-not $IncludeCompleted){return $null}
  $indexPath=SafePath $store 'Completed.json';if(-not(Test-Path -LiteralPath $indexPath)){return $null}
  $index=Read-HubDumbfireJson $indexPath 16384
  if($index.kind -ne 'neurotic-dumbfire-completed' -or @($index.transactions).Count -gt 64){throw 'Invalid direct-install recovery index'}
  if(-not @($index.transactions).Count){return $null}
  $pending=[pscustomobject]@{kind='neurotic-dumbfire-pending';transactionId=$index.transactions[0];directory=$Target.directory};$completed=$true
 }else{$pending=Read-HubDumbfireJson $marker 16384}
 if($pending.kind -ne 'neurotic-dumbfire-pending' -or $pending.transactionId -cnotmatch '^[a-f0-9]{32}$' -or $pending.directory -ine $Target.directory){throw 'Unrecognized Dumbfire pending marker; preserve it for recovery'}
 $root=SafePath $store $pending.transactionId;$receipt=SafePath $root 'Transaction.json';$journal=Read-HubDumbfireJson $receipt
 if($journal.kind -ne 'neurotic-dumbfire-transaction' -or $journal.transactionId -cne $pending.transactionId -or $journal.target.directory -ine $Target.directory -or $journal.target.executable -ine $Target.executable -or ($Target.sha256 -and $journal.target.sha256 -ne $Target.sha256) -or @($journal.files).Count -gt 2048){throw 'Dumbfire recovery target identity differs; preserve the receipt'}
 Initialize-NeuroticFileSafety
 if($journal.rootIdentity -ne [NeuRoticFileSafety]::Identity($Target.directory)){throw 'Dumbfire target directory identity changed'}
 $names=@{};foreach($file in $journal.files){
  if(-not (AllowedTarget $file.path) -or $names.ContainsKey($file.path) -or $file.snapshot -cnotmatch '^before\\[0-9]{4}\.bin$' -or $file.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or ($file.existed -and $file.beforeHash -notmatch '^[A-Fa-f0-9]{64}$')){throw 'Invalid Dumbfire recovery entry'}
  $names[$file.path]=$true;[void](SafePath $Target.directory $file.path);[void](SafePath $root $file.snapshot)
 }
 if($completed -and ($journal.status -notin @('Installed','RolledBack') -or (Get-HubProperty $journal 'preimagesPruned' $false))){throw 'Completed direct-install recovery is unavailable; preserve its receipt'}
 return [pscustomobject]@{root=$root;receipt=$receipt;marker=$marker;journal=$journal;revision=(HashFile $receipt);completed=$completed}
}
function Get-HubDumbfireHistory($Target){
 $entry=Get-HubDumbfirePending $Target -IncludeCompleted
 if(-not $entry){return [ordered]@{rollbackAvailable=$false}}
 return [ordered]@{rollbackAvailable=$true;pending=(-not $entry.completed);transactionId=$entry.journal.transactionId;receipt=$entry.receipt;backupRoot=$entry.root}
}
function Get-HubDumbfireLive([string]$Root,[string]$Relative){
 $path=SafePath $Root $Relative
 if(Test-Path -LiteralPath $path){
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw ('A directory occupies required file: '+$Relative)}
  $size=(Get-Item -LiteralPath $path).Length;if($size -gt 134217728){throw ('Required destination exceeds safe backup size: '+$Relative)}
  return [pscustomobject]@{existed=$true;sha256=(HashFile $path);bytes=$size}
 }
 return [pscustomobject]@{existed=$false;sha256='Absent';bytes=0}
}
function Get-HubDumbfireProjection($Target,$Package,[string]$Proxy){
 if($Target.bitness -ne 64){throw 'Dumbfire uses a 64-bit package and cannot install into a 32-bit game. Use a compatible prepared installation, or NR Anything until a compatible in-game route is available.'}
 Initialize-NeuroticFileSafety
 $root=$Target.directory;$app=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
 $packageRoot=[IO.Path]::GetFullPath($Package.root).TrimEnd('\')
 if($root -ieq $packageRoot -or $root.StartsWith($packageRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $packageRoot.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Keep the verified package outside the selected game folder'}
 if($root -ieq $app -or $root -ieq [IO.Path]::GetPathRoot($root).TrimEnd('\') -or (Test-Path -LiteralPath (Join-Path $root 'NeuRotic.Hub.exe')) -or (Test-Path -LiteralPath (Join-Path $root 'NeuRotic.exe'))){throw 'Application or volume root cannot receive Dumbfire installation'}
 if($Proxy -cnotin @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){throw 'Choose a supported proxy filename'}
 $files=New-Object Collections.Generic.List[object];$seen=@{};$parents=@{};$hasDll=$false;$hasIni=$false
 foreach($row in $Package.manifest.files){
  $source=[string]$row.path;$source=$source.Replace('/','\');if(-not $source.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)){continue}
  $relative=$source.Substring(8);if($relative -ieq 'OptiScaler.dll'){$relative=$Proxy;$hasDll=$true};if($relative -ieq 'OptiScaler.ini'){$hasIni=$true}
  if($files.Count -ge 2048 -or -not (AllowedTarget $relative) -or $seen.ContainsKey($relative) -or $row.sha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Invalid or duplicate Dumbfire payload destination'}
  $seen[$relative]=$true;$sourcePath=SafePath $Package.root $source
  $bytes=(Get-Item -LiteralPath $sourcePath).Length;if($bytes -gt 134217728 -or (HashFile $sourcePath) -ne $row.sha256){throw 'Required package source changed or exceeds safe size'}
  $live=Get-HubDumbfireLive $root $relative
  $parent=Split-Path -Parent (SafePath $root $relative)
  while($parent -ine $root){
   $name=$parent.Substring($root.Length+1)
   if(-not $parents.ContainsKey($name)){
    if((Test-Path -LiteralPath $parent) -and -not (Test-Path -LiteralPath $parent -PathType Container)){throw 'Required parent path is not a directory'}
    $parents[$name]=if(Test-Path -LiteralPath $parent){[NeuRoticFileSafety]::Identity($parent)}else{'Absent'}
   }
   $parent=Split-Path -Parent $parent
  }
  $files.Add([pscustomobject]@{path=$relative;source=$source;sha256=[string]$row.sha256;bytes=$bytes;existed=$live.existed;beforeHash=$live.sha256;beforeBytes=$live.bytes;operation=$(if($live.sha256 -eq $row.sha256){'already verified'}elseif($live.existed){'overwrite with verified package'}else{'install verified package'})})
 }
 if(-not $hasDll -or -not $hasIni){throw 'Approved package lacks required proxy or configuration'}
 $projection=[ordered]@{rootIdentity=[NeuRoticFileSafety]::Identity($root);parents=@($parents.Keys|Sort-Object|ForEach-Object{[pscustomobject]@{path=$_;identity=$parents[$_]}});files=@($files.ToArray()|Sort-Object path);packageDigest=$Package.digest}
 $revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(($projection|ConvertTo-Json -Depth 10 -Compress)))
 $projection.revision=$revision;return [pscustomobject]$projection
}
function Get-HubDumbfireRecoveryChanges($Pending,$Target){
 $changes=New-Object Collections.Generic.List[object];$blocked=New-Object Collections.Generic.List[string]
 foreach($file in $Pending.journal.files){
  if(-not $file.written -and $Pending.journal.pending -cne $file.path){continue}
  $live=Get-HubDumbfireLive $Target.directory $file.path
  if($live.sha256 -eq $file.beforeHash){continue}
  if($live.sha256 -ne $file.sha256){$blocked.Add($file.path);continue}
  if($file.existed -and (HashFile (SafePath $Pending.root $file.snapshot)) -ne $file.beforeHash){throw 'Dumbfire preimage changed; preserve it for recovery'}
  $changes.Add([pscustomobject]@{path=$file.path;operation=$(if($file.existed){'restore pre-Dumbfire bytes'}else{'remove file created by Dumbfire'});sha256=$live.sha256;restoreHash=$file.beforeHash})
 }
 return [pscustomobject]@{changes=$changes.ToArray();blocked=$blocked.ToArray()}
}
function New-HubDumbfirePlan($Request){
 try{
  $target=Get-HubTarget $Request.gameExecutable;$pending=Get-HubDumbfirePending $target -IncludeCompleted:($Request.operation -eq 'DumbfireRollback')
  if($Request.operation -eq 'DumbfireInstall' -and $pending){return [ordered]@{status='RecoveryRequired';dumbfire=$true;target=$target;reason='A previous Dumbfire transaction needs its own recovery before another direct install.';rollbackAvailable=$true;receipt=$pending.receipt;backupRoot=$pending.root}}
  $package=$null;$info=$null
  if($Request.operation -eq 'DumbfireRollback'){
   if(-not $pending){throw 'No recoverable direct installation for this selected game'}
   $recovery=Get-HubDumbfireRecoveryChanges $pending $target
   if($recovery.blocked.Count){return [ordered]@{status='RecoveryRequired';dumbfire=$true;target=$target;reason='Files changed outside Dumbfire; those files are preserved. Resolve them before reviewing recovery.';rollbackAvailable=$true;remainingRecovery=$recovery.blocked;receipt=$pending.receipt;backupRoot=$pending.root}}
   $changes=$recovery.changes;$revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(($recovery|ConvertTo-Json -Depth 8 -Compress)))
   $info=[ordered]@{transactionId=$pending.journal.transactionId;journalRevision=$pending.revision;receipt=$pending.receipt;rootIdentity=$pending.journal.rootIdentity}
  }else{
   $package=Resolve-HubPackage $Request.packageId;$projection=Get-HubDumbfireProjection $target $package $Request.proxyName
   $changes=$projection.files;$revision=$projection.revision;$info=[ordered]@{rootIdentity=$projection.rootIdentity;warning='Required package files, including settings, will be overwritten. Existing managed history is neither repaired nor adopted.'}
  }
  $plan=[ordered]@{schemaVersion=1;planId=[Guid]::NewGuid().ToString('N');status='Planned';createdUtc=[DateTime]::UtcNow.ToString('o');expiresUtc=[DateTime]::UtcNow.AddMinutes(5).ToString('o');request=$Request;target=$target;revision=$revision;configRevision=$null;packageDigest=$(if($package){$package.digest}else{$null});changes=@($changes);recovery=$null;antiCheat=$null;preserved=@('unrelated files','existing managed history and old recovery journals','private model files');requiresElevation=$false;dumbfire=$true;dumbfireInfo=$info}
  $plan.planFingerprint=Get-HubPlanFingerprint ([pscustomobject]$plan);SaveRecord (Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')) $plan;return $plan
 }catch{return [ordered]@{status='FailedWithoutMutation';dumbfire=$true;rollbackAvailable=$false;reason=$_.Exception.Message}}
}
function Restore-HubDumbfireTransaction($Pending,$Target){
 $journal=$Pending.journal;$restored=New-Object Collections.Generic.List[string];$blocked=New-Object Collections.Generic.List[string]
 $reverse=@($journal.files);[array]::Reverse($reverse)
 foreach($file in $reverse){
  if(-not $file.written -and $journal.pending -cne $file.path){continue}
  try{
   $live=Get-HubDumbfireLive $Target.directory $file.path
   if($live.sha256 -ne $file.beforeHash){
    if($live.sha256 -ne $file.sha256){throw 'Outside edit preserved'}
    if($file.existed){CopyVerified (SafePath $Pending.root $file.snapshot) (SafePath $Target.directory $file.path) $file.beforeHash $file.sha256}
    else{Remove-NeuroticPath -LiteralPath (SafePath $Target.directory $file.path) -ExpectedHash $file.sha256}
   }
   $file.written=$false;if($journal.pending -ceq $file.path){$journal.pending=$null};$restored.Add($file.path);SaveRecord $Pending.receipt $journal
  }catch{$blocked.Add($file.path)}
 }
 $journal.status=if($blocked.Count){'RecoveryRequired'}else{'RolledBack'};SaveRecord $Pending.receipt $journal
 if(-not $blocked.Count){
  if(Test-Path -LiteralPath $Pending.marker){Remove-NeuroticPath -LiteralPath $Pending.marker -ExpectedHash (HashFile $Pending.marker)}
  $indexPath=SafePath (Get-HubDumbfireStore $Target) 'Completed.json'
  if(Test-Path -LiteralPath $indexPath){$index=Read-HubDumbfireJson $indexPath 16384;$index.transactions=@($index.transactions|Where-Object{$_ -cne $journal.transactionId});SaveRecord $indexPath $index}
 }
 return [pscustomobject]@{complete=($blocked.Count -eq 0);restored=$restored.ToArray();remaining=$blocked.ToArray()}
}
function Update-HubDumbfireRetention($Target,[string]$TransactionId){
 # Only the bounded completion index admits a successful preimage set to pruning.
 # Pending, failed and unindexed transactions are never enumerated or removed.
 $store=Get-HubDumbfireStore $Target;$path=SafePath $store 'Completed.json';$ids=@()
 if(Test-Path -LiteralPath $path){$index=Read-HubDumbfireJson $path 16384;if($index.kind -ne 'neurotic-dumbfire-completed' -or @($index.transactions).Count -gt 64){throw 'Unrecognized fallback retention index; preimages retained'};$ids=@($index.transactions)}
 foreach($id in $ids){if($id -cnotmatch '^[a-f0-9]{32}$'){throw 'Invalid fallback completion ID; preimages retained'}}
 $ids=@($TransactionId)+@($ids|Where-Object{$_ -cne $TransactionId}|Select-Object -Unique)
 if($ids.Count -gt 64){throw 'Direct-install recovery history is at capacity; restore existing transactions before adding another.'}
 SaveRecord $path ([ordered]@{kind='neurotic-dumbfire-completed';transactions=$ids})

}
function Invoke-HubDumbfireExecute($Plan,$Target,[string]$PlanPath){
 $pending=$null;$ownsTransaction=$false;$possibleWrite=$false;$installed=New-Object Collections.Generic.List[string];$leases=New-Object Collections.Generic.List[object];$createdParents=New-Object Collections.Generic.List[string]
 try{
  $pending=Get-HubDumbfirePending $Target -IncludeCompleted:($Plan.request.operation -eq 'DumbfireRollback')
  if($Target.sha256 -ne $Plan.target.sha256){throw 'Selected executable changed; review again'}
  CheckGameClosed $Target.executable
  if($Plan.request.operation -eq 'DumbfireRollback'){
   if(-not $pending -or $pending.journal.transactionId -cne $Plan.dumbfireInfo.transactionId -or $pending.revision -ne $Plan.dumbfireInfo.journalRevision){throw 'Dumbfire recovery receipt changed; review again'}
   $recovery=Get-HubDumbfireRecoveryChanges $pending $Target;$revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(($recovery|ConvertTo-Json -Depth 8 -Compress)))
   if($recovery.blocked.Count -or $revision -ne $Plan.revision){throw 'Recovery targets changed; outside edits preserved'}
   foreach($parent in $pending.journal.parents){if($parent.identity -eq 'Absent'){continue};$path=SafePath $Target.directory $parent.path;$leases.Add([NeuRoticFileSafety]::PinTarget($path));if([NeuRoticFileSafety]::Identity($path) -ne $parent.identity){throw 'Recovery parent directory identity changed'}}
   $Plan.status='Executing';SaveRecord $PlanPath $Plan
   $restored=Restore-HubDumbfireTransaction $pending $Target
   $result=[ordered]@{status=$(if($restored.complete){'Succeeded'}else{'RecoveryRequired'});reason=$(if($restored.complete){'Pre-Dumbfire file contents were restored. Existing managed history was not changed.'}else{'Some fallback files could not be restored; outside edits were preserved.'});dumbfire=$true;rollbackAvailable=(-not $restored.complete);restored=$restored.restored;remainingRecovery=$restored.remaining;receipt=$pending.receipt;backupRoot=$pending.root;managedHistoryChanged=$false}
  }else{
   if($pending){return [ordered]@{status='RecoveryRequired';reason='Previous Dumbfire recovery is pending';dumbfire=$true;rollbackAvailable=$true;receipt=$pending.receipt;backupRoot=$pending.root;planId=$Plan.planId;target=$Target}}
   $completedIndex=SafePath (Get-HubDumbfireStore $Target) 'Completed.json'
   if(Test-Path -LiteralPath $completedIndex){$completed=Read-HubDumbfireJson $completedIndex 16384;if(@($completed.transactions).Count -ge 64){throw 'Direct-install recovery is at capacity; restore previous transactions before installing again.'}}
   $package=Resolve-HubPackage $Plan.request.packageId;$projection=Get-HubDumbfireProjection $Target $package $Plan.request.proxyName
   if($package.digest -ne $Plan.packageDigest -or $projection.revision -ne $Plan.revision){return [ordered]@{status='PreconditionChanged';reason='Required destination or package changed; review again';dumbfire=$true;rollbackAvailable=$false;planId=$Plan.planId;target=$Target}}
   foreach($parent in $projection.parents){if($parent.identity -eq 'Absent'){continue};$path=SafePath $Target.directory $parent.path;$leases.Add([NeuRoticFileSafety]::PinTarget($path));if([NeuRoticFileSafety]::Identity($path) -ne $parent.identity){throw 'Required parent directory changed'}}
   $root=SafePath (Get-HubDumbfireStore $Target) $Plan.planId
   if(Test-Path -LiteralPath $root){throw 'Dumbfire transaction directory already exists; review a fresh plan'}
   Ensure-NeuroticDirectory $root;$files=New-Object Collections.Generic.List[object];$i=0
   foreach($file in $projection.files){if($file.beforeHash -eq $file.sha256){continue};$row=$file|Select-Object *;$row|Add-Member snapshot ('before\'+$i.ToString('D4')+'.bin');$row|Add-Member written $false;$files.Add($row);$i++}
   $journal=[pscustomobject]@{kind='neurotic-dumbfire-transaction';transactionId=$Plan.planId;target=$Target;rootIdentity=$projection.rootIdentity;parents=$projection.parents;packageDigest=$package.digest;proxyName=$Plan.request.proxyName;status='Preparing';files=$files.ToArray();pending=$null;createdUtc=[DateTime]::UtcNow.ToString('o')}
   $receipt=SafePath $root 'Transaction.json';$marker=SafePath (Get-HubDumbfireStore $Target) 'Pending.json'
   SaveRecord $receipt $journal;SaveRecord $marker ([ordered]@{kind='neurotic-dumbfire-pending';transactionId=$Plan.planId;directory=$Target.directory})
   $pending=[pscustomobject]@{root=$root;receipt=$receipt;marker=$marker;journal=$journal}
   $ownsTransaction=$true
   foreach($file in $journal.files){if($file.existed){CopyVerified (SafePath $Target.directory $file.path) (SafePath $root $file.snapshot) $file.beforeHash 'Absent'}}
   foreach($parent in @($journal.parents|Sort-Object {$_.path.Length})){
    if($parent.identity -ne 'Absent'){continue};$path=SafePath $Target.directory $parent.path
    if(Test-Path -LiteralPath $path){throw 'Required absent parent appeared; review again'}
    Ensure-NeuroticDirectory $path;$createdParents.Add($parent.path);$leases.Add([NeuRoticFileSafety]::PinTarget($path));$parent.identity=[NeuRoticFileSafety]::Identity($path)
   }
   $journal.status='Ready';SaveRecord $receipt $journal
   $Plan.status='Executing';SaveRecord $PlanPath $Plan
   foreach($file in $journal.files){
    $journal.status='Writing';$journal.pending=$file.path;SaveRecord $receipt $journal
    $possibleWrite=$true
    try{CopyVerified (SafePath $package.root $file.source) (SafePath $Target.directory $file.path) $file.sha256 $file.beforeHash}
    catch{if(-not (Get-NeuroticFailureProperty $_.Exception 'NeuRoticWriteStarted') -and (Get-NeuroticFailureProperty $_.Exception 'NeuRoticRefusedPath')){$journal.pending=$null;SaveRecord $receipt $journal};throw}
    $file.written=$true;$journal.pending=$null;$installed.Add($file.path);SaveRecord $receipt $journal
   }
   foreach($file in $projection.files){if((HashFile (SafePath $Target.directory $file.path)) -ne $file.sha256){throw 'Installed payload changed before final verification'}}
   $journal.status='Installed';SaveRecord $receipt $journal;Update-HubDumbfireRetention $Target $Plan.planId;Remove-NeuroticPath -LiteralPath $marker -ExpectedHash (HashFile $marker)
   $result=[ordered]@{status='Succeeded';reason='Verified package files were installed directly. Existing managed history was not repaired; normal maintenance may still need Sanitize.';dumbfire=$true;rollbackAvailable=$true;installed=@($projection.files|ForEach-Object{$_.path});receipt=$receipt;backupRoot=$root;managedHistoryChanged=$false}

  }
 }catch{
  $reason=$_.Exception.Message;$status='FailedWithoutMutation';$available=$false;$remaining=@()
  if($pending -and $ownsTransaction -and $Plan.request.operation -eq 'DumbfireInstall'){
   try{$restored=Restore-HubDumbfireTransaction $pending $Target;$remaining=$restored.remaining;$available=-not $restored.complete;$status=if($available){'RecoveryRequired'}elseif($possibleWrite){'FailedRolledBack'}else{'FailedWithoutMutation'}}catch{$status='RecoveryRequired';$available=$true}
  }elseif($pending){$status='RecoveryRequired';$available=$true}
  if($createdParents.Count -and $status -in @('FailedWithoutMutation','FailedRolledBack')){$status='FailedWithChanges';$reason+=' Required payload directories were created and remain; file contents were restored.'}
  $result=[ordered]@{status=$status;reason=$reason;dumbfire=$true;rollbackAvailable=$available;remainingRecovery=$remaining;managedHistoryChanged=$false}
  if($createdParents.Count){$result.remainingCreatedDirectories=$createdParents.ToArray()}
  if($pending){$result.receipt=$pending.receipt;$result.backupRoot=$pending.root}
 }finally{foreach($lease in $leases){$lease.Dispose()}}
 $result.planId=$Plan.planId;$result.target=$Target;$Plan.status=$result.status
 try{SaveRecord $PlanPath $Plan}catch{$result.reason+=' The plan result could not be saved; inspect its independent receipt.';$result.receiptWarning=$_.Exception.Message}
 return $result
}
