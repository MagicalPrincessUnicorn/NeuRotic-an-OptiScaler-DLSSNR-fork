param([Parameter(Mandatory=$true)][string]$PackageRoot,
      [Parameter(Mandatory=$true)][string]$FixtureRoot,
      [Parameter(Mandatory=$true)][string]$SeedTrace)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $FixtureRoot){throw 'Fresh fixture required.'}
$steamRoot=Join-Path $FixtureRoot 'Fixture Steam'
$game=Join-Path $steamRoot 'steamapps\common\Fixture Game\bin64'
New-Item -ItemType Directory -Path $game | Out-Null
$exe=Join-Path $game 'CandidateSteamGame.exe'; $steam=Join-Path $steamRoot 'FixtureSteamClient.exe'
$gameSource=@'
using System; using System.IO; using System.Diagnostics; using System.Threading;
public class CandidateSteamGame {
 public static int Main() {
  string dir=Environment.GetEnvironmentVariable("NEUROTIC_DIAGNOSTIC_DIRECTORY");
  string session=Environment.GetEnvironmentVariable("NEUROTIC_FRAME_TRACE_SESSION");
  string mode=Environment.GetEnvironmentVariable("NEUROTIC_CANDIDATE_OBSERVER");
  string home=AppDomain.CurrentDomain.BaseDirectory;
  File.WriteAllText(Path.Combine(home,"OptiScaler.log"),"NR_FRAME_TRACE v=1 session="+session+" pid="+Process.GetCurrentProcess().Id+" tid=1 qpc=1 frequency=1 seq=1 native=0 present=1 kind=original-present-enter classification=unknown fixture=true\r\n");
  File.WriteAllText(Path.Combine(home,"child-mode.txt"),mode);
  if(mode=="1") {
   string dest=Path.Combine(dir,"candidate-"+session); Directory.CreateDirectory(dest);
   string trace=File.ReadAllText(Path.Combine(home,"seed.jsonl")).Replace("dddddddddddddddddddddddddddddddd",session);
   if(File.Exists(Path.Combine(home,"omit-footer"))) trace=trace.Substring(0,trace.LastIndexOf("{\"type\":\"footer\""));
   File.WriteAllText(Path.Combine(dest,"trace.jsonl"),trace,new System.Text.UTF8Encoding(false));
  }
  Thread.Sleep(2400); return 0;
 }
}
'@
$steamSource=@'
using System; using System.IO; using System.Diagnostics; using System.Threading;
public class FakeSteam {
 public static void Main(string[] args) {
  string home=AppDomain.CurrentDomain.BaseDirectory;
  File.WriteAllText(Path.Combine(home,"launch-args.txt"),string.Join(" ",args));
  string manifest=Path.Combine(home,"steamapps","appmanifest_12345.acf");
  string state=File.ReadAllText(manifest);
  state=System.Text.RegularExpressions.Regex.Replace(state,"\"LastPlayed\"\\s*\"[0-9]+\"","\"LastPlayed\" \""+DateTime.UtcNow.Ticks+"\"");
  if(File.Exists(Path.Combine(home,"change-build"))) state=state.Replace("\"buildid\" \"99\"","\"buildid\" \"100\"").Replace("\"manifest\" \"999\"","\"manifest\" \"1000\"");
  File.WriteAllText(manifest,state);
  if(File.Exists(Path.Combine(home,"hold-client"))) {Thread.Sleep(2200); return;}
  Thread.Sleep(350);
  if(File.Exists(Path.Combine(home,"no-game"))) return;
  var start=new ProcessStartInfo(); start.FileName=File.ReadAllText(Path.Combine(home,"game-path.txt"));
  start.UseShellExecute=false; start.WorkingDirectory=Path.GetDirectoryName(start.FileName);
  if(File.Exists(Path.Combine(home,"drop-env"))) start.EnvironmentVariables.Remove("NEUROTIC_FRAME_TRACE_SESSION");
  Process.Start(start); // Exit immediately; launcher must wait for the GAME, not this client.
 }
}
'@
Add-Type -TypeDefinition $gameSource -OutputAssembly $exe -OutputType ConsoleApplication
Add-Type -TypeDefinition $steamSource -OutputAssembly $steam -OutputType ConsoleApplication
[IO.File]::WriteAllText((Join-Path $steamRoot 'game-path.txt'),$exe)
[IO.File]::WriteAllText((Join-Path $steamRoot 'steamapps\appmanifest_12345.acf'),'"AppState" { "appid" "12345" "installdir" "Fixture Game" "buildid" "99" "LastPlayed" "0" "InstalledDepots" { "456" { "manifest" "999" } } }')
Copy-Item -LiteralPath $SeedTrace -Destination (Join-Path $game 'seed.jsonl')
foreach($row in @(@('payload\OptiScaler.dll','dxgi.dll'),@('payload\OptiScaler.ini','OptiScaler.ini'),
 @('payload\nvngx.dll_dlssnr.dll','nvngx.dll_dlssnr.dll'),@('payload\OptiScaler\amd_fidelityfx_dx12.dll','OptiScaler\amd_fidelityfx_dx12.dll'))){
 $dest=Join-Path $game $row[1]; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
 Copy-Item -LiteralPath (Join-Path $PackageRoot $row[0]) -Destination $dest
}
$launcher=Join-Path $PackageRoot 'support\Start-CandidateCapture.ps1'
function Run([string]$mode){
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher -Mode $mode -GameExecutable $exe -SteamExecutable $steam -MinimumRunSeconds 1 -StartupTimeoutSeconds 5 | Out-Host
 return $LASTEXITCODE
}
if((Run Off) -ne 0 -or (Run On) -ne 0){throw 'Steam child launch failed.'}
$id=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$pair=Join-Path $PackageRoot ('captures\pair-'+$id)
$off=Get-Content -Raw (Join-Path $pair 'off\run.json') | ConvertFrom-Json
$on=Get-Content -Raw (Join-Path $pair 'on\run.json') | ConvertFrom-Json
if($on.launch_route -ne 'Steam' -or $on.process_id -eq $on.launch_process_id){throw 'Client PID mistaken for gameplay PID.'}
if(-not $off.control_usable -or -not $on.log_session_pid_matched){throw 'Gameplay session not correlated.'}
if($on.capture_status -ne 'TRACE_FOOTER_PRESENT_PENDING_ANALYSIS'){throw 'Completed trace not distinguished.'}
if((Get-Content (Join-Path $game 'child-mode.txt')) -ne '1' -or (Get-Content (Join-Path $steamRoot 'launch-args.txt')) -ne '-applaunch 12345'){throw 'Environment or app ID was not inherited.'}
if((Run Off) -ne 0){throw 'New control failed.'}
[IO.File]::WriteAllText((Join-Path $game 'omit-footer'),'')
if((Run On) -ne 0){throw 'Incomplete trace evidence not retained.'}
$id=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$incomplete=Get-Content -Raw (Join-Path $PackageRoot ('captures\pair-'+$id+'\on\run.json')) | ConvertFrom-Json
if($incomplete.capture_status -ne 'TRACE_INCOMPLETE' -or $incomplete.pair_status -ne 'CAPTURE_INCONCLUSIVE'){throw 'Missing footer falsely accepted.'}
if((Run Off) -ne 0){throw 'Build-change control failed.'}
[IO.File]::WriteAllText((Join-Path $steamRoot 'change-build'),'')
if((Run On) -ne 0){throw 'Changed-build evidence not retained.'}
$id=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$changed=Get-Content -Raw (Join-Path $PackageRoot ('captures\pair-'+$id+'\on\run.json')) | ConvertFrom-Json
if($changed.inputs_unchanged -or $changed.pair_status -ne 'INPUTS_CHANGED'){throw 'Asset-only Steam update during launch was falsely matched.'}
Remove-Item -LiteralPath (Join-Path $steamRoot 'change-build')
[IO.File]::WriteAllText((Join-Path $steamRoot 'drop-env'),'')
if((Run Off) -eq 0){throw 'Lost environment was accepted as a control.'}
Remove-Item -LiteralPath (Join-Path $steamRoot 'drop-env')
[IO.File]::WriteAllText((Join-Path $steamRoot 'hold-client'),'')
$held=New-Object Diagnostics.Process
$held.StartInfo=New-Object Diagnostics.ProcessStartInfo
$held.StartInfo.FileName=$steam; $held.StartInfo.UseShellExecute=$false; $held.StartInfo.CreateNoWindow=$true
try {
 $held.Start() | Out-Null
 if((Run Off) -eq 0){throw 'Preexisting client was reused.'}
 $held.WaitForExit()
} finally {$held.Dispose(); Remove-Item -LiteralPath (Join-Path $steamRoot 'hold-client')}
[IO.File]::WriteAllText((Join-Path $steamRoot 'no-game'),'')
if((Run Off) -eq 0){throw 'No gameplay process was accepted.'}
Write-Output 'PASS: Steam discovery, child environment, client/game PID separation, gameplay session markers, incomplete footer refusal, in-run build/depot change refusal, lost environment refusal, preexisting client refusal and no-game timeout.'
