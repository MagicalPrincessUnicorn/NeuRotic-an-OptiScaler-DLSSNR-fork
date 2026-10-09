# Protocol1: selected-target operations, no installed-tree health or recovery gate.
function Get-HubProperty($Object,[string]$Name,$Default=$null){
 if($Object -is [Collections.IDictionary]){if($Object.Contains($Name)){return $Object[$Name]};return $Default}
 if($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name){return $Object.$Name};return $Default
}
function Read-HubRequestText([string]$Text){
 if([Text.Encoding]::UTF8.GetByteCount($Text) -gt 6291456){throw 'Request exceeds 6 MiB'}
 if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-HubJson.cs')}
 [NeuRoticHubJson]::ValidateSettingsRequest($Text);$request=$Text|ConvertFrom-Json
 $allowed=@('protocolVersion','requestId','kind','gameExecutable','operation','packageId','proxyName','existingProxyAction','planId','planFingerprint','freshInstall','fileDecisions','settings','configRevision','continueWithoutRuntime','gameRoot','steamRoot','steamAppId','verifyFiles','gameTitle','gameStore','gameStoreId')
 foreach($property in $request.PSObject.Properties){if($property.Name -cnotin $allowed){throw ('Unknown request field: '+$property.Name)}}
 if(($request.protocolVersion -isnot [int] -and $request.protocolVersion -isnot [long]) -or $request.protocolVersion -ne 1 -or $request.kind -cnotin @('Inspect','RestoreDefaults','Plan','Execute','Preflight','LaunchPreflight')){throw 'Unsupported protocol or kind'}
 foreach($name in @('requestId','gameExecutable','operation','packageId','proxyName','existingProxyAction','planId','planFingerprint','configRevision','gameRoot','steamRoot','steamAppId','gameTitle','gameStore','gameStoreId')){if($request.PSObject.Properties.Name -contains $name -and $request.$name -isnot [string]){throw ('Request field must be text: '+$name)}}
 foreach($name in @('freshInstall','continueWithoutRuntime','verifyFiles')){if($request.PSObject.Properties.Name -contains $name -and $request.$name -isnot [bool]){throw ('Request field must be Boolean: '+$name)}}
 if($request.PSObject.Properties.Name -contains 'freshInstall' -and ($request.kind -cne 'Plan' -or $request.operation -cne 'Install')){throw 'Fresh Install belongs only to Install'}
 if((Get-HubProperty $request 'existingProxyAction') -and $request.existingProxyAction -cnotin @('RenameReShade','Replace','Cancel')){throw 'Unknown proxy choice'}
 if((Get-HubProperty $request 'proxyName') -and $request.proxyName -cnotin $script:NeuroticSimpleProxies){throw 'Unknown proxy filename'}
 if($request.PSObject.Properties.Name -contains 'fileDecisions'){
  if($request.kind -cne 'Plan' -or $request.operation -cne 'Install' -or $request.fileDecisions -isnot [Array] -or $request.fileDecisions.Count -gt 10000){throw 'File decisions belong only to Install'}
  $seen=@{};foreach($file in $request.fileDecisions){if($null -eq $file -or @($file.PSObject.Properties).Count -ne 2 -or @($file.PSObject.Properties|Where-Object{$_.Name -cnotin @('path','action')}).Count -or $file.path -isnot [string] -or $file.action -cnotin @('Replace','Skip','Cancel')){throw 'Invalid file decision'};$path=ConvertTo-NeuroticRelativePath $file.path;if($seen.ContainsKey($path)){throw 'Duplicate file decision'};$seen[$path]=$true}
 }
 if($request.PSObject.Properties.Name -contains 'settings' -and $request.settings -isnot [Array]){throw 'Settings must be an array'}
 if($request.kind -eq 'Execute' -and @($request.PSObject.Properties|Where-Object{$_.Name -cnotin @('protocolVersion','requestId','kind','planId','planFingerprint')}).Count){throw 'Execute accepts only the bound operation identity'}
 $base=@('protocolVersion','requestId','kind')
 $fields=switch($request.kind){
  'Preflight'{@()}
  'Inspect'{@('gameExecutable')}
  'LaunchPreflight'{@('gameExecutable')}
  'RestoreDefaults'{@('gameExecutable','configRevision')}
  'Execute'{@('planId','planFingerprint')}
  'Plan'{if($request.operation -eq 'Install'){@('gameExecutable','operation','packageId','proxyName','existingProxyAction','freshInstall','fileDecisions','continueWithoutRuntime','gameRoot','steamRoot','steamAppId','gameTitle','gameStore','gameStoreId')}elseif($request.operation -eq 'SaveSettings'){@('gameExecutable','operation','settings','configRevision','gameRoot','steamRoot','steamAppId')}else{@('gameExecutable','operation')}}
 }
 foreach($property in $request.PSObject.Properties){if($property.Name -cnotin ($base+@($fields))){throw ('Field does not belong to this action: '+$property.Name)}}
 return $request
}
function Get-HubTarget([string]$Executable,[switch]$MetadataOnly){
 $exe=[IO.Path]::GetFullPath($Executable);if([IO.Path]::GetExtension($exe) -ine '.exe'){throw 'Select an actual game executable'}
 $root=Split-Path -Parent $exe;[void](SafePath $root ([IO.Path]::GetFileName($exe)))
 $architecture=Get-NeuroticExecutableArchitecture $exe;if($architecture -eq 'unknown'){throw 'Unsupported executable architecture'}
 return [pscustomobject]@{executable=$exe;directory=$root;bitness=$(if($architecture -eq 'x64'){64}else{32});architecture=$architecture;fileIdentity=[NeuRoticSimpleFileIo]::Stat($exe);sha256=$null;installationRoute=$(if($architecture -eq 'x64'){'native-x64'}else{'unsupported'})}
}
function Get-HubUserRoot {
 $root=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NeuRotic/HubData-v2';if($env:NEUROTIC_HUB_FIXTURE_ROOT){$root=$env:NEUROTIC_HUB_FIXTURE_ROOT}
 [void](Assert-NeuroticRuntimePath $root);New-Item -ItemType Directory -Path $root -Force|Out-Null;return $root
}
function Get-HubPackageAtRoot([string]$Root,[string]$Id){
 $Root=[IO.Path]::GetFullPath($Root);$path=SafePath $Root 'support/PACKAGE-MANIFEST.json'
 if((Get-Item -LiteralPath $path).Length -gt 6291456){throw 'Package inventory exceeds the metadata limit'}
 $manifest=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
 return [pscustomobject]@{id=$Id;root=$Root;architecture='x64';digest=HashFile $path;manifest=$manifest}
}
function Resolve-HubPackage([string]$Id){
 if($Id -cne 'flagship-approved'){throw 'Only the supported native package is available. Use NR Anything for incompatible in-game targets.'}
 $registry=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Hub-Packages.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $entries=@($registry.packages|Where-Object{$_.id -ceq $Id});if($entries.Count -ne 1){throw 'The locally approved native package is unavailable'}
 $package=Get-HubPackageAtRoot (Join-Path $PSScriptRoot $entries[0].relativeRoot) $Id
 if($package.digest -ine $entries[0].manifestSha256){throw 'Approved package inventory changed'};return $package
}
function Get-HubInspection($Target,[switch]$CurrentOnly,[switch]$Quick){
 $read=Read-NeuroticInstallReceipt $Target.directory;$state=$read.receipt
 $ini=SafePath $Target.directory 'OptiScaler.ini';$revision='Absent';if([IO.File]::Exists($ini)){if((Get-Item -LiteralPath $ini).Length -gt 6291456){throw 'Configuration exceeds the metadata limit'};$revision=HashFile $ini}
 $settings=@();if($Target.bitness -eq 64 -and [IO.File]::Exists($ini)){$settings=@(Get-HubSettingsProjection $Target.directory)}
 $receiptRevision=$(if($state){HashBytes ([Text.Encoding]::UTF8.GetBytes(($state|ConvertTo-Json -Depth 8 -Compress)))}else{'Absent'})
 return [pscustomobject]@{target=$Target;state=$state;revision=$receiptRevision;configRevision=$revision;settings=$settings;notes=@($read.notes);conflicts=@();missing=@();recoveryRequired=$false;verification='Metadata';inventory=[pscustomobject]@{files=@()};runtime='NotAttempted'}
}
function Get-HubPreflight {
 . (Join-Path $PSScriptRoot 'NeuRotic-Prerequisites.ps1')
 $runtime=Get-NeuroticVcRuntimeStatus ([version]'14.44.35207.0')
 return [ordered]@{status='PreflightChecked';runtime=[ordered]@{ready=[bool]$runtime.Ready;minimumVersion='14.44.35207.0';downloadUrl='https://aka.ms/vc14/vc_redist.x64.exe';problems=@($runtime.Problems)};components=(Get-NeuroticRuntimeComponents (Split-Path -Parent $PSScriptRoot))}
}
function Get-HubPlanFingerprint($Plan){return HashBytes ([Text.Encoding]::UTF8.GetBytes(([ordered]@{planId=$Plan.planId;request=$Plan.request;target=$Plan.target;packageDigest=$Plan.packageDigest;packageRoot=$Plan.packageRoot;antiCheat=$Plan.antiCheat;autoSetup=(Get-HubProperty $Plan 'autoSetup');installDefaults=(Get-HubProperty $Plan 'installDefaults')}|ConvertTo-Json -Depth 16 -Compress)))}
function New-HubPlan($Request,$ManualPackage=$null){
 if($Request.operation -cnotin @('Install','Uninstall','SaveSettings')){return [ordered]@{status='UnsupportedOperation';reasonCode='desktop.neurotic-hubprotocol.this_operation_is_no_longer_supported_use_instal_f64894c4';messageParameters=@{};reason='This operation is no longer supported. Use Install or Uninstall.'}}
 if($Request.operation -eq 'Install' -and (@(Get-HubProperty $Request 'fileDecisions' @()|Where-Object{$_.action -eq 'Cancel'}).Count -or (Get-HubProperty $Request 'existingProxyAction') -eq 'Cancel')){return [ordered]@{status='Cancelled';reasonCode='desktop.neurotic-hubprotocol.cancelled_no_files_were_changed_97002fba';messageParameters=@{};reason='Cancelled. No files were changed.'}}
 $target=Get-HubTarget $Request.gameExecutable;$package=$null;$antiCheat=$null
 if($Request.operation -in @('Install','SaveSettings') -and $target.bitness -ne 64){return [ordered]@{status='FailedWithoutChanges';reasonCode='desktop.manuallibrary.a_compatible_in_game_integration_is_unavailable__87dd6414';messageParameters=@{};reason='A compatible in-game integration is unavailable for this architecture. Use NR Anything.';target=$target}}
 if($Request.operation -eq 'SaveSettings'){
  # Reject malformed/retired edits before asking for anti-cheat consent. The
  # actual write still checks the revision while holding the file exclusively.
  [void](Get-HubSettingsBytes $target.directory @($Request.settings))
 }
 if($Request.operation -eq 'Install'){
  $package=$(if($ManualPackage){$ManualPackage}else{Resolve-HubPackage $Request.packageId})
  $identity=[pscustomobject]@{title=(Get-HubProperty $Request 'gameTitle');store=(Get-HubProperty $Request 'gameStore');storeId=(Get-HubProperty $Request 'gameStoreId')}
  $profile=Get-NeuroticInstallProfile $target.executable $package $identity;$read=Read-NeuroticInstallReceipt $target.directory
  $proxy=Get-HubProperty $Request 'proxyName';if(-not $proxy){$proxy=Get-NeuroticField $read.receipt 'proxy';if(-not $proxy){$proxy=$profile.proxy}};if(-not $proxy){return [ordered]@{status='NeedsDecision';decisionKind='ProxyRequired';reasonCode='desktop.neurotic-hubprotocol.choose_a_proxy_filename_febc5774';messageParameters=@{};reason='Choose a proxy filename.';target=$target.executable}}
  $Request|Add-Member -NotePropertyName proxyName -NotePropertyValue $proxy -Force
  $profile.proxy=$proxy;$actions=@(Get-NeuroticInstallActions $target.directory $package $profile ([bool](Get-HubProperty $Request 'freshInstall' $false)) $read.receipt (Get-NeuroticAcceptedRuntimeComponents))
  $decisions=@{};foreach($file in @(Get-HubProperty $Request 'fileDecisions' @())){$decisions[(ConvertTo-NeuroticRelativePath $file.path)]=$file.action}
  if((Get-HubProperty $Request 'existingProxyAction') -in @('RenameReShade','Replace')){$decisions[$proxy]='Replace'}
  if((Get-HubProperty $Request 'existingProxyAction') -eq 'RenameReShade' -and (Test-Path -LiteralPath (SafePath $target.directory 'ReShade64.dll'))){return [ordered]@{status='NeedsDecision';decisionKind='FileConflict';target=$target.executable;canKeepReShade=$false;fileConflicts=@([pscustomobject]@{path='dxgi.dll'});reasonCode='desktop.neurotic-hubprotocol.reshade64_dll_is_occupied_choose_replace_skip_or_4dd59574';messageParameters=@{};reason='ReShade64.dll is occupied. Choose Replace, Skip or Cancel for dxgi.dll.'}}
  $foreign=@($actions|Where-Object{$_.operation -ne 'Omit' -and $_.conflict -and -not $decisions.ContainsKey($_.path)}|ForEach-Object{[pscustomobject]@{path=$_.path}})
  if($foreign.Count){return [ordered]@{status='NeedsDecision';decisionKind='FileConflict';reasonCode='desktop.neurotic-hubprotocol.choose_how_to_handle_each_unfamiliar_file_bd8b419d';messageParameters=@{};reason='Choose how to handle each unfamiliar file.';target=$target.executable;fileConflicts=$foreign;canKeepReShade=($proxy -ieq 'dxgi.dll' -and -not(Test-Path -LiteralPath (SafePath $target.directory 'ReShade64.dll')))}}
 }
 if($Request.operation -in @('Install','SaveSettings')){$antiCheat=Get-HubAntiCheat $Request $target;if($antiCheat.scan_status -eq 'cancelled'){return [ordered]@{status='Cancelled';reasonCode='desktop.neurotic-hubprotocol.detection_cancelled_no_changes_made_f5bf3eea';messageParameters=@{};reason='Detection cancelled. No changes made.'}}}
 $plan=[ordered]@{status='Planned';planId=[Guid]::NewGuid().ToString('N');request=$Request;target=$target;packageDigest=$(if($package){$package.digest}else{$null});packageRoot=$(if($package){$package.root}else{$null});antiCheat=$antiCheat;changes=@()}
 if($Request.operation -eq 'Install'){$plan['autoSetup']=Get-NeuroticAutomaticSetup $profile $actions;$plan['installDefaults']=Read-NeuroticInstallDefaults}
 $plan.planFingerprint=Get-HubPlanFingerprint $plan;SaveRecord (Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')) $plan
 return $plan
}
function Add-HubOperationEvidence($Result,$Plan,$Rechecked){
 $planned=Get-HubProperty $Plan 'antiCheat';if(-not $planned){return $Result}
 # Keep the actual pre-write recheck for App diagnostics. A compact preview
 # summary distinguishes changed evidence from the report the user reviewed.
 $summary=[ordered]@{};foreach($name in @('fingerprint','game_state','headline','scan_status','issues')){$summary[$name]=Get-HubProperty $planned $name}
 $fields=[ordered]@{antiCheatReview=[ordered]@{planned=$summary;rechecked=($null -ne $Rechecked);evidenceChanged=$(if($null -ne $Rechecked){$planned.fingerprint -cne $Rechecked.fingerprint}else{$null})}}
 if($null -ne $Rechecked){$fields.antiCheat=$Rechecked}
 foreach($name in $fields.Keys){if($Result -is [Collections.IDictionary]){$Result[$name]=$fields[$name]}else{$Result|Add-Member -NotePropertyName $name -NotePropertyValue $fields[$name] -Force}}
 return $Result
}
function Invoke-HubExecute($Request,[bool]$ManualAcknowledged=$false){
 if($Request.planId -notmatch '^[a-f0-9]{32}$'){throw 'Invalid operation identity'}
 $path=Join-Path (Get-HubUserRoot) ($Request.planId+'.plan.json');if((Get-Item -LiteralPath $path).Length -gt 6291456){throw 'Operation metadata exceeds the limit'}
 $plan=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
 if($plan.status -ne 'Planned' -or $Request.planFingerprint -cne (Get-HubPlanFingerprint $plan)){throw 'The bound operation changed. Retry the selected action.'}
 $antiCheat=$null
 try{
 $request=$plan.request;$target=Get-HubTarget $request.gameExecutable
 if($target.fileIdentity -cne $plan.target.fileIdentity){return Add-HubOperationEvidence ([ordered]@{status='PreconditionChanged';reasonCode='desktop.neurotic-hubprotocol.selected_executable_changed_retry_the_action_343a6c17';messageParameters=@{};reason='Selected executable changed. Retry the action.'}) $plan $null}
 if($request.operation -in @('Install','SaveSettings')){
  $antiCheat=Get-HubAntiCheat $request $target
  if($antiCheat.scan_status -eq 'cancelled'){return Add-HubOperationEvidence ([ordered]@{status='Cancelled';reasonCode='desktop.neurotic-hubprotocol.detection_cancelled_no_changes_made_f5bf3eea';messageParameters=@{};reason='Detection cancelled. No changes made.'}) $plan $antiCheat}
  if($antiCheat.fingerprint -cne $plan.antiCheat.fingerprint){return Add-HubOperationEvidence (New-HubAntiCheatDecision $target $antiCheat) $plan $antiCheat}
  $expected="NRAC1`n"+$plan.planId+"`n"+$plan.planFingerprint+"`n"+$antiCheat.fingerprint+"`n"
  if($antiCheat.acknowledgementRequired -and -not $ManualAcknowledged -and [NeuRoticAntiCheatCollector]::ReadApproval() -cne $expected){return Add-HubOperationEvidence (New-HubAntiCheatDecision $target $antiCheat) $plan $antiCheat}
 }
 $package=$null;if($request.operation -eq 'Install'){$package=Get-HubPackageAtRoot $plan.packageRoot $request.packageId;if($package.digest -cne $plan.packageDigest){throw 'Selected package changed. Retry the action.'}}
 if($request.operation -eq 'Install' -and (Get-HubProperty $plan 'autoSetup')){if((Get-NeuroticRenderingRequirements|ConvertTo-Json -Depth 8 -Compress) -cne ($plan.autoSetup.requirements|ConvertTo-Json -Depth 8 -Compress)){throw (New-NeuroticCodedError 'desktop.automatic_setup.requirements_changed' 'Rendering requirements changed. Retry the action.')}}
 try{
  if($request.operation -eq 'Uninstall'){$result=Invoke-NeuroticSimpleUninstall $target.directory}
  elseif($request.operation -eq 'SaveSettings'){$lock=Enter-NeuroticSimpleLock $target.directory;try{$result=Invoke-HubSettingsSave $target $request}finally{$lock.ReleaseMutex();$lock.Dispose()}}
  elseif($request.operation -eq 'Install'){
   $identity=[pscustomobject]@{title=(Get-HubProperty $request 'gameTitle');store=(Get-HubProperty $request 'gameStore');storeId=(Get-HubProperty $request 'gameStoreId')}
   $result=Invoke-NeuroticSimpleInstall $target.executable $package $request.proxyName ([bool](Get-HubProperty $request 'freshInstall' $false)) (Get-HubProperty $request 'fileDecisions' @()) (Get-HubProperty $request 'existingProxyAction' '') (Get-NeuroticAcceptedRuntimeComponents) $identity (Get-HubProperty $plan 'installDefaults')
  }else{return [ordered]@{status='UnsupportedOperation';reasonCode='desktop.hubviewmodel.use_install_or_uninstall_f5911d3d';messageParameters=@{};reason='Use Install or Uninstall.'}}
  if($result.status -eq 'FailedWithoutChanges' -and @($result.conflicts).Count){$result.status='NeedsDecision';$result|Add-Member -NotePropertyName decisionKind -NotePropertyValue 'FileConflict';$result|Add-Member -NotePropertyName fileConflicts -NotePropertyValue @($result.conflicts);$result|Add-Member -NotePropertyName target -NotePropertyValue $target.executable}
  if($result -is [Collections.IDictionary]){$result.inspection=Get-HubInspection $target;$result.planId=$plan.planId}
  else{$result|Add-Member -NotePropertyName inspection -NotePropertyValue (Get-HubInspection $target);$result|Add-Member -NotePropertyName planId -NotePropertyValue $plan.planId}
  return Add-HubOperationEvidence $result $plan $antiCheat
 }finally{Remove-Item -LiteralPath $path -Force}
 }catch{
  # The protocol's existing error result must retain evidence collected before
  # a later package/settings/write failure, without treating it as approval.
  $_.Exception.Data['NeuroticOperationEvidence']=Add-HubOperationEvidence ([ordered]@{}) $plan $antiCheat
  throw
 }
}
function Get-HubLaunchPreflight($Request){
 $target=Get-HubTarget $Request.gameExecutable
 if($target.bitness -ne 64){return [ordered]@{status='FailedWithoutChanges';reasonCode='desktop.planintent.a_compatible_in_game_integration_is_unavailable__5a817f1b';messageParameters=@{};reason='A compatible in-game integration is unavailable. Use NR Anything.'}}
 return [ordered]@{status='LaunchReady';target=$target;targetLockName=('Global\NeuRotic-Installer-'+[NeuRoticSimpleFileIo]::DirectoryIdentity($target.directory));launcherPath=$null;cohort=@()}
}
function Resolve-HubSettingsDefaults($Request){
 $target=Get-HubTarget $Request.gameExecutable;$inspection=Get-HubInspection $target
 if($inspection.configRevision -cne $Request.configRevision){return [ordered]@{status='PreconditionChanged';reasonCode='desktop.neurotic-hubprotocol.settings_changed_externally_reload_before_resett_742a8b58';messageParameters=@{};reason='Settings changed externally. Reload before resetting defaults.'}}
 if($target.bitness -ne 64){throw 'In-game settings are unavailable for this architecture. Use NR Anything.'}
 $package=Resolve-HubPackage 'flagship-approved';$settings=@(Get-HubDefaultSettings $target $inspection $package)
 return [ordered]@{status='DefaultsResolved';inspection=$inspection;defaultSettings=$settings;defaultsSource=[ordered]@{packageId=$package.id;packageDigest=$package.digest;executable=$target.executable;executableFileIdentity=$target.fileIdentity;runtimeRules='Automatic game and GPU rules resolve on next launch'}}
}
function Invoke-HubProtocol([string]$RequestPath,[string]$ResultPath){
 try{
  if((Get-Item -LiteralPath $RequestPath).Length -gt 6291456){throw 'Request exceeds 6 MiB'}
  $request=Read-HubRequestText (Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8)
  $result=switch($request.kind){
   'Preflight'{Get-HubPreflight}
   'Inspect'{$target=Get-HubTarget $request.gameExecutable;[ordered]@{status='Inspected';inspection=(Get-HubInspection $target)}}
   'LaunchPreflight'{Get-HubLaunchPreflight $request}
   'RestoreDefaults'{Resolve-HubSettingsDefaults $request}
   'Plan'{New-HubPlan $request}
   'Execute'{Invoke-HubExecute $request}
  }
 }catch{$result=[ordered]@{status='FailedWithoutChanges';reason=$_.Exception.Message};$coded=$_.Exception;while($coded -and -not $coded.Data.Contains('NeuroticReasonCode')){$coded=$coded.InnerException};if($coded){$result.reasonCode=$coded.Data['NeuroticReasonCode'];$result.messageParameters=@{}};$evidence=$_.Exception.Data['NeuroticOperationEvidence'];if($evidence){foreach($name in $evidence.Keys){$result[$name]=$evidence[$name]}};if($env:NEUROTIC_HUB_FIXTURE_ROOT){$result.diagnostic=$_.ScriptStackTrace}}
 if($result -is [Collections.IDictionary]){$result.protocolVersion=1}else{$result|Add-Member -NotePropertyName protocolVersion -NotePropertyValue 1}
 SaveRecord $ResultPath $result
}
