. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
$loader=Join-Path $script:SimpleRepo 'builds/hub/simple-loader/OptiScaler.dll'
if(-not(Test-Path -LiteralPath $loader)){throw 'Run tests\hub\Build-SimpleLoaderFixture.cmd first'}
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 $package=New-SimplePackageFixture
 $installDefaults=[pscustomobject]@{status='Missing';preferences=$null;note=''}
 Copy-Item -LiteralPath $loader -Destination (Join-Path $package.root 'payload/OptiScaler.dll') -Force
 $shell=(Get-Process -Id $PID).Path
 function Invoke-InstalledEntry([string]$Game,[string]$Label){
  $entry=Join-Path $Game 'NeuRotic/Installer/NeuRotic-Uninstall.ps1'
  $stdout=Join-Path $script:SimpleWork ($Label+'.stdout.txt')
  $stderr=Join-Path $script:SimpleWork ($Label+'.stderr.txt')
  # A fresh process has no canonical dot-sourced helpers or already loaded C# type.
  # Execute only the installed script; the fixture game/loader are never launched.
  $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$entry+'"'),'-GameDirectory',('"'+$Game+'"'))
  $child=Start-Process -FilePath $shell -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  try{$child.Refresh();$exitCode=$child.ExitCode;if($null -eq $exitCode){throw 'Installed entry process exit code was not captured'}}finally{$child.Dispose()}
  $out=[IO.File]::ReadAllText($stdout);$err=[IO.File]::ReadAllText($stderr)
  Write-OutputToEntryLog $Label $exitCode $out $err
  $result=$null
  if($out.Trim()){$result=$out|ConvertFrom-Json}
  return [pscustomobject]@{exitCode=$exitCode;result=$result;stderr=$err}
 }
 function Write-OutputToEntryLog($Label,$Code,$Out,$Err){
  # Host output stays separate from the function's returned operation result.
  [Console]::WriteLine(('INSTALLED_ENTRY '+$Label+' exit='+$Code+' stdout='+$Out+' stderr='+$Err))
 }
 $clean=New-SimpleGameFixture 'installed-entry-clean'
 $installed=Invoke-NeuroticSimpleInstall (Join-Path $clean 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $installDefaults
 Assert-Simple ($installed.status -eq 'Succeeded') 'standalone entry fixture installed'
 $first=Invoke-InstalledEntry $clean 'clean'
 Assert-Simple ($first.exitCode -eq 0 -and $first.result.status -eq 'Succeeded' -and -not $first.stderr.Trim()) 'installed-only fresh process initializes and uninstalls'
 Assert-Simple (-not(Test-Path -LiteralPath (Join-Path $clean 'dxgi.dll')) -and -not(Test-Path -LiteralPath (Join-Path $clean 'NeuRotic/Installer/Current-Install.json')) -and [IO.File]::ReadAllText((Join-Path $clean 'foreign-mod.txt')) -eq 'foreign bytes') 'standalone removal preserves foreign files'
 $retry=New-SimpleGameFixture 'installed-entry-retry'
 $installed=Invoke-NeuroticSimpleInstall (Join-Path $retry 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $installDefaults
 Assert-Simple ($installed.status -eq 'Succeeded') 'standalone retry fixture installed'
 $ownedSupport=@($installed.receipt.files|Where-Object{$_.role -eq 'installer'})
 $handle=[IO.File]::Open((Join-Path $retry 'dependency.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
 try{
  $partial=Invoke-InstalledEntry $retry 'locked'
  Assert-Simple ($partial.exitCode -eq 2 -and $partial.result.status -eq 'Partial' -and -not $partial.stderr.Trim()) 'installed entry reports real locked-file partial outcome'
  Assert-Simple (@($partial.result.receipt.files|Where-Object{$_.role -eq 'installer'}).Count -eq $ownedSupport.Count -and @($ownedSupport|Where-Object{-not(Test-Path -LiteralPath (Join-Path $retry $_.path))}).Count -eq 0) 'partial removal retains every installed retry dependency'
 }finally{$handle.Dispose()}
 $finished=Invoke-InstalledEntry $retry 'retry'
 Assert-Simple ($finished.exitCode -eq 0 -and $finished.result.status -eq 'Succeeded' -and -not $finished.stderr.Trim() -and -not(Test-Path -LiteralPath (Join-Path $retry 'dependency.dll'))) 'fresh installed-only retry completes after lock release'
}finally{End-SimpleFixtures}
