. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub-refinement/installer-lifecycle-fixture'
$script:Support=Join-Path $script:Work 'app/support'
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-refinement/installer-lifecycle-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
Init-HubFixtures
try {
 # Inert package built from a few generated text payloads and current source
 # scripts; never copy a build tree or load any game/runtime DLL.
 $package=Join-Path $script:Work 'app/packages/current';$packageSupport=Join-Path $package 'support'
 foreach($dir in @($script:Support,$packageSupport,(Join-Path $package 'payload'))){New-Item -ItemType Directory -Path $dir -Force|Out-Null}
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){
  Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)
 }
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1')){Copy-Item -LiteralPath (Join-Path $script:Support $name) -Destination (Join-Path $packageSupport $name)}
 foreach($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')){[IO.File]::WriteAllText((Join-Path $package ('payload/'+$name)),('inert '+$name))}
 [IO.File]::WriteAllText((Join-Path $package 'payload/OptiScaler.ini'),"[Menu]`r`nOverlayMenu=true`r`n")
 foreach($name in @('NeuRotic-Manual-Setup.cmd','NeuRotic-Manual-Uninstall.cmd')){[IO.File]::WriteAllText((Join-Path $package $name),'@echo off')}
 $commit='a'*40
 [IO.File]::WriteAllText((Join-Path $packageSupport 'BUILD-MANIFEST.json'),(@{commit=$commit;status='built';build=@{exit_code=0};runtime_prerequisites=@{visual_cpp_x64_minimum='14.44.35207.0'}}|ConvertTo-Json -Depth 8))
 $files=@(Get-ChildItem -LiteralPath $package -Recurse -File|ForEach-Object{[pscustomobject]@{path=$_.FullName.Substring($package.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 $manifest=Join-Path $packageSupport 'PACKAGE-MANIFEST.json'
 [IO.File]::WriteAllText($manifest,(@{kind='neurotic-customer-candidate';name='Inert history fixture';commit=$commit;files=$files}|ConvertTo-Json -Depth 8))
 [IO.File]::WriteAllText((Join-Path $script:Support 'Hub-Packages.json'),(@{packages=@(@{id='flagship-approved';relativeRoot='../packages/current';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}|ConvertTo-Json -Depth 8))
 $game=New-GameFixture 'game';$ini=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($ini,"[Menu]`nOverlayMenu=false`n")
 $originalIni=(Get-FileHash -LiteralPath $ini).Hash
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'original foreign proxy')
 $plan=HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Install';packageId='flagship-approved';proxyName='dxgi.dll';existingProxyAction='Replace';continueWithoutRuntime=$true})
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded') 'History fixture installs inert package'
 foreach($i in 1..5){$result=Execute-Plan (Plan-Game $game 'Update');Assert ($result.status -eq 'Succeeded') ('Update '+$i+' verified')}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 # The App owns the engine/helper even when this inert package only supplies
 # Engine and Prerequisites; package-root override does not change PSScriptRoot.
 Assert ($state.install_history.Count -eq 3 -and $state.restore_chain.Count -eq 4) 'App retains three recent records and the original baseline checkpoint'
 $old=Join-Path $game 'NeuRotic-backups/unused-retired';New-Item -ItemType Directory -Path $old|Out-Null
 $oldFile=Join-Path $old 'unreadable.bin';[IO.File]::WriteAllText($oldFile,'retired unrelated bytes')
 $handle=[IO.File]::Open($oldFile,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{$plan=Plan-Game $game 'Repair';Assert ($plan.status -eq 'Planned') 'Repair planning ignores unreadable unrelated retired payload';$result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded') 'Repair apply also ignores unrelated retired payload'}finally{$handle.Dispose()}
 $state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 $retained=Get-Content -Raw -LiteralPath $state.restore_chain[2]|ConvertFrom-Json
 $unused=Join-Path $retained.backup 'previous/OptiScaler.ini'
 $handle=[IO.File]::Open($unused,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{$result=Execute-Plan (Plan-Game $game 'Update');Assert ($result.status -eq 'Succeeded') 'Update and retention do not consume unused retained intermediate payloads'}finally{$handle.Dispose()}
 $state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 $record=Get-Content -Raw -LiteralPath $state.restore_chain[0]|ConvertFrom-Json
 $backup=Join-Path $record.backup 'previous/OptiScaler.ini';$saved=[IO.File]::ReadAllBytes($backup)
 $plan=Plan-Game $game 'Uninstall';Assert ($plan.status -eq 'Planned') 'Uninstall preview produced before restore corruption'
 [IO.File]::AppendAllText($backup,'corrupt after preview')
 $stateHash=(Get-FileHash -LiteralPath $statePath).Hash;$loaderHash=(Get-FileHash -LiteralPath (Join-Path $game 'dxgi.dll')).Hash
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'FailedWithoutMutation' -and $result.reason -like '*Backup verification failed*') 'Uninstall refuses oldest required backup corruption after preview'
 Assert ((Get-FileHash -LiteralPath $statePath).Hash -eq $stateHash -and (Get-FileHash -LiteralPath (Join-Path $game 'dxgi.dll')).Hash -eq $loaderHash) 'Refusal preserves state and live installation'
 [IO.File]::WriteAllBytes($backup,$saved)
 $originalProxyBackup=Join-Path $record.backup 'previous/dxgi.dll';$savedProxy=[IO.File]::ReadAllBytes($originalProxyBackup)
 $plan=Plan-Game $game 'ChangeProxy' 'winmm.dll';Assert ($plan.status -eq 'Planned') 'Proxy change preview verifies its selected original baseline'
 [IO.File]::AppendAllText($originalProxyBackup,'corrupt after preview');$result=Execute-Plan $plan
 Assert ($result.status -eq 'FailedWithoutMutation' -and $result.reason -like '*original proxy baseline backup failed verification*') 'Proxy change refuses corrupt consumed baseline after preview'
 [IO.File]::WriteAllBytes($originalProxyBackup,$savedProxy)
 $result=Execute-Plan (Plan-Game $game 'ChangeProxy' 'winmm.dll');Assert ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'original foreign proxy') 'Proxy change preserves restoration after repeated automatic retention'
 $result=Execute-Plan (Plan-Game $game 'Uninstall' 'winmm.dll');Assert ($result.status -eq 'Succeeded') 'Uninstall restores original baseline after repeated automatic retention'
 Assert ((Get-FileHash -LiteralPath $ini).Hash -eq $originalIni) 'Uninstall restores original settings'
 Assert ((Test-Path (Join-Path $game 'private.txt')) -and (Test-Path (Join-Path $game 'nvngx_dlssnr.dll')) -and (Test-Path $oldFile)) 'Unknown files private model and retired backup remain preserved'
 # Full cleanup removes manifests before its final manager receipt. Emulate a
 # stop in that exact interval; Restore must use the saved cleanup authority.
 $state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 $removedManifest=[string]$state.restore_chain[0]
 $cleanup=Join-Path $game 'NeuRotic/Installer/Cleanup-Pending.json'
 $cleanupFiles=@([pscustomobject]@{path=$removedManifest.Substring($game.Length+1);sha256=(Get-FileHash -LiteralPath $removedManifest).Hash},[pscustomobject]@{path='NeuRotic\Installer\Current-Install.json';sha256=(Get-FileHash -LiteralPath $statePath).Hash})
 [IO.File]::WriteAllText($cleanup,(@{kind='neurotic-uninstall-cleanup';game_directory=$game;files=$cleanupFiles}|ConvertTo-Json -Depth 8))
 Remove-Item -LiteralPath $removedManifest -Force
 $plan=Plan-Game $game 'Restore';Assert ($plan.status -eq 'Planned') 'Recovery can plan after cleanup removed an active manifest before the state receipt'
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded' -and -not (Test-Path -LiteralPath $cleanup) -and -not (Test-Path -LiteralPath $statePath)) 'Recovery completes interrupted Full cleanup from its verified receipt'
 Assert ((Test-Path (Join-Path $game 'private.txt')) -and (Test-Path $oldFile)) 'Recovery preserves unknown files and unrelated retired data'
}finally{End-HubFixtures}
