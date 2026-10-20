. (Join-Path $PSScriptRoot 'NeuRotic-InstallReceipt.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-InstallProfiles.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-InstallDefaults.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubSettings.ps1')
if(-not ('NeuRoticSimpleFileIo' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-SimpleFileIo.cs')}
$script:NeuroticSimpleProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
function New-NeuroticSimpleResult {
 return [pscustomobject]@{status='FailedWithoutChanges';copied=@();reused=@();skipped=@();removed=@();errors=@();notes=@();receipt=$null;conflicts=@()}
}
function Test-NeuroticLoader([string]$Path){
 if(-not [IO.File]::Exists($Path)){return $false}
 try{return [Diagnostics.FileVersionInfo]::GetVersionInfo($Path).OriginalFilename -ieq 'OptiScaler.dll'}catch{return $false}
}
function Invoke-NeuroticSimpleBoundary([string]$Operation,[string]$Path){
 # Test code can inject failures only, not authorize or bypass an operation.
 # An optional test hook is an already defined function/filter or explicit alias.
 # Query those live providers without scanning modules or PATH for an absent hook.
 $hook=Get-Item -LiteralPath Alias:\Invoke-SimpleFault -ErrorAction Ignore
 if(-not $hook){$hook=Get-Item -LiteralPath Function:\Invoke-SimpleFault -ErrorAction Ignore}
 if($hook){& $hook $Operation $Path}
}
function Enter-NeuroticSimpleLock([string]$GameRoot){
 $name=[NeuRoticSimpleFileIo]::DirectoryIdentity($GameRoot)
 $lock=New-Object Threading.Mutex($false,('Global\NeuRotic-Installer-'+$name))
 try{try{$held=$lock.WaitOne(0)}catch [Threading.AbandonedMutexException]{$held=$true};if(-not $held){throw 'Another installer is working on this game. Retry when it finishes.'};return $lock}catch{$lock.Dispose();throw}
}
function Read-NeuroticSimpleIni([string]$Path){
 $bytes=[IO.File]::ReadAllBytes($Path);if($bytes.Length -gt 1MB){throw 'Configuration exceeds 1 MiB'}
 $offset=0;$encoding=New-Object Text.UTF8Encoding($false,$true)
 if($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254){$encoding=New-Object Text.UnicodeEncoding($false,$true,$true);$offset=2}
 elseif($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255){$encoding=New-Object Text.UnicodeEncoding($true,$true,$true);$offset=2}
 elseif($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191){$offset=3}
 try{$text=$encoding.GetString($bytes,$offset,$bytes.Length-$offset)}catch{if($offset){throw};$encoding=[Text.Encoding]::Default;$text=$encoding.GetString($bytes)}
 $bom=$(if($offset){[byte[]]$bytes[0..($offset-1)]}else{[byte[]]@()})
 return [pscustomobject]@{text=$text;encoding=$encoding;bom=$bom}
}
# Provider copying is an initial-install action. FreshInstall changes INI
# policy only. Ambiguous ownership withholds the provider, never own-file updates.
function Get-NeuroticNrDllTargetState([string]$GameRoot,$Receipt){
 if($null -ne $Receipt){return 'Existing'}
 try{
  if((Read-NeuroticNrDllOwnership $GameRoot).receipt){return 'Existing'}
  $marker=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/Current-Install.json'
  if(Test-Path -LiteralPath $marker){return 'Uncertain'}
  foreach($proxy in $script:NeuroticSimpleProxies){
   if(Test-NeuroticLoader (Resolve-NeuroticOwnedPath $GameRoot $proxy)){return 'Existing'}
  }
  # An interrupted or legacy installer may have no readable current receipt.
  $installer=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer'
  if(Test-Path -LiteralPath $installer){return 'Uncertain'}
  $configuration=Resolve-NeuroticOwnedPath $GameRoot 'OptiScaler.ini'
  if(Test-Path -LiteralPath $configuration){return 'Uncertain'}
  return 'Initial'
 }catch{return 'Uncertain'}
}

function Assert-NeuroticNrDllCopy($Copy){
 if($null -eq $Copy){throw (New-NeuroticCodedError 'desktop.nrdll.invalid_copy' 'The selected NR DLL request is invalid. Choose the file again.')}
 $mode=Get-NeuroticField $Copy 'mode';$keys=@(if($Copy -is [Collections.IDictionary]){$Copy.Keys}else{$Copy.PSObject.Properties.Name})
 $valid=$mode -is [string] -and $mode -cin @('none','preserve','profile')
 $allowed=@('mode');if($mode -ceq 'profile'){$allowed=@('mode','profileId','profileName','source','sha256','bytes')}
 if(-not $valid -or $keys.Count -ne $allowed.Count -or @($keys|Where-Object{$_ -cnotin $allowed}).Count){throw (New-NeuroticCodedError 'desktop.nrdll.invalid_copy' 'The selected NR DLL request is invalid. Choose the file again.')}
 if($mode -cne 'profile'){return}
 foreach($key in @('profileId','profileName','source','sha256')){if((Get-NeuroticField $Copy $key) -isnot [string]){throw (New-NeuroticCodedError 'desktop.nrdll.invalid_copy' 'The selected NR DLL request is invalid. Choose the file again.')}}
 $id=Get-NeuroticField $Copy 'profileId';$name=Get-NeuroticField $Copy 'profileName';$source=Get-NeuroticField $Copy 'source';$hash=Get-NeuroticField $Copy 'sha256';$bytes=Get-NeuroticField $Copy 'bytes'
 $pathRoot=[IO.Path]::GetPathRoot($source);$fullyQualified=$pathRoot -and $pathRoot.Length -ge 3 -and ($pathRoot.EndsWith('\') -or $pathRoot.EndsWith('/'))
 # Identity describes the user's choice. No developer size/hash/version is an
 # admission rule. Decimal permits exact uint64 JSON above Int64.MaxValue.
 if(-not $id -or $id.Length -gt 4096 -or -not $name -or $name.Length -gt 4096 -or $source.Length -gt 32768 -or
    -not $fullyQualified -or [IO.Path]::GetFileName($source) -ine 'nvngx_dlssnr.dll' -or $hash -cnotmatch '^[a-fA-F0-9]{64}$' -or
    ($bytes -isnot [int] -and $bytes -isnot [long] -and $bytes -isnot [uint64] -and $bytes -isnot [decimal]) -or
    $bytes -lt 0 -or $bytes -gt [decimal]([uint64]::MaxValue) -or ($bytes % 1) -ne 0){throw (New-NeuroticCodedError 'desktop.nrdll.invalid_copy' 'The selected NR DLL request is invalid. Choose the file again.')}
}
function Close-NeuroticNrDllSource($Components){
 $lease=Get-NeuroticField (Get-NeuroticField $Components 'neuralModel') 'sourceLease';if($lease){$lease.Dispose()}
}
function Resolve-NeuroticNrDllSource($Components,$Copy){
 Assert-NeuroticNrDllCopy $Copy
 $stream=Get-NeuroticField $Components 'streamline' ([pscustomobject]@{status='Missing';files=@()})
 $missing=[pscustomobject]@{status='Missing';path='';optional=$true}
 if((Get-NeuroticField $Copy 'mode') -cne 'profile'){return [pscustomobject]@{streamline=$stream;neuralModel=$missing}}
 $source=[IO.Path]::GetFullPath((Get-NeuroticField $Copy 'source'));$handle=$null
 try{
  $identity=[NeuRoticSimpleFileIo]::Stat($source);if($identity -eq 'Absent'){throw (New-NeuroticCodedError 'desktop.nrdll.source_unavailable' 'The selected NR DLL is unavailable. Choose another profile or manual installation.')}
  $handle=[IO.File]::Open($source,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  if([NeuRoticSimpleFileIo]::Stat($source) -cne $identity){throw (New-NeuroticCodedError 'desktop.nrdll.source_changed' 'The selected NR DLL changed after it was queued. Choose it again or use manual installation.')}
  $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($handle))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  if($hash -ine (Get-NeuroticField $Copy 'sha256') -or [uint64]$handle.Length -ne [uint64](Get-NeuroticField $Copy 'bytes')){throw (New-NeuroticCodedError 'desktop.nrdll.source_changed' 'The selected NR DLL changed after it was queued. Choose it again or use manual installation.')}
  $info=Get-Item -LiteralPath $source -Force
  $model=[pscustomobject]@{status='Present';path=$source;sha256=$hash;bytes=[uint64]$handle.Length;lastWriteTicks=$info.LastWriteTimeUtc.Ticks;fileIdentity=$identity;sourceLease=$handle;optional=$true}
  $handle=$null;return [pscustomobject]@{streamline=$stream;neuralModel=$model}
 }finally{if($handle){$handle.Dispose()}}
}

# The explicit action writes beside the selected executable/proxy. Custom
# OptiDllPath selects dependency search policy, not an NR model destination.
# This is an on-disk result; it does not identify or hot-swap a loaded provider.
function Get-NeuroticNrDllUpdateTarget([string]$Executable){
 $exe=[IO.Path]::GetFullPath($Executable)
 if([IO.Path]::GetExtension($exe) -ine '.exe'){throw 'Select an actual game executable'}
 [void](Get-NeuroticExecutableArchitecture $exe -RequireExecutable)
 $root=Split-Path -Parent $exe;[void](Resolve-NeuroticOwnedPath $root ([IO.Path]::GetFileName($exe)))
 $path=Resolve-NeuroticOwnedPath $root 'nvngx_dlssnr.dll'
 return [pscustomobject][ordered]@{path='nvngx_dlssnr.dll';identity=[NeuRoticSimpleFileIo]::Stat($path);directoryIdentity=[NeuRoticSimpleFileIo]::DirectoryIdentity($root);executableIdentity=[NeuRoticSimpleFileIo]::Stat($exe)}
}
function Invoke-NeuroticNrDllUpdate([string]$Executable,$NrDllCopy,$Target,[string]$OperationId){
 $result=New-NeuroticSimpleResult;$lock=$null;$source=$null;$pending=$null;$evidence=$null;$intentWritten=$false;$committed=$false
 try{
  Assert-NeuroticNrDllCopy $NrDllCopy
  if((Get-NeuroticField $NrDllCopy 'mode') -cne 'profile'){throw (New-NeuroticCodedError 'desktop.nrdll.choose_copy_profile' 'Choose a saved NR DLL profile to add or update the file.')}
  if($OperationId -cnotmatch '\A[a-f0-9]{32}\z'){throw 'Invalid NR DLL operation identity'}
  $Executable=[IO.Path]::GetFullPath($Executable);$root=Split-Path -Parent $Executable
  $lock=Enter-NeuroticSimpleLock $root
  $currentTarget=Get-NeuroticNrDllUpdateTarget $Executable
  foreach($key in @('path','identity','directoryIdentity','executableIdentity')){
   if((Get-NeuroticField $Target $key) -isnot [string] -or (Get-NeuroticField $Target $key) -cne $currentTarget.$key){throw (New-NeuroticCodedError 'desktop.nrdll.destination_changed' 'The selected destination changed. Review the NR DLL action again.')}
  }
  $source=Resolve-NeuroticNrDllSource $null $NrDllCopy
  $model=$source.neuralModel;$destination=Resolve-NeuroticOwnedPath $root $currentTarget.path
  $read=Read-NeuroticInstallReceipt $root;$componentRead=Read-NeuroticNrDllOwnership $root
  $result.notes=@($read.notes)+@($componentRead.notes)
  $pending=[ordered]@{schemaVersion=1;kind='neurotic-nr-dll-intent';operationId=$OperationId;confirmedOwnership=$false;phase='Prepared';selectedExecutable=[IO.Path]::GetFileName($Executable);destination=$currentTarget;source=[ordered]@{profileId=$NrDllCopy.profileId;profileName=$NrDllCopy.profileName;sha256=$model.sha256;bytes=$model.bytes};observed=$null}
  Save-NeuroticNrDllMetadata $root 'NeuRotic/Installer/NR-DLL-Pending.json' $pending;$intentWritten=$true
  $boundary=[Action[string]]{param($stage)Invoke-NeuroticSimpleBoundary ('NrDllCopy'+$stage) $currentTarget.path}
  $evidence=[NeuRoticSimpleFileIo]::CopyNrDllObserved($model.path,$destination,$currentTarget.identity,$model.fileIdentity,$model.sha256,$boundary)
  $pending.observed=[ordered]@{objectIdentity=$evidence.ObjectIdentity;createdNew=$evidence.CreatedNew;ownershipEstablished=$evidence.OwnershipEstablished;beforeKnown=$evidence.BeforeKnown;afterKnown=$evidence.AfterKnown;beforeSha256=$evidence.BeforeSha256;afterSha256=$evidence.AfterSha256;metadataChanged=$evidence.MetadataChanged;outcomeUnknown=$evidence.OutcomeUnknown;succeeded=$evidence.Succeeded;error=$evidence.Error}
  $pending.phase='CopyObserved'
  if($evidence.Succeeded){$result.copied+=,$currentTarget.path}
  if(-not $evidence.Succeeded){$result.errors+=,[pscustomobject]@{operation='UpdateNrDll';path=$currentTarget.path;reason=$evidence.Error}}
  if($evidence.OwnershipEstablished){
   $file=[pscustomobject]@{path=$currentTarget.path;role='nr-runtime';removeOnUninstall=$true;nrDllObjectId=$evidence.ObjectIdentity}
   $component=[pscustomobject]@{schemaVersion=1;kind='neurotic-nr-dll-ownership';operationId=$OperationId;selectedExecutable=[IO.Path]::GetFileName($Executable);files=@(Merge-NeuroticOwnedFiles (Get-NeuroticField $componentRead.receipt 'files' @()) @($file))}
   Invoke-NeuroticSimpleBoundary 'CommitNrDllOwnership' $currentTarget.path
   # Re-establish the actual object (and the observed bytes when known) before
   # publishing deletion authority. Closing Copy never grants it to a new file.
   $publish=[Action]{Save-NeuroticNrDllOwnership $root $component}
   [NeuRoticSimpleFileIo]::WithNrDllObject($destination,$evidence.ObjectIdentity,$(if($evidence.AfterKnown){$evidence.AfterSha256}else{$null}),$publish)
   $committed=$true;$result|Add-Member -NotePropertyName componentReceipt -NotePropertyValue $component
   if($read.receipt){
    try{
     $read.receipt.files=@(Merge-NeuroticOwnedFiles $read.receipt.files @($file))
     Save-NeuroticInstallReceipt $root $read.receipt;$result.receipt=$read.receipt
    }catch{$result.errors+=,[pscustomobject]@{operation='MergeNrDllOwnership';path='NeuRotic/Installer/Current-Install.json';reason=$_.Exception.Message}}
   }
   Remove-NeuroticNrDllMetadata $root 'NeuRotic/Installer/NR-DLL-Pending.json'
  }else{Save-NeuroticNrDllMetadata $root 'NeuRotic/Installer/NR-DLL-Pending.json' $pending}
  $result|Add-Member -NotePropertyName nrDllUpdate -NotePropertyValue ([pscustomobject]@{operationId=$OperationId;destination=$destination;fileCopied=[bool]$evidence.Succeeded;ownershipCommitted=$committed;runtime='NotAttempted';selectedProfile=$NrDllCopy.profileId;sourceSha256=$model.sha256})
  if($evidence.Succeeded){$result.notes+=,[pscustomobject]@{reasonCode='desktop.nrdll.saved_path';messageParameters=@{path=$destination};reason=('NR DLL saved: '+$destination)}}
  $result.status=$(if($result.errors.Count -or -not $committed){'Partial'}elseif($result.notes.Count){'SucceededWithNotes'}else{'Succeeded'})
 }catch{
  $entry=[pscustomobject]@{operation='UpdateNrDll';path=$(if($Target){Get-NeuroticField $Target 'path' ''}else{''});reason=$_.Exception.Message}
  $error=$_.Exception;while($error){if($error.Data.Contains('NeuroticReasonCode')){$entry|Add-Member -NotePropertyName reasonCode -NotePropertyValue $error.Data['NeuroticReasonCode'];break};$error=$error.InnerException}
  $result.errors+=,$entry;$result.status=$(if($intentWritten){'Partial'}else{'FailedWithoutChanges'})
  if($intentWritten -and $pending -and -not $committed){try{Save-NeuroticNrDllMetadata $root 'NeuRotic/Installer/NR-DLL-Pending.json' $pending}catch{$result.notes+=,'The pending NR DLL record could not be updated. No unconfirmed file will be removed automatically.'}}
 }finally{Close-NeuroticNrDllSource $source;if($lock){$lock.ReleaseMutex();$lock.Dispose()}}
 return $result
}
function Invoke-NeuroticNrDllComponentUninstall([string]$GameRoot,$Component,$Result){
 $remaining=@()
 foreach($file in @($Component.files)){
  try{
   $path=Resolve-NeuroticOwnedPath $GameRoot $file.path
   Invoke-NeuroticSimpleBoundary 'Remove' $file.path
   [NeuRoticSimpleFileIo]::RemoveNrDllObject($path,$file.nrDllObjectId);$Result.removed+=,$file.path
  }catch{$remaining+=,$file;$Result.errors+=,[pscustomobject]@{operation='RemoveNrDll';path=$file.path;reason=$_.Exception.Message}}
 }
 $Component.files=@($remaining)
 if($remaining.Count){Save-NeuroticNrDllOwnership $GameRoot $Component}
 else{Remove-NeuroticNrDllMetadata $GameRoot 'NeuRotic/Installer/NR-DLL-Ownership.json'}
}

function Get-NeuroticInstallActions([string]$GameRoot,$Package,$Profile,[bool]$FreshInstall,$Receipt,$RuntimeComponents,[bool]$InstallInspector=$false){
 $actions=@();$owned=@{};foreach($file in @(Get-NeuroticField $Receipt 'files' @())){$owned[$file.path]=$true}
 $root=[IO.Path]::GetFullPath($Package.root);$manifest=Get-NeuroticField $Package 'manifest';$members=@(Get-NeuroticField $manifest 'files' @())
 if(-not $members.Count -or $members.Count -gt 10000){throw 'Package has no bounded payload inventory'}
 $components=Get-NeuroticField $manifest 'components';$inspector=Get-NeuroticField $components 'characterInspector'
 if(-not $inspector -or $inspector.version -notmatch '^Version [1-9][0-9]{0,6}$' -or $inspector.relativeRoot -cne ('OptiScaler/CharacterInspector/'+$inspector.version)){throw 'Package is missing the required versioned Character Inspector component'}
 $inspectorRoot=$null;$reuseInspector=$false;if($InstallInspector){$inspectorRoot=Resolve-NeuroticOwnedPath $GameRoot $inspector.relativeRoot;$reuseInspector=[IO.Directory]::Exists($inspectorRoot)}
 $stageRelative='OptiScaler/CharacterInspector/.'+$inspector.version+'.installing'
 $seen=@{}
 foreach($file in $members){
  $sourceRelative=ConvertTo-NeuroticRelativePath $file.path
  if(-not $sourceRelative.StartsWith('payload/',[StringComparison]::OrdinalIgnoreCase)){continue}
  $relative=$sourceRelative.Substring(8)
  if($relative -match '(?i)(^|/)(PreparedGuides|NeuRotic\.GpuHost|Prepared)(/|$)|(^|/)ReShade(?:32|64)?\.(dll|ini)$|\.addon(?:32|64)$'){throw "Retired prepared/ReShade payload is unsupported: $relative"}
  if($relative -ieq 'OptiScaler.dll'){$relative=$Profile.proxy}
  $relative=ConvertTo-NeuroticRelativePath $relative;if($seen.ContainsKey($relative)){throw "Duplicate payload destination: $relative"};$seen[$relative]=$true
  # All inventory metadata above is validated even for an unselected component.
  # Only selected payload sources participate in filesystem/identity checks.
  $inspectorMember=$relative.StartsWith($inspector.relativeRoot+'/',[StringComparison]::OrdinalIgnoreCase)
  if($inspectorMember -and -not $InstallInspector){continue}
  $source=Resolve-NeuroticOwnedPath $root $sourceRelative;if(-not [IO.File]::Exists($source)){throw "Package source is missing: $sourceRelative"}
  if($inspectorMember){
   $suffix=$relative.Substring($inspector.relativeRoot.Length+1);$stagePath=$stageRelative+'/'+$suffix
   if(-not $reuseInspector){[void](Resolve-NeuroticOwnedPath $GameRoot $stagePath)}
   # Presence of the ordinary version root is the sole reuse criterion. Do
   # not inspect, hash or refresh its existing member files here.
   $actions+=,[pscustomobject]@{path=$relative;source=$source;sourceIdentity=[NeuRoticSimpleFileIo]::Stat($source);operation=$(if($reuseInspector){'Reuse'}else{'Copy'});configuration=$false;role='inspector';inspectorRoot=$inspector.relativeRoot;stageRoot=$stageRelative;stagePath=$stagePath;owned=$owned.ContainsKey($relative);conflict=$false;identity='Absent';optional=$false}
   continue
  }
  $destination=Resolve-NeuroticOwnedPath $GameRoot $relative;$identity=[NeuRoticSimpleFileIo]::Stat($destination)
  $configuration=$Profile.configurationPaths -contains $relative
  $operation=$(if($configuration -and $identity -ne 'Absent' -and -not $FreshInstall){'Reuse'}else{'Copy'})
  $known=$owned.ContainsKey($relative) -or ($relative -in $script:NeuroticSimpleProxies -and (Test-NeuroticLoader $destination))
  $actions+=,[pscustomobject]@{path=$relative;source=$source;sourceIdentity=[NeuRoticSimpleFileIo]::Stat($source);operation=$operation;configuration=$configuration;role=$(if($configuration){'configuration'}elseif($relative -ieq $Profile.proxy){'proxy'}else{'payload'});owned=$owned.ContainsKey($relative);conflict=($operation -ne 'Reuse' -and $identity -ne 'Absent' -and -not $known);identity=$identity;optional=$false}
 }
 if(-not @($actions|Where-Object{$_.role -eq 'proxy'}).Count){throw 'Package has no native core loader'}
 $core=@($actions|Where-Object{$_.role -eq 'proxy'})[0];if((Get-NeuroticExecutableArchitecture $core.source) -ne 'x64'){throw 'Package native core architecture is incompatible. Use NR Anything.'}
 # A small independent entry point travels with the relative receipt. It needs
 # neither the desktop package nor its old recovery/backup services.
 foreach($name in @('NeuRotic-Uninstall.ps1','NeuRotic-InstallCore.ps1','NeuRotic-InstallReceipt.ps1','NeuRotic-LegacyReceipt.ps1','NeuRotic-InstallDefaults.ps1','NeuRotic-InstallProfiles.ps1','NeuRotic-LoggingPolicy.ps1','NeuRotic-HubSettings.ps1','NeuRotic-ObjectRules.ps1','object-rules-v1.schema.json','NeuRotic-RenderingRequirements.json','NeuRotic-SimpleFileIo.cs','NeuRotic-Game-Uninstall.cmd')){
  $relative=$(if($name -eq 'NeuRotic-Game-Uninstall.cmd'){'Uninstall NeuRotic.cmd'}else{'NeuRotic/Installer/'+$name})
  if($seen.ContainsKey($relative)){throw "Duplicate installer destination: $relative"};$seen[$relative]=$true
  $source=Join-Path $PSScriptRoot $name;$destination=Resolve-NeuroticOwnedPath $GameRoot $relative;$identity=[NeuRoticSimpleFileIo]::Stat($destination)
  $actions+=,[pscustomobject]@{path=$relative;source=$source;sourceIdentity=[NeuRoticSimpleFileIo]::Stat($source);operation='Copy';configuration=$false;role='installer';owned=$owned.ContainsKey($relative);conflict=($identity -ne 'Absent' -and -not $owned.ContainsKey($relative));identity=$identity;optional=$false}
 }
 # Central deposits were accepted during explicit refresh. Installation checks
 # source stat metadata only and never launches a model/cohort verifier.
 $custom=$false;$ini=Resolve-NeuroticOwnedPath $GameRoot 'OptiScaler.ini'
 if(-not $FreshInstall -and [IO.File]::Exists($ini)){
  $document=Read-NeuroticSimpleIni $ini;$section=''
  foreach($line in $document.text -split '\r?\n'){if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}elseif($section -ieq 'Libraries' -and $line -match '^\s*OptiDllPath\s*=\s*([^;#]+)'){$value=$Matches[1].Trim();if($value -and $value -ine 'auto' -and $value -ine 'OptiScaler'){$custom=$true}}}
 }
 $deposits=@();$stream=Get-NeuroticField $RuntimeComponents 'streamline';$model=Get-NeuroticField $RuntimeComponents 'neuralModel'
 if((Get-NeuroticField $stream 'status') -eq 'Present'){foreach($file in @(Get-NeuroticField $stream 'files' @())){$deposits+=,[pscustomobject]@{path=('OptiScaler/streamline/'+$file.path);source=$file.source;bytes=$file.bytes;lastWriteTicks=(Get-NeuroticField $file 'lastWriteTicks');fileIdentity=(Get-NeuroticField $file 'fileIdentity')}}}
 if((Get-NeuroticNrDllTargetState $GameRoot $Receipt) -eq 'Initial' -and (Get-NeuroticField $model 'status') -eq 'Present'){$deposits+=,[pscustomobject]@{path='nvngx_dlssnr.dll';source=$model.path;bytes=(Get-NeuroticField $model 'bytes');lastWriteTicks=(Get-NeuroticField $model 'lastWriteTicks');fileIdentity=(Get-NeuroticField $model 'fileIdentity')}}
 foreach($file in $deposits){
  $relative=ConvertTo-NeuroticRelativePath $file.path;$destination=Resolve-NeuroticOwnedPath $GameRoot $relative
  if($custom){$actions+=,[pscustomobject]@{path=$relative;operation='Omit';note='Custom OptiDllPath retained; place compatible runtime files at that location yourself. No files were written outside this game.'};continue}
  $source=[IO.Path]::GetFullPath($file.source);$info=$null;try{if([NeuRoticSimpleFileIo]::Stat($source) -ne 'Absent'){$info=Get-Item -LiteralPath $source -Force}}catch{}
  if(-not $info -or $null -eq $file.bytes -or -not $file.lastWriteTicks -or -not $file.fileIdentity -or $info.Length -ne $file.bytes -or $info.LastWriteTimeUtc.Ticks -ne $file.lastWriteTicks -or [NeuRoticSimpleFileIo]::Stat($source) -ne $file.fileIdentity){$actions+=,[pscustomobject]@{path=$relative;operation='Omit';note='Runtime deposit changed or has no current acceptance. Refresh runtime files.'};continue}
  $identity=[NeuRoticSimpleFileIo]::Stat($destination)
  $actions+=,[pscustomobject]@{path=$relative;source=$source;sourceIdentity=$file.fileIdentity;operation='Copy';configuration=$false;role='runtime';owned=$owned.ContainsKey($relative);conflict=($identity -ne 'Absent' -and -not $owned.ContainsKey($relative));identity=$identity;optional=$true}
 }
 return @($actions)
}
function Add-NeuroticInstallOwnership([string]$GameRoot,$Receipt,[string]$RelativePath,[string]$Role){
 $file=[pscustomobject]@{path=$RelativePath;removeOnUninstall=$true;role=$Role}
 $Receipt.files=@(Merge-NeuroticOwnedFiles $Receipt.files @($file))
 Save-NeuroticInstallReceipt $GameRoot $Receipt
 # Existing fault injection observes a completed write and durable receipt,
 # never permission to claim a planned destination before its operation.
 Invoke-NeuroticSimpleBoundary 'OwnershipRecorded' $RelativePath
}
# Bind a foreign-file choice to the observed target and occupant, not its name
# alone. Identity uses the existing handle-backed metadata, never a DLL cohort
# or content-hash allowlist. Copy/Rename retain their under-handle checks.
function Get-NeuroticFileReviewIdentity([string]$Executable,[string]$RelativePath,[string]$FileIdentity){
 $root=Split-Path -Parent ([IO.Path]::GetFullPath($Executable));$relative=ConvertTo-NeuroticRelativePath $RelativePath
 return "NRFILE1`n"+[NeuRoticSimpleFileIo]::DirectoryIdentity($root)+"`n"+[NeuRoticSimpleFileIo]::Stat($Executable)+"`n"+$relative.ToLowerInvariant()+"`n"+$FileIdentity
}
function Test-NeuroticFileReviewChoice($Decision,[string]$Identity){
 $action=Get-NeuroticField $Decision 'action' ''
 if($action -eq 'Skip'){return $true}
 $reviewed=Get-NeuroticField $Decision 'reviewedIdentity' ''
 return $action -eq 'Replace' -and $reviewed -is [string] -and $reviewed -ceq $Identity
}
function Invoke-NeuroticSimpleInstall([string]$Executable,$Package,[string]$ProxyName,[bool]$FreshInstall,$FileDecisions,[string]$ExistingProxyAction,$RuntimeComponents,$GameIdentity,$InstallDefaults=$null,$NrDllCopy=$null,[bool]$InstallInspector=$false){
 $result=New-NeuroticSimpleResult;$lock=$null;$mutated=$false;$selectedComponents=$null
 try{
  $Executable=[IO.Path]::GetFullPath($Executable);if([IO.Path]::GetExtension($Executable) -ine '.exe'){throw 'Select an actual game executable'}
  $root=Split-Path -Parent $Executable;[void](Resolve-NeuroticOwnedPath $root ([IO.Path]::GetFileName($Executable)))
  $profile=Get-NeuroticInstallProfile $Executable $Package $GameIdentity
  $lock=Enter-NeuroticSimpleLock $root;$read=Read-NeuroticInstallReceipt $root;$old=$read.receipt
  $nrComponent=Read-NeuroticNrDllOwnership $root;$componentFiles=@(Get-NeuroticConfirmedNrDllFiles $root $nrComponent.receipt)
  $result.notes=@($read.notes)+@($nrComponent.notes)+@($profile.limitations)
  if(-not $ProxyName){$ProxyName=Get-NeuroticField $old 'proxy';if(-not $ProxyName){$ProxyName=$profile.proxy}}
  if($ProxyName -notin $script:NeuroticSimpleProxies){throw 'Choose a supported proxy filename'};$profile.proxy=$ProxyName
  # Decide under the installer mutex before reading a managed source. An
  # initial target that became an update preserves its current/missing DLL.
  $nrState=Get-NeuroticNrDllTargetState $root $old
  if($nrState -eq 'Existing'){$result.notes+=,[pscustomobject]@{reasonCode='desktop.nrdll.update_preserved';messageParameters=@{};reason='The existing installation was updated without changing its NR DLL. The selected profile applies only to a new installation.'}}
  elseif($nrState -eq 'Uncertain'){$result.notes+=,[pscustomobject]@{reasonCode='desktop.nrdll.target_uncertain';messageParameters=@{};reason='This folder may already contain a NeuRotic installation, so its NR DLL was left unchanged. You can place your chosen nvngx_dlssnr.dll beside the game executable.'}}
  if($null -ne $NrDllCopy){
   Assert-NeuroticNrDllCopy $NrDllCopy
   $effectiveCopy=$(if($nrState -eq 'Initial'){$NrDllCopy}else{[pscustomobject]@{mode='preserve'}})
   $selectedComponents=Resolve-NeuroticNrDllSource $RuntimeComponents $effectiveCopy;$RuntimeComponents=$selectedComponents

  }
  $actions=@(Get-NeuroticInstallActions $root $Package $profile $FreshInstall $old $RuntimeComponents $InstallInspector)
  $setup=Get-NeuroticAutomaticSetup $profile $actions
  # Eligibility uses the pre-write identity, including explicit Fresh Install.
  $defaultsEligible=@($actions|Where-Object{$_.path -ieq 'OptiScaler.ini' -and $_.identity -eq 'Absent'}).Count -eq 1
  if($null -eq $InstallDefaults){$InstallDefaults=Read-NeuroticInstallDefaults}
  # Fresh retains package-default behavior for the existing NR/style/MFG
  # preferences. Only the newly requested diagnostic defaults join Fresh.
  $effectiveDefaults=$InstallDefaults;$diagnosticFresh=$FreshInstall -and -not $defaultsEligible
  if($diagnosticFresh -and $InstallDefaults.status -eq 'Loaded'){
   $diagnosticPreferences=[ordered]@{schemaVersion=1}
   foreach($key in @('logToFile','logLevel')){
    if($InstallDefaults.preferences.PSObject.Properties.Name -ccontains $key -or ($InstallDefaults.preferences -is [Collections.IDictionary] -and $InstallDefaults.preferences.Contains($key))){$diagnosticPreferences[$key]=Get-NeuroticField $InstallDefaults.preferences $key}
   }
   $effectiveDefaults=ConvertTo-NeuroticInstallDefaults ([pscustomobject]$diagnosticPreferences)
  }
  $defaultsApplyEligible=$defaultsEligible -or $diagnosticFresh
  $resolved=Resolve-NeuroticInstallDefaults $setup.requirements $false $effectiveDefaults $profile.iniPatches $defaultsApplyEligible
  $defaultsEvidence=[pscustomobject]@{eligible=$defaultsApplyEligible;scope=$(if($defaultsEligible){'AllSavedDefaults'}elseif($diagnosticFresh){'DiagnosticDefaultsOnly'}else{'None'});applied=$false;sourceStatus=$InstallDefaults.status;settings=@();overrides=@($resolved.overrides)}
  $result|Add-Member -NotePropertyName installDefaults -NotePropertyValue $defaultsEvidence
  if($defaultsApplyEligible -and $InstallDefaults.note){
   $noteCode=Get-NeuroticField $InstallDefaults 'noteReasonCode'
   if($noteCode -ceq 'desktop.installdefaults.package_defaults_fallback'){
    $result.notes+=,[pscustomobject]@{reasonCode=$noteCode;messageParameters=(Get-NeuroticField $InstallDefaults 'noteMessageParameters' @{});reason=$InstallDefaults.note}
   }else{$result.notes+=,$InstallDefaults.note}
  }
  $decisions=@{};$reviewChoices=@{}
  foreach($file in @($FileDecisions)){if($null -eq $file){continue};$path=ConvertTo-NeuroticRelativePath $file.path;if($file.action -notin @('Replace','Skip','Cancel') -or $decisions.ContainsKey($path)){throw "Invalid file decision: $path"};$decisions[$path]=$file.action;$reviewChoices[$path]=$file}
  if($ExistingProxyAction -eq 'Cancel' -or $decisions.Values -contains 'Cancel'){$result.status='Cancelled';return $result}
  $rename=Get-NeuroticField $old 'reshadeRename'
  $keepReShade=$ExistingProxyAction -eq 'RenameReShade' -and $decisions[$ProxyName] -ne 'Skip'
  if($keepReShade){
   if($ProxyName -ine 'dxgi.dll'){throw 'Keep ReShade applies only to dxgi.dll'}
   $dxgi=Resolve-NeuroticOwnedPath $root 'dxgi.dll';$reshade=Resolve-NeuroticOwnedPath $root 'ReShade64.dll'
   if(-not [IO.File]::Exists($dxgi)){throw 'dxgi.dll is missing; Keep ReShade cannot rename it'}
   if(Test-Path -LiteralPath $reshade){throw 'ReShade64.dll is occupied. Choose Replace, Skip or Cancel for dxgi.dll.'}
   $rename=[pscustomobject]@{from='dxgi.dll';to='ReShade64.dll'}
  }
  # Recheck under the installer mutex before any receipt or destination write.
  # A legacy proxy choice cannot substitute for this file's reviewed identity.
  foreach($action in $actions){
   if($action.operation -eq 'Omit'){$result.notes+=,$action.note;continue}
   if($action.conflict -or ($keepReShade -and $action.path -ieq $ProxyName)){
    $reviewedIdentity=Get-NeuroticFileReviewIdentity $Executable $action.path $action.identity
    if(-not(Test-NeuroticFileReviewChoice $reviewChoices[$action.path] $reviewedIdentity)){$result.conflicts+=,[pscustomobject]@{path=$action.path;reviewedIdentity=$reviewedIdentity;reason=($action.path+' appears to not have come from NeuRotic. How should we proceed?')}}
   }
  }
  if($result.conflicts.Count){$result|Add-Member -NotePropertyName canKeepReShade -NotePropertyValue ($ProxyName -ieq 'dxgi.dll' -and -not(Test-Path -LiteralPath (Resolve-NeuroticOwnedPath $root 'ReShade64.dll')));$result.notes+=,'Choose Replace, Skip or Cancel for each unfamiliar file.';return $result}
  $logging=New-NeuroticLoggingPolicy $Package
  if($logging.profile){$result|Add-Member -NotePropertyName loggingPolicy -NotePropertyValue $logging.evidence}
  Prepare-NeuroticLoggingPolicy $logging $root $actions $rename
  if($logging.evidence.status -eq 'UnknownProfile'){$result.notes+='Diagnostic logging was left unchanged because this package requests an unknown logging policy.'}
  # Plans are not removal authority. Retain confirmed prior/component ownership;
  # each completed copy/acquisition publishes its own entry below.
  foreach($action in $actions){if($action.operation -eq 'Omit'){continue};if($action.conflict -and $decisions[$action.path] -eq 'Skip'){$result.skipped+=,$action.path;continue};if($action.operation -eq 'Reuse'){$result.reused+=,$action.path}}
  $receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';status='Partial';selectedExecutable=[IO.Path]::GetFileName($Executable);architecture=$profile.architecture;packageId=$Package.id;proxy=$ProxyName;profileId=$profile.profileId;files=@(Merge-NeuroticOwnedFiles (@(Get-NeuroticField $old 'files' @())+@($componentFiles)) @())}
  $receipt|Add-Member -NotePropertyName autoSetup -NotePropertyValue $setup
  $receipt|Add-Member -NotePropertyName installDefaults -NotePropertyValue $defaultsEvidence
  if($logging.profile){$receipt|Add-Member -NotePropertyName loggingPolicy -NotePropertyValue $logging.evidence}
  if($rename){$receipt|Add-Member -NotePropertyName reshadeRename -NotePropertyValue $rename}
  Save-NeuroticInstallReceipt $root $receipt;$mutated=$true;$result.receipt=$receipt
  # The real install receipt now durably owns the confirmed component objects.
  # Preserve a sidecar containing any object that could not be re-established.
  if($nrComponent.receipt -and $componentFiles.Count -eq @($nrComponent.receipt.files).Count){Remove-NeuroticNrDllMetadata $root 'NeuRotic/Installer/NR-DLL-Ownership.json'}
  if($keepReShade){
   $renameAction=@($actions|Where-Object{$_.path -ieq 'dxgi.dll'})[0]
   if([NeuRoticSimpleFileIo]::Stat($dxgi) -ne $renameAction.identity){throw 'dxgi.dll changed; choose Keep ReShade again.'}
   Invoke-NeuroticSimpleBoundary 'Rename' 'ReShade64.dll';[NeuRoticSimpleFileIo]::Rename($dxgi,$reshade,$renameAction.identity)
  }
  $workerActions=@($actions|Where-Object{$_.operation -eq 'Copy' -and $_.role -eq 'inspector'})
  if($workerActions.Count){Invoke-NeuroticInspectorInstall $root $workerActions $old $receipt $result}
  foreach($action in $actions){
   if($action.operation -in @('Reuse','Omit') -or $action.role -eq 'inspector' -or $result.skipped -contains $action.path){continue}
   try{
    $destination=Resolve-NeuroticOwnedPath $root $action.path;Invoke-NeuroticSimpleBoundary 'Copy' $action.path
    $expected=$(if($action.conflict){$action.identity}elseif(-not $action.owned -and $action.identity -eq 'Absent'){'Absent'}else{$null})
    if($keepReShade -and $action.path -ieq 'dxgi.dll'){$expected='Absent'}
    if($null -eq $expected -and -not [IO.File]::Exists($destination)){$expected='Absent'}
    [NeuRoticSimpleFileIo]::Copy($action.source,$destination,$expected,$action.sourceIdentity)
    Add-NeuroticInstallOwnership $root $receipt $action.path $action.role
    if($action.configuration -and $action.path -ieq 'OptiScaler.ini'){
     $document=Read-NeuroticSimpleIni $destination;$patches=@($resolved.patches)
     if($rename){$patches+=,[pscustomobject]@{section='Plugins';key='LoadReshade';value='true';policy='routeRequired'}}
     $text=Update-NeuroticProfileIni $document.text $patches $true
     if($text -cne $document.text){[NeuRoticSimpleFileIo]::Write([byte[]]($document.bom+$document.encoding.GetBytes($text)),$destination)}
     if($defaultsApplyEligible -and $effectiveDefaults.status -eq 'Loaded'){
      $global=@((ConvertTo-NeuroticInstallDefaults $effectiveDefaults.preferences).patches)
      $defaultsEvidence.applied=$global.Count -gt 0
      $defaultsEvidence.settings=@(foreach($entry in $global){$winner=@($patches|Where-Object{$_.section -ieq $entry.section -and $_.key -ieq $entry.key})|Select-Object -Last 1;[pscustomobject]@{section=$entry.section;key=$entry.key;requested=$entry.value;effective=$winner.value}})
     }
    }
    $result.copied+=,$action.path
   }catch{
    $errorException=$_.Exception;while($errorException -and -not $errorException.Data.Contains('NeuRoticDestinationOwned')){$errorException=$errorException.InnerException}
    $result.errors+=,[pscustomobject]@{operation='Copy';path=$action.path;reason=$_.Exception.Message}
    if($errorException -and $errorException.Data['NeuRoticDestinationOwned']){Add-NeuroticInstallOwnership $root $receipt $action.path $action.role}
   }
  }
  # Prepare fresh NR settings with NR OFF after every selected required file
  # is delivered by this operation. Reused NR preferences remain unchanged;
  # internal diagnostic logging is applied separately below.
  if($setup.state -eq 'ReadyForRuntime'){
   $required=@($setup.requirements.requiredRuntimeFiles)+@($ProxyName,'OptiScaler.ini')
   $failed=@($required|Where-Object{$result.copied -notcontains $_ -and $result.reused -notcontains $_})
   $blocking=@();foreach($error in $result.errors){$a=@($actions|Where-Object{$_.path -ieq $error.path});if(-not $a.Count -or -not $a[0].optional -or $required -contains $error.path){$blocking+=,$error}}
   $blockingSkips=@();foreach($skip in $result.skipped){$a=@($actions|Where-Object{$_.path -ieq $skip});if(-not $a.Count -or -not $a[0].optional -or $required -contains $skip){$blockingSkips+=,$skip}}
   if($blocking.Count -or $failed.Count -or $blockingSkips.Count){$setup.state='BlockedDelivery';$setup.reasonCode='desktop.automatic_setup.delivery_incomplete';$setup.reason='Automatic Neural Rendering was not enabled because installation did not complete. Retry the installation.'}
   elseif($result.copied -contains 'OptiScaler.ini'){
    try{$destination=Resolve-NeuroticOwnedPath $root 'OptiScaler.ini';Invoke-NeuroticSimpleBoundary 'Configure' 'OptiScaler.ini';$document=Read-NeuroticSimpleIni $destination;$final=Resolve-NeuroticInstallDefaults $setup.requirements $false $InstallDefaults $profile.iniPatches $defaultsEligible;$patches=@($final.patches);if($rename){$patches+=,[pscustomobject]@{section='Plugins';key='LoadReshade';value='true';policy='routeRequired'}};$text=Update-NeuroticProfileIni $document.text $patches $true;[NeuRoticSimpleFileIo]::Write([byte[]]($document.bom+$document.encoding.GetBytes($text)),$destination)}
    catch{$setup.state='BlockedDelivery';$setup.reasonCode='desktop.automatic_setup.configuration_failed';$setup.reason='Automatic Neural Rendering configuration could not be saved. Retry the installation.';$result.errors+=,[pscustomobject]@{operation='Configure';path='OptiScaler.ini';reason=$_.Exception.Message}}
   }else{$setup.state='PreservedPreferences';$setup.reasonCode='desktop.automatic_setup.preferences_preserved';$setup.reason='Existing Neural Rendering preferences were preserved. Runtime input and output safety will be validated in the game.'}
  }
  # Preserve existing rendering preferences; only explicit ReShade and the
  # selected internal logging policy may change their respective keys.
  if($result.reused -contains 'OptiScaler.ini'){
   if($setup.state -eq 'BlockedDelivery'){$setup|Add-Member -NotePropertyName preferencesPreserved -NotePropertyValue $true -Force;$setup.reason+=' Existing Neural Rendering preferences were preserved.'}
   else{$setup.state='PreservedPreferences';$setup.reasonCode='desktop.automatic_setup.preferences_runtime_preserved';$setup.reason='Existing Neural Rendering preferences were preserved. Runtime files, input and output safety will be validated in the game.'}
  }
  if($rename -and $result.reused -contains 'OptiScaler.ini' -and $logging.profile -cne 'internal-testing'){
   try{$destination=Resolve-NeuroticOwnedPath $root 'OptiScaler.ini';$document=Read-NeuroticSimpleIni $destination;$text=Update-NeuroticProfileIni $document.text @([pscustomobject]@{section='Plugins';key='LoadReshade';value='true';policy='routeRequired'}) $false;if($text -cne $document.text){[NeuRoticSimpleFileIo]::Write([byte[]]($document.bom+$document.encoding.GetBytes($text)),$destination);Add-NeuroticInstallOwnership $root $receipt 'OptiScaler.ini' 'configuration'}}
   catch{$result.errors+=,[pscustomobject]@{operation='Configure';path='OptiScaler.ini';reason=$_.Exception.Message}}
  }
  # Internal testing changes only the two explicit Log keys, independently of
  # model/default readiness. Reused INIs use the preflight revision+identity;
  # an explicit ReShade requirement joins this same guarded write.
  $loggingOwnership=$null
  if($rename -and $result.reused -contains 'OptiScaler.ini'){
   $loggingOwnership={param($commitRoot,$commitReceipt) Add-NeuroticInstallOwnership $commitRoot $commitReceipt 'OptiScaler.ini' 'configuration'}
  }
  try{Invoke-NeuroticLoggingPolicy $logging $root $result $loggingOwnership $receipt}
  catch{$result.errors+=,[pscustomobject]@{operation='ConfigureLogging';path='OptiScaler.ini';reason=$_.Exception.Message}}
  # A prior failed transition already records the requested new name. Keep the
  # former loader in ownership intent and retire it on this successful retry.
  if($result.copied -contains $ProxyName){foreach($oldProxy in @($receipt.files|Where-Object{$_.role -eq 'proxy' -and $_.path -ine $ProxyName -and $_.path -in $script:NeuroticSimpleProxies}|ForEach-Object{$_.path})){
   try{$oldPath=Resolve-NeuroticOwnedPath $root $oldProxy
    if($rename -and $oldProxy -ieq 'dxgi.dll' -and [IO.File]::Exists($oldPath) -and -not(Test-NeuroticLoader $oldPath)){$result.notes+=,'The foreign dxgi.dll was preserved during the proxy change.'}
    else{Invoke-NeuroticSimpleBoundary 'Remove' $oldProxy;[NeuRoticSimpleFileIo]::Remove($oldPath);$result.removed+=,$oldProxy}
    $receipt.files=@($receipt.files|Where-Object{$_.path -ine $oldProxy})}
   catch{$result.errors+=,[pscustomobject]@{operation='Remove';path=$oldProxy;reason=$_.Exception.Message}}
  }}
  if($rename -and $ProxyName -ine 'dxgi.dll' -and $result.copied -contains $ProxyName){Complete-NeuroticRecordedRename $root $receipt $result 'Install'}
  $receipt.status=$(if($result.errors.Count -or $result.skipped.Count){'Partial'}else{'Installed'});Save-NeuroticInstallReceipt $root $receipt
  $result.status=$(if($result.errors.Count){'Partial'}elseif($result.skipped.Count -or $result.notes.Count){'SucceededWithNotes'}else{'Succeeded'})
 }catch{$entry=[pscustomobject]@{operation='Install';path='';reason=$_.Exception.Message};$error=$_.Exception
  while($error){if($error.Data.Contains('NeuroticReasonCode')){$entry|Add-Member -NotePropertyName reasonCode -NotePropertyValue $error.Data['NeuroticReasonCode'];break};$error=$error.InnerException}
  $result.errors+=,$entry;$result.status=$(if($mutated){'Partial'}else{'FailedWithoutChanges'})}
 finally{Close-NeuroticNrDllSource $selectedComponents;if($lock){$lock.ReleaseMutex();$lock.Dispose()}}
 return $result
}
function Invoke-NeuroticInspectorInstall([string]$GameRoot,$Actions,$OldReceipt,$Receipt,$Result){
 $stageRoot=$Actions[0].stageRoot;$versionRoot=$Actions[0].inspectorRoot
 $published=$false;$action=$null
 try{
  $stage=Resolve-NeuroticOwnedPath $GameRoot $stageRoot;$destination=Resolve-NeuroticOwnedPath $GameRoot $versionRoot
  $previousStage=@(Get-NeuroticField $OldReceipt 'files' @()|Where-Object{$_.role -eq 'inspector-staging' -and $_.path.StartsWith($stageRoot+'/',[StringComparison]::OrdinalIgnoreCase)})
  foreach($file in $previousStage){
   [NeuRoticSimpleFileIo]::Remove((Resolve-NeuroticOwnedPath $GameRoot $file.path))
   $Receipt.files=@($Receipt.files|Where-Object{$_.path -ine $file.path});Save-NeuroticInstallReceipt $GameRoot $Receipt
  }
  Remove-NeuroticEmptyParents $GameRoot @($previousStage.path)
  if([IO.Directory]::Exists($stage) -and @(Get-ChildItem -LiteralPath $stage -Force).Count){throw 'Inspector staging contains unfamiliar or inaccessible files. Move those files before retrying.'}
  foreach($action in $Actions){
   Invoke-NeuroticSimpleBoundary 'Copy' $action.stagePath
   [NeuRoticSimpleFileIo]::Copy($action.source,(Resolve-NeuroticOwnedPath $GameRoot $action.stagePath),'Absent',$action.sourceIdentity)
   Add-NeuroticInstallOwnership $GameRoot $Receipt $action.stagePath 'inspector-staging'
  }
  Invoke-NeuroticSimpleBoundary 'Publish' $versionRoot;[NeuRoticSimpleFileIo]::RenameDirectory($stage,$destination);$published=$true
  $Receipt.files=@($Receipt.files|Where-Object{-not $_.path.StartsWith($stageRoot+'/',[StringComparison]::OrdinalIgnoreCase)})
  $confirmed=@(foreach($member in $Actions){[pscustomobject]@{path=$member.path;removeOnUninstall=$true;role='inspector'}})
  $Receipt.files=@(Merge-NeuroticOwnedFiles $Receipt.files $confirmed);Save-NeuroticInstallReceipt $GameRoot $Receipt
  $Result.copied+=@($Actions.path)
 }catch{
  $errorException=$_.Exception;while($errorException -and -not $errorException.Data.Contains('NeuRoticDestinationOwned')){$errorException=$errorException.InnerException}
  $Result.errors+=,[pscustomobject]@{operation='Install Inspector';path=$versionRoot;reason=$_.Exception.Message}
  if(-not $published -and $action -and $errorException -and $errorException.Data['NeuRoticDestinationOwned']){Add-NeuroticInstallOwnership $GameRoot $Receipt $action.stagePath 'inspector-staging'}
 }
}
function Complete-NeuroticRecordedRename([string]$GameRoot,$Receipt,$Result,[string]$Context='Uninstall'){
 if(-not(Get-NeuroticField $Receipt 'reshadeRename')){return}
 try{
  $from=Resolve-NeuroticOwnedPath $GameRoot 'dxgi.dll';$to=Resolve-NeuroticOwnedPath $GameRoot 'ReShade64.dll'
  if(Test-Path -LiteralPath $from){
   $note=$(if($Context -eq 'Uninstall'){'NeuRotic removed. ReShade was left as ReShade64.dll because dxgi.dll is occupied.'}else{'ReShade was left as ReShade64.dll because dxgi.dll is occupied.'})
   $Result.notes+=,$note;return
  }
  if(-not [IO.File]::Exists($to)){$Result.notes+=,'ReShade64.dll is missing; the recorded ReShade rename could not be returned to dxgi.dll.';return}
  Invoke-NeuroticSimpleBoundary 'Rename' 'dxgi.dll';[NeuRoticSimpleFileIo]::Rename($to,$from,$null)
  $Receipt.PSObject.Properties.Remove('reshadeRename')
 }catch{$Result.notes+=,('ReShade could not return to dxgi.dll: '+$_.Exception.Message)}
}
function Remove-NeuroticEmptyParents([string]$GameRoot,$Paths){
 $directories=@{};foreach($relative in @($Paths)){
  if(-not $relative){continue}
  $parent=Split-Path -Parent $relative
  while($parent){$directories[$parent]=$true;$parent=Split-Path -Parent $parent}
 }
 foreach($relative in @($directories.Keys|Sort-Object Length -Descending)){
  try{$path=Resolve-NeuroticOwnedPath $GameRoot $relative;if([IO.Directory]::Exists($path) -and -not @(Get-ChildItem -LiteralPath $path -Force).Count){[IO.Directory]::Delete($path,$false)}}catch{}
 }
}
function Invoke-NeuroticSimpleUninstall([string]$GameRoot){
 $result=New-NeuroticSimpleResult;$lock=$null;$mutated=$false
 try{
  $GameRoot=[IO.Path]::GetFullPath($GameRoot);$lock=Enter-NeuroticSimpleLock $GameRoot
  $read=Read-NeuroticInstallReceipt $GameRoot;$receipt=$read.receipt
  $componentRead=Read-NeuroticNrDllOwnership $GameRoot;$component=$componentRead.receipt
  $result.notes=@($read.notes)+@($componentRead.notes)
  $pendingPath=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/NR-DLL-Pending.json'
  if(Test-Path -LiteralPath $pendingPath){$result.notes+='An unfinished NR DLL action was preserved. Pending intent does not authorize removing an unconfirmed file.'}
  if(-not $receipt -and $component){
   $mutated=$true;Invoke-NeuroticNrDllComponentUninstall $GameRoot $component $result
   Remove-NeuroticEmptyParents $GameRoot (@($component.files.path)+@('NeuRotic/Installer/NR-DLL-Ownership.json'))
   $result.status=$(if($result.errors.Count){'Partial'}elseif($result.notes.Count){'SucceededWithNotes'}else{'Succeeded'});return $result
  }
  if(-not $receipt){
   . (Join-Path $PSScriptRoot 'NeuRotic-LegacyReceipt.ps1');$fallback=Get-NeuroticRecognizableLegacyReceipt $GameRoot;$receipt=$fallback.receipt;$result.notes+=@($fallback.notes)
   if(-not $receipt){$result.status='SucceededWithNotes';$result.notes+=,'No recognizable NeuRotic files were found. Ambiguous files were left alone.';return $result}
  }
  if($component){$receipt.files=@(Merge-NeuroticOwnedFiles $receipt.files $component.files)}
  $remaining=@();$paths=@($receipt.files.path)
  $retryTools=@($receipt.files|Where-Object{$_.removeOnUninstall -and $_.role -eq 'installer'})
  foreach($file in $receipt.files){
   if(-not $file.removeOnUninstall -or $file.role -eq 'installer'){continue}
   try{
    $path=Resolve-NeuroticOwnedPath $GameRoot $file.path
    # Narrow ReShade exception: an edited/unrecognizable DXGI occupant is
    # preserved while all other recorded owned files are removed normally.
    if((Get-NeuroticField $receipt 'reshadeRename') -and $file.path -ieq 'dxgi.dll' -and [IO.File]::Exists($path) -and -not(Test-NeuroticLoader $path)){continue}
    Invoke-NeuroticSimpleBoundary 'Remove' $file.path
    $nrObject=Get-NeuroticField $file 'nrDllObjectId'
    if($nrObject){[NeuRoticSimpleFileIo]::RemoveNrDllObject($path,$nrObject)}else{[NeuRoticSimpleFileIo]::Remove($path)}
    $result.removed+=,$file.path;$mutated=$true
   }catch{$remaining+=,$file;$result.errors+=,[pscustomobject]@{operation='Remove';path=$file.path;reason=$_.Exception.Message}}
  }
  $mutated=$true;Complete-NeuroticRecordedRename $GameRoot $receipt $result
  if(-not $remaining.Count -and -not(Get-NeuroticField $receipt 'reshadeRename')){
   try{
    $toolPaths=@();foreach($file in $retryTools){$toolPaths+=,(Resolve-NeuroticOwnedPath $GameRoot $file.path);Invoke-NeuroticSimpleBoundary 'Remove' $file.path}
    # The receipt is part of the retry dependency group too. A locked receipt
    # must not leave ownership bookkeeping behind after deleting its reader.
    $toolPaths+=,(Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/Current-Install.json')
    [NeuRoticSimpleFileIo]::RemoveTogether([string[]]$toolPaths);$result.removed+=@($retryTools.path)
   }catch{
    $remaining+=@($retryTools);$failedPath=$(if($file){$file.path}else{'NeuRotic/Installer'})
    $detail=$_.Exception;while($detail -and -not $detail.Data.Contains('NeuRoticPath')){$detail=$detail.InnerException}
    if($detail -and $detail.Data['NeuRoticPath']){$failedPath=$detail.Data['NeuRoticPath'].Substring($GameRoot.TrimEnd('\','/').Length+1).Replace('\','/')}
    $result.errors+=,[pscustomobject]@{operation='Remove';path=$failedPath;reason=$_.Exception.Message}
   }
  }else{$remaining+=@($retryTools)}
  $receipt.files=@($remaining);$receipt.status='Partial'
  if($remaining.Count -or (Get-NeuroticField $receipt 'reshadeRename')){$result.receipt=$receipt;Save-NeuroticInstallReceipt $GameRoot $receipt}
  if($component){
   $component.files=@($component.files|Where-Object{$result.removed -notcontains $_.path})
   if($component.files.Count){Save-NeuroticNrDllOwnership $GameRoot $component}
   else{Remove-NeuroticNrDllMetadata $GameRoot 'NeuRotic/Installer/NR-DLL-Ownership.json'}
  }
  Remove-NeuroticEmptyParents $GameRoot (@($paths)+@('NeuRotic/Installer/Current-Install.json','NeuRotic/Installer/NR-DLL-Ownership.json'))
  $result.status=$(if($result.errors.Count){'Partial'}elseif($result.notes.Count){'SucceededWithNotes'}else{'Succeeded'})
 }catch{$result.errors+=,[pscustomobject]@{operation='Uninstall';path='';reason=$_.Exception.Message};$result.status=$(if($mutated){'Partial'}else{'FailedWithoutChanges'})}
 finally{if($lock){$lock.ReleaseMutex();$lock.Dispose()}}
 return $result
}
