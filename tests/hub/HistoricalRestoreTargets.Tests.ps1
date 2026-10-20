. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub-recovery/historical-target-fixture'
$script:Support=Join-Path $script:Work 'app/support'
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-recovery/historical-target-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
Init-HubFixtures
try {
 # Small inert flagship package. No production binary or game is executed.
 $package=Join-Path $script:Work 'app/packages/current';$packageSupport=Join-Path $package 'support'
 foreach($dir in @($script:Support,$packageSupport,(Join-Path $package 'payload'))){New-Item -ItemType Directory -Path $dir -Force|Out-Null}
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)}
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1','NeuRotic-HubHistory.ps1')){Copy-Item -LiteralPath (Join-Path $script:Support $name) -Destination (Join-Path $packageSupport $name)}
 foreach($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')){[IO.File]::WriteAllText((Join-Path $package ('payload/'+$name)),('inert '+$name))}
 [IO.File]::WriteAllText((Join-Path $package 'payload/OptiScaler.ini'),"[Menu]`r`nOverlayMenu=true`r`n")
 foreach($name in @('NeuRotic-Manual-Setup.cmd','NeuRotic-Manual-Uninstall.cmd')){[IO.File]::WriteAllText((Join-Path $package $name),'@echo off')}
 $commit='a'*40
 [IO.File]::WriteAllText((Join-Path $packageSupport 'BUILD-MANIFEST.json'),(@{commit=$commit;status='built';build=@{exit_code=0};runtime_prerequisites=@{visual_cpp_x64_minimum='14.44.35207.0'}}|ConvertTo-Json -Depth 8))
 $files=@(Get-ChildItem -LiteralPath $package -Recurse -File|ForEach-Object{[pscustomobject]@{path=$_.FullName.Substring($package.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 $manifest=Join-Path $packageSupport 'PACKAGE-MANIFEST.json'
 [IO.File]::WriteAllText($manifest,(@{kind='neurotic-customer-candidate';name='Inert compatibility fixture';commit=$commit;files=$files}|ConvertTo-Json -Depth 8))
 [IO.File]::WriteAllText((Join-Path $script:Support 'Hub-Packages.json'),(@{packages=@(@{id='flagship-approved';relativeRoot='../packages/current';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}|ConvertTo-Json -Depth 8))
 $game=New-GameFixture 'game';$exe=Join-Path $game 'FixtureGame.exe'
 $result=Execute-Plan (Plan-Game $game 'Install');Assert ($result.status -eq 'Succeeded') 'Inert flagship installed'
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 # Use the donor's real b7bea38d record format and exact extra target names,
 # with generated text standing in for both installed and original DLL bytes.
 $backup=Join-Path $game 'NeuRotic-backups/install-donorfixture';New-Item -ItemType Directory -Path (Join-Path $backup 'previous') -Force|Out-Null
 $donorFiles=@(foreach($name in @('NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll')){
  $previous=Join-Path $backup ('previous/'+$name);[IO.File]::WriteAllText($previous,('original '+$name))
  $live=Join-Path $game $name;[IO.File]::WriteAllText($live,('donor '+$name))
  [pscustomobject]@{path=$name;source=('payload\'+$name);operation='copy-package';existed=$true;previous_hash=(Get-FileHash $previous).Hash;installed_hash=(Get-FileHash $live).Hash}
 })
 $recordPath=Join-Path $backup 'INSTALL-MANIFEST.json'
 $baseFiles=@(foreach($base in (Get-Content -Raw $state.restore_chain[-1]|ConvertFrom-Json).files){$live=Join-Path $game $base.path;Copy-Item -LiteralPath $live -Destination (Join-Path $backup ('previous/'+$base.path));$hash=(Get-FileHash $live).Hash;[pscustomobject]@{path=$base.path;source=$base.source;operation='copy-package';existed=$true;previous_hash=$hash;installed_hash=$hash}})
 $record=[ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';commit='b7bea38d617cf9c6fa9c916bbad455962015bc65';game_executable=$exe;backup=$backup;selected_proxy='dxgi.dll';files=@($donorFiles)+$baseFiles;previous_record=$state.restore_chain[-1];owned_files=@();restored_utc=$null;reconciled_paths=@()}
 [IO.File]::WriteAllText($recordPath,($record|ConvertTo-Json -Depth 8));$recordBytes=[IO.File]::ReadAllBytes($recordPath)
 $state.restore_chain=@($state.restore_chain)+$recordPath;[IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8))
 $inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=$exe})
 Assert ($inspection.status -eq 'Inspected') 'Known donor targets inspect successfully without changing records'
 Assert ((Get-FileHash $recordPath).Hash -eq (Get-FileHash -InputStream ([IO.MemoryStream]::new($recordBytes))).Hash) 'Inspection preserves original donor manifest'
 $result=Execute-Plan (Plan-Game $game 'Repair');Assert ($result.status -eq 'Succeeded') 'Flagship repair preserves known historical ownership'
 foreach($file in $donorFiles){Assert ((Get-FileHash (Join-Path $game $file.path)).Hash -eq $file.installed_hash) ('Repair preserves donor file '+$file.path)}
 # Load definitions only: no installer entry point or additional mutation.
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1'),[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll');$HubNonInteractive=$true
 foreach($file in $donorFiles){Assert (-not (AllowedTarget $file.path)) ('Historical target is not admitted as flagship package payload: '+$file.path)}
 $chain=@((Get-Content -Raw $statePath|ConvertFrom-Json).restore_chain);[array]::Reverse($chain)
 $journal=New-UninstallTransaction $game $chain
 [IO.File]::WriteAllText((Join-Path $game $donorFiles[0].path),'partial restore')
 Restore-UninstallTransaction $game $journal;Remove-UninstallSnapshots $game $journal
 Assert ((Get-FileHash (Join-Path $game $donorFiles[0].path)).Hash -eq $donorFiles[0].installed_hash) 'Interrupted uninstall recovers historical target from verified snapshot'
 foreach($case in @('unknown','duplicate','conflicting','unsafe')){
  $bad=$record|ConvertTo-Json -Depth 8|ConvertFrom-Json
  if($case -eq 'unknown'){$bad.files[0].path='unknown-addon.dll'}
  elseif($case -eq 'unsafe'){$bad.files[0].path='..\WinPixEventRuntime.dll'}
  else{$duplicate=$bad.files[0]|ConvertTo-Json|ConvertFrom-Json;if($case -eq 'conflicting'){$duplicate.installed_hash='f'*64};$bad.files=@($bad.files)+$duplicate}
  [IO.File]::WriteAllText($recordPath,($bad|ConvertTo-Json -Depth 8))
  $badHash=(Get-FileHash $recordPath).Hash;$liveHash=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash
  $inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=$exe})
  Assert ($inspection.status -eq 'FailedWithoutMutation' -and $inspection.reason.Contains($recordPath)) ($case+' inspection identifies offending record')
  Assert ($inspection.reason -like $(if($case -in @('duplicate','conflicting')){'*Duplicate restore target*'}else{'*Unrecognized restore target*'})) ($case+' has specific refusal')
  $plan=Plan-Game $game 'Repair';Assert ($plan.status -eq 'FailedWithoutMutation') ($case+' cannot bypass validation through repair')
  Assert ((Get-FileHash $recordPath).Hash -eq $badHash -and (Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $liveHash) ($case+' refusal preserves metadata and live files')
  [IO.File]::WriteAllBytes($recordPath,$recordBytes)
 }
 $previous=Join-Path $backup ('previous/'+$donorFiles[0].path);$saved=[IO.File]::ReadAllBytes($previous)
 $plan=Plan-Game $game 'Uninstall';[IO.File]::AppendAllText($previous,'tampered')
 $result=Execute-Plan $plan;Assert ($result.status -eq 'FailedWithoutMutation' -and $result.reason -like '*Backup verification failed*') 'Historical compatibility retains consumed backup hash validation'
 [IO.File]::WriteAllBytes($previous,$saved)
 foreach($i in 1..2){$result=Execute-Plan (Plan-Game $game 'Update');Assert ($result.status -eq 'Succeeded') ('Real installer update before compaction '+$i)}
 $plan=HubCall ([ordered]@{kind='Plan';gameExecutable=$exe;operation='CompactHistory';keepRestoreRecords=2});$result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and $result.history.beforeCount -eq 4 -and $result.history.afterCount -eq 3) 'Actual install repair and update chain compacts across donor history'
 $plan=HubCall ([ordered]@{kind='Plan';gameExecutable=$exe;operation='Uninstall';uninstallMode='Full'})
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded') 'Full uninstall completes across historical donor and flagship records'
 foreach($file in $donorFiles){Assert ((Get-FileHash (Join-Path $game $file.path)).Hash -eq $file.previous_hash) ('Uninstall restores original bytes: '+$file.path)}
 Assert ((Test-Path (Join-Path $game 'private.txt')) -and (Test-Path (Join-Path $game 'nvngx_dlssnr.dll'))) 'Private and unrelated files remain preserved'
}finally{End-HubFixtures}
