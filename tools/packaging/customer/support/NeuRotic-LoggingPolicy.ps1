# Install/update diagnostic policy from the already selected package manifest.
# This does not select a renderer, enable NR, or claim an effective runtime level.
function Get-NeuroticLoggingHash([byte[]]$Bytes){
 $sha=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function New-NeuroticLoggingPolicy($Package){
 $requested=Get-NeuroticField (Get-NeuroticField $Package 'manifest') 'loggingProfile' ''
 if($null -eq $requested){$requested=''}
 if($requested -isnot [string] -or $requested.Length -gt 64 -or $requested -match '[\x00-\x1f]'){throw 'Package logging policy is malformed'}
 $digest=Get-NeuroticField $Package 'digest' ''
 if($null -eq $digest){$digest=''}
 if($digest -isnot [string] -or ($digest -and $digest -notmatch '\A[0-9a-fA-F]{64}\z')){throw 'Package diagnostic identity is malformed'}
 $evidence=[pscustomobject][ordered]@{
  schemaVersion=1;profile=$requested;packageManifestSha256=$digest.ToLowerInvariant();packageDigestKnown=[bool]$digest
  action='None';status=$(if($requested -ceq 'internal-testing'){'Pending'}elseif($requested -ceq 'public'){'Preserved'}elseif($requested){'UnknownProfile'}else{'NotRequested'})
  beforeStage='BeforeLoggingDelta';beforeValuesKnown=$false;afterValuesKnown=$false;beforeValuesTruncated=$false;afterValuesTruncated=$false
  beforeLogToFile=$null;beforeLogLevel=$null;afterLogToFile=$null;afterLogLevel=$null
  beforeSha256='';afterSha256='';beforeIdentity='';afterIdentity='';writeAttempted=$false;patched=$false;failureCode=''
 }
 return [pscustomobject]@{profile=$requested;evidence=$evidence;action=$null;snapshot=$null;bytes=$null;patches=@()}
}
function Set-NeuroticLoggingObservation($Evidence,[string]$Stage,$Snapshot){
 $Evidence.($Stage+'Sha256')=$Snapshot.Revision;$Evidence.($Stage+'Identity')=$Snapshot.Identity
 $document=Read-HubSettingsBytesDocument $Snapshot.Bytes
 $sections=@($document.sections|Where-Object{$_.name -ieq 'Log'})
 if($sections.Count -gt 1){throw 'Resolve duplicate Log sections before configuring diagnostic logging'}
 $truncated=$false
 foreach($key in @('LogToFile','LogLevel')){
  $entries=@($document.entries|Where-Object{$_.section -ieq 'Log' -and $_.key -ieq $key})
  if($entries.Count -gt 1){throw "Resolve duplicate Log/$key entries before configuring diagnostic logging"}
  $value=$null
  if($entries.Count){$value=[string]$entries[0].value;if($value.Length -gt 64){$value=$value.Substring(0,64);$truncated=$true}}
  $Evidence.($Stage+$key)=$value
 }
 $Evidence.($Stage+'ValuesKnown')=$true;$Evidence.($Stage+'ValuesTruncated')=$truncated
 return $document
}
function Prepare-NeuroticLoggingPolicy($Plan,[string]$GameRoot,$Actions,$Rename){
 $configuration=@($Actions|Where-Object{$_.path -ieq 'OptiScaler.ini'})
 if($configuration.Count -eq 1){$Plan.action=$configuration[0];$Plan.evidence.action=$configuration[0].operation}
 if($Plan.profile -cne 'internal-testing'){return}
 try{
  $Plan.patches=@([pscustomobject]@{section='Log';key='LogToFile';value='true'},[pscustomobject]@{section='Log';key='LogLevel';value='0'})
  if($Rename){$Plan.patches+=,[pscustomobject]@{section='Plugins';key='LoadReShade';value='true'}}
  if($Plan.action -and $Plan.action.operation -eq 'Reuse'){
   $path=Resolve-NeuroticOwnedPath $GameRoot 'OptiScaler.ini'
   $Plan.snapshot=[NeuRoticSimpleFileIo]::ReadSettings($path,$Plan.action.identity)
   $document=Set-NeuroticLoggingObservation $Plan.evidence 'before' $Plan.snapshot
   $Plan.bytes=Get-HubSettingsDocumentBytes $document $Plan.patches
  }
 }catch{$Plan.evidence.status='Failed';$Plan.evidence.failureCode='ConfigurationPreflightFailed';throw}
}
function Invoke-NeuroticLoggingPolicy($Plan,[string]$GameRoot,$Result,[scriptblock]$OwnershipCommitted=$null,$OwnershipContext=$null){
 if($Plan.profile -cne 'internal-testing'){return}
 $path=Resolve-NeuroticOwnedPath $GameRoot 'OptiScaler.ini'
 try{
  if($Result.copied -notcontains 'OptiScaler.ini' -and $Result.reused -notcontains 'OptiScaler.ini'){
   $Plan.evidence.status='Skipped';$Plan.evidence.failureCode='ConfigurationDeliveryIncomplete'
   throw 'Diagnostic logging could not be configured because the configuration was not delivered'
  }
  if(-not $Plan.snapshot){
   $Plan.snapshot=[NeuRoticSimpleFileIo]::ReadSettings($path,$null)
   $document=Set-NeuroticLoggingObservation $Plan.evidence 'before' $Plan.snapshot
   $Plan.bytes=Get-HubSettingsDocumentBytes $document $Plan.patches
  }
  $desired=Get-NeuroticLoggingHash $Plan.bytes
  Invoke-NeuroticSimpleBoundary 'ConfigureLogging' 'OptiScaler.ini'
  if($desired -cne $Plan.snapshot.Revision){
   $Plan.evidence.writeAttempted=$true
   if(-not [NeuRoticSimpleFileIo]::WriteSettings($Plan.bytes,$path,$Plan.snapshot.Revision,$Plan.snapshot.Identity)){
    throw 'Configuration changed externally; reload and retry the installation'
   }
   # Publish only a completed write, before subsequent inspection can fail.
   if($OwnershipCommitted){& $OwnershipCommitted $GameRoot $OwnershipContext}
  }else{
   $current=[NeuRoticSimpleFileIo]::ReadSettings($path,$Plan.snapshot.Identity)
   if($current.Revision -cne $Plan.snapshot.Revision){throw 'Configuration changed externally; reload and retry the installation'}
  }
  $after=[NeuRoticSimpleFileIo]::ReadSettings($path,$null)
  [void](Set-NeuroticLoggingObservation $Plan.evidence 'after' $after)
  if($after.Revision -cne $desired -or $Plan.evidence.afterLogToFile -cne 'true' -or $Plan.evidence.afterLogLevel -cne '0'){
   throw 'Configuration changed after diagnostic logging was saved; reload and retry the installation'
  }
  $Plan.evidence.patched=$Plan.evidence.writeAttempted
  $Plan.evidence.status=$(if($Plan.evidence.patched){'Applied'}else{'AlreadyConfigured'})
 }catch{
  if($Plan.evidence.status -ne 'Skipped'){$Plan.evidence.status='Failed';$Plan.evidence.failureCode='LoggingConfigurationFailed'}
  try{$after=[NeuRoticSimpleFileIo]::ReadSettings($path,$null);[void](Set-NeuroticLoggingObservation $Plan.evidence 'after' $after)}catch{}
  throw
 }
}
function ConvertTo-NeuroticLoggingPolicyReceipt($Record){
 if($Record.schemaVersion -ne 1 -or $Record.status -cnotin @('Pending','Preserved','UnknownProfile','NotRequested','Applied','AlreadyConfigured','Skipped','Failed') -or $Record.action -cnotin @('None','Copy','Reuse')){throw 'Invalid logging diagnostic receipt'}
 if($Record.profile -isnot [string] -or $Record.profile.Length -gt 64 -or $Record.profile -match '[\x00-\x1f]' -or $Record.beforeStage -cne 'BeforeLoggingDelta'){throw 'Invalid logging diagnostic policy'}
 $normalized=[ordered]@{schemaVersion=1;profile=$Record.profile;action=$Record.action;status=$Record.status;beforeStage='BeforeLoggingDelta'}
 foreach($name in @('packageDigestKnown','beforeValuesKnown','afterValuesKnown','beforeValuesTruncated','afterValuesTruncated','writeAttempted','patched')){
  if($Record.$name -isnot [bool]){throw 'Invalid logging diagnostic flag'};$normalized[$name]=$Record.$name
 }
 foreach($name in @('packageManifestSha256','beforeSha256','afterSha256')){
  $value=$Record.$name;if($value -isnot [string] -or ($value -and $value -notmatch '\A[0-9a-fA-F]{64}\z')){throw 'Invalid logging diagnostic digest'};$normalized[$name]=$value.ToLowerInvariant()
 }
 foreach($name in @('beforeIdentity','afterIdentity','failureCode')){
  $value=$Record.$name;if($value -isnot [string] -or $value.Length -gt 256 -or $value -match '[\x00-\x1f]'){throw 'Invalid logging diagnostic identity'};$normalized[$name]=$value
 }
 foreach($name in @('beforeLogToFile','beforeLogLevel','afterLogToFile','afterLogLevel')){
  $value=$Record.$name;if($null -ne $value -and ($value -isnot [string] -or $value.Length -gt 64)){throw 'Invalid logging diagnostic value'};$normalized[$name]=$value
 }
 return [pscustomobject]$normalized
}
