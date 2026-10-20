. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
function Request($game,[string]$operation='Install',[string]$name='FixtureGame.exe'){$r=[ordered]@{kind='Plan';gameExecutable=(Join-Path $game $name);operation=$operation};if($operation -eq 'Install'){$r.packageId='flagship-approved';$r.proxyName='winmm.dll';$r.continueWithoutRuntime=$true};return $r}
try{
 $game=New-GameFixture 'anticheat-core';$before=@(Get-ChildItem -LiteralPath $game -Recurse -File|ForEach-Object {$_.FullName+':'+(Get-FileHash $_.FullName).Hash})
 $request=Request $game;$plan=HubCall $request
 Assert ($plan.status -eq 'Planned' -and $plan.antiCheat.scan_status -eq 'incomplete' -and $plan.antiCheat.requires_ack -and $plan.antiCheat.issues -contains 'trusted_install_root_unresolved') 'T01 manual root inspection is explicitly incomplete and reviewable'
 Assert (-not (Test-Path (Join-Path $game 'NeuRotic-backups')) -and -not (Test-Path (Join-Path $game 'winmm.dll'))) 'T01 Back before Execute creates no game files backups or receipts'
 $after=@(Get-ChildItem -LiteralPath $game -Recurse -File|ForEach-Object {$_.FullName+':'+(Get-FileHash $_.FullName).Hash});Assert (-not (Compare-Object $before $after)) 'T01 Back leaves every fixture file unchanged'
 $unapproved=HubCall ([ordered]@{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint})
 Assert ($unapproved.decisionKind -eq 'AntiCheatRisk' -and -not (Test-Path (Join-Path $game 'winmm.dll'))) 'T14 JSON plan digest alone cannot mint local approval'
 foreach($value in @($true,$false,'true',1,$null)){$invalid=Request $game;$invalid.antiCheatAcknowledged=$value;Assert ((HubCall $invalid).status -eq 'FailedWithoutChanges') 'T06 imported acknowledgement JSON rejected regardless of value'}
 Assert ((Execute-Plan $plan).status -eq 'Succeeded') 'T02 private current-plan approval invokes existing writer successfully'
 Assert ((Execute-Plan $plan).status -eq 'FailedWithoutChanges') 'T02 completed plan cannot be consumed a second time'
 $update=HubCall (Request $game 'Install');Assert ($update.status -eq 'Planned' -and $update.antiCheat.acknowledgementRequired) 'Repeat Install never remembers previous consent'
 $remove=HubCall (Request $game 'Uninstall');Assert ($remove.status -eq 'Planned' -and -not $remove.antiCheat) 'T10 uninstall does not require anti-cheat approval';Assert ((Execute-Plan $remove).status -eq 'Succeeded') 'T10 ownership-safe uninstall remains accessible'
 $stale=New-GameFixture 'anticheat-stale';$stalePlan=HubCall (Request $stale);Copy-Item (Join-Path $stale 'FixtureGame.exe') (Join-Path $stale 'EasyAntiCheat_EOS_Setup.exe')
 $changed=Execute-Plan $stalePlan;Assert ($changed.decisionKind -eq 'AntiCheatRisk' -and $changed.antiCheat.game_state -eq 'possible' -and -not (Test-Path (Join-Path $stale 'winmm.dll'))) 'T04 provider appeared after preview invalidates approval before writes'
 $names=New-GameFixture 'anticheat-names';Move-Item (Join-Path $names 'FixtureGame.exe') (Join-Path $names 'r5apex.exe');$namePlan=HubCall (Request $names 'Install' 'r5apex.exe');Assert ($namePlan.antiCheat.game_state -eq 'not_found' -and @($namePlan.antiCheat.games).Count -eq 0) 'T07 basename-only game names cannot create authoritative catalog association'
 $library=Join-Path $script:Work 'steam';$owned=Join-Path $library 'steamapps/common/OwnedFixture';New-Item -ItemType Directory (Join-Path $owned 'Binaries/Win64') -Force|Out-Null;Copy-Item (Join-Path $env:SystemRoot 'System32/cmd.exe') (Join-Path $owned 'Binaries/Win64/Renamed.exe')
 $manifest=Join-Path $library 'steamapps/appmanifest_1172470.acf';[IO.File]::WriteAllText($manifest,'"AppState" { "appid" "1172470" "installdir" "OwnedFixture" }')
 $steam=Request (Join-Path $owned 'Binaries/Win64') 'Install' 'Renamed.exe';$steam.gameRoot=$owned;$steam.steamRoot=$library;$steam.steamAppId='1172470';$steamPlan=HubCall $steam
 Assert ($steamPlan.status -eq 'Planned' -and $steamPlan.antiCheat.game_state -eq 'documented' -and $steamPlan.antiCheat.scan_status -eq 'complete' -and $steamPlan.antiCheat.games.id -contains 'apex-legends') 'T07 exact owned storefront identity retains curated association after local miss'
 New-Item -ItemType Directory (Join-Path $owned 'BattlEye')|Out-Null;Copy-Item (Join-Path $owned 'Binaries/Win64/Renamed.exe') (Join-Path $owned 'BattlEye/BEService_x64.exe');[IO.File]::WriteAllText((Join-Path $owned 'BattlEye/Install_BattlEye.bat'),'NEVER EXECUTE')
 $steamLocal=HubCall $steam;Assert ($steamLocal.antiCheat.game_state -eq 'detected' -and $steamLocal.antiCheat.findings.provider -contains 'battleye') 'N01 install-root provider recognized above nested selected EXE'
 [IO.File]::WriteAllText($manifest,'"AppState" { "appid" "999" "installdir" "OwnedFixture" }');$staleManifest=Execute-Plan $steamLocal;Assert ($staleManifest.decisionKind -eq 'AntiCheatRisk' -and -not (Test-Path (Join-Path $owned 'Binaries/Win64/winmm.dll'))) 'T03 changed owning manifest invalidates current plan approval'
 $source=Join-Path $script:Repo 'tools/packaging/customer/support';. (Join-Path $source 'NeuRotic-HubAntiCheat.ps1')
 function HashBytes($bytes){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
 $fake=Join-Path $names 'NeuRotic-AntiCheatProviders.json';[IO.File]::WriteAllText($fake,'{}');Assert ((Read-HubProviderRules).ruleset_version -eq '2026-10-03.1') 'T16 game/current-directory provider rules cannot replace bundled authority'
}finally{End-HubFixtures}
