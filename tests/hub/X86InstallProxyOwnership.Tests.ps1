. (Join-Path $PSScriptRoot 'X86InstallFixtureCommon.ps1')
$work=Join-Path $repo 'builds/universal-connections/x86-proxy-ownership'
if(Test-Path -LiteralPath $work){throw 'Prior proxy ownership fixtures remain; inspect before reuse'}
[void][IO.Directory]::CreateDirectory($work)
$priorFixtureRoot=$env:NEUROTIC_HUB_FIXTURE_ROOT;$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user-data'
try{
 foreach($name in @('bundled','renamed')){
  $package=Join-Path $work ($name+'-package');[void][IO.Directory]::CreateDirectory((Join-Path $package 'support'))
  foreach($file in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1')){Copy-Item -LiteralPath (Join-Path $support $file) -Destination (Join-Path $package ('support/'+$file))}
  foreach($file in @('NeuRotic-Manual-Setup.cmd','NeuRotic-Manual-Uninstall.cmd')){Write-X86File $package $file ([Text.Encoding]::ASCII.GetBytes('fixture'))}
  Write-X86File $package 'payload/OptiScaler.dll' (New-PeBytes 64 1)
  Write-X86File $package 'payload/nvngx.dll_dlssnr.dll' (New-PeBytes 64 2)
  Write-X86File $package 'payload/OptiScaler.ini' ([Text.Encoding]::ASCII.GetBytes("[Plugins]`r`nLoadReshade=false`r`n"))
  Write-X86File $package 'payload/ReShade64.dll' (New-PeBytes 64 3)
  [IO.File]::WriteAllText((Join-Path $package 'support/BUILD-MANIFEST.json'),([ordered]@{commit=('a'*40);status='built';build=@{exit_code=0};bit_size=64}|ConvertTo-Json -Depth 4));Seal-X86Package $package
  $game=Join-Path $work ($name+'-game');[void][IO.Directory]::CreateDirectory($game)
  Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $game 'FixtureGame.exe')
  $options=@('-ProxyName','dxgi.dll','-ConfirmInstall','-ContinueWithoutRuntime')
  if($name -eq 'renamed'){
   Write-X86File $game 'dxgi.dll' (New-PeBytes 64 19)
   Write-X86File $game 'OptiScaler.ini' (([byte[]]@(0xff,0xfe))+[Text.Encoding]::Unicode.GetBytes("[Plugins]`r`nLoadReshade=false`r`nCustom=keep`r`n"))
   $originalProxyHash=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash;$originalIniHash=(Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash
   $options+=@('-ExistingProxyAction','RenameReShade')
  }
  $result=Invoke-X86Engine $package $game $options
  Assert-X86 ($result.exit -eq 0) ($name+' proxy ownership fixture installs: '+$result.log)
  $reshadeHash=(Get-FileHash (Join-Path $game 'ReShade64.dll')).Hash
  if($name -eq 'renamed'){
   $result=Invoke-X86Engine $package $game @('-ExistingInstallAction','Update','-ConfirmInstall','-ContinueWithoutRuntime')
   Assert-X86 ($result.exit -eq 0) ('chained installation update records another transaction: '+$result.log)
   Assert-X86 ((Get-FileHash (Join-Path $game 'ReShade64.dll')).Hash -eq $originalProxyHash) 'bundled payload update preserves original user ReShade rename bytes'
  }
  $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$stateHash=(Get-FileHash $statePath).Hash
  $result=Invoke-X86Engine $package $game @('-ExistingInstallAction','ChangeProxy','-ProxyName','winmm.dll','-CheckOnly')
  if($name -eq 'bundled'){
   Assert-X86 ($result.exit -eq 0) ('bundled ReShade permits proxy change preview: '+$result.log)
   $result=Invoke-X86Engine $package $game @('-ExistingInstallAction','ChangeProxy','-ProxyName','winmm.dll','-ConfirmInstall','-ContinueWithoutRuntime')
   Assert-X86 ($result.exit -eq 0 -and -not (Test-Path (Join-Path $game 'dxgi.dll')) -and (Test-Path (Join-Path $game 'winmm.dll'))) ('bundled ReShade permits transactional proxy change: '+$result.log)
   Assert-X86 ((Get-FileHash (Join-Path $game 'ReShade64.dll')).Hash -eq $reshadeHash) 'proxy change preserves bundled ReShade bytes'
  }else{
   Assert-X86 ($result.exit -ne 0 -and $result.log.Contains('managed ReShade proxy move is active')) 'older restore-chain rename evidence refuses proxy change'
   Assert-X86 ((Get-FileHash $statePath).Hash -eq $stateHash -and (Get-FileHash (Join-Path $game 'ReShade64.dll')).Hash -eq $reshadeHash -and -not (Test-Path (Join-Path $game 'winmm.dll'))) 'refused genuine chain leaves state and proxy bytes intact'
   $result=Invoke-X86Engine $package $game @('-Uninstall','-UninstallMode','KeepSettings','-ConfirmUninstall')
   Assert-X86 ($result.exit -eq 0) ('chained bundled package uninstalls transactionally: '+$result.log)
   Assert-X86 ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $originalProxyHash -and (Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $originalIniHash -and -not (Test-Path (Join-Path $game 'ReShade64.dll'))) 'uninstall returns original user proxy and UTF16 config exactly'
  }
 }
}finally{
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/universal-connections/x86-proxy-ownership'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$priorFixtureRoot
}
