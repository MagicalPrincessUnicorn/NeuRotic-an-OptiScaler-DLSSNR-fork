$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Audit-InstallerLifecycle.Tests.ps1') -Reset
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$base=Join-Path $repo 'builds/audit-installer-lifecycle';$engine=Join-Path $base 'package/support/NeuRotic-Setup-Engine.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$null,[ref]$null)
foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
$allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll');$script:RestoreRecordLedger=@{}
$index=0
foreach($stage in @('after-journal','after-proxy-removal','after-payload:dxgi.dll','after-record','after-state')){
 $root=Join-Path $base ('crash-'+$index);$index++;[void][IO.Directory]::CreateDirectory($root)
 $exe=Join-Path $root 'AuditCrashGame.exe';[IO.File]::Copy("$env:windir/System32/cmd.exe",$exe)
 [IO.File]::WriteAllText((Join-Path $root 'dxgi.dll'),'original foreign wrapper')
 $before=HashFile (Join-Path $root 'dxgi.dll')
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName="$env:windir/System32/WindowsPowerShell/v1.0/powershell.exe";$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.EnvironmentVariables['NEUROTIC_HUB_FIXTURE_ROOT']=Join-Path $base 'user'
 $start.EnvironmentVariables['NEUROTIC_INSTALL_TEST_ROOT']=$root;$start.EnvironmentVariables['NEUROTIC_INSTALL_TEST_BOUNDARY']=$stage
 $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$engine+'" -GameExecutable "'+$exe+'" -ProxyName dxgi.dll -ExistingProxyAction Replace -HubNonInteractive -ContinueWithoutRuntime -ConfirmInstall'
 $child=[Diagnostics.Process]::Start($start);try{$output=$child.StandardOutput.ReadToEndAsync();$errorOutput=$child.StandardError.ReadToEndAsync();if(-not $child.WaitForExit(30000)){$child.Kill();throw 'Fixture child timeout'};[IO.File]::WriteAllText((Join-Path $base ('crash-'+$index+'.log')),$output.Result+$errorOutput.Result)}finally{$child.Dispose()}
 if((Get-NeuroticPendingTransaction $root) -ne 'Install-Transaction.json'){throw ('Interrupted install was not discoverable: '+$stage)}
 Restore-NeuroticMutation $root 'Install'
 if((HashFile (Join-Path $root 'dxgi.dll')) -ne $before -or (Get-NeuroticPendingTransaction $root)){throw ('Recovery did not preserve original baseline: '+$stage)}
 if(Test-Path -LiteralPath (Join-Path $root 'NeuRotic/Installer/Current-Install.json')){throw 'First-install recovery left active state'}
 Write-Output ('PASS terminated child recovered original baseline at '+$stage)
}
foreach($stage in @('restore-after-payload:dxgi.dll','restore-after-state')){
 $root=Join-Path $base ('restore-crash-'+$index);$index++;[void][IO.Directory]::CreateDirectory($root)
 $exe=Join-Path $root 'AuditRestoreGame.exe';[IO.File]::Copy("$env:windir/System32/cmd.exe",$exe)
 & $engine -GameExecutable $exe -ProxyName dxgi.dll -HubNonInteractive -ContinueWithoutRuntime -ConfirmInstall *> (Join-Path $base ('restore-install-'+$index+'.log'))
 $statePath=Join-Path $root 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
 $beforeState=HashFile $statePath;$beforeProxy=HashFile (Join-Path $root 'dxgi.dll')
 $restore=Join-Path (Split-Path $state.restore_chain[-1]) 'NeuRotic-Setup-Engine.ps1'
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName="$env:windir/System32/WindowsPowerShell/v1.0/powershell.exe";$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.EnvironmentVariables['NEUROTIC_HUB_FIXTURE_ROOT']=Join-Path $base 'user';$start.EnvironmentVariables['NEUROTIC_INSTALL_TEST_ROOT']=$root;$start.EnvironmentVariables['NEUROTIC_INSTALL_TEST_BOUNDARY']=$stage
 $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$restore+'" -Restore -HubNonInteractive'
 $child=[Diagnostics.Process]::Start($start);try{$o=$child.StandardOutput.ReadToEndAsync();$e=$child.StandardError.ReadToEndAsync();if(-not $child.WaitForExit(30000)){$child.Kill();throw 'Restore fixture child timeout'};[IO.File]::WriteAllText((Join-Path $base ('restore-crash-'+$index+'.log')),$o.Result+$e.Result)}finally{$child.Dispose()}
 if((Get-NeuroticPendingTransaction $root) -ne 'Restore-Transaction.json'){throw ('Interrupted restore was not discoverable: '+$stage)}
 Restore-NeuroticMutation $root 'Restore'
 if((HashFile $statePath) -ne $beforeState -or (HashFile (Join-Path $root 'dxgi.dll')) -ne $beforeProxy -or (Get-NeuroticPendingTransaction $root)){throw ('Restore recovery did not recover the active installation: '+$stage)}
 Write-Output ('PASS terminated restore recovered active state and payload at '+$stage)
}
