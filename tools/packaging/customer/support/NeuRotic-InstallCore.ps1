. (Join-Path $PSScriptRoot 'NeuRotic-InstallReceipt.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-InstallProfiles.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-InstallDefaults.ps1')
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
 if(Get-Command Invoke-SimpleFault -ErrorAction SilentlyContinue){Invoke-SimpleFault $Operation $Path}
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
function Get-NeuroticInstallActions([string]$GameRoot,$Package,$Profile,[bool]$FreshInstall,$Receipt,$RuntimeComponents){
 $actions=@();$owned=@{};foreach($file in @(Get-NeuroticField $Receipt 'files' @())){$owned[$file.path]=$true}
 $root=[IO.Path]::GetFullPath($Package.root);$manifest=Get-NeuroticField $Package 'manifest';$members=@(Get-NeuroticField $manifest 'files' @())
 if(-not $members.Count -or $members.Count -gt 10000){throw 'Package has no bounded payload inventory'}
 $components=Get-NeuroticField $manifest 'components';$inspector=Get-NeuroticField $components 'characterInspector'
 if(-not $inspector -or $inspector.version -notmatch '^Version [1-9][0-9]{0,6}$' -or $inspector.relativeRoot -cne ('OptiScaler/CharacterInspector/'+$inspector.version)){throw 'Package is missing the required versioned Character Inspector component'}
 $inspectorRoot=Resolve-NeuroticOwnedPath $GameRoot $inspector.relativeRoot;$reuseInspector=[IO.Directory]::Exists($inspectorRoot)
 $stageRelative='OptiScaler/CharacterInspector/.'+$inspector.version+'.installing'
 $seen=@{}
 foreach($file in $members){
  $sourceRelative=ConvertTo-NeuroticRelativePath $file.path
  if(-not $sourceRelative.StartsWith('payload/',[StringComparison]::OrdinalIgnoreCase)){continue}
  $relative=$sourceRelative.Substring(8)
  if($relative -match '(?i)(^|/)(PreparedGuides|NeuRotic\.GpuHost|Prepared)(/|$)|(^|/)ReShade(?:32|64)?\.(dll|ini)$|\.addon(?:32|64)$'){throw "Retired prepared/ReShade payload is unsupported: $relative"}
  if($relative -ieq 'OptiScaler.dll'){$relative=$Profile.proxy}
  $relative=ConvertTo-NeuroticRelativePath $relative;if($seen.ContainsKey($relative)){throw "Duplicate payload destination: $relative"};$seen[$relative]=$true
  $source=Resolve-NeuroticOwnedPath $root $sourceRelative;if(-not [IO.File]::Exists($source)){throw "Package source is missing: $sourceRelative"}
  if($relative.StartsWith($inspector.relativeRoot+'/',[StringComparison]::OrdinalIgnoreCase)){
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
 foreach($name in @('NeuRotic-Uninstall.ps1','NeuRotic-InstallCore.ps1','NeuRotic-InstallReceipt.ps1','NeuRotic-LegacyReceipt.ps1','NeuRotic-InstallDefaults.ps1','NeuRotic-InstallProfiles.ps1','NeuRotic-RenderingRequirements.json','NeuRotic-SimpleFileIo.cs','NeuRotic-Game-Uninstall.cmd')){
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
 if((Get-NeuroticField $model 'status') -eq 'Present'){$deposits+=,[pscustomobject]@{path='nvngx_dlssnr.dll';source=$model.path;bytes=(Get-NeuroticField $model 'bytes');lastWriteTicks=(Get-NeuroticField $model 'lastWriteTicks');fileIdentity=(Get-NeuroticField $model 'fileIdentity')}}
 foreach($file in $deposits){
  $relative=ConvertTo-NeuroticRelativePath $file.path;$destination=Resolve-NeuroticOwnedPath $GameRoot $relative
  if($custom){$actions+=,[pscustomobject]@{path=$relative;operation='Omit';note='Custom OptiDllPath retained; place compatible runtime files at that location yourself. No files were written outside this game.'};continue}
  $source=[IO.Path]::GetFullPath($file.source);$info=$null;try{if([NeuRoticSimpleFileIo]::Stat($source) -ne 'Absent'){$info=Get-Item -LiteralPath $source -Force}}catch{}
  if(-not $info -or -not $file.bytes -or -not $file.lastWriteTicks -or -not $file.fileIdentity -or $info.Length -ne $file.bytes -or $info.LastWriteTimeUtc.Ticks -ne $file.lastWriteTicks -or [NeuRoticSimpleFileIo]::Stat($source) -ne $file.fileIdentity){$actions+=,[pscustomobject]@{path=$relative;operation='Omit';note='Runtime deposit changed or has no current acceptance. Refresh runtime files.'};continue}
  $identity=[NeuRoticSimpleFileIo]::Stat($destination)
  $actions+=,[pscustomobject]@{path=$relative;source=$source;sourceIdentity=$file.fileIdentity;operation='Copy';configuration=$false;role='runtime';owned=$owned.ContainsKey($relative);conflict=($identity -ne 'Absent' -and -not $owned.ContainsKey($relative));identity=$identity;optional=$true}
 }
 return @($actions)
}
function Invoke-NeuroticSimpleInstall([string]$Executable,$Package,[string]$ProxyName,[bool]$FreshInstall,$FileDecisions,[string]$ExistingProxyAction,$RuntimeComponents,$GameIdentity,$InstallDefaults=$null){
 $result=New-NeuroticSimpleResult;$lock=$null;$mutated=$false
 try{
  $Executable=[IO.Path]::GetFullPath($Executable);if([IO.Path]::GetExtension($Executable) -ine '.exe'){throw 'Select an actual game executable'}
  $root=Split-Path -Parent $Executable;[void](Resolve-NeuroticOwnedPath $root ([IO.Path]::GetFileName($Executable)))
  $profile=Get-NeuroticInstallProfile $Executable $Package $GameIdentity
  if($profile.route -ne 'native-x64'){$result.notes=@($profile.limitations);return $result}
  $lock=Enter-NeuroticSimpleLock $root;$read=Read-NeuroticInstallReceipt $root;$old=$read.receipt;$result.notes=@($read.notes)
  if(-not $ProxyName){$ProxyName=Get-NeuroticField $old 'proxy';if(-not $ProxyName){$ProxyName=$profile.proxy}}
  if($ProxyName -notin $script:NeuroticSimpleProxies){throw 'Choose a supported proxy filename'};$profile.proxy=$ProxyName
  $actions=@(Get-NeuroticInstallActions $root $Package $profile $FreshInstall $old $RuntimeComponents)
  $setup=Get-NeuroticAutomaticSetup $profile $actions
  # Eligibility uses the pre-write identity, including explicit Fresh Install.
  $defaultsEligible=@($actions|Where-Object{$_.path -ieq 'OptiScaler.ini' -and $_.identity -eq 'Absent'}).Count -eq 1
  if($null -eq $InstallDefaults){$InstallDefaults=Read-NeuroticInstallDefaults}
  $resolved=Resolve-NeuroticInstallDefaults $setup.requirements $false $InstallDefaults $profile.iniPatches $defaultsEligible
  $defaultsEvidence=[pscustomobject]@{eligible=$defaultsEligible;applied=$false;sourceStatus=$InstallDefaults.status;settings=@();overrides=@($resolved.overrides)}
  $result|Add-Member -NotePropertyName installDefaults -NotePropertyValue $defaultsEvidence
  if($defaultsEligible -and $InstallDefaults.note){
   $noteCode=Get-NeuroticField $InstallDefaults 'noteReasonCode'
   if($noteCode -ceq 'desktop.installdefaults.package_defaults_fallback'){
    $result.notes+=,[pscustomobject]@{reasonCode=$noteCode;messageParameters=(Get-NeuroticField $InstallDefaults 'noteMessageParameters' @{});reason=$InstallDefaults.note}
   }else{$result.notes+=,$InstallDefaults.note}
  }
  $decisions=@{};foreach($file in @($FileDecisions)){if($null -eq $file){continue};$path=ConvertTo-NeuroticRelativePath $file.path;if($file.action -notin @('Replace','Skip','Cancel') -or $decisions.ContainsKey($path)){throw "Invalid file decision: $path"};$decisions[$path]=$file.action}
  if($ExistingProxyAction -eq 'Cancel' -or $decisions.Values -contains 'Cancel'){$result.status='Cancelled';return $result}
  $rename=Get-NeuroticField $old 'reshadeRename'
  if($ExistingProxyAction -eq 'RenameReShade'){
   if($ProxyName -ine 'dxgi.dll'){throw 'Keep ReShade applies only to dxgi.dll'}
   $dxgi=Resolve-NeuroticOwnedPath $root 'dxgi.dll';$reshade=Resolve-NeuroticOwnedPath $root 'ReShade64.dll'
   if(-not [IO.File]::Exists($dxgi)){throw 'dxgi.dll is missing; Keep ReShade cannot rename it'}
   if(Test-Path -LiteralPath $reshade){throw 'ReShade64.dll is occupied. Choose Replace, Skip or Cancel for dxgi.dll.'}
   $decisions['dxgi.dll']='Replace';$rename=[pscustomobject]@{from='dxgi.dll';to='ReShade64.dll'}
  }elseif($ExistingProxyAction -eq 'Replace'){$decisions[$ProxyName]='Replace'}
  foreach($action in $actions){if($action.operation -eq 'Omit'){$result.notes+=,$action.note;continue};if($action.conflict -and -not $decisions.ContainsKey($action.path)){$result.conflicts+=,[pscustomobject]@{path=$action.path;reason=($action.path+' appears to not have come from NeuRotic. How should we proceed?')}}}
  if($result.conflicts.Count){$result.notes+=,'Choose Replace, Skip or Cancel for each unfamiliar file.';return $result}
  $intended=@();foreach($action in $actions){if($action.operation -eq 'Omit'){continue};if($action.conflict -and $decisions[$action.path] -eq 'Skip'){$result.skipped+=,$action.path;continue};if($action.operation -eq 'Reuse'){$result.reused+=,$action.path;if(-not $action.owned -and -not ($rename -and $action.configuration)){continue}};$intended+=,[pscustomobject]@{path=$action.path;removeOnUninstall=$true;role=$action.role}}
  foreach($action in @($actions|Where-Object{$_.operation -eq 'Copy' -and $_.role -eq 'inspector'})){$intended+=,[pscustomobject]@{path=$action.stagePath;removeOnUninstall=$true;role='inspector-staging'}}
  $receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';status='Partial';selectedExecutable=[IO.Path]::GetFileName($Executable);architecture=$profile.architecture;packageId=$Package.id;proxy=$ProxyName;profileId=$profile.profileId;files=@(Merge-NeuroticOwnedFiles (Get-NeuroticField $old 'files' @()) $intended)}
  $receipt|Add-Member -NotePropertyName autoSetup -NotePropertyValue $setup
  $receipt|Add-Member -NotePropertyName installDefaults -NotePropertyValue $defaultsEvidence
  if($rename){$receipt|Add-Member -NotePropertyName reshadeRename -NotePropertyValue $rename}
  Save-NeuroticInstallReceipt $root $receipt;$mutated=$true;$result.receipt=$receipt
  if($ExistingProxyAction -eq 'RenameReShade'){
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
    if($ExistingProxyAction -eq 'RenameReShade' -and $action.path -ieq 'dxgi.dll'){$expected='Absent'}
    if($null -eq $expected -and -not [IO.File]::Exists($destination)){$expected='Absent'}
    [NeuRoticSimpleFileIo]::Copy($action.source,$destination,$expected,$action.sourceIdentity)
    if($action.configuration -and $action.path -ieq 'OptiScaler.ini'){
     $document=Read-NeuroticSimpleIni $destination;$patches=@($resolved.patches)
     if($rename){$patches+=,[pscustomobject]@{section='Plugins';key='LoadReshade';value='true';policy='routeRequired'}}
     $text=Update-NeuroticProfileIni $document.text $patches $true
     if($text -cne $document.text){[NeuRoticSimpleFileIo]::Write([byte[]]($document.bom+$document.encoding.GetBytes($text)),$destination)}
     if($defaultsEligible -and $InstallDefaults.status -eq 'Loaded'){
      $global=@((ConvertTo-NeuroticInstallDefaults $InstallDefaults.preferences).patches)
      $defaultsEvidence.applied=$global.Count -gt 0
      $defaultsEvidence.settings=@(foreach($entry in $global){$winner=@($patches|Where-Object{$_.section -ieq $entry.section -and $_.key -ieq $entry.key})|Select-Object -Last 1;[pscustomobject]@{section=$entry.section;key=$entry.key;requested=$entry.value;effective=$winner.value}})
     }
    }
    $result.copied+=,$action.path
   }catch{
    $errorException=$_.Exception;while($errorException -and -not $errorException.Data.Contains('NeuRoticDestinationOwned')){$errorException=$errorException.InnerException}
    if(-not $action.owned -and $errorException -and -not $errorException.Data['NeuRoticDestinationOwned']){$receipt.files=@($receipt.files|Where-Object{$_.path -ine $action.path})}
    $result.errors+=,[pscustomobject]@{operation='Copy';path=$action.path;reason=$_.Exception.Message}
   }
  }
  # Prepare fresh NR settings with NR OFF after every selected required file
  # is delivered by this operation. A reused INI remains byte-preserved.
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
  # Existing INI is byte-preserved except the explicit recorded ReShade requirement.
  if($result.reused -contains 'OptiScaler.ini'){
   if($setup.state -eq 'BlockedDelivery'){$setup|Add-Member -NotePropertyName preferencesPreserved -NotePropertyValue $true -Force;$setup.reason+=' Existing Neural Rendering preferences were preserved.'}
   else{$setup.state='PreservedPreferences';$setup.reasonCode='desktop.automatic_setup.preferences_runtime_preserved';$setup.reason='Existing Neural Rendering preferences were preserved. Runtime files, input and output safety will be validated in the game.'}
  }
  if($rename -and $result.reused -contains 'OptiScaler.ini'){
   try{$destination=Resolve-NeuroticOwnedPath $root 'OptiScaler.ini';$document=Read-NeuroticSimpleIni $destination;$text=Update-NeuroticProfileIni $document.text @([pscustomobject]@{section='Plugins';key='LoadReshade';value='true';policy='routeRequired'}) $false;if($text -cne $document.text){[NeuRoticSimpleFileIo]::Write([byte[]]($document.bom+$document.encoding.GetBytes($text)),$destination);$receipt.files=Merge-NeuroticOwnedFiles $receipt.files @([pscustomobject]@{path='OptiScaler.ini';removeOnUninstall=$true;role='configuration'})}}
   catch{$result.errors+=,[pscustomobject]@{operation='Configure';path='OptiScaler.ini';reason=$_.Exception.Message}}
  }
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
 }catch{$result.errors+=,[pscustomobject]@{operation='Install';path='';reason=$_.Exception.Message};$result.status=$(if($mutated){'Partial'}else{'FailedWithoutChanges'})}
 finally{if($lock){$lock.ReleaseMutex();$lock.Dispose()}}
 return $result
}
function Invoke-NeuroticInspectorInstall([string]$GameRoot,$Actions,$OldReceipt,$Receipt,$Result){
 $stageRoot=$Actions[0].stageRoot;$versionRoot=$Actions[0].inspectorRoot
 $written=@{};$oldOwned=@{};foreach($file in @(Get-NeuroticField $OldReceipt 'files' @())){$oldOwned[$file.path]=$true}
 try{
  $stage=Resolve-NeuroticOwnedPath $GameRoot $stageRoot;$destination=Resolve-NeuroticOwnedPath $GameRoot $versionRoot
  $previousStage=@(Get-NeuroticField $OldReceipt 'files' @()|Where-Object{$_.role -eq 'inspector-staging' -and $_.path.StartsWith($stageRoot+'/',[StringComparison]::OrdinalIgnoreCase)})
  foreach($file in $previousStage){[NeuRoticSimpleFileIo]::Remove((Resolve-NeuroticOwnedPath $GameRoot $file.path))}
  Remove-NeuroticEmptyParents $GameRoot @($previousStage.path)
  if([IO.Directory]::Exists($stage) -and @(Get-ChildItem -LiteralPath $stage -Force).Count){throw 'Inspector staging contains unfamiliar or inaccessible files. Move those files before retrying.'}
  foreach($action in $Actions){
   Invoke-NeuroticSimpleBoundary 'Copy' $action.stagePath
   [NeuRoticSimpleFileIo]::Copy($action.source,(Resolve-NeuroticOwnedPath $GameRoot $action.stagePath),'Absent',$action.sourceIdentity)
   $written[$action.stagePath]=$true
  }
  Invoke-NeuroticSimpleBoundary 'Publish' $versionRoot;[NeuRoticSimpleFileIo]::RenameDirectory($stage,$destination)
  $Result.copied+=@($Actions.path);$Receipt.files=@($Receipt.files|Where-Object{-not $_.path.StartsWith($stageRoot+'/',[StringComparison]::OrdinalIgnoreCase)})
 }catch{
  $errorException=$_.Exception;while($errorException -and -not $errorException.Data.Contains('NeuRoticDestinationOwned')){$errorException=$errorException.InnerException}
  if($errorException -and $errorException.Data['NeuRoticDestinationOwned']){$written[$action.stagePath]=$true}
  $Receipt.files=@($Receipt.files|Where-Object{-not $_.path.StartsWith($stageRoot+'/',[StringComparison]::OrdinalIgnoreCase) -or $written.ContainsKey($_.path) -or $oldOwned.ContainsKey($_.path)})
  $Result.errors+=,[pscustomobject]@{operation='Install Inspector';path=$versionRoot;reason=$_.Exception.Message}
  if([IO.Directory]::Exists($destination)){
   # A foreign version folder appeared before publication. Only the new stage
   # belongs to this operation; preserve outside files at the final path.
   $Receipt.files=@($Receipt.files|Where-Object{-not $_.path.StartsWith($versionRoot+'/',[StringComparison]::OrdinalIgnoreCase) -or $oldOwned.ContainsKey($_.path)})
  }
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
  $read=Read-NeuroticInstallReceipt $GameRoot;$receipt=$read.receipt;$result.notes=@($read.notes)
  if(-not $receipt){
   . (Join-Path $PSScriptRoot 'NeuRotic-LegacyReceipt.ps1');$fallback=Get-NeuroticRecognizableLegacyReceipt $GameRoot;$receipt=$fallback.receipt;$result.notes+=@($fallback.notes)
   if(-not $receipt){$result.status='SucceededWithNotes';$result.notes+=,'No recognizable NeuRotic files were found. Ambiguous files were left alone.';return $result}
  }
  $remaining=@();$paths=@($receipt.files.path)
  $retryTools=@($receipt.files|Where-Object{$_.removeOnUninstall -and $_.role -eq 'installer'})
  foreach($file in $receipt.files){
   if(-not $file.removeOnUninstall -or $file.role -eq 'installer'){continue}
   try{
    $path=Resolve-NeuroticOwnedPath $GameRoot $file.path
    # Narrow ReShade exception: an edited/unrecognizable DXGI occupant is
    # preserved while all other recorded owned files are removed normally.
    if((Get-NeuroticField $receipt 'reshadeRename') -and $file.path -ieq 'dxgi.dll' -and [IO.File]::Exists($path) -and -not(Test-NeuroticLoader $path)){continue}
    Invoke-NeuroticSimpleBoundary 'Remove' $file.path;[NeuRoticSimpleFileIo]::Remove($path);$result.removed+=,$file.path;$mutated=$true
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
  Remove-NeuroticEmptyParents $GameRoot (@($paths)+@('NeuRotic/Installer/Current-Install.json'))
  $result.status=$(if($result.errors.Count){'Partial'}elseif($result.notes.Count){'SucceededWithNotes'}else{'Succeeded'})
 }catch{$result.errors+=,[pscustomobject]@{operation='Uninstall';path='';reason=$_.Exception.Message};$result.status=$(if($mutated){'Partial'}else{'FailedWithoutChanges'})}
 finally{if($lock){$lock.ReleaseMutex();$lock.Dispose()}}
 return $result
}
