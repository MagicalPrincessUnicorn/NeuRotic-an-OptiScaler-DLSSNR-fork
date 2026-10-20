$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/character-inspector/Start-MfgGpuCapture.ps1')
function Check($value,$message) { if(-not $value){throw $message} }
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$fixture=Join-Path $repo 'builds/inspector-motion/gpu-capture-fixture'
Check (-not (Test-Path -LiteralPath $fixture)) 'Fixture is in use'
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $session='0123456789abcdef0123456789abcdef'
    $launch=@{client=(Join-Path $PSHOME 'powershell.exe');route='Direct';app_id=$null}
    $parent=$env:NEUROTIC_DRED
    $start=New-MfgGpuStartInfo $launch $session $fixture
    Check (-not $start.UseShellExecute) 'Environment requires explicit child launch'
    $child='$ProgressPreference="SilentlyContinue"; [ordered]@{dred=$env:NEUROTIC_DRED;fg=$env:NEUROTIC_FG_LIFECYCLE;session=$env:NEUROTIC_FRAME_TRACE_SESSION;trigger=$env:NEUROTIC_FRAME_TRACE_TRIGGER;directory=$env:NEUROTIC_DIAGNOSTIC_DIRECTORY} | ConvertTo-Json'
    $start.Arguments='-NoProfile -NonInteractive -EncodedCommand '+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($child))
    $start.RedirectStandardOutput=$true
    $p=[Diagnostics.Process]::new(); $p.StartInfo=$start
    try {
        Check ($p.Start()) 'Child must start'
        $value=$p.StandardOutput.ReadToEnd() | ConvertFrom-Json
        $p.WaitForExit()
        Check ($p.ExitCode -eq 0) 'Child failed'
        Check ($value.dred -eq '1' -and $value.fg -eq '1' -and $value.session -ceq $session -and $value.trigger -eq 'nr-enable' -and $value.directory -eq $fixture) 'Child must receive all capture settings'
        Check ($env:NEUROTIC_DRED -ceq $parent) 'Parent environment changed'
    } finally {$p.Dispose()}
    $status=Get-MfgGpuCaptureStatus $fixture $session 123
    Check (-not $status.ready -and $status.dred_report -eq 'ABSENT') 'Missing evidence must be inconclusive'
    $journal=Join-Path $fixture 'FG-LIFECYCLE.log'
    $prefix="NR_FG_LIFECYCLE v=1 session=$session pid=123 tid=9 qpc=1 frequency=10 seq=1 generation=0 instance=0 active=0 kind="
    [IO.File]::WriteAllText($journal,($prefix+"dred-config configured=true result=0`n"+$prefix+"dred-device configured=true available=true`n"))
    Check (-not (Get-MfgGpuCaptureStatus $fixture $session 123).ready) 'Lifecycle alone cannot prove frame logging'
    [IO.File]::WriteAllText((Join-Path $fixture 'OptiScaler.log'),"NR_FRAME_TRACE_CONTROL v=1 session=$session pid=124 qpc=1 frequency=1 state=waiting-for-nr-enable")
    Check (-not (Get-MfgGpuCaptureStatus $fixture $session 123).ready) 'Stale frame log cannot count'
    [IO.File]::WriteAllText((Join-Path $fixture 'OptiScaler.log'),"NR_FRAME_TRACE_CONTROL v=1 session=$session pid=123 qpc=1 frequency=1 state=waiting-for-nr-enable")
    Check ((Get-MfgGpuCaptureStatus $fixture $session 123).ready) 'Matching device and configuration should be ready'
    Check (-not (Get-MfgGpuCaptureStatus $fixture $session 124).ready) 'Wrong PID must not count'
    Check (-not (Get-MfgGpuCaptureStatus $fixture ('a'*32) 123).ready) 'Stale session must not count'
    [IO.File]::WriteAllText((Join-Path $fixture 'DRED.log'),($prefix+"fault reason=2289696774`n"))
    Check ((Get-MfgGpuCaptureStatus $fixture $session 123).dred_report -eq 'PRESENT_PARTIAL') 'Partial report must not count as complete'
    [IO.File]::AppendAllText((Join-Path $fixture 'DRED.log'),($prefix+"report-end complete=true localization=approximate`n"))
    Check ((Get-MfgGpuCaptureStatus $fixture $session 123).dred_report -eq 'PRESENT_COMPLETE') 'Complete matching report should count'
    Check ((Get-MfgGpuCaptureStatus $fixture $session 124).dred_report -eq 'UNRELATED') 'Unrelated report must not count'
    [IO.File]::WriteAllText($journal,($prefix+"dred-config configured=false result=1`n"+$prefix+"dred-device configured=false available=true`n"))
    Check (-not (Get-MfgGpuCaptureStatus $fixture $session 123).ready) 'Unavailable configuration must not count'
    $archive=$fixture+'.zip'
    Export-MfgGpuCapture $fixture $archive
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {Check ($null -ne $zip.GetEntry('DRED.log')) 'GPU evidence missing from ZIP'} finally {$zip.Dispose()}
    Remove-Item -LiteralPath $archive
    Write-Output 'PASS: real child environment, parent preservation, missing/stale/wrong-PID/unavailable/partial/complete evidence, ZIP retention'

    # Execute the whole launcher against an inert CPU-only child in this owned fixture.
    $hostDirectory=Join-Path $fixture 'host'
    $outDirectory=Join-Path $fixture 'output'
    New-Item -ItemType Directory -Path $hostDirectory,$outDirectory | Out-Null
    $hostExe=Join-Path $hostDirectory 'CrimsonDesert.exe'
    $source=@'
