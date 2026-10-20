param([Parameter(Mandatory=$true)][string]$Bundle,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
. (Join-Path $Bundle 'support/NRAdvanced-Harness.ps1')
$work=Join-Path (Split-Path $PSScriptRoot) 'builds/nr-advanced-harness-fixture'
$dir=Join-Path $work 'cmd path with spaces'
if(Test-Path -LiteralPath $dir){throw 'CMD fixture must be absent.'}
New-Item -ItemType Directory -Path $dir | Out-Null
foreach($name in @('InertGame.exe','dxgi.dll','nvngx_dlssnr.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini')){
 Copy-Item -LiteralPath (Join-Path $work ('roundtrip/'+$name)) -Destination (Join-Path $dir $name)
}
$exe=Join-Path $dir 'InertGame.exe'
$original=Get-NraHash (Join-Path $dir 'dxgi.dll');$settings=Get-NraHash (Join-Path $dir 'OptiScaler.ini')
function RunEntry([string]$cmd,[string]$inputText,[string]$log){
 $info=New-Object Diagnostics.ProcessStartInfo
 $info.FileName=$env:ComSpec
 $info.Arguments='/d /s /c ""'+$cmd+'" -GameExecutable "'+$exe+'""'
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardInput=$info.RedirectStandardOutput=$info.RedirectStandardError=$true
 $process=New-Object Diagnostics.Process;$process.StartInfo=$info
 [void]$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 $process.StandardInput.Write($inputText);$process.StandardInput.Close();$process.WaitForExit()
 ($stdout.Result+$stderr.Result) | Set-Content -LiteralPath (Join-Path $EvidenceRoot $log)
 if($process.ExitCode -ne 0){throw "CMD entry point failed: $cmd"}
 $process.Dispose()
}
RunEntry (Join-Path $Bundle 'Setup.cmd') "1`r`n`r`n" 'setup-cmd.log'
if((Get-NraHash (Join-Path $dir 'dxgi.dll')) -cne (Get-NraHash (Join-Path $Bundle 'payload/OptiScaler.dll'))){throw 'CMD Setup did not install the pinned runtime.'}
$records=@(Get-NraRecords (Get-NraGame $exe));if($records.Count -ne 1){throw 'Expected one active backup.'}
RunEntry (Join-Path (Split-Path $records[0].path) 'Restore.cmd') "`r`n" 'restore-cmd.log'
if((Get-NraHash (Join-Path $dir 'dxgi.dll')) -cne $original){throw 'CMD Restore did not restore original runtime.'}
if((Get-NraHash (Join-Path $dir 'OptiScaler.ini')) -cne $settings){throw 'CMD entry points changed settings.'}
@{setupCmd='passed';standaloneRestoreCmd='passed';pathWithSpaces='passed';gameExecuted=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'entry-points.json')
Write-Output 'PASS: Setup.cmd and standalone Restore.cmd with spaces; exact install/restore and settings preservation; no game executed.'
