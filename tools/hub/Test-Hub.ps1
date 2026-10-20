[CmdletBinding()]
param([ValidateSet('NH-00','NH-01','NH-02','NH-03','NH-04','NH-05','NH-06','All')][string]$Suite='All',[switch]$RequireGpu)
$ErrorActionPreference='Stop'
if($RequireGpu){Write-Output 'Blocked: local Hub fixtures do not qualify a GPU provider';exit 3}
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$supported=[ordered]@{
 'NH-00'=@('BaselineBinding.Tests.ps1')
 'NH-03'=@('SimpleInstallerParity.Tests.ps1','Preflight.Tests.ps1','PersistentRuntime.Tests.ps1')
 'NH-04'=@('SimpleReceipt.Tests.ps1','SimpleProfiles.Tests.ps1','SimpleInstall.Tests.ps1','CharacterVersionInstall.Tests.ps1')
 'NH-05'=@('SimpleUninstall.Tests.ps1','SimpleUninstallRetry.Tests.ps1','SimpleLegacyInstall.Tests.ps1','AntiCheat.Tests.ps1','AntiCheatPolicy.Tests.ps1','AntiCheatCollector.Tests.ps1','AntiCheatCancellation.Tests.ps1')
 'NH-06'=@('Settings.Tests.ps1','SettingsWriteBoundary.Tests.ps1','SettingsSchema.Tests.ps1','KeybindSchema.Tests.ps1')
}
try{
 # Source-backed tiny authority fixtures; package approval remains a separate
 # full-strength assembly check. Do not reuse a stale approved package here.
 & (Join-Path $PSScriptRoot 'Stage-Hub.ps1') -SupportOnly
 foreach($entry in $supported.GetEnumerator()){
  if($Suite -ne 'All' -and $Suite -ne $entry.Key){continue}
  foreach($name in $entry.Value){$file=Join-Path $root ('tests/hub/'+$name);if(-not(Test-Path -LiteralPath $file)){throw ('Missing supported fixture: '+$name)};& $file}
 }
 if($Suite -in @('All','NH-02')){
  $helper=Join-Path $root 'builds/hub/bin/discovery/NeuRotic.Discovery.exe';if(-not(Test-Path -LiteralPath $helper)){throw 'Steam discovery binary unavailable'}
  & $helper --self-test (Join-Path $root 'builds/hub/discovery-fixtures');if($LASTEXITCODE -ne 0){throw 'Unified discovery tests failed'}
  foreach($name in @('UnifiedDiscovery.Tests.ps1','FolderDiscovery.Tests.ps1','LibraryRoots.Tests.ps1')){& (Join-Path $root ('tests/hub/'+$name))}
 }
 if($Suite -in @('All','NH-01','NH-02')){
  $native=Join-Path $root 'builds/hub/bin/NeuRotic.exe';if(-not(Test-Path -LiteralPath $native)){throw 'Native Hub binary unavailable'}
  $process=Start-Process -FilePath $native -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $root 'builds/hub/native-tests.log') -RedirectStandardError (Join-Path $root 'builds/hub/native-tests-errors.log')
  Get-Content -LiteralPath (Join-Path $root 'builds/hub/native-tests.log');Get-Content -LiteralPath (Join-Path $root 'builds/hub/native-tests-errors.log')
  if($process.ExitCode -ne 0){throw 'Native Hub tests failed'}
 }
 Write-Output "PASS suite $Suite";exit 0
}catch{Write-Output "FAIL: $($_.Exception.Message)";exit 1}
