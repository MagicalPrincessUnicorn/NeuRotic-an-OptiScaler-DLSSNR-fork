. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 function Test-SuccessfulInspectorUpdate($Value){
  return ($Value.status -ceq 'SucceededWithNotes' -and @($Value.errors).Count -eq 0 -and @($Value.skipped).Count -eq 0 -and $Value.receipt.status -ceq 'Installed' -and @($Value.notes).Count -eq 1 -and $Value.notes[0].reasonCode -ceq 'desktop.nrdll.update_preserved')
 }
 $package=New-SimplePackageFixture;$game=New-SimpleGameFixture 'inspector';$exe=Join-Path $game 'FixtureGame.exe'
 $version='OptiScaler/CharacterInspector/Version 1';$stage='OptiScaler/CharacterInspector/.Version 1.installing'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple ($result.status -eq 'Succeeded' -and (Test-Path -LiteralPath (Join-Path $game ($version+'/worker.py'))) -and -not(Test-Path -LiteralPath (Join-Path $game $stage))) 'AbsentVersionInstalled and complete directory published'
 [IO.File]::WriteAllText((Join-Path $game ($version+'/worker.py')),'locally edited worker')
 Remove-Item -LiteralPath (Join-Path $game ($version+'/models.lock.json'))
 $script:SimpleFaultOperation='Copy';$script:SimpleFaultPath=$stage+'/worker.py'
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $true @() '' $null $null $null $null $true
 Assert-Simple ((Test-SuccessfulInspectorUpdate $result) -and [IO.File]::ReadAllText((Join-Path $game ($version+'/worker.py'))) -eq 'locally edited worker' -and -not(Test-Path -LiteralPath (Join-Path $game ($version+'/models.lock.json')))) 'ExistingVersionSkippedWithoutHashingOrCopy and FreshAlsoReusesVersion'
 $script:SimpleFaultOperation='';$script:SimpleFaultPath=''
 $older=Join-Path $game 'OptiScaler/CharacterInspector/Version 0';New-Item -ItemType Directory -Path $older|Out-Null;[IO.File]::WriteAllText((Join-Path $older 'worker.py'),'old version')
 $result=Invoke-NeuroticSimpleInstall $exe $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $older 'worker.py')) -eq 'old version') 'OldVersionsRemainOnUpdate'
 $partial=New-SimpleGameFixture 'partial-worker';$partialExe=Join-Path $partial 'FixtureGame.exe'
 $script:SimpleFaultOperation='Copy';$script:SimpleFaultPath=$stage+'/models.lock.json'
 $result=Invoke-NeuroticSimpleInstall $partialExe $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple ($result.status -eq 'Partial' -and -not(Test-Path -LiteralPath (Join-Path $partial $version)) -and (Test-Path -LiteralPath (Join-Path $partial ($stage+'/worker.py'))) -and (Test-Path -LiteralPath (Join-Path $partial 'dxgi.dll'))) 'PartialWorkerNotPublishedAsPresent and base copies continue'
 $script:SimpleFaultOperation='';$script:SimpleFaultPath=''
 $result=Invoke-NeuroticSimpleInstall $partialExe $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple ((Test-SuccessfulInspectorUpdate $result) -and (Test-Path -LiteralPath (Join-Path $partial ($version+'/models.lock.json'))) -and -not @($result.receipt.files|Where-Object{$_.path -like '*installing*'}).Count) 'partial staging retry publishes and removes temporary ownership'
 $moved=Join-Path $script:SimpleWork 'moved-worker';Move-Item -LiteralPath $partial -Destination $moved
 $result=Invoke-NeuroticSimpleUninstall $moved
 Assert-Simple ($result.status -eq 'Succeeded' -and -not(Test-Path -LiteralPath (Join-Path $moved $version))) 'MoveGameAndAppIndependent'
 $existing=New-SimpleGameFixture 'existing-worker';New-Item -ItemType Directory -Path (Join-Path $existing $version) -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $existing ($version+'/worker.py')),'preexisting unowned worker')
 $result=Invoke-NeuroticSimpleInstall (Join-Path $existing 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple (-not @($result.receipt.files|Where-Object{$_.path -like '*CharacterInspector*'}).Count) 'reused unowned version does not become owned'
 $result=Invoke-NeuroticSimpleUninstall $existing
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $existing ($version+'/worker.py'))) -eq 'preexisting unowned worker') 'uninstall preserves reused unowned worker'
 $unknown=New-SimpleGameFixture 'foreign-staging';New-Item -ItemType Directory -Path (Join-Path $unknown $stage) -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $unknown ($stage+'/worker.py')),'foreign staged bytes')
 $result=Invoke-NeuroticSimpleInstall (Join-Path $unknown 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $null $null $true
 Assert-Simple ($result.status -eq 'Partial' -and -not @($result.receipt.files|Where-Object{$_.path -eq ($stage+'/worker.py')}).Count) 'unfamiliar staging cannot become owned or published'
 $result=Invoke-NeuroticSimpleUninstall $unknown
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $unknown ($stage+'/worker.py'))) -eq 'foreign staged bytes') 'foreign staging survives uninstall'
}finally{End-SimpleFixtures}
