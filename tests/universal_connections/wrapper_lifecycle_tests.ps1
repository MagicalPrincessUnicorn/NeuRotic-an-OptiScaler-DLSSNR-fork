$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
foreach($name in @('SafePath','HashFile')) {
 $definition=$ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)|Where-Object{$_.Name -eq $name}|Select-Object -First 1
 Invoke-Expression $definition.Extent.Text
}
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-ConnectionDependencies.ps1')
$root=Join-Path $repo 'builds/universal-connections/wrapper-fixtures'
if(Test-Path -LiteralPath $root){throw 'Fixture directory must be absent before this test'}
New-Item -ItemType Directory -Path $root|Out-Null
function Assert($v,$message){if(-not $v){throw $message};Write-Output "PASS $message"}
function Refuses($action,$message){$refused=$false;try{& $action|Out-Null}catch{$refused=$true};Assert $refused $message}
try {
 $game=Join-Path $root 'game';$dep=Join-Path $root 'dependency';New-Item -ItemType Directory -Path $game,$dep|Out-Null
 $exe=Join-Path $game 'fixture.exe';Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination $exe
 $dll=Join-Path $dep 'd3d9.dll';Copy-Item -LiteralPath "$env:SystemRoot/System32/kernel32.dll" -Destination $dll
 $manifest=Join-Path $dep 'dependency.json'
 $m=@{schema=1;identity='DXVK';version='fixture-only';licenseStatus='user-supplied';bitness=64;inputApi='D3D9';outputApi='Vulkan';files=@(@{path='d3d9.dll';sha256=(HashFile $dll)})}
 function SaveManifest {$m|ConvertTo-Json -Depth 5|Set-Content $manifest}
 SaveManifest
 $plan=Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'
 Assert ($plan.bitness -eq 64 -and $plan.restartRequired -and $plan.files.Count -eq 1) 'Exact bitness/hash plan without mutation'
 Assert (-not(Test-Path (Join-Path $game 'd3d9.dll'))) 'Preview leaves game files untouched'
 Test-ConnectionPreimage $plan
 Copy-Item -LiteralPath $dll -Destination (Join-Path $game 'd3d9.dll')
 Refuses {Test-ConnectionPreimage $plan} 'Concurrent destination creation rejected'
 $preserved=Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'
 Assert ($preserved.files[0].operation -eq 'preserve-existing') 'Matching installed wrapper preserved'
 [IO.File]::WriteAllText((Join-Path $game 'd3d9.dll'),'foreign')
 Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'Foreign proxy collision rejected'
 Remove-Item -LiteralPath (Join-Path $game 'd3d9.dll')
 $m.bitness=32;SaveManifest;Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'Wrong manifest bitness rejected'
 $m.bitness=64;$m.files[0].sha256='0'*64;SaveManifest;Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'Hash tamper rejected'
 $m.files[0].sha256=HashFile $dll;$m.files[0].path='../d3d9.dll';SaveManifest;Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'Traversal rejected'
 $m.files[0].path='d3d9.dll';$m.inputApi='D3D8';SaveManifest;Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'D3D8 x64 not advertised'
 $m.inputApi='D3D9';SaveManifest
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'existing ReShade')
 Refuses {Plan-ConnectionInstall $exe 'DXVK' $manifest 'winmm.dll'} 'Unknown proxy chain preserved for compatibility review'
 # Execute the actual managed engine against a tiny self-owned fixture package.
 Remove-Item -LiteralPath (Join-Path $game 'dxgi.dll')
 $package=Join-Path $root 'package';$support=Join-Path $package 'support';$payload=Join-Path $package 'payload'
 New-Item -ItemType Directory -Path $support,$payload|Out-Null
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1','NeuRotic-ConnectionDependencies.ps1')){Copy-Item -LiteralPath (Join-Path $repo ('tools/packaging/customer/support/'+$name)) -Destination (Join-Path $support $name)}
 foreach($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini')){[IO.File]::WriteAllText((Join-Path $payload $name),'Fixture bytes, never executed')}
 foreach($name in @('NeuRotic-Manual-Setup.cmd','NeuRotic-Manual-Uninstall.cmd')){[IO.File]::WriteAllText((Join-Path $package $name),'@exit /b 0')}
 $commit='a'*40
 @{commit=$commit;status='built';build=@{exit_code=0}}|ConvertTo-Json -Depth 3|Set-Content (Join-Path $support 'BUILD-MANIFEST.json')
 $packageFiles=@(Get-ChildItem -LiteralPath $package -File -Recurse|ForEach-Object{@{path=$_.FullName.Substring($package.Length+1);sha256=(HashFile $_.FullName)}})
 @{kind='neurotic-customer-candidate';name='Connection lifecycle fixture';commit=$commit;files=$packageFiles}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $support 'PACKAGE-MANIFEST.json')
 $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
 $runEngine=Join-Path $support 'NeuRotic-Setup-Engine.ps1'
 & $ps -NoProfile -ExecutionPolicy Bypass -File $runEngine -GameExecutable $exe -ProxyName winmm.dll -ConnectionWrapper DXVK -ConnectionDependencyManifest $manifest -ConfirmInstall -ContinueWithoutRuntime *> (Join-Path $root 'install.log')
 if($LASTEXITCODE -ne 0){throw (Get-Content -Raw (Join-Path $root 'install.log'))}
 Assert ((HashFile (Join-Path $game 'd3d9.dll')) -eq (HashFile $dll)) 'Existing managed transaction installs exact optional dependency'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $record=Get-Content -Raw $state.restore_chain[0]|ConvertFrom-Json
 Assert (@($record.files|Where-Object{$_.path -eq 'd3d9.dll' -and $_.operation -eq 'copy-user-dependency'}).Count -eq 1) 'Dependency tracked in same transaction receipt'
 & $ps -NoProfile -ExecutionPolicy Bypass -File $runEngine -GameExecutable $exe -Uninstall -UninstallMode Full -ConfirmUninstall *> (Join-Path $root 'uninstall.log')
 if($LASTEXITCODE -ne 0){throw (Get-Content -Raw (Join-Path $root 'uninstall.log'))}
 Assert (-not(Test-Path -LiteralPath (Join-Path $game 'd3d9.dll'))) 'Managed uninstall removes only exact owned dependency'
 Assert (Test-Path -LiteralPath $exe) 'Managed uninstall preserves source executable'
} finally {
 $expected=[IO.Path]::GetFullPath((Join-Path $repo 'builds/universal-connections/wrapper-fixtures'))
 if([IO.Path]::GetFullPath($root) -ne $expected){throw 'Unexpected cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $root -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $root -Recurse -Force
}
