. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try {
 $game=New-SimpleGameFixture 'profiles'
 # Small real PE headers; no installed game or copied binary tree.
 function Write-ProfileExe([string]$Path,[int]$Bits){
  $bytes=New-Object byte[] 512;$bytes[0]=77;$bytes[1]=90
  [BitConverter]::GetBytes([int]64).CopyTo($bytes,60)
  [BitConverter]::GetBytes([int]17744).CopyTo($bytes,64)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x8664}else{0x14c})).CopyTo($bytes,68)
  [BitConverter]::GetBytes([uint16]1).CopyTo($bytes,70)
  [BitConverter]::GetBytes([uint16]2).CopyTo($bytes,86)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){240}else{224})).CopyTo($bytes,84)
  [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x20b}else{0x10b})).CopyTo($bytes,88)
  [IO.File]::WriteAllBytes($Path,$bytes)
 }
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallProfiles.ps1')
 $exe=Join-Path $game 'FixtureGame.exe';Write-ProfileExe $exe 64
 $package=[pscustomobject]@{architecture='x64'}
 $profile=Get-NeuroticInstallProfile $exe $package ([pscustomobject]@{store='steam';storeId='1222140';title='Detroit: Become Human'})
 Assert-Simple ($profile.proxy -eq 'winmm.dll' -and $profile.route -eq 'native-x64') 'KnownVulkanProxyIsPreserved'
 $profile=Get-NeuroticInstallProfile $exe $package ([pscustomobject]@{title='not in catalog'})
 Assert-Simple (-not $profile.proxy -and $profile.architecture -eq 'x64') 'UnknownGameUsesSelectedProxy'
 $profile=Get-NeuroticInstallProfile $exe $package ([pscustomobject]@{title="Baldur's Gate 3"})
 Assert-Simple (-not $profile.proxy) 'MultiApiGameDoesNotGuess'
 Write-ProfileExe $exe 32
 $profile=Get-NeuroticInstallProfile $exe $package $null
 Assert-Simple ($profile.route -eq 'native-x64' -and $profile.architecture -eq 'x86' -and $profile.limitations[0].reasonCode -ceq 'desktop.installation.architecture_warning' -and $profile.limitations[0].reason -match 'wrapper or custom setup') 'chosen x86 profile retains selected package and localized warning identity'
 foreach($name in @('dd2.exe','monsterhunterwilds.exe','monsterhunterrise.exe')){
  $capcom=Join-Path $game $name;Write-ProfileExe $capcom 64
  $profile=Get-NeuroticInstallProfile $capcom $package $null
  Assert-Simple (@($profile.iniPatches|Where-Object{$_.key -in @('Dxgi','RestoreComputeSignature')}).Count -eq 0) "FreshCapcomKeepsGpuDependentAuto $name"
 }
 $ini="[Hooks]`r`nDxgi=auto`r`n[DlssNr]`r`nEnabled=true`r`n[Plugins]`r`nLoadReshade=true`r`n"
 $patch=[pscustomobject]@{section='Plugins';key='LoadReshade';value='false';policy='default'}
 Assert-Simple ((Update-NeuroticProfileIni $ini @($patch) $false) -eq $ini) 'NormalInstallRetainsExplicitPreferences'
 Assert-Simple ((Update-NeuroticProfileIni $ini @($patch) $true) -match 'LoadReshade=false') 'new or fresh configuration applies defaults'
 $patch.policy='routeRequired';$patch.value='true'
 Assert-Simple ((Update-NeuroticProfileIni "[Plugins]`r`nLoadReshade=false`r`n" @($patch) $false) -match 'LoadReshade=true') 'explicit coexistence route applies without resetting other settings'
}finally{End-SimpleFixtures}
