param([string]$PackageRoot, [string]$FixtureRoot)
$ErrorActionPreference='Stop'
$launcherSource=Join-Path $PSScriptRoot '..\tools\packaging\customer\prepared-depth\Start-PreparedDepth-Test.ps1'
if (!(Test-Path -LiteralPath $launcherSource)) { throw 'Prepared depth test launcher is missing.' }
if (!$PackageRoot -or !$FixtureRoot) { throw 'PackageRoot and FixtureRoot are required.' }
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
$FixtureRoot=[IO.Path]::GetFullPath($FixtureRoot)
if(Test-Path -LiteralPath $FixtureRoot){throw 'Fixture must be fresh.'}
New-Item -ItemType Directory -Path $FixtureRoot|Out-Null
function Hash([string]$path){$stream=[IO.File]::OpenRead($path);$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}}
function Check([bool]$value,[string]$message){if(!$value){throw $message};Write-Output "PASS: $message"}
function Child([string]$script,[string[]]$arguments){
    $psi=[Diagnostics.ProcessStartInfo]::new('C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe')
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $values=@{};foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()){$values[$entry.Key]=$entry.Value}
    $psi.Environment.Clear();foreach($key in $values.Keys){$psi.Environment[$key]=[string]$values[$key]};$psi.Environment['PSModulePath']=''
    $psi.Environment['NEUROTIC_PREPARED_DEPTH_MIP0']='inherited'
    $psi.Environment['NEUROTIC_FRAME_TRACE_SESSION']='invalid-inherited-session'
    $psi.Environment['NEUROTIC_FRAME_TRACE_TRIGGER']='invalid-inherited-trigger'
    $psi.Environment['NEUROTIC_FRAME_TRACE_PROFILE']='frame-association'
    foreach($argument in @('-NoProfile','-ExecutionPolicy','Bypass','-File',$script)+$arguments){$psi.ArgumentList.Add($argument)}
    $p=[Diagnostics.Process]::Start($psi);$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$p.WaitForExit()
    $output=$out.Result+$err.Result;$output|Add-Content (Join-Path $FixtureRoot 'commands.log')
    if($p.ExitCode){throw "Child failed $($p.ExitCode): $output"}
}
function Batch([string]$script,[string]$exe){
    $psi=[Diagnostics.ProcessStartInfo]::new('C:\Windows\System32\cmd.exe')
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $values=@{};foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()){$values[$entry.Key]=$entry.Value}
    $psi.Environment.Clear();foreach($key in $values.Keys){$psi.Environment[$key]=[string]$values[$key]}
    $psi.Environment['NEUROTIC_PREPARED_DEPTH_MIP0']='inherited'
    $psi.Environment['NEUROTIC_FRAME_TRACE_PROFILE']='frame-association'
    $psi.Arguments='/d /s /c ""'+$script+'" "'+$exe+'" "space value" -Mode fullscreen -CheckOnly"'
    $p=[Diagnostics.Process]::Start($psi);$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync();$p.WaitForExit()
    $output=$out.Result+$err.Result;$output|Add-Content (Join-Path $FixtureRoot 'commands.log')
    if($p.ExitCode){throw "Batch failed $($p.ExitCode): $output"}
}
$game=Join-Path $FixtureRoot 'game';New-Item -ItemType Directory $game|Out-Null
$helper=Join-Path $PSScriptRoot '..\builds\prepared-depth\package\helper\FixtureGame.exe'
$exe=Join-Path $game 'FixtureGame.exe';Copy-Item $helper $exe
$original=@{'version.dll'='old proxy';'nvngx.dll_dlssnr.dll'='old forwarder';'nvngx_dlssnr.dll'='private model';'OptiScaler.ini'="[User]`r`nKeep=exact`r`n";'unrelated.txt'='user data'}
foreach($name in $original.Keys){[IO.File]::WriteAllText((Join-Path $game $name),$original[$name])}
$oldHashes=@{};foreach($name in $original.Keys){$oldHashes[$name]=Hash (Join-Path $game $name)}
$engine=Join-Path $PackageRoot 'support\NeuRotic-Setup-Engine.ps1'
Child $engine @('-GameExecutable',$exe,'-ProxyName','version.dll','-ExistingProxyAction','Replace')
Check ((Hash (Join-Path $game 'OptiScaler.ini')) -eq $oldHashes['OptiScaler.ini']) 'existing settings preserved'
Check ((Hash (Join-Path $game 'nvngx_dlssnr.dll')) -eq $oldHashes['nvngx_dlssnr.dll']) 'private model preserved'
$launcher=Join-Path $PackageRoot 'support\Start-PreparedDepth-Test.ps1'
Check ((Hash $launcher) -eq (Hash $launcherSource)) 'packaged launcher is exact source'
Child $launcher @('CheckBaseline',$exe,'-Mode','fullscreen','-CheckOnly')
Check (!(Test-Path (Join-Path $PackageRoot 'reviews'))) 'check-only does not launch or create capture'
foreach($mode in @('Baseline','Prepared')){
    if($mode -eq 'Baseline'){
        $oversize=[IO.File]::Create((Join-Path $game 'OptiScaler.log'))
        try{$oversize.SetLength(512MB+1)}finally{$oversize.Dispose()}
    }
    Child $launcher @($mode,$exe,'space value','embedded"quote','trailing\','-Mode','fullscreen','-CheckOnly')
    $environment=Get-Content (Join-Path $game 'child-environment.txt') -Raw
    Check (($environment.Contains('NEUROTIC_PREPARED_DEPTH_MIP0=1')) -eq ($mode -eq 'Prepared')) "$mode sets the correct child policy"
    if($mode -eq 'Baseline'){Check ($environment.Contains('NEUROTIC_PREPARED_DEPTH_MIP0=0')) 'Baseline overrides any saved depth compatibility selection'}
    Check ($environment.Contains('NEUROTIC_PREPARED_DEPTH_DIAGNOSTICS=1')) "$mode enables descriptor diagnostics independently of the experiment"
    Check ($environment -match 'NEUROTIC_FRAME_TRACE_SESSION=[0-9a-f]{32}') "$mode passes a valid session"
    Check ($environment.Contains('NEUROTIC_FRAME_TRACE_TRIGGER=nr-enable') -and !$environment.Contains('NEUROTIC_FRAME_TRACE_PROFILE=')) "$mode uses full deferred tracing"
    foreach($pair in @(@(1,'space value'),@(2,'embedded"quote'),@(3,'trailing\'),@(4,'-Mode'),@(5,'fullscreen'),@(6,'-CheckOnly'))){
        Check ((Get-Content (Join-Path $game ('child-argument-'+$pair[0]+'.txt')) -Raw) -eq $pair[1]) "$mode preserves argument $($pair[0])"
    }
    if($mode -eq 'Baseline'){
        Remove-Item -LiteralPath (Join-Path $game 'OptiScaler.log')
        $saved=Get-ChildItem (Join-Path $PackageRoot 'reviews') -Filter SESSION.json -Recurse|Select-Object -First 1
        $savedRecord=Get-Content $saved.FullName -Raw|ConvertFrom-Json
        Check ($savedRecord.prelaunch_retention -eq 'SkippedOversize' -and $savedRecord.retention_error -match 'size bound') 'oversized logs are bounded before and after launch with errors retained'
        Check (@(Get-ChildItem (Join-Path $PackageRoot 'reviews') -Filter '*OptiScaler.log' -Recurse).Count -eq 0) 'oversized log bytes were never copied'
    }
}
foreach($mode in @('Baseline','PreparedDepth')){
    Batch (Join-Path $PackageRoot ('Launch-'+$mode+'.cmd')) $exe
    $environment=Get-Content (Join-Path $game 'child-environment.txt') -Raw
    Check (($environment.Contains('NEUROTIC_PREPARED_DEPTH_MIP0=1')) -eq ($mode -eq 'PreparedDepth')) "$mode CMD wrapper passes policy"
    Check ((Get-Content (Join-Path $game 'child-argument-1.txt') -Raw) -eq 'space value') "$mode CMD wrapper preserves spaced argument"
    Check ((Get-Content (Join-Path $game 'child-argument-2.txt') -Raw) -eq '-Mode' -and
        (Get-Content (Join-Path $game 'child-argument-4.txt') -Raw) -eq '-CheckOnly') "$mode CMD wrapper preserves named game switches"
}
$sessions=@(Get-ChildItem (Join-Path $PackageRoot 'reviews') -Filter SESSION.json -Recurse|ForEach-Object {Get-Content $_.FullName -Raw|ConvertFrom-Json})
Check ($sessions.Count -eq 4 -and @($sessions|Where-Object {$_.runtime_result -ne 'Inconclusive' -or $_.preparation -ne 'NotExercised'}).Count -eq 0) 'environment-only fixtures cannot claim feature or model success'
Check (@($sessions|Where-Object {$_.timing.qpc_frequency -le 0 -or $_.timing.ended_qpc -lt $_.timing.started_qpc}).Count -eq 0) 'reader clock manifest recorded'
Child $launcher @('Prepared',$exe,'--emit-depth-eligibility')
$diagnosticSessions=@(Get-ChildItem (Join-Path $PackageRoot 'reviews') -Filter SESSION.json -Recurse|ForEach-Object {Get-Content $_.FullName -Raw|ConvertFrom-Json}|Where-Object {$_.depth_eligibility_observations.Count -gt 0})
Check ($diagnosticSessions.Count -eq 1) 'descriptor log is retained for its matching session'
$diagnostic=$diagnosticSessions[0]
Check ($diagnostic.depth_eligibility_observations.Count -eq 1 -and $diagnostic.depth_eligibility_reasons.'single-mip' -eq 1) 'foreign session/process, duplicate and out-of-bound observations are excluded'
Check ($diagnostic.depth_eligibility_observations[0].mips -eq 1 -and $diagnostic.depth_eligibility_observations[0].format -eq 19 -and $diagnostic.depth_eligibility_observations[0].enabled -eq 'true') 'one-mip typeless depth and effective opt-in survive collection'
Check ($diagnostic.runtime_result -eq 'Inconclusive' -and $diagnostic.preparation -eq 'NotExercised') 'descriptor-only evidence never claims a copy or qualification'
Check ($diagnostic.trace_budget_exhausted) 'descriptor diagnostics survive the exhausted trace budget'
Child $engine @('-Uninstall','-GameExecutable',$exe,'-UninstallMode','Full','-ConfirmUninstall')
foreach($name in $original.Keys){Check ((Hash (Join-Path $game $name)) -eq $oldHashes[$name]) "restored original $name"}
Write-Output 'PASS: reversible occupied install, launcher environment/arguments, truthful capture and exact restoration; only environment/argv fixtures launched.'
