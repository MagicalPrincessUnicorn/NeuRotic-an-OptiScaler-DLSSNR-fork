# Explicit permanent cleanup. This does not replay installation history or
# restore original game files; ordinary Uninstall remains the recovery workflow.
function Assert-HubSanitizeCategories($Categories){
 if($Categories -isnot [Array] -or $Categories.Count -lt 1 -or $Categories.Count -gt 6){throw 'Select one or more cleanup categories'}
 $seen=@{};foreach($category in $Categories){
  if($category -isnot [string] -or $category -cnotin @('rootFiles','assets','installer','backups','privateModels','screenshots') -or $seen.ContainsKey($category)){throw 'Invalid or duplicate cleanup category'}
  $seen[$category]=$true
 }
}
function Remove-HubSanitizeDirectory([string]$Path,[string]$ExpectedIdentity){
 Initialize-NeuroticFileSafety
 [NeuRoticFileSafety]::DeleteDirectory($Path,$ExpectedIdentity)
}
function Get-HubSanitizeTrustedHashes($Target){
 $hashes=@{}
 try{
  $package=Resolve-HubPackage $(if((Get-HubProperty $Target 'bitness' 64) -eq 32){'prepared-x86'}else{'flagship-approved'})
  foreach($file in $package.manifest.files){
   if($file.path.Replace('/','\') -in @('payload\OptiScaler.dll','payload\nvngx.dll_dlssnr.dll','payload\ReShade32.dll')){$hashes[[string]$file.sha256]='approved package hash'}
   if($file.path.Replace('/','\') -match '^payload\\(Licenses\\[^:]+|WinPixEventRuntime\.dll)$'){$hashes['asset:'+$matches[1]+':'+$file.sha256]='approved package asset hash'}
  }
 }catch{} # No package proof means shared filenames remain protected.
 $packageHashes=$hashes.Clone();$historyRecords=0;[long]$historyBytes=0
 # Independently valid old receipts can identify old installed proxies even
 # when the full restore chain is broken. Never follow paths from a receipt.
 foreach($folder in @('NeuRotic-backups','NeuRotic-test-backups')){
  $base=SafePath $Target.directory $folder;if(-not (Test-Path -LiteralPath $base -PathType Container)){continue}
  foreach($childPath in [IO.Directory]::EnumerateDirectories($base)){
   # Bound enumeration as well as parsing. On exhaustion, discard partial
   # historical proof; approved package hashes remain usable for cleanup.
   if(++$historyRecords -gt 4096){return $packageHashes}
   $child=Get-Item -LiteralPath $childPath -Force
   if($child.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked backup directory refused'}
   $path=SafePath $Target.directory ($folder+'\'+$child.Name+'\INSTALL-MANIFEST.json')
   if(-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 4194304){continue}
   $stream=$null;$reader=$null
   try{
    $stream=[IO.File]::OpenRead($path)
    if($stream.Length -gt 4194304){continue}
    if($historyBytes+$stream.Length -gt 67108864){return $packageHashes}
    $historyBytes+=$stream.Length
    # OpenRead denies concurrent writers while the bounded metadata is read.
    $reader=New-Object IO.StreamReader($stream,$true)
    $record=$reader.ReadToEnd()|ConvertFrom-Json
    if($record.kind -ne 'neurotic-customer-candidate-install' -or $record.status -ne 'installed-verified' -or
       [IO.Path]::GetFullPath((Split-Path -Parent $record.game_executable)).TrimEnd('\') -ine $Target.directory -or
       [IO.Path]::GetFullPath($record.backup).TrimEnd('\') -ine $child.FullName){continue}
    $names=@{};foreach($file in $record.files){Assert-RestoreTarget $file.path $path $names}
    foreach($file in $record.files){
     if($file.source -in @('payload\OptiScaler.dll','payload\nvngx.dll_dlssnr.dll','payload\ReShade32.dll') -and $file.operation -eq 'copy-package' -and $file.installed_hash -match '^[a-fA-F0-9]{64}$'){$hashes[[string]$file.installed_hash]='matching installed payload receipt'}
     if($file.path -ieq 'ReShade.ini' -and $file.source -ieq 'payload\ReShade.ini' -and $file.operation -eq 'copy-package' -and (Get-HubProperty $file 'existed' $true) -eq $false -and $file.installed_hash -match '^[a-fA-F0-9]{64}$'){$hashes['asset:ReShade.ini:'+$file.installed_hash]='matching newly installed capture settings receipt'}
     if($file.path -match '^(Licenses\\[^:]+|WinPixEventRuntime\.dll)$' -and $file.source -eq ('payload\'+$file.path) -and $file.operation -eq 'copy-package' -and $file.installed_hash -match '^[a-fA-F0-9]{64}$'){$hashes['asset:'+$file.path+':'+$file.installed_hash]='matching installed asset receipt'}
    }
   }catch{}finally{if($reader){$reader.Dispose()};if($stream){$stream.Dispose()}} # Malformed history never grants proof.
  }
 }
 return $hashes
}
function Get-HubSanitizeInventory($Target,$Categories,[hashtable]$TrustedHashes){
 Assert-HubSanitizeCategories $Categories
 Initialize-NeuroticFileSafety
 $root=$Target.directory.TrimEnd('\');$rootIdentity=[NeuRoticFileSafety]::Identity($root)
 # Never treat an App/program directory as a game-owned installation root.
 $app=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
 if($root -ieq $app -or $root -ieq [IO.Path]::GetPathRoot($root).TrimEnd('\') -or (Test-Path -LiteralPath (Join-Path $root 'NeuRotic.Hub.exe')) -or (Test-Path -LiteralPath (Join-Path $root 'NeuRotic.exe'))){throw 'Application or volume root cannot be sanitized'}
 $files=New-Object Collections.Generic.List[object];$retained=New-Object Collections.Generic.List[object];$directories=New-Object Collections.Generic.List[object]
 $pending=New-Object Collections.Generic.Stack[string];$pending.Push('');$count=0
 $shared=@('dxgi.dll','dxgl.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','nvngx.dll','ReShade64.dll','WinPixEventRuntime.dll')
 while($pending.Count){
  $relativeDir=$pending.Pop();$dir=if($relativeDir){SafePath $root $relativeDir}else{$root}
  foreach($entry in @(Get-ChildItem -LiteralPath $dir -Force|Sort-Object Name)){
   if(++$count -gt 20000){throw 'Cleanup inventory exceeds 20000 entries; no changes made'}
   $relative=if($relativeDir){$relativeDir+'\'+$entry.Name}else{$entry.Name}
   $top=($relative -split '\\')[0];$category=$null
   if($top -in @('NeuroticScreenshots','OptiScalerScreenshots') -or ($top -in @('NeuRotic','OptiScaler','NeuRotic-backups','NeuRotic-test-backups','OptiScaler-backups','NeuRotic-preserved-files') -and $relative -match '(^|\\)(NeuroticScreenshots|OptiScalerScreenshots|Screenshots?)(\\|$)')){$category='screenshots'}
   elseif(-not $entry.PSIsContainer -and $entry.Name -ieq 'nvngx_dlssnr.dll'){$category='privateModels'}
   elseif($top -in @('NeuRotic-backups','NeuRotic-test-backups','OptiScaler-backups','NeuRotic-preserved-files')){$category='backups'}
   elseif($relative -match '^NeuRotic\\(Installer|Uninstall|UserData)(\\|$)'){$category='installer'}
   elseif($top -in @('NeuRotic','OptiScaler')){$category='assets'}
   elseif($top -eq 'Licenses'){$category='assets'}
   elseif(-not $relativeDir -and $entry.Name -ieq 'nvngx_dlssnr.dll'){$category='privateModels'}
   elseif(-not $relativeDir -and ($entry.Name -in @('nvngx.dll_dlssnr.dll','Uninstall NeuRotic.cmd','Remove OptiScaler.bat','setup_OptiScaler.bat') -or $entry.Name -match '^(NeuRotic|OptiScaler)([-_. ][^\\]*)?\.(ini|log|json|cmd|bat|ps1|dll|asi|txt|md|addon64)$')){$category='rootFiles'}
   elseif(-not $relativeDir -and ($entry.Name -in $shared -or $entry.Name -ieq 'ReShade.ini')){$category='rootFiles'}
   if(-not $category){continue}
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Linked cleanup path refused: '+$relative)}
   [void](SafePath $root $relative)
   if($entry.PSIsContainer){
    $directories.Add([pscustomobject]@{path=$relative;identity=[NeuRoticFileSafety]::Identity($entry.FullName);category=$category})
    $pending.Push($relative);continue
   }
   if($entry.FullName -ieq $Target.executable){throw 'Selected executable cannot be removed'}
   if($category -notin $Categories){$retained.Add([pscustomobject]@{path=$relative;reason=('Category kept: '+$category)});continue}
   $hash=HashFile $entry.FullName;$reason='recognized NeuRotic/OptiScaler file or selected owned directory'
   if($top -eq 'Licenses' -or $entry.Name -in @('WinPixEventRuntime.dll','ReShade.ini')){
    $proof='asset:'+$relative+':'+$hash
    if(-not $TrustedHashes.ContainsKey($proof)){$retained.Add([pscustomobject]@{path=$relative;reason='Shared asset has no matching owned payload path and hash; preserved';sha256=$hash});continue}
    $reason=$TrustedHashes[$proof]
   }elseif(-not $relativeDir -and $entry.Name -in $shared){
    if(-not $TrustedHashes.ContainsKey($hash)){$retained.Add([pscustomobject]@{path=$relative;reason='Shared DLL has no matching trusted NR payload hash; preserved';sha256=$hash});continue}
    $reason=$TrustedHashes[$hash]
   }
   $files.Add([pscustomobject]@{path=$relative;operation='delete permanently';sha256=$hash;bytes=$entry.Length;category=$category;reason=$reason})
   Show-SetupProgress 'Reviewing permanent cleanup' $count
  }
 }
 $snapshot=[ordered]@{rootIdentity=$rootIdentity;files=@($files.ToArray()|Sort-Object path);directories=@($directories.ToArray()|Sort-Object path);retained=@($retained.ToArray()|Sort-Object path);categories=@($Categories|Sort-Object)}
 $revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(($snapshot|ConvertTo-Json -Depth 10 -Compress)))
 $snapshot.revision=$revision;return [pscustomobject]$snapshot
}
function New-HubSanitizePlan($Request){
 Assert-HubSanitizeCategories $Request.sanitizeCategories
 $target=Get-HubTarget $Request.gameExecutable
 $inventory=Get-HubSanitizeInventory $target $Request.sanitizeCategories (Get-HubSanitizeTrustedHashes $target)
 $plan=[ordered]@{schemaVersion=1;planId=[Guid]::NewGuid().ToString('N');status='Planned';createdUtc=[DateTime]::UtcNow.ToString('o');expiresUtc=[DateTime]::UtcNow.AddMinutes(5).ToString('o');request=$Request;target=$target;revision=$inventory.revision;configRevision=$null;packageDigest=$null;changes=$inventory.files;recovery=$null;antiCheat=$null;preserved=$inventory.retained;requiresElevation=$false;sanitation=[ordered]@{categories=$inventory.categories;retained=$inventory.retained;directories=$inventory.directories;rootIdentity=$inventory.rootIdentity;warning='Permanent cleanup. Original game files are not restored. Review every removal before confirming.'}}
 $plan.planFingerprint=Get-HubPlanFingerprint ([pscustomobject]$plan)
 SaveRecord (Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')) $plan
 return $plan
}
function Invoke-HubSanitizeExecute($Plan,$Target,[string]$PlanPath){
 $deleted=New-Object Collections.Generic.List[string];$deletedDirectories=New-Object Collections.Generic.List[string]
 $receiptPath=Join-Path (Get-HubUserRoot) ($Plan.planId+'.sanitize.json')
 $receipt=[ordered]@{kind='neurotic-permanent-cleanup';planId=$Plan.planId;target=$Target;status='Executing';deleted=@();deletedDirectories=@();remaining=@($Plan.changes|ForEach-Object{$_.path});pending=$null;originalsRestored=$false}
 try{
  $inventory=Get-HubSanitizeInventory $Target $Plan.request.sanitizeCategories (Get-HubSanitizeTrustedHashes $Target)
  if($Target.sha256 -ne $Plan.target.sha256 -or $inventory.revision -ne $Plan.revision){return [ordered]@{status='PreconditionChanged';reason='Cleanup target or files changed; review again'}}
  CheckGameClosed $Target.executable
  $rootLease=[NeuRoticFileSafety]::PinTarget($Target.directory)
  $Plan.status='Executing';SaveRecord $PlanPath $Plan;SaveRecord $receiptPath $receipt
  # Retire proven live payloads before deleting the receipts that identify old
  # versions. This retains ownership evidence if an earlier file refuses.
  $order=@{rootFiles=0;privateModels=1;assets=2;screenshots=3;backups=4;installer=5}
  foreach($file in @($inventory.files|Sort-Object @{Expression={$order[$_.category]}},'path')){
   $receipt.pending=$file.path;SaveRecord $receiptPath $receipt
   $path=SafePath $Target.directory $file.path;$parents=[NeuRoticFileSafety]::Parents($path,$false)
   try{
    if([NeuRoticFileSafety]::Identity($Target.directory) -ne $inventory.rootIdentity){throw 'Cleanup root identity changed'}
    foreach($directory in $inventory.directories){if($file.path.StartsWith($directory.path+'\',[StringComparison]::OrdinalIgnoreCase) -and [NeuRoticFileSafety]::Identity((SafePath $Target.directory $directory.path)) -ne $directory.identity){throw 'Cleanup subdirectory identity changed'}}
    Remove-NeuroticPath -LiteralPath $path -ExpectedHash $file.sha256
   }finally{$parents.Dispose()}
   $deleted.Add($file.path);$receipt.deleted=$deleted.ToArray();$receipt.remaining=@($receipt.remaining|Where-Object{$_ -cne $file.path});$receipt.pending=$null;SaveRecord $receiptPath $receipt
  }
  # Delete only reviewed, unchanged and now-empty selected directories, deepest
  # first. Retained screenshots or late-arriving files keep their parents.
  foreach($directory in @($inventory.directories|Sort-Object @{Expression={$_.path.Length};Descending=$true})){
   if($directory.category -notin $Plan.request.sanitizeCategories -or $directory.path -match '^Licenses(\\|$)'){continue}
   $path=SafePath $Target.directory $directory.path
   if([NeuRoticFileSafety]::Identity($path) -ne $directory.identity){throw 'Cleanup directory identity changed'}
   if(@(Get-ChildItem -LiteralPath $path -Force).Count){continue}
   $receipt.pending=$directory.path;SaveRecord $receiptPath $receipt
   Remove-HubSanitizeDirectory $path $directory.identity;$deletedDirectories.Add($directory.path);$receipt.deletedDirectories=$deletedDirectories.ToArray();$receipt.pending=$null;SaveRecord $receiptPath $receipt
  }
  $remaining=Get-HubSanitizeInventory $Target $Plan.request.sanitizeCategories (Get-HubSanitizeTrustedHashes $Target)
  if(@($remaining.files).Count){$receipt.remaining=@($remaining.files.path);throw 'New cleanup candidates appeared; review a fresh plan before continuing'}
  $receipt.status='Succeeded';SaveRecord $receiptPath $receipt
  $result=[ordered]@{status='Succeeded';reason='Selected cleanup completed. Original game files were not restored.';deleted=$deleted.ToArray();deletedDirectories=$deletedDirectories.ToArray();retained=$inventory.retained;receipt=$receiptPath;originalsRestored=$false;planId=$Plan.planId}
 }catch{
  $receipt.status=if($deleted.Count -or $deletedDirectories.Count -or $receipt.pending){'FailedWithChanges'}else{'FailedWithoutMutation'}
  $receipt.reason=$_.Exception.Message
  try{SaveRecord $receiptPath $receipt}catch{}
  $result=[ordered]@{status=$receipt.status;reason=$receipt.reason;deleted=$deleted.ToArray();remaining=$receipt.remaining;pending=$receipt.pending;receipt=$receiptPath;originalsRestored=$false;planId=$Plan.planId}
 }finally{if(Get-Variable rootLease -Scope Local -ErrorAction SilentlyContinue){if($rootLease){$rootLease.Dispose()}}}
 $Plan.status=$result.status;SaveRecord $PlanPath $Plan;return $result
}
