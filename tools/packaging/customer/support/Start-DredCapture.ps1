<#
.SYNOPSIS
Launch one diagnostic run with the correct working directory and a capture receipt.
.DESCRIPTION
Select the actual game executable. If a store client is required, fully exit it
and pass -LauncherExecutable with its path. Start the game through that client,
then exit both game and client to finish the receipt. Client-to-game environment
inheritance is not assumed. No game settings or installed files are changed.
#>
[CmdletBinding()]
param([string]$GameExecutable, [string]$GameArguments,
      [string]$OutputDirectory, [string]$LauncherExecutable, [string]$LauncherArguments,
      [switch]$FinishOnLaunchExit)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'

function ResolveCapturePath([string]$path) {
    $full=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path)
    if($full -notmatch '^[A-Za-z]:\\'){throw 'Choose a local drive path.'}
    $walk=$full
    while($walk){
        if(Test-Path -LiteralPath $walk){
            if((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Choose a path without links: $walk"}
        }
        $walk=Split-Path -Parent $walk
    }
    return $full
}
function ResolveCaptureExecutable([string]$path) {
    $full=ResolveCapturePath $path
    if(-not (Test-Path -LiteralPath $full -PathType Leaf) -or [IO.Path]::GetExtension($full) -ine '.exe'){
        throw "Select an existing executable: $full"
    }
    # Existing clients commonly reuse IPC and will not receive a new environment.
    $running=@(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($full)) -ErrorAction SilentlyContinue)
    if($running.Count){foreach($item in $running){$item.Dispose()};throw 'Fully exit the selected game or launcher, then retry. No process was stopped.'}
    return $full
}
function SaveCaptureReceipt {
    [IO.File]::WriteAllText($receiptPath,($record | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
}
function ReadCaptureJournal([string]$name) {
    $path=Join-Path $run $name
    $row=[ordered]@{name=$name;status='ABSENT';bytes=0;session_matched=$false;error=$null}
    if(Test-Path -LiteralPath $path -PathType Leaf){
        try {
            $safe=ResolveCapturePath $path
            $row.bytes=(Get-Item -LiteralPath $safe).Length
            $row.status='EMPTY'
            if($row.bytes -gt 32MB){$row.status='TOO_LARGE_TO_CORRELATE'}
            elseif($row.bytes -gt 0){
                $row.status='PRESENT_UNCORRELATED'
                # Inspect a bounded prefix only. A file's existence alone is not arming proof.
                $reader=[IO.File]::OpenText($safe)
                try {
                    $buffer=New-Object char[] 8192
                    $count=$reader.Read($buffer,0,$buffer.Length)
                    $prefix=-join $buffer[0..($count-1)]
                    $row.session_matched=$prefix -match ('(?m)^NR_FG_LIFECYCLE v=1 session='+$session+' pid=[0-9]+ ')
                    if($row.session_matched){$row.status='PRESENT_SESSION_MATCHED'}
                } finally {$reader.Dispose()}
            }
        } catch {$row.status='UNREADABLE';$row.error=$_.Exception.Message}
    }
    return [pscustomobject]$row
}
function ReadCaptureJournals {
    $names=@('FG-LIFECYCLE.log','DRED.log')
    # Each inherited game process may own its own journal. Only bounded known
    # filenames are inspected; unrelated capture files are not opened.
    $extra=@(Get-ChildItem -LiteralPath $run -File | Where-Object {$_.Name -match '^(FG-LIFECYCLE|DRED)-[0-9]+\.log$'} | Select-Object -First 128)
    $record.journal_scan_limited=($extra.Count -eq 128)
    foreach($item in $extra){$names+=$item.Name}
    foreach($name in $names){ReadCaptureJournal $name}
}

if(-not $GameExecutable){
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=New-Object Windows.Forms.OpenFileDialog
    try {
        $dialog.Title='Choose game executable';$dialog.Filter='Executable (*.exe)|*.exe';$dialog.CheckFileExists=$true
        if($dialog.ShowDialog() -ne 'OK'){throw 'Capture cancelled. No game was launched.'}
        $GameExecutable=$dialog.FileName
    } finally {$dialog.Dispose()}
}
$GameExecutable=ResolveCaptureExecutable $GameExecutable
$target=$GameExecutable;$arguments=$GameArguments;$route='Direct'
if($LauncherExecutable){
    if($GameArguments){throw 'For a store client, use its normal game launch controls or -LauncherArguments.'}
    $target=ResolveCaptureExecutable $LauncherExecutable;$arguments=$LauncherArguments;$route='Launcher'
} elseif($LauncherArguments){throw '-LauncherArguments requires -LauncherExecutable.'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path ([Environment]::GetFolderPath('Desktop')) 'NeuRotic-Captures'}
$output=ResolveCapturePath $OutputDirectory
$session=[Guid]::NewGuid().ToString('N')
$run=ResolveCapturePath (Join-Path $output ('DRED-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$session))
# Journals use CREATE_NEW. Every run needs a new, writable directory.
if($run.Length -gt 220){throw 'Choose a shorter capture output path.'}
$null=[IO.Directory]::CreateDirectory($output)
if(Test-Path -LiteralPath $run){throw 'Capture directory already exists. Retry for a new session.'}
$null=[IO.Directory]::CreateDirectory($run)
$receiptPath=Join-Path $run 'launch.json'
$record=[ordered]@{
    schema_version=1;kind='neurotic-dred-launch';session=$session
    game_executable=$GameExecutable;launch_executable=$target;launch_route=$route
    working_directory=(Split-Path -Parent $target);capture_directory=$run
    started_utc=[DateTime]::UtcNow.ToString('o');finished_utc=$null
    process_id=$null;process_status='NOT_STARTED';exit_code=$null;error=$null
    environment_scope='child-only';game_environment_inheritance='UNVERIFIED'
    capture_status='NOT_CHECKED';journals=@();journal_scan_limited=$false;finish_reason='not-finished'
    qualification='Launch and journal evidence only; GPU fault cause and game success require review.'
}
SaveCaptureReceipt
$process=New-Object Diagnostics.Process
$process.StartInfo=New-Object Diagnostics.ProcessStartInfo
$process.StartInfo.FileName=$target;$process.StartInfo.Arguments=$arguments
$process.StartInfo.WorkingDirectory=$record.working_directory
$process.StartInfo.UseShellExecute=$false
# Never mutate the caller's process/user/machine environment, even on failure.
$environment=@{
    NEUROTIC_FG_LIFECYCLE='1';NEUROTIC_DRED='1'
    NEUROTIC_FRAME_TRACE_SESSION=$session;NEUROTIC_FRAME_TRACE_PROFILE='frame-association'
    NEUROTIC_FRAME_TRACE_TRIGGER='nr-enable';NEUROTIC_DIAGNOSTIC_DIRECTORY=$run
}
foreach($key in $environment.Keys){$process.StartInfo.EnvironmentVariables[$key]=$environment[$key]}
Write-Host "Capture folder: $run"
Write-Host "Working directory: $($record.working_directory)"
Write-Host 'Keep this window open. Reproduce once, then exit the game.'
if($route -eq 'Launcher'){Write-Host 'Start the game in the launched client. Exit both game and client when finished.'}
try {
    if(-not $process.Start()){throw 'The operating system did not start a new process.'}
    $record.process_id=$process.Id;$record.process_status='RUNNING'
    SaveCaptureReceipt
    $process.WaitForExit()
    $record.exit_code=$process.ExitCode;$record.process_status='EXITED'
    # Executables can be bootstrap launchers even on the Direct route. Their
    # exit does not delimit the inherited game's diagnostic session.
    $record.finish_reason='awaiting-user';SaveCaptureReceipt
    if($FinishOnLaunchExit){$record.finish_reason='launch-process-exit-only'}
    else {
        Write-Host 'The launch process exited. Its game may still be running.'
        $null=Read-Host 'After the game and launcher are closed, press Enter to finish the capture'
        $record.finish_reason='user-confirmed'
    }
} catch {
    $record.error=$_.Exception.Message
    $record.process_status=$(if($record.process_id){'OBSERVATION_FAILED'}else{'LAUNCH_FAILED'})
    $record.finish_reason='observation-error'
} finally {
    $process.Dispose()
    $record.finished_utc=[DateTime]::UtcNow.ToString('o')
    $record.journals=@(ReadCaptureJournals)
    $record.capture_status='NO_DIAGNOSTIC_JOURNALS'
    if(@($record.journals | Where-Object {$_.status -notin @('ABSENT','EMPTY')}).Count){$record.capture_status='JOURNALS_UNCORRELATED'}
    if(@($record.journals | Where-Object {$_.session_matched -and $_.name -match '^FG-LIFECYCLE(?:-[0-9]+)?\.log$'}).Count){$record.capture_status='LIFECYCLE_PRESENT_DRED_ABSENT_OR_UNCORRELATED'}
    if(@($record.journals | Where-Object {$_.session_matched -and $_.name -match '^DRED(?:-[0-9]+)?\.log$'}).Count){$record.capture_status='DRED_PRESENT_PENDING_REVIEW'}
    if(@($record.journals | Where-Object {$_.session_matched}).Count){$record.game_environment_inheritance='SESSION_OBSERVED_IN_JOURNAL_GAME_PROCESS_UNVERIFIED'}
    SaveCaptureReceipt
}
Write-Host "Capture status: $($record.capture_status)"
Write-Host 'Send this capture folder with the normal game diagnostic bundle.'
Write-Host 'Missing DRED does not rule out a GPU fault. No game success is inferred.'
if($route -eq 'Launcher'){Write-Host 'Fully exit the launcher before ordinary tests to clear its inherited diagnostic settings.'}
if($record.error){throw "Launch/capture observation failed: $($record.error) Receipt: $receiptPath"}
Write-Output $receiptPath
