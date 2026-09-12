Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'MHWilds-TraceCapture.ps1')
$root='C:\OptiScaler-NR-Dev\logs\association-profile-fixture-'+[Guid]::NewGuid().ToString('N')
[IO.Directory]::CreateDirectory($root) | Out-Null
$fake=Join-Path $root 'MonsterHunterWilds.exe'
$source=@'
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
public static class ProfileFixture {
 public static void Main(string[] args) {
  string session=Environment.GetEnvironmentVariable("NEUROTIC_FRAME_TRACE_SESSION");
  string profile=Environment.GetEnvironmentVariable("NEUROTIC_FRAME_TRACE_PROFILE");
  if(profile!="frame-association") Environment.Exit(2);
  string[] kinds={"trace-started","nr-api","nr-ledger","nr-pcl","nr-before-fg-forward"};
  using(var file=new StreamWriter("OptiScaler.log")) {
   for(int i=0;i<kinds.Length;i++) {
    if(args[0]=="missing" && kinds[i]=="nr-pcl") continue;
    file.WriteLine("NR_FRAME_TRACE v=1 session={0} pid={1} tid=1 qpc={2} frequency={3} seq={4} native=0 present=0 kind={5} classification=unknown profile={6}",session,Process.GetCurrentProcess().Id,Stopwatch.GetTimestamp(),Stopwatch.Frequency,i+1,kinds[i],args[0]=="wrong"?"full":profile);
   }
  }
  Thread.Sleep(1500);
 }
}
'@
Add-Type -TypeDefinition $source -OutputAssembly $fake -OutputType ConsoleApplication
foreach($name in @('dxgi.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini','nvngx_dlssnr.dll')) { [IO.File]::WriteAllText((Join-Path $root $name),'fixture') }
$hash=(Get-FileHash -LiteralPath (Join-Path $root 'dxgi.dll')).Hash
$build=Join-Path $root 'BUILD-MANIFEST.json'
@{commit='fixture';artifacts=@(@{name='OptiScaler.dll';sha256=$hash},@{name='nvngx.dll_dlssnr.dll';sha256=$hash})} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $build
$priorSteam=$env:SteamAppId;$priorGame=$env:SteamGameId
try {
 $env:SteamAppId='2246340';$env:SteamGameId='2246340'
 foreach($mode in @('good','missing','wrong')) {
  $evidence=Join-Path $root $mode;$rejected=$false
  try { Invoke-MHWildsTraceCapture -GameDirectory $root -BuildManifest $build -EvidenceRoot $evidence -GameCommand @($fake,$mode) -RequireAssociationProfile }
  catch { if($mode -ne 'wrong'){throw};$rejected=$true }
  Get-Process MonsterHunterWilds -ErrorAction SilentlyContinue | Where-Object Path -eq $fake | ForEach-Object { $_.WaitForExit() }
  $record=Get-Content -LiteralPath (Get-ChildItem -LiteralPath $evidence -Recurse -Filter SESSION.json).FullName -Raw | ConvertFrom-Json
  if($mode -eq 'wrong') { if(!$rejected -or $record.trace_verified){throw 'Wrong profile accepted'} }
  elseif(!$record.trace_verified -or !$record.association_profile_requested){throw 'Profile not verified'}
  elseif($mode -eq 'good' -and $record.association_missing_kinds.Count){throw 'Complete coverage missing'}
  elseif($mode -eq 'missing' -and ($record.association_missing_kinds -join ',') -ne 'nr-pcl'){throw 'Missing PCL hidden'}
 }
} finally {$env:SteamAppId=$priorSteam;$env:SteamGameId=$priorGame}
Write-Host ('PASS: explicit profile child environment, wrong-profile refusal, complete/missing PCL coverage. Evidence: '+$root)
