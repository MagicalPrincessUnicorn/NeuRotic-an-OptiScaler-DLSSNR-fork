# Launcher-only helpers. Never install files, terminate processes or change game settings.
function GetCandidateLaunch($exe,[string]$route,[string]$client,[string]$appId,[switch]$AllowRunningClient) {
    if($route -eq 'Direct'){return [ordered]@{route='Direct';client=$exe;client_sha256=(Hash $exe);app_id=$null;manifest_path=$null;install_directory=$null;build_id=$null;depot_manifests=$null}}
    $apps=$null; $walk=Split-Path -Parent $exe
    while($walk){if((Split-Path -Leaf $walk) -ieq 'steamapps'){$apps=$walk;break};$walk=Split-Path -Parent $walk}
    if(-not $apps){
        if($route -eq 'Steam'){throw 'Choose the installed Steam game executable under steamapps\common.'}
        return GetCandidateLaunch $exe 'Direct' '' ''
    }
    $common=SafeLocal (Join-Path $apps 'common')
    if(-not $exe.StartsWith($common+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Game must be under steamapps\common.'}
    $folder=$exe.Substring($common.Length+1).Split('\')[0]; $matches=@()
    foreach($file in @(Get-ChildItem -LiteralPath (SafeLocal $apps) -Filter 'appmanifest_*.acf' -File)){
        $path=SafeLocal $file.FullName
        if($file.Length -gt 1MB){continue}
        $text=[IO.File]::ReadAllText($path)
        $install=[regex]::Match($text,'"installdir"\s*"([^"]+)"')
        $id=[regex]::Match($text,'"appid"\s*"([1-9][0-9]{0,9})"')
        if($install.Success -and $id.Success -and $install.Groups[1].Value -ieq $folder -and
           $file.Name -eq ('appmanifest_'+$id.Groups[1].Value+'.acf') -and [uint64]$id.Groups[1].Value -le [uint32]::MaxValue){
            $build=[regex]::Match($text,'"buildid"\s*"([0-9]+)"')
            if(-not $build.Success){throw 'Installed Steam build identity is missing.'}
            $depots=@([regex]::Matches($text,'"manifest"\s*"([0-9]+)"') | ForEach-Object {$_.Groups[1].Value} | Sort-Object)
            $matches+=@([ordered]@{id=$id.Groups[1].Value;path=$path;build=$build.Groups[1].Value;depots=($depots -join ',')})
        }
    }
    if($matches.Count -ne 1){throw 'Could not identify one installed Steam app manifest for this game.'}
    if($appId -and $appId -ne $matches[0].id){throw 'Steam app ID does not match the selected installed game.'}
    if(-not $client){
        $nearby=SafeLocal (Join-Path (Split-Path -Parent $apps) 'steam.exe')
        if(Test-Path -LiteralPath $nearby -PathType Leaf){$client=$nearby}
        else {
            $entry=Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamExe -ErrorAction SilentlyContinue
            if($entry){$client=[string]$entry.SteamExe}
        }
    }
    if(-not $client){throw 'Steam client not found. Supply -SteamExecutable with the local Steam executable.'}
    $client=SafeLocal $client
    if(-not (Test-Path -LiteralPath $client -PathType Leaf) -or [IO.Path]::GetExtension($client) -ine '.exe'){throw 'Choose an existing local Steam client executable.'}
    # A preexisting client receives an IPC request, not our child environment.
    if(-not $AllowRunningClient -and (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($client)) -ErrorAction SilentlyContinue)){throw 'Fully exit Steam using Steam > Exit, then run this launcher again. No process will be stopped automatically.'}
    return [ordered]@{route='Steam';client=$client;client_sha256=(Hash $client);app_id=$matches[0].id;
        manifest_path=$matches[0].path;install_directory=$folder;build_id=$matches[0].build;depot_manifests=$matches[0].depots}
}

function WaitCandidateGame([string]$exe,[datetime]$since,[int]$timeout) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while($watch.Elapsed.TotalSeconds -lt $timeout){
        $found=@()
        foreach($p in @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue)){
            try {
                if($p.Path -ieq $exe -and $p.StartTime.ToUniversalTime() -ge $since -and -not $p.HasExited -and
                   ([DateTime]::UtcNow-$p.StartTime.ToUniversalTime()).TotalSeconds -ge 1){$found+=@($p)} else {$p.Dispose()}
            } catch {$p.Dispose()}
        }
        if($found.Count -gt 1){foreach($p in $found){$p.Dispose()};throw 'Multiple matching gameplay processes; capture is ambiguous.'}
        if($found.Count -eq 1){
            # Retain a query/synchronize process handle while the child is alive;
            # a freshly queried Process without it can lose its exit code at exit.
            $tracked=[Diagnostics.Process]::GetProcessById($found[0].Id)
            $null=$tracked.Handle
            $found[0].Dispose()
            return $tracked
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'Gameplay process was not observed before the startup timeout. Evidence retained; do not treat this as a control.'
}

function CandidateSessionInLog([string]$path,[string]$session,[int]$pidValue) {
    $path=SafeLocal $path
    if(-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 32MB){return $false}
    $reader=[IO.File]::OpenText($path)
    try {while(($line=$reader.ReadLine()) -ne $null){
        if($line -match ('NR_FRAME_TRACE(?:_CONTROL)? v=1 session='+$session+' pid='+$pidValue+' ')){return $true}
    }} finally {$reader.Dispose()}
    return $false
}

function CandidateTraceStatus([string]$path,[string]$session,[datetime]$gameStart) {
    $path=SafeLocal $path
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){return 'CAPTURE_NOT_OBSERVED'}
    $info=Get-Item -LiteralPath $path
    if($info.Length -gt 32MB -or $info.CreationTimeUtc -lt $gameStart.AddMilliseconds(-100)){return 'TRACE_PROCESS_UNCORRELATED'}
    $reader=[IO.File]::OpenText($path); $first=$null; $last=$null
    try {while(($line=$reader.ReadLine()) -ne $null){
        if($line.Length -gt 8192){return 'TRACE_INCOMPLETE'}
        if(-not $first){$first=$line}; if($line.Trim()){$last=$line}
    }} finally {$reader.Dispose()}
    try {
        $header=$first | ConvertFrom-Json; $footer=$last | ConvertFrom-Json
        if($header.type -ne 'header' -or $header.session -cne $session -or $header.schema_version -ne '2.0'){return 'TRACE_PROCESS_UNCORRELATED'}
        if($footer.type -ne 'footer' -or $footer.schema_version -ne '2.0' -or $footer.complete_transport -isnot [bool] -or -not $footer.complete_transport){return 'TRACE_INCOMPLETE'}
        return 'TRACE_FOOTER_PRESENT_PENDING_ANALYSIS'
    } catch {return 'TRACE_INCOMPLETE'}
}
