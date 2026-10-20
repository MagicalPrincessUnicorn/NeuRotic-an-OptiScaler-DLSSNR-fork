# No named script parameters: all remaining game switches must stay literal argv.
# The first argument is Baseline/Prepared (or CheckBaseline/CheckPrepared).
# Nothing changes the user's INI, model, or system environment.
$ErrorActionPreference='Stop'
$launchArgs=@($args|ForEach-Object {[string]$_})
if($launchArgs.Count -lt 2 -or $launchArgs[0] -cnotin @('Baseline','Prepared','CheckBaseline','CheckPrepared')){
    throw 'Use the CMD launcher in Steam Launch Options followed by %command%, or pass the complete game EXE command. See READ ME.txt.'
}
$CheckOnly=$launchArgs[0].StartsWith('Check')
$Mode=$launchArgs[0].Replace('Check','')
$command=@($launchArgs[1..($launchArgs.Count-1)])
$gameExe=(Resolve-Path -LiteralPath $command[0]).Path
if([IO.Path]::GetExtension($gameExe) -ine '.exe'){throw 'The first argument must be the actual game EXE.'}
$game=Split-Path $gameExe -Parent
$package=Split-Path $PSScriptRoot -Parent
function Hash([string]$path){
    $stream=[IO.File]::OpenRead($path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally{$stream.Dispose();$sha.Dispose()}
}
$manifest=Get-Content (Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json') -Raw|ConvertFrom-Json
$product=@($manifest.files|Where-Object path -eq 'payload\OptiScaler.dll')
$forwarder=@($manifest.files|Where-Object path -eq 'payload\nvngx.dll_dlssnr.dll')
if($product.Count -ne 1 -or $forwarder.Count -ne 1){throw 'Package identity is incomplete.'}
$matched=@()
foreach($name in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){
    $path=Join-Path $game $name
    if((Test-Path -LiteralPath $path -PathType Leaf) -and (Hash $path) -eq $product[0].sha256){$matched+=$name}
}
if(!$matched.Count){throw 'The prepared-depth candidate is not installed beside this game EXE. Run NeuRotic-Manual-Setup.cmd first.'}
if((Hash (Join-Path $game 'nvngx.dll_dlssnr.dll')) -ne $forwarder[0].sha256){throw 'Installed NR forwarder differs from this package.'}
if($CheckOnly){Write-Output "PASS: $Mode launcher identity verified; no game launched or capture created.";return}
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($gameExe)) -ErrorAction SilentlyContinue){throw 'Close this game before starting a fresh comparison run.'}
function Quote-WindowsArgument([string]$value){
    # Same Windows argv quoting used by the existing trace launcher.
    $escaped=[regex]::Replace($value,'(\\*)"','$1$1\"')
    $escaped=[regex]::Replace($escaped,'(\\+)$','$1$1')
    return '"'+$escaped+'"'
}
$session=[Guid]::NewGuid().ToString('N')
$capture=Join-Path $package ('reviews\'+$Mode.ToLowerInvariant()+'-'+$session)
New-Item -ItemType Directory -Path $capture|Out-Null
$log=Join-Path $game 'OptiScaler.log'
$record=[ordered]@{session_id=$session;mode=$Mode;product_commit=$manifest.product_commit;
    game_executable=$gameExe;proxy_names=$matched;product_sha256=$product[0].sha256;
    forwarder_sha256=$forwarder[0].sha256;runtime_result='Inconclusive';preparation='NotExercised';
    status='prepared';timing=[ordered]@{qpc_frequency=[Diagnostics.Stopwatch]::Frequency;started_qpc=[Diagnostics.Stopwatch]::GetTimestamp();ended_qpc=$null};
    requested_environment=[ordered]@{prepared_depth_mip0=($Mode -eq 'Prepared');prepared_depth_diagnostics=$true};
    depth_eligibility_observations=@();depth_eligibility_reasons=@{};
    limitations='Trace ends at 65536 events; descriptor diagnostics have a separate 64-change bound. Descriptor candidates, wrapper results and recorded copies do not establish provider entry, GPU completion, image quality or performance.'}
function Save-Session{[IO.File]::WriteAllText((Join-Path $capture 'SESSION.json'),($record|ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))}
Save-Session
$process=$null
try{
    try{
        if(Test-Path -LiteralPath $log){
            if((Get-Item -LiteralPath $log).Length -gt 512MB){$record['prelaunch_retention']='SkippedOversize'}
            else{Copy-Item -LiteralPath $log -Destination (Join-Path $capture 'prelaunch-OptiScaler.log');$record['prelaunch_retention']='Saved'}
        }
    }catch{$record['prelaunch_retention_error']=$_.Exception.Message}
    Save-Session
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$gameExe;$start.WorkingDirectory=$game;$start.UseShellExecute=$false
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal # Explicit user-launched interactive game.
    $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_SESSION']=$session
    $start.EnvironmentVariables['NEUROTIC_FRAME_TRACE_TRIGGER']='nr-enable'
    $start.EnvironmentVariables.Remove('NEUROTIC_FRAME_TRACE_PROFILE')
    $start.EnvironmentVariables['NEUROTIC_PREPARED_DEPTH_DIAGNOSTICS']='1'
    if($Mode -eq 'Prepared'){$start.EnvironmentVariables['NEUROTIC_PREPARED_DEPTH_MIP0']='1'}
    else{$start.EnvironmentVariables['NEUROTIC_PREPARED_DEPTH_MIP0']='0'}
    $quoted=@();for($i=1;$i -lt $command.Count;$i++){$quoted+=Quote-WindowsArgument $command[$i]}
    $start.Arguments=$quoted -join ' '
    Write-Host "$Mode test. Enable Info file logging, enter gameplay with NR off, then enable Native D3D12 NR."
    Write-Host "After closing the game, capture files will be saved to: $capture"
    $process=[Diagnostics.Process]::Start($start);$record['process_id']=$process.Id;$record.status='game launched';Save-Session
    $process.WaitForExit();$record['game_exit_code']=$process.ExitCode;$record.status='game exited'
}catch{$record.status='launch/capture failed';$record['error']=$_.Exception.Message;throw}
finally{
    $record.timing.ended_qpc=[Diagnostics.Stopwatch]::GetTimestamp()
    try{
        if(Test-Path -LiteralPath $log){
            if((Get-Item -LiteralPath $log).Length -gt 512MB){throw 'Log exceeds capture size bound.'}
            Copy-Item -LiteralPath $log -Destination (Join-Path $capture 'OptiScaler.log')
            $record['log_sha256']=Hash (Join-Path $capture 'OptiScaler.log')
            $events=@{};$copies=0;$exhausted=$false
            $eligibility=New-Object 'Collections.Generic.List[object]'
            $eligibilitySeen=New-Object 'Collections.Generic.HashSet[long]'
            $reasons=@{}
            foreach($line in [IO.File]::ReadLines((Join-Path $capture 'OptiScaler.log'))){
                if(!$line.Contains('session='+$session+' ')){continue}
                $entry=[regex]::Match($line,('NR_DEPTH_ELIGIBILITY v=1 session='+$session+' pid=(\d+) qpc=(\d+) frequency=(\d+) phase=descriptor-only observation=(\d+) limit=64 budgetExhausted=(true|false) (.*)$'))
                if($entry.Success -and $record.Contains('process_id')){
                    $observation=[long]$entry.Groups[4].Value;$qpc=[long]$entry.Groups[2].Value
                    if([long]$entry.Groups[1].Value -eq $record.process_id -and
                        [long]$entry.Groups[3].Value -eq $record.timing.qpc_frequency -and
                        $qpc -ge $record.timing.started_qpc -and $qpc -le $record.timing.ended_qpc -and
                        $observation -ge 1 -and $observation -le 64 -and $eligibility.Count -lt 64 -and
                        !$eligibilitySeen.Contains($observation)){
                        $row=[ordered]@{observation=$observation;qpc=$qpc;phase='descriptor-only';budgetExhausted=$entry.Groups[5].Value}
                        $numeric=@('generation','source','width','height','mips','format','dimension','arraySize','sampleCount','sampleQuality','flags','x','y','activeWidth','activeHeight')
                        $allowed=$numeric+@('enabled','privateList','protocolObserver','decision','reason');$validTokens=$true
                        foreach($token in $entry.Groups[6].Value.Split(' ')){
                            $pair=$token.Split('=',2)
                            if($pair.Count -eq 2 -and $pair[0] -cin $allowed -and !$row.Contains($pair[0])){$row[$pair[0]]=$pair[1]}
                            else{$validTokens=$false}
                        }
                        $valid=$validTokens -and $row.enabled -cin @('true','false') -and $row.privateList -cin @('true','false') -and
                            $row.protocolObserver -cin @('true','false') -and $row.decision -cin @('direct','candidate','refused') -and $row.reason -cmatch '^[a-z-]+$'
                        foreach($key in $numeric){if(!$row.Contains($key) -or $row[$key] -cnotmatch '^\d+$'){$valid=$false}}
                        if($valid){
                            foreach($key in $numeric){$row[$key]=[uint64]$row[$key]}
                            [void]$eligibilitySeen.Add($observation);$eligibility.Add($row)
                            if(!$reasons.ContainsKey($row.reason)){$reasons[$row.reason]=0};$reasons[$row.reason]++
                        }
                    }
                }
                if($line -match 'kind=(nr-prepared-depth-[a-z-]+) '){$kind=$Matches[1];if(!$events.ContainsKey($kind)){$events[$kind]=0};$events[$kind]++}
                if($line.Contains('kind=nr-prepared-depth-decision ') -and $line.Contains('copyRecorded=true')){$copies++}
                if($line.Contains('kind=trace-ended ')){$exhausted=$true}
            }
            $record['event_counts']=$events;$record['copies_recorded']=$copies;$record['trace_budget_exhausted']=$exhausted
            $record.depth_eligibility_observations=$eligibility.ToArray();$record.depth_eligibility_reasons=$reasons
            if($copies){$record.preparation='CopyRecorded'}
        }
        foreach($name in @('OptiScaler.ini','NeuRotic-NR-admission.json')){
            $path=Join-Path $game $name;if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $capture $name)}
        }
    }catch{$record['retention_error']=$_.Exception.Message}
    Save-Session;if($process){$process.Dispose()}
    Write-Host ("Capture saved. Preparation: {0}. Runtime: Inconclusive. {1}" -f $record.preparation,$capture)
}
