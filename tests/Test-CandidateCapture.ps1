param([Parameter(Mandatory=$true)][string]$PackageRoot,
      [Parameter(Mandatory=$true)][string]$FixtureRoot,
      [Parameter(Mandatory=$true)][string]$SeedTrace)
$ErrorActionPreference='Stop'
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
if(-not (Test-Path -LiteralPath (Join-Path $PackageRoot 'support\Start-CandidateCapture.ps1'))){throw 'Capture launcher is missing.'}
if(Test-Path -LiteralPath $FixtureRoot){throw 'Fresh fixture required.'}
New-Item -ItemType Directory -Path $FixtureRoot | Out-Null
$exe=Join-Path $FixtureRoot 'CandidateFixture.exe'
# This CPU-only child records its environment and emits a preverified synthetic trace.
$source=@'
using System; using System.IO; using System.Diagnostics; using System.Threading;
public class CandidateFixture {
 public static int Main() {
  if(File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"bootstrap-only"))) { Thread.Sleep(200); return 1; }
  string mode=Environment.GetEnvironmentVariable("NEUROTIC_CANDIDATE_OBSERVER");
  string session=Environment.GetEnvironmentVariable("NEUROTIC_FRAME_TRACE_SESSION");
  string dir=Environment.GetEnvironmentVariable("NEUROTIC_DIAGNOSTIC_DIRECTORY");
  File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"OptiScaler.log"),"NR_FRAME_TRACE v=1 session="+session+" pid="+Process.GetCurrentProcess().Id+" tid=1 fixture=true\r\n");
  File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"child-mode.txt"),mode);
  if(mode=="1" && File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"change-settings")))
   File.AppendAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"OptiScaler.ini"),"\r\n;changed during observer\r\n");
  if(mode=="1" && !File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"omit-trace"))) {
   string dest=Path.Combine(dir,"candidate-"+session); Directory.CreateDirectory(dest);
   string trace=File.ReadAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"seed.jsonl"));
   File.WriteAllText(Path.Combine(dest,"trace.jsonl"),trace.Replace("dddddddddddddddddddddddddddddddd",session),new System.Text.UTF8Encoding(false));
  }
  Thread.Sleep(1600); return 0;
 }
}
'@
Add-Type -TypeDefinition $source -OutputAssembly $exe -OutputType ConsoleApplication
Copy-Item -LiteralPath $SeedTrace -Destination (Join-Path $FixtureRoot 'seed.jsonl')
[IO.File]::WriteAllText((Join-Path $FixtureRoot 'nvngx_dlssnr.dll'),'private model sentinel - never collect')
$largeLog=[IO.File]::OpenWrite((Join-Path $FixtureRoot 'OptiScaler.log'))
$largeLog.SetLength(32MB+1); $largeLog.Dispose()
foreach($row in @(@('payload\OptiScaler.dll','dxgi.dll'),@('payload\OptiScaler.ini','OptiScaler.ini'),
 @('payload\nvngx.dll_dlssnr.dll','nvngx.dll_dlssnr.dll'),@('payload\OptiScaler\amd_fidelityfx_dx12.dll','OptiScaler\amd_fidelityfx_dx12.dll'))){
 $dest=Join-Path $FixtureRoot $row[1]; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
 Copy-Item -LiteralPath (Join-Path $PackageRoot $row[0]) -Destination $dest
}
$launcher=Join-Path $PackageRoot 'support\Start-CandidateCapture.ps1'
$env:NEUROTIC_CANDIDATE_OBSERVER='inherited-invalid'
function Run([string]$mode) {
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher -Mode $mode -GameExecutable $exe -ProxyName dxgi.dll -MinimumRunSeconds 1 | Out-Host
 return $LASTEXITCODE
}
if((Run Off) -ne 0){throw 'Control launch failed.'}
if((Get-Content (Join-Path $FixtureRoot 'child-mode.txt')) -ne '0'){throw 'Control inherited observer flag.'}
if($env:NEUROTIC_CANDIDATE_OBSERVER -ne 'inherited-invalid'){throw 'Parent environment changed.'}
if((Run On) -ne 0){throw 'Observer launch failed.'}
if((Get-Content (Join-Path $FixtureRoot 'child-mode.txt')) -ne '1'){throw 'Observer flag absent.'}
$pair=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$folder=Join-Path $PackageRoot ('captures\pair-'+$pair)
$off=Get-Content -Raw (Join-Path $folder 'off\run.json') | ConvertFrom-Json
$on=Get-Content -Raw (Join-Path $folder 'on\run.json') | ConvertFrom-Json
if($off.capture_status -ne 'CONTROL_NO_TRACE' -or $on.capture_status -ne 'TRACE_FOOTER_PRESENT_PENDING_ANALYSIS'){throw 'Capture classification wrong.'}
if($on.matching_module_observed -or $on.validation_status -ne 'INCONCLUSIVE'){throw 'CPU fixture falsely qualified a loaded module.'}
if($off.session -eq $on.session){throw 'Session reused.'}
if(-not (Test-Path -LiteralPath ($folder+'.zip'))){throw 'Review archive missing.'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($folder+'.zip')
try {
 $names=@($zip.Entries | ForEach-Object {$_.FullName})
 if(@($names | Where-Object {$_ -match '/nvngx_dlssnr.dll$|^off/before-OptiScaler.log$'}).Count -or $off.log_before -ne 'omitted_above_32MiB'){throw 'Private model or oversized startup log collected.'}
 if($names -notcontains 'review-files.json'){throw 'Review integrity inventory absent.'}
 $inventory=Get-Content -Raw (Join-Path $folder 'review-files.json') | ConvertFrom-Json
 foreach($row in $inventory.files){
  $entry=$zip.GetEntry($row.path.Replace('\','/'))
  if(-not $entry -or $entry.Length -ne $row.bytes){throw 'Archived member differs.'}
  $stream=$entry.Open(); $hash=[Security.Cryptography.SHA256]::Create()
  try {$actual=([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant()} finally {$hash.Dispose();$stream.Dispose()}
  if($actual -ne $row.sha256){throw 'Archived hash differs.'}
 }
} finally {$zip.Dispose()}
if((Run On) -eq 0){throw 'Observer run overwritten.'}
if((Run Off) -ne 0){throw 'Second control failed.'}
Add-Content -LiteralPath (Join-Path $FixtureRoot 'OptiScaler.ini') -Value ';changed'
if((Run On) -eq 0){throw 'Changed settings accepted.'}
if((Run Off) -ne 0){throw 'Third control failed.'}
[IO.File]::WriteAllText((Join-Path $FixtureRoot 'omit-trace'),'')
if((Run On) -ne 0){throw 'Missing capture should be collected, not crash collector.'}
$pair=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$missing=Get-Content -Raw (Join-Path $PackageRoot ('captures\pair-'+$pair+'\on\run.json')) | ConvertFrom-Json
if($missing.capture_status -ne 'CAPTURE_NOT_OBSERVED' -or $missing.validation_status -ne 'INCONCLUSIVE'){throw 'Missing capture falsely passed.'}
if((Run Off) -ne 0){throw 'Fourth control failed.'}
[IO.File]::WriteAllText((Join-Path $FixtureRoot 'change-settings'),'')
if((Run On) -ne 0){throw 'Changed observer evidence was not retained.'}
$pair=Get-Content (Join-Path $PackageRoot 'captures\latest-control.txt')
$changed=Get-Content -Raw (Join-Path $PackageRoot ('captures\pair-'+$pair+'\on\run.json')) | ConvertFrom-Json
if($changed.inputs_unchanged -or $changed.pair_status -ne 'INPUTS_CHANGED'){throw 'Settings changed during observer run were accepted as a matched pair.'}
if((Run Off) -ne 0){throw 'Diagnostics control failed.'}
$oldLifecycle=$env:NEUROTIC_FG_LIFECYCLE
try {$env:NEUROTIC_FG_LIFECYCLE=$(if($oldLifecycle -eq '1'){'0'}else{'1'}); if((Run On) -eq 0){throw 'Changed inherited diagnostics were accepted.'}}
finally {$env:NEUROTIC_FG_LIFECYCLE=$oldLifecycle}
$oldContract=$env:NEUROTIC_CONTRACT_OBSERVATION
try {
 $env:NEUROTIC_CONTRACT_OBSERVATION='shadow'
 if((Run Off) -ne 0){throw 'Contract diagnostics control failed.'}
 $env:NEUROTIC_CONTRACT_OBSERVATION='SHADOW'
 if((Run On) -eq 0){throw 'Case-sensitive diagnostic mode change was accepted.'}
} finally {$env:NEUROTIC_CONTRACT_OBSERVATION=$oldContract}
[IO.File]::WriteAllText((Join-Path $FixtureRoot 'bootstrap-only'),'')
if((Run Off) -eq 0){throw 'Sub-second nonzero bootstrap was accepted as a control.'}
if((Run On) -eq 0){throw 'Failed new control reused an older successful control.'}
Write-Output 'PASS: child environment, distinct sessions, matched inputs, no overwrite, changed settings refusal, changed observer pair invalid, missing capture, no module qualification, bounded logs, private model excluded and archive integrity.'
