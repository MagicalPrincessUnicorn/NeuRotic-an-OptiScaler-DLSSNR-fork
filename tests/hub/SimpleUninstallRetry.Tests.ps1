. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
$script:RetryFailures=0
function Check-Retry($Condition,[string]$Message){if($Condition){Write-Output ('PASS '+$Message)}else{$script:RetryFailures++;Write-Output ('FAIL '+$Message)}}
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 $package=New-SimplePackageFixture
 $loader=Join-Path $script:SimpleRepo 'builds/hub/simple-loader/OptiScaler.dll'
 Copy-Item -LiteralPath $loader -Destination (Join-Path $package.root 'payload/OptiScaler.dll') -Force
 foreach($scenario in @('payload','helper','receipt','rename')){
  $game=New-SimpleGameFixture ('retry-'+$scenario);$action=''
  if($scenario -eq 'rename'){[IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'User ReShade');$action='RenameReShade'}
  $installed=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() $action $null $null
  Assert-Simple ($installed.status -eq 'Succeeded') ('Retry fixture installed: '+$scenario)
  $handle=$null
  if($scenario -eq 'rename'){[IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'Another mod')}
  else{$locked=$(if($scenario -eq 'payload'){'dependency.dll'}elseif($scenario -eq 'receipt'){'NeuRotic/Installer/Current-Install.json'}else{'NeuRotic/Installer/NeuRotic-InstallReceipt.ps1'});$handle=[IO.File]::Open((Join-Path $game $locked),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)}
  try{$result=Invoke-NeuroticSimpleUninstall $game}finally{if($handle){$handle.Dispose()}}
  Check-Retry ($result.status -eq $(if($scenario -eq 'rename'){'SucceededWithNotes'}else{'Partial'})) ('Expected incomplete uninstall: '+$scenario)
  $missing=@($installed.receipt.files|Where-Object{$_.role -eq 'installer' -and -not(Test-Path -LiteralPath (Join-Path $game $_.path))})
  Check-Retry ($missing.Count -eq 0) ('Complete game-local retry tool survives '+$scenario+' failure')
  Check-Retry ([IO.File]::ReadAllText((Join-Path $game 'foreign-mod.txt')) -eq 'foreign bytes') ('Unrelated mod retained: '+$scenario)
  if($scenario -eq 'rename'){Remove-Item -LiteralPath (Join-Path $game 'dxgi.dll')}
  if($missing.Count -eq 0){
   $powerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
   $env:PSModulePath=Join-Path (Split-Path -Parent $powerShell) 'Modules'
   & $powerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $game 'NeuRotic/Installer/NeuRotic-Uninstall.ps1') *> (Join-Path $script:SimpleWork ($scenario+'-retry.log'))
   Check-Retry ($LASTEXITCODE -eq 0 -and -not(Test-Path -LiteralPath (Join-Path $game 'Uninstall NeuRotic.cmd')) -and -not(Test-Path -LiteralPath (Join-Path $game 'NeuRotic/Installer/Current-Install.json'))) ('Independent new-process retry completes and removes its tools: '+$scenario)
  }
 }
 if($script:RetryFailures){throw ($script:RetryFailures.ToString()+' independent uninstall retry assertions failed')}
}finally{End-SimpleFixtures}
