. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
$loader=Join-Path $script:SimpleRepo 'builds/hub/simple-loader/OptiScaler.dll'
if(-not(Test-Path -LiteralPath $loader)){throw 'Run tests\hub\Build-SimpleLoaderFixture.cmd first'}
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 $package=New-SimplePackageFixture;Copy-Item -LiteralPath $loader -Destination (Join-Path $package.root 'payload/OptiScaler.dll') -Force
 function Install-Simple($Game,[string]$Action='') {return Invoke-NeuroticSimpleInstall (Join-Path $Game 'FixtureGame.exe') $package 'dxgi.dll' $false @() $Action $null $null}
 $game=New-SimpleGameFixture 'remove';$installed=Install-Simple $game
 [IO.File]::WriteAllText((Join-Path $game 'dependency.dll'),'edited owned bytes');Remove-Item -LiteralPath (Join-Path $game 'OptiScaler.ini')
 [IO.File]::WriteAllText((Join-Path $game 'unknown-addon.dll'),'user addon')
 $result=Invoke-NeuroticSimpleUninstall $game
 Assert-Simple ($result.status -eq 'Succeeded' -and -not(Test-Path -LiteralPath (Join-Path $game 'dependency.dll')) -and $result.removed -contains 'OptiScaler.ini') 'ModifiedOwnedFileDeleted and MissingOwnedFileAlreadyRemoved'
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $game 'unknown-addon.dll')) -eq 'user addon' -and -not(Test-Path -LiteralPath (Join-Path $game 'NeuRotic/Installer/Current-Install.json'))) 'UnknownAndUntouchedDependencyPreserved, completed receipt removed'
 $reshade=New-SimpleGameFixture 'reshade';[IO.File]::WriteAllText((Join-Path $reshade 'dxgi.dll'),'user ReShade bytes')
 [IO.File]::WriteAllText((Join-Path $reshade 'OptiScaler.ini'),"; user comment`r`n[Plugins]`r`nLoadReshade = false ; keep comment`r`n",[Text.Encoding]::Unicode)
 $result=Install-Simple $reshade 'RenameReShade'
 if($result.status -ne 'Succeeded'){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $reshade 'ReShade64.dll')) -eq 'user ReShade bytes' -and (Test-NeuroticLoader (Join-Path $reshade 'dxgi.dll'))) 'ReShadeRenameSetsIni and native loader identity'
 $bytes=[IO.File]::ReadAllBytes((Join-Path $reshade 'OptiScaler.ini'));$text=[IO.File]::ReadAllText((Join-Path $reshade 'OptiScaler.ini'))
 Assert-Simple ($bytes[0] -eq 255 -and $bytes[1] -eq 254 -and $text -match 'LoadReshade = true ; keep comment') 'explicit coexistence preserves INI encoding/comments'
 $result=Invoke-NeuroticSimpleUninstall $reshade
 Assert-Simple ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $reshade 'dxgi.dll')) -eq 'user ReShade bytes' -and -not(Test-Path -LiteralPath (Join-Path $reshade 'ReShade64.dll'))) 'RecordedReShadeReturnedToDxgi'
 $foreign=New-SimpleGameFixture 'occupied-dxgi';[IO.File]::WriteAllText((Join-Path $foreign 'dxgi.dll'),'user ReShade bytes');$result=Install-Simple $foreign 'RenameReShade'
 [IO.File]::WriteAllText((Join-Path $foreign 'dxgi.dll'),'unrecognizable foreign loader')
 $result=Invoke-NeuroticSimpleUninstall $foreign
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and [IO.File]::ReadAllText((Join-Path $foreign 'dxgi.dll')) -eq 'unrecognizable foreign loader' -and -not(Test-Path -LiteralPath (Join-Path $foreign 'dependency.dll')) -and ($result.notes -join ' ') -match 'NeuRotic removed. ReShade was left as ReShade64.dll because dxgi.dll is occupied.') 'ForeignDxgiPreservedWithNote while independent removal completes'
 Remove-Item -LiteralPath (Join-Path $foreign 'dxgi.dll');$result=Invoke-NeuroticSimpleUninstall $foreign
 if($result.status -ne 'Succeeded'){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $foreign 'dxgi.dll')) -eq 'user ReShade bytes') 'rename-only receipt allows later retry'
 $missing=New-SimpleGameFixture 'missing-reshade';[IO.File]::WriteAllText((Join-Path $missing 'dxgi.dll'),'user ReShade bytes');$result=Install-Simple $missing 'RenameReShade';Remove-Item -LiteralPath (Join-Path $missing 'ReShade64.dll')
 $result=Invoke-NeuroticSimpleUninstall $missing
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and -not(Test-Path -LiteralPath (Join-Path $missing 'dxgi.dll')) -and -not(Test-Path -LiteralPath (Join-Path $missing 'dependency.dll'))) 'MissingReShadeDoesNotStopRemoval'
 $collision=New-SimpleGameFixture 'rename-collision';[IO.File]::WriteAllText((Join-Path $collision 'dxgi.dll'),'foreign');[IO.File]::WriteAllText((Join-Path $collision 'ReShade64.dll'),'occupied')
 $result=Install-Simple $collision 'RenameReShade'
 Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and [IO.File]::ReadAllText((Join-Path $collision 'ReShade64.dll')) -eq 'occupied' -and -not(Test-Path -LiteralPath (Join-Path $collision 'dependency.dll'))) 'OccupiedReShade64NotOverwritten'
 $locked=New-SimpleGameFixture 'locked';$result=Install-Simple $locked
 $installedSupport=@($result.receipt.files|Where-Object{$_.role -eq 'installer'})
 $handle=[IO.File]::Open((Join-Path $locked 'dependency.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
 try{$result=Invoke-NeuroticSimpleUninstall $locked;Assert-Simple ($result.status -eq 'Partial' -and -not(Test-Path -LiteralPath (Join-Path $locked 'dxgi.dll')) -and @($result.receipt.files|Where-Object{$_.role -ne 'installer'}).Count -eq 1 -and @($result.receipt.files|Where-Object{$_.role -eq 'installer'}).Count -eq $installedSupport.Count -and @($installedSupport|Where-Object{-not(Test-Path -LiteralPath (Join-Path $locked $_.path))}).Count -eq 0 -and $result.errors[0].path -eq 'dependency.dll') 'LockedOwnedFileDoesNotStopOthers, unresolved payload and complete retry tool retained'}finally{$handle.Dispose()}
 $result=Invoke-NeuroticSimpleUninstall $locked
 Assert-Simple ($result.status -eq 'Succeeded' -and -not(Test-Path -LiteralPath (Join-Path $locked 'dependency.dll'))) 'UninstallRetryCompletesRemaining'
 Assert-Simple (-not @(Get-ChildItem -LiteralPath $script:SimpleWork -Directory -Recurse|Where-Object{$_.Name -match 'backup|preserved|snapshot'}).Count) 'NoUninstallSnapshots'
}finally{End-SimpleFixtures}
