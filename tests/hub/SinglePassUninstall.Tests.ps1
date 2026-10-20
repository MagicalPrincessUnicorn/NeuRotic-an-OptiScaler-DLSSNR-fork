. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub-streamline/single-uninstall-fixture'
$script:Support=Join-Path $script:Work 'app/support'
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-streamline/single-uninstall-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force;Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
function Write-TestJson($path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 16))}
function New-LongHistory($name,[int]$count=18){
 $game=New-GameFixture $name;$exe=Join-Path $game 'FixtureGame.exe';$chain=@();$history=@();$manager=Join-Path $game 'NeuRotic/Installer';New-Item -ItemType Directory -Path $manager -Force|Out-Null
 foreach($i in 0..($count-1)){
  $backup=Join-Path $game ('NeuRotic-backups/install-'+$i);New-Item -ItemType Directory -Path (Join-Path $backup 'previous') -Force|Out-Null
  $files=@(foreach($target in @('dxgi.dll','OptiScaler.ini','WinPixEventRuntime.dll')){
   $before=if($i -eq 0){'original '+$target}else{'version '+($i-1)+' '+$target};$after='version '+$i+' '+$target
   $previous=Join-Path $backup ('previous/'+$target);[IO.File]::WriteAllText($previous,$before);$live=Join-Path $game $target;[IO.File]::WriteAllText($live,$after)
   [pscustomobject]@{path=$target;existed=$true;previous_hash=(Get-FileHash $previous).Hash;installed_hash=(Get-FileHash $live).Hash;operation='copy-package'}
  })
  $path=Join-Path $backup 'INSTALL-MANIFEST.json'
  Write-TestJson $path ([ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;selected_proxy='dxgi.dll';files=$files;previous_record=$(if($chain.Count){$chain[-1]}else{$null});owned_files=@();restored_utc=$null;reconciled_paths=@();completed_utc='2026-10-04T12:00:00Z'})
  $chain+=$path;$history+=[pscustomobject]@{record=$path;install_id=[string]$i}
 }
 Write-TestJson (Join-Path $manager 'Current-Install.json') ([ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';product='NeuRotic';release_name='Single uninstall fixture';commit=('a'*40);game_directory=$game;game_executable=$exe;selected_proxy='dxgi.dll';restore_chain=$chain;install_history=$history;manager_files=@();uninstalled_utc=$null;uninstall_mode=$null;first_install_utc='2026-10-01T12:00:00Z';last_install_utc='2026-10-04T12:00:00Z'})
 return $game
}
function Uninstall-Plan($game,$mode='KeepSettings'){HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Uninstall';uninstallMode=$mode})}
Init-HubFixtures
try{
 New-Item -ItemType Directory -Path $script:Support -Force|Out-Null
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)}
 $game=New-LongHistory 'many';$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json
 $latest=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($latest,"[User]`nKeep=latest`n");$iniHash=(Get-FileHash $latest).Hash
 # No final restoration needs this middle-generation payload. Lock it to prove
 # uninstall neither hashes nor restores each historical generation.
 $unused=Join-Path (Split-Path $state.restore_chain[8]) 'previous/dxgi.dll';$handle=[IO.File]::Open($unused,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{$result=Execute-Plan (Uninstall-Plan $game)}finally{$handle.Dispose()}
 Assert ($result.status -eq 'Succeeded') 'Many-record uninstall does not consume an unused middle backup'
 foreach($name in @('dxgi.dll','OptiScaler.ini','WinPixEventRuntime.dll')){Assert ([IO.File]::ReadAllText((Join-Path $game $name)) -eq ('original '+$name)) ('Original restored once: '+$name)}
 Assert ((Get-FileHash (Join-Path $game 'NeuRotic/UserData/OptiScaler.ini')).Hash -eq $iniHash) 'KeepSettings preserves exact latest user settings separately'
 $undo=@(Get-ChildItem -LiteralPath (Join-Path $game 'NeuRotic-backups') -Directory -Recurse|Where-Object{$_.Name -like 'restore-*'})
 Assert ($undo.Count -eq 0) 'Uninstall creates no per-generation restore copies'
 foreach($path in $state.restore_chain){Assert ((Get-Content -Raw $path|ConvertFrom-Json).status -eq 'restored') 'Every retained manifest becomes inactive'}
 $game=New-LongHistory 'full';$state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $unknown=Join-Path (Split-Path $state.restore_chain[3]) 'my-notes.txt';[IO.File]::WriteAllText($unknown,'user note')
 $changed=Join-Path (Split-Path $state.restore_chain[4]) 'previous/WinPixEventRuntime.dll';[IO.File]::AppendAllText($changed,'user changed retired backup')
 $result=Execute-Plan (Uninstall-Plan $game 'Full');Assert ($result.status -eq 'Succeeded') 'Full uninstall restores once and cleans recorded owned history'
 Assert ((Test-Path $unknown) -and (Test-Path $changed) -and (Test-Path (Join-Path $game 'private.txt')) -and (Test-Path (Join-Path $game 'nvngx_dlssnr.dll'))) 'Full cleanup preserves unknown changed and private files'
 Assert (-not(Test-Path (Join-Path $game 'NeuRotic/Installer/Current-Install.json')) -and -not(Test-Path $state.restore_chain[0])) 'Full cleanup removes owned state and original record'
 $game=New-LongHistory 'bad-baseline';$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json;$baseline=Join-Path (Split-Path $state.restore_chain[0]) 'previous/dxgi.dll';$plan=Uninstall-Plan $game;[IO.File]::AppendAllText($baseline,'corrupt');$before=(Get-FileHash $statePath).Hash
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded' -and -not(Test-Path (Join-Path $game 'dxgi.dll')) -and @(Get-Content -Raw $statePath|ConvertFrom-Json|ForEach-Object{$_.uninstall_limitations}).Count -eq 1 -and (Test-Path $baseline)) 'Unavailable original is reported while unrelated uninstall completes and backup evidence remains'
 $game=New-LongHistory 'reconciled';$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -Raw $statePath|ConvertFrom-Json
 $record=Get-Content -Raw $state.restore_chain[12]|ConvertFrom-Json;$record.reconciled_paths=@('dxgi.dll');$saved=Join-Path $record.backup 'previous/dxgi.dll';[IO.File]::WriteAllText($saved,'external reconciled loader');$record.files[0].previous_hash=(Get-FileHash $saved).Hash;Write-TestJson $state.restore_chain[12] $record
 $state|Add-Member -NotePropertyName adopted_modified_install -NotePropertyValue $true;Write-TestJson $statePath $state
 $first=Get-Content -Raw $state.restore_chain[0]|ConvertFrom-Json;$moved=Join-Path $first.backup 'previous/version.dll';[IO.File]::WriteAllText($moved,'relocated duplicate loader');$first.files+= [pscustomobject]@{path='version.dll';existed=$true;previous_hash=(Get-FileHash $moved).Hash;restore_exists=$false;installed_exists=$false;installed_hash=$null};Write-TestJson $state.restore_chain[0] $first
 $result=Execute-Plan (Uninstall-Plan $game 'Full');Assert ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'original dxgi.dll' -and -not(Test-Path (Join-Path $game 'version.dll'))) 'Adopted reconciled history and relocated-proxy absence preserve the effective original baseline'
 foreach($stage in @('during-baseline','during-metadata')){
  $game=New-LongHistory ('crash-'+$stage) 8;$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$stateHash=(Get-FileHash $statePath).Hash;$loaderHash=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash
  $plan=Uninstall-Plan $game;$env:NEUROTIC_UNINSTALL_TEST_STOP='crash-'+$stage
  try{$result=Execute-Plan $plan}finally{Remove-Item Env:NEUROTIC_UNINSTALL_TEST_STOP}
  Assert ($result.status -eq 'RecoveryRequired') ($stage+' crash retains durable pre-uninstall recovery')
  $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}))
  Assert ($result.status -eq 'Succeeded' -and (Get-FileHash $statePath).Hash -eq $stateHash -and (Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $loaderHash) ($stage+' recovery restores exact pre-uninstall state')
 }
 $game=New-LongHistory 'external-edit' 6;$plan=Uninstall-Plan $game;$env:NEUROTIC_UNINSTALL_TEST_STOP='crash-during-baseline'
 try{$result=Execute-Plan $plan}finally{Remove-Item Env:NEUROTIC_UNINSTALL_TEST_STOP}
 $loader=Join-Path $game 'dxgi.dll';[IO.File]::WriteAllText($loader,'external edit after interrupted uninstall')
 $result=Execute-Plan (HubCall ([ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation='Restore'}))
 Assert ($result.status -eq 'RecoveryRequired' -and [IO.File]::ReadAllText($loader) -eq 'external edit after interrupted uninstall') 'Recovery refuses to overwrite a new external edit'
 # Inject a saved-settings edit after the final prewrite scan to exercise the
 # actual deletion boundary, not just the earlier inventory comparison.
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1'),[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 $game=New-LongHistory 'saved-settings-race' 3;$GameDirectory=$game;$GameExecutable=Join-Path $game 'FixtureGame.exe';$UninstallMode='Full';$ConfirmUninstall=$true;$CheckOnly=$false;$Installed=$false;$RemovePrivateModel=$false;$LegacyManifest=$null;$HubPlanPreviewPath=$null;$HubNonInteractive=$true
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
 $script:savedRace=Join-Path $game 'NeuRotic/UserData/OptiScaler.ini';New-Item -ItemType Directory -Path (Split-Path $script:savedRace) -Force|Out-Null;[IO.File]::WriteAllText($script:savedRace,'saved prior settings');$script:closeChecks=0
 function CheckGameClosed([string]$Path){$script:closeChecks++;if($script:closeChecks -eq 2){[IO.File]::WriteAllText($script:savedRace,'outside settings change')}}
 $refused=$false;try{Invoke-PublicUninstall|Out-Null}catch{$refused=$true}
 Assert ($refused -and $script:closeChecks -eq 2 -and [IO.File]::ReadAllText($script:savedRace) -eq 'outside settings change') 'Saved-settings removal refuses a write-boundary race without deleting the edit'
}finally{End-HubFixtures}
