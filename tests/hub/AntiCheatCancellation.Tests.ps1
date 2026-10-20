$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=Join-Path $repo 'tools/packaging/customer/support'
$work=Join-Path $repo 'builds/hub/anticheat-cancel-fixtures'
if(Test-Path $work){throw 'Inspect retained cancellation fixtures before reuse'}
[IO.Directory]::CreateDirectory($work)|Out-Null
. (Join-Path $source 'NeuRotic-HubAntiCheat.ps1')
function HashBytes($bytes){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function Get-HubProperty($object,[string]$name,$default=$null){if($object.PSObject.Properties.Name -contains $name){return $object.$name};return $default}
function AssertCancelled($report,[string]$label){if($report.scan_status -cne 'cancelled' -or $report.headline -cne 'cancelled' -or $report.requires_ack -or $report.acknowledgementRequired){throw $label};Write-Output ('PASS '+$label)}
try{
 $manual=Join-Path $work 'Manual';[IO.Directory]::CreateDirectory($manual)|Out-Null;$exe=Join-Path $manual 'Fixture.exe';Copy-Item (Join-Path $env:SystemRoot 'System32/cmd.exe') $exe
 $request=[pscustomobject]@{operation='Install'};$target=[pscustomobject]@{directory=$manual;executable=$exe;sha256=(Get-FileHash $exe).Hash}
 Initialize-HubAntiCheatCollector
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_enumerate'){throw (New-Object OperationCanceledException)}}
 AssertCancelled (Get-HubAntiCheat $request $target) 'Cancelled manual root stays cancelled rather than acknowledgeable incomplete'
 function Read-HubAntiCheatDataset{return [pscustomobject]@{status='Unavailable';id=$null;reviewDate=$null;sha256=$null;reason='synthetic catalog unavailable';games=@()}}
 AssertCancelled (Get-HubAntiCheat $request $target) 'Cancelled scan stays cancelled when catalog is unavailable'
 $library=Join-Path $work 'Steam';$root=Join-Path $library 'steamapps/common/Owned';[IO.Directory]::CreateDirectory($root)|Out-Null;$ownedExe=Join-Path $root 'Owned.exe';Copy-Item $exe $ownedExe
 [IO.File]::WriteAllText((Join-Path $library 'steamapps/appmanifest_12345.acf'),'"AppState" { "appid" "12345" "installdir" "Owned" }')
 function Read-HubAntiCheatDataset{return [pscustomobject]@{status='Available';id='synthetic-catalog';reviewDate='2026-10-03';sha256=('a'*64);reason=$null;games=@([pscustomobject]@{id='owned';title='Owned';identity_match_enabled=$true;steam_app_ids=@('12345');anti_cheat_reported=@('Unmapped provider metadata')})}}
 $ownedRequest=[pscustomobject]@{operation='Install';steamAppId='12345';steamRoot=$library;gameRoot=$root};$ownedTarget=[pscustomobject]@{directory=$root;executable=$ownedExe;sha256=(Get-FileHash $ownedExe).Hash}
 $cancelled=Get-HubAntiCheat $ownedRequest $ownedTarget;AssertCancelled $cancelled 'Curated supplemental game metadata cannot replace cancelled headline';if($cancelled.games.Count -ne 1){throw 'Curated context erased after cancellation'}
 function Read-HubProviderRules{return $null}
 AssertCancelled (Get-HubAntiCheat $request $target) 'Missing provider rules cannot turn cancellation into an approval warning'
 . (Join-Path $source 'NeuRotic-HubProtocol.ps1')
 . (Join-Path $source 'NeuRotic-InstallCore.ps1')
 . (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1');$script:SimpleWork=$work
 $fixturePackage=New-SimplePackageFixture
 function Get-NeuroticAcceptedRuntimeComponents{return @()}
 $target|Add-Member -NotePropertyName bitness -NotePropertyValue 64
 $script:CancelledPlanTarget=$target
 function Get-HubTarget([string]$executable){return $script:CancelledPlanTarget}
 function Get-HubInspection($target){throw 'Cancelled plan reached transaction preview'}
 $cancelledPlan=New-HubPlan ([pscustomobject]@{gameExecutable=$exe;operation='Install';proxyName='winmm.dll'}) $fixturePackage
 if($cancelledPlan.status -cne 'Cancelled' -or $cancelledPlan.Contains('planId')){throw 'Cancelled scan produced a reviewable installation plan'}
 Write-Output 'PASS Cancelled detector result stops Plan before inspection preview or writes'
}finally{
 [NeuRoticAntiCheatCollector]::TestBoundary=$null
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/anticheat-cancel-fixtures'))){throw 'Cleanup target mismatch'}
 foreach($entry in Get-ChildItem -LiteralPath $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
