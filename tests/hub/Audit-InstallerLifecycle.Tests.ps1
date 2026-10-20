param([switch]$Reset)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/audit-installer-lifecycle'
if(Test-Path -LiteralPath $work){
 if(-not $Reset){throw 'Inspect and clear the previous owned fixture before reuse'}
 $resolved=[IO.Path]::GetFullPath($work)
 if($resolved -ine ([IO.Path]::GetFullPath((Join-Path $repo 'builds/audit-installer-lifecycle')))){throw 'Unexpected fixture path'}
 $queue=New-Object Collections.Generic.Queue[string];$queue.Enqueue($resolved)
 while($queue.Count){foreach($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};if($item.PSIsContainer){$queue.Enqueue($item.FullName)}}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
[void][IO.Directory]::CreateDirectory($work)
$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
$support=Join-Path $work 'package/support';$payload=Join-Path $work 'package/payload'
[void][IO.Directory]::CreateDirectory($support);[void][IO.Directory]::CreateDirectory($payload)
Copy-Item -Path (Join-Path $repo 'tools/packaging/customer/support/*') -Destination $support
function Put($path,$text){[void][IO.Directory]::CreateDirectory((Split-Path $path));[IO.File]::WriteAllText($path,$text)}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output "PASS $message"}
Copy-Item -LiteralPath "$env:windir/System32/cmd.exe" -Destination (Join-Path $payload 'OptiScaler.dll')
Put (Join-Path $payload 'OptiScaler.ini') '[User]Setting=1'
Put (Join-Path $payload 'nvngx.dll_dlssnr.dll') 'fixture forwarder'
Put (Join-Path $payload 'WinPixEventRuntime.dll') 'retire me'
Put (Join-Path $payload 'OptiScaler/obsolete-fixture.bin') 'owned old optional component'
Put (Join-Path $payload 'OptiScaler/CharacterInspector/inspector-fixture.bin') 'optional inspector fixture'
Put (Join-Path $support 'BUILD-MANIFEST.json') (@{commit=('a'*40);status='built';build=@{exit_code=0}}|ConvertTo-Json)
function Seal {
 $package=Split-Path $support;$manifest=Join-Path $support 'PACKAGE-MANIFEST.json'
 $files=@(Get-ChildItem -LiteralPath $package -Recurse -File|Where-Object{$_.FullName -ne $manifest}|ForEach-Object{@{path=$_.FullName.Substring($package.Length+1);sha256=(Get-FileHash $_.FullName).Hash}})
 Put $manifest (@{kind='neurotic-customer-candidate';name='audit';commit=('a'*40);files=$files}|ConvertTo-Json -Depth 8)
}
Seal
$game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory($game)
$exe=Join-Path $game 'AuditFixtureGame.exe';Copy-Item -LiteralPath "$env:windir/System32/cmd.exe" -Destination $exe
$engine=Join-Path $support 'NeuRotic-Setup-Engine.ps1'
& $engine -GameExecutable $exe -ProxyName dxgi.dll -HubNonInteractive -ContinueWithoutRuntime -ConfirmInstall *> (Join-Path $work 'install.log')
$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$state=Get-Content $statePath -Raw|ConvertFrom-Json
$record=Get-Content $state.restore_chain[-1] -Raw|ConvertFrom-Json
Assert (@($record.files|Where-Object{$_.path -eq 'Uninstall NeuRotic.cmd'}).Count -eq 1) 'Generated launcher is committed ownership'
Assert (-not(Test-Path -LiteralPath (Join-Path $game 'OptiScaler/CharacterInspector/inspector-fixture.bin'))) 'Fresh installation omits optional Inspector by default'
Put (Join-Path $game 'OptiScaler/obsolete-fixture.bin') 'externally changed optional component'
Remove-Item -LiteralPath (Join-Path $payload 'OptiScaler/obsolete-fixture.bin')
Remove-Item -LiteralPath (Join-Path $payload 'WinPixEventRuntime.dll');Seal
& $engine -GameExecutable $exe -ProxyName dxgi.dll -ExistingInstallAction Update -HubNonInteractive -ContinueWithoutRuntime -IncludeInspector -ConfirmInstall *> (Join-Path $work 'update.log')
Assert (Test-Path -LiteralPath (Join-Path $game 'OptiScaler/CharacterInspector/inspector-fixture.bin')) 'Explicit Inspector inclusion deploys the optional payload'
Assert (-not (Test-Path -LiteralPath (Join-Path $game 'WinPixEventRuntime.dll'))) 'Update retires omitted owned component'
Assert ([IO.File]::ReadAllText((Join-Path $game 'OptiScaler/obsolete-fixture.bin')) -eq 'externally changed optional component') 'Update preserves modified omitted component'
$state=Get-Content $statePath -Raw|ConvertFrom-Json;$restore=Join-Path (Split-Path $state.restore_chain[-1]) 'NeuRotic-Setup-Engine.ps1'
& $restore -Restore -HubNonInteractive *> (Join-Path $work 'restore.log')
$state=Get-Content $statePath -Raw|ConvertFrom-Json
Assert ($state.status -eq 'installed-verified' -and @($state.restore_chain).Count -eq 1) 'Individual restore rewinds active lineage'
Assert ([IO.File]::ReadAllText((Join-Path $game 'WinPixEventRuntime.dll')) -eq 'retire me') 'Individual restore recovers retired component'
Put (Join-Path $game 'Uninstall NeuRotic.cmd') 'foreign launcher keep'
& $engine -Uninstall -GameDirectory $game -UninstallMode Full -ConfirmUninstall -HubNonInteractive *> (Join-Path $work 'uninstall.log')
Assert ([IO.File]::ReadAllText((Join-Path $game 'Uninstall NeuRotic.cmd')) -eq 'foreign launcher keep') 'Full uninstall preserves modified launcher'
$deposit=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Runtime/streamline';[void][IO.Directory]::CreateDirectory($deposit)
foreach($name in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')){[IO.File]::Copy((Join-Path (Split-Path $repo) ('provider-baselines/20260926-canonical/'+$name)),(Join-Path $deposit $name),$true)}
$dependencyGame=Join-Path $work 'dependencies';[void][IO.Directory]::CreateDirectory($dependencyGame);$dependencyExe=Join-Path $dependencyGame 'DependencyFixture.exe';[IO.File]::Copy("$env:windir/System32/cmd.exe",$dependencyExe)
$foreign=Join-Path $dependencyGame 'OptiScaler/streamline/sl.interposer.dll';Put $foreign 'foreign Streamline mod'
$hash=(Get-FileHash -LiteralPath $foreign).Hash
$decisions=@(@{path='OptiScaler\streamline\sl.interposer.dll';sha256=$hash;action='Skip'})|ConvertTo-Json -Compress
try{& $engine -GameExecutable $dependencyExe -ProxyName dxgi.dll -HubNonInteractive -ContinueWithoutRuntime -ConfirmInstall -FileDecisionsJson $decisions *> (Join-Path $work 'dependency-skip.log');throw 'Skip unexpectedly installed'}catch{if(Test-Path -LiteralPath (Join-Path $dependencyGame 'dxgi.dll')){throw 'Skip mutated the live installation'}}
Assert ([IO.File]::ReadAllText($foreign) -eq 'foreign Streamline mod') 'Runtime-deposit Skip leaves all game files untouched'
$decisions=@(@{path='OptiScaler\streamline\sl.interposer.dll';sha256=$hash;action='SaveAndReplace'})|ConvertTo-Json -Compress
& $engine -GameExecutable $dependencyExe -ProxyName dxgi.dll -HubNonInteractive -ContinueWithoutRuntime -ConfirmInstall -FileDecisionsJson $decisions *> (Join-Path $work 'dependency-install.log')
Assert ((Get-FileHash -LiteralPath $foreign).Hash -eq (Get-FileHash -LiteralPath (Join-Path $deposit 'sl.interposer.dll')).Hash) 'Verified deposited cohort reaches native runtime consumer location'
Assert (@(Get-ChildItem -LiteralPath (Join-Path $dependencyGame 'NeuRotic-preserved-files') -Recurse -File|Where-Object{[IO.File]::ReadAllText($_.FullName) -eq 'foreign Streamline mod'}).Count -eq 1) 'Save and Replace retains the foreign dependency outside cleanup'
& $engine -Uninstall -GameDirectory $dependencyGame -UninstallMode Full -ConfirmUninstall -HubNonInteractive *> (Join-Path $work 'dependency-uninstall.log')
Assert ([IO.File]::ReadAllText($foreign) -eq 'foreign Streamline mod') 'Managed dependency uninstall restores exact original bytes'
