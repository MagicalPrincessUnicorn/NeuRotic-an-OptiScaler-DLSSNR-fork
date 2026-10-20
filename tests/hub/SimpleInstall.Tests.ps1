. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
function Test-SimpleNrPreservedUpdate($value){
 return ($value.status -ceq 'SucceededWithNotes' -and @($value.errors).Count -eq 0 -and @($value.notes).Count -eq 1 -and @($value.notes|Where-Object{$_.reasonCode -ceq 'desktop.nrdll.update_preserved'}).Count -eq 1)
}
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 $package=New-SimplePackageFixture;$game=New-SimpleGameFixture 'install';$exe=Join-Path $game 'FixtureGame.exe'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null
 if($result.status -ne 'Succeeded'){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ($result.status -eq 'Succeeded' -and (Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))) 'base install succeeds without model deposit'
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'manually edited owned proxy')
 Remove-Item -LiteralPath (Join-Path $game 'dependency.dll')
 $ini="; explicit user preference`r`n[DlssNr]`r`nEnabled=true`r`n";[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),$ini)
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null
 if($result.status -ne 'Succeeded'){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ((Test-SimpleNrPreservedUpdate $result) -and (Get-Item -LiteralPath (Join-Path $game 'dxgi.dll')).Length -eq 512 -and (Test-Path -LiteralPath (Join-Path $game 'dependency.dll'))) 'EditedOwnedFileReplaced and missing owned file recreated'
 Assert-Simple (([IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))) -ceq $ini) 'NormalIniPreserved'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $true @() '' $null $null
 Assert-Simple (([IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))) -match 'Enabled=false' -and (Test-SimpleNrPreservedUpdate $result)) 'FreshIniResetsAndAppliesProfile'
 $foreign=New-SimpleGameFixture 'foreign';$foreignExe=Join-Path $foreign 'FixtureGame.exe';[IO.File]::WriteAllText((Join-Path $foreign 'dxgi.dll'),'foreign mod')
 $result=Invoke-NeuroticSimpleInstall $foreignExe $package 'dxgi.dll' $false @([pscustomobject]@{path='dxgi.dll';action='Cancel'}) '' $null $null
 Assert-Simple ($result.status -eq 'Cancelled' -and -not(Test-Path -LiteralPath (Join-Path $foreign 'dependency.dll')) -and -not(Test-Path -LiteralPath (Join-Path $foreign 'NeuRotic'))) 'ForeignCancel makes no writes'
 $result=Invoke-NeuroticSimpleInstall $foreignExe $package 'dxgi.dll' $false @([pscustomobject]@{path='dxgi.dll';action='Skip'}) '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and $result.receipt.status -eq 'Partial' -and [IO.File]::ReadAllText((Join-Path $foreign 'dxgi.dll')) -eq 'foreign mod' -and (Test-Path -LiteralPath (Join-Path $foreign 'dependency.dll'))) 'RequiredSkipDoesNotCancelOtherCopies or claim foreign ownership'
 Assert-Simple (-not @($result.receipt.files|Where-Object{$_.path -eq 'dxgi.dll'}).Count) 'skipped foreign file remains unowned'
 $result=Invoke-NeuroticSimpleInstall $foreignExe $package 'dxgi.dll' $false @([pscustomobject]@{path='dxgi.dll';action='Replace'}) '' $null $null
 Assert-Simple ((Test-SimpleNrPreservedUpdate $result) -and @($result.receipt.files|Where-Object{$_.path -eq 'dxgi.dll'}).Count -eq 1) 'ForeignReplace records explicit ownership'
 $broken=New-SimpleGameFixture 'partial';$brokenExe=Join-Path $broken 'FixtureGame.exe'
 $script:SimpleFaultOperation='Copy';$script:SimpleFaultPath='dependency.dll'
 $result=Invoke-NeuroticSimpleInstall $brokenExe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'Partial' -and $result.errors[0].path -eq 'dependency.dll' -and (Read-NeuroticInstallReceipt $broken).receipt.status -eq 'Partial') 'CopyFailureIsRemovableAndRetryable'
 $script:SimpleFaultOperation='';$script:SimpleFaultPath=''
 $result=Invoke-NeuroticSimpleInstall $brokenExe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple (Test-SimpleNrPreservedUpdate $result) 'partial installation retries without recovery prerequisite'
 $script:SimpleFaultOperation='Copy';$script:SimpleFaultPath='winmm.dll'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'winmm.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'Partial' -and (Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))) 'failed proxy transition preserves old loader'
 $script:SimpleFaultOperation='';$script:SimpleFaultPath=''
 $result=Invoke-NeuroticSimpleInstall $exe $package 'winmm.dll' $false @() '' $null $null
 Assert-Simple ((Test-SimpleNrPreservedUpdate $result) -and -not(Test-Path -LiteralPath (Join-Path $game 'dxgi.dll')) -and (Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'successful proxy transition removes only previous owned loader'
 $x86=New-SimpleGameFixture 'x86';Write-SimplePe (Join-Path $x86 'FixtureGame.exe') 32
 $result=Invoke-NeuroticSimpleInstall (Join-Path $x86 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.installation.architecture_warning' -and $_.reason -match 'wrapper or custom setup'}).Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $x86 'dxgi.dll'))) 'user-chosen x86 installs with localized architecture warning'
 Assert-Simple ($result.receipt.architecture -eq 'x86' -and [IO.File]::ReadAllText((Join-Path $x86 'foreign-mod.txt')) -eq 'foreign bytes') 'custom setup retains actual target architecture and unrelated files'
 $customIni="; custom preferences`r`n[DlssNr]`r`nEnabled=true`r`n";[IO.File]::WriteAllText((Join-Path $x86 'OptiScaler.ini'),$customIni)
 $result=Invoke-NeuroticSimpleInstall (Join-Path $x86 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.nrdll.update_preserved'}).Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $x86 'OptiScaler.ini')) -ceq $customIni) 'custom architecture normal update preserves exact INI'
 $result=Invoke-NeuroticSimpleInstall (Join-Path $x86 'FixtureGame.exe') $package 'dxgi.dll' $true @() '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.nrdll.update_preserved'}).Count -eq 1 -and [IO.File]::ReadAllText((Join-Path $x86 'OptiScaler.ini')) -match 'Enabled=false') 'custom architecture Fresh Install retains safe NR-off default'
 $result=Invoke-NeuroticSimpleUninstall $x86
 Assert-Simple ($result.status -in @('Succeeded','SucceededWithNotes') -and -not(Test-Path -LiteralPath (Join-Path $x86 'dxgi.dll')) -and [IO.File]::ReadAllText((Join-Path $x86 'foreign-mod.txt')) -eq 'foreign bytes') 'custom architecture uninstall removes only owned payload'
 $arm=New-SimpleGameFixture 'arm64';$armExe=Join-Path $arm 'FixtureGame.exe';$armBytes=[IO.File]::ReadAllBytes($armExe);[BitConverter]::GetBytes([uint16]0xaa64).CopyTo($armBytes,68);[IO.File]::WriteAllBytes($armExe,$armBytes)
 $result=Invoke-NeuroticSimpleInstall $armExe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and $result.receipt.architecture -eq 'pe64-machine-aa64' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.installation.other_architecture_warning' -and $_.reason -match 'wrapper or custom setup'}).Count -eq 1) 'well-formed other machine identity installs with localized warning without execution whitelist'
 [IO.File]::WriteAllText((Join-Path $arm 'OptiScaler.ini'),$customIni)
 $result=Invoke-NeuroticSimpleInstall $armExe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.nrdll.update_preserved'}).Count -eq 1 -and $result.receipt.architecture -ceq 'pe64-machine-aa64' -and [IO.File]::ReadAllText((Join-Path $arm 'OptiScaler.ini')) -ceq $customIni) 'observed machine identity survives actual normal update with exact INI'
 $result=Invoke-NeuroticSimpleUninstall $arm
 Assert-Simple ($result.status -in @('Succeeded','SucceededWithNotes') -and -not(Test-Path -LiteralPath (Join-Path $arm 'dxgi.dll')) -and [IO.File]::ReadAllText((Join-Path $arm 'foreign-mod.txt')) -ceq 'foreign bytes') 'observed machine identity supports owned uninstall and preserves unrelated bytes'
 $badMachine=New-SimpleGameFixture 'bad-machine';$badExe=Join-Path $badMachine 'FixtureGame.exe';Write-SimplePe $badExe 32;$badBytes=[IO.File]::ReadAllBytes($badExe);[BitConverter]::GetBytes([uint16]0x8664).CopyTo($badBytes,68);[IO.File]::WriteAllBytes($badExe,$badBytes)
 $result=Invoke-NeuroticSimpleInstall $badExe $package 'dxgi.dll' $false @() '' $null $null
 Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and -not(Test-Path -LiteralPath (Join-Path $badMachine 'dxgi.dll')) -and [IO.File]::ReadAllText((Join-Path $badMachine 'foreign-mod.txt')) -eq 'foreign bytes') 'contradictory known machine and PE header refuses before writes'
 $deposit=Join-Path $script:SimpleWork 'central-model.dll';[IO.File]::WriteAllText($deposit,'accepted model fixture')
 $components=[pscustomobject]@{neuralModel=[pscustomobject]@{status='Present';path=$deposit;bytes=(Get-Item -LiteralPath $deposit).Length;lastWriteTicks=(Get-Item -LiteralPath $deposit).LastWriteTimeUtc.Ticks;fileIdentity=[NeuRoticSimpleFileIo]::Stat($deposit)}}
 $deposited=New-SimpleGameFixture 'deposited'
 $result=Invoke-NeuroticSimpleInstall (Join-Path $deposited 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $components $null
 Assert-Simple ($result.status -eq 'Succeeded' -and $result.copied -contains 'nvngx_dlssnr.dll') 'DepositedDependenciesCopied from accepted source stat'
 [IO.File]::WriteAllText($deposit,'changed deposit')
 $changed=New-SimpleGameFixture 'changed-deposit'
 $result=Invoke-NeuroticSimpleInstall (Join-Path $changed 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $components $null
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and -not(Test-Path -LiteralPath (Join-Path $changed 'nvngx_dlssnr.dll')) -and ($result.notes -join ' ') -match 'Refresh runtime files') 'changed deposit is skipped without blocking base or reverifying model'
 [IO.File]::WriteAllText($deposit,'accepted model fixture');$info=Get-Item -LiteralPath $deposit;$components.neuralModel.bytes=$info.Length;$components.neuralModel.lastWriteTicks=$info.LastWriteTimeUtc.Ticks;$components.neuralModel.fileIdentity=[NeuRoticSimpleFileIo]::Stat($deposit)
 $beforeCustomNr=[Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $deposited 'nvngx_dlssnr.dll')));$customOptiIni="[Libraries]`r`nOptiDllPath=Z:\user-custom\path`r`n"
 [IO.File]::WriteAllText((Join-Path $deposited 'OptiScaler.ini'),$customOptiIni)
 $result=Invoke-NeuroticSimpleInstall (Join-Path $deposited 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $components $null
 Assert-Simple ((Test-SimpleNrPreservedUpdate $result) -and $result.copied -notcontains 'nvngx_dlssnr.dll' -and [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $deposited 'nvngx_dlssnr.dll'))) -ceq $beforeCustomNr -and [IO.File]::ReadAllText((Join-Path $deposited 'OptiScaler.ini')) -ceq $customOptiIni) 'custom OptiDllPath ordinary update preserves exact installed NR bytes and INI without redirecting provider writes'
 $linkGame=New-SimpleGameFixture 'hard-link';$outside=Join-Path $script:SimpleWork 'outside.dll';[IO.File]::WriteAllText($outside,'foreign outside bytes')
 New-Item -ItemType HardLink -Path (Join-Path $linkGame 'dxgi.dll') -Target $outside|Out-Null
 $result=Invoke-NeuroticSimpleInstall (Join-Path $linkGame 'FixtureGame.exe') $package 'dxgi.dll' $false @([pscustomobject]@{path='dxgi.dll';action='Replace'}) '' $null $null
 Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and [IO.File]::ReadAllText($outside) -eq 'foreign outside bytes') 'hard-linked destination cannot modify outside game'
 Assert-Simple (-not @(Get-ChildItem -LiteralPath $script:SimpleWork -Directory -Recurse|Where-Object{$_.Name -match 'backup|preserved'}).Count) 'InstallCreatesNoBackups'
}finally{End-SimpleFixtures}
