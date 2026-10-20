[CmdletBinding()]
param([string]$GameExecutable,[string]$OutputDirectory=$PSScriptRoot,[switch]$CheckOnly)

function SafeLocal([string]$path) {
    $full=[IO.Path]::GetFullPath($path)
    if($full -notmatch '^[A-Za-z]:\\'){throw 'Extract the capture tool to a local drive first.'}
    $walk=$full
    while($walk){
        if((Test-Path -LiteralPath $walk) -and ((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw "Linked paths are not supported: $walk"
        }
        $walk=Split-Path -Parent $walk
    }
    return $full
}
function Hash([string]$path) {return (Get-FileHash -LiteralPath (SafeLocal $path) -Algorithm SHA256).Hash.ToLowerInvariant()}
function New-MfgGpuStartInfo($launch,[string]$session,[string]$directory) {
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$launch.client
    $start.WorkingDirectory=Split-Path -Parent $launch.client
    $start.UseShellExecute=$false
    if($launch.route -eq 'Steam'){$start.Arguments='-applaunch '+$launch.app_id}
    $start.EnvironmentVariables['NEUROTIC_FG_LIFECYCLE']='1'
    $start.EnvironmentVariables['NEUROTIC_DRED']='1'
    $start.EnvironmentVariables['NEUROTIC_DIAGNOSTIC_DIRECTORY']=$directory
    $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_SESSION']=$session
    $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_TRIGGER']='nr-enable'
    $start.EnvironmentVariables.Remove('NEUROTIC_FRAME_TRACE_PROFILE')
    return $start
}
function Get-MfgGpuCaptureStatus([string]$directory,[string]$session,[long]$processId,[string]$gameLog='') {
    $matched='NR_FG_LIFECYCLE v=1 session='+[regex]::Escape($session)+' pid='+$processId+' '
    $configured=$false; $device=$false; $report='ABSENT'; $trace=$false
    foreach($name in @('FG-LIFECYCLE.log','DRED.log','OptiScaler.log')){
        $path=Join-Path $directory $name
        if($name -eq 'OptiScaler.log' -and $gameLog){$path=SafeLocal $gameLog}
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){continue}
        $stream=$null; $reader=$null
        try {
            $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            if($stream.Length -gt 32MB){if($name -eq 'DRED.log'){$report='OVERSIZE'};continue}
            $reader=[IO.StreamReader]::new($stream)
            if($name -eq 'DRED.log'){$report='UNRELATED'}
            while(($line=$reader.ReadLine()) -ne $null){
                if($name -eq 'OptiScaler.log'){
                    if($line -match ('NR_FRAME_TRACE(?:_CONTROL)? v=1 session='+[regex]::Escape($session)+' pid='+$processId+' ')){$trace=$true;break}
                    continue
                }
                if($line -notmatch $matched){continue}
                if($name -eq 'FG-LIFECYCLE.log'){
                    if($line -match ' kind=dred-config ' -and $line -match '\bconfigured=true\b'){$configured=$true}
                    if($line -match ' kind=dred-device ' -and $line -match '\bconfigured=true\b' -and $line -match '\bavailable=true\b'){$device=$true}
                } else {
                    if($report -ne 'PRESENT_COMPLETE'){$report='PRESENT_PARTIAL'}
                    if($line -match ' kind=report-end complete=true\b'){$report='PRESENT_COMPLETE'}
                }
            }
        } catch [IO.IOException] {if($name -eq 'DRED.log'){$report='READ_INCOMPLETE'}}
        finally {if($reader){$reader.Dispose()}elseif($stream){$stream.Dispose()}}
    }
    return [pscustomobject]@{ready=($configured -and $device -and $trace);configured=$configured;device_observed=$device;frame_log_session_matched=$trace;dred_report=$report}
}
function Export-MfgGpuCapture([string]$directory,[string]$archive) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # Only explicit diagnostic files are exported. Never include installed DLLs/models or dumps.
    $zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach($name in @('SESSION.json','FG-LIFECYCLE.log','DRED.log','OptiScaler.log','startup-OptiScaler.ini','after-OptiScaler.ini','NeuRotic-NR-admission.json')){
            $path=SafeLocal (Join-Path $directory $name)
            if(Test-Path -LiteralPath $path -PathType Leaf){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$path,$name) | Out-Null}
        }
    } finally {$zip.Dispose()}
}
if($MyInvocation.InvocationName -eq '.') {return}
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'Candidate-Launch.ps1'
if(-not (Test-Path -LiteralPath $helper)){$helper=Join-Path $PSScriptRoot '../packaging/customer/support/Candidate-Launch.ps1'}
. $helper
if(-not $GameExecutable){
    Add-Type -AssemblyName System.Windows.Forms
    $picker=[Windows.Forms.OpenFileDialog]::new()
    try {
        $picker.Title='Select the installed CrimsonDesert.exe'
        $picker.Filter='Crimson Desert|CrimsonDesert.exe'
        if($picker.ShowDialog() -ne 'OK'){throw 'No game selected; nothing launched.'}
        $GameExecutable=$picker.FileName
    } finally {$picker.Dispose()}
}
$GameExecutable=SafeLocal $GameExecutable
if([IO.Path]::GetFileName($GameExecutable) -ine 'CrimsonDesert.exe' -or -not (Test-Path -LiteralPath $GameExecutable -PathType Leaf)){throw 'Select the installed CrimsonDesert.exe.'}
if(Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue){throw 'Close Crimson Desert before this capture.'}
$game=Split-Path -Parent $GameExecutable
$dllHash=Hash (Join-Path $game 'dxgi.dll')
if($dllHash -ne '93d3bfab7c7d064399e010d2397e9b527a48d57ed5e1e81cb6c932de33a6e517'){throw 'This capture expects the installed 11dad02e MFG real-frame DLL. No files were changed.'}
$launch=GetCandidateLaunch $GameExecutable 'Auto' '' ''
if($CheckOnly){Write-Host 'PASS: selected game, installed DLL and launch route verified. Nothing launched or changed.';return}
$root=SafeLocal (Resolve-Path -LiteralPath $OutputDirectory).Path
$session=[guid]::NewGuid().ToString('N')
$run=Join-Path $root ('NeuRotic-GPU-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$session.Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$record=[ordered]@{session=$session;product='11dad02e';dll_sha256=$dllHash;game_executable=$GameExecutable;
    launch_route=$launch;started_utc=[DateTime]::UtcNow.ToString('o');finished_utc=$null;process_id=$null;exit_code=$null;
    status='PREPARED';capture=$null;error=$null;notes=@();settings_written_by_tool=$false;
    limitation='DRED can change timing. A complete report is not proof of root cause or GPU health. Missing evidence is inconclusive.'}
