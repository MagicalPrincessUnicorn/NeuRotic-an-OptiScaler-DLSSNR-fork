. (Join-Path $PSScriptRoot 'X86InstallFixtureCommon.ps1')
$work=Join-Path $repo 'builds/universal-connections/x86-installer'
if(Test-Path -LiteralPath $work){throw 'Prior x86 fixtures remain; inspect before reuse'}
[void][IO.Directory]::CreateDirectory($work)
$priorFixtureRoot=$env:NEUROTIC_HUB_FIXTURE_ROOT;$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user-data'
try{
 $package=Join-Path $work 'package';[void][IO.Directory]::CreateDirectory((Join-Path $package 'support'))
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1')){Copy-Item -LiteralPath (Join-Path $support $name) -Destination (Join-Path $package ('support/'+$name))}
 Write-X86File $package 'NeuRotic-Manual-Setup.cmd' ([Text.Encoding]::ASCII.GetBytes('fixture'))
 Write-X86File $package 'NeuRotic-Manual-Uninstall.cmd' ([Text.Encoding]::ASCII.GetBytes('fixture'))
 Write-X86File $package 'payload/ReShade32.dll' (New-PeBytes 32 1)
 Write-X86File $package 'payload/NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32' (New-PeBytes 32 2)
 foreach($name in @('NeuRotic.PreparedGpuHost.exe','OptiScaler.dll','WinPixEventRuntime.dll','nvngx.dll_dlssnr.dll')){Write-X86File $package ('payload/NeuRotic/Prepared/NeuRotic.GpuHost/'+$name) (New-PeBytes 64 3)}
 Write-X86File $package 'payload/NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini' ([Text.Encoding]::ASCII.GetBytes("[Neurotic]`r`nSource=3`r`n"))
 Write-X86File $package 'payload/NeuRotic/Prepared/Shaders/Prepared.fx' ([Text.Encoding]::ASCII.GetBytes('fixture shader'))
 Write-X86File $package 'payload/NeuRotic/Prepared/Prepared.ini' ([Text.Encoding]::ASCII.GetBytes('Techniques=Prepared'))
 Write-X86File $package 'payload/NeuRotic/Prepared/Start-PreparedGame.ps1' ([Text.Encoding]::ASCII.GetBytes('fixture launch'))
 Write-X86File $package 'payload/ReShade.ini' ([Text.Encoding]::ASCII.GetBytes("[GENERAL]`r`nEffectSearchPaths=.\NeuRotic\Prepared\Shaders,.\NeuRotic\Prepared\Shaders,,extra`r`nPresetPath=.\NeuRotic\Prepared\Prepared.ini`r`nPreprocessorDefinitions=NRPG_MV_PROVIDER=0`r`n[ADDON]`r`nAddonPath=.\NeuRotic\Prepared`r`n"))
 $build=[ordered]@{commit=('a'*40);status='built';build=@{exit_code=0};bit_size=64;install_route='prepared-x86';target_bit_size=32}
 [IO.File]::WriteAllText((Join-Path $package 'support/BUILD-MANIFEST.json'),($build|ConvertTo-Json -Depth 4));Seal-X86Package $package
 $game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory($game)
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'SysWOW64/cmd.exe') -Destination (Join-Path $game 'FixtureGame.exe')
 $original=[Text.Encoding]::Unicode.GetBytes("[GENERAL]`r`nEffectSearchPaths=./my,,shaders,./other-shaders`r`nPresetPath=./my-preset.ini`r`nPreprocessorDefinitions=RESHADE_DEPTH_INPUT_IS_REVERSED=1,NRPG_MV_PROVIDER=2`r`n[INPUT]`r`nKeyOverlay=36,0,0,0`r`n")
 $original=([byte[]]@(0xff,0xfe))+$original
 Write-X86File $game 'ReShade.ini' $original;Write-X86File $game 'dxgi.dll' (New-PeBytes 32 19)
 Write-X86File $game 'my-preset.ini' ([Text.Encoding]::ASCII.GetBytes('my preset'));Write-X86File $game 'nvngx_dlssnr.dll' ([Text.Encoding]::ASCII.GetBytes('private model'))
 $proxyHash=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash;$configHash=(Get-FileHash (Join-Path $game 'ReShade.ini')).Hash
 $result=Invoke-X86Engine $package $game @('-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly')
 Assert-X86 ($result.exit -eq 0) ('x86 verified package preflight admitted: '+$result.log)
 Assert-X86 (-not (Test-Path (Join-Path $game 'NeuRotic'))) 'check-only creates no game install directories'
 $result=Invoke-X86Engine $package $game @('-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-ConfirmInstall','-ContinueWithoutRuntime')
 Assert-X86 ($result.exit -eq 0) ('x86 transaction installs: '+$result.log)
 Assert-X86 ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq (Get-FileHash (Join-Path $package 'payload/ReShade32.dll')).Hash) 'x86 loader comes from ReShade32, never x64 OptiScaler'
 Assert-X86 (-not (Test-Path (Join-Path $game 'OptiScaler.dll')) -and -not (Test-Path (Join-Path $game 'nvngx.dll_dlssnr.dll'))) '64-bit runtime stays inside prepared helper directory'
 $config=[IO.File]::ReadAllText((Join-Path $game 'ReShade.ini'))
 $expectedSearchPaths='EffectSearchPaths=./my,,shaders,./other-shaders,.\NeuRotic\Prepared\Shaders,.\NeuRotic\Prepared\Shaders,,extra'
 Assert-X86 ($config.Contains($expectedSearchPaths)) 'routing merge preserves escaped literal comma and original search path order'
 Assert-X86 ($config.Contains('KeyOverlay=36,0,0,0') -and $config.Contains('NeuRotic\Prepared')) 'existing ReShade config retains custom values and gains prepared paths'
 Assert-X86 ($config.Contains('RESHADE_DEPTH_INPUT_IS_REVERSED=1') -and $config.Contains('NRPG_MV_PROVIDER=0') -and -not $config.Contains('NRPG_MV_PROVIDER=2') -and $config.Contains('[ADDON]')) 'routing merge retains depth definitions and replaces only prepared motion provider'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 Assert-X86 ($state.install_route -eq 'prepared-x86' -and $state.target_bit_size -eq 32) 'receipt names actual x86 installation route'
 Write-X86File $package 'payload/ReShade32.dll' (New-PeBytes 32 4);Seal-X86Package $package
 Write-X86File $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini' ([Text.Encoding]::ASCII.GetBytes("[Neurotic]`r`nSource=3`r`nCustomQuality=11`r`n"))
 $helperConfigHash=(Get-FileHash (Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini')).Hash
 $result=Invoke-X86Engine $package $game @('-ExistingInstallAction','Update','-ConfirmInstall','-ContinueWithoutRuntime')
 Assert-X86 ($result.exit -eq 0 -and (Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq (Get-FileHash (Join-Path $package 'payload/ReShade32.dll')).Hash) ('x86 update keeps restore lineage: '+$result.log)
 $updatedConfig=[IO.File]::ReadAllText((Join-Path $game 'ReShade.ini'))
 $updatedSearchPaths=[regex]::Match($updatedConfig,'(?m)^EffectSearchPaths=[^\r\n]+').Value
 Assert-X86 ($updatedSearchPaths -eq $expectedSearchPaths) 'repeated routing merge preserves path list and does not duplicate prepared path'
 Assert-X86 ((Get-FileHash (Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini')).Hash -eq $helperConfigHash) 'update preserves user helper settings without adoption'
 Remove-Item -LiteralPath (Join-Path $game 'NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32')
 $result=Invoke-X86Engine $package $game @('-ExistingInstallAction','Repair','-ConfirmInstall','-ContinueWithoutRuntime')
 Assert-X86 ($result.exit -eq 0 -and (Test-Path (Join-Path $game 'NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32'))) ('x86 repair restores missing addon: '+$result.log)
 Write-X86File $game 'NeuRotic/Prepared/Shaders/Prepared.fx' ([Text.Encoding]::ASCII.GetBytes('user modified shader'))
 foreach($stage in @('crash-during-baseline','crash-during-metadata')){
  $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json'
  $stateBefore=(Get-FileHash $statePath).Hash;$installedProxy=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash
  $env:NEUROTIC_UNINSTALL_TEST_STOP=$stage
  try{$result=Invoke-X86Engine $package $game @('-Uninstall','-UninstallMode','KeepSettings','-ConfirmUninstall')}finally{Remove-Item Env:NEUROTIC_UNINSTALL_TEST_STOP}
  Assert-X86 (Test-Path (Join-Path $game 'NeuRotic/Installer/Uninstall-Transaction.json')) ($stage+' retains recovery journal')
  $result=Invoke-X86Engine $package $game @('-Uninstall','-UninstallMode','KeepSettings','-ConfirmUninstall')
  Assert-X86 ($result.exit -eq 0 -and (Get-FileHash $statePath).Hash -eq $stateBefore -and (Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $installedProxy -and (Get-FileHash (Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini')).Hash -eq $helperConfigHash) ($stage+' restores exact pre-uninstall x86 state and helper settings: '+$result.log)
 }
 Write-X86File $game 'NeuRotic/Prepared/private.txt' ([Text.Encoding]::ASCII.GetBytes('unknown user file'))
 $result=Invoke-X86Engine $package $game @('-Uninstall','-UninstallMode','KeepSettings','-ConfirmUninstall')
 Assert-X86 ($result.exit -eq 0) ('x86 uninstall transaction succeeds: '+$result.log)
 Assert-X86 ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $proxyHash -and (Get-FileHash (Join-Path $game 'ReShade.ini')).Hash -eq $configHash) 'uninstall restores exact original proxy and UTF16 configuration'
 $copies=@(Get-ChildItem -LiteralPath (Join-Path $game 'NeuRotic-preserved-files') -File -Recurse -Filter FILES.json|ForEach-Object{
  $receipt=$_;$preserved=Get-Content -Raw -LiteralPath $receipt.FullName|ConvertFrom-Json
  foreach($entry in $preserved.files){if($entry.path -eq 'NeuRotic\Prepared\Shaders\Prepared.fx'){
   $snapshot=Join-Path $receipt.DirectoryName $entry.snapshot
   if((Get-FileHash -LiteralPath $snapshot).Hash -eq $entry.sha256 -and [IO.File]::ReadAllText($snapshot) -eq 'user modified shader'){$snapshot}
  }}
 })
 Assert-X86 ($copies.Count -gt 0) 'uninstall retains modified shader bytes separately'
 Assert-X86 ((Test-Path (Join-Path $game 'NeuRotic/Prepared/private.txt')) -and (Test-Path (Join-Path $game 'my-preset.ini')) -and (Test-Path (Join-Path $game 'nvngx_dlssnr.dll'))) 'uninstall preserves unknown files, user preset and private model'
 Assert-X86 ((Get-FileHash (Join-Path $game 'NeuRotic/UserData/PreparedGpuHost.ini')).Hash -eq $helperConfigHash) 'KeepSettings retains latest helper settings'
 $wrong=Join-Path $work 'wrong-game';[void][IO.Directory]::CreateDirectory($wrong);Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $wrong 'FixtureGame.exe')
 $result=Invoke-X86Engine $package $wrong @('-ProxyName','dxgi.dll','-CheckOnly')
 Assert-X86 ($result.exit -ne 0 -and $result.log -match 'architecture|bitness' -and -not (Test-Path (Join-Path $wrong 'NeuRotic'))) 'prepared x86 package refuses x64 game before mutation'
 Write-X86File $package 'payload/ReShade32.dll' (New-PeBytes 64 8);Seal-X86Package $package
 $result=Invoke-X86Engine $package $game @('-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly')
 Assert-X86 ($result.exit -ne 0 -and $result.log -match 'architecture') 'prepared package refuses a 64-bit loader before mutation'
 Write-X86File $package 'payload/ReShade32.dll' (New-PeBytes 32 8)
 Write-X86File $package 'payload/OptiScaler.dll' (New-PeBytes 64 8);Seal-X86Package $package
 $result=Invoke-X86Engine $package $game @('-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly')
 Assert-X86 ($result.exit -ne 0 -and $result.log -match 'Unexpected prepared x86 payload') 'prepared package refuses misplaced root OptiScaler payload'
 $build.install_route='native-x64';$build.target_bit_size=64
 [IO.File]::WriteAllText((Join-Path $package 'support/BUILD-MANIFEST.json'),($build|ConvertTo-Json -Depth 4))
 Write-X86File $package 'payload/nvngx.dll_dlssnr.dll' (New-PeBytes 64 7);Write-X86File $package 'payload/OptiScaler.ini' ([Text.Encoding]::ASCII.GetBytes('fixture native config'));Seal-X86Package $package
 $result=Invoke-X86Engine $package $game @('-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly')
 Assert-X86 ($result.exit -ne 0 -and $result.log -match 'architecture does not match this 64-bit') 'native x64 package refuses PE32 game before mutation'
}finally{
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/universal-connections/x86-installer'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$priorFixtureRoot
}