using System; using System.IO; using System.Diagnostics; using System.Threading;
public class GpuCaptureFixture {
 public static int Main() {
  string dir=Environment.GetEnvironmentVariable("NEUROTIC_DIAGNOSTIC_DIRECTORY");
  string session=Environment.GetEnvironmentVariable("NEUROTIC_FRAME_TRACE_SESSION");
  string prefix="NR_FG_LIFECYCLE v=1 session="+session+" pid="+Process.GetCurrentProcess().Id+" tid=1 qpc=1 frequency=1 seq=1 generation=0 instance=0 active=0 kind=";
  File.WriteAllText(Path.Combine(dir,"FG-LIFECYCLE.log"),prefix+"dred-config configured=true result=0\n"+prefix+"dred-device configured=true available=true\n");
  File.WriteAllText(Path.Combine(dir,"DRED.log"),prefix+"report-end complete=true localization=approximate\n");
  File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"OptiScaler.log"),"NR_FRAME_TRACE_CONTROL v=1 session="+session+" pid="+Process.GetCurrentProcess().Id+" qpc=1 frequency=1 state=waiting-for-nr-enable");
  Thread.Sleep(2500); return 17;
 }
}
'@
    Add-Type -TypeDefinition $source -OutputAssembly $hostExe -OutputType ConsoleApplication
    [IO.File]::WriteAllText((Join-Path $hostDirectory 'OptiScaler.ini'),'settings sentinel')
    [IO.File]::WriteAllText((Join-Path $hostDirectory 'dxgi.dll'),'inert sentinel - never loaded')
    function Get-FileHash {
        param([string]$LiteralPath,[string]$Algorithm)
        if($LiteralPath -eq (Join-Path $hostDirectory 'dxgi.dll')){return [pscustomobject]@{Hash='93d3bfab7c7d064399e010d2397e9b527a48d57ed5e1e81cb6c932de33a6e517'}}
        Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
    }
    & (Join-Path $repo 'tools/character-inspector/Start-MfgGpuCapture.ps1') -GameExecutable $hostExe -OutputDirectory $outDirectory
    $archives=@(Get-ChildItem -LiteralPath $outDirectory -Filter '*.zip' -File)
    Check ($archives.Count -eq 1) 'Full launcher must export one archive'
    $zip=[IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
    try {
        $reader=[IO.StreamReader]::new($zip.GetEntry('SESSION.json').Open())
        try {$record=$reader.ReadToEnd() | ConvertFrom-Json} finally {$reader.Dispose()}
        Check ($record.status -eq 'EXITED' -and $record.exit_code -eq 17 -and $record.capture.ready) 'Full launcher must preserve actual exit and verify child evidence'
        Check ($null -ne $zip.GetEntry('OptiScaler.log') -and $null -ne $zip.GetEntry('startup-OptiScaler.ini')) 'Full launcher must retain evidence'
        Check ($null -eq $zip.GetEntry('dxgi.dll')) 'Installed binary must not be exported'
    } finally {$zip.Dispose()}
    Check ([IO.File]::ReadAllText((Join-Path $hostDirectory 'OptiScaler.ini')) -ceq 'settings sentinel') 'Launcher changed saved settings'
    Write-Output 'PASS: full launcher on inert CPU child, readiness, nonzero exit, evidence ZIP, settings and binary exclusion'
} finally {
    Check ($fixture.StartsWith($repo+'\builds\',[StringComparison]::OrdinalIgnoreCase)) 'Fixture escaped checkout'
    $items=@(Get-ChildItem -LiteralPath $fixture -Recurse -Force)
    Check (-not ($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint})) 'Refuse fixture junction cleanup'
    Remove-Item -LiteralPath $fixture -Recurse
}
