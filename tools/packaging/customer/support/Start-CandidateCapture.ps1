[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('Off','On')][string]$Mode,
      [string]$GameExecutable, [string]$ProxyName, [string]$GameArguments,
      [ValidateRange(1,600)][int]$MinimumRunSeconds=45,
      [ValidateRange(1,1800)][int]$StartupTimeoutSeconds=300,
      [ValidateSet('Auto','Direct','Steam')][string]$LaunchRoute='Auto',
      [string]$SteamExecutable,[string]$SteamAppId)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
$package=Split-Path -Parent $PSScriptRoot
function SafeLocal([string]$path) {
    $full=[IO.Path]::GetFullPath($path)
    if($full -notmatch '^[A-Za-z]:\\'){throw 'Use an extracted package and game on a local drive.'}
    $drive=New-Object IO.DriveInfo([IO.Path]::GetPathRoot($full))
    if($drive.DriveType -notin @([IO.DriveType]::Fixed,[IO.DriveType]::Removable)){throw 'A local fixed or removable drive is required.'}
    $walk=$full
    while($walk){
        if(Test-Path -LiteralPath $walk){
            if((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked paths are not supported: $walk"}
        }
        $walk=Split-Path -Parent $walk
    }
    return $full
}
function Hash([string]$path){return (Get-FileHash -LiteralPath (SafeLocal $path) -Algorithm SHA256).Hash.ToLowerInvariant()}
. (Join-Path $PSScriptRoot 'Candidate-Launch.ps1')
function Save([string]$path,$value){[IO.File]::WriteAllText((SafeLocal $path),($value | ConvertTo-Json -Depth 20),(New-Object Text.UTF8Encoding($false)))}
function Snapshot {
    $result=[ordered]@{}
    foreach($name in @([IO.Path]::GetFileName($GameExecutable),$ProxyName,'OptiScaler.ini','OptiScaler\amd_fidelityfx_dx12.dll','nvngx.dll_dlssnr.dll')){
        $path=SafeLocal (Join-Path $game $name)
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Required installed file missing: $name. Run this package's Setup first."}
        $result[$name]=Hash $path
    }
    $model=SafeLocal (Join-Path $game 'nvngx_dlssnr.dll')
    $result['nvngx_dlssnr.dll']=$(if(Test-Path -LiteralPath $model -PathType Leaf){Hash $model}else{'ABSENT'})
    return $result
}
function SameInputs($a,$b){
    $keys=@($a.PSObject.Properties.Name)
    if($b -is [System.Collections.IDictionary]){
        if($keys.Count -ne $b.Count){return $false}
        foreach($key in $keys){if(-not $b.Contains($key) -or $a.$key -cne $b[$key]){return $false}}
    } else {
        if($keys.Count -ne @($b.PSObject.Properties.Name).Count){return $false}
        foreach($key in $keys){if($a.$key -cne $b.$key){return $false}}
    }
    return $true
}
function CopyLog([string]$label){
    $path=SafeLocal (Join-Path $game 'OptiScaler.log')
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){return 'absent'}
    if((Get-Item -LiteralPath $path).Length -gt 32MB){return 'omitted_above_32MiB'}
    Copy-Item -LiteralPath $path -Destination (Join-Path $run ($label+'-OptiScaler.log'))
    return 'copied_not_session_attributed'
}
function ArchivePair {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $rows=@(); $paths=@()
    foreach($side in @('off','on')){
        $receipt=Get-Content -Raw -LiteralPath (SafeLocal (Join-Path $pair ($side+'\run.json'))) | ConvertFrom-Json
        if($receipt.session -notmatch '^[a-f0-9]{32}$'){throw 'Invalid archived session.'}
        foreach($name in @('run.json','startup-OptiScaler.ini','before-OptiScaler.log','after-OptiScaler.log',('candidate-'+$receipt.session+'\trace.jsonl'))){
            $relative=$side+'\'+$name; $path=SafeLocal (Join-Path $pair $relative)
            if(Test-Path -LiteralPath $path -PathType Leaf){
                $bytes=(Get-Item -LiteralPath $path).Length
                if($bytes -gt 32MB){throw 'Evidence file exceeds the review collection bound.'}
                $rows+=@([ordered]@{path=$relative;bytes=$bytes;sha256=(Hash $path)}); $paths+=@($relative)
            }
        }
    }
    Save (Join-Path $pair 'review-files.json') ([ordered]@{kind='passive-discovery-review-inventory';files=$rows})
    $stream=[IO.File]::Open((SafeLocal ($pair+'.zip')),[IO.FileMode]::CreateNew)
    $archive=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try {
        foreach($relative in @($paths)+@('review-files.json')){
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(SafeLocal (Join-Path $pair $relative)),$relative.Replace('\','/')) | Out-Null
        }
    } finally {$archive.Dispose(); $stream.Dispose()}
}
$package=SafeLocal $package
$manifest=Get-Content -Raw -LiteralPath (SafeLocal (Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json')) | ConvertFrom-Json
$product=@($manifest.files | Where-Object {$_.path -eq 'payload\OptiScaler.dll'})
if($product.Count -ne 1){throw 'Package product identity is missing.'}
$captures=SafeLocal (Join-Path $package 'captures')
New-Item -ItemType Directory -Force -Path $captures | Out-Null
$previous=$null
if($Mode -eq 'On'){
    $id=[IO.File]::ReadAllText((SafeLocal (Join-Path $captures 'latest-control.txt'))).Trim()
    if($id -notmatch '^[a-f0-9]{32}$'){throw 'Invalid control identifier.'}
    $pair=SafeLocal (Join-Path $captures ('pair-'+$id))
    $previous=Get-Content -Raw -LiteralPath (SafeLocal (Join-Path $pair 'off\run.json')) | ConvertFrom-Json
    if($previous.PSObject.Properties.Name -notcontains 'control_usable' -or -not $previous.control_usable) {throw 'A correlated, sufficiently long control run with unchanged inputs is required. Start a new control with this package.'}
    if($GameExecutable -and [IO.Path]::GetFullPath($GameExecutable) -ine $previous.game_executable){throw 'Choose the same executable as the control run.'}
    $GameExecutable=[string]$previous.game_executable
    if($ProxyName -and $ProxyName -ine $previous.proxy_name){throw 'Proxy differs from control.'}
    $ProxyName=[string]$previous.proxy_name
    if($PSBoundParameters.ContainsKey('GameArguments') -and $GameArguments -cne $previous.game_arguments){throw 'Arguments differ from control.'}
    $GameArguments=[string]$previous.game_arguments
    if($PSBoundParameters.ContainsKey('LaunchRoute') -and $LaunchRoute -ne 'Auto' -and $LaunchRoute -ne $previous.launch_route){throw 'Launch route differs from control.'}
    $LaunchRoute=[string]$previous.launch_route
    if($SteamExecutable -and [IO.Path]::GetFullPath($SteamExecutable) -ine $previous.launch_inputs.client){throw 'Steam client differs from control.'}
    if($LaunchRoute -eq 'Steam'){$SteamExecutable=[string]$previous.launch_inputs.client}
    if($PSBoundParameters.ContainsKey('MinimumRunSeconds') -and $MinimumRunSeconds -ne $previous.minimum_run_seconds){throw 'Minimum duration differs from control.'}
    $MinimumRunSeconds=[int]$previous.minimum_run_seconds
} else {
    if(-not $GameExecutable){
        Add-Type -AssemblyName System.Windows.Forms
        $dialog=New-Object Windows.Forms.OpenFileDialog
        $dialog.Title='Choose the game executable used by NeuRotic Setup'; $dialog.Filter='Game executable (*.exe)|*.exe'
        if($dialog.ShowDialog() -ne 'OK'){throw 'No game selected.'}
        $GameExecutable=$dialog.FileName; $dialog.Dispose()
    }
    $id=[Guid]::NewGuid().ToString('N'); $pair=SafeLocal (Join-Path $captures ('pair-'+$id))
}
$GameExecutable=SafeLocal $GameExecutable
if(-not (Test-Path -LiteralPath $GameExecutable -PathType Leaf) -or [IO.Path]::GetExtension($GameExecutable) -ine '.exe'){throw 'Select an existing game executable.'}
$game=Split-Path -Parent $GameExecutable
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($GameExecutable)) -ErrorAction SilentlyContinue){throw 'Close the game before starting a capture.'}
$launch=GetCandidateLaunch $GameExecutable $LaunchRoute $SteamExecutable $SteamAppId
if($previous -and -not (SameInputs $previous.launch_inputs $launch)){throw 'Steam/client launch identity changed. Start a new control.'}
if(-not $ProxyName){
    $matches=@(@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll') | Where-Object {
        $p=SafeLocal (Join-Path $game $_); (Test-Path -LiteralPath $p -PathType Leaf) -and (Hash $p) -eq $product[0].sha256.ToLowerInvariant()
    })
    if($matches.Count -ne 1){throw 'Could not identify one installed test proxy. Run Setup or supply -ProxyName.'}
    $ProxyName=$matches[0]
}
if($ProxyName -notin @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){throw 'Unsupported proxy name.'}
$inputs=Snapshot
if((Get-Item -LiteralPath (Join-Path $game 'OptiScaler.ini')).Length -gt 4MB){throw 'Settings exceed the collection bound.'}
if($inputs[$ProxyName] -ne $product[0].sha256.ToLowerInvariant()){throw 'Installed proxy does not match this test package.'}
foreach($item in @(@('OptiScaler\amd_fidelityfx_dx12.dll','payload\OptiScaler\amd_fidelityfx_dx12.dll'),@('nvngx.dll_dlssnr.dll','payload\nvngx.dll_dlssnr.dll'))){
    $row=@($manifest.files | Where-Object {$_.path -eq $item[1]})
    if($row.Count -ne 1 -or $inputs[$item[0]] -ne $row[0].sha256.ToLowerInvariant()){throw 'Installed supporting binaries differ from the package.'}
}
$traceEnvironment=[ordered]@{
    trigger=[Environment]::GetEnvironmentVariable('NEUROTIC_FRAME_TRACE_TRIGGER')
    profile=[Environment]::GetEnvironmentVariable('NEUROTIC_FRAME_TRACE_PROFILE')
    fg_lifecycle=[Environment]::GetEnvironmentVariable('NEUROTIC_FG_LIFECYCLE')
    w03_provenance=[Environment]::GetEnvironmentVariable('NEUROTIC_W03_PROVENANCE')
    contract_observation=[Environment]::GetEnvironmentVariable('NEUROTIC_CONTRACT_OBSERVATION')
    dxil_spirv_config=[Environment]::GetEnvironmentVariable('DXIL_SPIRV_CONFIG')
}
if($previous){
    if(-not (SameInputs $previous.input_sha256 $inputs)){throw 'Game, binaries or settings changed. Start a new control run.'}
    if(-not (SameInputs $previous.frame_trace_environment $traceEnvironment)){throw 'Frame-trace environment changed. Start a new control run.'}
}
$run=SafeLocal (Join-Path $pair $Mode.ToLowerInvariant())
if(Test-Path -LiteralPath $run){throw 'This run already exists. Start a new control run for another pair.'}
New-Item -ItemType Directory -Path $run | Out-Null
$session=[Guid]::NewGuid().ToString('N')
$record=[ordered]@{kind='passive-discovery-game-review';pair_id=$id;mode=$Mode;session=$session;
    game_executable=$GameExecutable;game_arguments=$GameArguments;proxy_name=$ProxyName;input_sha256=$inputs;
    launch_route=$launch.route;launch_inputs=$launch;launch_inputs_after=$null;launch_process_id=$null;minimum_run_seconds=$MinimumRunSeconds;
    steam_manifest_sha256_start=$(if($launch.manifest_path){Hash $launch.manifest_path}else{$null});steam_manifest_sha256_after=$null;
    game_started_utc=$null;game_duration_seconds=0;log_session_pid_matched=$false;control_usable=$false;
    frame_trace_environment=$traceEnvironment;started_utc=[DateTime]::UtcNow.ToString('o');finished_utc=$null;
    process_id=$null;process_status='NOT_STARTED';exit_code=$null;matching_module_observed=$false;module_observation_error=$null;
    capture_status='CAPTURE_NOT_OBSERVED';validation_status='INCONCLUSIVE';pair_status='AWAITING_OBSERVER';inputs_unchanged=$null;
    log_before=(CopyLog 'before');log_after=$null;error=$null}
Save (Join-Path $run 'run.json') $record
if($Mode -eq 'Off'){
    # Publish this unfinished attempt immediately so a failed new control
    # cannot silently fall back to a previous successful pair.
    [IO.File]::WriteAllText((SafeLocal (Join-Path $captures 'latest-control.txt')),$id)
}
Copy-Item -LiteralPath (Join-Path $game 'OptiScaler.ini') -Destination (Join-Path $run 'startup-OptiScaler.ini')
$launcher=New-Object Diagnostics.Process
$launcher.StartInfo=New-Object Diagnostics.ProcessStartInfo
$launcher.StartInfo.FileName=$launch.client
$launcher.StartInfo.Arguments=$(if($launch.route -eq 'Steam'){'-applaunch '+$launch.app_id+' '+$GameArguments}else{$GameArguments})
$launcher.StartInfo.WorkingDirectory=Split-Path -Parent $launch.client; $launcher.StartInfo.UseShellExecute=$false
$launcher.StartInfo.EnvironmentVariables['NEUROTIC_CANDIDATE_OBSERVER']=$(if($Mode -eq 'On'){'1'}else{'0'})
$launcher.StartInfo.EnvironmentVariables['NEUROTIC_FRAME_TRACE_SESSION']=$session
$launcher.StartInfo.EnvironmentVariables['NEUROTIC_DIAGNOSTIC_DIRECTORY']=$run
$process=$null
Write-Host "Starting $Mode run via $($launch.route). Repeat the same scene/settings, stay in game for at least $MinimumRunSeconds seconds, then exit normally."
Write-Host 'Observer capture covers startup only (up to 30 seconds). No FPS or buffer qualification is automatic.'
try {
    if(-not $launcher.Start()){throw 'Launch process did not start.'}
    $record.launch_process_id=$launcher.Id
    if($launch.route -eq 'Steam'){
        Write-Host 'Waiting for the selected gameplay process. Allow Steam to sign in/start the game.'
        $process=WaitCandidateGame $GameExecutable ([datetime]$record.started_utc) $StartupTimeoutSeconds
    } else {$process=$launcher}
    $record.process_id=$process.Id; $record.process_status='STARTED'
    $gameStart=$process.StartTime.ToUniversalTime(); $record.game_started_utc=$gameStart.ToString('o')
    Write-Host "Tracking gameplay PID $($process.Id). This window stays open until the game exits."
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while(-not $process.WaitForExit(250)){
        if(-not $record.matching_module_observed -and $watch.Elapsed.TotalSeconds -le 35){
            try {
                $process.Refresh()
                foreach($module in $process.Modules){
                    if($module.FileName -ieq (Join-Path $game $ProxyName) -and (Hash $module.FileName) -eq $inputs[$ProxyName]){$record.matching_module_observed=$true}
                }
            } catch {$record.module_observation_error=$_.Exception.Message}
        }
    }
    $record.process_status='EXITED'; $record.exit_code=$process.ExitCode
    $record.game_duration_seconds=([DateTime]::UtcNow-$gameStart).TotalSeconds
} catch {$record.process_status='LAUNCH_FAILED'; $record.error=$_.Exception.Message}
finally {
    if($process){$process.Dispose()}; $launcher.Dispose(); $record.finished_utc=[DateTime]::UtcNow.ToString('o')
    try {$record.inputs_unchanged=SameInputs ([pscustomobject]$inputs) (Snapshot)} catch {$record.error=$_.Exception.Message; $record.inputs_unchanged=$false}
    try {$record.log_after=CopyLog 'after'} catch {$record.log_after='copy_failed'; $record.error=$_.Exception.Message}
    if($launch.manifest_path){
        try {
            $record.steam_manifest_sha256_after=Hash $launch.manifest_path
            $record.launch_inputs_after=GetCandidateLaunch $GameExecutable 'Steam' $launch.client $launch.app_id -AllowRunningClient
            if(-not (SameInputs ([pscustomobject]$launch) $record.launch_inputs_after)){$record.inputs_unchanged=$false}
        } catch {$record.error=$_.Exception.Message; $record.inputs_unchanged=$false}
    }
    if($record.process_id){try {$record.log_session_pid_matched=CandidateSessionInLog (Join-Path $game 'OptiScaler.log') $session $record.process_id} catch {$record.error=$_.Exception.Message}}
    $trace=SafeLocal (Join-Path $run ('candidate-'+$session+'\trace.jsonl'))
    if(Test-Path -LiteralPath $trace -PathType Leaf){
        $record.capture_status=$(if($Mode -eq 'On' -and $record.game_started_utc){CandidateTraceStatus $trace $session ([datetime]$record.game_started_utc)}elseif($Mode -eq 'Off'){'UNEXPECTED_CONTROL_TRACE'}else{'TRACE_PROCESS_UNCORRELATED'})
    } elseif($Mode -eq 'Off'){$record.capture_status='CONTROL_NO_TRACE'}
    if(-not $record.inputs_unchanged){$record.pair_status='INPUTS_CHANGED'}
    elseif($Mode -eq 'On'){$record.pair_status='INPUTS_MATCHED'}
    $gameUsable=$record.process_status -eq 'EXITED' -and $record.exit_code -eq 0 -and $record.game_duration_seconds -ge $MinimumRunSeconds -and $record.log_session_pid_matched
    if($record.inputs_unchanged){
        if(-not $gameUsable){$record.pair_status='GAMEPLAY_INCONCLUSIVE'}
        elseif($Mode -eq 'On' -and $record.capture_status -ne 'TRACE_FOOTER_PRESENT_PENDING_ANALYSIS'){$record.pair_status='CAPTURE_INCONCLUSIVE'}
    }
    $record.control_usable=$Mode -eq 'Off' -and $gameUsable -and $record.inputs_unchanged -and $record.capture_status -eq 'CONTROL_NO_TRACE'
    Save (Join-Path $run 'run.json') $record
}
if($record.process_status -ne 'EXITED'){throw "Launch failed; evidence retained at $run"}
if($Mode -eq 'Off'){
    if(-not $record.control_usable){throw "Control is inconclusive: game/session not correlated, short run, nonzero exit or changed inputs. Evidence: $run"}
    [IO.File]::WriteAllText((SafeLocal (Join-Path $captures 'latest-control.txt')),$id)
    Write-Host 'Control saved. Run Start-Candidate-Observer.cmd next without changing settings.'
    if($launch.route -eq 'Steam'){Write-Host 'Fully exit Steam using Steam > Exit before the observer run.'}
} else {
    ArchivePair
    Write-Host "Review archive: $pair.zip"
    Write-Host "Capture: $($record.capture_status). Send this archive for analysis; game qualification remains pending."
    if($record.pair_status -eq 'INPUTS_CHANGED'){Write-Warning 'PAIR INVALID: inputs changed during the observer run. Evidence retained; start a fresh control for comparison.'}
    if($record.pair_status -in @('GAMEPLAY_INCONCLUSIVE','CAPTURE_INCONCLUSIVE')){Write-Warning "PAIR INCONCLUSIVE: $($record.pair_status). Send the evidence; this is not a successful capture."}
    if($launch.route -eq 'Steam'){Write-Host 'Fully exit Steam after this capture before other launches.'}
}
