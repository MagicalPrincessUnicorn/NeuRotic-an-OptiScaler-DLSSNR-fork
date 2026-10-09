# Four installation preferences, never a replacement game INI. Missing fields
# inherit package defaults. Read once per operation and bind the snapshot to Plan.
function ConvertTo-NeuroticInstallDefaults($Document){
 if($null -eq $Document -or $Document -is [Array] -or $Document -is [string]){throw 'Invalid install defaults object'}
 $names=@($Document.PSObject.Properties.Name)
 if($Document -is [Collections.IDictionary]){$names=@($Document.Keys)}
 if(@($names|Where-Object{$_ -cnotin @('schemaVersion','nrMode','modelPreset','style','mfgUnlock','mfgConsent')}).Count){throw 'Unknown install default'}
 $version=Get-NeuroticField $Document 'schemaVersion'
 if(($version -isnot [int] -and $version -isnot [long]) -or $version -ne 1){throw 'Unsupported install defaults version'}
 $patches=@()
 foreach($field in @(@('nrMode','DlssNr','Route',@(0,2,3)),@('style','DlssNr','Style',@(0,1,2)))){
  if($names -ccontains $field[0]){
   $value=Get-NeuroticField $Document $field[0]
   if(($value -isnot [int] -and $value -isnot [long]) -or $value -notin $field[3]){throw ('Invalid install default: '+$field[0])}
   $patches+=,[pscustomobject]@{section=$field[1];key=$field[2];value=[string]$value;policy='default'}
  }
 }
 if($names -ccontains 'modelPreset'){
  $value=Get-NeuroticField $Document 'modelPreset'
  if($value -isnot [string] -or $value -cnotin @('auto','0','1','2','3')){throw 'Invalid model preset default'}
  $patches+=,[pscustomobject]@{section='DlssNr';key='Preset';value=$value;policy='default'}
 }
 $consent=Get-NeuroticField $Document 'mfgConsent' $false
 if($consent -isnot [bool]){throw 'Invalid MFG consent'}
 if($names -ccontains 'mfgUnlock'){
  $value=Get-NeuroticField $Document 'mfgUnlock'
  if($value -isnot [string] -or $value -cnotin @('off','rtx40','rtx30','rtx20') -or ($value -cne 'off' -and -not $consent)){throw 'MFG unlock requires explicit experimental consent'}
  foreach($choice in @(@('rtx40','NativeMfgExperimental'),@('rtx30','ExperimentalUnlockRTX30'),@('rtx20','ExperimentalUnlockRTX20'))){
   $patches+=,[pscustomobject]@{section='DLSSG';key=$choice[1];value=($value -ceq $choice[0]).ToString().ToLowerInvariant();policy='default'}
  }
 }elseif($consent){throw 'MFG consent requires an unlock selection'}
 return [pscustomobject]@{status='Loaded';preferences=$Document;patches=@($patches);note=''}
}
function Read-NeuroticInstallDefaults {
 try{
  $root=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NeuRotic/HubData-v2'
  if($env:NEUROTIC_HUB_FIXTURE_ROOT){$root=$env:NEUROTIC_HUB_FIXTURE_ROOT}
  $path=[IO.Path]::GetFullPath((Join-Path $root 'install-defaults.json'));$cursor=$path
  while($cursor){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked install defaults path'}};$parent=Split-Path -Parent $cursor;if($parent -eq $cursor){break};$cursor=$parent}
  if(-not [IO.File]::Exists($path)){return [pscustomobject]@{status='PackageDefaults';preferences=$null;patches=@();note=''}}
  $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try{if($stream.Length -gt 4096){throw 'Install defaults exceed the size limit'};$reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false,$true),$false);try{$text=$reader.ReadToEnd();if($text.Length -gt 0 -and $text[0] -eq [char]0xfeff){$text=$text.Substring(1)}}finally{$reader.Dispose()}}finally{$stream.Dispose()}
  if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-HubJson.cs')}
  [NeuRoticHubJson]::Validate($text)
  return ConvertTo-NeuroticInstallDefaults ($text|ConvertFrom-Json)
 }catch{return [pscustomobject]@{status='Invalid';preferences=$null;patches=@();note='Install defaults could not be read. Package defaults were used; the preference file was preserved.';noteReasonCode='desktop.installdefaults.package_defaults_fallback';noteMessageParameters=@{}}}
}
function Resolve-NeuroticInstallDefaults($Requirements,[bool]$Enabled,$Snapshot,$ProfilePatches,[bool]$Eligible){
 $global=@();if($Eligible -and $Snapshot.status -eq 'Loaded'){$global=@((ConvertTo-NeuroticInstallDefaults $Snapshot.preferences).patches)}
 $special=@($ProfilePatches|Where-Object{-not (Get-NeuroticField $_ 'path') -or $_.path -ieq 'OptiScaler.ini'});$overrides=@()
 foreach($patch in $global){$winner=@($special|Where-Object{$_.section -ieq $patch.section -and $_.key -ieq $patch.key})|Select-Object -Last 1
  if($winner -and $winner.value -cne $patch.value){$overrides+=,[pscustomobject]@{section=$patch.section;key=$patch.key;requested=$patch.value;effective=$winner.value;reason='Installation-specific configuration'}}
 }
 return [pscustomobject]@{patches=@(Get-NeuroticAutomaticSetupPatches $Requirements $Enabled)+$global+$special;overrides=@($overrides)}
}
