[CmdletBinding()]
param([string]$InstallRoot,[switch]$NoLaunch,[switch]$NoGpuTiming)
$ErrorActionPreference='Stop'
if(-not $InstallRoot){$InstallRoot=Read-Host 'Folder containing your updated NeuRotic.Hub.exe'}
$root=[IO.Path]::GetFullPath($InstallRoot)
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'UPDATE-MANIFEST.json') -Raw | ConvertFrom-Json
$allowed=@('NeuRotic.Hub.exe','Tools/WindowWorker/NeuRotic.WindowWorker.exe','Tools/WindowWorker/nvngx.dll_dlssnr.dll')
if($manifest.schemaVersion -ne 1 -or $manifest.files.Count -ne 3){throw 'Unexpected update manifest'}
$seen=@{};$identities=@()
foreach($entry in $manifest.files){
 if($entry.path -notin $allowed -or $seen.ContainsKey($entry.path)){throw 'Unexpected or duplicate file'}
 $seen[$entry.path]=$true;$file=Join-Path $root $entry.path
 $algorithm=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($file)
 try{$hash=([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$algorithm.Dispose()}
 if($hash -ne $entry.sha256){throw "Please apply this update first: $($entry.path) differs"}
 $identities+=@{path=$entry.path;sha256=$hash}
}
if($NoLaunch){Write-Output 'PASS trace launcher validates the exact installed update without launching anything';return}
$hub=Join-Path $root 'NeuRotic.Hub.exe'
foreach($process in @(Get-Process -Name 'NeuRotic.Hub','NeuRotic.WindowWorker' -ErrorAction SilentlyContinue)){
 try{$running=$process.Path}catch{continue}
 if($running -and $running.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){
  throw 'Close this installation of NeuRotic and its worker before starting the diagnostic session.'
 }
}
$folder=Join-Path $PSScriptRoot ('Anything-Trace-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($folder) | Out-Null
$environment=@{utc=[DateTime]::UtcNow.ToString('o');files=$identities;build=$manifest.sourceParentCheckpoint;os=[Environment]::OSVersion.VersionString;gpu=@();note='Metadata only. No frame pixels, media audio, or model bytes are collected. Provider identity and dimensions appear in session/frame records.'}
try{$environment.gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate)}catch{$environment.gpuError=$_.Exception.Message}
$environment | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $folder 'environment.json') -Encoding UTF8
$previous=$env:NEUROTIC_ANYTHING_TRACE_DIR
$previousGpu=$env:NEUROTIC_ANYTHING_GPU_TIMING
try{
 $env:NEUROTIC_ANYTHING_TRACE_DIR=$folder
 if($NoGpuTiming){$env:NEUROTIC_ANYTHING_GPU_TIMING='0'}else{$env:NEUROTIC_ANYTHING_GPU_TIMING='1'}
 # This is the interactive App the user explicitly starts through Trace-Anything.cmd.
 Start-Process -FilePath $hub -WorkingDirectory $root -WindowStyle Normal | Out-Null
}finally{$env:NEUROTIC_ANYTHING_TRACE_DIR=$previous;$env:NEUROTIC_ANYTHING_GPU_TIMING=$previousGpu}
Write-Output "Diagnostics folder: $folder"
Write-Output 'Start Anything normally. Let it run for about 60 seconds, move the comparison slider, then Stop.'
Write-Output 'Copy session status and return it with this diagnostics folder. Close the App when finished.'
Write-Output 'Tracing is bounded to 32,768 records per worker. Settings, model, source resolution and audio are unchanged.'
