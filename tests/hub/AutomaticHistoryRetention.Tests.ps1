. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub-streamline/automatic-history-fixture'
$script:Support=Join-Path $script:Work 'app/support'
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-streamline/automatic-history-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force;Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
Init-HubFixtures
try{
 $package=Join-Path $script:Work 'app/packages/current';$packageSupport=Join-Path $package 'support'
 foreach($dir in @($script:Support,$packageSupport,(Join-Path $package 'payload'))){New-Item -ItemType Directory -Path $dir -Force|Out-Null}
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){foreach($dest in @($script:Support,$packageSupport)){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $dest $source.Name)}}
 foreach($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')){[IO.File]::WriteAllText((Join-Path $package ('payload/'+$name)),('inert '+$name))}
 [IO.File]::WriteAllText((Join-Path $package 'payload/OptiScaler.ini'),"[Menu]`nOverlayMenu=true`n")
 New-Item -ItemType Directory -Path (Join-Path $package 'payload/OptiScaler/CharacterInspector') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $package 'payload/OptiScaler/CharacterInspector/fixture.bin'),'inert owned nested payload')
 foreach($name in @('NeuRotic-Manual-Setup.cmd','NeuRotic-Manual-Uninstall.cmd')){[IO.File]::WriteAllText((Join-Path $package $name),'@echo off')}
 $commit='a'*40
 [IO.File]::WriteAllText((Join-Path $packageSupport 'BUILD-MANIFEST.json'),(@{commit=$commit;status='built';build=@{exit_code=0};runtime_prerequisites=@{visual_cpp_x64_minimum='14.44.35207.0'}}|ConvertTo-Json -Depth 8))
 $files=@(Get-ChildItem -LiteralPath $package -Recurse -File|Where-Object{$_.Name -ne 'PACKAGE-MANIFEST.json'}|ForEach-Object{[pscustomobject]@{path=$_.FullName.Substring($package.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 $manifest=Join-Path $packageSupport 'PACKAGE-MANIFEST.json';[IO.File]::WriteAllText($manifest,(@{kind='neurotic-customer-candidate';name='Automatic retention fixture';commit=$commit;files=$files}|ConvertTo-Json -Depth 8))
 [IO.File]::WriteAllText((Join-Path $script:Support 'Hub-Packages.json'),(@{packages=@(@{id='flagship-approved';relativeRoot='../packages/current';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}|ConvertTo-Json -Depth 8))
 $game=New-GameFixture 'game';$exe=Join-Path $game 'FixtureGame.exe';[IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'foreign original proxy');[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[User]`nKeep=original`n");$originalIni=(Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=$exe;operation='Install';packageId='flagship-approved';proxyName='dxgi.dll';existingProxyAction='Replace';continueWithoutRuntime=$true}));Assert ($result.status -eq 'Succeeded') 'Initial inert install succeeds'
 foreach($i in 1..5){$result=Execute-Plan (Plan-Game $game 'Update');Assert ($result.status -eq 'Succeeded') ('Update '+$i+' succeeds with automatic retention')}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json
 Assert ($state.restore_chain.Count -eq 4 -and $state.install_history.Count -eq 3) 'Automatic retention keeps three newest transactions plus original checkpoint'
 Assert ($result.inspection.history.retention.status -eq 'Complete' -and $result.inspection.history.retention.keepRecent -eq 3) 'Inspection reports applied automatic retention truthfully'
 $checkpoint=Get-Content -Raw $state.restore_chain[0]|ConvertFrom-Json;Assert ($checkpoint.history_checkpoint) 'Original baseline is a distinct safeguarded checkpoint'
 Assert ([IO.File]::ReadAllText((Join-Path $checkpoint.backup 'previous/dxgi.dll')) -eq 'foreign original proxy') 'Repeated compaction retains first foreign proxy bytes'
 & $script:PowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $packageSupport 'NeuRotic-Setup-Engine.ps1') -GameExecutable $exe -ProxyName dxgi.dll -ExistingInstallAction Update -ConfirmInstall -ContinueWithoutRuntime *> (Join-Path $script:Work 'direct-update.log')
 Assert ($LASTEXITCODE -eq 0 -and (Get-Content -Raw $statePath|ConvertFrom-Json).restore_chain.Count -eq 4) 'Direct installer also enforces automatic retention'
 $plan=Plan-Game $game 'Update';$env:NEUROTIC_HISTORY_TEST_STOP='before-stage'
 try{$result=Execute-Plan $plan}finally{Remove-Item Env:NEUROTIC_HISTORY_TEST_STOP}
 $inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=$exe})
 Assert ($result.status -eq 'RecoveryRequired' -and $inspection.inspection.history.retention.status -eq 'RecoveryRequired') 'Interrupted automatic retention reports committed install needing history recovery'
 $result=Execute-Plan (Plan-Game $game 'Restore')
 Assert ($result.status -eq 'Succeeded' -and (Get-Content -Raw $statePath|ConvertFrom-Json).restore_chain.Count -eq 5) 'History recovery keeps the successful install and rolls back only maintenance'
 $result=Execute-Plan (Plan-Game $game 'Update')
 Assert ($result.status -eq 'Succeeded' -and (Get-Content -Raw $statePath|ConvertFrom-Json).restore_chain.Count -eq 4) 'Next update retries deferred maintenance successfully'
 # Preserve a completed older same-game archive outside the active chain. Full
 # cleanup should remove its verified owned entries but keep unlisted content.
 $state=Get-Content -Raw $statePath|ConvertFrom-Json;$old=Get-Content -Raw $state.restore_chain[1]|ConvertFrom-Json;$archive=Join-Path $game 'NeuRotic-backups/old-adopted';New-Item -ItemType Directory -Path (Join-Path $archive 'previous') -Force|Out-Null
 foreach($f in $old.files){if($f.existed){$destination=Join-Path $archive ('previous/'+$f.path);New-Item -ItemType Directory -Path (Split-Path $destination) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $old.backup ('previous/'+$f.path)) -Destination $destination}}
 $old.backup=$archive;$old.previous_record=$null;$old.owned_files=@();[IO.File]::WriteAllText((Join-Path $archive 'INSTALL-MANIFEST.json'),($old|ConvertTo-Json -Depth 16));[IO.File]::WriteAllText((Join-Path $archive 'user-note.txt'),'preserve')
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=$exe;operation='Uninstall';uninstallMode='Full'}));Assert ($result.status -eq 'Succeeded') 'Full uninstall succeeds after automatic retention'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'foreign original proxy' -and (Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $originalIni) 'Single-pass uninstall restores original baseline after repeated automatic retention'
 Assert (-not(Test-Path (Join-Path $archive 'INSTALL-MANIFEST.json')) -and (Test-Path (Join-Path $archive 'user-note.txt'))) 'Full cleanup includes verified older same-game archives while preserving unknown files'
 Assert (-not(Test-Path (Join-Path $game 'OptiScaler/CharacterInspector'))) 'Repeated checkpoints preserve ownership of now-empty installed folders'
}finally{End-HubFixtures}
