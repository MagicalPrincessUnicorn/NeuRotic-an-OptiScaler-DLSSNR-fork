# Typed companion entry point owned by the existing Setup engine.
# No executable/script path may be supplied by a request or package manifest.
. (Join-Path $PSScriptRoot 'NeuRotic-HubAntiCheat.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-RuntimeFiles.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubHistory.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubSanitize.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubDumbfire.ps1')
function Read-HubRequestText([string]$Text) {
 if([Text.Encoding]::UTF8.GetByteCount($Text) -gt 6291456){throw 'Request exceeds 6 MiB'}
 if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-HubJson.cs')}
 [NeuRoticHubJson]::ValidateSettingsRequest($Text)
 $request=$Text | ConvertFrom-Json
 $allowed=@('protocolVersion','requestId','kind','gameExecutable','operation','packageId','proxyName','existingProxyAction','existingDxgiAction','uninstallMode','planId','planFingerprint','settings','configRevision','continueWithoutRuntime','adoptModifiedInstall','gameRoot','steamRoot','steamAppId','keepRestoreRecords','sanitizeCategories','cleanupCategories','fileDecisions','verifyFiles','includeInspector')
 foreach($property in $request.PSObject.Properties){if($property.Name -cnotin $allowed){throw "Unknown request field: $($property.Name)"}}
 if($request.protocolVersion -isnot [int] -and $request.protocolVersion -isnot [long]){throw 'Protocol version must be an integer'}
 foreach($name in @('requestId','kind','gameExecutable','operation','packageId','proxyName','existingProxyAction','existingDxgiAction','uninstallMode','planId','planFingerprint','configRevision','gameRoot','steamRoot','steamAppId')){
  if($request.PSObject.Properties.Name -contains $name -and $request.$name -isnot [string]){throw "Request field must be text: $name"}
 }
 foreach($name in @('gameRoot','steamRoot','steamAppId')){if($request.PSObject.Properties.Name -contains $name -and $request.kind -cnotin @('Plan','Inspect','LaunchPreflight')){throw 'Game identity context belongs only to Plan, Inspect or LaunchPreflight'}}
 if($request.PSObject.Properties.Name -contains 'includeInspector' -and ($request.includeInspector -isnot [bool] -or $request.kind -cne 'Plan' -or $request.operation -cnotin @('Install','Update','Repair','ChangeProxy'))){throw 'Inspector inclusion is a Boolean installation choice'}
 if($request.PSObject.Properties.Name -contains 'continueWithoutRuntime' -and $request.continueWithoutRuntime -isnot [bool]){throw 'Runtime decision must be Boolean'}
 if($request.PSObject.Properties.Name -contains 'adoptModifiedInstall'){
  [NeuRoticHubJson]::ValidateBoolean($request.adoptModifiedInstall,'adoptModifiedInstall')
  if($request.kind -cne 'Plan' -or $request.operation -cnotin @('Update','Repair')){throw 'Modified-install adoption is available only in an Update or Repair Plan'}
 }
 if($request.PSObject.Properties.Name -contains 'settings' -and $request.settings -isnot [Array]){throw 'Settings must be an array'}
 if($request.PSObject.Properties.Name -contains 'verifyFiles' -and ($request.kind -cne 'Inspect' -or $request.verifyFiles -isnot [bool])){throw 'File verification is a Boolean inspection choice'}
 if($request.PSObject.Properties.Name -contains 'fileDecisions'){
  if($request.kind -cne 'Plan' -or $request.operation -cnotin @('Install','Update','Repair','ChangeProxy','Uninstall') -or $request.fileDecisions -isnot [Array] -or $request.fileDecisions.Count -gt 10000){throw 'File decisions require an installation or uninstall plan'}
  $seen=@{}
  foreach($file in $request.fileDecisions){
   if($null -eq $file -or @($file.PSObject.Properties).Count -ne 3 -or @($file.PSObject.Properties|Where-Object{$_.Name -cnotin @('path','sha256','action')}).Count){throw 'Invalid file decision fields'}
   if($file.path -isnot [string] -or -not $file.path -or $file.path -match '(^[\\/]|:|(^|[\\/])\.{1,2}([\\/]|$)|[\x00-\x1f])' -or $file.path.EndsWith(' ') -or $file.path.EndsWith('.') -or $file.path.Length -gt 240){throw 'File decision requires a relative file path'}
   $key=$file.path.Replace('/','\');if($seen.ContainsKey($key)){throw 'Duplicate file decision'};$seen[$key]=$true
   if($file.sha256 -isnot [string] -or $file.sha256 -cnotmatch '^[a-fA-F0-9]{64}$' -or $file.action -cnotin @('SaveAndReplace','Replace','Skip','Cancel')){throw 'Invalid file decision identity or action'}
  }
 }
 if($request.PSObject.Properties.Name -contains 'cleanupCategories'){
  if($request.kind -cne 'Plan' -or (Get-HubProperty $request 'operation') -cne 'Uninstall' -or $request.cleanupCategories -isnot [Array] -or $request.cleanupCategories.Count -gt 3){throw 'Optional uninstall cleanup requires an Uninstall plan and at most three categories'}
  $seenCleanup=@{};foreach($category in $request.cleanupCategories){if($category -isnot [string] -or $category -cnotin @('backups','privateModels','screenshots') -or $seenCleanup.ContainsKey($category)){throw 'Invalid or duplicate optional uninstall cleanup category'};$seenCleanup[$category]=$true}
 }
 if($request.PSObject.Properties.Name -contains 'keepRestoreRecords'){
  if(($request.keepRestoreRecords -isnot [int] -and $request.keepRestoreRecords -isnot [long]) -or $request.keepRestoreRecords -lt 1 -or $request.keepRestoreRecords -gt 20 -or $request.kind -cne 'Plan' -or $request.operation -cne 'CompactHistory'){throw 'History retention must be an integer from 1 to 20 in a CompactHistory plan.'}
 }
 if($request.protocolVersion -ne 1 -or $request.kind -cnotin @('Inspect','RestoreDefaults','Plan','Execute','Preflight','LaunchPreflight')){throw 'Unsupported protocol or kind'}
 if($request.kind -eq 'LaunchPreflight' -and (-not(Get-HubProperty $request 'gameExecutable') -or @($request.PSObject.Properties|Where-Object{$_.Name -cnotin @('protocolVersion','requestId','kind','gameExecutable','gameRoot','steamRoot','steamAppId')}).Count)){throw 'Launch preflight accepts only the selected game identity'}
 if($request.kind -eq 'RestoreDefaults'){
  if(@($request.PSObject.Properties|Where-Object{$_.Name -cnotin @('protocolVersion','requestId','kind','gameExecutable','configRevision')}).Count){throw 'Restore Defaults accepts only target and settings revision'}
  if(-not (Get-HubProperty $request 'gameExecutable') -or (Get-HubProperty $request 'configRevision') -notmatch '^[a-fA-F0-9]{64}$'){throw 'Restore Defaults requires the inspected target and settings revision'}
 }
 if($request.kind -eq 'Preflight' -and @($request.PSObject.Properties | Where-Object {$_.Name -cnotin @('protocolVersion','requestId','kind')}).Count){throw 'Preflight does not accept target or mutation fields'}
 if($request.PSObject.Properties.Name -contains 'operation' -and $request.operation -cnotin @('Install','Update','Repair','ChangeProxy','Uninstall','SaveSettings','Restore','CompactHistory','Sanitize','DumbfireInstall','DumbfireRollback')){throw 'Unknown operation'}
 if((Get-HubProperty $request 'operation') -in @('DumbfireInstall','DumbfireRollback')){
  $fields=@('protocolVersion','requestId','kind','operation','gameExecutable','gameRoot','steamRoot','steamAppId')
  if($request.operation -eq 'DumbfireInstall'){$fields+=@('packageId','proxyName');if((Get-HubProperty $request 'packageId') -cne 'flagship-approved' -or -not (Get-HubProperty $request 'proxyName')){throw 'Dumbfire requires the approved package and selected proxy'}}
  if($request.kind -cne 'Plan' -or @($request.PSObject.Properties|Where-Object{$_.Name -cnotin $fields}).Count){throw 'Invalid Dumbfire plan fields'}
 }
 if($request.PSObject.Properties.Name -contains 'sanitizeCategories' -or (Get-HubProperty $request 'operation') -eq 'Sanitize'){
  if($request.kind -cne 'Plan' -or (Get-HubProperty $request 'operation') -cne 'Sanitize'){throw 'Cleanup categories belong only to a Sanitize plan'}
  if(@($request.PSObject.Properties|Where-Object{$_.Name -cnotin @('protocolVersion','requestId','kind','operation','gameExecutable','gameRoot','steamRoot','steamAppId','sanitizeCategories')}).Count){throw 'Sanitize does not accept unrelated mutation fields'}
  Assert-HubSanitizeCategories (Get-HubProperty $request 'sanitizeCategories')
 }
 if($request.PSObject.Properties.Name -contains 'proxyName' -and $request.proxyName -cnotin @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){throw 'Unknown proxy'}
 return $request
}
function Get-HubProperty($Object,[string]$Name,$Default=$null){if($Object.PSObject.Properties.Name -contains $Name){return $Object.$Name};return $Default}
function Get-HubPreflight {
 . (Join-Path $PSScriptRoot 'NeuRotic-Prerequisites.ps1')
 $package=Resolve-HubPackage 'flagship-approved'
 $build=Get-Content -LiteralPath (SafePath $package.root 'support/BUILD-MANIFEST.json') -Raw | ConvertFrom-Json
 $minimum=[version]$build.runtime_prerequisites.visual_cpp_x64_minimum
 $runtime=Get-NeuroticVcRuntimeStatus $minimum
 return [ordered]@{status='PreflightChecked';runtime=[ordered]@{ready=[bool]$runtime.Ready;minimumVersion=$minimum.ToString();downloadUrl='https://aka.ms/vc14/vc_redist.x64.exe';problems=@($runtime.Problems)};components=(Get-NeuroticRuntimeComponents (Split-Path -Parent $PSScriptRoot))}
}
function Get-HubPlanFingerprint($Plan){
 $body=[ordered]@{planId=$Plan.planId;requestedPackageId=(Get-HubProperty $Plan 'requestedPackageId');resolvedPackage=(Get-HubProperty $Plan 'resolvedPackage');runtimeDependencyIdentity=(Get-HubProperty $Plan 'runtimeDependencyIdentity');request=$Plan.request;target=$Plan.target;revision=$Plan.revision;configRevision=$Plan.configRevision;packageDigest=$Plan.packageDigest;expiresUtc=$Plan.expiresUtc;changes=$Plan.changes;recovery=(Get-HubProperty $Plan 'recovery');antiCheat=(Get-HubProperty $Plan 'antiCheat');sanitation=(Get-HubProperty $Plan 'sanitation');dumbfireInfo=(Get-HubProperty $Plan 'dumbfireInfo');uninstallCleanup=(Get-HubProperty $Plan 'uninstallCleanup')}
 return HashBytes ([Text.Encoding]::UTF8.GetBytes(($body|ConvertTo-Json -Depth 16 -Compress)))
}
function Get-HubUserRoot {
 $root=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NeuRotic/HubData-v2'
 if($env:NEUROTIC_HUB_FIXTURE_ROOT){$root=$env:NEUROTIC_HUB_FIXTURE_ROOT}
 if(-not (Test-Path -LiteralPath $root)){New-Item -ItemType Directory -Path $root -Force|Out-Null}
 return $root
}
function Get-HubTarget([string]$Executable,[switch]$MetadataOnly){
 if(-not $Executable -or [IO.Path]::GetExtension($Executable) -ine '.exe'){throw 'Select an actual game executable'}
 $exe=[IO.Path]::GetFullPath($Executable);$game=Split-Path -Parent $exe
 [void](SafePath $game ([IO.Path]::GetFileName($exe)))
 if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Game executable is missing'}
 $stream=[IO.File]::OpenRead($exe)
 try{
  $header=New-Object byte[] 64
  if($stream.Length -lt 128 -or $stream.Read($header,0,64) -ne 64 -or $header[0] -ne 77 -or $header[1] -ne 90){throw 'Invalid executable'}
  $pe=[BitConverter]::ToInt32($header,60)
  if($pe -lt 64 -or $pe -gt $stream.Length-26 -or $pe -gt 16777216){throw 'Invalid executable header offset'}
  $stream.Position=$pe;$header=New-Object byte[] 26
  if($stream.Read($header,0,26) -ne 26 -or [BitConverter]::ToUInt32($header,0) -ne 17744 -or ([BitConverter]::ToUInt16($header,22) -band 8192)){throw 'Select a compatible x86 or x64 game executable'}
  $machine=[BitConverter]::ToUInt16($header,4);$magic=[BitConverter]::ToUInt16($header,24)
  $optionalSize=[BitConverter]::ToUInt16($header,20)
  $bits=0
  if($machine -eq 0x14c -and $magic -eq 0x10b -and $optionalSize -ge 224){$bits=32}
  elseif($machine -eq 0x8664 -and $magic -eq 0x20b -and $optionalSize -ge 240){$bits=64}
  if(-not $bits -or $optionalSize -gt 4096 -or $pe+24+$optionalSize -gt $stream.Length){throw 'Unsupported or malformed executable architecture'}
 }finally{$stream.Dispose()}
 return [pscustomobject]@{executable=$exe;directory=$game;sha256=$(if(-not $MetadataOnly){HashFile $exe}else{$null});bitness=$bits;installationRoute=$(if($bits -eq 32){'prepared-x86'}else{'native-x64'})}
}
function Get-HubInventory([string]$Root){
 $items=New-Object Collections.Generic.List[object]
 $included=@{}
 Show-SetupProgress 'Checking installation inventory'
 # Inspect explicit transaction metadata, then recorded owned targets. User
 # screenshots, mods, models and linked folders are not installation revisions.
 foreach($relative in @('OptiScaler.ini','NeuRotic/Prepared/ReShade.ini','NeuRotic/Installer/Current-Install.json','NeuRotic/Installer/Install-Transaction.json','NeuRotic/Installer/Restore-Transaction.json','NeuRotic/Installer/Uninstall-Transaction.json','NeuRotic/Installer/Cleanup-Pending.json','NeuRotic/Installer/History-Transaction.json')){
  $path=SafePath $Root $relative
  if(Test-Path -LiteralPath $path -PathType Leaf){$items.Add([pscustomobject]@{path=$relative.Replace('/','\');sha256=(HashFile $path);bytes=(Get-Item -LiteralPath $path).Length});$included[$relative.Replace('/','\')]=$true}
 }
 # Fingerprint the active lineage and every live owned target, never retired
 # payload trees. The engine verifies backup bytes before restoring them;
 # append-only updates verify fresh preimages. A revision is not byte trust.
 $statePath=SafePath $Root 'NeuRotic/Installer/Current-Install.json'
 # Recovery owns its journal/cleanup receipt. In an interrupted Full cleanup,
 # manifests can already be removed while Current-Install.json still exists.
 # The manager inventory above binds those receipts; their executor validates
 # remaining snapshot/cleanup bytes. Do not require a still-complete old chain.
 $recovering=[bool](Get-NeuroticPendingTransaction $Root)
 if(-not $recovering -and (Test-Path -LiteralPath $statePath -PathType Leaf)){
  $state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
  if($state.status -ne 'installed-verified'){return @($items|Sort-Object path)}
  Initialize-RestoreRecordLedger $Root $state
  $recordCount=0
  Show-SetupProgress 'Checking active installation records' 0 @($state.restore_chain).Count
  foreach($recordPath in @($state.restore_chain)){
   $recordPath=Assert-PathUnderRoot $Root ([string]$recordPath)
   if($recordPath.Substring($Root.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$'){throw 'Invalid restore record path'}
   $record=Read-InstallRecord $recordPath
   $paths=@($recordPath.Substring($Root.Length+1))
   $names=@{}
   foreach($file in $record.files){Assert-RestoreTarget $file.path $recordPath $names;$paths+=[string]$file.path}
   foreach($relative in $paths){
    if($included.ContainsKey($relative)){continue};$included[$relative]=$true
    $path=SafePath $Root $relative
    if(Test-Path -LiteralPath $path -PathType Leaf){
     if($items.Count -ge 10000){throw 'Installation inventory limit reached'}
     $items.Add([pscustomobject]@{path=$relative;sha256=(HashFile $path);bytes=(Get-Item -LiteralPath $path).Length})
    }
   }
   $recordCount++;Show-SetupProgress 'Checking active installation records' $recordCount @($state.restore_chain).Count
  }
 }
 return @($items | Sort-Object path)
}
function Get-HubCurrentFiles($Target,$State){
 # A library check needs the newest ownership entry for each live file, not
 # the contents of old backup payloads. Recovery validates bytes when consumed.
 $latest=@{};$records=@{};$observed=New-Object Collections.Generic.List[object]
 $chain=@($State.restore_chain|ForEach-Object{[string]$_});[array]::Reverse($chain)
 foreach($path in $chain){
  [void](Assert-PathUnderRoot $Target.directory $path)
  if($path.Substring($Target.directory.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$' -or $records.ContainsKey($path)){throw 'Invalid or duplicate restore record; preserved'}
  $records[$path]=$true;$record=Read-InstallRecord $path
  if($record.status -ne 'installed-verified' -or (Split-Path -Parent $record.game_executable) -ine $Target.directory -or $record.backup -ine (Split-Path -Parent $path)){throw 'Restore record identity or status is invalid'}
  $names=@{}
  foreach($file in $record.files){
   Assert-RestoreTarget $file.path $path $names
   if(-not $latest.ContainsKey($file.path)){$latest[$file.path]=$file}
  }
 }
 foreach($name in @($latest.Keys|Sort-Object)){
  $file=$latest[$name];$path=SafePath $Target.directory $name
  if((Test-Path -LiteralPath $path) -and -not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Directory occupies managed target: $name"}
  $exists=Test-Path -LiteralPath $path -PathType Leaf;$hash=if($exists){HashFile $path}else{$null}
  $expected=-not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
  $observed.Add([pscustomobject]@{path=$name;existed=[bool]$exists;sha256=$hash;changed=($exists -and -not (Test-SettingsTarget $name) -and (-not $expected -or $hash -ne $file.installed_hash));missing=($expected -and -not $exists)})
 }
 return $observed.ToArray()
}
function ConvertTo-HubInspectionSummary($Inspection){
 $state=$null
 if($Inspection.state){
  $state=[ordered]@{}
  foreach($key in @('kind','schema_version','status','product','release_name','commit','game_executable','game_directory','selected_proxy','first_install_utc','last_install_utc','uninstalled_utc','uninstall_mode','uninstall_limitations')){$state[$key]=Get-HubProperty $Inspection.state $key}
 }
 return [ordered]@{target=$Inspection.target;state=$state;revision=$Inspection.revision;configRevision=$Inspection.configRevision;settings=$Inspection.settings;conflicts=$Inspection.conflicts;missing=$Inspection.missing;runtime=$Inspection.runtime;recoveryRequired=$Inspection.recoveryRequired;history=$Inspection.history;verification=(Get-HubProperty $Inspection 'verification' 'Checked')}
}
function Get-HubInspection($Target,[switch]$CurrentOnly,[switch]$Quick){
 if($Quick){$CurrentOnly=$true}
 $statePath=SafePath $Target.directory 'NeuRotic/Installer/Current-Install.json'
 $recovering=[bool](Get-NeuroticPendingTransaction $Target.directory)
 $state=$null
 if(Test-Path -LiteralPath $statePath){
  try{$state=Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json;if($state.kind -ne 'neurotic-public-install' -or $state.schema_version -ne 2 -or [IO.Path]::GetFullPath($state.game_directory).TrimEnd('\') -ine $Target.directory){throw 'Unrecognized installation receipt; preserved'}}
  catch{if(-not $recovering){throw};$state=$null}
 }
 if($state -and $state.status -eq 'installed-verified' -and -not $Quick -and -not $recovering){Initialize-RestoreRecordLedger $Target.directory $state}
 $inventory=@();$revision=$null
 if(-not $CurrentOnly){$inventory=@(Get-HubInventory $Target.directory);$revision=HashBytes ([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject @($inventory) -Depth 6 -Compress)))}
 $settingsRoot=Get-HubSettingsRoot $Target
 $configPath=Get-HubSettingsRelativePath $Target
 $ini=SafePath $Target.directory $configPath;$configRevision=if(Test-Path -LiteralPath $ini){HashFile $ini}else{'Absent'}
 $conflicts=@();$missing=@();$observed=@()
 if($state -and $state.status -eq 'installed-verified' -and -not $recovering -and -not $Quick){
  if($CurrentOnly){$observed=@(Get-HubCurrentFiles $Target $state)}
  else{
   $chain=@($state.restore_chain|ForEach-Object{[string]$_});[array]::Reverse($chain)
   $liveHashes=@{};foreach($entry in $inventory){$liveHashes[[string]$entry.path]=[string]$entry.sha256}
   $observed=@(Test-RestoreChain $Target.directory $chain -ReportLiveFiles -MetadataOnly -LiveHashes $liveHashes)
  }
  $conflicts=@($observed|Where-Object{$_.changed -and $_.existed -and -not (Test-SettingsTarget $_.path)}|ForEach-Object{$_.path})
  $missing=@($observed|Where-Object{(-not $_.existed) -and -not (Test-SettingsTarget $_.path) -and ((-not $CurrentOnly) -or $_.missing)}|ForEach-Object{$_.path})
 }
 if($CurrentOnly){$revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(([ordered]@{scope=$(if($Quick){'quick-status'}else{'current-files'});state=$(if($state){HashFile $statePath}else{'Absent'});config=$configRevision;files=@($observed)}|ConvertTo-Json -Depth 6 -Compress)))}
 $settings=@(Get-HubSettingsProjection $settingsRoot)
 if($Target.bitness -eq 32){foreach($setting in $settings){if(-not (Test-HubPreparedSetting $setting.section $setting.key)){$setting.available=$false}}}
 return [pscustomobject]@{target=$Target;state=$state;revision=$revision;configRevision=$configRevision;settings=$settings;inventory=$inventory;conflicts=$conflicts;missing=$missing;runtime='NotAttempted';recoveryRequired=$recovering;history=(Get-HubHistorySummary $Target $state $recovering);verification=$(if($Quick){'NotChecked'}else{'Checked'})}
}
function Resolve-HubPackage([string]$Id){
 $registry=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'Hub-Packages.json')|ConvertFrom-Json
 $entry=@($registry.packages|Where-Object{$_.id -ceq $Id})
 if($entry.Count -ne 1){throw 'Package is not locally approved'}
 $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $entry[0].relativeRoot))
 $manifestPath=SafePath $root 'support/PACKAGE-MANIFEST.json'
 if((HashFile $manifestPath) -ine $entry[0].manifestSha256){throw 'Approved package manifest changed'}
 $manifest=Get-Content -Raw -LiteralPath $manifestPath|ConvertFrom-Json
 # Resolve the pinned manifest only. The engine validates package bytes before
 # mutation; repeating that full read in the resolver adds no write authority.
 return [pscustomobject]@{id=$Id;root=$root;digest=(HashFile $manifestPath);manifest=$manifest}
}
function Get-HubLaunchPreflight($Request){
 $target=Get-HubTarget $Request.gameExecutable
 $pending=Get-NeuroticPendingTransaction $target.directory
 if($pending){return [ordered]@{status='RecoveryRequired';target=$target;reason='Finish the pending installer recovery before starting this game.'}}
 $statePath=SafePath $target.directory 'NeuRotic\Installer\Current-Install.json'
 $revision=if(Test-Path -LiteralPath $statePath -PathType Leaf){HashFile $statePath}else{'Absent'}
 $cohort=@();$launcher=$null
 if($target.bitness -eq 32){
  if($revision -eq 'Absent'){throw 'Install the compatible prepared route before using Play, or use NR Anything.'}
  $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
  if($state.kind -ne 'neurotic-public-install' -or $state.schema_version -ne 2 -or $state.status -ne 'installed-verified' -or (Get-HubProperty $state 'install_route') -ne 'prepared-x86' -or $state.game_executable -ine $target.executable){throw 'The selected game has no current compatible prepared installation.'}
  Initialize-RestoreRecordLedger $target.directory $state
  $required=@('dxgi.dll','NeuRotic\Prepared\NeuRotic-PreparedGuides.addon32','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.dll','NeuRotic\Prepared\NeuRotic.GpuHost\nvngx.dll_dlssnr.dll','NeuRotic\Prepared\NeuRotic.GpuHost\WinPixEventRuntime.dll','NeuRotic\Prepared\Start-PreparedGame.ps1')
  $effective=@{};foreach($recordPath in $state.restore_chain){foreach($file in (Read-InstallRecord $recordPath).files){$effective[[string]$file.path]=$file}}
  foreach($name in @($effective.Keys)){
   if($name -match '^NeuRotic\\Prepared\\NeuRotic\.GpuHost\\(?:nvngx_dlssnr\.dll|OptiScaler\\streamline\\[^\\]+\.dll)$' -and -not($effective[$name].PSObject.Properties.Name -contains 'installed_exists' -and -not $effective[$name].installed_exists)){$required += $name}
  }
  foreach($relative in $required){
   $entry=$effective[$relative];$path=SafePath $target.directory $relative
   if(-not $entry -or (Get-HubProperty $entry 'installed_exists' $true) -ne $true -or -not(Test-Path -LiteralPath $path -PathType Leaf) -or (HashFile $path) -ne $entry.installed_hash){throw ('Prepared launch component needs repair: '+$relative)}
   $cohort += [pscustomobject]@{path=$path;sha256=$entry.installed_hash}
  }
  $package=Resolve-HubPackage 'prepared-x86';$relative='payload\NeuRotic\Prepared\Start-PreparedGame.ps1'
  $row=@($package.manifest.files|Where-Object{$_.path -ieq $relative})
  $launcher=SafePath $package.root $relative
  if($row.Count -ne 1 -or (HashFile $launcher) -ne $row[0].sha256){throw 'The trusted prepared launcher is missing or changed; repair the NeuRotic application.'}
  $cohort += [pscustomobject]@{path=$launcher;sha256=$row[0].sha256}
 }
 Initialize-NeuroticFileSafety
 return [ordered]@{status='LaunchReady';target=$target;installRevision=$revision;targetLockName=('Global\NeuRotic-Installer-'+[NeuRoticFileSafety]::Identity($target.directory));launcherPath=$launcher;cohort=$cohort}
}
function Get-HubEngineArguments($Request,$Target,$Package){
 $arguments=@{GameExecutable=$Target.executable;HubNonInteractive=$true}
 if($Request.PSObject.Properties.Name -contains 'fileDecisions'){$arguments.FileDecisionsJson=ConvertTo-Json -InputObject @($Request.fileDecisions) -Depth 6 -Compress}
 if($Request.operation -eq 'Uninstall'){$arguments.Uninstall=$true;$arguments.ConfirmUninstall=$true;$arguments.UninstallMode=(Get-HubProperty $Request 'uninstallMode' 'KeepSettings');$arguments.RemoveBackupRecords=(@(Get-HubProperty $Request 'cleanupCategories' @()) -contains 'backups')}
 else{
  $arguments.HubPackageRoot=$Package.root;$arguments.ProxyName=$Request.proxyName;$arguments.ConfirmInstall=$true
  $arguments.ContinueWithoutRuntime=[bool](Get-HubProperty $Request 'continueWithoutRuntime' $false)
  $arguments.IncludeInspector=[bool](Get-HubProperty $Request 'includeInspector' $false)
  if([bool](Get-HubProperty $Request 'adoptModifiedInstall' $false)){$arguments.AdoptModifiedInstall=$true}
  if($Request.operation -in @('Update','Repair','ChangeProxy')){$arguments.ExistingInstallAction=$Request.operation}
  $decision=Get-HubProperty $Request 'existingProxyAction';if($decision -and (Test-Path (SafePath $Target.directory $Request.proxyName))){$arguments.ExistingProxyAction=$decision}
  $dxgi=Get-HubProperty $Request 'existingDxgiAction';if($dxgi){$arguments.ExistingDxgiAction=$dxgi}
 }
 return $arguments
}
function Get-HubUninstallCleanupProof($Target,$Request){
 $categories=@(Get-HubProperty $Request 'cleanupCategories' @()|Where-Object{$_ -in @('privateModels','screenshots')})
 if(-not $categories.Count){return $null}
 # Reuse the bounded cleanup classifier, but never grant root/assets/installer
 # deletion authority after original files have been restored.
 $inventory=Get-HubSanitizeInventory $Target $categories @{}
 $files=@($inventory.files|Where-Object{$_.path -notmatch '^(?i:NeuRotic-(?:test-)?backups|OptiScaler-backups|NeuRotic-preserved-files|NeuRotic[\\/](?:Installer|Uninstall|UserData))(?:\\|/|$)'})
 $directories=@($inventory.directories|Where-Object{$directory=$_;@($files|Where-Object{$_.path.StartsWith($directory.path+'\',[StringComparison]::OrdinalIgnoreCase)}).Count})
 $proof=[ordered]@{rootIdentity=$inventory.rootIdentity;files=$files;directories=$directories;categories=@($categories|Sort-Object)}
 $proof.revision=HashBytes ([Text.Encoding]::UTF8.GetBytes(($proof|ConvertTo-Json -Depth 12 -Compress)))
 return [pscustomobject]$proof
}
function Invoke-HubUninstallCleanup($Target,$Plan){
 $receiptPath=Join-Path (Get-HubUserRoot) ($Plan.planId+'.uninstall-cleanup.json')
 $receipt=[ordered]@{kind='neurotic-uninstall-extra-cleanup';planId=$Plan.planId;target=$Target;originalsRestored=$true;status='Executing';deleted=@();remaining=@($Plan.uninstallCleanup.files|ForEach-Object{$_.path});pending=$null}
 try{
  SaveRecord $receiptPath $receipt
  $fresh=Get-HubUninstallCleanupProof $Target $Plan.request
  if($fresh.revision -ne $Plan.uninstallCleanup.revision){throw 'Originals were restored, but optional cleanup files changed; those files were kept. Review cleanup again.'}
  CheckGameClosed $Target.executable
  foreach($file in $Plan.uninstallCleanup.files){
   $receipt.pending=$file.path;SaveRecord $receiptPath $receipt
   $path=SafePath $Target.directory $file.path;$parents=[NeuRoticFileSafety]::Parents($path,$false)
   try{
    if([NeuRoticFileSafety]::Identity($Target.directory) -ne $Plan.uninstallCleanup.rootIdentity){throw 'Optional cleanup root changed'}
    foreach($directory in $Plan.uninstallCleanup.directories){if($file.path.StartsWith($directory.path+'\',[StringComparison]::OrdinalIgnoreCase) -and [NeuRoticFileSafety]::Identity((SafePath $Target.directory $directory.path)) -ne $directory.identity){throw 'Optional cleanup parent changed'}}
    Remove-NeuroticPath -LiteralPath $path -ExpectedHash $file.sha256
   }finally{$parents.Dispose()}
   $receipt.deleted+=@($file.path);$receipt.remaining=@($receipt.remaining|Where-Object{$_ -cne $file.path});$receipt.pending=$null;SaveRecord $receiptPath $receipt
  }
  $receipt.status='Succeeded';SaveRecord $receiptPath $receipt
  return [ordered]@{status='Succeeded';originalsRestored=$true;deleted=$receipt.deleted;receipt=$receiptPath}
 }catch{
  $receipt.status='FailedWithChanges';$receipt.reason=$_.Exception.Message;try{SaveRecord $receiptPath $receipt}catch{}
  return [ordered]@{status='FailedWithChanges';decisionKind='UninstallCleanup';originalsRestored=$true;reason=$receipt.reason;deleted=$receipt.deleted;remaining=$receipt.remaining;pending=$receipt.pending;receipt=$receiptPath}
 }
}
function New-HubPlan($Request){
 $requestedPackageId=Get-HubProperty $Request 'packageId'
 if($Request.operation -in @('DumbfireInstall','DumbfireRollback')){return New-HubDumbfirePlan $Request}
 if($Request.operation -eq 'Sanitize'){return New-HubSanitizePlan $Request}
 $target=Get-HubTarget $Request.gameExecutable;$operation=$Request.operation
 if($target.bitness -eq 32){
  if($operation -eq 'ChangeProxy'){throw 'The 32-bit prepared capture route uses dxgi.dll; select Update or Repair'}
  if($operation -in @('Install','Update','Repair')){
   if((Get-HubProperty $Request 'packageId') -cnotin @('flagship-approved','prepared-x86')){throw 'Unsupported package for 32-bit capture'}
   $Request|Add-Member -NotePropertyName packageId -NotePropertyValue 'prepared-x86' -Force
   $selectedProxy=Get-HubProperty $Request 'proxyName' 'dxgi.dll'
   if($selectedProxy -cne 'dxgi.dll'){throw 'The 32-bit prepared capture route requires dxgi.dll'}
   $Request|Add-Member -NotePropertyName proxyName -NotePropertyValue 'dxgi.dll' -Force
   if((Get-HubProperty $Request 'existingProxyAction') -and (Get-HubProperty $Request 'existingProxyAction') -cnotin @('Replace','Cancel')){throw 'Existing 32-bit ReShade must be preserved and replaced, or installation cancelled'}
  }
 }
 $antiCheat=$null
 if($operation -in @('Install','Update','Repair','ChangeProxy','SaveSettings')){
  $antiCheat=Get-HubAntiCheat $Request $target
  if($antiCheat.scan_status -ceq 'cancelled'){return [ordered]@{status='Cancelled';reason='Detection cancelled; no changes made';antiCheat=$antiCheat}}

 }
 $inspection=Get-HubInspection $target
 if($inspection.recoveryRequired -and $operation -ne 'Restore'){return [ordered]@{status='RecoveryRequired';reason='Recover the pending transaction before planning another change'}}
 $adopt=[bool](Get-HubProperty $Request 'adoptModifiedInstall' $false)
 $recordedProxy=if($inspection.state){[string]$inspection.state.selected_proxy}else{$null}
 if($adopt){
  if($Request.kind -cne 'Plan' -or $operation -cnotin @('Update','Repair')){throw 'Modified-install adoption requires an Update or Repair Plan'}
  if(-not $inspection.state -or $inspection.state.status -ne 'installed-verified'){throw 'Modified-install adoption requires an active verified managed installation'}
  if(-not $inspection.conflicts.Count){throw 'No modified managed files require a new recovery baseline'}
  if((Get-HubProperty $Request 'existingProxyAction') -cne 'Replace'){throw 'Review replacement explicitly: modified-install adoption requires existingProxyAction Replace'}
  if($Request.PSObject.Properties.Name -contains 'existingDxgiAction'){throw 'Modified-install adoption cannot be combined with a legacy dxgi decision'}
  $requestedProxy=Get-HubProperty $Request 'proxyName'
  if($requestedProxy -and $requestedProxy -ine $recordedProxy){throw 'Modified-install adoption must keep the recorded proxy filename'}
 }
 # The engine returns individual, content-bound decisions for touched files.
 if(@(Get-HubProperty $Request 'fileDecisions' @()|Where-Object{$_.action -eq 'Cancel'}).Count){return [ordered]@{status='Cancelled';reason='Installation cancelled. No files were changed.'}}
 if($operation -eq 'Install' -and $inspection.state -and $inspection.state.status -eq 'installed-verified'){throw 'Already installed; select Update or Repair'}
 $package=$null
 if($operation -in @('Install','Update','Repair','ChangeProxy')){$package=Resolve-HubPackage $Request.packageId}
 if($operation -in @('Update','Repair','ChangeProxy','Uninstall') -and (-not $inspection.state -or $inspection.state.status -ne 'installed-verified')){throw 'No verified managed installation'}
 if($operation -eq 'Restore' -and -not $inspection.recoveryRequired){throw 'No interrupted transaction to recover'}
 $proxy=Get-HubProperty $Request 'proxyName'
 if($operation -in @('Update','Repair') -and -not $proxy -and $recordedProxy){$proxy=$recordedProxy;$Request|Add-Member -NotePropertyName proxyName -NotePropertyValue $proxy -Force}
 if($operation -in @('Install','Update','Repair','ChangeProxy') -and -not $proxy){return [ordered]@{status='NeedsDecision';decisionKind='ProxyRequired';reason='Select a proxy filename'}}
 $decision=Get-HubProperty $Request 'existingProxyAction'
 if($decision -and $decision -cnotin @('RenameReShade','Replace','Cancel')){throw 'Unknown occupied-proxy decision'}
 $dxgi=Get-HubProperty $Request 'existingDxgiAction';if($dxgi -and $dxgi -cnotin @('Rename','Delete','Cancel')){throw 'Unknown dxgi decision'}
 $mode=Get-HubProperty $Request 'uninstallMode' 'KeepSettings';if($mode -cnotin @('KeepSettings','RemoveSettings','Full')){throw 'Unknown uninstall mode'}
 if($operation -eq 'SaveSettings'){
  if($Request.configRevision -ne $inspection.configRevision){return [ordered]@{status='PreconditionChanged';reason='Settings changed externally; reload the draft'}}
  if($target.bitness -eq 32){foreach($patch in $Request.settings){if(-not (Test-HubPreparedSetting $patch.section $patch.key)){throw 'This setting is unavailable in the 32-bit prepared renderer'}}}
  [void](Get-HubSettingsBytes (Get-HubSettingsRoot $target) $Request.settings)
 }
 $runtimeDependencyIdentity=$null;$changes=New-Object Collections.Generic.List[object]
 if($operation -eq 'CompactHistory'){
  $historyPlan=Get-HubHistoryPlan $target $inspection.state ([int](Get-HubProperty $Request 'keepRestoreRecords' 3))
  $changes.Add([ordered]@{path='NeuRotic-backups';operation=('Compact '+$historyPlan.beforeCount+' restore records into the original-baseline checkpoint plus '+$historyPlan.keepRestoreRecords+' newest records. Remove only unchanged owned retired backup files; preserve unknown or modified files. Game files and settings stay unchanged.')})
 }
 if($package -or $operation -eq 'Uninstall'){
  $previewPath=Join-Path (Get-HubUserRoot) ([Guid]::NewGuid().ToString('N')+'.preview.json')
  try{
   $arguments=Get-HubEngineArguments $Request $target $package
   $arguments.CheckOnly=$true;$arguments.HubPlanPreviewPath=$previewPath
   & (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1') @arguments | Out-Host
   if(-not (Test-Path -LiteralPath $previewPath)){throw 'Installer could not produce a complete structured preview'}
   $preview=Get-Content -Raw -LiteralPath $previewPath|ConvertFrom-Json
   $runtimeDependencyIdentity=Get-HubProperty $preview 'runtimeDependencyIdentity'
   $fileConflicts=@(Get-HubProperty $preview 'fileConflicts' @())
   if($fileConflicts.Count){return [ordered]@{status='NeedsDecision';decisionKind='FileConflict';target=$target.executable;fileConflicts=$fileConflicts;reason='Review each file before continuing.'}}
   if([bool](Get-HubProperty $preview 'cancelled' $false) -or [bool](Get-HubProperty $preview 'skipped' $false)){return [ordered]@{status='Cancelled';reason='Installation was not applied because a required file was skipped or the operation was cancelled. Existing files were left alone.'}}
   if($package -and -not $preview.runtimeReady -and -not [bool](Get-HubProperty $Request 'continueWithoutRuntime' $false)){return [ordered]@{status='NeedsDecision';decisionKind='RuntimeMissing';reason='Visual C++ x64 runtime is missing. Install it or explicitly choose to continue.'}}
   foreach($change in $preview.changes){$changes.Add($change)}
  }finally{if(Test-Path -LiteralPath $previewPath){Remove-Item -LiteralPath $previewPath -Force}}
 }
 $recovery=$null
 if($adopt){
  $recovery=[ordered]@{adoptModifiedInstall=$true;recordedProxy=$recordedProxy;conflicts=$inspection.conflicts;packageId=$package.id;packageName=$package.manifest.name;baseline='Current game files; uninstall restores this baseline';historicalRecords='Retained untouched; excluded from the new active restore chain'}
  $changes.Add([ordered]@{path=$recordedProxy;operation=('install bundled approved '+$package.manifest.name+' package; preserve current files as new recovery baseline; retain earlier backup records untouched')})
 }
 if($operation -eq 'SaveSettings'){$changes.Add([ordered]@{path=(Get-HubSettingsRelativePath $target);operation='save reviewed fields; preserve encoding/comments'})}
 if($operation -eq 'Restore'){$changes.Add([ordered]@{path='pending installer transaction';operation='recover verified snapshots or finish verified pending cleanup'})}
 $uninstallCleanup=$null
 if($operation -eq 'Uninstall'){$uninstallCleanup=Get-HubUninstallCleanupProof $target $Request;if($uninstallCleanup){foreach($file in $uninstallCleanup.files){$changes.Add($file)}}}
 $id=[Guid]::NewGuid().ToString('N')
 $plan=[ordered]@{schemaVersion=1;planId=$id;status='Planned';createdUtc=[DateTime]::UtcNow.ToString('o');expiresUtc=[DateTime]::UtcNow.AddMinutes(5).ToString('o');request=$Request;target=$target;revision=$inspection.revision;configRevision=$inspection.configRevision;packageDigest=$(if($package){$package.digest}else{$null});changes=$changes.ToArray();runtimeDependencyIdentity=$runtimeDependencyIdentity;recovery=$recovery;antiCheat=$antiCheat;preserved=@($(if(@(Get-HubProperty $Request 'cleanupCategories' @()) -notcontains 'privateModels'){'private nvngx_dlssnr.dll'}),'unknown files','third-party backups');requiresElevation=$false}
 if($uninstallCleanup){$plan.uninstallCleanup=$uninstallCleanup}
 $plan.requestedPackageId=$requestedPackageId
 $plan.resolvedPackage=if($package){[ordered]@{id=$package.id;digest=$package.digest;architecture=$target.bitness;route=$(if($package.id -eq 'prepared-x86'){'prepared-x86'}else{'native-x64'})}}else{$null}
 $plan.planFingerprint=Get-HubPlanFingerprint ([pscustomobject]$plan)
 SaveRecord (Join-Path (Get-HubUserRoot) ($id+'.plan.json')) $plan
 return $plan
}
function Resolve-HubSettingsDefaults($Request){
 $target=Get-HubTarget $Request.gameExecutable
 $inspection=Get-HubInspection $target -CurrentOnly
 if($inspection.configRevision -ne $Request.configRevision){return [ordered]@{status='PreconditionChanged';reason='Settings changed externally; refresh before restoring defaults'}}
 if($inspection.recoveryRequired){throw 'Finish installation recovery before restoring defaults'}
 $package=Resolve-HubPackage $(if($target.bitness -eq 32){'prepared-x86'}else{'flagship-approved'})
 $settings=@(Get-HubDefaultSettings $target $inspection $package)
 $after=Get-HubInspection (Get-HubTarget $Request.gameExecutable) -CurrentOnly
 if($after.revision -ne $inspection.revision -or $after.target.sha256 -ne $target.sha256){return [ordered]@{status='PreconditionChanged';reason='Game or settings changed while resolving defaults; refresh and retry'}}
 return [ordered]@{status='DefaultsResolved';inspection=$after;defaultSettings=$settings;defaultsSource=[ordered]@{packageId=$package.id;packageDigest=$package.digest;executable=$target.executable;executableSha256=$target.sha256;runtimeRules='Automatic game and GPU rules are resolved on next launch'}}
}
function Invoke-HubProtocol([string]$RequestPath,[string]$ResultPath){
 try{
  $bytes=[IO.File]::ReadAllBytes($RequestPath);if($bytes.Length -gt 6291456){throw 'Request exceeds 6 MiB'}
  $request=Read-HubRequestText ((New-Object Text.UTF8Encoding($false,$true)).GetString($bytes))
  if($request.kind -eq 'Preflight'){$result=Get-HubPreflight}
  elseif($request.kind -eq 'LaunchPreflight'){$result=Get-HubLaunchPreflight $request}
  elseif($request.kind -eq 'Inspect'){
   $verifyFiles=[bool](Get-HubProperty $request 'verifyFiles' $false)
   $target=Get-HubTarget $request.gameExecutable -MetadataOnly:(-not $verifyFiles)
   $result=[ordered]@{status='Inspected';inspection=$(if($verifyFiles){Get-HubInspection $target -CurrentOnly}else{Get-HubInspection $target -Quick})}
   $result.dumbfireHistory=Get-HubDumbfireHistory $target
   if($verifyFiles){$result.antiCheat=Get-HubAntiCheat $request $target}
  }
  elseif($request.kind -eq 'RestoreDefaults'){$result=Resolve-HubSettingsDefaults $request}
  elseif($request.kind -eq 'Plan'){$result=New-HubPlan $request}
  else {$result=Invoke-HubExecute $request}
 }catch{$result=[ordered]@{status='FailedWithoutMutation';reason=$_.Exception.Message};if($env:NEUROTIC_HUB_FIXTURE_ROOT){$result.diagnostic=$_.ScriptStackTrace}}
 if($result.Contains('inspection') -and $result.inspection){$result.inspection=ConvertTo-HubInspectionSummary $result.inspection}
 $result.protocolVersion=1
 SaveRecord $ResultPath $result
}

function Invoke-HubExecute($Request){
 if($Request.planId -notmatch '^[a-f0-9]{32}$'){throw 'Invalid plan ID'}
 $planPath=Join-Path (Get-HubUserRoot) ($Request.planId+'.plan.json')
 $plan=Get-Content -Raw -LiteralPath $planPath|ConvertFrom-Json
 $dumbfire=$plan.request.operation -in @('DumbfireInstall','DumbfireRollback')
 if($Request.planFingerprint -cne (Get-HubPlanFingerprint $plan)){return [ordered]@{status='PreconditionChanged';reason='Stored plan differs from the reviewed fingerprint; review again';dumbfire=$dumbfire;planId=$plan.planId;target=$plan.target;rollbackAvailable=$false}}
 if($plan.status -ne 'Planned'){
  if(-not $dumbfire){throw 'Plan already consumed'}
  return [ordered]@{status='PreconditionChanged';reason='Plan already consumed; review again';dumbfire=$true;planId=$plan.planId;target=$plan.target;rollbackAvailable=$false}
 }
 if([DateTime]::UtcNow -gt [DateTime]::Parse($plan.expiresUtc).ToUniversalTime()){return [ordered]@{status='PreconditionChanged';reason='Plan expired; review again';dumbfire=$dumbfire;planId=$plan.planId;target=$plan.target;rollbackAvailable=$false}}
 $request=$plan.request
 try{$target=Get-HubTarget $request.gameExecutable;$lock=Enter-NeuroticTargetLock $target.directory}catch{if(-not $dumbfire){throw};return [ordered]@{status='FailedWithoutMutation';dumbfire=$true;rollbackAvailable=$false;planId=$plan.planId;target=$plan.target;reason=$_.Exception.Message}}
 try{
  if($request.operation -in @('DumbfireInstall','DumbfireRollback')){return Invoke-HubDumbfireExecute $plan $target $planPath}
  if($request.operation -eq 'Sanitize'){return Invoke-HubSanitizeExecute $plan $target $planPath}
  if($request.operation -in @('Install','Update','Repair','ChangeProxy','SaveSettings')){
   Initialize-HubAntiCheatCollector
   $localApproval=[NeuRoticAntiCheatCollector]::ReadApproval()
   $antiCheat=Get-HubAntiCheat $request $target
   $reviewedAntiCheat=Get-HubProperty $plan 'antiCheat'
   if($antiCheat.scan_status -ceq 'cancelled'){return [ordered]@{status='Cancelled';reason='Detection cancelled; no changes made'}}
   if(-not $reviewedAntiCheat -or $antiCheat.fingerprint -cne $reviewedAntiCheat.fingerprint){
    if($reviewedAntiCheat -and $antiCheat.scan_status -cne 'complete' -and @($reviewedAntiCheat.findings).Count){$antiCheat|Add-Member -NotePropertyName stale_findings -NotePropertyValue @($reviewedAntiCheat.findings) -Force}
    return New-HubAntiCheatDecision $target $antiCheat
   }
   $expectedApproval="NRAC1`n"+$plan.planId+"`n"+$plan.planFingerprint+"`n"+$antiCheat.fingerprint+"`n"
   if($antiCheat.acknowledgementRequired -and -not [StringComparer]::Ordinal.Equals($localApproval,$expectedApproval)){return New-HubAntiCheatDecision $target $antiCheat}
   $localApproval=$null
  }
  $inspection=Get-HubInspection $target
  if($target.sha256 -ne $plan.target.sha256 -or $inspection.revision -ne $plan.revision){return [ordered]@{status='PreconditionChanged';reason='Executable or installation files changed; review again'}}
  if((Get-HubProperty $plan 'uninstallCleanup')){$freshCleanup=Get-HubUninstallCleanupProof $target $request;if($freshCleanup.revision -ne $plan.uninstallCleanup.revision){return [ordered]@{status='PreconditionChanged';reason='Optional uninstall cleanup changed; review again before restoring or deleting files'}}}
  CheckGameClosed $target.executable
  $package=$null;if($plan.packageDigest){$package=Resolve-HubPackage $request.packageId;if($package.digest -ne $plan.packageDigest){return [ordered]@{status='PreconditionChanged';reason='Package changed'}}}
  $plan.status='Executing';SaveRecord $planPath $plan
  $operation=$request.operation
  if($operation -eq 'SaveSettings'){$result=Invoke-HubSettingsSave $target $request}
  elseif($operation -eq 'CompactHistory'){
   $history=Invoke-HubHistoryCompact $target ([int](Get-HubProperty $request 'keepRestoreRecords' 3))
   $result=[ordered]@{status='Succeeded';inspection=(Get-HubInspection $target -CurrentOnly);history=$history}
  }
  elseif($operation -eq 'Restore'){
   $journalPath=SafePath $target.directory 'NeuRotic/Installer/Uninstall-Transaction.json'
   $pending=Get-NeuroticPendingTransaction $target.directory
   if($pending -in @('Install-Transaction.json','Restore-Transaction.json')){Restore-NeuroticMutation $target.directory ($pending.Replace('-Transaction.json',''))}
   elseif($pending -and [IO.Path]::IsPathRooted($pending)){[void](Invoke-RestoreRecord $pending)}
   elseif(Test-Path -LiteralPath (SafePath $target.directory 'NeuRotic/Installer/History-Transaction.json')){[void](Complete-HubHistory $target.directory)}
   elseif(Test-Path -LiteralPath $journalPath){
    $journal=Get-Content -Raw -LiteralPath $journalPath|ConvertFrom-Json
    Restore-UninstallTransaction $target.directory $journal
    $staleCleanup=SafePath $target.directory 'NeuRotic/Installer/Cleanup-Pending.json'
    if(Test-Path -LiteralPath $staleCleanup){Remove-NeuroticPath -LiteralPath $staleCleanup -Force}
    Remove-UninstallSnapshots $target.directory $journal
   }
   else{
    $cleanupPath=SafePath $target.directory 'NeuRotic/Installer/Cleanup-Pending.json'
    if(-not (Test-Path -LiteralPath $cleanupPath)){throw 'No interrupted transaction to recover'}
    Complete-UninstallCleanup $target.directory (Get-Content -Raw -LiteralPath $cleanupPath|ConvertFrom-Json)
   }
   $after=Get-HubInspection (Get-HubTarget $target.executable) -CurrentOnly
   if($after.recoveryRequired){throw 'Recovery remains pending'}
   $result=[ordered]@{status='Succeeded';inspection=$after}
  }else{
   $arguments=Get-HubEngineArguments $request $target $package
   if($package){$arguments.HubExpectedRuntimeIdentity=Get-HubProperty $plan 'runtimeDependencyIdentity'}
   $completionPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.completion.json')
   $arguments.HubOperationResultPath=$completionPath
   try{
    if(Test-Path -LiteralPath $completionPath){throw 'Unexpected existing operation completion receipt'}
    & (Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1') @arguments | Out-Host
    if(-not(Test-Path -LiteralPath $completionPath -PathType Leaf)){throw 'Installer returned without a completed transaction receipt'}
    $completion=Get-Content -LiteralPath $completionPath -Raw|ConvertFrom-Json
    $expectedOperation=if($operation -eq 'Uninstall'){'Uninstall'}else{'Install'}
    if($completion.status -cne 'Succeeded' -or $completion.operation -cne $expectedOperation){throw 'Installer completion did not match the requested operation'}
   }finally{if(Test-Path -LiteralPath $completionPath){Remove-Item -LiteralPath $completionPath -Force}}
   # The engine has already verified guarded writes and committed its receipt.
   # Re-read only current metadata for UI state, not every payload a second time.
   $after=Get-HubInspection (Get-HubTarget $target.executable -MetadataOnly) -Quick
   if($operation -eq 'Uninstall'){
    if($after.state -and $after.state.status -eq 'installed-verified'){throw 'Engine returned without completing uninstall'}
   }elseif(-not $after.state -or $after.state.status -ne 'installed-verified'){throw 'Installer receipt does not describe the completed installation'}
   $limitations=@(Get-HubProperty $completion 'limitations' @())
   $result=[ordered]@{status='Succeeded';inspection=$after;limitations=$limitations;preservedForeignFiles=@(Get-HubProperty $completion 'preservedForeignFiles' @())}
   if($limitations.Count){$result.reason='NeuRotic files were removed where safe. '+$limitations.Count+' file(s) need attention; modified files and recovery records were kept. See installation details.'}
   if($operation -eq 'Uninstall' -and (Get-HubProperty $plan 'uninstallCleanup')){
    if($limitations.Count){$result.reason+=' Optional cleanup was skipped to retain recovery data.'}
    else{$cleanup=Invoke-HubUninstallCleanup $target $plan;if($cleanup.status -ne 'Succeeded'){$result=$cleanup;$result.inspection=$after}else{$result.cleanup=$cleanup}}
   }
  }
  $plan.status=$result.status;SaveRecord $planPath $plan;$result.planId=$plan.planId;return $result
 }catch{
  if($request.operation -in @('DumbfireInstall','DumbfireRollback')){return [ordered]@{status='RecoveryRequired';dumbfire=$true;rollbackAvailable=$true;planId=$plan.planId;target=$target;reason=$_.Exception.Message}}
  $journal=SafePath $target.directory 'NeuRotic/Installer/Uninstall-Transaction.json'
  $cleanup=SafePath $target.directory 'NeuRotic/Installer/Cleanup-Pending.json'
  $failureStatus=Get-NeuroticFailureProperty $_.Exception 'NeuRoticMutationStatus'
  if((Test-Path -LiteralPath $journal) -or (Test-Path -LiteralPath $cleanup) -or (Test-Path -LiteralPath (SafePath $target.directory 'NeuRotic/Installer/History-Transaction.json'))){$failureStatus='RecoveryRequired'}
  if(-not $failureStatus){
   $failureStatus='FailedWithoutMutation'
   try{if((Get-HubInspection $target).revision -ne $plan.revision){$failureStatus='FailedWithChanges'}}catch{$failureStatus='RecoveryRequired'}
  }
  $result=[ordered]@{status=$failureStatus;reason=$_.Exception.Message;planId=$plan.planId}
  $plan.status=$result.status;SaveRecord $planPath $plan;return $result
 }finally{$lock.ReleaseMutex();$lock.Dispose()}
}
