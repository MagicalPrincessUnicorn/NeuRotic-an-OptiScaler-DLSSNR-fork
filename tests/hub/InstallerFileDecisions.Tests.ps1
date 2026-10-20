$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
if($errors.Count){throw $errors[0]}
foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output ('PASS '+$message)}
$allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
$hash='a'*64;$newHash='b'*64
$files=@([pscustomobject]@{path='dxgi.dll';existed=$true;previous_hash=$hash;installed_hash=$newHash;operation='copy-package'},[pscustomobject]@{path='OptiScaler.ini';existed=$true;previous_hash=$hash;installed_hash=$hash;operation='preserve-existing'})
$result=Resolve-InstallFileDecisions $files @() '' -NonInteractive
Assert ($result.conflicts.Count -eq 1 -and $result.conflicts[0].path -eq 'dxgi.dll') 'Foreign files require per-file decision; settings do not'
$result=Resolve-InstallFileDecisions $files @([pscustomobject]@{path='dxgi.dll';sha256=$hash;changed=$false}) '' -NonInteractive
Assert ($result.conflicts.Count -eq 0) 'Recorded current ownership allows ordinary update'
$result=Resolve-InstallFileDecisions $files @([pscustomobject]@{path='dxgi.dll';sha256=$hash;changed=$true}) '' -NonInteractive
Assert ($result.conflicts.Count -eq 1) 'Changed owned file requires decision'
foreach($action in @('SaveAndReplace','Replace','Skip','Cancel')){
 $json=ConvertTo-Json -InputObject @([pscustomobject]@{path='dxgi.dll';sha256=$hash;action=$action}) -Compress
 $result=Resolve-InstallFileDecisions $files @() $json -NonInteractive
 Assert ($result.conflicts.Count -eq 0 -and $result.decisions['dxgi.dll'] -eq $action) ('Accept hash-bound '+$action)
}
$json=ConvertTo-Json -InputObject @([pscustomobject]@{path='dxgi.dll';sha256=$newHash;action='Replace'}) -Compress
$refused=$false;try{Resolve-InstallFileDecisions $files @() $json -NonInteractive}catch{$refused=$true}
Assert $refused 'Stale file decision refuses'
$result=Resolve-InstallFileDecisions $files @() $json -NonInteractive -Review
Assert ($result.conflicts.Count -eq 1 -and $result.conflicts[0].sha256 -eq $hash) 'Preview asks again for changed file while execution refuses stale consent'
$identical=@([pscustomobject]@{path='dxgi.dll';existed=$true;previous_hash=$hash;installed_hash=$hash;operation='copy-package'})
$result=Resolve-InstallFileDecisions $identical @() '' -NonInteractive
Assert ($result.conflicts.Count -eq 0) 'Exact package bytes need no provenance decision'
# End-to-end engine fixtures use a tiny local payload and no persistent user data.
$work=Join-Path $repo 'builds/hub-streamline/file-decisions-fixture'
if(Test-Path -LiteralPath $work){throw 'Fixture root already exists; inspect before reuse'}
New-Item -ItemType Directory -Path $work|Out-Null
$env:NEUROTIC_HUB_FIXTURE_ROOT=$work
try{
 $package=Join-Path $work 'package';$support=Join-Path $package 'support';$payload=Join-Path $package 'payload'
 New-Item -ItemType Directory -Path $support,$payload|Out-Null
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1')){Copy-Item (Join-Path (Split-Path $engine) $name) (Join-Path $support $name)}
 Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') (Join-Path $payload 'OptiScaler.dll')
 [IO.File]::WriteAllText((Join-Path $payload 'OptiScaler.ini'),"[User]`nSetting=true`n")
 [IO.File]::WriteAllText((Join-Path $payload 'nvngx.dll_dlssnr.dll'),'fixture shim')
 [IO.File]::WriteAllText((Join-Path $payload 'WinPixEventRuntime.dll'),'fixture runtime')
 $build=@{commit=('a'*40);status='built';build=@{exit_code=0}}
 $build|ConvertTo-Json -Depth 8|Set-Content (Join-Path $support 'BUILD-MANIFEST.json') -Encoding UTF8
 $manifest=@{kind='neurotic-customer-candidate';name='fixture';commit=('a'*40);files=@(Get-ChildItem $package -Recurse -File|ForEach-Object{@{path=$_.FullName.Substring($package.Length+1);sha256=(Get-FileHash $_.FullName).Hash}})}
 $manifest|ConvertTo-Json -Depth 8|Set-Content (Join-Path $support 'PACKAGE-MANIFEST.json') -Encoding UTF8
 $game=Join-Path $work 'game';New-Item -ItemType Directory -Path $game|Out-Null
 Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') (Join-Path $game 'FixtureGame.exe')
 $foreign=Join-Path $game 'WinPixEventRuntime.dll';[IO.File]::WriteAllText($foreign,'user runtime')
 $preview=Join-Path $work 'preview.json';$exe=Join-Path $game 'FixtureGame.exe';$localEngine=Join-Path $support 'NeuRotic-Setup-Engine.ps1'
 function EngineCall($extra){$script:engineError=$null;try{& $localEngine -GameExecutable $exe -ProxyName dxgi.dll -HubNonInteractive -ContinueWithoutRuntime @extra *> (Join-Path $work 'engine.log')}catch{$script:engineError=$_}}
 EngineCall @{CheckOnly=$true;HubPlanPreviewPath=$preview}
 if($script:engineError){throw $script:engineError}
 $plan=Get-Content -Raw $preview|ConvertFrom-Json
 Assert ($plan.fileConflicts.Count -eq 1 -and $plan.fileConflicts[0].path -eq 'WinPixEventRuntime.dll') 'Engine preview reports foreign payload without mutation'
 EngineCall @{ConfirmInstall=$true}
 Assert ($script:engineError -and -not(Test-Path (Join-Path $game 'NeuRotic-backups'))) 'ConfirmInstall does not authorize foreign payload replacement'
 $decision=ConvertTo-Json -InputObject @(@{path='WinPixEventRuntime.dll';sha256=(Get-FileHash $foreign).Hash;action='Skip'}) -Compress
 EngineCall @{FileDecisionsJson=$decision;CheckOnly=$true;HubPlanPreviewPath=$preview}
 $plan=Get-Content -Raw $preview|ConvertFrom-Json
 Assert ($plan.skipped -and $plan.skippedFiles[0] -eq 'WinPixEventRuntime.dll') 'Preview explains consistent-set Skip'
 $cancelDecision=$decision.Replace('Skip','Cancel')
 EngineCall @{FileDecisionsJson=$cancelDecision}
 Assert (-not $script:engineError -and -not(Test-Path (Join-Path $game 'NeuRotic-backups'))) 'Explicit Cancel returns normally without game mutation'
 $decision=$decision.Replace('Skip','SaveAndReplace')
 EngineCall @{FileDecisionsJson=$decision}
 if($script:engineError){throw $script:engineError}
 Assert (@(Get-ChildItem (Join-Path $game 'NeuRotic-preserved-files') -Recurse -Filter FILES.json).Count -eq 1) 'Save and Replace retains separate original copy'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $originalRecord=$state.restore_chain[0]
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $latest=Get-Content -Raw $state.restore_chain[-1]|ConvertFrom-Json
 Assert (@($latest.files|Where-Object{$_.path -eq 'WinPixEventRuntime.dll'}).Count -eq 0) 'Identical owned runtime omits redundant backup and copy'
 Assert (-not(Test-Path (Join-Path $latest.backup 'previous/dxgi.dll')) -and -not(Test-Path (Join-Path $latest.backup 'previous/OptiScaler.ini'))) 'Unchanged owned proxy and configuration avoid redundant backups'
 Assert ($state.restore_chain[0] -eq $originalRecord) 'Original uninstall baseline remains in history'
 Remove-Item (Join-Path $game 'nvngx.dll_dlssnr.dll')
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 Assert (Test-Path (Join-Path $game 'nvngx.dll_dlssnr.dll')) 'Missing owned payload repairs without conflict'
 [IO.File]::WriteAllText($foreign,'edited runtime to replace')
 $replaceDecision=ConvertTo-Json -InputObject @(@{path='WinPixEventRuntime.dll';sha256=(Get-FileHash $foreign).Hash;action='Replace'}) -Compress
 EngineCall @{FileDecisionsJson=$replaceDecision}
 if($script:engineError){throw $script:engineError}
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $lastBackup=Split-Path $state.restore_chain[-1]
 Assert (-not(Test-Path (Join-Path $lastBackup 'previous/WinPixEventRuntime.dll'))) 'Replace retires temporary modified preimage after successful commit'
 Assert (@(Get-ChildItem (Join-Path $game 'NeuRotic-preserved-files') -Recurse -Filter FILES.json).Count -eq 1) 'Replace creates no permanent saved edit'
 Remove-Item -LiteralPath $originalRecord
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 Assert (-not(Test-Path $originalRecord)) 'Repair uses metadata ledger without rewriting deleted historical manifest'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 Assert ($state.restore_record_ledger.Count -eq $state.restore_chain.Count) 'Repair preserves bounded recovery metadata for original baseline'
 $state.PSObject.Properties.Remove('restore_record_ledger')
 $state|ConvertTo-Json -Depth 20|Set-Content (Join-Path $game 'NeuRotic/Installer/Current-Install.json') -Encoding UTF8
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 Assert (@($state.restore_record_ledger|Where-Object{$_.record_json -match 'recovery_metadata_missing'}).Count -gt 0) 'Legacy missing metadata repairs with explicit unknown baseline'
 [IO.File]::WriteAllText((Join-Path $game 'nvngx.dll_dlssnr.dll'),'foreign replacement')
 Remove-Item -LiteralPath (Join-Path (Split-Path $originalRecord) 'previous/WinPixEventRuntime.dll')
 & $localEngine -Uninstall -GameDirectory $game -UninstallMode Full -ConfirmUninstall -HubNonInteractive *> (Join-Path $work 'uninstall.log')
 Assert ([IO.File]::ReadAllText((Join-Path $game 'nvngx.dll_dlssnr.dll')) -eq 'foreign replacement') 'Uninstall preserves externally replaced payload in place'
 Assert (-not(Test-Path (Join-Path $game 'dxgi.dll')) -and -not(Test-Path (Join-Path $game 'WinPixEventRuntime.dll'))) 'Uninstall removes owned files despite unavailable original'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 Assert ($state.uninstall_limitations.Count -ge 2 -and (Test-Path $originalRecord)) 'Partial original restoration retains recovered manifests and per-file limitations'
 $game=Join-Path $work 'all-metadata-lost';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') $exe
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json
 foreach($path in $state.restore_chain){Remove-Item -LiteralPath $path}
 $state.PSObject.Properties.Remove('restore_record_ledger');$state|ConvertTo-Json -Depth 20|Set-Content $statePath -Encoding UTF8
 $foreign=Join-Path $game 'WinPixEventRuntime.dll';[IO.File]::WriteAllText($foreign,'unknown after metadata loss')
 $extra=Join-Path $game 'user-extra.dat';[IO.File]::WriteAllText($extra,'private extra')
 $unknownIni=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($unknownIni,'unrelated preexisting settings')
 EngineCall @{CheckOnly=$true;HubPlanPreviewPath=$preview}
 if($script:engineError){throw $script:engineError}
 $plan=Get-Content -Raw $preview|ConvertFrom-Json
 Assert ($plan.fileConflicts.Count -eq 1 -and $plan.fileConflicts[0].path -eq 'WinPixEventRuntime.dll') 'All lost metadata still produces per-file ownership decision'
 $decision=ConvertTo-Json -InputObject @(@{path='WinPixEventRuntime.dll';sha256=(Get-FileHash $foreign).Hash;action='SaveAndReplace'}) -Compress
 $completion=Join-Path $work 'completion.json'
 EngineCall @{FileDecisionsJson=$decision;HubOperationResultPath=$completion}
 if($script:engineError){throw $script:engineError}
 Assert ((Get-Content -Raw $completion|ConvertFrom-Json).status -eq 'Succeeded') 'Verified engine completion emits receipt'
 & $localEngine -Uninstall -GameDirectory $game -UninstallMode Full -ConfirmUninstall -HubNonInteractive -HubOperationResultPath $completion *> (Join-Path $work 'uninstall.log')
 $result=Get-Content -Raw $completion|ConvertFrom-Json
 Assert ($result.status -eq 'Succeeded' -and $result.limitations.Count -gt 0 -and (Test-Path $extra)) 'Recovered installation uninstalls with unknown originals reported and unknown extras untouched'
 Assert ([IO.File]::ReadAllText($unknownIni) -eq 'unrelated preexisting settings') 'Full uninstall preserves unproven INI after all metadata loss and repair'
 $game=Join-Path $work 'middle-metadata-lost';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') $exe
 foreach($iteration in 1..3){EngineCall @{};if($script:engineError){throw $script:engineError}}
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json
 Remove-Item -LiteralPath $state.restore_chain[1]
 $state.PSObject.Properties.Remove('restore_record_ledger');$state|ConvertTo-Json -Depth 20|Set-Content $statePath -Encoding UTF8
 $unrecorded=Join-Path $game 'unrecorded-extra.dat';[IO.File]::WriteAllText($unrecorded,'unknown retained extra')
 & $localEngine -Uninstall -GameDirectory $game -UninstallMode Full -ConfirmUninstall -HubNonInteractive -HubOperationResultPath $completion *> (Join-Path $work 'middle-uninstall.log')
 $result=Get-Content -Raw $completion|ConvertFrom-Json
 Assert (@($result.limitations|Where-Object{$_.path -eq 'installation history'}).Count -eq 1 -and (Test-Path $unrecorded)) 'Missing middle metadata reports unknown remaining files even with a surviving original baseline'
 $game=Join-Path $work 'late-edit';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') $exe
 EngineCall @{}
 if($script:engineError){throw $script:engineError}
 # Inject a modder edit at the real post-copy boundary in the disposable package.
 $source=[IO.File]::ReadAllText($localEngine)
 $boundary="    Show-SetupProgress 'Installing files' `$files.Count `$files.Count"
 $injection=$boundary+"`r`n    [IO.File]::WriteAllText((Join-Path `$gameDir 'WinPixEventRuntime.dll'),'outside edit after copy')"
 if(-not $source.Contains($boundary)){throw 'Install verification boundary moved'}
 [IO.File]::WriteAllText($localEngine,$source.Replace($boundary,$injection))
 $manifest.files|Where-Object{$_.path -eq 'support\NeuRotic-Setup-Engine.ps1'}|ForEach-Object{$_.sha256=(Get-FileHash $localEngine).Hash}
 $manifest|ConvertTo-Json -Depth 8|Set-Content (Join-Path $support 'PACKAGE-MANIFEST.json') -Encoding UTF8
 EngineCall @{}
 Assert ($script:engineError -and [IO.File]::ReadAllText((Join-Path $game 'WinPixEventRuntime.dll')) -eq 'outside edit after copy') 'Final verification detects late edit of omitted identical payload without overwriting it'
 [IO.File]::WriteAllText($localEngine,$source)
 $game=Join-Path $work 'private-model-edit';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') $exe
 $privateModel=Join-Path $game 'nvngx_dlssnr.dll';[IO.File]::WriteAllText($privateModel,'user model before setup')
 $modelInjection=$boundary+"`r`n    [IO.File]::WriteAllText((Join-Path `$gameDir 'nvngx_dlssnr.dll'),'user model changed during setup')"
 [IO.File]::WriteAllText($localEngine,$source.Replace($boundary,$modelInjection))
 $manifest.files|Where-Object{$_.path -eq 'support\NeuRotic-Setup-Engine.ps1'}|ForEach-Object{$_.sha256=(Get-FileHash $localEngine).Hash}
 $manifest|ConvertTo-Json -Depth 8|Set-Content (Join-Path $support 'PACKAGE-MANIFEST.json') -Encoding UTF8
 EngineCall @{HubOperationResultPath=$completion}
 Assert (-not $script:engineError -and [IO.File]::ReadAllText($privateModel) -eq 'user model changed during setup' -and (Test-Path (Join-Path $game 'dxgi.dll'))) 'Private model edit during install remains untouched without rolling back owned payload'
 [IO.File]::WriteAllText((Join-Path $support 'NeuRotic-RuntimeFiles.ps1'),"function Get-NeuroticRuntimeComponents { throw 'Installer must not verify private models' }")
 $modelHandle=[IO.File]::Open($privateModel,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{EngineCall @{CheckOnly=$true;HubPlanPreviewPath=$preview}}finally{$modelHandle.Dispose()}
 if($script:engineError){throw $script:engineError}
 $plan=Get-Content -Raw $preview|ConvertFrom-Json
 Assert ($plan.preserved[0].ownership -eq 'user' -and -not($plan.preserved[0].PSObject.Properties.Name -contains 'sha256')) 'Preview observes private-model presence without reading locked model or invoking runtime verifier'
 Remove-Item -LiteralPath (Join-Path $support 'NeuRotic-RuntimeFiles.ps1')
 [IO.File]::WriteAllText($localEngine,$source)
 $game=Join-Path $work 'prepared-metadata-lost';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item (Join-Path $env:WINDIR 'System32/cmd.exe') $exe
 $manager=Join-Path $game 'NeuRotic/Installer';New-Item -ItemType Directory -Path $manager -Force|Out-Null
 $preparedIni=Join-Path $game 'ReShade.ini';[IO.File]::WriteAllText($preparedIni,'unknown prepared settings')
 $state=@{kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory=$game;game_executable=$exe;selected_proxy='dxgi.dll';install_route='prepared-x86';restore_chain=@((Join-Path $game 'NeuRotic-backups/install-missing/INSTALL-MANIFEST.json'));last_install_utc='2026-10-05T00:00:00Z';uninstalled_utc=$null;uninstall_mode=$null}
 $state|ConvertTo-Json -Depth 10|Set-Content (Join-Path $manager 'Current-Install.json') -Encoding UTF8
 & $localEngine -Uninstall -GameDirectory $game -UninstallMode RemoveSettings -ConfirmUninstall -HubNonInteractive *> (Join-Path $work 'prepared-uninstall.log')
 Assert ([IO.File]::ReadAllText($preparedIni) -eq 'unknown prepared settings') 'Missing prepared metadata retains correct ReShade INI even with RemoveSettings'
}finally{
 if([IO.Path]::GetFullPath($work) -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub-streamline/file-decisions-fixture'))){throw 'Unexpected cleanup root'}
 foreach($entry in Get-ChildItem $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $work -Recurse -Force
 Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
# A missing original is a per-file limitation, not a reason to strand unrelated files.
$baselineRoot=Join-Path $repo 'builds/hub-streamline/unavailable-original-fixture'
New-Item -ItemType Directory -Path $baselineRoot -Force|Out-Null
try{
 $record=[pscustomobject]@{backup=(Join-Path $baselineRoot 'NeuRotic-backups/install-1');files=@([pscustomobject]@{path='dxgi.dll';existed=$true;previous_hash=('a'*64)},[pscustomobject]@{path='WinPixEventRuntime.dll';existed=$false;previous_hash=$null})}
 $baseline=@(Get-UninstallBaseline $baselineRoot @($record) -Verify -AllowUnavailable)
 Assert (@($baseline|Where-Object{$_.path -eq 'dxgi.dll' -and -not $_.available}).Count -eq 1) 'Missing original is reported per file'
 Assert (@($baseline|Where-Object{$_.path -eq 'WinPixEventRuntime.dll' -and $_.available -and -not $_.existed}).Count -eq 1) 'Unrelated owned removal remains available'
}finally{
 if([IO.Path]::GetFullPath($baselineRoot) -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub-streamline/unavailable-original-fixture'))){throw 'Unexpected cleanup root'}
 Remove-Item -LiteralPath $baselineRoot -Force
}
