# Read-only historical metadata comparison plus current repeat-install fixture.
# All writes stay inside this checkout's stable disposable fixture directory.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try {
 $package=New-SimplePackageFixture
 Move-Item -LiteralPath (Join-Path $package.root "payload/dependency.dll") -Destination (Join-Path $package.root "payload/WinPixEventRuntime.dll");($package.manifest.files|Where-Object{$_.path -eq "payload/dependency.dll"}).path="payload/WinPixEventRuntime.dll"
 $game=New-SimpleGameFixture 'performance';$exe=Join-Path $game 'FixtureGame.exe'
 $bulk=New-Object byte[] 1048576;$bulk[0]=1
 $members=@();for($i=0;$i -lt 64;$i++){
  $relative='OptiScaler/CharacterInspector/Version 1/data'+$i+'.bin'
  $path=Join-Path $package.root ('payload/'+$relative);[IO.File]::WriteAllBytes($path,$bulk)
  $members+=[pscustomobject]@{path=('payload/'+$relative)}
 }
 $package.manifest.files+=@($members)
 $legacy=Join-Path $script:SimpleRepo 'builds/installer-performance/baseline-support'
 $engine=Join-Path $legacy 'NeuRotic-Setup-Engine.ps1'
 # Load only the original function definitions. No old entry-point UI/mutation
 # or game launch runs. Read-only routines retain their original path checks.
 foreach($source in @($engine,(Join-Path $legacy 'NeuRotic-HubProtocol.ps1'),(Join-Path $legacy 'NeuRotic-HubSettings.ps1'),(Join-Path $legacy 'NeuRotic-HubHistory.ps1'))){
  $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
  if($errors.Count){throw 'Historical source could not be parsed'}
  foreach($node in $ast.EndBlock.Statements){if($node -is [Management.Automation.Language.FunctionDefinitionAst]){. ([scriptblock]::Create($node.Extent.Text))}}
 }
 # The settings module's top-level schema is product data, not a mutation.
 # Execute it from its historical directory so selection compares real schemas.
 . (Join-Path $legacy 'NeuRotic-HubSettings.ps1')
 $allowedProxies=@('dxgi.dll');$script:RestoreRecordLedger=@{};$script:RestoreMetadataMissing=$false
 $backup=Join-Path $game 'NeuRotic-test-backups/baseline';New-Item -ItemType Directory -Path $backup -Force|Out-Null
 $recordPath=Join-Path $backup 'INSTALL-MANIFEST.json';$oldFiles=@()
 foreach($member in $package.manifest.files){$relative=$member.path.Substring(8);if($relative -eq 'OptiScaler.dll'){$relative='dxgi.dll'};$destination=Join-Path $game $relative;New-Item -ItemType Directory -Path (Split-Path $destination) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $package.root $member.path) -Destination $destination;$oldFiles+=[pscustomobject]@{path=$relative.Replace('/','\');installed_hash=(Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant();installed_exists=$true}}
 $record=[ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;files=$oldFiles}
 [IO.File]::WriteAllText($recordPath,($record|ConvertTo-Json -Depth 8))
 $oldState=[pscustomobject]@{restore_chain=@($recordPath);status='installed-verified';selected_proxy='dxgi.dll'}
 $target=[pscustomobject]@{directory=$game;executable=$exe;bitness=64}
 $originalHash=${function:HashFile};$script:HashCalls=0;$script:HashedBytes=0
 function HashFile([string]$Path){$script:HashCalls++;$script:HashedBytes+=(Get-Item -LiteralPath $Path).Length;return & $originalHash $Path}
 function Show-SetupProgress {}
 $results=@();for($i=0;$i -lt 3;$i++){$script:HashCalls=0;$script:HashedBytes=0;$timer=[Diagnostics.Stopwatch]::StartNew();$observed=@(Get-HubCurrentFiles $target $oldState);$timer.Stop();if($observed.Count -ne $oldFiles.Count){throw 'Historical observation omitted owned files'};$results+=@([ordered]@{operation='historical-current-file-review';iteration=$i;milliseconds=$timer.Elapsed.TotalMilliseconds;hashCalls=$script:HashCalls;hashedBytes=$script:HashedBytes;ownedFiles=$observed.Count})}
 $oldReceipt=[ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory=$game;game_executable=$exe;selected_proxy='dxgi.dll';restore_chain=@($recordPath)}
 $oldReceiptPath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';New-Item -ItemType Directory -Path (Split-Path $oldReceiptPath) -Force|Out-Null
 [IO.File]::WriteAllText($oldReceiptPath,($oldReceipt|ConvertTo-Json -Depth 8))
 for($i=0;$i -lt 3;$i++){$script:HashCalls=0;$script:HashedBytes=0;$timer=[Diagnostics.Stopwatch]::StartNew();$inspected=Get-HubInspection $target -Quick;$timer.Stop();if($inspected.verification -ne 'NotChecked'){throw 'Historical quick-selection path changed'};$results+=@([ordered]@{operation='historical-game-selection-metadata';iteration=$i;milliseconds=$timer.Elapsed.TotalMilliseconds;hashCalls=$script:HashCalls;hashedBytes=$script:HashedBytes;settings=$inspected.settings.Count})}
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 . (Join-Path $script:SimpleSupport 'NeuRotic-HubProtocol.ps1')
 . (Join-Path $script:SimpleSupport 'NeuRotic-HubSettings.ps1')
 $receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';selectedExecutable='FixtureGame.exe';architecture='x64';packageId=$package.id;proxy='dxgi.dll';status='Installed';files=@($oldFiles|ForEach-Object{[pscustomobject]@{path=$_.path.Replace('\','/');removeOnUninstall=$true;role='payload'}})}
 Save-NeuroticInstallReceipt $game $receipt
 for($i=0;$i -lt 3;$i++){$script:HashCalls=0;$script:HashedBytes=0;$timer=[Diagnostics.Stopwatch]::StartNew();$inspected=Get-HubInspection $target -Quick;$timer.Stop();if($inspected.verification -ne 'Metadata'){throw 'Current metadata-selection path changed'};$results+=@([ordered]@{operation='current-game-selection-metadata';iteration=$i;milliseconds=$timer.Elapsed.TotalMilliseconds;hashCalls=$script:HashCalls;hashedBytes=$script:HashedBytes;settings=$inspected.settings.Count})}
 foreach($member in $package.manifest.files){if(-not $member.path){throw ("Malformed fixture member: "+($package.manifest.files|ConvertTo-Json -Depth 4))}}
 $profile=Get-NeuroticInstallProfile $exe $package $null
 $profile.proxy="dxgi.dll"
 for($i=0;$i -lt 3;$i++){$script:HashCalls=0;$script:HashedBytes=0;$timer=[Diagnostics.Stopwatch]::StartNew();$actions=@(Get-NeuroticInstallActions $game $package $profile $false $receipt @());$timer.Stop();$results+=@([ordered]@{operation='current-repeat-install-actions';iteration=$i;milliseconds=$timer.Elapsed.TotalMilliseconds;hashCalls=$script:HashCalls;hashedBytes=$script:HashedBytes;copyActions=@($actions|Where-Object{$_.operation -eq 'Copy'}).Count;reusedInspector=@($actions|Where-Object{$_.role -eq 'inspector' -and $_.operation -eq 'Reuse'}).Count})}
 $script:CopyBoundaries=0;function Invoke-SimpleFault([string]$Operation,[string]$Path){if($Operation -eq 'Copy'){$script:CopyBoundaries++}}
 $timer=[Diagnostics.Stopwatch]::StartNew();$installed=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() $null @() $null;$timer.Stop()
 if($installed.status -notin @('Succeeded','SucceededWithNotes')){throw ($installed|ConvertTo-Json -Depth 8)}
 $results+=@([ordered]@{operation='current-repeat-install';milliseconds=$timer.Elapsed.TotalMilliseconds;copied=$installed.copied.Count;reused=$installed.reused.Count;copyBoundaries=$script:CopyBoundaries;hashCalls=$script:HashCalls;hashedBytes=$script:HashedBytes})
 $out=Join-Path $script:SimpleRepo 'docs/implementation-evidence/overnight-universal-intake/installer-performance.json'
 [IO.File]::WriteAllText($out,([ordered]@{baselineCommit='e1774af8';fixturePayloadMiB=64;processStartupIncluded=$false;comparison='Historical live-owned-file review versus current repeat-install action construction; not identical end-to-end operations';instrumentation='Actual HashFile invocations and bytes; current action and copy boundary counts. Does not count every filesystem metadata probe or OS cache I/O.';realGamesChanged=$false;samples=$results}|ConvertTo-Json -Depth 8))
 $results|ConvertTo-Json -Depth 5
}catch{Write-Output $_.ScriptStackTrace;throw}finally{End-SimpleFixtures}
