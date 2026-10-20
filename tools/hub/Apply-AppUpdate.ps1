param([string]$InstallDirectory,[switch]$Rollback,[int]$TestFailAfter=-1)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-AppUpdateSafety.cs')
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function OwnedPath([string]$root,[string]$relative){
 if([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)|:|(^|[\\/])nvngx_dlssnr\.dll$'){throw 'Unsafe application update member'}
 $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
 if(-not $path.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Update member leaves the application folder'}
 return $path
}
function SaveJournal {
 $bytes=[Text.Encoding]::UTF8.GetBytes(($script:journal|ConvertTo-Json -Depth 8))
 $expected=if([IO.File]::Exists($script:journalPath)){Hash $script:journalPath}else{'Absent'}
 $hash=([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes))).Replace('-','')
 if(-not [NeuRoticAppUpdateFiles]::Write($bytes,$script:journalPath,$hash,$expected)){throw 'The update recovery record changed'}
}
function RestoreUpdate {
 foreach($row in @($script:manifest.files)){
  $target=OwnedPath $script:install $row.path;$backup=OwnedPath $script:backup $row.path
  $current=if([IO.File]::Exists($target)){Hash $target}else{$null}
  if($row.previousSha256){
   if($current -eq $row.previousSha256){continue}
   if($row.action -eq 'remove'){
    if($current){throw "Changed file preserved: $($row.path). Original is in $script:backup"}
   }elseif($current -ne $row.sha256){throw "Changed file preserved: $($row.path). Original is in $script:backup"}
   if(-not [IO.File]::Exists($backup) -or (Hash $backup) -ne $row.previousSha256){throw 'A verified original backup is missing'}
   [NeuRoticAppUpdateFiles]::Copy($backup,$target,$row.previousSha256,$(if($current){$current}else{'Absent'}))
  }elseif($current){
   if($current -ne $row.sha256){throw "New user content preserved: $($row.path)"}
   [NeuRoticAppUpdateFiles]::Delete($target,$row.sha256)
  }
 }
 $script:journal.phase='RolledBack';SaveJournal
 Write-Output 'The previous NeuRotic application files are restored. Settings and private runtime files were preserved.'
}
if(-not $InstallDirectory){
 Add-Type -AssemblyName System.Windows.Forms
 $picker=New-Object Windows.Forms.OpenFileDialog;$picker.Title='Choose your installed NeuRotic application';$picker.Filter='NeuRotic application|NeuRotic.Hub.exe'
 try{if($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){Write-Output 'Canceled. No files changed.';exit 0};$InstallDirectory=Split-Path -Parent $picker.FileName}finally{$picker.Dispose()}
}
$script:install=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
if(-not [IO.File]::Exists((Join-Path $script:install 'NeuRotic.Hub.exe'))){throw 'Choose the folder containing your installed NeuRotic application'}
foreach($process in @(Get-Process -Name 'NeuRotic.Hub' -ErrorAction SilentlyContinue)){if($process.Path -and [IO.Path]::GetFullPath($process.Path) -eq (Join-Path $script:install 'NeuRotic.Hub.exe')){throw 'Close NeuRotic, then run this application update again.'}}
$script:manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'APP-UPDATE-MANIFEST.json') -Raw|ConvertFrom-Json
if($script:manifest.kind -ne 'neurotic-app-delta' -or $script:manifest.schemaVersion -ne 1 -or $script:manifest.id -notmatch '^[a-f0-9]{16}$'){throw 'Unsupported application update'}
if($TestFailAfter -ge 0 -and (-not $env:NEUROTIC_APP_UPDATE_TEST_ROOT -or $script:install -ne [IO.Path]::GetFullPath($env:NEUROTIC_APP_UPDATE_TEST_ROOT).TrimEnd('\'))){throw 'Failure injection belongs only to an explicit test fixture'}
$lease=[NeuRoticAppUpdateFiles]::PinTarget($script:install)
$mutex=New-Object Threading.Mutex($false,('Global\NeuRotic-AppUpdate-'+[NeuRoticAppUpdateFiles]::Identity($script:install)))
$entered=$false
try {
 try{$entered=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$entered=$true}
 if(-not $entered){throw 'Another application update owns this folder.'}
 $script:backup=Join-Path $script:install ('.neurotic-app-update/'+$script:manifest.id)
 $script:journalPath=Join-Path $script:backup 'RECOVERY.json'
 $identity=[NeuRoticAppUpdateFiles]::Identity($script:install)
 if([IO.File]::Exists($script:journalPath)){
  $script:journal=Get-Content -LiteralPath $script:journalPath -Raw|ConvertFrom-Json
  if($script:journal.id -ne $script:manifest.id -or $script:journal.targetIdentity -ne $identity){throw 'Recovery record belongs to another application folder'}
 }else{$script:journal=[ordered]@{id=$script:manifest.id;targetIdentity=$identity;phase='Preparing'}}
 foreach($row in $script:manifest.files){
  if($row.action -notin @('put','remove') -or ($row.previousSha256 -and $row.previousSha256 -notmatch '^[a-f0-9]{64}$')){throw 'Invalid application update entry'}
  [void](OwnedPath $script:install $row.path)
  if(-not $Rollback -and $row.action -eq 'put'){$source=OwnedPath $PSScriptRoot ('NeuRotic/'+$row.path);if($row.sha256 -notmatch '^[a-f0-9]{64}$' -or (Hash $source) -ne $row.sha256){throw "Update download is incomplete or changed: $($row.path)"}}
 }
 if(-not $Rollback){foreach($dependency in $script:manifest.dependencies){$path=OwnedPath $script:install $dependency.path;if(-not [IO.File]::Exists($path) -or (Hash $path) -ne $dependency.sha256){throw "This update requires the installed consolidated flagship dependency: $($dependency.path). No application files were changed."}}}
 if($Rollback -and ($script:journal.PSObject.Properties.Name -contains 'manifestSha256') -and $script:journal.manifestSha256 -ne (Hash (Join-Path $PSScriptRoot 'APP-UPDATE-MANIFEST.json'))){throw 'Recovery manifest differs from the committed update'}
 if($Rollback){if($script:journal.phase -eq 'Preparing'){throw 'There is no prepared application update to roll back'};RestoreUpdate;exit 0}
 if($script:journal.phase -eq 'Committed'){
  foreach($row in $script:manifest.files){$target=OwnedPath $script:install $row.path;$current=if([IO.File]::Exists($target)){Hash $target}else{$null};if(($row.action -eq 'put' -and $current -ne $row.sha256) -or ($row.action -eq 'remove' -and $current)){throw "Updated application content changed: $($row.path). It was preserved; review your application folder before retrying."}}
  Write-Output 'This application update is already installed.';exit 0
 }
 if($script:journal.phase -notin @('Preparing','RolledBack')){throw 'An interrupted application update needs recovery. Run Rollback NeuRotic App Update.cmd first.'}
 foreach($row in $script:manifest.files){
  $target=OwnedPath $script:install $row.path;$current=if([IO.File]::Exists($target)){Hash $target}else{$null}
  if($current -ne $row.previousSha256){throw "Application file differs from the supported baseline: $($row.path). It was preserved; use the matching consolidated flagship folder."}
 }
 foreach($row in $script:manifest.files){if($row.previousSha256){$target=OwnedPath $script:install $row.path;$saved=OwnedPath $script:backup $row.path;
  if([IO.File]::Exists($saved)){if((Hash $saved) -ne $row.previousSha256){throw 'Original backup changed; it was preserved'}}else{[NeuRoticAppUpdateFiles]::Copy($target,$saved,$row.previousSha256,'Absent')}
 }}
 # Recovery uses only this durable local tool/manifest set and verified preimages.
 $recoveryTools=Join-Path $script:backup 'recovery-tools'
 foreach($name in @('Apply-AppUpdate.ps1','NeuRotic-AppUpdateSafety.cs','APP-UPDATE-MANIFEST.json')){
  $source=Join-Path $PSScriptRoot $name;$target=Join-Path $recoveryTools $name;$hash=Hash $source
  if([IO.File]::Exists($target)){if((Hash $target) -ne $hash){throw 'Local recovery tools changed; preserve the original recovery folder'}}
  else{[NeuRoticAppUpdateFiles]::Copy($source,$target,$hash,'Absent')}
 }
 $script:journal|Add-Member -NotePropertyName manifestSha256 -NotePropertyValue (Hash (Join-Path $recoveryTools 'APP-UPDATE-MANIFEST.json')) -Force
 $restoreText='@echo off'+"`r`n"+'"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0recovery-tools\Apply-AppUpdate.ps1" -InstallDirectory "%~dp0..\.." -Rollback'+"`r`npause`r`n"
 $restoreBytes=[Text.Encoding]::ASCII.GetBytes($restoreText);$restoreHash=([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($restoreBytes))).Replace('-','')
 if(-not [NeuRoticAppUpdateFiles]::Write($restoreBytes,(Join-Path $script:backup 'Restore.cmd'),$restoreHash,$null)){throw 'Recovery launcher could not be written'}
 $script:journal.phase='Applying';SaveJournal;$count=0
 try {
  foreach($row in $script:manifest.files){
   if($TestFailAfter -eq $count){throw 'Injected application-update failure'}
   $target=OwnedPath $script:install $row.path
   if($row.action -eq 'put'){[NeuRoticAppUpdateFiles]::Copy((OwnedPath $PSScriptRoot ('NeuRotic/'+$row.path)),$target,$row.sha256,$(if($row.previousSha256){$row.previousSha256}else{'Absent'}))}
   else{[NeuRoticAppUpdateFiles]::Delete($target,$row.previousSha256)}
   $count++;Write-Output ("Updating NeuRotic: $count of $($script:manifest.files.Count) files")
  }
  $script:journal.phase='Committed';SaveJournal
 }catch{
  $reason=$_.Exception.Message
  try{RestoreUpdate}catch{throw "Update stopped: $reason. Recovery could not complete: $($_.Exception.Message). Original application files are retained in $script:backup"}
  throw "Update stopped: $reason. The previous application files were restored; no settings or private runtime files were removed."
 }
 Write-Output 'NeuRotic application updated. Open NeuRotic.Hub.exe as usual. Settings and private runtime files are preserved.'
}finally{if($entered){$mutex.ReleaseMutex()};$mutex.Dispose();$lease.Dispose()}