function SaveSession {[IO.File]::WriteAllText((Join-Path $run 'SESSION.json'),($record | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))}
function Retain([string]$sourceName,[string]$destinationName) {
    $path=SafeLocal (Join-Path $game $sourceName)
    if(Test-Path -LiteralPath $path -PathType Leaf){
        if((Get-Item -LiteralPath $path).Length -gt 128MB){throw "Capture bound exceeded: $sourceName"}
        Copy-Item -LiteralPath $path -Destination (Join-Path $run $destinationName)
    }
}
Retain 'OptiScaler.ini' 'startup-OptiScaler.ini'
SaveSession
$launcher=[Diagnostics.Process]::new(); $launcher.StartInfo=New-MfgGpuStartInfo $launch $session $run
$process=$null
try {
    Write-Host 'Launching with GPU tracing. Begin with NR OFF; keep your 6x MFG and Inspector settings.'
    if(-not $launcher.Start()){throw 'Launch failed.'}
    if($launch.route -eq 'Steam'){$process=WaitCandidateGame $GameExecutable ([datetime]$record.started_utc) 300}else{$process=$launcher}
    $record.process_id=$process.Id; $record.status='RUNNING'; SaveSession
    $readyShown=$false; $unavailableShown=$false; $watch=[Diagnostics.Stopwatch]::StartNew()
    while(-not $process.WaitForExit(2000)){
        if(-not $readyShown){
            $record.capture=Get-MfgGpuCaptureStatus $run $session $process.Id (Join-Path $game 'OptiScaler.log')
            if($record.capture.ready){$readyShown=$true;SaveSession;Write-Host 'GPU TRACE READY. Test NR and your resolution changes, then close the game when finished.'}
            elseif($watch.Elapsed.TotalSeconds -gt 120 -and -not $unavailableShown){
                $unavailableShown=$true; Write-Host 'GPU trace could not be verified. Close the game and return the archive; this run is inconclusive.'
            }
        }
    }
    $record.exit_code=$process.ExitCode; $record.status='EXITED'
} catch {$record.status='INCONCLUSIVE';$record.error=$_.Exception.Message;Write-Warning $record.error}
finally {
    $record.finished_utc=[DateTime]::UtcNow.ToString('o')
    foreach($pair in @(@('OptiScaler.log','OptiScaler.log'),@('OptiScaler.ini','after-OptiScaler.ini'),@('NeuRotic-NR-admission.json','NeuRotic-NR-admission.json'))){
        try {Retain $pair[0] $pair[1]} catch {$record.notes+=@($_.Exception.Message)}
    }
    if($record.process_id){$record.capture=Get-MfgGpuCaptureStatus $run $session $record.process_id}
    if(-not $record.capture -or -not $record.capture.frame_log_session_matched){$record.notes+=@('OptiScaler.log missing, oversized or unmatched to this session. Any retained log is unverified; configured alternate logs are not collected.')}
    SaveSession
    if($process -and $process -ne $launcher){$process.Dispose()};$launcher.Dispose()
    Export-MfgGpuCapture $run ($run+'.zip')
    Write-Host ('Send this archive: '+$run+'.zip')
    Write-Host 'Fully exit Steam after this capture so subsequent launches do not inherit tracing.'
}
