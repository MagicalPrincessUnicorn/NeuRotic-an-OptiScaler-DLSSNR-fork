[CmdletBinding()]
param([string]$GameExecutable,[string]$ProxyName,[switch]$FreshInstall,[switch]$Uninstall,
 [string]$GameDirectory,[switch]$Installed,[switch]$CheckOnly,[switch]$ConfirmInstall,
 [switch]$ConfirmUninstall,[switch]$ContinueWithoutRuntime,[string]$ExistingProxyAction,
 [string]$FileDecisionsJson,[string]$HubRequestPath,[string]$HubResultPath,
 [string]$HubPackageRoot,[switch]$HubNonInteractive)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NeuRotic-InstallCore.ps1')
function HashBytes([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function HashFile([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function SafePath([string]$Root,[string]$Relative){return Resolve-NeuroticOwnedPath $Root $Relative}
function SaveRecord([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 16),(New-Object Text.UTF8Encoding($false)))}
function WriteVerifiedBytes([byte[]]$Bytes,[string]$Path,[string]$NewDigest,[string]$Revision){if((HashBytes $Bytes) -ine $NewDigest){throw 'Invalid settings bytes'};return [NeuRoticSimpleFileIo]::WriteSettings($Bytes,$Path,$Revision)}
function Show-SetupProgress([string]$Stage,[int]$Done=0,[int]$Total=0,[string]$Detail=''){Write-Output ('NRPROGRESS '+([ordered]@{stage=$Stage;done=$Done;total=$Total;detail=$Detail}|ConvertTo-Json -Compress))}
function Get-HubProperty($Object,[string]$Name,$Default=$null){return Get-NeuroticField $Object $Name $Default}
. (Join-Path $PSScriptRoot 'NeuRotic-RuntimeFiles.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubSettings.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-AntiCheatPolicy.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubAntiCheat.ps1')
. (Join-Path $PSScriptRoot 'NeuRotic-HubProtocol.ps1')
if($HubRequestPath){if(-not $HubResultPath){throw 'A protocol result path is required'};Invoke-HubProtocol $HubRequestPath $HubResultPath;return}
try{
 if($Uninstall -and $GameDirectory){$result=Invoke-NeuroticSimpleUninstall $GameDirectory}
 else{
  if(-not $GameExecutable){
   Add-Type -AssemblyName System.Windows.Forms;$picker=New-Object Windows.Forms.OpenFileDialog
   $picker.Title=$(if($Uninstall){'NeuRotic Uninstall - select game executable'}else{'NeuRotic Install - select game executable'});$picker.Filter='Game executable (*.exe)|*.exe'
   try{if($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){Write-Output 'Cancelled. No files were changed.';return};$GameExecutable=$picker.FileName}finally{$picker.Dispose()}
  }
  if($Uninstall){$result=Invoke-NeuroticSimpleUninstall (Split-Path -Parent ([IO.Path]::GetFullPath($GameExecutable)))}
  else{
   $target=Get-HubTarget $GameExecutable
   if($target.bitness -ne 64){throw 'A compatible in-game integration is unavailable for this architecture. Use NR Anything.'}
   if(-not $ProxyName){
    $read=Read-NeuroticInstallReceipt $target.directory;$ProxyName=Get-HubProperty $read.receipt 'proxy'
    if(-not $ProxyName){for($i=0;$i -lt $script:NeuroticSimpleProxies.Count;$i++){Write-Host (($i+1).ToString()+'. '+$script:NeuroticSimpleProxies[$i])};$choice=0;if(-not [int]::TryParse((Read-Host 'Proxy filename (0 cancels)'),[ref]$choice) -or $choice -lt 1 -or $choice -gt $script:NeuroticSimpleProxies.Count){return};$ProxyName=$script:NeuroticSimpleProxies[$choice-1]}
   }
   $request=[pscustomobject]@{protocolVersion=1;kind='Plan';operation='Install';gameExecutable=$GameExecutable;packageId='flagship-approved';proxyName=$ProxyName;freshInstall=[bool]$FreshInstall;continueWithoutRuntime=[bool]$ContinueWithoutRuntime}
   if($ExistingProxyAction){$request|Add-Member -NotePropertyName existingProxyAction -NotePropertyValue $ExistingProxyAction}
   if($FileDecisionsJson){$request|Add-Member -NotePropertyName fileDecisions -NotePropertyValue @($FileDecisionsJson|ConvertFrom-Json)}
   $manualPackage=$null;if($HubPackageRoot){$manualPackage=Get-HubPackageAtRoot $HubPackageRoot 'flagship-approved'}elseif(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json')){$manualPackage=Get-HubPackageAtRoot (Split-Path -Parent $PSScriptRoot) 'flagship-approved'}
   while($true){
    $plan=New-HubPlan $request $manualPackage
    if($plan.status -ne 'NeedsDecision' -or $plan.decisionKind -ne 'FileConflict'){break}
    $choices=@(Get-HubProperty $request 'fileDecisions' @())
    foreach($file in $plan.fileConflicts){
     Write-Host ($file.path+' appears to not have come from NeuRotic. How should we proceed?')
     Write-Host '1. Replace';Write-Host '2. Skip';Write-Host '0. Cancel'
     if($file.path -ieq 'dxgi.dll' -and $plan.canKeepReShade){Write-Host '3. Keep ReShade (rename dxgi.dll to ReShade64.dll)'}
     $answer=Read-Host 'Choose an option'
     if($answer -eq '3' -and $file.path -ieq 'dxgi.dll' -and $plan.canKeepReShade){$request|Add-Member -NotePropertyName existingProxyAction -NotePropertyValue 'RenameReShade' -Force;$action='Replace'}else{$action=$(switch($answer){'1'{'Replace'}'2'{'Skip'}default{'Cancel'}});if($request.existingProxyAction -eq 'RenameReShade'){$request.PSObject.Properties.Remove('existingProxyAction')}}
     $choices+=,[pscustomobject]@{path=$file.path;action=$action};if($action -eq 'Cancel'){Write-Output 'Cancelled. No files were changed.';return}
    }
    $request|Add-Member -NotePropertyName fileDecisions -NotePropertyValue @($choices) -Force
   }
   if($plan.status -ne 'Planned'){$result=$plan}
   elseif($CheckOnly){Write-Output 'Selected-target and package checks passed. No game files were changed.';return}
   else{
    $acknowledged=$false
    if($plan.antiCheat -and $plan.antiCheat.acknowledgementRequired){Write-Host $plan.antiCheat.reason;$acknowledged=(Read-Host 'Type I UNDERSTAND to continue, or press Enter to cancel') -ceq 'I UNDERSTAND';if(-not $acknowledged){Write-Output 'Cancelled. No files were changed.';return}}
    $result=Invoke-HubExecute ([pscustomobject]@{planId=$plan.planId;planFingerprint=$plan.planFingerprint}) $acknowledged
   }
  }
 }
 Write-Output ($result|ConvertTo-Json -Depth 8)
 if($result.status -notin @('Succeeded','SucceededWithNotes','Cancelled')){exit 2}
}catch{Write-Error $_;exit 2}
